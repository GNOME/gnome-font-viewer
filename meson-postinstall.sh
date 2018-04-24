#!/bin/sh

# Package managers set this so we don't need to run
if [ -z "$DESTDIR" ]; then
  echo Updating desktop database...
  update-desktop-database -q ${MESON_INSTALL_PREFIX}/share/applications
fi
