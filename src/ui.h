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

#ifndef UI_H
#define UI_H

#include <stdarg.h>

#include "aquaria.h"

/* Aquaria UI keys defines */
typedef enum {
	AQ_KEY_QUIT = -2,
	AQ_KEY_ERROR = -1,
	AQ_KEY_NOP = 0,
	AQ_KEY_0,		/* Numeric keypad */
	AQ_KEY_1,
	AQ_KEY_2,
	AQ_KEY_3,
	AQ_KEY_4,
	AQ_KEY_5,
	AQ_KEY_6,
	AQ_KEY_7,
	AQ_KEY_8,
	AQ_KEY_9,
	AQ_KEY_PERIOD,	/* . */
	AQ_KEY_UP,
	AQ_KEY_DOWN,
	AQ_KEY_LEFT,
	AQ_KEY_RIGHT,
	AQ_KEY_SELECT,	/* YES, ENTER, etc */
	AQ_KEY_CANCEL,	/* NO, ESC, etc */
} aq_key;

void *ui_create(const char *name, int argc, char **argv);

/* DLL exported functions
 */
struct aquaria_ui {
	const char *name;
	void *(* open)(int argc, char **argv);
	void (* close)(void *ui);

	aq_key (* keywait)(void *ui, unsigned int ms);

	void (* timestamp)(void *ui);
	void (* debug)(void *ui, const char *fmt, va_list va);
	void (* show_title)(void *ui, const char *title);
	void (* show_sensor)(void *ui, const char *title, struct aq_sensor *sen);
	void (* show_device)(void *ui, const char *title, struct aq_device *dev);
	void (* clear)(void *ui);
	void (* flush)(void *ui);
};

extern const struct aquaria_ui *aquaria_ui;

static inline void *ui_open(int argc, char **argv)
{
	return aquaria_ui->open(argc, argv);
}

static inline void ui_close(void *ui)
{
	aquaria_ui->close(ui);
}


static inline aq_key ui_keywait(void *ui, unsigned int ms)
{
	return aquaria_ui->keywait(ui, ms);
}


static inline void ui_timestamp(void *ui)
{
	aquaria_ui->timestamp(ui);
}

static inline void ui_debug(void *ui, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	aquaria_ui->debug(ui, fmt, args);
	va_end(args);
}

static inline void ui_show_title(void *ui, const char *title)
{
	aquaria_ui->show_title(ui, title);
}

static inline void ui_show_sensor(void *ui, const char *title, struct aq_sensor *sen)
{
	aquaria_ui->show_sensor(ui, title, sen);
}

static inline void ui_show_device(void *ui, const char *title, struct aq_device *dev)
{
	aquaria_ui->show_device(ui, title, dev);
}

static inline void ui_clear(void *ui)
{
	aquaria_ui->clear(ui);
}

static inline void ui_flush(void *ui)
{
	aquaria_ui->flush(ui);
}


#endif /* UI_H */
