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

#ifndef LOG_H
#define LOG_H

#include <sys/time.h>

#include "schedule.h"

struct log;

/* Open/close the log
 */
struct log *log_open(const char *path);
void log_close(struct log *log);

void *log_register_sensor(struct log *log, const char *name, enum aq_sensor_type type);
void *log_register_device(struct log *log, const char *name);

/* Mark the start of a log entry
 */
int log_start(struct log *log, struct timeval *tv);

/* Mark a sensor reading
 */
int log_sensor(struct log *log, void *id, uint64_t reading);

/* Mark a device state change
 */
int log_device(struct log *log, void *id, int is_on);

/* End the log entry
 */
int log_pause(struct log *log);

#endif /* LOG_H */
