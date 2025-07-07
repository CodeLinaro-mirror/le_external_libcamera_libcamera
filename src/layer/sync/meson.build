# SPDX-License-Identifier: CC0-1.0

layer_name = 'sync'

sync_sources = files([
    'sync.cpp',
])

mod = shared_module(layer_name, [sync_sources, libcamera_internal_headers],
                    name_prefix : '',
                    include_directories : layer_includes,
                    dependencies : libcamera_private,
                    gnu_symbol_visibility: 'hidden',
                    install : true,
                    install_dir : layer_install_dir)
