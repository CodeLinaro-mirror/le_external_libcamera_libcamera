# SPDX-License-Identifier: CC0-1.0

softisp_enabled = pipelines.contains('simple')
summary({'SoftISP support' : softisp_enabled}, section : 'Configuration')

if not softisp_enabled
    subdir_done()
endif

libcamera_internal_sources += files([
    'benchmark.cpp',
    'debayer.cpp',
    'debayer_cpu.cpp',
    'software_isp.cpp',
    'swstats_cpu.cpp',
])

libegl = cc.find_library('EGL', required : false)
libgbm = cc.find_library('gbm', required: false)
libglesv2 = cc.find_library('GLESv2', required : false)

if libegl.found()
    config_h.set('HAVE_LIBEGL', 1)
endif

if libgbm.found()
    config_h.set('HAVE_GBM', 1)
endif

if libglesv2.found()
    config_h.set('HAVE_GLESV2', 1)
endif

if libegl.found() and libgbm.found() and libglesv2.found()
libcamera_deps += [
    libegl,
    libgbm,
    libglesv2,
]

libcamera_internal_sources += files([
    'debayer_egl.cpp',
    'egl.cpp',
    'gbm.cpp',
])
endif
