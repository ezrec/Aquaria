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

#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdint.h>
#include <time.h>

/* For all sensors, a reading of MAXINT64 is considered invalid.
 */
enum aq_sensor_type {
	AQ_SENSOR_INVALID = -1,
	AQ_SENSOR_NOP = 0,
	AQ_SENSOR_TIME,		/* Reading = micro seconds since local midnight */
	AQ_SENSOR_WEEKDAY,	/* Reading = 0 = sunday, ... 6 = saturday */
	AQ_SENSOR_TEMP,		/* Reading = micro degrees kelvin */
#if 0 /* Unsupported types */
	AQ_SENSOR_PH,		/* Reading = micro ph */
	AQ_SENSOR_FLOW,		/* Reading = micro cubic centimeters/second */
	AQ_SENSOR_PPB,		/* Reading = parts per billion */
	AQ_SENSOR_RATIO,	/* Reading = ratio * 1000000 */
#endif
};

struct aq_sched;
struct aq_device;
struct aq_sensor;

/* Schedule memory management
 */
struct aq_sched *aq_sched_alloc(void);
void aq_sched_free(struct aq_sched *sched);

/* Read the configuration file
 */
int aq_sched_config(struct aq_sched *sched, const char *file);

/* Read/Write the schedule file.
 * These are comments-preserving routines.
 */
int aq_sched_read(struct aq_sched *sched, const char *file);
int aq_sched_write(struct aq_sched *sched, const char *file);

/* Evaluate the schedule
 */
void aq_sched_eval(struct aq_sched *sched);

/* Get the first device
 */
struct aq_device *aq_sched_devices(struct aq_sched *sched);

/* Get the next device
 */
struct aq_device *aq_device_next(struct aq_device *dev);


/* Get a device's name
 */
const char *aq_device_name(struct aq_device *dev);

/* Get a device by name
 */
struct aq_device *aq_device_find(struct aq_sched *sched, const char *name);

/* Get current desired status (on = 1, off = 0) of a device.
 */
int aq_device_get(struct aq_device *dev, time_t *override);
void aq_device_set(struct aq_device *dev, int is_on, time_t *override);

/* Get the first sensor
 */
struct aq_sensor *aq_sched_sensors(struct aq_sched *sched);

/* Get the next sensor
 */
struct aq_sensor *aq_sensor_next(struct aq_sensor *sen);

/* Get a sensor's name
 */
const char *aq_sensor_name(struct aq_sensor *sen);

/* Get a sensor by name
 */
struct aq_sensor *aq_sensor_find(struct aq_sched *sched, const char *name);

/* Get current reading of a sensor
 */
enum aq_sensor_type aq_sensor_type(struct aq_sensor *sen);
uint64_t aq_sensor_reading(struct aq_sensor *sen);


#endif /* SCHEDULE_H */
