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

struct aq_sensor;
struct aq_device;

/* Aquaria UI keys defines */
typedef enum {
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

#endif /* AQUARIA_H */
