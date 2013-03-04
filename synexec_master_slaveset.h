/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_master_slaveset.h
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

#ifndef SYNEXEC_MASTER_SLAVESET_H
#define SYNEXEC_MASTER_SLAVESET_H

// Header files
#include <inttypes.h>
#include <netinet/in.h>

// Slave entry
typedef struct _slave {
	struct sockaddr_in      slave_addr;             // Slave sockaddr
	int                     slave_fd;               // TCP Socket
	struct timeval          slave_time[3];          // 0-started, 1-finished, 2-zero for ref
	struct _slave           *next;                  // Next slave in the linked list
} slave_t;

// Slave set
typedef struct {
	int32_t                 slaves;                 // Total number of slaves REQUIRED in the set
	slave_t                 *slave;                 // Pointer to the first slave
} slaveset_t;

// Related functions
int
slaveset_complete(slaveset_t *slaveset);

int
slave_in_list(slaveset_t *slaveset, struct sockaddr_in *slave_addr);

int
slave_add(slaveset_t *slaveset, struct sockaddr_in *slave_addr, int slave_sock);

void
slave_times(slaveset_t *slaveset);

#endif /* SYNEXEC_MASTER_SLAVESET_H */
