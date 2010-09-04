/*
 * Copyright (C) 2010, Jason S. McMullan. All rights reserved.
 * Author: Jason S. McMullan <jason.mcmullan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <limits.h>
#include <string.h>
#include <assert.h>

#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include "aquaria.h"
#include "ui.h"

#define debug(fmt, args...)	do { if (debug_ui != NULL) ui_debug(debug_ui, fmt ,##args); } while (0)

struct menu {
	struct node {
		const char *title;
		enum { NODE_NONE, NODE_MENU, NODE_DEVICE, NODE_SENSOR } type;
		union {
			struct aq_device *device;
			struct aq_sensor *sensor;
			struct menu *menu;
		} sub;
		struct node *prev, *next;
	} **node;
	int nodes;
	struct node *parent;
};

const struct aquaria_ui *aquaria_ui;

struct node top_node = {
	.title = "",
	.type = NODE_NONE,
	.next = &top_node,
	.prev = &top_node,
};

static struct node *menu_mkpath(struct node *node, const char *path)
{
	struct menu *menu;
	char buff[PATH_MAX];
	char *cp;
	int i;

	if (node->type == NODE_NONE) {
		node->type = NODE_MENU;
		node->sub.menu = calloc(1, sizeof(struct menu));
	}

	assert(node->type == NODE_MENU);

	if (path[0] == 0) {
		return node;
	}

	while (*path && *path == '.') path++;

	strncpy(buff, path, PATH_MAX);
	cp = strchr(buff, '.');
	if (cp != NULL) {
		*cp = 0;
		cp++;
	}

	menu = node->sub.menu;
	for (i = 0; i < menu->nodes; i++) {
		if (strcasecmp(menu->node[i]->title, buff) == 0)
			break;
	}

	if (i == menu->nodes) {
		menu->nodes++;
		menu->node = realloc(menu->node, menu->nodes*sizeof(menu->node[0]));
		menu->node[i] = malloc(sizeof(struct node));
		menu->node[i]->type = NODE_NONE;
		menu->node[i]->title = strdup(buff);

		/* Maintain cyclical prev/next links */
		menu->node[i]->next = menu->node[0];
		menu->node[i]->prev = menu->node[i == 0 ? 0 : (i-1)];
		menu->node[0]->prev = menu->node[i];
		menu->node[i]->prev->next = menu->node[i];
	}

	if (cp != NULL) {
		if (menu->node[i]->type == NODE_NONE) {
			menu->node[i]->type = NODE_MENU;
			menu->node[i]->sub.menu = calloc(1, sizeof(*menu));
		}
		assert(menu->node[i]->type == NODE_MENU);
		return menu_mkpath(menu->node[i], cp);
	}

	return menu->node[i];
}

#ifdef MENU_DEBUG
void menu_dump(char *buff, struct node *node)
{
	struct menu *menu;

	printf("%s%s", buff, node->title);
	switch (node->type) {
	case NODE_DEVICE: printf(" (device %s)\n", aq_device_name(node->sub.device)); break;
	case NODE_SENSOR: printf(" (sensor %s)\n", aq_sensor_name(node->sub.sensor)); break;
	case NODE_NONE:  printf(" (none)\n"); break;
	case NODE_MENU: {
		char *cp;
		int i;

		cp = buff + strlen(buff);
		printf(" (menu)\n");
		menu = node->sub.menu;
		for (i = 0; i < menu->nodes; i++) {
			strcat(cp, node->title);
			strcat(cp, ".");
			menu_dump(buff, menu->node[i]);
			*cp = 0;
		}
		}
		break;
	default:
		printf(" (?)\n");
		break;
	}
}
#endif

/* NOTE: Yes, this is a very retarded setup, with all the
 *       realloc() and sorting. We only do it once, and
 *       unless you have a few hundred devices, it really
 *       doesn't matter.
 */
static void menu_setup(struct aquaria *aq)
{
	struct aq_device *dev;
	struct aq_sensor *sen;
	struct node *node;
	int count, i;
	const char **names;

	/* Find all sensors, sort, and place */
	count = 0;
	for (sen = aq_sensors(aq); sen != NULL; sen = aq_sensor_next(sen)) count++;

	names = malloc(sizeof(char*) * count);
	i = 0;
	for (sen = aq_sensors(aq); sen != NULL; sen = aq_sensor_next(sen)) {
		names[i++] = aq_sensor_name(sen);
	}
	qsort(names, count, sizeof(char *), (int (*)(const void *,const void *))strcasecmp);
	for (i = 0; i < count; i ++) {
		node = menu_mkpath(&top_node, names[i]);
		assert(node->type == NODE_NONE);
		node->type = NODE_SENSOR;
		node->sub.sensor = aq_sensor_find(aq, names[i]);
	}

	/* Find all devices, sort, and place */
	count = 0;
	for (dev = aq_devices(aq); dev != NULL; dev = aq_device_next(dev)) count++;

	names = realloc(names, sizeof(char*) * count);
	i = 0;
	for (dev = aq_devices(aq); dev != NULL; dev = aq_device_next(dev)) {
		names[i++] = aq_device_name(dev);
	}
	qsort(names, count, sizeof(char *), (int (*)(const void *,const void *))strcasecmp);
	for (i = 0; i < count; i ++) {
		node = menu_mkpath(&top_node, names[i]);
		assert(node->type == NODE_NONE);
		node->type = NODE_DEVICE;
		node->sub.device = aq_device_find(aq, names[i]);
	}

#ifdef MENU_DEBUG
	{
		char buff[PATH_MAX] = "";
		menu_dump(buff, &top_node);
	}
#endif
}

static void show_menu(void *ui, const char *menu_path)
{
	struct node *node;

	ui_clear(ui);
	ui_timestamp(ui);
	node = menu_mkpath(&top_node, menu_path);
	switch (node->type) {
	case NODE_SENSOR:
		ui_show_sensor(ui, node->title, node->sub.sensor);
		break;
	case NODE_DEVICE:
		ui_show_device(ui, node->title, node->sub.device);
		break;
	case NODE_MENU:
		ui_show_title(ui, node->title);
		break;
	default:
		break;
	}
	ui_flush(ui);
}


void handle_key(char menu_path[PATH_MAX], aq_key key)
{
	struct node *node;
	char *cp;

	if (menu_path[0] == 0) {
		key = AQ_KEY_RIGHT;
	}

	switch (key) {
	case AQ_KEY_LEFT:
	case AQ_KEY_4:
		cp = strrchr(menu_path, '.');
		if (cp == NULL) {
			cp = &menu_path[0];
		}
		*cp = 0;
		break;
	case AQ_KEY_RIGHT:
	case AQ_KEY_6:
		node = menu_mkpath(&top_node, menu_path);
		if (node->type == NODE_MENU && node->sub.menu->nodes > 0) {
			strcat(menu_path, ".");
			strcat(menu_path, node->sub.menu->node[0]->title);
		}
		break;
	case AQ_KEY_DOWN:
	case AQ_KEY_8:
		node = menu_mkpath(&top_node, menu_path);
		node = node->next;
		cp = strrchr(menu_path, '.');
		assert(cp != NULL);
		cp++;
		*cp = 0;
		strcpy(cp, node->title);
		break;
	case AQ_KEY_UP:
	case AQ_KEY_2:
		node = menu_mkpath(&top_node, menu_path);
		node = node->prev;
		cp = strrchr(menu_path, '.');
		assert(cp != NULL);
		cp++;
		*cp = 0;
		strcpy(cp, node->title);
		break;
	case AQ_KEY_SELECT:
		node = menu_mkpath(&top_node, menu_path);
		if (node->type == NODE_DEVICE) {
			int is_on = aq_device_get(node->sub.device, NULL);
			aq_device_set(node->sub.device, !is_on, NULL);
		}
		break;
	case AQ_KEY_CANCEL:
		node = menu_mkpath(&top_node, menu_path);
		if (node->type == NODE_DEVICE) {
			time_t done = 0;
			int is_on = aq_device_get(node->sub.device, NULL);
			aq_device_set(node->sub.device, is_on, &done);
		}
		break;
	default:
		break;
	}

	return;
}

int ui_mainloop(struct aquaria *aq, void *ui)
{
	char menu_path[PATH_MAX] = {};

	while (1) {
		aq_key key;

		key = ui_keywait(ui, 1000);	/* Wait up to 1 second */
		aq_sync(aq);
		if (key > AQ_KEY_NOP) {
			handle_key(menu_path, key);
		}

		show_menu(ui, menu_path);
	}
}

int main(int argc, char **argv)
{
	int err;
	struct aquaria *aq;
	struct sockaddr_in sin;
	void *ui;

	sin.sin_family = AF_INET;
	sin.sin_port = htons(4444);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	aq = aq_connect((const struct sockaddr *)&sin, sizeof(sin));
	menu_setup(aq);

	ui = ui_open(argc, argv);
	if (ui == NULL)
		return EXIT_FAILURE;

	err = ui_mainloop(aq, ui);
	aq_free(aq);
	if (err < 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
