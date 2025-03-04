/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 *
 * Pipine handler element for the Renesas RZ/G2L Camera Receiver Unit
 */

#pragma once

#include <memory>
#include <vector>

#include <libcamera/base/signal.h>

#include "libcamera/internal/v4l2_subdevice.h"
#include "libcamera/internal/v4l2_videodevice.h"

#include <queue>

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

class RZG2LCRU
{
public:
	static constexpr unsigned int kBufferCount = 4;

	RZG2LCRU() = default;

	std::vector<Size> sizes(unsigned int mbusCode) const;
	int init(const MediaDevice *media, MediaEntity **sensorEntity);
	const Size resolution() const;
	void setSensorAndCSI2Pointers(std::shared_ptr<CameraSensor> sensor,
				      std::shared_ptr<V4L2Subdevice> csi2);
	int configure(V4L2SubdeviceFormat *subdevFormat, V4L2DeviceFormat *inputFormat);
	FrameBuffer *queueBuffer(Request *request);
	void cruReturnBuffer(FrameBuffer *buffer);
	V4L2VideoDevice *output() { return output_.get(); }
	int start();
	int stop();
private:
	void freeBuffers();

	void cruBufferReady(FrameBuffer *buffer);

	std::shared_ptr<CameraSensor> sensor_;
	std::shared_ptr<V4L2Subdevice> csi2_;
	std::unique_ptr<V4L2Subdevice> cru_;
	std::unique_ptr<V4L2VideoDevice> output_;

	std::vector<std::unique_ptr<FrameBuffer>> buffers_;
	std::queue<FrameBuffer *> availableBuffers_;

	Size csi2Resolution_;
};

} /* namespace libcamera */
