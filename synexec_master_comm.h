/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_master_comm.h
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

#ifndef SYNEXEC_MASTER_COMM_H
#define SYNEXEC_MASTER_COMM_H

// Header files
#include <inttypes.h>
#include "synexec_master_slaveset.h"

// Global definitions
#define SYNEXEC_MASTER_COMM_PROBE_WAIT  1       // Time to wait for probe replies (secs)

// Related functions
int
wait_slaves(slaveset_t *slaveset);

int
slave_fd_probe(int sock);

int
slave_probe(slave_t *slave_aux);

int
config_slaves(slaveset_t *slaveset, char *conf_ptr, off_t conf_len);

int
execute_slaves(slaveset_t *slaveset);

int
join_slaves(slaveset_t *slaveset);

#endif /* SYNEXEC_MASTER_COMM_H */
