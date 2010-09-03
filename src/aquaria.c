/*
 * Aquarium Power Manager
 * Configuration files are in /etc/Aquaria/schedule
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
#include <inttypes.h>

#include <json.h>

#include <sys/poll.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include "aquaria.h"
#include "schedule.h"

struct aq_server_conn {
	struct aq_sched *sched;
	struct sockaddr saddr;
	socklen_t slen;
	int sock;

	json_parser parser;
	struct {
		enum {
			AQ_JKEY_INVALID,
			AQ_JKEY_REQUEST,
			AQ_JKEY_NAME,
			AQ_JKEY_TIMEOUT,
			AQ_JKEY_READING,
			AQ_JKEY_VALUE,
			AQ_JKEY_UNITS,
			AQ_JKEY_ACTIVE
		} key, subkey;
		int  depth;
		char request[PATH_MAX];
		char name[PATH_MAX];
		int active;
		struct {
			uint64_t value;
			char units[PATH_MAX];
		} timeout,reading, *subobj;
	} json;
};

static int wr_json(void *userdata, const char *s, uint32_t len)
{
	struct aq_server_conn *conn = userdata;
	int err;

	err = write(conn->sock, s, len);
	if (err < 0)
		return -errno;

	return 0;
}

static int aq_server_wr_sensor(json_printer *print, struct aq_sensor *sensor)
{
	int len;
	const char *cp;
	enum aq_sensor_type type;
	char buff[PATH_MAX];

	if (sensor == NULL)
		return 0;

	json_print_pretty(print, JSON_OBJECT_BEGIN, NULL, 0);

	/* name */
	json_print_pretty(print, JSON_KEY, "name", 4);
	cp = aq_sensor_name(sensor);
	assert(cp != NULL);
	len = strlen(cp);
	json_print_pretty(print, JSON_STRING, cp, len);

	/* type */
	json_print_pretty(print, JSON_KEY, "type", 4);
	type = aq_sensor_type(sensor);
	cp = aq_sensor_typename(type);
	assert(cp != NULL);
	len = strlen(cp);
	json_print_pretty(print, JSON_STRING, cp, len);

	/* reading */
	json_print_pretty(print, JSON_KEY, "reading", 7);
	json_print_pretty(print, JSON_OBJECT_BEGIN, NULL, 0);

	/* reading.value */
	json_print_pretty(print, JSON_KEY, "value", 5);
	snprintf(buff, sizeof(buff), "%" PRIu64, aq_sensor_reading(sensor));
	buff[sizeof(buff)-1]=0;
	json_print_pretty(print, JSON_STRING, buff, strlen(buff));

	/* reading.units */
	json_print_pretty(print, JSON_KEY, "units", 5);
	cp = aq_sensor_typeunits(type);
	len = strlen(cp);
	json_print_pretty(print, JSON_STRING, cp, len);

	json_print_pretty(print, JSON_OBJECT_END, NULL, 0);

	json_print_pretty(print, JSON_OBJECT_END, NULL, 0);

	return 0;
}

static int aq_server_wr_device(json_printer *print, struct aq_device *dev)
{
	int len;
	const char *cp;
	time_t override, now;
	char buff[PATH_MAX];
	int active;

	if (dev == NULL)
		return 0;

	now = time(NULL);

	json_print_pretty(print, JSON_OBJECT_BEGIN, NULL, 0);

	/* name */
	json_print_pretty(print, JSON_KEY, "name", 4);
	cp = aq_device_name(dev);
	assert(cp != NULL);
	len = strlen(cp);
	json_print_pretty(print, JSON_STRING, cp, len);

	/* active */
	json_print_pretty(print, JSON_KEY, "active", 6);
	active = aq_device_get(dev, &override);
	json_print_pretty(print, active ? JSON_TRUE : JSON_FALSE, NULL, 0);

	if (now < override) {
		/* reason */
		json_print_pretty(print, JSON_KEY, "reason", 6);
		json_print_pretty(print, JSON_OBJECT_BEGIN, NULL, 0);

		/* reason.input */
		json_print_pretty(print, JSON_KEY, "input", 5);
		json_print_pretty(print, JSON_STRING, "set", 3);

		/* reason.active */
		json_print_pretty(print, JSON_KEY, "active", 6);
		json_print_pretty(print, active ? JSON_TRUE : JSON_FALSE, NULL, 0);

		/* reason.expires */
		json_print_pretty(print, JSON_KEY, "expires" , 7);
		json_print_pretty(print, JSON_OBJECT_BEGIN, NULL, 0);

		/* reason.expires.value */
		json_print_pretty(print, JSON_KEY, "value", 5);
		snprintf(buff, sizeof(buff), "%" PRIu64, (override - now) * 1000000UL);
		buff[sizeof(buff)-1] = 0;
		len = strlen(buff);
		json_print_pretty(print, JSON_INT, buff, len);

		/* reason.expires.units */
		json_print_pretty(print, JSON_KEY, "units", 5);
		json_print_pretty(print, JSON_STRING, "us", 2);

		json_print_pretty(print, JSON_OBJECT_END, NULL, 0);
		/* .. reason.expires */

		json_print_pretty(print, JSON_OBJECT_END, NULL, 0);
		/* .. reason */
	}

	json_print_pretty(print, JSON_OBJECT_END, NULL, 0);

	return 0;
}

static int aq_server_respond(struct aq_server_conn *conn)
{
	int err;
	json_printer print;

	err = json_print_init(&print, wr_json, conn);
	assert(err >= 0);

	if (strcmp(conn->json.request, "get-sensor") == 0) {
		struct aq_sensor *sensor;

		json_print_pretty(&print, JSON_OBJECT_BEGIN, NULL, 0);
		json_print_pretty(&print, JSON_KEY, "sensor", 6);
		json_print_pretty(&print, JSON_ARRAY_BEGIN, NULL, 0);
		if (conn->json.name[0] == 0) {
			for (sensor = aq_sched_sensors(conn->sched);
			     sensor != NULL;
			     sensor = aq_sensor_next(sensor)) {
				aq_server_wr_sensor(&print, sensor);
			}
		} else {
			sensor = aq_sensor_find(conn->sched, conn->json.name);
			aq_server_wr_sensor(&print, sensor);
		}
		json_print_pretty(&print, JSON_ARRAY_END, NULL, 0);
		json_print_pretty(&print, JSON_OBJECT_END, NULL, 0);
	} else if (strcmp(conn->json.request, "get-device") == 0) {
		struct aq_device *dev;

		json_print_pretty(&print, JSON_OBJECT_BEGIN, NULL, 0);
		json_print_pretty(&print, JSON_KEY, "device", 6);
		json_print_pretty(&print, JSON_ARRAY_BEGIN, NULL, 0);
		if (conn->json.name[0] == 0) {
			for (dev = aq_sched_devices(conn->sched);
			     dev != NULL;
			     dev = aq_device_next(dev)) {
				aq_server_wr_device(&print, dev);
			}
		} else {
			dev = aq_device_find(conn->sched, conn->json.name);
			aq_server_wr_device(&print, dev);
		}
		json_print_pretty(&print, JSON_ARRAY_END, NULL, 0);
		json_print_pretty(&print, JSON_OBJECT_END, NULL, 0);
	} else {
		err = -EINVAL;
	}

	if (err < 0) {
		fprintf(stderr, "WARNING: Invalid request \"%s\"\n", conn->json.request);
		json_print_pretty(&print, JSON_OBJECT_BEGIN, NULL, 0);
		json_print_pretty(&print, JSON_OBJECT_END, NULL, 0);
	}

	json_print_free(&print);

	return err;
}


static int rd_json(void *userdata, int type, const char *data, uint32_t len)
{
	struct aq_server_conn *conn = userdata;
	char *cp;
	int err = 0;

	/* If we're in a bad state, anything else from this connection */
	if (conn->json.depth < 0)
		return -EINVAL;

	if (conn->json.depth == 0)
		memset(&conn->json, 0, sizeof(conn->json));

	switch (type) {
	case JSON_OBJECT_BEGIN:
		if (conn->json.key == AQ_JKEY_TIMEOUT ||
		    conn->json.key == AQ_JKEY_READING) {
			if (conn->json.depth != 1) {
				err=-EINVAL;
				break;
			}
			conn->json.depth = 2;
			conn->json.subkey = AQ_JKEY_INVALID;
			break;
		}
		if (conn->json.depth > 0) {
			err=-EINVAL;
			break;
		}
		conn->json.depth = 1;
		conn->json.key = AQ_JKEY_INVALID;
		break;
	case JSON_OBJECT_END:
		conn->json.depth--;
		if (conn->json.depth == 0)
			aq_server_respond(conn);
		break;
	case JSON_KEY:
		if (conn->json.depth == 1) {
			if (strcmp(data, "request") == 0) {
				conn->json.key = AQ_JKEY_REQUEST;
			} else if (strcmp(data, "name") == 0) {
				conn->json.key = AQ_JKEY_NAME;
			} else if (strcmp(data, "active") == 0) {
				conn->json.key = AQ_JKEY_ACTIVE;
			} else if (strcmp(data, "timeout") == 0) {
				conn->json.key = AQ_JKEY_TIMEOUT;
				conn->json.subobj = &conn->json.timeout;
			} else if (strcmp(data, "reading") == 0) {
				conn->json.key = AQ_JKEY_READING;
				conn->json.subobj = &conn->json.reading;
			} else {
				err=-EINVAL;
				break;
			}
		} else if (conn->json.depth == 2) {
			if (strcmp(data, "value") == 0) {
				conn->json.subkey = AQ_JKEY_VALUE;
			} else if (strcmp(data, "units") == 0) {
				conn->json.subkey = AQ_JKEY_UNITS;
			} else {
				err=-EINVAL;
				break;
			}
		} else {
			err=-EINVAL;
			break;
		}
		break;
	case JSON_INT:
		if (conn->json.depth == 2 && conn->json.subkey == AQ_JKEY_VALUE) {
			conn->json.subobj->value = strtoull(data, NULL, 0);
		} else {
			err=-EINVAL;
			break;
		}
		break;
	case JSON_STRING:
		cp = NULL;
		len = 0;
		if (conn->json.depth == 2 && conn->json.subkey == AQ_JKEY_UNITS) {
			cp = &conn->json.subobj->units[0];
			len = sizeof(conn->json.subobj->units);
		} else if (conn->json.depth != 1) {
			err=-EINVAL;
			break;
		} else switch (conn->json.key) {
			case AQ_JKEY_REQUEST:
				cp = &conn->json.request[0];
				len = sizeof(conn->json.request);
				break;
			case AQ_JKEY_NAME:
				cp = &conn->json.name[0];
				len = sizeof(conn->json.name);
				break;
			default:
				err=-EINVAL;
				break;
		}
		strncpy(cp, data, len - 1);
		cp[len-1] = 0;
		break;
	case JSON_TRUE:
	case JSON_FALSE:
		if (conn->json.depth == 1 && conn->json.key == AQ_JKEY_ACTIVE) {
			conn->json.active = (type == JSON_TRUE) ? 1 : -1;
		}
		break;
	default:
		err=-EINVAL;
		break;
	}

	if (err < 0) {
		fprintf(stderr, "JSON: Parser error, type=%d, data=\"%s\"\n", type, data);
	}

	return err;
}

static int aq_server_open(int port)
{
	struct sockaddr_in sin;
	int sock, err;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = INADDR_ANY;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return sock;

	err = bind(sock, (const struct sockaddr *)&sin, sizeof(sin));
	if (err < 0) {
		close(sock);
		return err;
	}

	err = listen(sock, 128);
	if (err < 0) {
		close(sock);
		return err;
	}

	return sock;
}

static struct aq_server_conn *aq_server_connect(struct aq_sched *sched, int sock)
{
	struct aq_server_conn *conn;
	int err, sfd, flags;
	struct sockaddr saddr;
	socklen_t slen = sizeof(saddr);

	sfd = accept(sock, &saddr, &slen);
	if (sfd < 0)
		return NULL;

	conn = calloc(1, sizeof(*conn));
	if (conn == NULL)
		return NULL;

	conn->sched = sched;
	conn->sock = sfd;
	conn->saddr = saddr;
	conn->slen = slen;

	err = json_parser_init(&conn->parser, NULL, rd_json, conn);
	assert(err >= 0);

	conn->json.depth = 0;

	return conn;
}

static void aq_server_disconnect(struct aq_server_conn *conn)
{
	close(conn->sock);
	json_parser_free(&conn->parser);
	free(conn);
}

int aq_server_handle(struct aq_server_conn *conn)
{
	int err, len;
	char *cp;
	char buff[PATH_MAX];

	len = read(conn->sock, &buff[0], sizeof(buff));
	if (len == 0)
		return -1;	/* EOF */

	if (len < 0)
		return len;

	/* The JSON parser will call callbacks if need be.
	 */
	err = json_parser_string(&conn->parser, buff, len, NULL);
	assert(err >= 0);

	return 0;
}

void log_exit_reason(int exit_code, void *priv)
{
	if (exit_code != 0) {
		syslog(LOG_ERR, "Abnormal exit code %d", exit_code);
	} else {
		syslog(LOG_NOTICE, "Shutting down");
	}
}

void usage(const char *program)
{
	fprintf(stderr, "Usage:\n"
			"%s [options]\n"
			"\n"
			"Options:\n"
			"  -d DIR, --datadir DIR       location of Aquaria data\n"
			"\n"
			"Commands:\n"
			"  -h, -?, --help              this help message\n"
			"  -V, --version               version of this utility\n"
			,program);
	exit(EXIT_FAILURE);
}

void version(void)
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
	struct aq_sched *sched;
	int err;
	int sock;
	struct pollfd *fd;
	struct aq_server_conn **conn;
	int fds, i;
	time_t last_time;
	int c, option;
	const char *datadir = "/etc/aquaria";
	struct option options[] = {
		{ .name = "datadir", .has_arg = 1, .flag = NULL, .val = 'd' },
		{ .name = "help", .has_arg = 0, .flag = NULL, .val = 'h' },
		{ .name = "version", .has_arg = 0, .flag = NULL, .val = 'V' },
		{ .name = NULL },
	};

	while ((c = getopt_long(argc, argv, "+d:hV", options, &option)) >= 0) {
		switch (c) {
		case 'd':
			datadir = optarg;
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

	err = chdir(datadir);
	if (err < 0) {
		exit(EXIT_FAILURE);
	}
	sched = aq_sched_alloc("/tmp/aquaria.log");
	aq_sched_config(sched, "config");
	aq_sched_read(sched, "schedule");

	sock = aq_server_open(4444);
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
	aq_sched_eval(sched);

	while (1) {
		err = poll(fd, fds, 1000);
		if (err == 0 || time(NULL) != last_time) {
			aq_sched_eval(sched);
			last_time = time(NULL);
		}

		if (fd[0].revents & POLLIN) {
			conn = realloc(conn, sizeof(conn[0]) * (fds + 1));
			conn[fds] = aq_server_connect(sched, fd[0].fd);
			fd = realloc(fd, sizeof(fd[0]) * (fds + 1));
			fd[fds].fd = conn[fds]->sock;
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
				memcpy(&conn[i], &conn[i+1], fds - i - 1);
				memcpy(&fd[i], &fd[i+1], fds - i - 1);
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
