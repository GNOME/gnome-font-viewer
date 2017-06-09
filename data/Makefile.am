appdatadir = $(datadir)/appdata
appdata_in_files = org.gnome.font-viewer.appdata.xml.in
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
%.xml: %.xml.in Makefile
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

EXTRA_DIST = 				\
	$(appdata_in_files)

CLEANFILES = \
	$(appdata_DATA)
