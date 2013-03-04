/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_slave_worker.h
 * ------------------------
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

#ifndef SYNEXEC_SLAVE_WORKER_H
#define SYNEXEC_SLAVE_WORKER_H

// Global definitions
#define MT_SYNEXEC_SLAVE_CONFDIR        "/tmp/"                 // Directory to place temporary configuration files
#define MT_SYNEXEC_SLAVE_OUTPUT         "/tmp/synexec.out"      // Redirected output of forked worker

// Related functions
void *
worker();

#endif /* SYNEXEC_SLAVE_WORKER_H */
