# SPDX-License-Identifier: CC0-1.0

subdir('tracepoints')

libcamera_tracepoint_header = custom_target(
    'tp_header',
    input : ['tracepoints.h.in', tracepoint_files],
    output : 'tracepoints.h',
    command : [gen_tracepoints_header, include_build_dir, '@OUTPUT@', '@INPUT@'],
)

# Where libcamera's public API classes have Extensible::Private counterparts we
# need to include them in doxygen's public API run or it will complain that the
# functions defined in the "public" source are undeclared.
libcamera_internal_headers_documented = files([
    'camera.h',
    'camera_manager.h',
    'framebuffer.h',
    'request.h',
])

libcamera_internal_headers_undocumented = files([
    'bayer_format.h',
    'byte_stream_buffer.h',
    'camera_controls.h',
    'camera_lens.h',
    'camera_sensor.h',
    'camera_sensor_properties.h',
    'control_serializer.h',
    'control_validator.h',
    'converter.h',
    'delayed_controls.h',
    'device_enumerator.h',
    'device_enumerator_sysfs.h',
    'device_enumerator_udev.h',
    'dma_buf_allocator.h',
    'formats.h',
    'ipa_manager.h',
    'ipa_module.h',
    'ipa_proxy.h',
    'ipc_unixsocket.h',
    'mapped_framebuffer.h',
    'media_device.h',
    'media_object.h',
    'pipeline_handler.h',
    'process.h',
    'pub_key.h',
    'shared_mem_object.h',
    'source_paths.h',
    'sysfs.h',
    'v4l2_device.h',
    'v4l2_pixelformat.h',
    'v4l2_subdevice.h',
    'v4l2_videodevice.h',
    'yaml_parser.h',
])

libcamera_internal_headers = [
    libcamera_internal_headers_documented,
    libcamera_internal_headers_undocumented,
]

foreach lph : libcamera_internal_headers_documented
	doxygen_public_sources += '/'.join(
		meson.project_source_root(),
		'@0@'.format(lph)
	)
endforeach

subdir('converter')
subdir('software_isp')
