AM_CPPFLAGS =					\
	$(FONTVIEW_CFLAGS)			\
	-DDIRECTORY_DIR=\"$(directorydir)\" 	\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"

bin_PROGRAMS = \
	gnome-thumbnail-font \
	gnome-font-viewer

resource_files = $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gnome-font-viewer.gresource.xml)
font-view-resources.c: gnome-font-viewer.gresource.xml $(resource_files)
	$(AM_V_GEN)glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-source --c-name font_view $(srcdir)/gnome-font-viewer.gresource.xml
font-view-resources.h: gnome-font-viewer.gresource.xml $(resource_files)
	$(AM_V_GEN)glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-header --c-name font_view $(srcdir)/gnome-font-viewer.gresource.xml

BUILT_SOURCES = \
	font-view-resources.c		\
	font-view-resources.h		\
	$(NULL)

font_loader_SOURCES = \
	sushi-font-loader.h \
	sushi-font-loader.c

gnome_thumbnail_font_LDADD = \
	-lm \
	$(FONTVIEW_LIBS)

gnome_thumbnail_font_SOURCES = \
	$(font_loader_SOURCES) \
	font-thumbnailer.c \
	totem-resources.c \
	totem-resources.h

gnome_font_viewer_LDADD = \
	-lm \
	$(FONTVIEW_LIBS)

gnome_font_viewer_SOURCES = \
	$(BUILT_SOURCES) \
	$(font_loader_SOURCES) \
	font-model.h \
	font-model.c \
	font-utils.h \
	font-utils.c \
	sushi-font-widget.h \
	sushi-font-widget.c \
	font-view.c

desktopdir = $(datadir)/applications
desktop_in_files = org.gnome.font-viewer.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
%.desktop: %.desktop.in Makefile
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_in_files = org.gnome.font-viewer.service.in
dbusservice_DATA = $(service_in_files:.service.in=.service)

org.gnome.font-viewer.service: org.gnome.font-viewer.service.in
	$(AM_V_GEN)     \
		[ -d $(@D) ] || $(mkdir_p) $(@D) ; \
		sed -e "s|\@bindir\@|$(bindir)|" $< > $@.tmp && mv $@.tmp $@

thumbnailerdir = $(datadir)/thumbnailers
thumbnailer_DATA = gnome-font-viewer.thumbnailer

install-desktop-database: install-desktopDATA
	update-desktop-database $(DESTDIR)$(desktopdir)

uninstall-local:
	rm -f $(DESTDIR)$(desktopdir)/mimeinfo.cache

CLEANFILES = \
	$(dbusservice_DATA) \
	$(desktop_DATA) \
	$(directory_DATA)

EXTRA_DIST = \
	$(desktop_in_files) \
	$(resource_files) \
	$(dbusservice_in_files) \
	gnome-font-viewer.thumbnailer \
	gnome-font-viewer.gresource.xml

-include $(top_srcdir)/git.mk
