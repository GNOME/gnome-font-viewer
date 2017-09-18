project('gnome-font-viewer', 'c',
        version: '3.26.0',
        license: 'GPLv2+',
        meson_version: '>= 0.40.1',
)

cc = meson.get_compiler('c')

gnome = import('gnome')
i18n = import('i18n')

prefix = get_option('prefix')

bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
localedir = join_paths(prefix, get_option('localedir'))

pkgdatadir = join_paths(datadir, meson.project_name())

appdatadir = join_paths(datadir, 'metainfo')
desktopdir = join_paths(datadir, 'applications')
servicesdir = join_paths(datadir, 'dbus-1', 'services')
thumbnailerdir = join_paths(datadir, 'thumbnailers')

glib_req_version = '>= 2.35.1'
gtk_req_version = '>= 3.20.0'
harfbuzz_req_version = '>= 0.9.9'

glib_dep = dependency('glib-2.0', version: glib_req_version)
gtk_dep = dependency('gtk+-3.0', version: gtk_req_version)
harfbuzz_dep = dependency('harfbuzz', version: harfbuzz_req_version)
fontconfig_dep = dependency('fontconfig')
freetype2_dep = dependency('freetype2')
gnomedesktop_dep = dependency('gnome-desktop-3.0')
mathlib_dep = cc.find_library('m')

config_h = configuration_data()
config_h.set_quoted('VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_h.set_quoted('GNOMELOCALEDIR', localedir)
configure_file(output: 'config.h', configuration: config_h)

root_inc = include_directories('.')

appdata_file = 'org.gnome.font-viewer.appdata.xml'
i18n.merge_file(appdata_file,
  input: 'data/' + appdata_file + '.in',
  output: appdata_file,
  po_dir: 'po',
  install: true,
  install_dir: appdatadir
)

subdir('src')
subdir('po')

meson.add_install_script('meson-postinstall.sh')
