ACLOCAL_AMFLAGS=-I m4
AM_CFLAGS = -include $(top_builddir)/config.h \
	    $(IP_USBPH_CFLAGS) $(JSON_CFLAGS) \
	    -Wall -Werror

include_HEADERS = aquaria.h

lib_LTLIBRARIES = libaquaria.la

bin_PROGRAMS = \
	aquaria \
	aquaria-ip-usbph \
	aquaria-tty \
	aquaria-curses

libaquaria_la_SOURCES = \
	aquaria.h \
	aquaria.c \
	aq_server.c \
	aq_server.h \
	log.h \
	log.c

aquaria_SOURCES = \
	server.c

aquaria_LDADD = \
	libaquaria.la \
	$(JSON_LIBS)

aquaria_ip_usbph_SOURCES = \
	ui-ip-usbph.c \
	ui.h \
	ui.c

aquaria_ip_usbph_LDADD = \
	libaquaria.la \
	$(IP_USBPH_LIBS) \
	$(JSON_LIBS)

aquaria_curses_SOURCES = \
	ui-curses.c \
	ui.h \
	ui.c

aquaria_curses_LDADD = \
	libaquaria.la \
	$(JSON_LIBS) \
	-lncurses

aquaria_tty_SOURCES = \
	ui-tty.c \
	ui.h \
	ui.c

aquaria_tty_LDADD = \
	libaquaria.la \
	$(JSON_LIBS)
