/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_comm.c
 * ----------------
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "synexec_comm.h"
#include "synexec_common.h"
#include "synexec_netops.h"

// Global variables
struct in_addr          net_ifip;               // Interface main IP address
struct in_addr          net_ifbc;               // Interface broadcast IP address
uint16_t                net_port = 0;           // Network port

extern uint32_t         session;
extern int              verbose;

/*
 * int
 * comm_init(uint16_t _net_port, char *net_ifname, char force_bcast);
 * ------------------------------------------------------------------
 *  This function initialises the global structures 'net_ifip', 'net_ifbc' and
 *  'net_port'. To set the local IP address and the broadcast IP address, it
 *  gets the data related to interface 'net_ifname'. If 'net_ifname' is NULL,
 *  it uses the data related to the interface which the default route is
 *  assigned. If 'force_bcast' is set, the broadcast address is always forced
 *  to 255.255.255.255.
 *
 *  Mandatory params: _net_port
 *  Optional params : net_ifname, force_bcast
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
int
comm_init(uint16_t _net_port, char *net_ifname, char force_bcast){
	// Local variables
	int             err = 0;                // Return code

	// Initialise memory structs
	memset(&net_ifip, 0, sizeof(net_ifip));
	memset(&net_ifbc, 0, sizeof(net_ifbc));

	// Set interface name
	if (net_ifname == NULL){
		if ((get_ifdef(&net_ifname, NULL) != 0) || (!net_ifname)){
			fprintf(stderr, "Unable to find default route.\n");
			goto err;
		}
	}

	// Get other network configuration
	if (get_ifipaddr(net_ifname, &net_ifip) != 0){
		goto err;
	}
	if (force_bcast){
		err = get_ifbroad("any", &net_ifbc);
	}else{
		err = get_ifbroad(net_ifname, &net_ifbc);
	}
	if (err){
		goto err;
	}
	net_port = _net_port;

	// Debug
	if (verbose > 0){
		printf("%s: net_ifname = '%s',", __FUNCTION__, net_ifname);
		printf(   " net_ifip = '%s',", inet_ntoa(net_ifip));
		printf(   " net_ifbc = '%s',", inet_ntoa(net_ifbc));
		printf(   " net_port = '%hu'\n", net_port);
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
 * _comm_send(int sock, struct timeval *timeout, void *data, uint16_t datalen);
 * ----------------------------------------------------------------------------
 *  This function sends 'datalen' bytes from the buffer contained in 'data' to
 *  the TCP socket 'sock'. If 'timeout' is specified, it will be used.
 *  Otherwise the system default is used.
 *
 *  Mandatory params: sock, data, datalen
 *  Optional params : timeout
 *
 *  Return values:
 *   -1 Error
 *    0 Timeout
 *    n Bytes sent
 */
static int
_comm_send(int sock, struct timeval *timeout, void *data, uint16_t datalen){
	// Local variables
	fd_set                  fds;            // Select fd_set
	struct timeval          fds_timeout;    // Select timeout

	int                     err = 0;        // Return code

	// Wait for socket to become ready for writing
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	if (timeout){
		err = select(sock+1, NULL, &fds, NULL, timeout);
	}else{
		fds_timeout.tv_sec  = SYNEXEC_COMM_TIMEOUT_SEC;
		fds_timeout.tv_usec = SYNEXEC_COMM_TIMEOUT_USEC;
		err = select(sock+1, NULL, &fds, NULL, &fds_timeout);
	}
	if (err == 0){
		if (verbose > 0){
			fprintf(stdout, "%s: Select timed out to write on the socket.\n", __FUNCTION__);
			fflush(stdout);
		}
		goto out;
	}else
	if (err != 1){
		if (verbose > 0){
			perror("select");
			fprintf(stderr, "%s: Error selecting socket to write.\n", __FUNCTION__);
			fflush(stderr);
		}
		goto err;
	}

	// Send the message
	err = send(sock, data, datalen, 0);
	if (err != datalen){
		// Unable to send (whole?) message
		if (verbose > 0){
			perror("send");
			fprintf(stderr, "%s: Send returned %d. Expected %hu.\n", __FUNCTION__, err, datalen);
			fflush(stderr);
		}
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
 * comm_send(int sock, char command, struct timeval *timeout,
 *           void **data, uint16_t *datalen);
 * ----------------------------------------------------------
 *  This function sends a TCP message to 'sock'. It creates the header net_msg
 *  based on 'command', 'data' and 'datalen'. If 'data'/'datalen' are NULL,
 *  only the header is sent (e.g. as in a PROBE).
 *  Upon return, 'timeout' (when specified) will contain the value set by
 *  select() (which can be undefined in case of error).
 *
 *  Mandatory params: sock, command
 *  Optional params : timeout, data, datalen
 *
 *  NOTES:
 *  If 'timeout' is not specified, the netops default will be used.
 *  If 'data' or 'datalen' are not specified, only the header is sent.
 *
 * Return values:
 *  -1 Error
 *   0 Select timed out
 *   1 Data sent
 */
int
comm_send(int sock, char command, struct timeval *timeout, void *data, uint16_t datalen){
	// Local variables
	synexec_msg_t           net_msg;        // synexec msg
	ssize_t                 xfer_bytes;     // Transfered bytes

	int                     err = 0;        // Return code

	// Compose message
	memset(&net_msg, 0, sizeof(net_msg));
	net_msg.version = MT_SYNEXEC_VERSION;
	net_msg.session = session;
	net_msg.command = command;
	net_msg.datalen = datalen;
	net_msg_hton(&net_msg);

	// Send the message
	err = _comm_send(sock, timeout, &net_msg, sizeof(net_msg));
	if (err <= 0){
		goto err;
	}
	xfer_bytes = err;

	if (!datalen){
		goto out;
	}

	err = _comm_send(sock, timeout, data, datalen);
	if (err <= 0){
		goto err;
	}
	xfer_bytes += err;

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * static int
 * _comm_recv(int sock, struct timeval *timeout, void *data, uint16_t datalen);
 * ----------------------------------------------------------------------------
 *  This function reads data from the TCP socket 'sock'. It will read at most
 *  'datalen' bytes from the socket, storing them in 'data'. If 'timeout' is
 *  specified, it will be used. Otherwise the system default is used.
 *
 *  Mandatory params: sock, data, datalen
 *  Optional params : timeout
 *
 *  Return values:
 *   -1 Error
 *    0 Timeout
 *    n Bytes read
 */
static int
_comm_recv(int sock, struct timeval *timeout, void *data, uint16_t datalen){
	// Local variables
	fd_set                  fds;            // Select fd_set
	struct timeval          fds_timeout;    // Select timeout
	ssize_t                 xfer_bytes = 0; // Transfered bytes

	int                     i;              // Temporary integer
	int                     err = 0;        // Return code

	// Validate mandatory arguments
	if ((sock < 0) || (!data) || (!datalen)){
		goto err;
	}

	// Wait for the socket to become ready for reading
retry:
	while(datalen){
		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		if (timeout){
			err = select(sock+1, &fds, NULL, NULL, timeout);
		}else{
			fds_timeout.tv_sec  = SYNEXEC_COMM_TIMEOUT_SEC;
			fds_timeout.tv_usec = SYNEXEC_COMM_TIMEOUT_USEC;
			err = select(sock+1, &fds, NULL, NULL, &fds_timeout);
		}
		if (err == 0){
			// Select timed out
			if (verbose > 1){
				fprintf(stdout, "%s: Select timed out.\n", __FUNCTION__);
				fflush(stdout);
			}
			break;
		}else
		if (err != 1){
			// Select failed
			if (errno == EINTR)
				goto retry;
			if (verbose > 0){
				perror("select");
				fprintf(stderr, "%s: Select error while reading from socket.\n", __FUNCTION__);
				fflush(stderr);
			}
			goto err;
		}

		// Receive data
		memset(data, 0, datalen);
		i = read(sock, data+xfer_bytes, datalen);
		if (verbose > 2){
			fprintf(stdout, "%s: read(%d, %p, %hu) = %d\n", __FUNCTION__, sock, data, datalen, i);
			fflush(stdout);
		}
		if (i == 0){
			fprintf(stderr, "%s: Expected to read data but got none\n", __FUNCTION__);
			fflush(stderr);
			goto err;
		} else if (i < 0){
			perror("read");
			fprintf(stderr, "%s: Read error while reading from socket.\n", __FUNCTION__);
			fflush(stderr);
			goto err;
		}
		xfer_bytes += i;
		datalen -= i;
	}

	// Return how many bytes were read
	err = xfer_bytes;

out:
	// Return
	return(err);

err:
	err = -1;
	goto out;
}

/*
 * int
 * comm_recv(int sock, synexec_msg_t *net_msg, struct timeval *timeout,
 *           void **data, uint16_t *datalen);
 * --------------------------------------------------------------------
 *  This function reads a TCP message from 'sock'. It stores the header in
 *  'net_msg' and any further data in the area pointed to by 'data'
 *  (setting 'datalen' accordingly).
 *  Upon return, 'timeout' (when specified) will contain the value set by
 *  select() (which can be undefined in case of error).
 *
 *  Mandatory params: sock, net_msg
 *  Optional params : timeout, data, datalen
 *
 *  NOTES:
 *  If 'timeout' is not specified, the netops default will be used.
 *  If 'data' is NULL and we receive net_msg->datalen != 0, this function will
 *  still read (and then discard) the read data. '*data', on the other hand,
 *  should be initially NULL and will be allocated if needed.
 *  '*data' is allocated with malloc() and should be free()d after use.
 *  If 'datalen' is not specified, it is simply not set.
 *
 * Return values:
 *  -1 Error
 *   0 Select timed out
 *   1 Data read
 */
int
comm_recv(int sock, synexec_msg_t *net_msg, struct timeval *timeout, void **data, uint16_t *datalen){
	// Local variables
	uint16_t                xfer_bytes = 0; // Bytes actually transferred
	char                    *_data = NULL;  // Temporary data buffer
	int                     err = 0;        // Return code

	// Validate mandatory arguments
	if ((sock < 0) || (!net_msg)){
		goto err;
	}

	// Receive header
	err = _comm_recv(sock, timeout, (void *)net_msg, sizeof(*net_msg));
	if (verbose > 2){
		fprintf(stderr, "%s: _comm_recv(%d, %p, %p, %lu) = %d\n", __FUNCTION__, sock, timeout, net_msg, (unsigned long)sizeof(*net_msg), err);
		fflush(stderr);
	}
	if (err < 0){
		goto err;
	}else
	if (err == 0){
		goto out;
	}

	// Validate header
	net_msg_ntoh(net_msg);
	if ((net_msg->version != MT_SYNEXEC_VERSION) ||
	    (net_msg->session != session)){
		// Invalid reply
		if (verbose > 0){
			fprintf(stderr, "%s: Read header with invalid session id or version.\n", __FUNCTION__);
			fflush(stderr);
		}
		goto err;
	}

	// Return if no further data to read
	if (net_msg->datalen == 0){
		goto out;
	}

	// Read net_msg->datalen more bytes
	xfer_bytes = net_msg->datalen;
	if (datalen){
		*datalen = xfer_bytes;
	}
	if (data == NULL){
		data = (void **)&_data;
	}
	if ((*data = calloc(1, xfer_bytes)) == NULL){
		perror("calloc");
		fprintf(stderr, "%s: Error allocating %d bytes for reading buffer.\n", __FUNCTION__, xfer_bytes);
		fflush(stderr);
		goto err;
	}
	err = _comm_recv(sock, timeout, *data, xfer_bytes);
	if (err <= 0){
		// We cannot afford to tolerate timeouts at this stage, so error out on 0 as well
		goto err;
	}

	// At this point we must have read a header, so count it up
	err += sizeof(*net_msg);

out:
	// Return
	return(err);

err:
	err = -1;
	if (data && *data){
		free(*data);
		*data = NULL;
	}
	goto out;
}
