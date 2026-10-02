qt6 = import('qt6')

qt_moc = qt6.compile_moc(
  headers: files('src/daemon.h', 'src/device.h', 'src/frontend.h'),
  dependencies: qt_dep,
)

qt_resources = qt6.compile_resources(sources: files('tandem.qrc'))

executable('tandem-qt',
  files('src/main.cpp', 'src/daemon.cpp', 'src/device.cpp', 'src/frontend.cpp'),
  qt_moc,
  qt_resources,
  cpp_args: ['-DTANDEM_VERSION="@0@"'.format(meson.project_version())],
  override_options: ['cpp_std=c++20'],
  dependencies: qt_dep,
  install: true,
)

configure_file(
  input: 'data/de.flk.Tandem.desktop',
  output: app_id + '.desktop',
  copy: true,
  install: true,
  install_dir: datadir / 'applications',
)

qt_conf = configuration_data()
qt_conf.set('BINDIR', bindir)
configure_file(
  input: 'data/de.flk.Tandem.service.in',
  output: app_id + '.service',
  configuration: qt_conf,
  install: true,
  install_dir: datadir / 'dbus-1/services',
)
