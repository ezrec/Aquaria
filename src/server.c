/*
 * Aquarium Power Manager
 * Configuration files are in /etc/Aquaria/aqule
 *
 * Copyright 2009, Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 * GPL v2.0
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <inttypes.h>

#include <sys/poll.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include "aquaria.h"
#include "aq_server.h"

static void log_exit_reason(int exit_code, void *priv)
{
	if (exit_code != 0) {
		syslog(LOG_ERR, "Abnormal exit code %d", exit_code);
	} else {
		syslog(LOG_NOTICE, "Shutting down");
	}
}

static void usage(const char *program)
{
	fprintf(stderr, "Usage:\n"
			"%s [options]\n"
			"\n"
			"Options:\n"
			"  -d DIR, --datadir DIR       location of Aquaria data\n"
			"  -v FILE, --vcdlog FILE      VCD log (for use with gtkwave)\n"
			"  -p PORT, --port NUM         port to listen at\n"
			"\n"
			"Commands:\n"
			"  -h, -?, --help              this help message\n"
			"  -V, --version               version of this utility\n"
			,program);
	exit(EXIT_FAILURE);
}

static void version(void)
{
	printf("%s (%s) %s\n", PACKAGE_NAME, PACKAGE_BUGREPORT, PACKAGE_VERSION);
	printf("Copyright (C) 2010 Jason S. McMullan\n");
	printf("This program is free software; you may redistribute it under the terms of\n"
	       "the GNU General Public License version 2 or (at your option) a later version.\n"
	       "This program has absolutely no warranty.\n");
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	struct aquaria *aq;
	int err;
	int sock;
	struct pollfd *fd;
	struct aq_server_conn **conn;
	int fds, i;
	time_t last_time;
	int port = 4444;	// Default aquaria port
	int c, option;
	char *cp;
	const char *datadir = "/etc/aquaria";
	const char *vcdlog = "/dev/null";
	struct option options[] = {
		{ .name = "datadir", .has_arg = 1, .flag = NULL, .val = 'd' },
		{ .name = "vcdlog", .has_arg = 1, .flag = NULL, .val = 'v' },
		{ .name = "help", .has_arg = 0, .flag = NULL, .val = 'h' },
		{ .name = "version", .has_arg = 0, .flag = NULL, .val = 'V' },
		{ .name = "port", .has_arg = 1, .flag = NULL, .val = 'p' },
		{ .name = NULL },
	};

	while ((c = getopt_long(argc, argv, "+d:hpvV", options, &option)) >= 0) {
		switch (c) {
		case 'd':
			datadir = optarg;
			break;
		case 'p':
			port = strtol(optarg, &cp, 0);
			if (port < 0 || *cp != 0)
				usage(argv[0]);
			break;
		case 'v':
			vcdlog = optarg;
			break;
		case 'V':
			version();
		case 'h':
		case '?':
		default:
			usage(argv[0]);
			break;
		}
	}

	openlog("aquaria", LOG_CONS | LOG_PERROR, LOG_USER);
	on_exit(log_exit_reason, NULL);

	/* Ignore SIGPIPE errors */
	signal(SIGPIPE, SIG_IGN);

	err = chdir(datadir);
	if (err < 0) {
		exit(EXIT_FAILURE);
	}
	aq = aq_create(vcdlog);
	aq_config_read(aq, "config");
	aq_sched_read(aq, "schedule");

	sock = aq_server_open(port);
	if (sock < 0) {
		perror(argv[0]);
		exit(EXIT_FAILURE);
	}
	fd = calloc(1, sizeof(*fd));
	fds = 1;
	fd[0].fd = sock;
	fd[0].events = POLLIN | POLLHUP | POLLNVAL;
	fd[0].revents = 0;

	/* Placeholder for the inbound socket
	 * This keeps us from having to do off-by-one
	 * math everywhere.
	 */
	conn = malloc(sizeof(*conn));
	conn[0] = NULL;

	last_time = time(NULL);
	aq_sched_eval(aq);

	while (1) {
		err = poll(fd, fds, 1000);
		if (err == 0 || time(NULL) != last_time) {
			aq_sched_eval(aq);
			last_time = time(NULL);
		}

		if (fd[0].revents & POLLIN) {
			conn = realloc(conn, sizeof(conn[0]) * (fds + 1));
			conn[fds] = aq_server_connect(aq, fd[0].fd);
			fd = realloc(fd, sizeof(fd[0]) * (fds + 1));
			fd[fds].fd = aq_server_socket(conn[fds]);
			fd[fds].events = POLLIN | POLLHUP | POLLNVAL;
			fd[fds].revents = 0;
			fds++;
			fd[0].revents = 0;
		}

		for (i = 1; i < fds; i++) {
			if (fd[i].revents & POLLIN) {
				err = aq_server_handle(conn[i]);
				if (err < 0)
					fd[i].revents |= POLLHUP;
				fd[i].revents &= ~POLLIN;
			}
			if (fd[i].revents) {
				/* Socket died. */
				aq_server_disconnect(conn[i]);
				close(fd[i].fd);
				memmove(&conn[i], &conn[i+1], sizeof(*conn) * (fds - i - 1));
				memmove(&fd[i], &fd[i+1], sizeof(*fd) * (fds - i - 1));
				fds--;
			}
		}
	}

	for (i = 0; i < fds; i ++) {
		close(fd[i].fd);
	}

	free(fd);

	return EXIT_SUCCESS;
}
