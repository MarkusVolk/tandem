# Credits

tandem-noctalia is built on the UI library of [Noctalia](https://github.com/noctalia-dev/noctalia)
and borrows application plumbing from [Luciole](https://github.com/noctalia-dev/luciole) and
[Plume](https://github.com/MarkusVolk/plume). Noctalia and Luciole are MIT licensed,
Copyright (c) 2026 noctalia-dev; see `LICENSE.noctalia`.

- `lib/` is an unchanged copy of Noctalia's `lib/` (without `third_party/`), refreshed with
  `tools/sync-lib.sh`. Changes to it belong upstream.
- Adapted from Luciole by way of Plume: `src/app/main_loop.*`, `src/app/palette_watcher.*`,
  `src/event_loop/`, `src/fs/`, `src/platform/desktop_integration.*`,
  `src/platform/launch_activation.*`, `src/ui/palette_file.*`, `src/ui/tooltip_presenter.*`.
- From Plume: `src/app/instance_service.*`, `src/platform/clipboard.*`,
  `src/platform/file_portal.*`, and the dialog of `src/ui/form_dialog.*`.
- `lib/assets/fonts/` carries the Tabler icon font; its license is
  `lib/assets/fonts/tabler-icons-license.txt`.
