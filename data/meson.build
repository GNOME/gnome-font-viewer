i18n = import('i18n')

appdatadir = join_paths(datadir, 'metainfo')
appdata_file = 'org.gnome.font-viewer.appdata.xml'
i18n.merge_file(appdata_file,
  input: appdata_file + '.in',
  output: appdata_file,
  po_dir: '../po',
  install: true,
  install_dir: appdatadir
)

subdir('icons')
