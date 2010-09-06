/*
 * Copyright 2009, Jason S. McMullan
 * Author: Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sysexits.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <poll.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <json.h>

#include "aquaria.h"
#include "log.h"
#include "uthash.h"

struct aquaria {
	struct {
		int noop:1;
	} flags;
	struct log *log;
	struct aq_sensor {
		void *log_id;
		char *name;
		enum aq_sensor_type type;
		uint64_t reading;

		int (*get_reading)(void *priv, uint64_t *reading);
		void *priv;

		UT_hash_handle hh;
	} *sensors;
	struct aq_device {
		void *log_id;
		char *name;
		int (*set_state)(void *priv, int is_on);
		void *priv;
		enum aq_state state;
		struct {
			enum aq_state state;
			time_t expire;
		} override;
		struct aq_condition {
			int id;
			struct aq_sensor *sensor;	/* Name of the sensor */
			enum aq_state state;
			enum aq_operator operator;
			struct {
				uint64_t reading;
				uint64_t span;
			} range;

			UT_hash_handle hh;
		} *conditions;

		struct aquaria *aq;
		UT_hash_handle hh;
	} *devices;
	struct aq_line {
		int num;

		char *line;
		struct aq_device *device;
		struct aq_condition *condition;

		UT_hash_handle hh;
	} *lines;
	struct {
		json_parser parser;
		json_printer print;
		int sock;
		struct sockaddr sockaddr;
		socklen_t socklen;

		int depth;
		int (*json_handler)(struct aquaria *aq, int type, const char *data, uint32_t len);
		struct {
			struct aq_sensor sensor;
			struct aq_device device;
			struct aq_condition cond;
		} tmp;
		char units[PATH_MAX];
		enum {
			AQ_JSTATE_NONE = 0,
			AQ_JSTATE_NAME,
			AQ_JSTATE_TYPE,
			AQ_JSTATE_READING,
			AQ_JSTATE_UNITS,
			AQ_JSTATE_ACTIVE,
			AQ_JSTATE_CONDITION,
			AQ_JSTATE_OPERATOR,
			AQ_JSTATE_EXPIRE,
			AQ_JSTATE_SPAN
		} state;
		int done;
	} client;
};

/* Default sensors
 * Always - always 0
 * Time   - Time of day
 * Date   - Microseconds since epoc
 */
static int get_reading_always(void *priv, uint64_t *reading)
{
	*reading = 0;
	return 1;
}

static struct reading_time_s {
	struct timeval now;
	struct tm localnow;
} reading_time;

static int get_reading_time(void *priv, uint64_t *reading)
{
	struct reading_time_s *time = priv;

	*reading = (time->localnow.tm_hour * 3600 +
		    time->localnow.tm_min * 60 +
		    time->localnow.tm_sec) * 1000000ULL +
		    time->now.tv_usec;

	return 1;
}

static int get_reading_weekday(void *priv, uint64_t *reading)
{
	struct reading_time_s *time = priv;

	*reading = time->localnow.tm_wday;

	return 1;
}

/* Add sensors to the schedule
 * This must be done before reading the schedule file!
 */
static int aquaria_sensor(struct aquaria *aq, const char *name,
                    enum aq_sensor_type type, int (*get_reading)(void *priv, uint64_t *reading),
                    void *get_reading_priv)
{
	struct aq_sensor *sen;

	sen = aq_sensor_find(aq, name);
	if (sen != NULL) {
		return -EBUSY;
	}

	sen = calloc(1, sizeof(*sen));
	sen->name = strdup(name);
	sen->type = type;
	sen->get_reading = get_reading;
	sen->priv = get_reading_priv;
	if (type == AQ_SENSOR_NOP ||
	    type == AQ_SENSOR_TIME ||
	    type == AQ_SENSOR_WEEKDAY) {
		sen->log_id = NULL;
	} else {
		sen->log_id = log_register_sensor(aq->log, name, type);
	}

	HASH_ADD_KEYPTR(hh, aq->sensors, sen->name, strlen(sen->name), sen);

	return 0;
}

/* Add devices to the schedule
 * This must be done before reading the schedule file!
 */
static int aquaria_device(struct aquaria *aq, const char *name,
                    int (*set_state)(void *priv, int is_on),
                    void *set_state_priv)
{
	struct aq_device *dev;

	dev = aq_device_find(aq, name);
	if (dev != NULL) {
		return -EBUSY;
	}

	dev = calloc(1, sizeof(*dev));
	dev->aq = aq;
	dev->name = strdup(name);
	dev->state = AQ_STATE_UNCHANGED;
	dev->set_state = set_state;
	dev->priv = set_state_priv;
	dev->log_id = log_register_device(aq->log, name);

	HASH_ADD_KEYPTR(hh, aq->devices, dev->name, strlen(dev->name), dev);

	return 0;
}

/* Create a new aquaria (for server side)
 */
struct aquaria *aq_create(const char *log, int noop)
{
	struct aquaria *aq;

	aq = calloc(1, sizeof(*aq));
	aq->client.sock = -1;
	aq->flags.noop = noop;
	aq->log = log_open(log);
	if (aq->log == NULL) {
		free(aq);
		return NULL;
	}

	/* Predefined sensors */
	aquaria_sensor(aq, "Always",  AQ_SENSOR_NOP,     get_reading_always, NULL);
	aquaria_sensor(aq, "Time",    AQ_SENSOR_TIME,    get_reading_time,    &reading_time);
	aquaria_sensor(aq, "Weekday", AQ_SENSOR_WEEKDAY, get_reading_weekday, &reading_time);

	return aq;
}

static int rd_json_sensor(struct aquaria *aq, int type, const char *data, uint32_t len)
{
	int err = 0;

	switch (type) {
	case JSON_ARRAY_BEGIN:
		aq->client.depth++;
		if (aq->client.depth != 2)
			err = -EINVAL;
		break;
	case JSON_ARRAY_END:
		aq->client.depth--;
		if (aq->client.depth != 1)
			err = -EINVAL;
		aq->client.state = AQ_JSTATE_NONE;
		aq->client.json_handler = NULL;
		break;
	case JSON_OBJECT_BEGIN:
		aq->client.depth++;
		if (aq->client.depth != 3) {
			err = -EINVAL;
			break;
		}
		memset(&aq->client.tmp.sensor, 0, sizeof(aq->client.tmp.sensor));
		aq->client.state = AQ_JSTATE_NONE;
		break;
	case JSON_OBJECT_END:
		aq->client.depth--;
		aq->client.state = AQ_JSTATE_NONE;
		if (aq->client.depth != 2)
			err = -EINVAL;
		else {
			struct aq_sensor *sen;

			HASH_FIND_STR(aq->sensors, aq->client.tmp.sensor.name, sen);
			if (sen == NULL) {
				sen = malloc(sizeof(*sen));
				*sen = aq->client.tmp.sensor;
				HASH_ADD_KEYPTR(hh, aq->sensors, sen->name, strlen(sen->name), sen);
			} else if (sen->type == aq->client.tmp.sensor.type) {
				sen->reading = aq->client.tmp.sensor.reading;
			} else {
				err = -EINVAL;
			}
		}
		break;
	case JSON_KEY:
		if (strcmp(data, "name") == 0) {
			aq->client.state = AQ_JSTATE_NAME;
		} else if (strcmp(data, "type") == 0) {
			aq->client.state = AQ_JSTATE_TYPE;
		} else if (strcmp(data, "reading") == 0) {
			aq->client.state = AQ_JSTATE_READING;
		} else if (strcmp(data, "units") == 0) {
			aq->client.state = AQ_JSTATE_UNITS;
		} else {
			err = -EINVAL;
		}
		break;
	case JSON_STRING:
		if (aq->client.state == AQ_JSTATE_NAME) {
			if (aq->client.tmp.sensor.name != NULL)
				free(aq->client.tmp.sensor.name);
			aq->client.tmp.sensor.name = strdup(data);
		} else if (aq->client.state == AQ_JSTATE_UNITS) {
			/* Ignored for now */
		} else if (aq->client.state == AQ_JSTATE_TYPE) {
			enum aq_sensor_type type;

			type = aq_sensor_nametype(data);
			if (type == AQ_SENSOR_INVALID)
				err = -EINVAL;
			else
				aq->client.tmp.sensor.type = type;
		} else {
			err = -EINVAL;
		}
		break;
	case JSON_INT:
		if (aq->client.state == AQ_JSTATE_READING) {
			aq->client.tmp.sensor.reading = strtoull(data, NULL, 0);
		} else {
			err = -EINVAL;
		}
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static int rd_json_device(struct aquaria *aq, int type, const char *data, uint32_t len);

static int rd_json_device_override(struct aquaria *aq, int type, const char *data, uint32_t len)
{
	int err = 0;

	switch (type) {
	case JSON_OBJECT_BEGIN:
		aq->client.depth++;
		if (aq->client.depth != 4) {
fprintf(stderr, "ERR: Incorrect override depth %d\n", aq->client.depth);
			err = -EINVAL;
			break;
		}
		aq->client.tmp.device.override.state = AQ_STATE_UNCHANGED;
		aq->client.tmp.device.override.expire = 0;
		break;
	case JSON_OBJECT_END:
		aq->client.depth--;
		aq->client.state = AQ_JSTATE_NONE;
		aq->client.json_handler = rd_json_device;
		break;
	case JSON_KEY:
		if (aq->client.depth == 4 && strcmp(data, "active") == 0) {
			aq->client.state = AQ_JSTATE_ACTIVE;
		} else if (aq->client.depth == 4 && strcmp(data, "units") == 0) {
			aq->client.state = AQ_JSTATE_UNITS;
		} else if (aq->client.depth == 4 && strcmp(data, "expire") == 0) {
			aq->client.state = AQ_JSTATE_EXPIRE;
		} else {
fprintf(stderr, "ERR: Key %s at depth %d\n", data, aq->client.depth);
			err = -EINVAL;
		}
		break;
	case JSON_STRING:
		if (aq->client.state == AQ_JSTATE_UNITS) {
			/* TODO */
		} else {
fprintf(stderr, "ERR: String %s at depth %d\n", data, aq->client.depth);
			err = -EINVAL;
		}
		break;
	case JSON_INT:
		if (aq->client.state == AQ_JSTATE_EXPIRE) {
			aq->client.tmp.device.override.expire = time(NULL) + strtoull(data, NULL, 0);
		} else {
fprintf(stderr, "ERR: Int %s at depth %d\n", data, aq->client.depth);
			err = -EINVAL;
		}
		break;
	case JSON_TRUE:
	case JSON_FALSE:
		if (aq->client.state == AQ_JSTATE_ACTIVE) {
			aq->client.tmp.device.override.state =
				(type == JSON_TRUE) ? AQ_STATE_ON : AQ_STATE_OFF;
		} else {
			err = -EINVAL;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int rd_json_device(struct aquaria *aq, int type, const char *data, uint32_t len)
{
	int err = 0;

	switch (type) {
	case JSON_ARRAY_BEGIN:
		aq->client.depth++;
		if (aq->client.depth != 2)
			err = -EINVAL;
		break;
	case JSON_ARRAY_END:
		aq->client.depth--;
		if (aq->client.depth != 1)
			err = -EINVAL;
		aq->client.state = AQ_JSTATE_NONE;
		aq->client.json_handler = NULL;
		break;
	case JSON_OBJECT_BEGIN:
		aq->client.depth++;
		if (aq->client.depth != 3) {
			err = -EINVAL;
			break;
		}
		memset(&aq->client.tmp.device, 0, sizeof(aq->client.tmp.device));
		aq->client.tmp.device.state = AQ_STATE_UNCHANGED;
		aq->client.state = AQ_JSTATE_NONE;
		break;
	case JSON_OBJECT_END:
		aq->client.depth--;
		aq->client.state = AQ_JSTATE_NONE;
		if (aq->client.depth == 0) {
			/* End of request */
		} else if (aq->client.depth == 2 && aq->client.tmp.device.name != NULL) {
			struct aq_device *dev;

			HASH_FIND_STR(aq->devices, aq->client.tmp.device.name, dev);
			if (dev == NULL) {
				dev = malloc(sizeof(*dev));
				*dev = aq->client.tmp.device;
				dev->aq = aq;
				HASH_ADD_KEYPTR(hh, aq->devices, dev->name, strlen(dev->name), dev);
			} else {
				dev->state = aq->client.tmp.device.state;
				dev->override = aq->client.tmp.device.override;
			}
		} else {
			err = -EINVAL;
		}
		break;
	case JSON_KEY:
		if (strcmp(data, "name") == 0) {
			aq->client.state = AQ_JSTATE_NAME;
		} else if (strcmp(data, "active") == 0) {
			aq->client.state = AQ_JSTATE_ACTIVE;
		} else if (strcmp(data, "override") == 0) {
			aq->client.state = AQ_JSTATE_NONE;
			aq->client.json_handler = rd_json_device_override;
		} else {
			err = -EINVAL;
		}
		break;
	case JSON_STRING:
		if (aq->client.state == AQ_JSTATE_NAME) {
			if (aq->client.tmp.device.name != NULL)
				free(aq->client.tmp.device.name);
			aq->client.tmp.device.name = strdup(data);
		} else {
			err = -EINVAL;
		}
		break;
	case JSON_TRUE:
	case JSON_FALSE:
		if (aq->client.state == AQ_JSTATE_ACTIVE) {
			aq->client.tmp.device.state = (type == JSON_TRUE) ? AQ_STATE_ON : AQ_STATE_OFF;
		} else {
			err = -EINVAL;
		}
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static int rd_json(void *userdata, int type, const char *data, uint32_t len)
{
	struct aquaria *aq = userdata;
	int err = 0;

	if (aq->client.depth < 0)
		return -EINVAL;

	if (aq->client.json_handler)
		err = aq->client.json_handler(aq, type, data, len);
	else switch (type) {
	case JSON_OBJECT_BEGIN:
		aq->client.depth++;
		break;
	case JSON_OBJECT_END:
		aq->client.depth--;
		if (aq->client.depth == 0)
			aq->client.done = 1;
		break;
	case JSON_KEY:
		if (strcmp(data, "sensor") == 0) {
			aq->client.json_handler = rd_json_sensor;
		} else if (strcmp(data, "device") == 0) {
			aq->client.json_handler = rd_json_device;
		} else {
			err = -EINVAL;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err < 0) {
		fprintf(stderr, "JSON: Error parsing %d item \"%s\"\n",
				type, data);
	}

	return err;
}

static int wr_json(void *userdata, const char *s, uint32_t len)
{
	struct aquaria *aq = userdata;
	int err;

	err = write(aq->client.sock, s, len);
	if (err < 0)
		return err;

	return 0;
}

int aq_sync(struct aquaria *aq, const char *request,const struct aq_device *dev)
{
	json_printer *print;
	int err;
	time_t now;

	if (aq->client.socklen == 0)
		return 0;

	if (request == NULL) {
		err = aq_sync(aq,  "get-sensor", NULL);
		if (err < 0)
			return err;
		err = aq_sync(aq,  "get-device", NULL);
		if (err < 0)
			return err;

		return 0;
	}

	aq->client.sock = socket(aq->client.sockaddr.sa_family, SOCK_STREAM, 0);
	if (aq->client.sock < 0)
		return -errno;

	err = connect(aq->client.sock, &aq->client.sockaddr, aq->client.socklen);
	if (err < 0) {
		err = -errno;
		close(aq->client.sock);
		aq->client.sock = -1;
		return err;
	}

	err = json_parser_init(&aq->client.parser, NULL, rd_json, aq);
	if (err < 0) {
		err = -errno;
		close(aq->client.sock);
		return err;
	}

	err = json_print_init(&aq->client.print, wr_json, aq);
	if (err < 0) {
		err = -errno;
		json_parser_free(&aq->client.parser);
		close(aq->client.sock);
		return err;
	}

	print = &aq->client.print;

	/* Request and parse */
	json_print_pretty(print, JSON_OBJECT_BEGIN, NULL, 0);
	json_print_pretty(print, JSON_KEY, "request", 7);
	json_print_pretty(print, JSON_STRING, request, 10);

	now = time(NULL);
	if (dev != NULL) {
		json_print_pretty(print, JSON_KEY, "name", 4);
		json_print_pretty(print, JSON_STRING, dev->name, strlen(dev->name));

		if (dev->state != AQ_STATE_UNCHANGED &&
		    dev->override.expire > now) {
			char buff[256];

			json_print_pretty(print, JSON_KEY, "active", 6);
			json_print_pretty(print, (dev->override.state==AQ_STATE_ON) ? JSON_TRUE : JSON_FALSE, NULL, 0);
			json_print_pretty(print, JSON_KEY, "expire", 6);
			snprintf(buff, sizeof(buff), "%" PRIu64, (uint64_t)(dev->override.expire - now));
			buff[sizeof(buff)-1]=0;
			json_print_pretty(print, JSON_INT, buff, strlen(buff));
		}
	}

	json_print_pretty(print, JSON_OBJECT_END, NULL, 0);

	/* Read till we can't read no more */
	aq->client.done = 0;
	do {
		char buff;

		do {
			err = read(aq->client.sock, &buff, 1);
			if (err < 0)
				return err;
		} while (err == 0);

		err = json_parser_string(&aq->client.parser, &buff, 1, NULL);
		if (err < 0)
			return err;
	} while (!aq->client.done);

	close(aq->client.sock);
	aq->client.sock = -1;

	return 0;
}

struct aquaria *aq_connect(const struct sockaddr *sin, socklen_t len)
{
	int err;
	struct aquaria *aq;

	aq = calloc(1, sizeof(*aq));
	aq->client.sockaddr = *sin;
	aq->client.socklen = len;

	err = aq_sync(aq, NULL, NULL);
	if (err < 0) {
		aq_free(aq);
		return NULL;
	}

	return aq;
}

void aq_free(struct aquaria *aq)
{
	struct aq_sensor *sen;
	struct aq_device *dev;
	struct aq_line *line;

	if (aq->client.sock >= 0) {
		json_print_free(&aq->client.print);
		json_parser_free(&aq->client.parser);
		close(aq->client.sock);
	}

	while (aq->lines) {
		line = aq->lines;
		HASH_DEL(aq->lines, line);
		free(line->line);
		free(line);
	}

	while (aq->devices) {
		struct aq_condition *cond;

		dev = aq->devices;
		HASH_DEL(aq->devices, dev);

		while (dev->conditions) {
			cond = dev->conditions;
			HASH_DEL(dev->conditions, cond);
			free(cond);
		}

		free(dev);
	}

	while (aq->sensors) {
		sen = aq->sensors;
		HASH_DEL(aq->sensors, sen);
		free(sen);
	}

	free(aq);
}

struct aq_sensor *aq_sensor_find(struct aquaria *aq, const char *name)
{
	struct aq_sensor *sen;

	HASH_FIND_STR(aq->sensors, name, sen);

	return sen;
}

struct aq_device *aq_device_find(struct aquaria *aq, const char *name)
{
	struct aq_device *dev;

	HASH_FIND_STR(aq->devices, name, dev);

	return dev;
}

const char *aq_sensor_typename(enum aq_sensor_type type)
{
	const char *c;

	c = "unknown";
	switch (type) {
	case AQ_SENSOR_NOP: c = "no-op"; break;
	case AQ_SENSOR_WEEKDAY: c = "weekday"; break;
	case AQ_SENSOR_TIME: c = "time"; break;
	case AQ_SENSOR_TEMP: c = "temp"; break;
	default:
		/* TODO: Other sensor types */
		break;
	}

	return c;
}

const char *aq_sensor_typeunits(enum aq_sensor_type type)
{
	const char *c;

	c = "unknown";
	switch (type) {
	case AQ_SENSOR_NOP: c = "no-op"; break;
	case AQ_SENSOR_WEEKDAY: c = "weekday"; break;	// 0 - 6
	case AQ_SENSOR_TIME: c = "us"; break;	// microseconds
	case AQ_SENSOR_TEMP: c = "uK"; break;	// microkelvin
	default:
		/* TODO: Other sensor types */
		break;
	}

	return c;
}


enum aq_sensor_type aq_sensor_nametype(const char *name)
{
	enum aq_sensor_type type = AQ_SENSOR_INVALID;

	if (strcasecmp(name, "no-op") == 0) {
		type = AQ_SENSOR_NOP;
	} else if (strcasecmp(name, "temp") == 0) {
		type = AQ_SENSOR_TEMP;
	} else if (strcasecmp(name, "time") == 0) {
		type = AQ_SENSOR_TIME;
	} else if (strcasecmp(name, "weekday") == 0) {
		type = AQ_SENSOR_WEEKDAY;
	}

	return type;
}

static int aq_device_exec(void *priv, int is_on)
{
	char **argv = priv;
	char buff[256];
	pid_t pid;
	int status;

	/* NOTE: argv[0] is already set to the program to run,
	 *       argv[1] is NULL,
	 *       argv[N] has the arguments passed in from the config file
	 */
	snprintf(buff, sizeof(buff), "--state=%s", is_on ? "on" : "off");
	argv[1] = buff;

	pid = fork();
	if (pid == 0) { /* child */
		execvp(argv[0], argv);
		exit(EX_UNAVAILABLE);
	}

	/* parent - wait for child to die */
	while (waitpid(pid, &status, 0) == 1) {
		if (WIFEXITED(status))
			break;

		if (WIFSIGNALED(status)) {
			return -EX_SOFTWARE;
		}
	}

	return -WEXITSTATUS(status);
}

static int aq_device_debug(void *priv, int is_on)
{
	char **argv = priv;
	char buff[256];
	int i;

	/* NOTE: argv[0] is already set to the program to run,
	 *       argv[1] is NULL,
	 *       argv[N] has the arguments passed in from the config file
	 */
	snprintf(buff, sizeof(buff), "--state=%s", is_on ? "on" : "off");
	argv[1] = buff;

	fprintf(stderr, "EXEC:");
	for (i = 0; argv[i] != NULL; i++) {
		fprintf(stderr, " %s", argv[i]);
	}
	fprintf(stderr,"\n");

	return 0;
}

struct aq_process {
	char * const *argv;
	pid_t pid;
	struct pollfd pollfd[2];	// 1 = stdout, 2 = stderr
	char input[256];	// We only want one line of input
	int input_len;
	char error[256];	// and only one line of error
	int error_len;
};

static int aquaria_exec(struct aq_process *proc)
{
	char * const *argv = proc->argv;
	pid_t pid;
	int pfd_stdout[2];
	int pfd_stderr[2];
	int err;
	struct pollfd *pollfd = &proc->pollfd[0];

	proc->pid = 0;
	proc->input_len = 0;
	proc->error_len = 0;

	err = pipe(pfd_stdout);
	assert(err >= 0);
	err = pipe(pfd_stderr);
	assert(err >= 0);

	/* NOTE: argv[0] is already set to the program to run,
	 *       argv[N] has the arguments passed in from the config file
	 */
	pid = fork();
	if (pid < 0) {
		close(pfd_stdout[1]);
		close(pfd_stderr[1]);
		return -1;
	} else if (pid == 0) { /* child */
		close(pfd_stdout[0]);
		dup2(pfd_stdout[1], 1);
		close(pfd_stderr[0]);
		dup2(pfd_stderr[1], 2);
		execvp(argv[0], argv);
		exit(EX_UNAVAILABLE);
	}

	close(pfd_stdout[1]);
	close(pfd_stderr[1]);

	pollfd[0].fd = pfd_stdout[0];
	pollfd[0].events = POLLIN | POLLERR | POLLHUP;
	pollfd[0].revents = 0;
	pollfd[1].fd = pfd_stderr[0];
	pollfd[1].events = POLLIN | POLLERR | POLLHUP;
	pollfd[1].revents = 0;

	proc->pid = pid;

	return 0;
}

/* Returns 0 for ok, 1 for valid input line, < 0 for closed pipe
 */
static int aquaria_exec_handler(struct aq_process *proc)
{
	char *cp, *ecp;
	struct pollfd *pollfd = &proc->pollfd[0];
	int status;
	int got_input = 0;
	int got_error = 0;
	int err = 0;
	int len;

	cp = &proc->input[proc->input_len];
	ecp = &proc->error[proc->error_len];

	err = poll(proc->pollfd, 2, 0);
	if (err == 0)
		return 0;

	if ((err > 0) && pollfd[0].revents & POLLIN) {
		len = read(pollfd[0].fd, cp, sizeof(proc->input) - (cp - proc->input));
		if (len > 0) {
			cp[len] = 0;
			if (strchr(cp, '\n') != NULL)
				got_input = 1;
			cp += len;
			proc->input_len += len;
		} else {
			err = -errno;
		}
	}

	if ((err > 0) && pollfd[1].revents & POLLIN) {
		len = read(pollfd[1].fd, ecp, sizeof(proc->error) - (ecp - proc->error));
		if (len > 0) {
			ecp[len] = 0;
			if (strchr(ecp, '\n') != NULL)
				got_error = 1;
			ecp += len;
			proc->error_len += len;
		} else {
			err = -errno;
		}
	}

	if ((err > 0) &&
	    ((pollfd[0].revents & (POLLERR | POLLHUP)) ||
	     (pollfd[1].revents & (POLLERR | POLLHUP)))) {
		err = -EPIPE;
	}

	if (err > 0)
		err = 0;

	if (got_error) {
		*ecp = 0;
		syslog(LOG_ERR, "%s: %s", proc->argv[0], proc->error);
		err = 0;
	}

	if (got_input)
		err = 1;

	/* wait for child to die */
	if (err != 0) {
		close(pollfd[0].fd);
		close(pollfd[1].fd);
		kill(proc->pid, SIGTERM);
		waitpid(proc->pid, &status, 0);
		proc->pid = 0;
	}

	return err;
}

static int aquaria_sensor_exec(void *priv, uint64_t *reading)
{
	struct aq_process *proc = priv;
	int err;

	if (proc->pid <= 0) {
		err = aquaria_exec(proc);
		if (err < 0)
			return err;
		err = 0;
	}

	if (proc->pid > 0)
		err = aquaria_exec_handler(proc);

	if (err == 1)
		*reading = strtoull(proc->input, NULL, 0);

	return err;
}

static char **read_args(int argc, char **argv, char **s)
{
	char *tok;

	while ((tok = strtok_r(NULL, " \t,", s)) != NULL) {
		argv[argc++] = strdup(tok);
		argv = realloc(argv, (argc+1) * sizeof(char *));
		argv[argc] = NULL;
	}

	return argv;
}

/* Read the configuration file
 */
int aq_config_read(struct aquaria *aq, const char *file)
{
	FILE *inf;
	char buff[1024+1], *s, *tok;
	int lineno = 0;
	int err;

	inf = fopen(file, "r");
	if (inf == NULL) {
		return -errno;
	}

	while ((s = fgets(buff, sizeof(buff), inf)) != NULL) {
		int len = strlen(s);
		lineno++;

		if (s[len-1] != '\n') {
			syslog(LOG_ERR, "%s:%d: Line length too long (> %d characters)",
			       file, lineno, (int)(sizeof(buff) - 1));
			exit(EX_DATAERR);
		}

		buff[len - 1] = 0;	/* Trim off the trailing \n */

		/* Get rid of comments */
		s = strchr(buff, '#');
		if (s != NULL) {
			*s = 0;
		}

		/* Get the first word */
		tok = strtok_r(buff, " \t", &s);

		/* Ignore empty lines */
		if (tok == NULL) {
			continue;
		}

		/* Device configuration:
		 * device <name> <program> [options...]
		 */
		if (strcasecmp(tok, "device") == 0) {
			struct aq_device *dev;
			char **argv;
			const char *dev_name;

			/* Get device name */
			tok = strtok_r(NULL, " \t,", &s);
			if (tok == NULL) {
				syslog(LOG_ERR, "%s:%d: No device name given",
				       file, lineno);
				exit(EX_DATAERR);
			}

			dev = aq_device_find(aq, tok);
			if (dev != NULL) {
				syslog(LOG_ERR, "%s:%d: Device '%s' already defined.",
				       file, lineno, tok);
				exit(EX_DATAERR);
			}
			dev_name = tok;

			/* Get device program name */
			tok = strtok_r(NULL, " \t,", &s);
			if (tok == NULL) {
				syslog(LOG_ERR, "%s:%d: No device program given",
				       file, lineno);
				exit(EX_DATAERR);
			}

			argv = malloc(sizeof(char *)*3);
			argv[0] = strdup(tok);
			argv[1] = NULL;		/* For the --state= option */
			argv[2] = NULL;		/* Tail end */

			/* Read in arguments */
			argv = read_args(2, argv, &s);

			err = aquaria_device(aq, dev_name, aq->flags.noop ? aq_device_debug : aq_device_exec, argv);
			if (err < 0) {
				syslog(LOG_ERR, "%s:%d: Cannot create device '%s'", file, lineno, argv[0]);
				exit(EX_DATAERR);
			}
		} else if (strcasecmp(tok, "sensor") == 0) {
			/* sensor <name> <type> <device> <options...>
			 */
			struct aq_sensor *sen;
			char **argv;
			struct aq_process *proc;
			const char *sen_name;
			enum aq_sensor_type sen_type;

			/* Get sensor name */
			tok = strtok_r(NULL, " \t,", &s);
			if (tok == NULL) {
				syslog(LOG_ERR, "%s:%d: No sensor name given",
				       file, lineno);
				exit(EX_DATAERR);
			}

			sen = aq_sensor_find(aq, tok);
			if (sen != NULL) {
				syslog(LOG_ERR, "%s:%d: Sensor '%s' already defined.",
				       file, lineno, tok);
				exit(EX_DATAERR);
			}
			sen_name = tok;

			/* Get sensor type */
			tok = strtok_r(NULL, " \t,", &s);
			if (tok == NULL) {
				syslog(LOG_ERR, "%s:%d: No sensor type given",
				       file, lineno);
				exit(EX_DATAERR);
			}
			sen_type = aq_sensor_nametype(tok);

			/* Get sensor program name */
			tok = strtok_r(NULL, " \t,", &s);
			if (tok == NULL) {
				syslog(LOG_ERR, "%s:%d: No device program given",
				       file, lineno);
				exit(EX_DATAERR);
			}

			proc = calloc(1, sizeof(*proc));
			argv = malloc(sizeof(char *)*2);
			argv[0] = strdup(tok);
			argv[1] = NULL;		/* Tail end */

			/* Read in arguments */
			proc->argv = read_args(1, argv, &s);

			err = aquaria_sensor(aq, sen_name, sen_type, aquaria_sensor_exec, proc);
			if (err < 0) {
				syslog(LOG_ERR, "%s:%d: Cannot create device '%s'", file, lineno, argv[0]);
				exit(EX_DATAERR);
			}
		} else {
			syslog(LOG_ERR, "%s:%d: Unrecognized config directive '%s'",
			       file, lineno, tok);
			exit(EX_DATAERR);
		}
	}

	fclose(inf);
	return 0;
}

static int aq_cond_nop(struct aq_condition *cond, char **s)
{
	const char *tok;

	assert(cond->sensor->type == AQ_SENSOR_NOP);

	/* No tokens allowed. */
	tok = strtok_r(NULL, " \t", s);
	if (tok != NULL) {
		return -1;
	}

	cond->operator = AQ_COND_EQUAL;
	cond->range.reading = 0;
	cond->range.span = 0;

	return 0;
}

#define C_TO_K(c)	((c) + 273.15)
#define F_TO_C(f)	(((f) - 32) * 5.0/9.0)
static int aq_sensor_unitize(enum aq_sensor_type type, uint64_t *pval, const char *tok)
{
	double d_val,s;
	int h,m;
	char *rest;

	switch (type) {
	case AQ_SENSOR_TEMP:
		d_val = strtod(tok, &rest);
		if (rest == tok) {
			return -1;
		}
		if (strcasecmp(rest,"F") == 0) {
			/* Fahrenheit */
			*pval = (uint64_t)(C_TO_K(F_TO_C(d_val)) * 1000000ULL);
		} else if (strcasecmp(rest,"C") == 0) {
			/* Celsius */
			*pval = (uint64_t)(C_TO_K(d_val) * 1000000ULL);
		} else if (strcasecmp(rest,"K") == 0) {
			/* Kelvin */
			*pval = (uint64_t)(d_val * 1000000ULL);
		} else {
			return -1;
		}
		break;
	case AQ_SENSOR_TIME:
		h = strtol(tok, &rest, 10);
		if (rest == tok) {
			return -1;
		}
		if (h < 0 || h >= 24) {
			return -1;
		}
		*pval = (h * 3600ULL) * 1000000ULL;
		if (*rest == 0) {
			break;
		}
		if (*rest != ':') {
			return -1;
		}
		tok = rest + 1;
		m = strtol(tok, &rest, 10);
		if (m < 0 || m >= 60) {
			return -1;
		}
		*pval += (m * 60) * 1000000;
		if (*rest == 0) {
			break;
		}
		if (*rest != ':') {
			return -1;
		}
		tok = rest + 1;
		s = strtod(tok, &rest);
		if (s < 0 || s >= 60) {
			return -1;
		}
		*pval += s * 1000000;
		if (*rest != 0) {
			return -1;
		}
		break;
	case AQ_SENSOR_WEEKDAY:
		if ((strcasecmp(tok,"sun") == 0) || (strcasecmp(tok,"sunday") == 0)) {
			*pval = 0;
		} else if ((strcasecmp(tok,"mon") == 0) || (strcasecmp(tok,"monday") == 0)) {
			*pval = 1;
		} else if ((strcasecmp(tok,"tue") == 0) || (strcasecmp(tok,"tuesday") == 0)) {
			*pval = 2;
		} else if ((strcasecmp(tok,"wed") == 0) || (strcasecmp(tok,"wednesday") == 0)) {
			*pval = 3;
		} else if ((strcasecmp(tok,"thu") == 0) || (strcasecmp(tok,"thursday") == 0)) {
			*pval = 4;
		} else if ((strcasecmp(tok,"fri") == 0) || (strcasecmp(tok,"friday") == 0)) {
			*pval = 5;
		} else if ((strcasecmp(tok,"sat") == 0) || (strcasecmp(tok,"saturday") == 0)) {
			*pval = 6;
		} else {
			return -1;
		}
		break;
	default:
		/* TODO: Add other sensor types */
		return -1;
	}

	return 0;
}

static int aq_cond_value(struct aq_condition *cond, char **s)
{
	const char *tok, *tmp;

	/* We need the value */
	tok = strtok_r(NULL, " \t", s);
	if (tok == NULL) {
		return -1;
	}

	/* And verify that there was only one value */
	tmp = strtok_r(NULL, " \t", s);
	if (tmp != NULL) {
		return -1;
	}

	cond->range.span = 0;
	return aq_sensor_unitize(cond->sensor->type, &cond->range.reading, tok);
}


static int aq_cond_range(struct aq_condition *cond, char **s)
{
	const char *tok;
	int err;

	/* Need an operator token */
	tok = strtok_r(NULL, " \t", s);
	if (tok == NULL) {
		return -1;
	}

	if (strcasecmp(tok, "<") == 0) {
		cond->operator = AQ_COND_LESS;
		err = aq_cond_value(cond, s);
		if (err < 0) {
			return err;
		}
	} else if (strcasecmp(tok, "<=") == 0){
		cond->operator = AQ_COND_LEQUAL;
		err = aq_cond_value(cond, s);
		if (err < 0) {
			return err;
		}
	} else if ((strcasecmp(tok, "=") == 0  ||
	            strcasecmp(tok, "==") == 0 ||
	            strcasecmp(tok, "is") == 0)
			&& cond->sensor->type != AQ_SENSOR_TIME) {
		cond->operator = AQ_COND_EQUAL;
		err = aq_cond_value(cond, s);
		if (err < 0) {
			return err;
		}
	} else if ((strcasecmp(tok, "!=") == 0 || strcasecmp(tok, "<>") == 0)
			&& cond->sensor->type != AQ_SENSOR_TIME) {
		cond->operator = AQ_COND_NEQUAL;
		err = aq_cond_value(cond, s);
		if (err < 0) {
			return err;
		}
	} else if (strcasecmp(tok, ">=") == 0){
		cond->operator = AQ_COND_GEQUAL;
		err = aq_cond_value(cond, s);
		if (err < 0) {
			return err;
		}
	} else if (strcasecmp(tok, ">") == 0) {
		cond->operator = AQ_COND_GREATER;
		err = aq_cond_value(cond, s);
		if (err < 0) {
			return err;
		}
	} else if (strcasecmp(tok, "in") == 0 ||
	           strcasecmp(tok, "from") == 0) {
		uint64_t val1, val2;

		tok = strtok_r(NULL, " \t", s);
		if (tok == NULL) {
			return -1;
		}
		err = aq_sensor_unitize(cond->sensor->type, &val1, tok);
		if (err < 0) {
			return err;
		}
		tok = strtok_r(NULL, " \t", s);
		if (tok == NULL) {
			return -1;
		}
		if (strcasecmp(tok, "to") != 0) {
			return -1;
		}
		tok = strtok_r(NULL, " \t", s);
		if (tok == NULL) {
			return -1;
		}
		err = aq_sensor_unitize(cond->sensor->type, &val2, tok);
		if (err < 0) {
			return err;
		}
		tok = strtok_r(NULL, " \t", s);
		if (tok != NULL) {
			return -1;
		}
		if (val2 < val1) {
			return -1;
		}
		if (strcasecmp(tok, "in") == 0) {
			cond->operator = AQ_COND_IN;
		} else {
			cond->operator = AQ_COND_EQUAL;
		}
		cond->range.reading = val1;
		cond->range.span = val2 - val1;
	} else if (cond->sensor->type == AQ_SENSOR_TIME) {
		uint64_t val1, val2;
		int is_until = 0;

		cond->operator = AQ_COND_AT;

		if (strcasecmp(tok, "at") != 0) {
			return -1;
		}
		tok = strtok_r(NULL, " \t", s);
		if (tok == NULL) {
			return -1;
		}
		err = aq_sensor_unitize(cond->sensor->type, &val1, tok);
		if (err < 0) {
			return err;
		}
		cond->range.reading = val1;
		tok = strtok_r(NULL, " \t", s);
		if (tok == NULL) {
			cond->range.span = 1 * 60 * 1000000;	/* 1 minute duration */
			return 0;
		}
		if (strcasecmp(tok, "until") == 0) {
			is_until = 1;
		} else if (strcasecmp(tok, "for") != 0) {
			return -1;
		}
		tok = strtok_r(NULL, " \t", s);
		if (tok == NULL) {
			return -1;
		}
		err = aq_sensor_unitize(cond->sensor->type, &val2, tok);
		if (err < 0) {
			return err;
		}
		if (is_until) {
			if (val2 < val1) {
				return -1;
			}
			cond->range.span = val2 - val1;
		} else {
			if ((val1 + val2) > (24 * 3600 * 1000000ULL)) {
				return -1;
			}
			cond->range.span = val2;
		}
	} else {
		return -1;
	}

	return 0;
}

/* Read/Write the schedule config file.
 * These are comments-preserving routines.
 */
int aq_sched_read(struct aquaria *aq, const char *file)
{
	FILE *inf;
	struct aq_line *line;
	struct aq_condition **cond_ptr = NULL;
	int cond_id = 0;
	char buff[1024+1], *s, *tok;
	int lineno = 0;

	inf = fopen(file, "r");
	if (inf == NULL) {
		return -errno;
	}

	while ((s = fgets(buff, sizeof(buff), inf)) != NULL) {
		int len = strlen(s);
		lineno++;

		if (s[len-1] != '\n') {
			syslog(LOG_ERR, "%s:%d: Line length too long (> %d characters)",
			       file, lineno, (int)(sizeof(buff) - 1));
			exit(EX_DATAERR);
		}

		buff[len - 1] = 0; /* Trim off the trailing \n */

		line = calloc(1, sizeof(*line));
		line->num = lineno;
		line->line = strdup(buff);

		HASH_ADD_INT(aq->lines, num, line);

		/* Get rid of comments */
		s = strchr(buff, '#');
		if (s != NULL) {
			*s = 0;
		}

		/* Get the first word */
		tok = strtok_r(buff, " \t", &s);

		/* Ignore empty lines */
		if (tok == NULL) {
			continue;
		}

		/* Device section */
		if (strcasecmp(tok, "device") == 0) {
			struct aq_device *dev;

			/* Get device name */
			tok = strtok_r(NULL, " \t,", &s);
			if (tok == NULL) {
				syslog(LOG_ERR, "%s:%d: No device name given",
				       file, lineno);
				exit(EX_DATAERR);
			}

			dev = aq_device_find(aq, tok);
			if (dev == NULL) {
				syslog(LOG_ERR, "%s:%d: No such device \"%s\".",
				       file, lineno, tok);
				exit(EX_DATAERR);
			}

			/* Verify that there are no extra tokens on the line */
			tok = strtok_r(NULL, " \t,", &s);
			if (tok != NULL) {
				syslog(LOG_ERR, "%s:%d: Syntax error - unexpected options found.",
				       file, lineno);
				exit(EX_DATAERR);
			}

			if (dev->conditions != NULL) {
				syslog(LOG_ERR, "%s:%d: Only one 'device' block allowed per device!",
				       file, lineno);
				exit(EX_DATAERR);
			}

			line->device = dev;

			/* Set current condition list */
			cond_ptr = &dev->conditions;
			cond_id = 0;
		} else if (strcasecmp(tok, "on") == 0 || strcasecmp(tok, "off") == 0) {
			int is_on = (strcasecmp(tok, "on") == 0);
			struct aq_sensor *sen;
			struct aq_condition *cond;
			int err = -1;

			if (cond_ptr == NULL) {
				syslog(LOG_ERR, "%s:%d: State control line outside of a 'device' block!",
				                file, lineno);
				exit(EX_DATAERR);
			}

			/* Get the sensor name */
			tok = strtok_r(NULL, " \t,", &s);
			if (tok == NULL) {
				syslog(LOG_ERR, "%s:%d: No sensor name given.",
				       file, lineno);
				exit(EX_DATAERR);
			}

			sen = aq_sensor_find(aq, tok);
			if (sen == NULL) {
				syslog(LOG_ERR, "%s:%d: No such sensor \"%s\".",
				       file, lineno, tok);
				exit(EX_DATAERR);
			}

			cond = calloc(1, sizeof(*cond));
			cond->id = cond_id++;
			cond->sensor = sen;
			cond->state = (is_on) ? AQ_STATE_ON : AQ_STATE_OFF;

			if (sen->type == AQ_SENSOR_NOP) {
				err = aq_cond_nop(cond, &s);
			} else {
				err = aq_cond_range(cond, &s);
			}

			if (err < 0) {
				free(cond);
				syslog(LOG_ERR, "%s:%d: Syntax error parsing conditions for a %s sensor",
				       file, lineno, aq_sensor_typename(sen->type));
				exit(EX_DATAERR);
			}

			line->condition = cond;

			HASH_ADD_INT(*cond_ptr, id, cond);
		} else {
			syslog(LOG_ERR, "%s:%d: Unrecognized schedule directive '%s'",
			       file, lineno, tok);
			exit(EX_DATAERR);
		}
	}

	fclose(inf);
	return 0;
}

static enum aq_state aq_eval(struct aq_condition *cond)
{
	uint64_t reading;
	enum aq_state state = AQ_STATE_UNCHANGED;
	int use_state = 0;

	reading = cond->sensor->reading;

	/* Check for invalid sensor reading
	 */
	if (reading == ~0ULL) {
		return state;
	}

	switch (cond->operator) {
	case AQ_COND_INVALID: use_state = 0;
			    break;
	case AQ_COND_LESS:  use_state = (reading < cond->range.reading);
			    break;
	case AQ_COND_LEQUAL:
			    use_state = (reading <= cond->range.reading);
			    break;
	case AQ_COND_EQUAL:
			    use_state = (reading >= cond->range.reading) &&
			                (reading <= (cond->range.reading + cond->range.span));
			    break;
	case AQ_COND_IN:
			    use_state = (reading > cond->range.reading) &&
			                (reading < (cond->range.reading + cond->range.span));
			    break;
	case AQ_COND_AT:
			    use_state = (reading >= cond->range.reading) &&
			                (reading < (cond->range.reading + cond->range.span));
			    break;
	case AQ_COND_NEQUAL:
			    use_state = (reading < cond->range.reading) ||
			                (reading > (cond->range.reading + cond->range.span));
	case AQ_COND_GEQUAL:
			    use_state = (reading >= (cond->range.reading + cond->range.span));
			    break;
	case AQ_COND_GREATER:
			    use_state = (reading > (cond->range.reading + cond->range.span));
			    break;
	}

	if (use_state) {
		state = cond->state;
	}

	return state;
}

/* Get current desired status (on = 1, off = 0) of a device.
 * If return value is < 0, don't change the status of the device
 */
static enum aq_state aq_device_eval(struct aq_device *dev)
{
	struct aq_condition *cond;
	enum aq_state ret = AQ_STATE_UNCHANGED;

	if (dev->override.expire > time(NULL)) {
		return dev->override.state;
	}

	for (cond = dev->conditions; cond != NULL; cond = cond->hh.next) {
		enum aq_state state;

		state = aq_eval(cond);
		if (state != AQ_STATE_UNCHANGED) {
			ret = state;
		}
	}

	return (int)ret;
}

/* Evaluate the schedule
 */
void aq_sched_eval(struct aquaria *aq)
{
	struct aq_device *dev;
	struct aq_sensor *sen;
	enum aq_state state;
	int ret;

	gettimeofday(&reading_time.now, NULL);
	localtime_r(&reading_time.now.tv_sec, &reading_time.localnow);

	/* Update and log all the readings
	 */
	log_start(aq->log, &reading_time.now);
	for (sen = aq->sensors; sen != NULL; sen = sen->hh.next) {
		ret = sen->get_reading(sen->priv, &sen->reading);
		if (ret == 1 && sen->log_id != NULL)
			log_sensor(aq->log, sen->log_id, sen->reading);
	}

	for (dev = aq->devices; dev != NULL; dev = dev->hh.next) {
		state = aq_device_eval(dev);
		if (state != AQ_STATE_UNCHANGED &&
		    dev->state != state) {
			int is_on = (state == AQ_STATE_ON) ? 1 : 0;

			dev->set_state(dev->priv, is_on);
			dev->state = state;
			if (dev->log_id != NULL)
				log_device(aq->log, dev->log_id, is_on);
		}
	}
	log_pause(aq->log);
}

/* Get the first device
 */
struct aq_device *aq_devices(struct aquaria *aq)
{
	return aq->devices;
}

struct aq_device *aq_device_next(struct aq_device *dev)
{
	return dev->hh.next;
}

/* Get a device's name
 */
const char *aq_device_name(struct aq_device *dev)
{
	return dev->name;
}

/* Get current desired status (on = 1, off = 0) of a device.
 */
enum aq_state aq_device_get(struct aq_device *dev, time_t *override)
{
	if (override != NULL) {
		*override = dev->override.expire;
	}
	return dev->state;
}

void aq_device_set(struct aq_device *dev, enum aq_state state, time_t *override)
{
	if (override != NULL) {
		dev->override.expire = time(NULL) + *override;
	} else {
		/* Default override is 10 minutes */
		dev->override.expire = time(NULL) + 10 * 60;
	}
	dev->override.state = state;

	aq_sync(dev->aq, "set-device", dev);
}

/* Get the first device condition
 */
struct aq_condition *aq_device_conditions(struct aq_device *dev)
{
	return dev->conditions;
}

/* Get the next device condition
 */
struct aq_condition *aq_condition_next(struct aq_condition *cond)
{
	return cond->hh.next;
}

/* Get the condition's desired state
 */
enum aq_state aq_condition_state(struct aq_condition *cond)
{
	return cond->state;
}

/* Get the condition's sensor
 */
struct aq_sensor *aq_condition_sensor(struct aq_condition *cond)
{
	return cond->sensor;
}

/* Get the condition's trigger
 */
void aq_condition_trigger(struct aq_condition *cond, enum aq_operator *op, uint64_t *reading, uint64_t *span)
{
	*op = cond->operator;
	*reading = cond->range.reading;
	*span = cond->range.span;
}

/* Get the first sensor
 */
struct aq_sensor *aq_sensors(struct aquaria *aq)
{
	return aq->sensors;
}

struct aq_sensor *aq_sensor_next(struct aq_sensor *sen)
{
	/* Don't show system sensors */
	assert(sen != NULL);

	return sen->hh.next;
}

/* Get a sensor's name
 */
const char *aq_sensor_name(struct aq_sensor *sen)
{
	return sen->name;
}

/* Get current reading of a sensor
 */
enum aq_sensor_type aq_sensor_type(struct aq_sensor *sen)
{
	return sen->type;
}

uint64_t aq_sensor_reading(struct aq_sensor *sen)
{
	return sen->reading;
}
