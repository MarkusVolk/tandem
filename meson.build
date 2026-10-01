project('tandem', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 1.0.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'buildtype=debugoptimized'],
)

app_id = 'de.flk.Tandem'
prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')

cc = meson.get_compiler('c')

glib_dep = dependency('glib-2.0', version: '>= 2.76')
gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')
json_dep = dependency('json-glib-1.0')
gtk_dep = dependency('gtk4', version: '>= 4.14')
adw_dep = dependency('libadwaita-1', version: '>= 1.5')
gnutls_dep = dependency('gnutls')
pulse_dep = dependency('libpulse-mainloop-glib')
wayland_dep = dependency('wayland-client')
xkb_dep = dependency('xkbcommon')
m_dep = cc.find_library('m', required: false)

config_h = configuration_data()
config_h.set_quoted('TANDEM_APP_ID', app_id)
config_h.set_quoted('TANDEM_VERSION', meson.project_version())
config_h.set_quoted('TANDEM_DATADIR', datadir)
config_h.set_quoted('TANDEM_BINDIR', bindir)
configure_file(output: 'tandem-build.h', configuration: config_h)
config_inc = include_directories('.')

add_project_arguments(
  '-DG_LOG_DOMAIN="tandem"',
  '-DG_LOG_USE_STRUCTURED',
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76',
  '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_14',
  '-DADW_VERSION_MIN_REQUIRED=ADW_VERSION_1_5',
  '-Wno-unused-parameter',
  language: 'c',
)

subdir('protocols')
subdir('data')
subdir('src')
