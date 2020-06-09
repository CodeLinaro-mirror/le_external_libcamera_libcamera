# SPDX-License-Identifier: CC0-1.0

linux_public_headers = files([
    'drm_fourcc.h',
    'drm.h',
    'drm_mode.h',
])

linux_include_dir = join_paths(libcamera_include_dir, 'linux')

install_headers(linux_public_headers,
                subdir : linux_include_dir)
