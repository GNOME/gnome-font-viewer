resources = gnome.compile_resources(
  'gnome-font-viewer',
  'gnome-font-viewer.gresource.xml',
  gresource_bundle: true,
  install: true,
  install_dir: pkgdatadir,
)

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

conf = configuration_data()
conf.set('APP_ID', application_id)
conf.set('PKGDATADIR', pkgdatadir)
conf.set('VERSION', meson.project_version())
conf.set('LOCALEDIR', localedir)

configure_file(
    input: 'config.rs.in',
    output: 'config.rs',
    configuration: conf
)

build_root = meson.project_build_root()
source_root = meson.project_source_root()

# Copy the config.rs output to the source directory.
run_command(
  'cp',
  build_root / 'src' / 'config.rs',
  source_root / 'src' / 'config.rs',
  check: true
)

cargo_options = [ '--manifest-path', source_root / 'Cargo.toml' ]
cargo_options += [ '--target-dir', build_root / 'src' ]

if get_option('profile') == 'default'
  cargo_options += [ '--release' ]
  rust_target = 'release'
  message('Building in release mode')
else
  rust_target = 'debug'
  message('Building in debug mode')
endif

cargo_env = { 'CARGO_HOME': build_root / 'cargo-home' }

cargo_build = custom_target(
  'cargo-build',
  build_by_default: true,
  build_always_stale: true,
  output: 'src',
  console: true,
  env: cargo_env,
  command: [
    cargo, 'build',
    cargo_options,
  ]
)

copy_binary = custom_target(
  'cp-binary',
  depends: cargo_build,
  build_by_default: true,
  build_always_stale: true,
  install: true,
  install_dir: get_option('bindir'),
  output: meson.project_name(),
  command: [
    'cp',
    'src' / rust_target / meson.project_name(),
    '@OUTPUT@',
  ],
)

cargo_doc = custom_target(
  'cargo-doc',
  build_by_default: false,
  build_always_stale: true,
  output: 'doc',
  console: true,
  command: [
    cargo, 'doc',
    cargo_options + ['--no-deps', '--document-private-items'],
  ],
)