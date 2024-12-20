# SPDX-License-Identifier: CC0-1.0

libgtest = dependency('gtest', version : '>=1.10.0',
                      required : get_option('lc-compliance'),
                      fallback : ['gtest', 'gtest_dep'])

lc_compliance_enabled = opt_lc_compliance.allowed() and libgtest.found()
if not lc_compliance_enabled
    subdir_done()
endif

lc_compliance_sources = files([
    'environment.cpp',
    'helpers/capture.cpp',
    'main.cpp',
    'tests/capture_test.cpp',
])

lc_compliance_includes = ([
    include_directories('.'),
    include_directories('helpers/')
])

lc_compliance  = executable('lc-compliance', lc_compliance_sources,
                            cpp_args : [ '-fexceptions' ],
                            link_with : apps_lib,
                            dependencies : [
                                libatomic,
                                libcamera_public,
                                libgtest,
                            ],
                            include_directories : lc_compliance_includes,
                            install : true,
                            install_tag : 'bin-devel')
