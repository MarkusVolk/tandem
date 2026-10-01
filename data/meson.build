gnome = import('gnome')

conf = configuration_data()
conf.set('APP_ID', app_id)
conf.set('BINDIR', bindir)

if build_gui
  configure_file(
    input: 'de.flk.Tandem.desktop.in',
    output: app_id + '.desktop',
    configuration: conf,
    install: true,
    install_dir: datadir / 'applications',
  )

  configure_file(
    input: 'de.flk.Tandem.service.in',
    output: app_id + '.service',
    configuration: conf,
    install: true,
    install_dir: datadir / 'dbus-1/services',
  )
endif

configure_file(
  input: 'de.flk.Tandem.Daemon.service.in',
  output: app_id + '.Daemon.service',
  configuration: conf,
  install: true,
  install_dir: datadir / 'dbus-1/services',
)

systemd_dep = dependency('systemd', required: false)
if systemd_dep.found()
  systemd_user_unit_dir = systemd_dep.get_variable('systemduserunitdir', pkgconfig_define: ['prefix', prefix])
else
  systemd_user_unit_dir = prefix / 'lib/systemd/user'
endif
configure_file(
  input: 'tandem.service.in',
  output: 'tandem.service',
  configuration: conf,
  install: true,
  install_dir: systemd_user_unit_dir,
)

if systemd_dep.found()
  systemd_system_unit_dir = systemd_dep.get_variable('systemdsystemunitdir', pkgconfig_define: ['prefix', prefix])
else
  systemd_system_unit_dir = prefix / 'lib/systemd/system'
endif
configure_file(
  input: 'tandemd.service.in',
  output: 'tandemd.service',
  configuration: conf,
  install: true,
  install_dir: systemd_system_unit_dir,
)
install_data('de.flk.Tandem.Daemon.conf', install_dir: datadir / 'dbus-1/system.d')

install_data('de.flk.Tandem.gschema.xml', install_dir: datadir / 'glib-2.0/schemas')
gnome.compile_schemas(depend_files: files('de.flk.Tandem.gschema.xml'))

install_data('de.flk.Tandem.metainfo.xml', install_dir: datadir / 'metainfo')

install_data('icons/hicolor/scalable/apps/de.flk.Tandem.svg',
  install_dir: datadir / 'icons/hicolor/scalable/apps')
install_data('icons/hicolor/symbolic/apps/de.flk.Tandem-symbolic.svg',
  install_dir: datadir / 'icons/hicolor/symbolic/apps')

gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)
