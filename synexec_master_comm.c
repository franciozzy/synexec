/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_master_comm.c
 * -----------------------
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
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#include "synexec_netops.h"
#include "synexec_comm.h"
#include "synexec_common.h"
#include "synexec_master_slaveset.h"
#include "synexec_master_comm.h"

// Global variables
extern struct in_addr   net_ifip;
extern struct in_addr   net_ifbc;
extern char *           net_ifname;
extern uint16_t         net_port;

extern uint32_t         session;
extern int              verbose;

/*
 * static int
 * comm_tcp_accept(int sock, struct timeval *timeout, slaveset_t *slaveset);
 * -------------------------------------------------------------------------
 *  This function accepts TCP connections from slaves and inserts them into
 *  'slaveset'.
 * 
 *  Mandatory params: sock, slaveset
 *  Optional params : timeout
 *
 *  NOTES:
 *  If 'timeout' is not specified, the function may wait forever.
 *
 *  Return values:
 *   -1: Error
 *    0: Timeout while waiting
 *    1: One slave added
 */
static int
comm_tcp_accept(int sock, struct timeval *timeout, slaveset_t *slaveset){
	// Local variables
	fd_set                  fds;                    // Select fd_set
	int                     slave_sock = -1;        // Socket to accept new slaves
	struct sockaddr_in      slave_addr;             // Slave's address
	socklen_t               slave_len;              // Address length
	synexec_msg_t           net_msg;                // synexec msg
	int                     err = 0;                // Return code

	// Wait until there is something to read
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	err = select(sock+1, &fds, NULL, NULL, timeout);
	if (err == -1){
		perror("select");
		fprintf(stderr, "%s: Error waiting to receive response from slaves.\n", __FUNCTION__);
		goto out;
	}else
	if (err == 0){
		goto out;
	}

	// There is a connection on the pipe
	slave_len = sizeof(slave_addr);
	if ((slave_sock = accept(sock, (struct sockaddr *)&slave_addr, &slave_len)) < 0){
		perror("accept");
		fprintf(stderr, "%s: Error accepting new connection.\n", __FUNCTION__);
		goto err;
	}
	if (verbose > 0){
		printf("%s: Accepted connection from '%s:%hu'.\n", __FUNCTION__,
			inet_ntoa(slave_addr.sin_addr), ntohs(slave_addr.sin_port));
		fflush(stdout);
	}

        // Process hello
	if (comm_recv(slave_sock, &net_msg, NULL, NULL, NULL) <= 0){
		(void)close(slave_sock);
		err = 0;
		goto out;
	}
	if (net_msg.command != MT_SYNEXEC_MSG_REPLY){
		(void)close(slave_sock);
		err = 0;
		goto out;
	}

	// Add the connection to the slaveset
	err = slave_add(slaveset, &slave_addr, slave_sock);
	if (err && (verbose > 1)){
		printf("%s: Added slave: %s:%hu (%d).\n", __FUNCTION__,
			inet_ntoa(slave_addr.sin_addr), ntohs(slave_addr.sin_port), slave_sock);
		fflush(stdout);
	}

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * static int
 * comm_udp_broadcast(int sock);
 * -----------------------------
 *  Create a UDP broadcast probe message and send it over 'sock'.
 *
 *  Mandatory params: sock
 *  Optional params :
 *
 *  Return values:
 *   -1: Error
 *    0: Success
 */
static int
comm_udp_broadcast(int sock){
	// Local variables
	fd_set                  fds;                    // Select fd_set
	struct timeval          fds_timeout;            // Select timeout;
	struct sockaddr_in      net_udpaddr;            // Sock addr for sending
	synexec_msg_t           net_msg;                // synexec msg

	int                     i;                      // Temporary integer
	int                     err = 0;                // Return code

	// Ensure socket is ready to send
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	fds_timeout.tv_sec = 1;
	fds_timeout.tv_usec = 0;
	i = select(sock+1, NULL, &fds, NULL, &fds_timeout);
	if (i != 1){
		if (i != 0){
			perror("select");
		}
		fprintf(stderr, "%s: UDP Broadcast socket not ready to transmit in time.\n", __FUNCTION__);
		goto err;
	}

	// Setup sending address
	net_udpaddr.sin_family = AF_INET;
	memcpy(&net_udpaddr.sin_addr, &net_ifbc, sizeof(net_udpaddr.sin_addr));
	net_udpaddr.sin_port = htons(net_port);

	// Setup synexec msg
	memset(&net_msg, 0, sizeof(net_msg));
	net_msg.version = MT_SYNEXEC_VERSION;
	net_msg.session = session;
	net_msg.command = MT_SYNEXEC_MSG_PROBE;
	net_msg.datalen = 0;
	net_msg_hton(&net_msg);

	// Send broadcast
	if (sendto(sock, &net_msg, sizeof(net_msg), 0, (struct sockaddr *)&net_udpaddr, sizeof(net_udpaddr)) < 0){
		perror("sendto");
		fprintf(stderr, "%s: Error sending UDP broadcast.\n", __FUNCTION__);
		goto err;
	}

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * int
 * slave_fd_probe(int sock);
 * -------------------------
 *  Probe the slave at the other end of 'sock'.
 *
 *  Mandatory params: sock
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
slave_fd_probe(int sock){
	// Local variables
	synexec_msg_t           net_msg;                // synexec msg
	int                     err = 0;                // Return code

	// Probe the slave
	if (comm_send(sock, MT_SYNEXEC_MSG_PROBE, NULL, NULL, 0) <= 0){
		goto err;
	}

	// Process the reply
	if (comm_recv(sock, &net_msg, NULL, NULL, NULL) <= 0){
		goto err;
	}
	if (net_msg.command != MT_SYNEXEC_MSG_REPLY){
		goto err;
	}

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}


/*
 * int
 * slave_probe(slave_t *slave_aux);
 * --------------------------------
 *  Probe the TCP fd for 'slave_aux'.
 *
 *  Mandatory params: slave_aux
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
slave_probe(slave_t *slave_aux){
	// Local variables
	int                     err = 0;                // Return code

	if (verbose > 0){
		printf("%s: Probing slave (%s:%hu).\n", __FUNCTION__,
			inet_ntoa(slave_aux->slave_addr.sin_addr), ntohs(slave_aux->slave_addr.sin_port));
	}

	// Probe the slave
	if (slave_fd_probe(slave_aux->slave_fd) < 0){
		if (verbose > 0){
			printf("%s: Error probing slave (%s:%hu).\n", __FUNCTION__,
				inet_ntoa(slave_aux->slave_addr.sin_addr), ntohs(slave_aux->slave_addr.sin_port));
		}
		goto err;
	} else {
		if (verbose > 0){
			printf("%s: Slave (%s:%hu) replied to probe.\n", __FUNCTION__,
				inet_ntoa(slave_aux->slave_addr.sin_addr), ntohs(slave_aux->slave_addr.sin_port));
		}
	}

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * int
 * wait_slaves(slaveset_t *slaveset);
 * ----------------------------------
 *  This function implements the main loop that sends UDP broadcasts and awaits
 *  TCP connections potential slaves. It returns when all required slaves have
 *  joined the session.
 *
 *  Mandatory params: slaveset
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
wait_slaves(slaveset_t *slaveset){
	// Local variables
	int                     net_udpfd = -1;         // UDP socket
	socklen_t               net_udplen = 0;         // UDP socket len

	int                     net_tcpfd = -1;         // TCP socket
	struct sockaddr_in      net_tcpaddr;            // TCP address

	struct timeval          fds_timeout;            // select timeout

	int                     i;                      // Temporary integer
	int                     err = 0;                // Return code

	// Setup UDP socket to broadcast
	if ((net_udpfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		perror("socket");
		fprintf(stderr, "%s: Error creating UDP socket.\n", __FUNCTION__);
		goto err;
	}
	i = 1; // SO_BROADCAST = true
	net_udplen = sizeof(net_udpfd);
	if (setsockopt(net_udpfd, SOL_SOCKET, SO_BROADCAST, &i, net_udplen) < 0){
		perror("setsockopt");
		fprintf(stderr, "%s: Error setting socket to broadcast mode.\n", __FUNCTION__);
		goto err;
	}
	if (strcmp(net_ifname, "any")){
		if (setsockopt(net_udpfd, SOL_SOCKET, SO_BINDTODEVICE, net_ifname, IFNAMSIZ-1) < 0){
			perror("SO_BINDTODEVICE");
			fprintf(stderr, "%s: Error binding UDP socket to interface\n", __FUNCTION__);
			goto err;
		}
	}

	// Setup TCP socket to accept new connections
	if ((net_tcpfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("socket");
		fprintf(stderr, "%s: Error creating TCP socket.\n", __FUNCTION__);
		goto err;
	}
	i = 1; // SO_REUSEADDR = true
	if (setsockopt(net_tcpfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&i, sizeof(int)) < 0){
		perror("setsockopt");
		fprintf(stderr, "%s: Error setting SO_REUSEADDR to TCP socket.\n", __FUNCTION__);
		goto err;
	}
	memset(&net_tcpaddr, 0, sizeof(net_tcpaddr));
	net_tcpaddr.sin_family = AF_INET;
	memcpy(&net_tcpaddr.sin_addr, &net_ifip, sizeof(net_tcpaddr.sin_addr));
	net_tcpaddr.sin_port = htons(net_port);
	if (bind(net_tcpfd, (struct sockaddr *)&net_tcpaddr, sizeof(net_tcpaddr)) < 0){
		perror("bind");
		fprintf(stderr, "%s: Error binding TCP socket.\n", __FUNCTION__);
		goto err;
	}
	listen(net_tcpfd, 128);

	// Send UDP broadcast query every second until I have my slaves up
	do {
		// Send the probe broadcast
		if (verbose > 0){
			printf("%s: Sending UDP Probe broadcast...\n", __FUNCTION__);
			fflush(stdout);
		}
		comm_udp_broadcast(net_udpfd);

		// Set time to wait for replies
		fds_timeout.tv_sec = SYNEXEC_MASTER_COMM_PROBE_WAIT;
		fds_timeout.tv_usec = 0;
		do {
			i = comm_tcp_accept(net_tcpfd, &fds_timeout, slaveset);
			slaveset->active += i;
		} while ((i > 0) && (slaveset->active < slaveset->slaves));
		if (verbose > 1){
			printf("%s: Done waiting for UDP replies.\n", __FUNCTION__);
			fflush(stdout);
		}
		if (slaveset_probe(slaveset) < 0){
			goto err;
		}
	}
	while (slaveset->slaves != slaveset->active);

out:
	// Free resources
	if (net_udpfd != -1){
		close(net_udpfd);
	}
	if (net_tcpfd != -1){
		close(net_tcpfd);
	}

	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * static int
 * config_slave(slave_t *slave, char *conf_ptr, off_t conf_len);
 * -------------------------------------------------------------
 *  This function sends the session configuration file to 'slave' and confirms
 *  that he is happy with the contents.
 *
 *  Mandatory params: slave, conf_ptr
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
static int
config_slave(slave_t *slave, char *conf_ptr, off_t conf_len){
	// Local variables
	synexec_msg_t		net_msg;		// Synexec msg
	int                     err = 0;                // Return code

	// Send configuration to slave
	if (comm_send(slave->slave_fd, MT_SYNEXEC_MSG_CONF, NULL, conf_ptr, conf_len) < 0){
		goto err;
	}
	if (verbose > 0){
		printf("%s: Configuration sent to slave (%s:%hu).\n", __FUNCTION__, inet_ntoa(slave->slave_addr.sin_addr), ntohs(slave->slave_addr.sin_port));
		fflush(stdout);
	}

	// Await reply
	if (comm_recv(slave->slave_fd, &net_msg, NULL, NULL, NULL) < 0){
		goto err;
	}
	if (net_msg.command != MT_SYNEXEC_MSG_CONF_OK){
		fprintf(stderr, "%s: Slave (%s:%hu) refused configuration file.\n", __FUNCTION__, inet_ntoa(slave->slave_addr.sin_addr), ntohs(slave->slave_addr.sin_port));
		goto err;
	}
	if (verbose > 0){
		printf("%s: Configuration OK from slave (%s:%hu).\n", __FUNCTION__, inet_ntoa(slave->slave_addr.sin_addr), ntohs(slave->slave_addr.sin_port));
		fflush(stdout);
	}

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * int
 * config_slaves(slaveset_t *slaveset, char *conf_ptr, off_t conf_len);
 * --------------------------------------------------------------------
 *  This function sends the session configuration file to all the slaves and
 *  confirms that they are happy with the contents.
 *
 *  Mandatory params: slaveset, conf_ptr
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
config_slaves(slaveset_t *slaveset, char *conf_ptr, off_t conf_len){
	// Local variables
	slave_t                 *slave;                 // Temporary slave
	int                     err = 0;                // Return code

	// Iterate through slaves
	slave = slaveset->slave;
	while(slave){
		if (config_slave(slave, conf_ptr, conf_len) != 0){
			goto err;
		}
		slave = slave->next;
	}

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * int
 * execute_slaves(slaveset_t *slaveset);
 * -------------------------------------
 *  This function sends the execution command to all the slaves, without
 *  waiting for acknowledgement, causing them to start the execution
 *  immediately.
 *
 *  Mandatory params: slaveset
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
execute_slaves(slaveset_t *slaveset){
	// Local variables
	slave_t                 *slave = NULL;          // Temporary slave
	int                     err = 0;

	// Iterate through slaves
	slave = slaveset->slave;
	while(slave){
		if (comm_send(slave->slave_fd, MT_SYNEXEC_MSG_EXEC, NULL, NULL, 0) <= 0){
			goto err;
		}
		slave = slave->next;
	}

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * int
 * join_slaves(slaveset_t *slaveset);
 * ----------------------------------
 *  This function keep track of the slaves that are executing, waiting for
 *  them to finish their work and return the time values.
 *
 *  Mandatory params: slaveset
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
join_slaves(slaveset_t *slaveset){
	// Local variables
	fd_set                  fds;                    // Select fd_set
	int                     mfd;                    // Largest fd
	synexec_msg_t           net_msg;                // Synexec msg
	char                    *data;                  // Transfer buffer

	int                     i;                      // Temporary integer
	slave_t                 *slave = NULL;          // Temporary slave
	int                     err = 0;                // Return code

	// Loop until all slaves have finished
	do {
		// Set all slaves into the fd_set
		FD_ZERO(&fds);
		mfd = -1;
		slave = slaveset->slave;
		while(slave){
			if (!memcmp(&(slave->slave_time[2]), &(slave->slave_time[1]), sizeof(slave->slave_time[2]))){
				FD_SET(slave->slave_fd, &fds);
				if (slave->slave_fd > mfd){
					mfd = slave->slave_fd;
				}
			}
			slave = slave->next;
		}

		if (mfd == -1){
			// All slaves in set have completed
			goto out;
		}

		// Check which slaves have something to say
		// TODO: This should timeout and then I need to probe the slaves
		i = select(mfd+1, &fds, NULL, NULL, NULL);
		if (i <= 0){
			perror("select");
			goto err;
		}
		slave = slaveset->slave;
		while(slave){
			if (FD_ISSET(slave->slave_fd, &fds)){
				// Read from this slave
				data = NULL;
				i = comm_recv(slave->slave_fd, &net_msg, NULL, (void**)&data, NULL);
				if (i < 0)
					goto err;
				if ((i > 0) && (net_msg.command == MT_SYNEXEC_MSG_FINISHD)){
					struct {
						int64_t tv_sec;
						int64_t tv_usec;
					} net_time[3];

					if (net_msg.datalen != sizeof(net_time)){
						fprintf(stderr, "%s: Wrong datalen for FINISHD (slave %s:%hu).\n", __FUNCTION__,
						        inet_ntoa(slave->slave_addr.sin_addr), ntohs(slave->slave_addr.sin_port));
						fflush(stderr);
						continue;
					}

					// Unmarshal data
					memcpy(net_time, data, sizeof(net_time));
					slave->slave_time[0].tv_sec = net_time[0].tv_sec; slave->slave_time[0].tv_usec = net_time[0].tv_usec;
					slave->slave_time[1].tv_sec = net_time[1].tv_sec; slave->slave_time[1].tv_usec = net_time[1].tv_usec;
					slave->slave_time[2].tv_sec = net_time[2].tv_sec; slave->slave_time[2].tv_usec = net_time[2].tv_usec;

					printf("%s: Slave (%s:%hu) completed\n", __FUNCTION__,
					        inet_ntoa(slave->slave_addr.sin_addr), ntohs(slave->slave_addr.sin_port));
					fflush(stdout);
				}
			}
			slave = slave->next;
		}

	} while(1);


out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}
