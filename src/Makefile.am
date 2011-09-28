INCLUDES =					\
	$(FONTVIEW_CFLAGS)			\
	-DDIRECTORY_DIR=\"$(directorydir)\" 	\
	-DGNOMELOCALEDIR=\"$(datadir)/locale\"

bin_PROGRAMS = \
	gnome-thumbnail-font \
	gnome-font-viewer

ftstream_SOURCES = \
	ftstream-vfs.h \
	ftstream-vfs.c

gnome_thumbnail_font_LDADD = \
	$(FONTVIEW_LIBS)

gnome_thumbnail_font_SOURCES = \
	$(ftstream_SOURCES) \
	font-thumbnailer.c \
	totem-resources.c \
	totem-resources.h

gnome_font_viewer_LDADD = \
	$(FONTVIEW_LIBS)

gnome_font_viewer_SOURCES = \
	$(ftstream_SOURCES) \
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
