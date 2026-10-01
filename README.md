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

The service is D-Bus activated (`de.flk.Tandem`) and keeps running in the background with a
status notifier item whose menu offers the common actions per device. Closing the window only
hides it; `tandem quit` or the tray menu stops the service.

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
under `/de/flk/Tandem/devices/<id>` (`de.flk.Tandem.Device`).

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
