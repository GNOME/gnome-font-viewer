dnl -*- mode: m4 -*-
AC_PREREQ(2.63)
AC_INIT([gnome-font-viewer], [3.10.0],
	[http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-font-viewer],
	[gnome-font-viewer],
	[http://live.gnome.org/GnomeUtils])

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([1.11 dist-xz no-dist-gzip foreign])
AM_SILENT_RULES([yes])

GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",[The name of the gettext domain])
IT_PROG_INTLTOOL(0.40.0)
PKG_PROG_PKG_CONFIG([0.22])

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_ISC_POSIX
AC_HEADER_STDC

LT_PREREQ([2.2.6])
LT_INIT([disable-static])

## don't rerun to this point if we abort
AC_CACHE_SAVE

GLIB_GSETTINGS

GLIB_MIN_VERSION=2.35.1
GTK_MIN_VERSION=3.11.4

PKG_CHECK_MODULES(FONTVIEW,
                  fontconfig
                  freetype2
                  gnome-desktop-3.0
                  gio-2.0 >= $GLIB_MIN_VERSION
                  glib-2.0 >= $GLIB_MIN_VERSION
                  gtk+-3.0 >= $GTK_MIN_VERSION)

AC_CONFIG_FILES([
Makefile
src/Makefile
src/gnome-font-viewer.desktop.in
po/Makefile.in
])

AC_OUTPUT

dnl ==========================================================================
echo "

        GNOME Font Viewer $VERSION
        ==========================

        prefix:    ${prefix}
        compiler:  ${CC}

        Now type 'make' to build $PACKAGE
"
