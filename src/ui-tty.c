/*
 * Aquarium Power Manager
 * IP-USBPH user interface
 *
 * Copyright 2009, Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 * GPL v2.0
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <syslog.h>
#include <unistd.h>

#include <sys/poll.h>

#include "ui.h"

static void *tty_open(int argc, char **argv)
{
	return fopen("/dev/tty", "r+");
}

static void tty_close(void *ui)
{
	FILE *tty = ui;
	fclose(tty);
}

static void tty_clear(void *ui)
{
	FILE *tty = ui;
	fprintf(tty, "clear: \r\n");
}

static void tty_flush(void *ui)
{
	FILE *tty = ui;
	fprintf(tty, "flush: \r\n");
}

#ifndef ARRAY_SIZE
#define  ARRAY_SIZE(x)	(sizeof(x)/sizeof(x[0]))
#endif

static aq_key tty_keywait(void *ui, unsigned int ms)
{
	FILE *tty = ui;
	int i, ch;
	struct {
		int ch;
		aq_key key;
	} keymap[] = {
		{ .ch = '0', .key = AQ_KEY_0 },
		{ .ch = '1', .key = AQ_KEY_1 },
		{ .ch = '2', .key = AQ_KEY_2 },
		{ .ch = '3', .key = AQ_KEY_3 },
		{ .ch = '4', .key = AQ_KEY_4 },
		{ .ch = '5', .key = AQ_KEY_5 },
		{ .ch = '6', .key = AQ_KEY_6 },
		{ .ch = '7', .key = AQ_KEY_7 },
		{ .ch = '8', .key = AQ_KEY_8 },
		{ .ch = '9', .key = AQ_KEY_9 },
		{ .ch = 's', .key = AQ_KEY_SELECT },
		{ .ch = 13 /* Enter */, .key = AQ_KEY_SELECT },
		{ .ch = 27 /* Esc */, .key = AQ_KEY_CANCEL },
		{ .ch = '\b' /* Backspace */, .key = AQ_KEY_CANCEL },
		{ .ch = 'l', .key = AQ_KEY_RIGHT },
		{ .ch = 'h', .key = AQ_KEY_LEFT },
		{ .ch = 'k', .key = AQ_KEY_UP },
		{ .ch = 'j', .key = AQ_KEY_DOWN },
		{ .ch = '.', .key = AQ_KEY_PERIOD },
	};

	ch = fgetc(tty);
	if (ch < 0) {
		return AQ_KEY_ERROR;
	}

	for (i = 0; i < ARRAY_SIZE(keymap); i++) {
		if (keymap[i].ch == ch) {
			return keymap[i].key;
		}
	}

	return AQ_KEY_NOP;
}

static void tty_timestamp(void *ui)
{
	FILE *tty = ui;
	struct tm local_now;
	time_t time_now;
	char buff[256];
	int i;
	const char *day_of_week[7] = {
		"Sat", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};

	time(&time_now);
	localtime_r(&time_now, &local_now);

	snprintf(buff, sizeof(buff), "%2d-%2d %2d:%02d",
	         local_now.tm_mon, local_now.tm_mday,
	         local_now.tm_hour, local_now.tm_min);

	fprintf(tty, "timestamp: %s (%s)\r\n", buff, day_of_week[local_now.tm_wday]);
}

static void tty_debug(void *ui, const char *fmt, va_list va)
{
	FILE *tty = ui;

	vfprintf(tty, fmt, va);
}


static void tty_show_title(void *ui, const char *title)
{
	FILE *tty = ui;

	fprintf(tty, "show_title: %s\r\n", title);
}

static void tty_show_sensor(void *ui, const char *title, struct aq_sensor *sen)
{
	FILE *tty = ui;
	uint64_t reading;
	double f;
	char buff[10];
	int i;

	ui_show_title(ui, title);

	reading = aq_sensor_reading(sen);
	buff[0] = 0;

	switch (aq_sensor_type(sen)) {
	case AQ_SENSOR_TEMP:
		/* Convert from microkelvin to F */
		f = ((reading / 1000000.0) - 273.15) * 9.0 / 5.0 + 32;

		sprintf(buff, "%.1f", f);
		break;
	default:
		break;	/* TODO: Other sensor types */
	}

	fprintf(tty, "show_sensor: %s\r\n", buff);
}

static void tty_show_device(void *ui, const char *title, struct aq_device *dev)
{
	FILE *tty = ui;
	int is_on;
	time_t override;
	time_t now;

	ui_show_title(ui, title);

	is_on = aq_device_get(dev, &override);
	fprintf(tty, "show_device: %s\r\n", is_on ? "on" : "off");

	now = time(NULL);
	if (override > now) {
		fprintf(tty, "show_device: Override %4d", (int)(override - now));
	}
}

static struct aquaria_ui tty_ui = {
	.name = "tty",
	.open = tty_open,
	.close = tty_close,
	.keywait = tty_keywait,
	.timestamp = tty_timestamp,
	.debug = tty_debug,
	.show_title = tty_show_title,
	.show_sensor = tty_show_sensor,
	.show_device = tty_show_device,
	.clear = tty_clear,
	.flush = tty_flush,
};

const struct aquaria_ui *aquaria_ui = &tty_ui;
