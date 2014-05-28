/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_slave.c
 * -----------------
 *  Copyright 2014 (c) Citrix
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

// Support 64bit targets
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS       64

// Header files
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/fs.h>
#include <pthread.h>

#include "synexec_common.h"
#include "synexec_comm.h"
#include "synexec_slave_beacon.h"
#include "synexec_slave_worker.h"

// Global variables
uint32_t                session = 0;            // Session ID
int                     verbose = 0;            // Verbose level
char                    quit = 0;               // Global quit condition

// Print program usage
static void
usage(char *argv0){
	// Local variables
	int                     i;

	// Print usage
	for (i=0; i<MT_PROGNAME_LEN+2; i++) fprintf(stderr, "-");
	fprintf(stderr, "\n %s\n", MT_PROGNAME);
	for (i=0; i<MT_PROGNAME_LEN+2; i++) fprintf(stderr, "-");
	fprintf(stderr, "\nUsage: %s [ -hv ] [ -i <if_name> ] [-s <session> ]\n", argv0);
	fprintf(stderr, "       -h             Print this help message and quit.\n");
	fprintf(stderr, "       -v             Increase verbosity (may be used multiple times).\n");
	fprintf(stderr, "       -i <if_name>   Use interface <if_name> instead of default.\n");
	fprintf(stderr, "       -p <port>      Override default network port (%hu) with <port>.\n", MT_NETPORT);
	fprintf(stderr, "       -s <session>   Define session ID to <session> (unit32_t, default 0).\n");
}

// Main
int
main(int argc, char **argv){
	// Local variables
	char                    *net_ifname = NULL;     // Interface name
	uint16_t                net_port = 0;           // Network port we operate on

	pthread_t               beacon_tid;             // Beacon pthread id
	pthread_t               worker_tid;             // Worker pthread id

	int                     i = 0;                  // Temporary integer
	int                     err = 0;                // Return code

	// Fetch arguments
	while ((i = getopt(argc, argv, "hvi:p:s:")) != -1){
		switch (i){
		case 'h':
			// Print help
			usage(argv[0]);
			goto out;

		case 'v':
			// Increase verbosity
			verbose++;
			break;

		case 'i':
			// Force interface name, if unset
			if (net_ifname != NULL){
				fprintf(stderr, "%s: Error, interface name already set to '%s'.\n", argv[0], net_ifname);
				goto err;
			}else
			if ((net_ifname = strdup(optarg)) == NULL){
				perror("strdup");
				fprintf(stderr, "%s: Error setting interface name.\n", argv[0]);
				goto err;
			}
			break;

		case 'p':
			// Set port, if unset
			if (net_port != 0){
				fprintf(stderr, "%s: Error, network port already set to '%hu'.\n", argv[0], net_port);
				goto err;
			}else
			if ((net_port = atoi(optarg)) == 0){
				fprintf(stderr, "%s: Error, network port must be greater than zero.\n", argv[0]);
				goto err;
			}
			break;

		case 's':
			// Set session ID, if unset
			if (session != 0){
				fprintf(stderr, "%s: Error, session ID already set to: %u.\n", argv[0], session);
				goto err;
			}else
			if ((session = atoi(optarg)) == 0){
				fprintf(stderr, "%s: Error, session ID must be greater than zero.\n", argv[0]);
				goto err;
			}
			break;

		default:
			// Unknown option
			fprintf(stderr, "\n");
			usage(argv[0]);
			goto err;
		}
	}
	// Check for remaining parameters
	if (argc != optind){
		if (argc > optind+1){
			fprintf(stderr, "%s: Error, too many arguments.\n\n", argv[0]);
		}
		usage(argv[0]);
		goto err;
	}

	// Set default network port if none specified
	if (net_port == 0){
		net_port = MT_NETPORT;
	}

	// Initialise comm features
	if (comm_init(net_port, net_ifname) != 0){
		goto err;
	}

	// Free local copy of net_ifname
	if (net_ifname){
		free(net_ifname);
		net_ifname = NULL;
	}

	// Launch threads
	if (pthread_create(&beacon_tid, NULL, &beacon, NULL) != 0){
		perror("pthread_create");
		fprintf(stderr, "%s: Error creating Beacon thread.\n", argv[0]);
		goto err;
	}
	if (pthread_create(&worker_tid, NULL, &worker, NULL) != 0){
		perror("pthread_create");
		fprintf(stderr, "%s: Error creating Worker thread.\n", argv[0]);
		goto err;
	}

	// Wait for threads to finish
	pthread_join(worker_tid, NULL);
	pthread_join(beacon_tid, NULL);

	// Check if quit due to error
	if (quit == 2){
		goto err;
	}

	// Bypass error section
	goto out;

err:
	err = 1;

out:
	// Release allocated resources
	if (net_ifname){
		free(net_ifname);
		net_ifname = NULL;
	}

	// Return
	return(err);
}
