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

#ifndef AQUARIA_H
#define AQUARIA_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <netinet/in.h>

/* For all sensors, a reading of MAXINT64 is considered invalid.
 */
enum aq_sensor_type {
	AQ_SENSOR_INVALID = -1,
	AQ_SENSOR_NOP = 0,
	AQ_SENSOR_TIME,		/* Reading = micro seconds since local midnight */
	AQ_SENSOR_WEEKDAY,	/* Reading = 0 = sunday, ... 6 = saturday */
	AQ_SENSOR_TEMP,		/* Reading = micro degrees kelvin */
#if 0 /* Unsupported types */
	AQ_SENSOR_PH,		/* Reading = micro pH */
	AQ_SENSOR_FLOW,		/* Reading = micro cubic centimeters/second */
	AQ_SENSOR_PPB,		/* Reading = parts per billion */
	AQ_SENSOR_RATIO,	/* Reading = ratio * 1000000 */
#endif
};

enum aq_state {
	AQ_STATE_UNCHANGED = -1,
	AQ_STATE_OFF = 0,
	AQ_STATE_ON = 1,
};

enum aq_operator {
	AQ_COND_INVALID = 0,
	AQ_COND_LESS,	/* sensor < range */
	AQ_COND_LEQUAL,	/* sensor <= range */
	AQ_COND_EQUAL,	/* sensor = [range] */
	AQ_COND_IN,	/* sensor = (range) */
	AQ_COND_AT,	/* sensor = [range) */
	AQ_COND_NEQUAL,	/* sensor != range */
	AQ_COND_GEQUAL, /* sensor >= range */
	AQ_COND_GREATER, /* sensor > range */
};

struct aquaria;
struct aq_device;
struct aq_sensor;
struct aq_condition;

/* Instantiation
 */
struct aquaria *aq_connect(const struct sockaddr *sin, socklen_t len);
struct aquaria *aq_create(const char *log, int noop);

void aq_free(struct aquaria *aq);

/* server: Read the configuration file
 */
int aq_config_read(struct aquaria *aq, const char *file);

/* server: Read the schedule file.
 */
int aq_sched_read(struct aquaria *aq, const char *file);

/* server: Evaluate the schedule
 */
void aq_sched_eval(struct aquaria *aq);

/* Refresh sensor and device states (for client connections,
 * unneeded on server)
 */
int aq_sync(struct aquaria *aq, const char *request, const struct aq_device *dev);

/* Get the first device
 */
struct aq_device *aq_devices(struct aquaria *aq);

/* Get the next device
 */
struct aq_device *aq_device_next(struct aq_device *dev);

/* Get a device's name
 */
const char *aq_device_name(struct aq_device *dev);

/* Get a device by name
 */
struct aq_device *aq_device_find(struct aquaria *aq, const char *name);

/* Get current desired state of a device.
 */
enum aq_state aq_device_get(struct aq_device *dev, time_t *override);
void aq_device_set(struct aq_device *dev, enum aq_state, time_t *override);

/* Get the first device condition
 */
struct aq_condition *aq_device_conditions(struct aq_device *dev);

/* Get the next device condition
 */
struct aq_condition *aq_condition_next(struct aq_condition *cond);

/* Get the condition's desired state
 */
enum aq_state aq_condition_state(struct aq_condition *cond);

/* Get the condition's sensor
 */
struct aq_sensor *aq_condition_sensor(struct aq_condition *cond);

/* Get the condition's trigger
 */
void aq_condition_trigger(struct aq_condition *cond, enum aq_operator *op, uint64_t *reading, uint64_t *span);

/* Get the first sensor
 */
struct aq_sensor *aq_sensors(struct aquaria *aq);

/* Get the next sensor
 */
struct aq_sensor *aq_sensor_next(struct aq_sensor *sen);

/* Get a sensor's name
 */
const char *aq_sensor_name(struct aq_sensor *sen);

/* Get a sensor by name
 */
struct aq_sensor *aq_sensor_find(struct aquaria *aq, const char *name);

/* Get current reading of a sensor
 */
enum aq_sensor_type aq_sensor_type(struct aq_sensor *sen);
uint64_t aq_sensor_reading(struct aq_sensor *sen);

/* Type to name mappings
 */
enum aq_sensor_type aq_sensor_nametype(const char *name);

const char *aq_sensor_typename(enum aq_sensor_type type);

/* Type to units mappings */
const char *aq_sensor_typeunits(enum aq_sensor_type type);

#endif /* AQUARIA_H */
