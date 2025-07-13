subdir('icons')

metainfodir = join_paths(datadir, 'metainfo')
metainfo_file = 'org.gnome.font-viewer.metainfo.xml'
merged_metainfo = i18n.merge_file(
  input: metainfo_file + '.in',
  output: metainfo_file,
  po_dir: '../po',
  install: true,
  install_dir: metainfodir
)

appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test(
    'validate-metainfo',
    appstreamcli,
    args: ['validate', '--no-net', '--explain', merged_metainfo.full_path()]
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
