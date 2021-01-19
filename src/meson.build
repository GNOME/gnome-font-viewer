loader_sources = [
  'sushi-font-loader.h',
  'sushi-font-loader.c',
]

thumbnail_sources = [
  loader_sources,
  'font-thumbnailer.c',
  'totem-resources.c',
  'totem-resources.h',
]

viewer_sources = [
  loader_sources,
  'font-model.h',
  'font-model.c',
  'font-utils.h',
  'font-utils.c',
  'sushi-font-widget.h',
  'sushi-font-widget.c',
  'font-view.c',
]

resources = gnome.compile_resources('font-view-resources',
  'gnome-font-viewer.gresource.xml',
  source_dir: '.',
  c_name: 'font_view')

executable('gnome-thumbnail-font', thumbnail_sources,
  include_directories: [ root_inc, include_directories('.') ],
  dependencies: [ mathlib_dep, glib_dep, gtk_dep, freetype2_dep],
  install: true)

executable('gnome-font-viewer', viewer_sources + resources,
  include_directories: [ root_inc, include_directories('.') ],
  dependencies: [ mathlib_dep, glib_dep, gtk_dep, libhandy_dep, harfbuzz_dep, fontconfig_dep, freetype2_dep, gnomedesktop_dep ],
  install: true)

desktop_file = 'org.gnome.font-viewer.desktop'

desktop_conf = configuration_data()
desktop_conf.set('VERSION', meson.project_version())
desktop_conf.set('APPLICATION_ID', application_id)
i18n.merge_file(desktop_file,
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
