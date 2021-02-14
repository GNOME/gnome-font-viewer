appdatadir = join_paths(datadir, 'metainfo')
appdata_file = 'org.gnome.font-viewer.appdata.xml'
merged_appdata = i18n.merge_file(appdata_file,
  input: appdata_file + '.in',
  output: appdata_file,
  po_dir: '../po',
  install: true,
  install_dir: appdatadir
)

appstream_util = find_program('appstream-util', required: false)
if appstream_util.found()
  test(
    'validate-appdata', appstream_util,
    args: [
      'validate-relax', '--nonet', merged_appdata.full_path()
    ]
  )
endif

subdir('icons')
