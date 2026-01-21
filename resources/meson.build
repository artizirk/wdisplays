# SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
# SPDX-License-Identifier: CC0-1.0

resources_xml = configure_file(
  input: 'resources.xml.in',
  output: 'resources.xml',
  configuration: conf
)

gnome = import('gnome')
resources = gnome.compile_resources(
  'wdisplays-resources', resources_xml,
  source_dir : '.',
  c_name : 'wdisplays_resources')

scour = find_program('scour', required: false)

icon = 'wdisplays.svg'
icondir = get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps'

if scour.found()
  custom_target('optimize-icon',
      input: icon,
      output: '@0@.svg'.format(meson.project_name()),
      command: [scour,
      '--enable-viewboxing',
      '--enable-comment-stripping',
      '--remove-descriptive-elements',
      '--enable-id-stripping',
      '--shorten-ids',
      '--create-groups',
      '--strip-xml-space',
      '--strip-xml-prolog',
      '--indent=none',
      '--no-line-breaks',
      '@INPUT@', '@OUTPUT@'],
      install: true,
      install_dir: icondir)
else
  install_data(icon,
    rename: '@0@.svg'.format(meson.project_name()),
    install_dir: icondir
  )
endif

install_data(
    configure_file(input: 'wdisplays.desktop.in',
      output: '@0@.desktop'.format(meson.project_name()),
      configuration: conf),
    install_dir: get_option('datadir') / 'applications')

install_data('wdisplays.gschema.xml',
  rename: '@0@.gschema.xml'.format(meson.project_name()),
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas')

gnome.post_install(
  glib_compile_schemas: true
)
