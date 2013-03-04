/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_netops.h
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

#ifndef SYNEXEC_NETOPS_H
#define SYNEXEC_NETOPS_H

// Header files
#include <netinet/in.h>
#include "synexec_common.h"

// Definitions
#define SYNEXEC_PROC_ROUTE      "/proc/net/route"

// Function prototypes
int
get_ifdef(char **defif_name, struct in_addr *gw_addr);

int
get_ifipaddr(char *if_name, struct in_addr *if_addr);

int
get_ifbroad(char *if_name, struct in_addr *if_broad);

#endif /* SYNEXEC_NETOPS_H */
