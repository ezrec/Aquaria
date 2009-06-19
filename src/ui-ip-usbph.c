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
#include <unistd.h>

#include <sys/poll.h>

#include <ip-usbph.h>

#define UI_FUNC(x)	x
#include "ui.h"

static void *ipusbph_open(int argc, char **argv)
{
	struct ip_usbph *ph;
	int err;

	ph = ip_usbph_acquire(0);
	if (ph == NULL) {
		return NULL;
	}
	return ph;
}

static void ipusbph_close(void *ui)
{
	struct ip_usbph *ph = ui;

	ip_usbph_release(ph);
}

static void ipusbph_clear(void *ui)
{
	struct ip_usbph *ph = ui;

	if (ph == NULL)
		return;

	ip_usbph_clear(ph);
}

static void ipusbph_flush(void *ui)
{
	struct ip_usbph *ph = ui;

	ip_usbph_flush(ph);
}

static aq_key ipusbph_keywait(void *ui, unsigned int ms)
{
	struct ip_usbph *ph = ui;
	struct pollfd fds[1];
	int err;
	const aq_key keymap[32] = {	/* The empty spots will have 0 - AQ_NOP */
		[IP_USBPH_KEY_0] = AQ_KEY_0,
		[IP_USBPH_KEY_1] = AQ_KEY_1,
		[IP_USBPH_KEY_2] = AQ_KEY_2,
		[IP_USBPH_KEY_3] = AQ_KEY_3,
		[IP_USBPH_KEY_4] = AQ_KEY_4,
		[IP_USBPH_KEY_5] = AQ_KEY_5,
		[IP_USBPH_KEY_6] = AQ_KEY_6,
		[IP_USBPH_KEY_7] = AQ_KEY_7,
		[IP_USBPH_KEY_8] = AQ_KEY_8,
		[IP_USBPH_KEY_9] = AQ_KEY_9,
		[IP_USBPH_KEY_YES] = AQ_KEY_SELECT,
		[IP_USBPH_KEY_NO] = AQ_KEY_CANCEL,
		[IP_USBPH_KEY_VOL_UP] = AQ_KEY_RIGHT,
		[IP_USBPH_KEY_VOL_DOWN] = AQ_KEY_LEFT,
		[IP_USBPH_KEY_UP] = AQ_KEY_UP,
		[IP_USBPH_KEY_DOWN] = AQ_KEY_DOWN,
		[IP_USBPH_KEY_ASTERISK] = AQ_KEY_PERIOD,
	};

	fds[0].fd = ip_usbph_key_fd(ph);
	fds[0].events = POLLIN | POLLERR;
	fds[0].revents = 0;

	while ((err = poll(fds, 1, ms)) == 1) {
		uint16_t key;

		if (fds[0].revents & POLLERR) {
			break;
		}

		key = ip_usbph_key_get(ph);
		if (key == IP_USBPH_KEY_INVALID) {
			break;
		}

		if ((key & IP_USBPH_KEY_PRESSED) != 0) {
			/* Turn on backlight on keypress */
			ip_usbph_backlight(ui);
			return keymap[key & 0x1f];
		}
	}

	return AQ_KEY_ERROR;
}

static void ipusbph_timestamp(void *ui)
{
	struct ip_usbph *ph = ui;
	struct tm local_now;
	time_t time_now;
	char buff[256];
	int i;
	const ip_usbph_sym day_of_week[7] = {
		IP_USBPH_SYMBOL_SUN,
		IP_USBPH_SYMBOL_MON,
		IP_USBPH_SYMBOL_TUE,
		IP_USBPH_SYMBOL_WED,
		IP_USBPH_SYMBOL_THU,
		IP_USBPH_SYMBOL_FRI,
		IP_USBPH_SYMBOL_SAT,
	};

	time(&time_now);
	localtime_r(&time_now, &local_now);

	snprintf(buff, sizeof(buff), "%2d%2d%2d%02d",
	         local_now.tm_mon, local_now.tm_mday,
	         local_now.tm_hour, local_now.tm_min);

	for (i = 0; buff[i] != 0; i++) {
		ip_usbph_top_digit(ph, i + 3, ip_usbph_font_digit(buff[i]));
	}

	ip_usbph_symbol(ph, IP_USBPH_SYMBOL_COLON, local_now.tm_sec & 1);
	for (i = 0; i < 7; i++) {
		ip_usbph_symbol(ph, day_of_week[i], (i == local_now.tm_wday) ? 1 : 0);
	}

	ip_usbph_symbol(ph, IP_USBPH_SYMBOL_M_AND_D, 1);
	ip_usbph_flush(ph);
}

static void ipusbph_debug(void *ui, const char *fmt, va_list args)
{
	vprintf(fmt, args);
}

static void ipusbph_show_title(void *ui, const char *title)
{
	struct ip_usbph *ph = ui;
	int i;

	ip_usbph_symbol(ph, IP_USBPH_SYMBOL_MAN, 0);
	ip_usbph_symbol(ph, IP_USBPH_SYMBOL_UP, 0);
	ip_usbph_symbol(ph, IP_USBPH_SYMBOL_DOWN, 0);
	for (i = 0; title[i] != 0; i++) {
		ip_usbph_top_char(ph, i, ip_usbph_font_char(title[i]));
	}
}

static void ipusbph_show_sensor(void *ui, const char *title, struct aq_sensor *sen)
{
	struct ip_usbph *ph = ui;
	uint64_t reading;
	double f;
	char buff[10];
	int i;

	ui_show_title(ui, title);

	reading = aq_sensor_reading(sen);
	buff[0] = 0;

	switch (aq_sensor_type(sen)) {
	case AQ_SENSOR_TEMP:
		ip_usbph_symbol(ph, IP_USBPH_SYMBOL_DECIMAL, 1);

		/* Convert from microkelvin to F */
		f = ((reading / 1000000.0) - 273.15) * 9.0 / 5.0 + 32;

		sprintf(buff, "%4d", (int)(f * 10));
		break;
	default:
		break;	/* TODO: Other sensor types */
	}

	for (i = 0; buff[i] != 0; i++) {
		ip_usbph_bot_char(ph, i, ip_usbph_font_char(buff[i]));
	}
	return;
}

static void ipusbph_show_device(void *ui, const char *title, struct aq_device *dev)
{
	struct ip_usbph *ph = ui;
	time_t override;
	int is_on;
	char buff[10];
	time_t now;

	ui_show_title(ui, title);

	is_on = aq_device_get(dev, &override);
	if (is_on) {
		ip_usbph_symbol(ph, IP_USBPH_SYMBOL_UP, 1);
	} else {
		ip_usbph_symbol(ph, IP_USBPH_SYMBOL_DOWN, 1);
	}

	now = time(NULL);
	if (override > now) {
		int i;

		sprintf(buff, "%4d", (int)(override - now));
		for (i = 0; buff[i] != 0; i++) {
			ip_usbph_bot_char(ph, i, ip_usbph_font_char(buff[i]));
		}

		ip_usbph_symbol(ph, IP_USBPH_SYMBOL_MAN, 1);
	}

	return;
}

static struct aquaria_ui ipusbph_ui = {
	.name = "ipusbph",
	.open = ipusbph_open,
	.close = ipusbph_close,
	.keywait = ipusbph_keywait,
	.timestamp = ipusbph_timestamp,
	.debug = ipusbph_debug,
	.show_title = ipusbph_show_title,
	.show_sensor = ipusbph_show_sensor,
	.show_device = ipusbph_show_device,
	.clear = ipusbph_clear,
	.flush = ipusbph_flush,
};

const struct aquaria_ui *aquaria_ui = &ipusbph_ui;
