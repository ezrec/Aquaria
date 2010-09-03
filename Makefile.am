ACLOCAL_AMFLAGS=-I m4
AM_CFLAGS = -include $(top_builddir)/config.h \
	    $(IP_USBPH_CFLAGS) $(JSON_CFLAGS)

lib_LTLIBRARIES = \
	src/libui-null.la \
	src/libui-curses.la \
	src/libui-ip-usbph.la

src_libui_null_la_SOURCES = src/ui-null.c
src_libui_null_la_LDFLAGS = -no-undefined -version-info 0:0:0

src_libui_curses_la_SOURCES = src/ui-curses.c
src_libui_curses_la_LDFLAGS = -no-undefined -version-info 0:0:0

src_libui_ip_usbph_la_SOURCES = src/ui-ip-usbph.c
src_libui_ip_usbph_la_LDFLAGS = -no-undefined -version-info 0:0:0
src_libui_ip_usbph_la_LIBADD  = $(IP_USBPH_LIBS)

bin_PROGRAMS = src/aquaria

src_aquaria_SOURCES = \
	src/aquaria.h \
	src/aquaria.c \
	src/schedule.h \
	src/schedule.c \
	src/ui.h \
	src/ui.c \
	src/ui-tty.c \
	src/log.h \
	src/log.c

src_aquaria_LDADD = $(src_libui_ip_usbph_la_LIBADD) \
		    $(JSON_LIBS) \
		    -ldl
