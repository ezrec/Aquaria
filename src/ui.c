/*
 * Copyright 2009, Netronome Systems
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
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
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>

#include "ui.h"

/* The 'aquaria_ui' pointer is in the
 * default linked-in UI.
 */

void *ui_create(const char *name, int argc, char **argv)
{
	void *dl, *ui;
	char buff[PATH_MAX];

	if (name == NULL) {
		/* Use the default UI */
		return ui_open(argc, argv);
	}

	snprintf(buff, sizeof(buff), "libui-%s.so", name);

	dl = dlopen(buff, RTLD_NOW | RTLD_LOCAL);
	if (dl == NULL || name != NULL) {
		return ui_create(NULL, argc, argv);
	}

	ui = dlsym(dl, "aquaria_ui");
	if (ui == NULL) {
		return ui_create(NULL, argc, argv);
	}

	aquaria_ui = ui;

	return ui_open(argc, argv);
}
