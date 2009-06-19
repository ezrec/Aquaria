lib_LTLIBRARIES = \
	libui-null.la \
	libui-curses.la \
	libui-ip-usbph.la

libui_null_la_SOURCES = ui-null.c
libui_null_la_LDFLAGS = -no-undefined -version-info 0:0:0

libui_curses_la_SOURCES = ui-curses.c
libui_curses_la_LDFLAGS = -no-undefined -version-info 0:0:0

libui_ip_usbph_la_SOURCES = ui-ip-usbph.c
libui_ip_usbph_la_LDFLAGS = -no-undefined -version-info 0:0:0
libui_ip_usbph_la_LIBADD  = -lip-usbph

bin_PROGRAMS = aquaria

aquaria_SOURCES = \
	aquaria.h aquaria.c \
	schedule.h schedule.c \
	ui.h ui.c ui-tty.c

aquaria_CFLAGS = -g3 -O0

aquaria_LDADD = $(libui_ip_usbph_la_LIBADD) -ldl
