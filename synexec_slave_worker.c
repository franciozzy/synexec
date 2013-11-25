/*
 * ------------------------------------
 *  synexec - Synchronised Executioner
 * ------------------------------------
 *  synexec_slave_worker.c
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

// Header files
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>
#include "synexec_common.h"
#include "synexec_comm.h"
#include "synexec_slave_worker.h"

// Global variables
extern struct sockaddr_in       master_addr;
extern pthread_mutex_t          master_mutex;
extern pthread_cond_t           master_cond;

extern struct in_addr           net_ifip;
extern struct in_addr           net_ifbc;
extern uint16_t                 net_port;

extern uint32_t                 session;
extern int                      verbose;
extern char                     quit;

static int                      worker_pid = 0;
static struct timeval           worker_time[3];         // execution: 0-started, 1-finished, 2-zero for ref

/*
 * void
 * sigchld_h();
 * ------------
 *  This is the handler for SIGCHLD, which resets the worker_pid global var.
 */
void
sigchld_h(){
	// Mark worker as finished
	worker_pid = 0;

	// Get time worker finished
	gettimeofday(&worker_time[1], NULL);

	// Capture (well, ignore) child exit status
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

/*
 * static void
 * free_argvp(char **argp, char ***argv);
 * --------------------------------------
 *  This function frees the memory used by arg[pv] arrays and set them to NULL.
 *
 *  Mandatory params: argp, argv
 *  Optional params :
 *
 *  Return values:
 *   None
 */
static void
free_argvp(char **argp, char ***argv){
	// Local variables
	int                     i = 0;                  // Temporary integer

	// Free argv
	if (*argv){
		// Iterate through argv, freeing individual entries
		while ((*argv)[i]){
			free((*argv)[i++]);
		}

		// Free argv itself
		if (*argv){
			free(*argv);
			*argv = NULL;
		}
	}

	// Free argp
	if (*argp){
		free(*argp);
		*argp = NULL;
	}

	// Return
	return;
}

/*
 * static int
 * make_argv(char *data, char *conf_fn, char **argp, char ***argv);
 * ----------------------------------------------------------------
 *  This function takes a 'data' string and breaks it up in an argv-style array
 *  that can be then passed to exec()-like functions. After usage, argp and argv
 *  must be free()d with free_argvp(). The special token MT_SYNEXEC_CONF_TOKEN
 *  will be replaced with the contents of 'conf_fn'.
 *
 *  Mandatory params: data, conf_fn, argp, argv
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    n Number of args in *argv
 */
static int
make_argv(char *data, char *conf_fn, char **argp, char ***argv){
	// Local variables
	int                     argc = 0;               // Number of arguments

	int                     i;                      // Temporary integer
	char                    *ptr = NULL;            // Temporary pointer
	int                     err = 0;                // Return value

	// Count how many items we have (needed for initial malloc)
	ptr = data;
	while(*ptr){
		while (isspace(*ptr)){
			ptr++;
		}
		if (*ptr){
			argc++;
			while ((*ptr != 0) && (!isspace(*ptr))){
				ptr++;
			}
		}
	}

	// Return 0 on 'data' without actual items
	if (argc == 0){
		goto out;
	}

	// argc should also account for NULL
	argc++;

	// Allocate argv array
	if ((*argv = (char **)calloc(1, argc*sizeof(char *))) == NULL){
		perror("calloc");
		fprintf(stderr, "%s: Error alloc'ing %lu bytes for worker argv.\n", __FUNCTION__, argc*(long unsigned)sizeof(char *));
		goto err;
	}

	// Fill argp and first entry of argv
	if (!(ptr = strtok(data, " \f\n\r\t\v"))) goto err;   // Tokenize 'data' using same set as isspace()
	if (!(*argp = strdup(ptr)))               goto err;   // Copy first entry into *argp
	if (!(ptr = strrchr(ptr, '/')))           goto err;   // Needs to be an absolute path, must have at least one '/'
	if (!*++ptr)                              goto err;   // Needs to have something after last '/'
	if (!((*argv)[0] = strdup(ptr)))          goto err;   // Copy basename to *argv[0]

	// Fill remaining entries
	for (i=1; i<argc-1; i++){
		if (!(ptr = strtok(NULL, " \f\n\r\t\v"))){
			goto err;
		}
		if (!strcmp(ptr, MT_SYNEXEC_CONF_TOKEN)){
			if (!((*argv)[i] = strdup(conf_fn))){
				goto err;
			}
		}else{
			if (!((*argv)[i] = strdup(ptr))){
				goto err;
			}
		}
	}
	
	// Return number of arguments (including the terminating NULL)
	err = argc;

	// Bypass error section
	goto out;

err:
	err = -1;
	free_argvp(argp, argv);

out:
	// Return
	return(err);
}

/*
 * static int
 * handle_conn(int worker_fd, char *conf_fn);
 * ------------------------------------------
 *  This function implements a loop that handles the slave TCP connection with
 *  a master process.
 *
 *  Mandatory params: worker_fd, conf_fn
 *  Optional params :
 *
 *  Return values:
 *   -1 Error
 *    0 Success
 */
static int
handle_conn(int worker_fd, char *conf_fn){
	// Local variables
	int                     master_eof = 0;         // Master connection keep alive
	FILE                    *conf_fp = NULL;        // Configuration file pointer
	int                     exec_fd  = -1;          // Redirected output of forked worker

	synexec_msg_t           net_msg;                // Synexec msg
	char                    *data = NULL;           // Synexec msg data

	char                    **argv = NULL;          // Arg array for command line
	char                    *argp = NULL;           // Path for command line
	int                     argc = 0;               // Size of argv

	char                    *ptr  = NULL;           // Temporary pointer
	int                     i;                      // Temporary integer
	int                     err = 0;                // Return value

	// Loop listening for commands
	while(!quit && !master_eof){
		if (verbose > 0){
			printf("%s: About to wait for commands from the master...\n", __FUNCTION__);
		}
		if (data){
			free(data);
			data = NULL;
		}
		i = comm_recv(worker_fd, &net_msg, NULL, (void **)&data, NULL);
		if (i == -1){
			master_eof = 1;
			break;
		}else
		if (i == 0){
			// If finished working, report back
			if (memcmp(&worker_time[1], &worker_time[2], sizeof(worker_time[1]))){
				printf("%s: Work finished. Notifying master...\n", __FUNCTION__);
				fflush(stdout);
				i = comm_send(worker_fd, MT_SYNEXEC_MSG_FINISHD, NULL, &worker_time, sizeof(worker_time));
				memset(&worker_time, 0, sizeof(worker_time));
			}
			continue;
		}

		// Process packet received
		if (net_msg.command == MT_SYNEXEC_MSG_PROBE){
			if (verbose > 0){
				printf("%s: Received PROBE from master...\n", __FUNCTION__);
				fflush(stdout);
			}
			if (comm_send(worker_fd, MT_SYNEXEC_MSG_REPLY, NULL, NULL, 0) < 0){
				master_eof = 1;
			}
		}else
		if (net_msg.command == MT_SYNEXEC_MSG_CONF){
			if (verbose > 0){
				printf("%s: Received CONF from master...\n", __FUNCTION__);
				fflush(stdout);
			}

			// First, open the configuration file
			if ((conf_fp = fopen(conf_fn, "w")) == NULL){
				perror("fopen");
				fprintf(stderr, "%s: Error opening configuration file '%s' for writing.\n", __FUNCTION__, conf_fn);
				goto err;
			}

			// Detect where the actual command (first line) ends
			if ((ptr = strchr(data, '\n')) != NULL){
				// Split the first line
				*ptr++ = 0;

				// Check if there's anything at all in 'data'
				if (!*data){
					fprintf(stderr, "%s: Error parsing configuration file: no command given.\n", __FUNCTION__);
					goto conf_deny;
				}

				// Check if there's anything else in 'ptr'
				if (!*ptr){
					if (verbose > 0){
						printf("%s: Configuration file empty.\n", __FUNCTION__);
						fflush(stdout);
					}
					goto conf_maybe;
				}
			}else{
				// It could be that there is only one line, but no line break
				ptr = data+strlen(data);

				if (data == ptr){
					fprintf(stderr, "%s: Error parsing configuration file: no command given.\n", __FUNCTION__);
					goto conf_deny;
				}

				goto conf_maybe;
			}
			// Write the configuration file to disk
			if (fwrite(ptr, 1, net_msg.datalen-(ptr-data), conf_fp) < 0){
				perror("fwrite");
				fprintf(stderr, "%s: Error writing %hu bytes to configuration file '%s'.\n", __FUNCTION__, net_msg.datalen-(uint16_t)(ptr-data), conf_fn);
				goto err;
			}

conf_maybe:
			// Validate if the command given is executable
			if ((argc = make_argv(data, conf_fn, &argp, &argv)) <= 0){
				fprintf(stderr, "%s: Error parsing command line '%s'.\n", __FUNCTION__, data);
				goto conf_deny;
			}
			if (access(argp, X_OK) != 0){
				fprintf(stderr, "%s: Error, unable to execute command '%s'.\n", __FUNCTION__, data);
				goto conf_deny;
			}
//conf_accept:
			// Accept the configuration command
			if (comm_send(worker_fd, MT_SYNEXEC_MSG_CONF_OK, NULL, NULL, 0) < 0){
				master_eof = 1;
			}
			goto conf_close;
conf_deny:
			// Reject the configuration command
			if (comm_send(worker_fd, MT_SYNEXEC_MSG_CONF_NO, NULL, NULL, 0) < 0){
				master_eof = 1;
			}
			free_argvp(&argp, &argv);
conf_close:
			fclose(conf_fp);
			conf_fp = NULL;
		}else
		if (net_msg.command == MT_SYNEXEC_MSG_EXEC){
			if (!argv){
				fprintf(stderr, "%s: Master called EXEC without a valid CONFIG. Rejecting.\n", __FUNCTION__);
				if (comm_send(worker_fd, MT_SYNEXEC_MSG_EXEC_NO, NULL, NULL, 0) < 0){
					master_eof = 1;
				}
			}else
			if (worker_pid != 0){
				fprintf(stderr, "%s: Master called EXEC while a worker is already running. Rejecting.\n", __FUNCTION__);
				if (comm_send(worker_fd, MT_SYNEXEC_MSG_EXEC_NO, NULL, NULL, 0) < 0){
					master_eof = 1;
				}
			}else{
				worker_pid = fork();
				if (worker_pid < 0){
					// Fork failed
					perror("fork");
					fprintf(stderr, "%s: Error forking worker.\n", __FUNCTION__);
					if (comm_send(worker_fd, MT_SYNEXEC_MSG_EXEC_NO, NULL, NULL, 0) < 0){
						master_eof = 1;
					}
					goto err;
				}else
				if (worker_pid == 0){
					// Child
					if ((exec_fd = creat(MT_SYNEXEC_SLAVE_OUTPUT, S_IRUSR|S_IWUSR)) < 0){
						perror("creat");
						goto err;
					}
					close(fileno(stdout));
					close(fileno(stderr));
					if ((dup2(exec_fd, fileno(stdout)) < 0) ||
					    (dup2(exec_fd, fileno(stderr)) < 0)){
						perror("dup2");
						goto err;
					}
					execv(argp, argv);
					goto err;
				}else{
					// Get time worker started
					gettimeofday(&worker_time[0], NULL);
					memset(&worker_time[1], 0, sizeof(worker_time[1]));

					// Parent
					if (comm_send(worker_fd, MT_SYNEXEC_MSG_EXEC_OK, NULL, NULL, 0) < 0){
						master_eof = 1;
					}
				}
			}
		}
	}

	// Bypass error section
	goto out;

err:
	err = -1;

out:
	if (argv){
		free_argvp(&argp, &argv);
	}
	if (data){
		free(data);
		data = NULL;
	}
	if (conf_fp){
		fclose(conf_fp);
		conf_fp = NULL;
	}

	return(err);
}

/*
 * void *
 * worker();
 * ---------
 *  This thread relies on the beacon thread receiving a probe from a master
 *  process. It waits for a signal (cond/mutex) from beacon and connects to the
 *  master process, handling the connection (and the commands sent by the
 *  master) while it is alive.
 *
 *  Mandatory params:
 *  Optional params :
 *
 *  Return values:
 *   0 This function always return NULL
 */
void *
worker(){
	// Local variables
	int                     worker_fd = -1;         // Worker TCP socket
	struct sockaddr_in      worker_addr;            // Local copy of master address
	char                    *conf_fn = NULL;        // Configuration file name

	// Initialise configuration file path name
	if (asprintf(&conf_fn, "%s/synexec_slave_conf.%d", MT_SYNEXEC_SLAVE_CONFDIR, getpid()) < 0){
		perror("asprintf");
		fprintf(stderr, "%s: Error allocating memory for configuration file name.\n", __FUNCTION__);
		conf_fn = NULL;
		goto err;
	}
	if (access(MT_SYNEXEC_SLAVE_CONFDIR, W_OK) != 0){
		perror("access");
		fprintf(stderr, "%s: Unable to write to configuration directory '%s'.\n", __FUNCTION__, MT_SYNEXEC_SLAVE_CONFDIR);
		goto err;
	}

	// Initialise sigchld signal handler and time vals
	signal(SIGCHLD, sigchld_h);
	memset(&worker_time, 0, sizeof(worker_time));

	// Loop
	while (!quit){
		// Create TCP socket
		if ((worker_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
			perror("socket");
			fprintf(stderr, "%s: Error creating worker TCP socket.\n", __FUNCTION__);
			goto err;
		}

		// Wait for signal from beacon and copy master_addr to worker_addr
		memset(&worker_addr, 0, sizeof(worker_addr));
		pthread_mutex_lock(&master_mutex);
		while(!memcmp(&worker_addr, &master_addr, sizeof(worker_addr)))
			pthread_cond_wait(&master_cond, &master_mutex);
		memcpy(&worker_addr, &master_addr, sizeof(worker_addr));
		memset(&master_addr, 0, sizeof(master_addr));
		pthread_mutex_unlock(&master_mutex);

		// We could have been signalled due to quit, so double check
		if (quit){
			break;
		}

		// Connect to worker_addr
		if (verbose > 0){
			printf("%s: Connecting to '%s:%hu' with socket %d.\n", __FUNCTION__, inet_ntoa(worker_addr.sin_addr), ntohs(worker_addr.sin_port), worker_fd);
		}
		if (connect(worker_fd, (struct sockaddr *)&worker_addr, sizeof(worker_addr)) < 0){
			perror("connect");
			fprintf(stderr, "%s: Error connecting to master at '%s:%hu'. Looping...\n", __FUNCTION__, inet_ntoa(worker_addr.sin_addr), ntohs(worker_addr.sin_port));
			close(worker_fd);
			worker_fd = -1;
			pthread_mutex_lock(&master_mutex);
			memset(&master_addr, 0, sizeof(master_addr));
			pthread_mutex_unlock(&master_mutex);
			continue;
		}
		if (verbose > 0){
			printf("%s: Connected to '%s:%hu'.\n", __FUNCTION__, inet_ntoa(worker_addr.sin_addr), ntohs(worker_addr.sin_port));
		}

		// Handle connection loop
		if (handle_conn(worker_fd, conf_fn) != 0){
			goto err;
		}

		// Clean up configuration file
		if (conf_fn && (access(conf_fn, W_OK) == 0)){
			unlink(conf_fn);
		}

		// Close connection
		close(worker_fd);
		worker_fd = -1;
		pthread_mutex_lock(&master_mutex);
		memset(&master_addr, 0, sizeof(master_addr));
		pthread_mutex_unlock(&master_mutex);
		if (verbose > 0){
			printf("%s: Disconnecting from master and looping...\n", __FUNCTION__);
			fflush(stdout);
		}
	}

	// Skip error section
	goto out;

err:
	quit = 2;

out:
	// Release resources
	if (worker_fd >= 0){
		close(worker_fd);
	}
	if (conf_fn){
		if (access(conf_fn, W_OK) == 0){
			unlink(conf_fn);
		}
		free(conf_fn);
		conf_fn = NULL;
	}

	// Return
	return NULL;
}
