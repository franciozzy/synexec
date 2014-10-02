/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_master.c
 * ------------------
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

// Header files
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "synexec_common.h"
#include "synexec_netops.h"
#include "synexec_comm.h"
#include "synexec_master_comm.h"
#include "synexec_master_slaveset.h"

// Global variables
uint32_t                session = 0;            // Session ID
int                     verbose = 0;            // Verbose level

// Print program usage
static void
usage(char *argv0){
	// Local variables
	int                     i;

	// Print usage
	for (i=0; i<MT_PROGNAME_LEN+2; i++) fprintf(stderr, "-");
	fprintf(stderr, "\n %s\n", MT_PROGNAME);
	for (i=0; i<MT_PROGNAME_LEN+2; i++) fprintf(stderr, "-");
	fprintf(stderr, "\nUsage: %s [ -hvd ] [ -i <if_name> ] [ -p <port> ] [-s <session> ] <slaves> <conf>\n", argv0);
	fprintf(stderr, "       -h             Print this help message and quit.\n");
	fprintf(stderr, "       -v             Increase verbosity (may be used multiple times).\n");
	fprintf(stderr, "       -d             Run as daemon. stdout/stderr will be redirect to a log file.\n");
	fprintf(stderr, "       -i <if_name>   Use interface <if_name> instead of default.\n");
	fprintf(stderr, "       -p <port>      Override default network port (%hu) with <port>.\n", MT_NETPORT);
	fprintf(stderr, "       -s <session>   Define session ID to <session> (uint32_t, default 0).\n");
	fprintf(stderr, "       <slaves>       Wait for this many slaves before starting.\n");
	fprintf(stderr, "       <conf>         Configuration file for this session.\n");
}

// Main
int
main(int argc, char **argv){
	// Local variables
	char                    *net_ifname = NULL;     // Interface name
	uint16_t                net_port = 0;           // Network port (udp/tcp)
	char                    daemonize = 0;          // Run as a daemon
	slaveset_t              slaveset;               // Set of slaves
	char                    *conf_fn = NULL;        // Configuration file name
	int                     conf_fd = -1;           // Configuration file descriptor
	struct stat             conf_sb;                // Configuration file stats
        char                    *conf_ptr = NULL;       // Configuration file data pointer

	int                     i = 0;                  // Temporary integer
	int                     err = 0;                // Return code

	// Initialise structs
	memset(&slaveset, 0, sizeof(slaveset));
	slaveset.slaves = -1;

	// Fetch arguments
	while ((i = getopt(argc, argv, "hvdi:p:s:")) != -1){
		switch (i){
		case 'h':
			// Print help
			usage(argv[0]);
			goto out;

		case 'v':
			// Increase verbosity
			verbose++;
			break;

		case 'd':
			// Run as daemon
			if (daemonize == 1){
				fprintf(stderr, "%s: Error, already set to run as daemon.\n", argv[0]);
				goto err;
			}
			daemonize = 1;
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
	if (argc != optind+2){
		if (argc > optind+2){
			fprintf(stderr, "%s: Error, too many arguments.\n\n", argv[0]);
		}
		usage(argv[0]);
		goto err;
	}
	if ((slaveset.slaves = atoi(argv[optind++])) <= 0){
		fprintf(stderr, "%s: Error: number of slaves need to be greater than 0.\n", argv[0]);
		goto err;
	}
	if ((conf_fn = strdup(argv[optind])) == NULL){
		perror("strdup");
		fprintf(stderr, "%s: Error copying configuration file name.\n", argv[0]);
		goto err;
	}

	// Attempt to map configuration file
	if ((conf_fd = open(conf_fn, O_RDONLY)) < 0){
		perror("open");
		fprintf(stderr, "%s: Error opening configuration file '%s' for reading.\n", argv[0], conf_fn);
		goto err;
	}
	if (fstat(conf_fd, &conf_sb) < 0){
		perror("fstat");
		fprintf(stderr, "%s: Error stat'ing configuration file '%s'.\n", argv[0], conf_fn);
		goto err;
	}
	if (!S_ISREG(conf_sb.st_mode)){
		fprintf(stderr, "%s: Configuration file '%s' must be a regular file.\n", argv[0], conf_fn);
		goto err;
	}
	if ((conf_ptr = mmap(0, conf_sb.st_size, PROT_READ, MAP_SHARED, conf_fd, 0)) == MAP_FAILED){
		perror("mmap");
		fprintf(stderr, "%s: Error mapping configuration file '%s' to memory.\n", argv[0], conf_fn);
		goto err;
	}
	if (close(conf_fd) < 0){
		perror("close");
		fprintf(stderr, "%s: Error closing configuration file '%s' after mapping.\n", argv[0], conf_fn);
		goto err;
	}
	conf_fd = -1;
	free(conf_fn);
	conf_fn = NULL;

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

	// Run as daemon, if requested
	if (daemonize){
		i = fork();
		if (i < 0){
			perror("fork");
			fprintf(stderr, "%s: Error forking to become a daemon.\n", argv[0]);
			goto err;
		}
		if (i > 0){
			fprintf(stdout, "%d\n", i);
			fflush(stdout);
			goto out;
		}
		umask(0);
		setsid();
		if (chdir("/") != 0){
			perror("chdir");
			fprintf(stderr, "%s: Unable to chdir() to \"/\".\n", argv[0]);
			goto err;
		}
		// TODO: Redirect to log
	}

	// Wait for slaves to join
	if (wait_slaves(&slaveset) != 0){
		goto err;
	}

	printf("All %d slaves have joined in. Going into configuration phase.\n", slaveset.slaves);
	fflush(stdout);

	// Configure slaves
	if (config_slaves(&slaveset, conf_ptr, conf_sb.st_size) != 0){
		goto err;
	}

	printf("All %d slaves are configured. Going into execution phase.\n", slaveset.slaves);
	fflush(stdout);

	// Execute slaves
	if (execute_slaves(&slaveset) != 0){
		goto err;
	}

	printf("Slaves executing... waiting for them to return.\n");
	fflush(stdout);
	
	// Wait for slaves to finish
	if (join_slaves(&slaveset) != 0){
		goto err;
	}

	slave_times(&slaveset);

	printf("Session finished.\n");
	fflush(stdout);

out:
	// Free local resources
	if (conf_fd >= 0){
		close(conf_fd);
		conf_fd = -1;
	}
	if (conf_ptr){
		munmap(conf_ptr, conf_sb.st_size);
		conf_ptr = NULL;
	}
	if (conf_fn){
		free(conf_fn);
		conf_fn = NULL;
	}

	// Return
	return(err);

err:
	err = 1;
	goto out;
}
