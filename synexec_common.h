/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_common.h
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

#ifndef SYNEXEC_COMMON_H
#define SYNEXEC_COMMON_H

// Header files
#include <inttypes.h>

// Global definitions
#define MT_PROGNAME             "Synchronised Executioner"
#define MT_PROGNAME_LEN	        strlen(MT_PROGNAME)

// Program defaults
#define MT_NETPORT              5165            // Default network port (udp/tcp)
#define MT_SYNEXEC_VERSION      1

// Available message commands
#define MT_SYNEXEC_MSG_REPLY    0
#define MT_SYNEXEC_MSG_PROBE    1
#define MT_SYNEXEC_MSG_CONF     2
#define MT_SYNEXEC_MSG_CONF_OK  3
#define MT_SYNEXEC_MSG_CONF_NO  4
#define MT_SYNEXEC_MSG_EXEC     5
#define MT_SYNEXEC_MSG_EXEC_OK  6
#define MT_SYNEXEC_MSG_EXEC_NO  7
#define MT_SYNEXEC_MSG_RUNNING  8
#define MT_SYNEXEC_MSG_STOPPED  9
#define MT_SYNEXEC_MSG_FINISHD  10

// Configuration token
#define MT_SYNEXEC_CONF_TOKEN   ":CONF:"

// Network message
typedef struct {
	uint32_t        version;
	uint32_t        session;
	char            command;
	uint16_t        datalen;
}__attribute__((packed)) synexec_msg_t;

// Byte-ordering conversion routines
inline void
net_msg_hton(synexec_msg_t *net_msg);

inline void
net_msg_ntoh(synexec_msg_t *net_msg);

#endif /* SYNEXEC_COMMON_H */
