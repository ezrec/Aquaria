/*
 * Aquarium Power Manager
 * Configuration files are in /etc/Aquaria/schedule
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

#include "aquaria.h"
#include "schedule.h"
#include "ui.h"

const char *DOTDIR="/etc/Aquaria";

const char *SCHEDULE="schedule";

void *debug_ui;

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
static void menu_setup(struct aq_sched *sched)
{
	struct aq_device *dev;
	struct aq_sensor *sen;
	struct node *node;
	int count, i;
	const char **names;

	/* Find all sensors, sort, and place */
	count = 0;
	for (sen = aq_sched_sensors(sched); sen != NULL; sen = aq_sensor_next(sen)) count++;

	names = malloc(sizeof(char*) * count);
	i = 0;
	for (sen = aq_sched_sensors(sched); sen != NULL; sen = aq_sensor_next(sen)) {
		names[i++] = aq_sensor_name(sen);
	}
	qsort(names, count, sizeof(char *), (int (*)(const void *,const void *))strcasecmp);
	for (i = 0; i < count; i ++) {
		node = menu_mkpath(&top_node, names[i]);
		assert(node->type == NODE_NONE);
		node->type = NODE_SENSOR;
		node->sub.sensor = aq_sensor_find(sched, names[i]);
	}

	/* Find all devices, sort, and place */
	count = 0;
	for (dev = aq_sched_devices(sched); dev != NULL; dev = aq_device_next(dev)) count++;

	names = realloc(names, sizeof(char*) * count);
	i = 0;
	for (dev = aq_sched_devices(sched); dev != NULL; dev = aq_device_next(dev)) {
		names[i++] = aq_device_name(dev);
	}
	qsort(names, count, sizeof(char *), (int (*)(const void *,const void *))strcasecmp);
	for (i = 0; i < count; i ++) {
		node = menu_mkpath(&top_node, names[i]);
		assert(node->type == NODE_NONE);
		node->type = NODE_DEVICE;
		node->sub.device = aq_device_find(sched, names[i]);
	}

#ifdef MENU_DEBUG
	{
		char buff[PATH_MAX] = "";
		menu_dump(buff, &top_node);
	}
#endif
}

#if 0
	system("curl --user admin:1234 --silent \"http://$POWER_SWITCH/outlet?".$Switch{$class}->{$name}->{'id'}."=".$state."\" >/dev/null");

sub power_config {
	open(CONF, "curl --user admin:1234 --silent http://$POWER_SWITCH/admin.cgi |") or die "Can't open port to read config!";

	my $id=1;
	my $label;
	while (<CONF>) {
		if ($_ =~ /^\<td\>(.*)\<\/td\>/) {
			$label = $1;
			my ($class,$name) = split(/:/, $label, 2);

			$Switch{$class}->{$name} = { 'id'=>$id };
			$id++;
		}
	}
	close(CONF);
}
#endif

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

void log_exit_reason(int exit_code, void *priv)
{
	if (exit_code != 0) {
		syslog(LOG_ERR, "Abnormal exit code %d", exit_code);
	} else {
		syslog(LOG_NOTICE, "Shutting down");
	}
}

int main(int argc, char **argv)
{
	void *ui;
	struct aq_sched *sched;
	char menu_path[PATH_MAX];
	int err;

	openlog("aquaria", LOG_CONS | LOG_PERROR, LOG_USER);
	on_exit(log_exit_reason, NULL);

	err = chdir("examples");
//	err = chdir("/etc/aquaria");
	if (err < 0) {
		exit(EXIT_FAILURE);
	}
	sched = aq_sched_alloc();
	aq_sched_config(sched, "config");
	aq_sched_read(sched, "schedule");

	menu_setup(sched);
	ui = ui_create(NULL, argc, argv);
	debug_ui = ui;
	assert(ui != NULL);

	while (1) {
		aq_key key;

		key = ui_keywait(ui, 1000);	/* Wait up to 1 second */
		if (key > AQ_KEY_NOP) {
			handle_key(menu_path, key);
		}

		show_menu(ui, menu_path);
		aq_sched_eval(sched);
	}

	return EXIT_SUCCESS;
}
