# SPDX-License-Identifier: CC0-1.0

libcamera_include_dir = 'libcamera' / 'libcamera'

libcamera_public_headers = files([
    'camera.h',
    'camera_manager.h',
    'color_space.h',
    'controls.h',
    'fence.h',
    'framebuffer.h',
    'framebuffer_allocator.h',
    'geometry.h',
    'logging.h',
    'orientation.h',
    'pixel_format.h',
    'request.h',
    'stream.h',
    'transform.h',
])

subdir('base')
subdir('internal')
subdir('ipa')

install_headers(libcamera_public_headers,
                subdir : libcamera_include_dir)

foreach lph : libcamera_public_headers
	doxygen_public_sources += '/'.join(
		meson.project_source_root(),
		'@0@'.format(lph)
	)
endforeach

#
# Generate headers from templates.
#

libcamera_headers_install_dir = get_option('includedir') / libcamera_include_dir

controls_map = {
    'controls': {
        'draft': 'control_ids_draft.yaml',
        'core': 'control_ids_core.yaml',
        'rpi/vc4': 'control_ids_rpi.yaml',
    },

    'properties': {
        'draft': 'property_ids_draft.yaml',
        'core': 'property_ids_core.yaml',
    }
}

control_headers = []
controls_files = []
properties_files = []

foreach mode, entry : controls_map
    files_list = []
    input_files = []
    foreach vendor, header : entry
        if vendor != 'core' and vendor != 'draft'
            if vendor not in pipelines
                continue
            endif
        endif

        if header in files_list
            continue
        endif

        files_list += header
        input_files += files('../../src/libcamera/' + header)
    endforeach

    outfile = ''
    if mode == 'controls'
        outfile = 'control_ids.h'
        controls_files += files_list
    else
        outfile = 'property_ids.h'
        properties_files += files_list
    endif

    template_file = files(outfile + '.in')
    ranges_file = files('../../src/libcamera/control_ranges.yaml')
    control_headers += custom_target(header + '_h',
                                     input : input_files,
                                     output : outfile,
                                     command : [gen_controls, '-o', '@OUTPUT@',
                                                '--mode', mode, '-t', template_file,
                                                '-r', ranges_file, '@INPUT@'],
                                     install : true,
                                     install_dir : libcamera_headers_install_dir)
    doxygen_public_sources += control_headers.get(-1).full_path()
endforeach

libcamera_public_headers += control_headers

# formats.h
formats_h = custom_target('formats_h',
                          input : files(
                              '../../src/libcamera/formats.yaml',
                              'formats.h.in',
                              '../linux/drm_fourcc.h'
                          ),
                          output : 'formats.h',
                          command : [gen_formats, '-o', '@OUTPUT@', '@INPUT@'],
                          install : true,
                          install_dir : libcamera_headers_install_dir)
libcamera_public_headers += formats_h
doxygen_public_sources += formats_h.full_path()

# libcamera.h
libcamera_h = custom_target('gen-header',
                            input : 'meson.build',
                            output : 'libcamera.h',
                            command : [gen_header, meson.current_source_dir(), '@OUTPUT@'],
                            install : true,
                            install_dir : libcamera_headers_install_dir)

libcamera_public_headers += libcamera_h
doxygen_public_sources += libcamera_h.full_path()

# version.h
version = libcamera_version.split('.')
libcamera_version_config = configuration_data()
libcamera_version_config.set('LIBCAMERA_VERSION_MAJOR', version[0])
libcamera_version_config.set('LIBCAMERA_VERSION_MINOR', version[1])
libcamera_version_config.set('LIBCAMERA_VERSION_PATCH', version[2])

configure_file(input : 'version.h.in',
               output : 'version.h',
               configuration : libcamera_version_config,
               install_dir : libcamera_headers_install_dir)
