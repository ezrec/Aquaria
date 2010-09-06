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
#include "aq_server.h"

struct aq_server_conn {
	struct aquaria *aq;
	struct sockaddr saddr;
	socklen_t slen;
	int sock;

	json_parser parser;
	struct {
		enum {
			AQ_JKEY_INVALID = 0,
			AQ_JKEY_REQUEST,
			AQ_JKEY_NAME,
			AQ_JKEY_TIMEOUT,
			AQ_JKEY_UNITS,
			AQ_JKEY_EXPIRE,
			AQ_JKEY_ACTIVE
		} key;
		int  depth;
		char request[PATH_MAX];
		char name[PATH_MAX];
		enum aq_state state;
		time_t expire;
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
	snprintf(buff, sizeof(buff), "%" PRIu64, aq_sensor_reading(sensor));
	buff[sizeof(buff)-1]=0;
	json_print_pretty(print, JSON_INT, buff, strlen(buff));

	/* units */
	json_print_pretty(print, JSON_KEY, "units", 5);
	cp = aq_sensor_typeunits(type);
	len = strlen(cp);
	json_print_pretty(print, JSON_STRING, cp, len);

	json_print_pretty(print, JSON_OBJECT_END, NULL, 0);

	return 0;
}

static int wr_json_device_condition(json_printer *print, struct aq_condition *cond)
{
	const char *name, *cp;
	char buff[PATH_MAX];
	enum aq_operator op;
	uint64_t reading, span;
	struct aq_sensor *sen;
	enum aq_state state;
	enum aq_sensor_type type;

	sen = aq_condition_sensor(cond);
	type = aq_sensor_type(sen);
	name = aq_sensor_name(sen);

	state = aq_condition_state(cond);
	aq_condition_trigger(cond, &op, &reading, &span);

	if (state == AQ_STATE_UNCHANGED)
		return 0;

	json_print_pretty(print, JSON_OBJECT_BEGIN, NULL, 0);

	json_print_pretty(print, JSON_KEY, "sensor", 6);
	json_print_pretty(print, JSON_STRING, name, strlen(name));

	json_print_pretty(print, JSON_KEY, "active", 6);
	json_print_pretty(print, (state == AQ_STATE_ON) ? JSON_TRUE : JSON_FALSE, NULL, 0);

	json_print_pretty(print, JSON_KEY, "operator", 8);
	switch (op) {
	case AQ_COND_LESS: cp = "<"; break;
	case AQ_COND_LEQUAL: cp = "<="; break;
	case AQ_COND_EQUAL: cp = "="; break;
	case AQ_COND_IN: cp = "in"; break;
	case AQ_COND_AT: cp = "at"; break;
	case AQ_COND_NEQUAL: cp = "!="; break;
	case AQ_COND_GEQUAL: cp = ">="; break;
	case AQ_COND_GREATER: cp = ">"; break;
	default: cp = "invalid"; break;
	}
	json_print_pretty(print, JSON_STRING, cp, strlen(cp));

	json_print_pretty(print, JSON_KEY, "reading", 7);
	snprintf(buff, sizeof(buff), "%" PRIu64, reading);
	buff[sizeof(buff)-1] = 0;
	json_print_pretty(print, JSON_INT, buff, strlen(buff));

	json_print_pretty(print, JSON_KEY, "span", 4);
	snprintf(buff, sizeof(buff), "%" PRIu64, span);
	buff[sizeof(buff)-1] = 0;
	json_print_pretty(print, JSON_INT, buff, strlen(buff));

	/* units */
	json_print_pretty(print, JSON_KEY, "units", 5);
	cp = aq_sensor_typeunits(type);
	json_print_pretty(print, JSON_STRING, cp, strlen(cp));

	json_print_pretty(print, JSON_OBJECT_END, NULL, 0);

	return 0;
}

static int aq_server_wr_device(json_printer *print, struct aq_device *dev)
{
	int len;
	const char *cp;
	time_t override, now;
	char buff[PATH_MAX];
	enum aq_state state;
	struct aq_condition *cond;

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
	state = aq_device_get(dev, &override);
	if (state != AQ_STATE_UNCHANGED) {
		json_print_pretty(print, JSON_KEY, "active", 6);
		json_print_pretty(print, (state==AQ_STATE_ON) ? JSON_TRUE : JSON_FALSE, NULL, 0);
	}

	if (now < override) {
		/* override */
		json_print_pretty(print, JSON_KEY, "override", 8);
		json_print_pretty(print, JSON_OBJECT_BEGIN, NULL, 0);

		/* override.active */
		json_print_pretty(print, JSON_KEY, "active", 6);
		json_print_pretty(print, (state==AQ_STATE_ON) ? JSON_TRUE : JSON_FALSE, NULL, 0);

		/* override.expire */
		json_print_pretty(print, JSON_KEY, "expire" , 6);
		snprintf(buff, sizeof(buff), "%" PRIu64, (uint64_t)(override - now));
		buff[sizeof(buff)-1] = 0;
		len = strlen(buff);
		json_print_pretty(print, JSON_INT, buff, len);

		/* override.units */
		json_print_pretty(print, JSON_KEY, "units", 5);
		json_print_pretty(print, JSON_STRING, "s", 1);

		json_print_pretty(print, JSON_OBJECT_END, NULL, 0);
		/* .. override */
	}

	cond = aq_device_conditions(dev);
	/* Disable printing device condition sets for now */
	if (0 && cond != NULL) {
		/* conditions */
		json_print_pretty(print, JSON_KEY, "condition", 9);
		json_print_pretty(print, JSON_ARRAY_BEGIN, NULL, 0);

		for (; cond != NULL; cond = aq_condition_next(cond))
			wr_json_device_condition(print, cond);

		json_print_pretty(print, JSON_ARRAY_END, NULL, 0);
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

	if (strcmp(conn->json.request, "set-device") == 0) {
		struct aq_device *dev;
		dev = aq_device_find(conn->aq, conn->json.name);

		if (dev == NULL)
			return -EINVAL;

		aq_device_set(dev, conn->json.state, &conn->json.expire);

		strcpy(conn->json.request, "get-device");
	}

	if (strcmp(conn->json.request, "get-sensor") == 0) {
		struct aq_sensor *sensor;

		json_print_pretty(&print, JSON_OBJECT_BEGIN, NULL, 0);
		json_print_pretty(&print, JSON_KEY, "sensor", 6);
		json_print_pretty(&print, JSON_ARRAY_BEGIN, NULL, 0);
		if (conn->json.name[0] == 0) {
			for (sensor = aq_sensors(conn->aq);
			     sensor != NULL;
			     sensor = aq_sensor_next(sensor)) {
				aq_server_wr_sensor(&print, sensor);
			}
		} else {
			sensor = aq_sensor_find(conn->aq, conn->json.name);
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
			for (dev = aq_devices(conn->aq);
			     dev != NULL;
			     dev = aq_device_next(dev)) {
				aq_server_wr_device(&print, dev);
			}
		} else {
			dev = aq_device_find(conn->aq, conn->json.name);
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
		if (conn->json.depth > 0) {
			err=-EINVAL;
			break;
		}
		conn->json.depth++;
		conn->json.key = AQ_JKEY_INVALID;
		break;
	case JSON_OBJECT_END:
		conn->json.depth--;
		if (conn->json.depth == 0) {
			aq_server_respond(conn);
			err = write(conn->sock, "\n", 1);
			if (err > 0)
				err = 0;
		}
		break;
	case JSON_KEY:
		if (conn->json.depth == 1) {
			if (strcmp(data, "request") == 0) {
				conn->json.key = AQ_JKEY_REQUEST;
			} else if (strcmp(data, "name") == 0) {
				conn->json.key = AQ_JKEY_NAME;
			} else if (strcmp(data, "active") == 0) {
				conn->json.key = AQ_JKEY_ACTIVE;
			} else if (strcmp(data, "expire") == 0) {
				conn->json.key = AQ_JKEY_EXPIRE;
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
		if (conn->json.depth == 1 && conn->json.key == AQ_JKEY_EXPIRE) {
			conn->json.expire = (time_t)strtoull(data, NULL, 0);
		} else {
			err=-EINVAL;
			break;
		}
		break;
	case JSON_STRING:
		cp = NULL;
		len = 0;
		if (conn->json.depth != 1) {
			err=-EINVAL;
			break;
		} else switch (conn->json.key) {
			case AQ_JKEY_UNITS:
				/* Ignore units */
				break;
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
			conn->json.state = (type == JSON_TRUE) ? AQ_STATE_ON : AQ_STATE_OFF;
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

int aq_server_open(int port)
{
	struct sockaddr_in sin;
	int sock, err;
	int one = 1;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = INADDR_ANY;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return sock;

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

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

struct aq_server_conn *aq_server_connect(struct aquaria *aq, int sock)
{
	struct aq_server_conn *conn;
	int err, sfd;
	struct sockaddr saddr;
	socklen_t slen = sizeof(saddr);

	sfd = accept(sock, &saddr, &slen);
	if (sfd < 0)
		return NULL;

	conn = calloc(1, sizeof(*conn));
	if (conn == NULL)
		return NULL;

	conn->aq = aq;
	conn->sock = sfd;
	conn->saddr = saddr;
	conn->slen = slen;

	err = json_parser_init(&conn->parser, NULL, rd_json, conn);
	assert(err >= 0);

	conn->json.depth = 0;

	return conn;
}

void aq_server_disconnect(struct aq_server_conn *conn)
{
	close(conn->sock);
	json_parser_free(&conn->parser);
	free(conn);
}

int aq_server_handle(struct aq_server_conn *conn)
{
	int err, len;
	char buff[PATH_MAX];

	len = read(conn->sock, &buff[0], sizeof(buff));
	if (len == 0) {
		return -1;	/* EOF */
	}

	if (len < 0)
		return len;

	/* The JSON parser will call callbacks if need be.
	 */
	err = json_parser_string(&conn->parser, buff, len, NULL);
	if (err)
		return -EINVAL;

	return 0;
}

int aq_server_socket(struct aq_server_conn *conn)
{
	return conn->sock;
}
