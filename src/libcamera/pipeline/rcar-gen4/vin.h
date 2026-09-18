/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 VIN pipeline
 */

#pragma once

#include <memory>

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
	int init(const MediaDevice *media, const std::string &pipeId);
	int configure(const V4L2SubdeviceFormat &format, Transform transform,
		      V4L2DeviceFormat *outputFormat);

	int start(unsigned int bufferCount);
	void stop();

	CameraSensor *sensor() { return sensor_.get(); }
	const CameraSensor *sensor() const { return sensor_.get(); }
	V4L2VideoDevice *output() { return output_.get(); }
	const V4L2VideoDevice *output() const { return output_.get(); }

	int queueBuffer(FrameBuffer *buffer)
	{
		return output_->queueBuffer(buffer);
	}

	Signal<FrameBuffer *> &bufferReady() { return output_->bufferReady; }
	Signal<uint32_t> &frameStart() { return output_->frameStart; }

private:
	std::unique_ptr<CameraSensor> sensor_;
	std::unique_ptr<V4L2Subdevice> csi2_;
	std::unique_ptr<V4L2Subdevice> csisp_;
	std::unique_ptr<V4L2VideoDevice> output_;
};

} /* namespace libcamera */
