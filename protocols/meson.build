wayland_scanner = find_program('wayland-scanner', native: true)
wayland_protocols_dep = dependency('wayland-protocols', version: '>= 1.39')
wayland_protocols_dir = wayland_protocols_dep.get_variable('pkgdatadir')

protocol_xmls = [
  files('virtual-keyboard-unstable-v1.xml'),
  files('wlr-virtual-pointer-unstable-v1.xml'),
  wayland_protocols_dir / 'staging/ext-data-control/ext-data-control-v1.xml',
]

protocol_sources = []
foreach xml : protocol_xmls
  protocol_sources += custom_target(
    input: xml,
    output: '@BASENAME@-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  protocol_sources += custom_target(
    input: xml,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
endforeach

protocols_lib = static_library('protocols', protocol_sources, dependencies: wayland_dep)
protocols_dep = declare_dependency(link_with: protocols_lib, sources: protocol_sources)
