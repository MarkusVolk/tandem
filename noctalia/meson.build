cpp = meson.get_compiler('cpp')

if cpp.get_define('_LIBCPP_VERSION', prefix: '#include <version>') != ''
  add_project_arguments('-fexperimental-library', language: 'cpp')
  add_project_link_arguments('-lc++experimental', language: 'cpp')
endif

if not cpp.has_header('stb/stb_image.h')
  error('tandem-noctalia: stb/stb_image.h not found')
endif

wayland_protocols = dependency('wayland-protocols', version: '>=1.49')
noctalia_scanner = find_program('wayland-scanner')
noctalia_protocol_root = wayland_protocols.get_variable('pkgdatadir')
noctalia_protocol_sources = []
foreach protocol : [
  ['xdg-decoration-unstable-v1', 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
  ['xdg-foreign-unstable-v2', 'unstable/xdg-foreign/xdg-foreign-unstable-v2.xml'],
  ['primary-selection-unstable-v1', 'unstable/primary-selection/primary-selection-unstable-v1.xml'],
]
  header = custom_target(protocol[0] + '-header',
    input: noctalia_protocol_root / protocol[1],
    output: protocol[0] + '-client-protocol.h',
    command: [noctalia_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  source = custom_target(protocol[0] + '-source',
    input: noctalia_protocol_root / protocol[1],
    output: protocol[0] + '-protocol.c',
    command: [noctalia_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    depends: header,
  )
  noctalia_protocol_sources += [header, source]
endforeach

subdir('lib')

tomlplusplus_dep = dependency('tomlplusplus', version: '>=3.4')

noctalia_build_config = configuration_data()
noctalia_build_config.set_quoted('TANDEM_VERSION', meson.project_version())
noctalia_build_config.set_quoted('TANDEM_INSTALL_ASSETSDIR', datadir / 'tandem/assets')
noctalia_build_info = configure_file(
  input: 'src/app/build_info.h.in',
  output: 'tandem_build_info.h',
  configuration: noctalia_build_config,
)
noctalia_inc = include_directories('src')

# The daemon client knows nothing about the toolkit, so the tests run it without a window.
daemon_client_lib = static_library('tandem-daemon-client',
  files('src/daemon/client.cpp', 'src/daemon/variant.cpp'),
  include_directories: noctalia_inc,
  dependencies: [gio_dep],
)
daemon_client_dep = declare_dependency(
  link_with: daemon_client_lib, include_directories: noctalia_inc, dependencies: [gio_dep],
)

executable('tandem-noctalia',
  files(
    'src/app/application.cpp',
    'src/app/instance_service.cpp',
    'src/app/main_loop.cpp',
    'src/app/palette_watcher.cpp',
    'src/app/presenter_window.cpp',
    'src/app/window.cpp',
    'src/event_loop/glib_poll_source.cpp',
    'src/fs/directory_watch.cpp',
    'src/fs/xdg_dirs.cpp',
    'src/main.cpp',
    'src/pages/device_page.cpp',
    'src/pages/plugin_groups.cpp',
    'src/pages/preferences_page.cpp',
    'src/pages/sms_page.cpp',
    'src/platform/clipboard.cpp',
    'src/platform/desktop_integration.cpp',
    'src/platform/file_portal.cpp',
    'src/platform/launch_activation.cpp',
    'src/platform/stb_image_decoder.cpp',
    'src/ui/form_dialog.cpp',
    'src/ui/palette_file.cpp',
    'src/ui/rows.cpp',
    'src/ui/tooltip_presenter.cpp',
  ) + noctalia_protocol_sources + [noctalia_build_info],
  include_directories: noctalia_inc,
  dependencies: [
    daemon_client_dep, noctalia_ui_dep, gio_unix_dep, tomlplusplus_dep,
    dependency('wayland-client'), dependency('wayland-cursor'), dependency('threads'),
  ],
  install: true,
)

install_subdir(noctalia_ui_assets_dir / 'fonts', install_dir: datadir / 'tandem/assets')
install_data('data/colors.toml', install_dir: datadir / 'tandem/templates')
install_data('LICENSE.noctalia', install_dir: datadir / 'licenses/tandem')

if meson.can_run_host_binaries()
  subdir('tests')
endif
