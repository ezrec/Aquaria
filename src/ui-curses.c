/*
 * Aquarium Power Manager
 * ncurses user interface
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

#include <curses.h>
#undef clear

#include "ui.h"

static void *curses_open(int argc, char **argv)
{
	initscr();	/* Fullscreen */
	cbreak();	/* Character at a time */
	noecho();	/* No local echo */

	nonl();		/* No newline */
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);

	return stdscr;
}

static void curses_close(void *ui)
{
}

static void curses_clear(void *ui)
{
	WINDOW *win = ui;

	werase(win);
}

static void curses_flush(void *ui)
{
	WINDOW *win = ui;

	wrefresh(win);
}

#ifndef ARRAY_SIZE
#define  ARRAY_SIZE(x)	(sizeof(x)/sizeof(x[0]))
#endif

static aq_key curses_keywait(void *ui, unsigned int ms)
{
	WINDOW *win = ui;
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
		{ .ch = KEY_SELECT, .key = AQ_KEY_SELECT },
		{ .ch = 13 /* Enter */, .key = AQ_KEY_SELECT },
		{ .ch = 'z', .key = AQ_KEY_SELECT },
		{ .ch = KEY_CANCEL, .key = AQ_KEY_CANCEL },
		{ .ch = '\b' /* Backspace */, .key = AQ_KEY_CANCEL },
		{ .ch = 'x', .key = AQ_KEY_CANCEL },
		{ .ch = KEY_RIGHT, .key = AQ_KEY_RIGHT },
		{ .ch = KEY_LEFT, .key = AQ_KEY_LEFT },
		{ .ch = KEY_UP, .key = AQ_KEY_UP },
		{ .ch = KEY_DOWN, .key = AQ_KEY_DOWN },
		{ .ch = '.', .key = AQ_KEY_PERIOD },
	};

	wtimeout(win, ms);
	ch = wgetch(win);
	if (ch == ERR) {
		return AQ_KEY_ERROR;
	}

	for (i = 0; i < ARRAY_SIZE(keymap); i++) {
		if (keymap[i].ch == ch) {
			return keymap[i].key;
		}
	}

	return AQ_KEY_NOP;
}

static void curses_timestamp(void *ui)
{
	WINDOW *win = ui;
	struct tm local_now;
	time_t time_now;
	char buff[256];
	int i;
	const chtype day_of_week[7] = {
		'S', 'M', 'T', 'W', 'T', 'F', 'S'
	};

	time(&time_now);
	localtime_r(&time_now, &local_now);

	snprintf(buff, sizeof(buff), "%2d-%2d %2d:%02d",
	         local_now.tm_mon, local_now.tm_mday,
	         local_now.tm_hour, local_now.tm_min);

	wmove(win, 0, 0);
	for (i = 0; buff[i] != 0; i++) {
		waddch(win, A_BOLD | buff[i]);
	}

	wmove (win, 1, 0);
	for (i = 0; i < 7; i++) {
		waddch(win, ((i == local_now.tm_wday) ? 0 : A_REVERSE) | day_of_week[i]);
	}
}

static void curses_debug(void *ui, const char *fmt, va_list va)
{
	WINDOW *win = ui;
	int i;
	char buff[PATH_MAX];

	vsnprintf(buff, sizeof(buff), fmt, va);

	wmove(win, 10, 0);
	for (i = 0; buff[i] != 0; i++) {
		waddch(win, A_REVERSE | buff[i]);
	}

}

static void curses_show_title(void *ui, const char *title)
{
	WINDOW *win = ui;
	int i;

	wmove(win, 3, 0);
	for (i = 0; title[i] != 0; i++) {
		waddch(win, title[i]);
	}
}

static void curses_show_sensor(void *ui, const char *title, struct aq_sensor *sen)
{
	WINDOW *win = ui;
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

		sprintf(buff, "%.1f F", f);
		break;
	default:
		break;	/* TODO: Other sensor types */
	}

	wmove(win, 5, 4);
	for (i = 0; buff[i] != 0; i++) {
		waddch(win, buff[i]);
	}
}

static void curses_show_device(void *ui, const char *title, struct aq_device *dev)
{
	WINDOW *win = ui;
	time_t override;
	int is_on;
	char buff[10];
	time_t now;

	ui_show_title(ui, title);

	is_on = aq_device_get(dev, &override);
	wmove(win, 5, 0);
	if (is_on) {
		waddch(win, '1');
	} else {
		waddch(win, '0');
	}

	now = time(NULL);
	if (override > now) {
		int i;

		wmove(win, 5, 2);
		waddch(win, 'M');
		wmove(win, 5, 4);
		sprintf(buff, "%4d", (int)(override - now));
		for (i = 0; buff[i] != 0; i++) {
			waddch(win, buff[i]);
		}
	}
}

static struct aquaria_ui curses_ui = {
	.name = "curses",
	.open = curses_open,
	.close = curses_close,
	.keywait = curses_keywait,
	.timestamp = curses_timestamp,
	.debug = curses_debug,
	.show_title = curses_show_title,
	.show_sensor = curses_show_sensor,
	.show_device = curses_show_device,
	.clear = curses_clear,
	.flush = curses_flush,
};

const struct aquaria_ui *aquaria_ui = &curses_ui;
