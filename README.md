# Tandem

Tandem connects a Linux desktop with phones running [KDE Connect](https://kdeconnect.kde.org/).
It speaks the KDE Connect protocol (version 8) and is written in C with GLib, GTK 4 and
libadwaita. It runs as a D-Bus activated background service with a status notifier item,
opens a window on demand and can be driven from the command line.

Tandem talks only to standard desktop services: `org.freedesktop.Notifications`, MPRIS,
UPower, `org.freedesktop.login1`, PipeWire's PulseAudio interface, the
`org.freedesktop.portal.RemoteDesktop` portal or the wlroots virtual input protocols for
remote input, and `ext-data-control-v1` for the clipboard. It needs no plugin loader, no
database and no Evolution Data Server.

## Building

```
meson setup build
meson compile -C build
meson install -C build
```

Dependencies: glib-2.0, gio-unix-2.0, json-glib-1.0, gtk4, libadwaita-1, gnutls,
libpulse-mainloop-glib, wayland-client, wayland-protocols, xkbcommon and wayland-scanner at
build time; glib-networking and xdg-user-dirs at run time. Avahi, xdg-desktop-portal and
sound-theme-freedesktop are optional.

## Running

```
systemctl --user enable --now tandem.service
tandem                 # open the window
tandem list            # devices
tandem --smoke-test    # check the build and the host without a phone
```

## License

GPL-2.0-or-later. The Wayland protocol files under `protocols/` are MIT licensed and come
from wlroots and wlr-protocols.
