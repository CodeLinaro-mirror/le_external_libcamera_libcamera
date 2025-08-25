# SPDX-License-Identifier: CC0-1.0

layer_includes = [
    libcamera_includes,
]

layer_install_dir = libcamera_libdir / 'layer'

config_h.set('LAYER_DIR',
             '"' + get_option('prefix') / layer_install_dir + '"')

layers_env = environment()
layers_env.set('LIBCAMERA_LAYER_PATH', meson.current_build_dir())
meson.add_devenv(layers_env)
