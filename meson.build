# SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
# SPDX-License-Identifier: CC0-1.0

project('network.cycles.wdisplays', 'c',
  license: 'GPL-3.0-or-later',
  version: '1.1.2'
)

conf = configuration_data({
  'app_id': meson.project_name(),
  'version': meson.project_version(),
  'resource_prefix': '/' / '/'.join(meson.project_name().split('.')),
})

subdir('protocol')
subdir('resources')
subdir('src')
