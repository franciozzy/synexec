/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_netops.c
 * ------------------
 *  Copyright 2013 (c) Felipe Franciosi <felipe@paradoxo.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version only.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Read the README file for the changelog and information on how to
 * compile and use this program.
 */

// Header files
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "synexec_netops.h"
#include "synexec_common.h"

/*
 * int
 * get_ifdef(char **defif_name, struct in_addr *gw_addr);
 * ------------------------------------------------------
 *  This function will search the route table for the default route and
 *  fill '*defif_name' with the interface name associated to it. If the
 *  interface is found and 'gw_addr' is a valid pointer, it is filled
 *  with the gateway address as well. '*defif_name' should be free()d
 *  after its use.
 *
 *  Mandatory params: defif_name
 *  Optional params : gw_addr
 *
 *  NOTES:
 *   '*defif_name' will be allocated with malloc() and should be free()d.
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
get_ifdef(char **defif_name, struct in_addr *gw_addr){
	// Local variables
	char                    buf[256];       // Buffer for line reading
	char                    *bufp;          // Buffer pointer
	char                    ifname[16];     // Temporary interface name buffer
	FILE                    *routefp;       // Route table file pointer
	char                    tstr[3];        // Temporary char array
	char                    tgw_ip[15];     // Temporary gw addr as a char array
	char                    *tstrp;         // Temporary char array pointer
	int                     i, j;           // Temporary integers

	// TODO: The instructions below can be replaced by an ioctl
	// TODO: using SIOCRTMSG, but I couldn't find a way to do it,
	// TODO: so I'm just greping /proc for the default route.
	// TODO: Using ioctl will increase compatibility and look cleaner.

	// Reset defif_name
	if (defif_name == NULL){
		return(-1);
	}
	*defif_name = NULL;

	// Open route table
	if ((routefp = fopen(SYNEXEC_PROC_ROUTE, "r")) == NULL){
		perror("fopen");
		fprintf(stderr, "%s: Error opening route table (ro) at '%s'.\n", __FUNCTION__, SYNEXEC_PROC_ROUTE);
		return(-1);
	}

	// Loop searching for default route
	while(!feof(routefp)){
		// Reads a line from the route table
		bufp = buf;
		memset(bufp, 0, sizeof(buf));
		if (fgets(bufp, sizeof(buf), routefp) == NULL){
			// Error reading or eof
			break;
		}

		// Remove trailing line breaks
		i = strlen(bufp) - 1;
		if (i <= 0){
			continue;
		}
		while ((bufp[i] == '\n') || (bufp[i] == '\r')){
			bufp[i] = '\0';
			i = strlen(bufp) - 1;
			if (i <= 0){
				break;
			}
		}

		// Skip empty lines
		if (strlen(bufp) < 1){
			continue;
		}

		// Copy first field to temporary interface name buffer
		if ((bufp = strchr(bufp, '\t')) == NULL){
			// Table fields not separated by '\t'?
			fprintf(stderr, "%s: WARNING while reading route table: entries not separated by tabs.\n", __FUNCTION__);
			continue;
		}
		*bufp++ = 0;
		memcpy(ifname, buf, sizeof(ifname));

		// Test destination address
		if ((bufp = strchr(bufp, '\t')) == NULL){
			// Corrupt table?
			fprintf(stderr, "%s: WARNING while reading route table: entries not separated by tabs.\n", __FUNCTION__);
			memset(ifname, 0, sizeof(ifname));
			continue;
		}
		*bufp = 0;
		bufp = buf + strlen(buf) + 1;
		if (!strncmp((void *)bufp, "00000000", 8)){
			// Found it
			if (asprintf(defif_name, "%s", ifname) < 0){
				perror("asprintf");
				fprintf(stderr, "%s: Error allocating memory for interface name.\n", __FUNCTION__);
				defif_name = NULL;
				break;
			}

			// Get gateway address as well
			// TODO: There's gotta be a more straightforward way to do this
			if (gw_addr != NULL){
				bufp += 15;
				memset(&tgw_ip, 0, sizeof(tgw_ip));
				tstrp = tgw_ip;
				for (i=0; i<4; i++){
					memset(&tstr, 0, sizeof(tstr));
					for (j=0; j<2; j++){
						tstr[j] = *bufp++;
					}
					bufp -= 4;
					sprintf((void *)tstrp, "%ld", strtol((void *)tstr, NULL, 16));
					tstrp = tgw_ip + strlen((char *)&tgw_ip);
					if (i < 3){
						*tstrp++ = '.';
					}
				}
				inet_aton((void *)tgw_ip, gw_addr);
#if 0
// Failed attempt to do it in a more straightforward way:
				bufp += 17;
				*bufp = 0;
				bufp -= 8;
printf("get_defif: gw = %s\n", bufp); // Ok, at the right position
				gw_addr.s_addr = strtol((void *)bufp, NULL, 16);
printf("get_defif: gw = %s\n", inet_ntoa(gw_addr)); // Well, strtol just doesn't do it.
#endif

			}

			// Stop searching
			break;
		}

		// Reset temporary interface name buffer
		memset(ifname, 0, sizeof(ifname));
	}

	// Close route table
	fclose(routefp);

	// Verify if we missed it
	if (strlen(*defif_name) == 0){
		return(-1);
	}

	// Return success
	return(0);
}

/*
 * int
 * get_ifipaddr(char *if_name, struct in_addr *if_addr);
 * -----------------------------------------------------
 *  This function gets the address of interface named 'if_name' and stores it
 *  into 'if_addr'.
 *
 *  Mandatory params: if_name, if_addr
 *  Optional params :
 *
 *  Return values:
 *   -1	Error
 *    0 Success
 */
int
get_ifipaddr(char *if_name, struct in_addr *if_addr){
	// Local variables
	int                     sock = -1;      // Temporary socket
	struct ifreq            ifr;            // Structure for interface request

	int			err = 0;	// Return code

	// Validate arguments
	if (!if_name || !if_addr){
		fprintf(stderr, "%s: invalid arguments.\n", __FUNCTION__);
		goto err;
	}

	// Create temporary socket for ioctl request
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		perror("socket");
		fprintf(stderr, "%s: error creating temporary socket.\n", __FUNCTION__);
		goto err;
	}

	// Set request structure
	memset(&ifr, 0, sizeof(ifr));
	memcpy(&ifr.ifr_name, if_name, sizeof(ifr.ifr_name));

	// Execute ioctl and fetch result
	if (ioctl(sock, SIOCGIFADDR, &ifr) < 0){
		perror("ioctl");
		fprintf(stderr, "%s: error fetching interface address.\n", __FUNCTION__);
		goto err;
	}
	memcpy(if_addr, (ifr.ifr_addr.sa_data)+2, sizeof(if_addr));

	// Bypass error section
	goto out;

err:
	err = -1;

out:
	// Close socket
	if (sock >= 0){
		close(sock);
	}

	// Return
	return(err);
}

/*
 * int
 * get_ifbroad(char *if_name, struct in_addr *if_broad);
 * -----------------------------------------------------
 *  This function gets the broadcast address of interface named 'if_name' and
 *  stores it into 'if_broad'.
 *
 *  Mandatory params: if_name, if_broad
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
get_ifbroad(char *if_name, struct in_addr *if_broad){
	// Local variables
	int                     sock = -1;      // Temporary socket
	struct ifreq            ifr;            // Structure for interface request

	int                     err = 0;	// Return code

	// Validate arguments
	if (!if_name || !if_broad){
		fprintf(stderr, "%s: invalid arguments.\n", __FUNCTION__);
		goto err;
	}

	// Create temporary socket for ioctl request
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		fprintf(stderr, "%s: error creating temporary socket.\n", __FUNCTION__);
		perror("socket");
		goto err;
	}

	// Set request structure
	memset(&ifr, 0, sizeof(ifr));
	memcpy(&ifr.ifr_name, if_name, sizeof(ifr.ifr_name));

	// Execute ioctl and fetch result
	if (ioctl(sock, SIOCGIFBRDADDR, &ifr) < 0){
		fprintf(stderr, "%s: error fetching interface netmask.\n", __FUNCTION__);
		perror("ioctl");
		goto err;
	}
	memcpy(if_broad, (ifr.ifr_addr.sa_data)+2, sizeof(if_broad));

	// Bypass error section
	goto out;

err:
	err = -1;

out:
	// Close socket
	if (sock >= 0){
		close(sock);
	}

	// Return
	return(err);
}
