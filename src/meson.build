daemon_sources = files(
  'main.c',
  'app.c',
  'config.c',
  'cert.c',
  'packet.c',
  'util.c',
  'link.c',
  'handshake.c',
  'lan.c',
  'device.c',
  'manager.c',
  'plugin.c',
  'notify.c',
  'transfer.c',
  'sni.c',
  'dbusmenu.c',
  'mdns.c',
  'frontend.c',
  'mpris-watch.c',
  'input.c',
  'input-portal.c',
  'input-uinput.c',
  'vcard.c',
  'clipboard.c',
  'dbus-api.c',
  'smoke.c',
  'plugins/ping.c',
  'plugins/battery.c',
  'plugins/connectivity.c',
  'plugins/findmyphone.c',
  'plugins/lock.c',
  'plugins/runcommand.c',
  'plugins/notifications.c',
  'plugins/share.c',
  'plugins/mpriscontrol.c',
  'plugins/mprisremote.c',
  'plugins/mousepad.c',
  'plugins/presenter.c',
  'plugins/contacts.c',
  'plugins/telephony.c',
  'plugins/sms.c',
  'plugins/clipboard.c',
  'plugins/sendnotifications.c',
  'plugins/remoteinput.c',
)

daemon_deps = [glib_dep, gio_dep, gio_unix_dep, json_dep, gnutls_dep, xkb_dep, m_dep]

if pulse_dep.found()
  daemon_sources += files('pulse.c', 'plugins/systemvolume.c')
  daemon_deps += pulse_dep
endif
if gst_dep.found()
  daemon_deps += gst_dep
endif
if wayland_dep.found()
  daemon_sources += files('wayland.c', 'input-wlr.c')
  daemon_deps += [wayland_dep, protocols_dep]
endif

executable('tandemd',
  daemon_sources,
  dependencies: daemon_deps,
  include_directories: [config_inc, include_directories('.')],
  install: true,
)

executable('tandem',
  files('cli-main.c', 'cli.c', 'util.c', 'frontend.c'),
  dependencies: [glib_dep, gio_dep, gio_unix_dep],
  include_directories: [config_inc, include_directories('.')],
  install: true,
)
