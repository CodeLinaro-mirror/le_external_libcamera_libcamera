# SPDX-License-Identifier: CC0-1.0

layer_includes = [
    libcamera_includes,
]

layer_install_dir = libcamera_libdir / 'layers'

config_h.set('LAYER_DIR',
             '"' + get_option('prefix') / layer_install_dir + '"')

subdir('inject_controls')
