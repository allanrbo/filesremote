DISTCHECK_CONFIGURE_FLAGS = --disable-icon-framing --enable-png-creation

SUBDIRS = 16x16 22x22 32x32 scalable po

THEME_IN_FILES = index.theme.in

@INTLTOOL_THEME_RULE@

theme_DATA = index.theme

EXTRA_DIST=				\
	$(THEME_IN_FILES)		\
	svg2png.sh.in			\
	intltool-update.in 		\
	intltool-merge.in 		\
	intltool-extract.in

# we don't want to install mo files, all translations are already stored
# in theme files
install-data-local:
	$(MAKE) -C po uninstall
	if [ -h $(DESTDIR)$(themedir) ]; then \
		rm -f $(DESTDIR)$(themedir); \
	fi
	if [ -d $(DESTDIR)$(themedir) ]; then \
		touch $(DESTDIR)$(themedir); \
	fi

CLEANFILES = 				\
	index.theme

DISTCLEANFILES = 			\
	svg2png.sh			\
	intltool-extract		\
	intltool-merge			\
	intltool-update

MAINTAINERCLEANFILES =			\
	Makefile.in			\
	aclocal.m4			\
	configure			\
	install-sh			\
	intltool-extract.in		\
	intltool-merge.in		\
	intltool-update.in		\
	missing				\
	mkinstalldirs

