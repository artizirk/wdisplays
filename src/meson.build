# SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
# SPDX-License-Identifier: CC0-1.0

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)
rt_dep = cc.find_library('rt', required : false)
gdk = dependency('gdk-3.0', version: '>= 3.24')
gtk = dependency('gtk+-3.0', version: '>= 3.24')
assert(gdk.get_pkgconfig_variable('targets').split().contains('wayland'), 'Wayland GDK backend not present')
epoxy = dependency('epoxy')

configure_file(input: 'config.h.in', output: 'config.h', configuration: conf)

executable(
  'wdisplays',
  [
    'main.c',
    'glviewport.c',
    'headform.c',
    'outputs.c',
    'overlay.c',
    'render.c',
    'store.c',
    resources,
  ],
  dependencies : [
    m_dep,
    rt_dep,
    wayland_client,
    client_protos,
    epoxy,
    gtk
  ],
  install: true
)
