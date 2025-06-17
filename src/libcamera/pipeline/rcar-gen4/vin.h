/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 VIN pipeline
 */

#pragma once

#include <memory>
#include <queue>
#include <vector>

#include <libcamera/base/signal.h>

#include "libcamera/internal/v4l2_subdevice.h"
#include "libcamera/internal/v4l2_videodevice.h"

namespace libcamera {

class CameraSensor;
class FrameBuffer;
class MediaDevice;
class PixelFormat;
class Request;
class Size;
class SizeRange;
struct StreamConfiguration;
enum class Transform;

class RCarVINDevice
{
public:
	static constexpr unsigned int kBufferCount = 4;

	RCarVINDevice();

	std::vector<PixelFormat> formats() const;
	std::vector<SizeRange> sizes(const PixelFormat &format) const;

	int init(const MediaDevice *media, const std::string &pipeId);
	int configure(const Size &size, const Transform &transform,
		      V4L2DeviceFormat *outputFormat);

	StreamConfiguration generateConfiguration(Size size) const;

	int start();
	void stop();

	CameraSensor *sensor() { return sensor_.get(); }
	const CameraSensor *sensor() const { return sensor_.get(); }

	int queueBuffer(FrameBuffer *buffer);

	Signal<FrameBuffer *> &bufferReady() { return output_->bufferReady; }
	Signal<uint32_t> &frameStart() { return output_->frameStart; }
private:
	V4L2SubdeviceFormat getSensorFormat(const std::vector<unsigned int> &mbusCodes,
					    const Size &size) const;

	std::unique_ptr<CameraSensor> sensor_;
	std::unique_ptr<V4L2Subdevice> csi2_;
	std::unique_ptr<V4L2Subdevice> csisp_;
	std::unique_ptr<V4L2VideoDevice> output_;
};

} /* namespace libcamera */
