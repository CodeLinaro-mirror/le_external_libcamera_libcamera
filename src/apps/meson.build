# SPDX-License-Identifier: CC0-1.0

opt_cam = get_option('cam')
opt_lc_compliance = get_option('lc-compliance')

libevent = dependency('libevent_pthreads', required : opt_cam)

libtiff = dependency('libtiff-4', required : false)

subdir('common')

subdir('lc-compliance')

subdir('cam')
subdir('qcam')

subdir('ipa-verify')
