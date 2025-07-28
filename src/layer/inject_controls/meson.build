# SPDX-License-Identifier: CC0-1.0

layer_name = 'layer_inject_controls'

layer_inject_sources = files([
    'inject_controls.h',
    'inject_controls.cpp',
])

mod = shared_module(layer_name, layer_inject_sources,
                    name_prefix : '',
                    include_directories : [layer_includes],
                    dependencies : libcamera_public,
                    gnu_symbol_visibility: 'hidden',
                    install : true,
                    install_dir : layer_install_dir)
