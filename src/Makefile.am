INCLUDES =					\
	$(FONTVIEW_CFLAGS)			\
	-DDIRECTORY_DIR=\"$(directorydir)\" 	\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"

bin_PROGRAMS = \
	gnome-thumbnail-font \
	gnome-font-viewer

font_loader_SOURCES = \
	sushi-font-loader.h \
	sushi-font-loader.c

gnome_thumbnail_font_LDADD = \
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
	$(font_loader_SOURCES) \
	font-model.h \
	font-model.c \
	gd-main-toolbar.h \
	gd-main-toolbar.c \
	sushi-font-widget.h \
	sushi-font-widget.c \
	font-view.c

desktopdir = $(datadir)/applications
desktop_in_files = gnome-font-viewer.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

thumbnailerdir = $(datadir)/thumbnailers
thumbnailer_DATA = gnome-font-viewer.thumbnailer

install-desktop-database: install-desktopDATA
	update-desktop-database $(DESTDIR)$(desktopdir)

uninstall-local:
	rm -f $(DESTDIR)$(desktopdir)/mimeinfo.cache

@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_DIRECTORY_RULE@

CLEANFILES = $(desktop_in_files) $(desktop_DATA) \
 $(directory_DATA)

EXTRA_DIST = \
  gnome-font-viewer.thumbnailer \
  gnome-font-viewer.desktop.in.in

-include $(top_srcdir)/git.mk
