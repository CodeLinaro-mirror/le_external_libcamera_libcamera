# SPDX-License-Identifier: CC0-1.0

softisp_adjust_test = executable(
    'softisp_adjust',
    [
        'adjust.cpp',
        '../../../src/ipa/softisp/algorithms/adjust.cpp',
    ],
    dependencies : [libcamera_private, libipa_dep],
    link_with : [test_libraries],
    include_directories : [
        test_includes_internal,
        include_directories('../../../src/ipa/softisp'),
    ],
)

test('softisp_adjust', softisp_adjust_test, suite : 'ipa')
