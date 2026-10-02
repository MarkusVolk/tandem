# Tandem

Tandem connects a Linux desktop with phones running [KDE Connect](https://kdeconnect.kde.org/).
It speaks the KDE Connect protocol (version 8) and is written in C with GLib and GIO.

It comes in three parts:

- `tandemd`, a background service without any toolkit dependency. It does the networking,
  pairing and all plugins, exports a status notifier item with a menu, announces itself over
  mDNS and offers a D-Bus API (`de.flk.Tandem.Daemon`) that the `tandem` command line tool and
  the frontends use.
- `tandem-gtk`, a GTK 4 and libadwaita frontend (`de.flk.Tandem`) for the window, dialogs and
  the presenter overlay.
- `tandem-kirigami`, a Qt 6 and Kirigami frontend under `qt/` with the same pages and the same
  application id. Only one of the two frontends is installed at a time.

Other frontends can be written against the same D-Bus API.

The daemon talks only to standard desktop services: `org.freedesktop.Notifications`, MPRIS,
UPower, `org.freedesktop.login1`, PipeWire's PulseAudio interface, the
`org.freedesktop.portal.RemoteDesktop` portal or the wlroots virtual input protocols for
remote input, and `ext-data-control-v1` for the clipboard. It opens its own Wayland connection
for the latter two, so it runs under any compositor and also without one (remote input then
uses `/dev/uinput`, and the clipboard is not synchronised).

## Building

```
meson setup build
meson compile -C build
meson install -C build
```

Dependencies of the daemon: glib-2.0, gio-unix-2.0, json-glib-1.0, gnutls and xkbcommon.
Optional, each behind a meson feature option: libpulse-mainloop-glib (`-Dpulse`, system volume
and muting during calls), gstreamer-1.0 (`-Dgstreamer`, ring tone), wayland-client,
wayland-protocols and wayland-scanner (`-Dwayland`, wlroots remote input and clipboard). The
frontend needs gtk4 and libadwaita-1 (`-Dgui`). At run time: glib-networking and xdg-user-dirs;
Avahi, xdg-desktop-portal and sound-theme-freedesktop are optional.

The Kirigami frontend is a separate CMake project following KDE conventions. Build the daemon
with `-Dgui=disabled`, then:

```
cmake -S qt -B build-qt
cmake --build build-qt
cmake --install build-qt
```

It needs Qt 6 (Core, Gui, Qml, Quick, QuickControls2, DBus, Widgets), extra-cmake-modules and
KF6 Kirigami; at run time also Kirigami Addons and qqc2-desktop-style.

## Running

```
systemctl --user enable --now tandem.service
tandem                 # open the window
tandem list            # devices
tandem --smoke-test    # check the build and the host without a phone
```

The daemon is D-Bus activated (`de.flk.Tandem.Daemon`) and keeps running in the background with
a status notifier item whose menu offers the common actions per device. `tandem` without a
command, `tandem show` and the tray menu open the frontend; `tandem quit` or the tray menu stop
the daemon.

## Without a desktop session

`tandemd.service` is a system unit for appliances such as a Kodi box without a user session: it
puts the daemon on the system bus (`DBUS_SESSION_BUS_ADDRESS` pointing at the system socket, with
the shipped bus policy) and stores settings with the GSettings keyfile backend. Point
`XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME` at persistent storage in a drop-in, pair
with `tandem accept <device>` over SSH, and remote input goes through `/dev/uinput`. The `tandem`
tool falls back to the system bus when there is no session bus.

## What works

| Plugin | Direction | Notes |
|---|---|---|
| ping, battery, connectivity | both | battery from UPower |
| notifications | phone to desktop | actions and inline replies, icons as payload |
| sendnotifications | desktop to phone | captured with `BecomeMonitor`, own notifications excluded |
| clipboard | both | `ext-data-control-v1`, falls back to `GdkClipboard` |
| share | both | files, text and URLs; drop files onto the device page |
| findmyphone | both | rings through GStreamer with `phone-incoming-call.oga` |
| lock | both | `org.freedesktop.login1` |
| runcommand | phone runs desktop commands | commands kept in GSettings |
| mpriscontrol | phone controls desktop players | MPRIS over D-Bus, album art as payload |
| mprisremote | desktop controls phone players | exported as `org.mpris.MediaPlayer2.tandem.<device>.<player>` |
| systemvolume | phone controls desktop volume | libpulse |
| mousepad, presenter | phone controls desktop input | RemoteDesktop portal, wlroots virtual keyboard and pointer, or uinput |
| remoteinput | desktop types on the phone | text and special keys |
| telephony | phone to desktop | caller notification with contact photo, mute, pause media |
| contacts | phone to desktop | vCards under `$XDG_DATA_HOME/tandem/<device>/contacts` |
| sms | both | conversation list, chat page, attachments, sending |

Remote input picks the first backend that works: the RemoteDesktop portal (GNOME, KDE), the
`zwp_virtual_keyboard_v1` and `zwlr_virtual_pointer_v1` protocols (wlroots compositors), or
`/dev/uinput` when the user may write to it. The choice can be forced in the preferences.

## Command line

```
tandem list | my-id | refresh | monitor | show | quit
tandem pair|accept|reject|unpair|key <dev>
tandem ping <dev> [message]       tandem ring <dev>
tandem share <dev> <file>...      tandem share <dev> <url>
tandem text <dev> <text>          tandem clipboard <dev>
tandem lock|unlock <dev>          tandem keys <dev> <text>
tandem sms <dev> <number> <text>  tandem notifications|dismiss <dev> [id]
tandem commands|add-command|remove-command|run <dev> ...
tandem players <dev>
```

Devices are addressed by id, id prefix or name. The same operations are available on D-Bus
under `/de/flk/Tandem/devices/<id>` (`de.flk.Tandem.Device`); `GetPluginState` and
`CallPlugin` expose every plugin's state and actions generically, which is what the frontend
uses.

## State

```
$XDG_CONFIG_HOME/tandem/{certificate.pem,privateKey.pem,device-id}
$XDG_CONFIG_HOME/tandem/trusted/<device>/{certificate.pem,info.ini}
$XDG_DATA_HOME/tandem/<device>/contacts/*.vcf
$XDG_CACHE_HOME/tandem/<device>/{icons,albumart,photos,sms-attachments}
```

Settings live in GSettings under `de.flk.Tandem` and, per device, under
`de.flk.Tandem.Device` at `/de/flk/Tandem/devices/<id>/`.

## License

GPL-2.0-or-later. The Wayland protocol files under `protocols/` are MIT licensed and come
from wlroots and wlr-protocols.
