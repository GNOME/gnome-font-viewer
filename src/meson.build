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
  'sushi-font-widget.h',
  'sushi-font-widget.c',
  'font-view-application.h',
  'font-view-application.c',
  'font-view-window.h',
  'font-view-window.c',
  'main.c',
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
  dependencies: [
    mathlib_dep,
    glib_dep,
    gtk_dep,
    libadwaita_dep,
    harfbuzz_dep,
    fontconfig_dep,
    freetype2_dep,
    fribidi_dep,
  ],
  install: true)
