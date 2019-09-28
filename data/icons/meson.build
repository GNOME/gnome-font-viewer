scalable_icondir = join_paths('hicolor', 'scalable', 'apps')
install_data (
  join_paths(scalable_icondir, '@0@.svg'.format(application_id)),
  install_dir: join_paths(datadir, 'icons', scalable_icondir)
)

symbolic_icondir = join_paths('hicolor', 'symbolic', 'apps')
install_data (
  join_paths(symbolic_icondir, 'org.gnome.font-viewer-symbolic.svg'),
  install_dir: join_paths(datadir, 'icons', symbolic_icondir),
  rename: ['@0@-symbolic.svg'.format(application_id)]
)
