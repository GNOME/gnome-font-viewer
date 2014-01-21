appdatadir = $(datadir)/appdata
appdata_in_files = gnome-font-viewer.appdata.xml.in
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
@INTLTOOL_XML_RULE@

EXTRA_DIST = 				\
	$(appdata_in_files)

CLEANFILES = \
	$(appdata_DATA)
