/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_common.c
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
#include <inttypes.h>
#include <netinet/in.h>
#include "synexec_common.h"

// Byte-ordering conversion routines
inline void
net_msg_hton(synexec_msg_t *net_msg){
	net_msg->version = htonl(net_msg->version);
	net_msg->session = htonl(net_msg->session);
	net_msg->datalen = htons(net_msg->datalen);
}

inline void
net_msg_ntoh(synexec_msg_t *net_msg){
	net_msg->version = ntohl(net_msg->version);
	net_msg->session = ntohl(net_msg->session);
	net_msg->datalen = ntohs(net_msg->datalen);
}
