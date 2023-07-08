subdir('icons')

appdatadir = join_paths(datadir, 'metainfo')
appdata_file = 'org.gnome.font-viewer.appdata.xml'
merged_appdata = i18n.merge_file(
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

desktop_file = 'org.gnome.font-viewer.desktop'

desktop_conf = configuration_data()
desktop_conf.set('APPLICATION_ID', application_id)
i18n.merge_file(
  input: configure_file(input: desktop_file + '.in.in',
    output: desktop_file + '.in',
    configuration: desktop_conf),
  output: desktop_file,
  po_dir: '../po',
  install: true,
  install_dir: desktopdir,
  type: 'desktop'
)

service_file = 'org.gnome.font-viewer.service'

service_conf = configuration_data()
service_conf.set('bindir', bindir)
service_conf.set('application_id', application_id)
configure_file(input: service_file + '.in',
  output: '@0@.service'.format(application_id),
  configuration: service_conf,
  install: true,
  install_dir: servicesdir)

install_data('gnome-font-viewer.thumbnailer',
  install_dir: thumbnailerdir)