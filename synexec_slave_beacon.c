/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_slave_beacon.c
 * ------------------------
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
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>
#include "synexec_common.h"
#include "synexec_netops.h"
#include "synexec_slave_beacon.h"

// Global variables
struct sockaddr_in              master_addr;
pthread_mutex_t                 master_mutex;
pthread_cond_t                  master_cond;

extern struct in_addr           net_ifip;
extern struct in_addr           net_ifbc;
extern uint16_t                 net_port;

extern uint32_t                 session;
extern int                      verbose;
extern char                     quit;

/*
 * void *
 * beacon();
 * ---------
 *  This thread creates an UDP socket and binds it to the predefined broadcast
 *  address. It listens on port 'net_port' for MT_SYNEXEC_MSG_PROBE and sets
 *  the global struct 'master_addr' with the address of the sender (correcting
 *  master_addr.sin_port to match net_port, facilitating further usage of the
 *  structure.
 *
 *  Mandatory params:
 *  Optional params :
 *
 *  NOTES:
 *  This function always return NULL, setting the global quit flag on error
 *  Return values:
 *   0 This function always return NULL
 */
void *
beacon(){
	// Local variables
	int                     beacon_fd = -1;         // Beacon UDP socket
	struct sockaddr_in      beacon_addr;            // Beacon sockaddr
	socklen_t               beacon_slen = 0;        // Beacon socket len
	struct sockaddr_in      sender_addr;            // Sender sockaddr
	synexec_msg_t           net_msg;                // Synexec msg
	ssize_t                 net_len = 0;            // Bytes read

	fd_set                  fds;                    // Select fds
	struct timeval          fds_timeout;            // Select timeout

	int                     i;                      // Temporary integer

	// Initialise structs
	memset(&master_addr, 0, sizeof(master_addr));
	memset(&beacon_addr, 0, sizeof(beacon_addr));
	memset(&sender_addr, 0, sizeof(sender_addr));
	memset(&net_msg, 0, sizeof(net_msg));

	// Create UDP socket
	if ((beacon_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		perror("socket");
		fprintf(stderr, "%s: Error creating beacon UDP socket.\n", __FUNCTION__);
		goto err;
	}

	// Bind beacon UDP socket to net_ifbc
	beacon_addr.sin_family = AF_INET;
	memcpy(&beacon_addr.sin_addr, &net_ifbc, sizeof(beacon_addr.sin_addr));
	beacon_addr.sin_port = htons(net_port);
	if (bind(beacon_fd, (struct sockaddr *)&beacon_addr, sizeof(struct sockaddr)) < 0){
		perror("bind");
		fprintf(stderr, "%s: Error binding beacon UDP socket.\n", __FUNCTION__);
		goto err;
	}

	// Listen to packets
	while(!quit){
		memset(&net_msg, 0, sizeof(net_msg));
		beacon_slen = sizeof(struct sockaddr);
		FD_ZERO(&fds);
		FD_SET(beacon_fd, &fds);
		fds_timeout.tv_sec = SYNEXEC_SLAVE_BEACON_LOOPTIMEO_SEC;
		fds_timeout.tv_usec = 0;
		i = select(beacon_fd+1, &fds, NULL, NULL, &fds_timeout);
		if (i == 0){
			continue;
		}else
		if (i == -1){
			goto err;
		}

		// Receive and parse the packet
		net_len = recvfrom(beacon_fd, &net_msg, sizeof(net_msg), 0, (struct sockaddr *)&sender_addr, &beacon_slen);
		net_msg_ntoh(&net_msg);
		if (verbose > 0){
			printf("%s: UDP Received %d bytes.\n", __FUNCTION__, net_len);
			fflush(stdout);
		}
		if (net_len < 0){
			perror("recvfrom");
			fprintf(stderr, "%s: Error reading from beacon UDP socket.\n", __FUNCTION__);
			goto err;
		}else
		if (net_len != sizeof(net_msg)){
			// Disregard packets that are not compliant
			continue;
		}else
		if ((net_msg.version != MT_SYNEXEC_VERSION) ||
		    (net_msg.command != MT_SYNEXEC_MSG_PROBE) ||
		    (net_msg.session != session)){
			// Disregard commands other than probe
			continue;
		}

		if (verbose > 2){
			printf("%s: Received probe from '%s'.\n", __FUNCTION__, inet_ntoa(sender_addr.sin_addr));
			printf("%s:  net_msg.version = %u\n", __FUNCTION__, net_msg.version);
			printf("%s:  net_msg.session = %u\n", __FUNCTION__, net_msg.session);
			printf("%s:  net_msg.command = %hhu\n", __FUNCTION__, net_msg.command);
			printf("%s:  net_msg.datalen = %hu\n", __FUNCTION__, net_msg.datalen);
			if (verbose > 3){
				printf("%s: Packet had %d bytes: ", __FUNCTION__, net_len);
				for (i=0; i<net_len; i++){
					printf("%02hhX ", *(((char *)&net_msg)+i));
				}
				printf("\n");
			}
			fflush(stdout);
		}

		// Set global master IP address and port, signalling the worker thread
		pthread_mutex_lock(&master_mutex);
		if (master_addr.sin_port == 0){
			memcpy(&master_addr, &sender_addr, sizeof(master_addr));
			master_addr.sin_port = htons(net_port);
			pthread_cond_signal(&master_cond);
		}
		pthread_mutex_unlock(&master_mutex);
	}

	// Bypass error section
	goto out;

err:
	// Set global error condition
	quit = 2;

	// Unlock the worker thread
	pthread_mutex_lock(&master_mutex);
	memset(&master_addr, 0xFF, sizeof(master_addr));
	pthread_cond_signal(&master_cond);
	pthread_mutex_unlock(&master_mutex);

out:
	if (beacon_fd >= 0){
		close(beacon_fd);
	}
	return NULL;
}
