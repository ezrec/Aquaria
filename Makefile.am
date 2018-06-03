
initdir=$(sysconfdir)/init
initddir=$(sysconfdir)/init.d

init_DATA = etc/init/aquaria.conf \
            etc/init/aquaria-ip-usbph.conf

initd_SCRIPTS = \
            etc/init.d/aquaria

$(init_DATA): Makefile
	rm -f $@ $@.tmp
	srcdir=''; \
	   test -f ./$@.in || srcdir=$(srcdir)/; \
	   sed -e 's|@bindir[@]|$(bindir)|g' \
	      $${srcdir}$@.in >$@.tmp
	mv $@.tmp $@

$(initd_DATA): Makefile
	rm -f $@ $@.tmp
	srcdir=''; \
	   test -f ./$@.in || srcdir=$(srcdir)/; \
	   sed -e 's|@bindir[@]|$(bindir)|g' \
	      $${srcdir}$@.in >$@.tmp
	mv $@.tmp $@

etc/init.d/aquaria: etc/init.d/aquaria.in
etc/init/aquaria.conf: etc/init/aquaria.conf.in
etc/init/aquaria-ip-usbph.conf: etc/init/aquaria-ip-usbph.conf.in

SUBDIRS=src
