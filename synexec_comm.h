/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_comm.h
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

#ifndef SYNEXEC_COMM_H
#define SYNEXEC_COMM_H

// Header files
#include <sys/select.h>
#include "synexec_common.h"

// Definitions
#define SYNEXEC_COMM_TIMEOUT_SEC        1
#define SYNEXEC_COMM_TIMEOUT_USEC       0

// Function prototypes
int
comm_init(uint16_t _net_udpport, char *_net_ifname);

int
comm_send(int sock, char command, struct timeval *timeout, void *data, uint16_t datalen);

int
comm_recv(int sock, synexec_msg_t *net_msg, struct timeval *timeout, void **data, uint16_t *datalen);

#endif /* SYNEXEC_COMM_H */
