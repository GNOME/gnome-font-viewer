project('gnome-font-viewer', 'c',
        version: '43.0',
        license: 'GPLv2+',
        meson_version: '>= 0.50.0',
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

desktopdir = join_paths(datadir, 'applications')
servicesdir = join_paths(datadir, 'dbus-1', 'services')
thumbnailerdir = join_paths(datadir, 'thumbnailers')

glib_req_version = '>= 2.56.0'
gtk_req_version = '>= 4.5.0'
libadwaita_req_version = '>= 1.2.alpha'
harfbuzz_req_version = '>= 0.9.9'

glib_dep = dependency('glib-2.0', version: glib_req_version)
gtk_dep = dependency('gtk4', version: gtk_req_version)
libadwaita_dep = dependency('libadwaita-1', version: libadwaita_req_version)
harfbuzz_dep = dependency('harfbuzz', version: harfbuzz_req_version)
fontconfig_dep = dependency('fontconfig')
freetype2_dep = dependency('freetype2')
gnomedesktop_dep = dependency('gnome-desktop-4')
fribidi_dep = dependency('fribidi')
mathlib_dep = cc.find_library('m')

if get_option('profile') == 'development'
  profile = 'Devel'
else
  profile = ''
endif

application_id = 'org.gnome.font-viewer@0@'.format(profile)

config_h = configuration_data()
config_h.set_quoted('VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_h.set_quoted('GNOMELOCALEDIR', localedir)
config_h.set_quoted('APPLICATION_ID', application_id)
configure_file(output: 'config.h', configuration: config_h)

root_inc = include_directories('.')

subdir('src')
subdir('po')
subdir('data')

meson.add_install_script('meson-postinstall.sh')
