/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_master_slaveset.c
 * ---------------------------
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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include "synexec_master_slaveset.h"
#include "synexec_master_comm.h"
#include "synexec_common.h"

// Global variables
extern int              verbose;

/*
 * static int
 * slave_remove(slaveset_t *slaveset, slave_t *slave_aux);
 * -------------------------------------------------------
 *  This function removes 'slave_aux' from 'slaveset', if present. It will also
 *  free() all associated memory.
 *
 *  Mandatory params: slaveset, slave_aux
 *  Optional params :
 *
 *  Return values:
 *   0 'slave_aux' not present
 *   1 'slave_aux' successfully removed
 */
static int
slave_remove(slaveset_t *slaveset, slave_t *slave_aux){
	// Local variables
	int                     ret = 0;                // Return code
	slave_t                 *slave_aux_a = NULL;    // Auxiliary slave_t
	slave_t                 *slave_aux_b = NULL;    // Auxiliary slave_t

	// Check if its the first slave
	if (!memcmp(slaveset->slave, slave_aux, sizeof(*slave_aux))){
		slave_aux_a = slave_aux->next;
		free(slave_aux);
		slaveset->slave = slave_aux_a;
		ret = 1;
		goto out;
	}

	// Go through the list, aux_b is always the parent
	slave_aux_b = slaveset->slave;
	slave_aux_a = slaveset->slave->next;
	while(slave_aux_a){
		if (!memcmp(slave_aux_a, slave_aux, sizeof(*slave_aux))){
			slave_aux_b->next = slave_aux_a->next;
			free(slave_aux);
			ret = 1;
			goto out;
		}
		slave_aux_b = slave_aux_a;
		slave_aux_a = slave_aux_a->next;
	}

out:
	return(ret);
}

/*
 * int
 * slaveset_complete(slaveset_t *slaveset);
 * ----------------------------------------
 *  This function tests if all required slaves are present in 'slaveset'.
 *
 *  Mandatory params: slaveset
 *  Optional params :
 *
 *  Return values:
 *   0 slaveset is incomplete
 *   1 slaveset is complete
 */
int
slaveset_complete(slaveset_t *slaveset){
	// Local variables
	int		        slave_count = 0;        // Number of slaves in the list
	slave_t                 *slave_aux;             // Auxiliary slave_t
	slave_t                 *slave_aux_b;           // Auxiliary slave_t

	// Run through all the slaves in the set, counting
	if (verbose > 1){
		printf("%s: Validating current slaveset.\n", __FUNCTION__);
		fflush(stdout);
	}
	slave_aux = slaveset->slave;
	while(slave_aux){
		if (slave_fd_probe(slave_aux) < 0){
			// Close its socket
			if (slave_aux->slave_fd >= 0){
				(void)close(slave_aux->slave_fd);
				slave_aux->slave_fd = -1;
			}
			slave_aux_b = slave_aux->next;
//printf("Removing dead slave: '%s:%hu'\n", inet_ntoa(slave_aux->slave_addr.sin_addr), ntohs(slave_aux->slave_addr.sin_port));
			slave_remove(slaveset, slave_aux);
			slave_aux = slave_aux_b;
		}else{
			slave_count++;
//printf("Worker alive: '%s:%hu'\n", inet_ntoa(slave_aux->slave_addr.sin_addr), ntohs(slave_aux->slave_addr.sin_port));
			slave_aux = slave_aux->next;
		}
	}

	// Return list completeness
	return((slave_count == slaveset->slaves));
}

/*
 * int
 * slave_in_list(slaveset_t *slaveset, struct sockaddr_in *slave_addr);
 * --------------------------------------------------------------------
 *  This function checks if 'slave_addr' is present in 'slaveset'.
 *
 *  Mandatory params: slaveset, slave_addr
 *  Optional params :
 *
 *  Return values:
 *   1 'slave_addr' is in 'slaveset'
 *   0 'slave_addr' is not in 'slaveset'
 */
int
slave_in_list(slaveset_t *slaveset, struct sockaddr_in *slave_addr){
	// Local variables
	slave_t                 *slave_aux;

	// Run through the list returning if we found the guy
	slave_aux = slaveset->slave;
	while(slave_aux){
		if (!memcmp(&(slave_addr->sin_addr), &(slave_aux->slave_addr.sin_addr), sizeof(struct sockaddr_in))){
			// Found
			return(1);
		}
		slave_aux = slave_aux->next;
	}

	// Not found
	return(0);
}

/*
 * int
 * slave_add(slaveset_t *slaveset, struct sockaddr_in *slave_addr,
 *           int slave_sock);
 * ---------------------------------------------------------------
 *  This function adds 'slave_addr' and 'slave_sock' to 'slaveset'.
 *
 *  Mandatory params: slaveset, slave_addr, slave_sock
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Already in list
 *    1 Inserted
 */
int
slave_add(slaveset_t *slaveset, struct sockaddr_in *slave_addr, int slave_sock){
	// Local variables
	slave_t                 *slave_aux = NULL;      // Auxiliary slave_t
	int                     err = 0;                // Return code

	// Check if already in list
	if (slave_in_list(slaveset, slave_addr)){
		if (verbose > 1){
			printf("%s: Slave (%s) already in list.\n", __FUNCTION__, inet_ntoa(slave_addr->sin_addr));
			fflush(stdout);
		}
		goto out;
	}

	// Allocate new slave_t
	if ((slave_aux = (slave_t *)calloc(1, sizeof(slave_t))) == NULL){
		perror("calloc");
		fprintf(stderr, "%s: Error allocating new slave.\n", __FUNCTION__);
		goto err;
	}

	// Fill slave contents
	memcpy(&(slave_aux->slave_addr), slave_addr, sizeof(struct sockaddr_in));
	slave_aux->slave_fd = slave_sock;

	// Insert it into list
	slave_aux->next = slaveset->slave;
	slaveset->slave = slave_aux;
	err = 1;

	// Bypass error section
	goto out;

err:
	if (slave_aux){
		free(slave_aux);
	}
	if (slave_sock >= 0){
		(void)close(slave_sock);
	}
	err = -1;

out:
	return(err);
}

void
slave_times(slaveset_t *slaveset){
	slave_t *slave;

	slave = slaveset->slave;
	while (slave){
		printf("Slave %s:%hu, %ld.%ld -> %ld.%ld\n",
		       inet_ntoa(slave->slave_addr.sin_addr), ntohs(slave->slave_addr.sin_port),
		       slave->slave_time[0].tv_sec, slave->slave_time[0].tv_usec,
		       slave->slave_time[1].tv_sec, slave->slave_time[1].tv_usec);
		fflush(stdout);
		slave = slave->next;
	}
}
