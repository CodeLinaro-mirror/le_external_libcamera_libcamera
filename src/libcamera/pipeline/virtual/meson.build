# SPDX-License-Identifier: CC0-1.0

libcamera_sources += files([
    'virtual.cpp',
    'test_pattern_generator.cpp',
])

libyuv_dep = dependency('libyuv', required : false)

# Fallback to a subproject if libyuv isn't found, as it's typically not
# provided by distributions.
if not libyuv_dep.found()
    cmake = import('cmake')

    libyuv_vars = cmake.subproject_options()
    libyuv_vars.add_cmake_defines({'CMAKE_POSITION_INDEPENDENT_CODE': 'ON'})
    libyuv_vars.set_override_option('cpp_std', 'c++17')
    libyuv_vars.append_compile_args('cpp',
         '-Wno-sign-compare',
         '-Wno-unused-variable',
         '-Wno-unused-parameter')
    libyuv_vars.append_link_args('-ljpeg')
    libyuv = cmake.subproject('libyuv', options : libyuv_vars)
    libyuv_dep = libyuv.dependency('yuv')
endif

libcamera_deps += [libyuv_dep]
