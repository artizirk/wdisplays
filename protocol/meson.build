# SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
# SPDX-License-Identifier: CC0-1.0

wayland_scanner = find_program('wayland-scanner')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.17')

wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

wayland_scanner_code = generator(
  wayland_scanner,
  output: '@BASENAME@-protocol.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
  wayland_scanner,
  output: '@BASENAME@-client-protocol.h',
  arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

client_protocols = [
  [wl_protocol_dir, 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
  [wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
  ['wlr-output-management-unstable-v1.xml'],
  ['wlr-screencopy-unstable-v1.xml'],
  ['wlr-layer-shell-unstable-v1.xml']
]

client_protos_src = []
client_protos_headers = []

foreach p : client_protocols
  xml = join_paths(p)
  client_protos_src += wayland_scanner_code.process(xml)
  client_protos_headers += wayland_scanner_client.process(xml)
endforeach

lib_client_protos = static_library(
  'client_protos',
  client_protos_src + client_protos_headers,
  dependencies: [wayland_client]
)

client_protos = declare_dependency(
  link_with: lib_client_protos,
  sources: client_protos_headers,
)
