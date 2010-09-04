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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <search.h>
#include <sys/time.h>

#include "aquaria.h"
#include "log.h"

struct log {
	FILE *file;
	struct hsearch_data hash;
	int wire_id;
	enum { LOG_STATE_INIT, LOG_STATE_ACTIVE } state;
};

/* Open/close the log (VCD format)
 */
struct log *log_open(const char *path)
{
	struct log *log;
	FILE *file;
	int err;

	file = fopen(path, "w");
	if (file == NULL)
		return NULL;

	log = calloc(1, sizeof(*log));
	log->file = file;
	log->state = LOG_STATE_INIT;
	log->wire_id = 1;
	err = hcreate_r(256, &log->hash);
	if (err == 0) {
		fclose(file);
		free(log);
		return NULL;
	}

	fprintf(file, "$version Aquaria Aquarium Controller $end\n");
	fprintf(file, "$timescale 1 ns $end\n");
	fprintf(file, "$scope module Aquaria $end\n");

	return log;
}

void log_close(struct log *log)
{
	hdestroy_r(&log->hash);
	fclose(log->file);
	free(log);
}

void *log_register_sensor(struct log *log, const char *name, enum aq_sensor_type type)
{
	ENTRY ent, *pent = NULL;
	char buff[32];
	int err;

	snprintf(buff, sizeof(buff), "%X", log->wire_id++);
	ent.key = strdup(name);
	ent.data = strdup(buff);

	assert(log->state == LOG_STATE_INIT);

	err = hsearch_r(ent, ENTER, &pent, &log->hash);
	assert(err != 0);

	fprintf(log->file, "$var real 64 %s Sensor.%s $end\n", buff, name);

	return pent;
}

void *log_register_device(struct log *log, const char *name)
{
	ENTRY ent, *pent = NULL;
	char buff[32];
	int err;

	snprintf(buff, sizeof(buff), "%X", log->wire_id++);
	ent.key = strdup(name);
	ent.data = strdup(buff);

	assert(log->state == LOG_STATE_INIT);

	err = hsearch_r(ent, ENTER, &pent, &log->hash);
	assert(err != 0);

	fprintf(log->file, "$var wire 1 %s Device.%s $end\n", buff, name);

	return pent;
}

/* Mark the start of a log entry
 */
int log_start(struct log *log, struct timeval *tv)
{
	if (log->state == LOG_STATE_INIT) {
		log->state = LOG_STATE_ACTIVE;
		fprintf(log->file, "$upscope $end\n");
		fprintf(log->file, "$enddefinitions $end\n");
	}

	fprintf(log->file, "#%u%06u\n", (unsigned int)tv->tv_sec, (unsigned int)tv->tv_usec * 1000);
	return 0;
}

/* Mark a sensor reading
 */
int log_sensor(struct log *log, void *id, uint64_t reading)
{
	ENTRY *ent = id;

	fprintf(log->file, "r%.16g %s\n", (double)reading, (const char *)ent->data);
	return 0;
}

/* Mark a device state change
 */
int log_device(struct log *log, void *id, int is_on)
{
	ENTRY *ent = id;

	fprintf(log->file, "b%d %s\n", is_on ? 1 : 0, (const char *)ent->data);
	return 0;
}

/* End the log entry
 */
int log_pause(struct log *log)
{
	fflush(log->file);
	return 0;
}
