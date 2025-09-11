/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 ISP pipeline
 */

#pragma once

#include <memory>
#include <string>

#include "libcamera/internal/v4l2_videodevice.h"

namespace libcamera {

class MediaDevice;
class Size;
struct StreamConfiguration;

class RCarISPDevice
{
public:
	static constexpr unsigned int kBufferCount = 4;

	std::vector<PixelFormat> formats() const;

	int init(const MediaDevice *media, const std::string &pipeId);

	StreamConfiguration generateConfiguration(PixelFormat format, Size size) const;

	int configure(V4L2DeviceFormat *inputFormat, const PixelFormat &outputPixelFormat);

	int start();
	void stop();

	std::unique_ptr<V4L2VideoDevice> input_;
	std::unique_ptr<V4L2VideoDevice> param_;
	std::unique_ptr<V4L2VideoDevice> stat_;
	std::unique_ptr<V4L2VideoDevice> output_;
};

} /* namespace libcamera */
