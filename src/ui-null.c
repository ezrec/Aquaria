/*
 * Copyright 2009, Jason S. McMullan <jason.mcmullan@gmail.com>
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

#include "ui.h"

static void *null_open(int argc, char **argv)
{
	return &null_open;
}

static void null_close(void *ui)
{
}

static aq_key null_keywait(void *ui, unsigned int ms)
{
	usleep(ms * 1000);
	return AQ_KEY_NOP;
}

static void null_timestamp(void *ui)
{
}

static void null_debug(void *ui, const char *fmt, va_list args)
{
}

static void null_show_title(void *ui, const char *title)
{
}

static void null_show_sensor(void *ui, const char *title, struct aq_sensor *sen)
{
}

static void null_show_device(void *ui, const char *title, struct aq_device *dev)
{
}

static void null_clear(void *ui)
{
}

static void null_flush(void *ui)
{
}

static struct aquaria_ui null_ui = {
	.name = "null",
	.open = null_open,
	.close = null_close,
	.keywait = null_keywait,
	.timestamp = null_timestamp,
	.debug = null_debug,
	.show_title = null_show_title,
	.show_sensor = null_show_sensor,
	.show_device = null_show_device,
	.clear = null_clear,
	.flush = null_flush,
};

const struct aquaria_ui *aquaria_ui = &null_ui;
