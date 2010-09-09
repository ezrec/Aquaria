ACLOCAL_AMFLAGS=-I m4
AM_CFLAGS = -include $(top_builddir)/config.h \
	    $(IP_USBPH_CFLAGS) $(JSON_CFLAGS) \
	    -Wall -Werror

initdir=$(sysconfdir)/init

init_DATA = etc/init/aquaria.conf \
            etc/init/aquaria-ip-usbph.conf

$(init_DATA): Makefile
	rm -f $@ $@.tmp
	srcdir=''; \
	   test -f ./$@.in || srcdir=$(srcdir)/; \
	   sed -e 's|@bindir[@]|$(bindir)|g' \
	      $${srcdir}$@.in >$@.tmp
	mv $@.tmp $@

etc/init/aquaria.conf: etc/init/aquaria.conf.in
etc/init/aquaria-ip-usbph.conf: etc/init/aquaria-ip-usbph.conf.in

noinst_LIBRARIES = src/libaquaria.a

bin_PROGRAMS = \
	src/aquaria \
	src/aquaria-ip-usbph \
	src/aquaria-tty \
	src/aquaria-curses

src_libaquaria_a_SOURCES = \
	src/aquaria.h \
	src/aquaria.c \
	src/aq_server.c \
	src/aq_server.h \
	src/log.h \
	src/log.c

src_aquaria_SOURCES = \
	src/server.c

src_aquaria_LDADD = \
	src/libaquaria.a \
	$(JSON_LIBS)

src_aquaria_ip_usbph_SOURCES = \
	src/ui-ip-usbph.c \
	src/ui.h \
	src/ui.c

src_aquaria_ip_usbph_LDADD = \
	src/libaquaria.a \
	$(IP_USBPH_LIBS) \
	$(JSON_LIBS)

src_aquaria_curses_SOURCES = \
	src/ui-curses.c \
	src/ui.h \
	src/ui.c

src_aquaria_curses_LDADD = \
	src/libaquaria.a \
	$(JSON_LIBS) \
	-lncurses

src_aquaria_tty_SOURCES = \
	src/ui-tty.c \
	src/ui.h \
	src/ui.c

src_aquaria_tty_LDADD = \
	src/libaquaria.a \
	$(JSON_LIBS)
