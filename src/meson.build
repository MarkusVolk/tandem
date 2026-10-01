tandem_sources = files(
  'main.c',
  'app.c',
  'config.c',
  'cert.c',
  'packet.c',
  'util.c',
  'dbus-api.c',
  'cli.c',
  'smoke.c',
  'ui/window.c',
)

tandem_deps = [
  glib_dep,
  gio_dep,
  gio_unix_dep,
  json_dep,
  gtk_dep,
  adw_dep,
  gnutls_dep,
  pulse_dep,
  wayland_dep,
  xkb_dep,
  m_dep,
  protocols_dep,
]

executable('tandem',
  tandem_sources,
  dependencies: tandem_deps,
  include_directories: [config_inc, include_directories('.')],
  install: true,
)
