/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 *
 * Pipine handler element for the Renesas RZ/G2L Camera Receiver Unit
 */

#include "rzg2l-cru.h"

#include <map>

#include <libcamera/base/log.h>
#include <libcamera/base/regex.h>

#include <libcamera/formats.h>
#include <libcamera/pixel_format.h>

#include "libcamera/internal/bayer_format.h"
#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/request.h"

namespace libcamera {

static const std::map<uint8_t, PixelFormat> bitDepthToFmt {
	{ 10, formats::RAW10_CRU },
	{ 12, formats::RAW12_CRU },
	{ 14, formats::RAW14_CRU },
};

LOG_DEFINE_CATEGORY(RZG2LCRU)

std::vector<Size> RZG2LCRU::sizes(unsigned int mbusCode) const
{
	std::vector<Size> sensorSizes = sensor_->sizes(mbusCode);
	std::vector<Size> cruSizes = {};

	for (auto size : sensorSizes) {
		if (size > csi2Resolution_)
			continue;

		cruSizes.push_back(size);
	}

	return cruSizes;
}

const Size RZG2LCRU::resolution() const
{
	return sensor_->resolution().boundedTo(csi2Resolution_);
}

void RZG2LCRU::setSensorAndCSI2Pointers(std::shared_ptr<CameraSensor> sensor,
					std::shared_ptr<V4L2Subdevice> csi2)
{
	std::vector<Size> csi2Sizes;

	sensor_ = sensor;
	csi2_ = csi2;

	V4L2Subdevice::Formats formats = csi2_->formats(0);
	if (formats.empty())
		return;

	for (const auto &format : formats) {
		const std::vector<SizeRange> &ranges = format.second;
		std::transform(ranges.begin(), ranges.end(), std::back_inserter(csi2Sizes),
			       [](const SizeRange &range) { return range.max; });
	}

	csi2Resolution_ = csi2Sizes.back();
}

FrameBuffer *RZG2LCRU::queueBuffer(Request *request)
{
	FrameBuffer *buffer;

	if (availableBuffers_.empty()) {
		LOG(RZG2LCRU, Debug) << "CRU buffer underrun";
		return nullptr;
	}

	buffer = availableBuffers_.front();

	int ret = output_->queueBuffer(buffer);
	if (ret) {
		LOG(RZG2LCRU, Error) << "Failed to queue buffer to CRU";
		return nullptr;
	}

	availableBuffers_.pop();
	buffer->_d()->setRequest(request);

	return buffer;
}

void RZG2LCRU::cruReturnBuffer(FrameBuffer *buffer)
{
	for (const std::unique_ptr<FrameBuffer> &buf : buffers_) {
		if (buf.get() == buffer) {
			availableBuffers_.push(buffer);
			break;
		}
	}
}

int RZG2LCRU::start()
{
	int ret = output_->exportBuffers(kBufferCount, &buffers_);
	if (ret < 0)
		return ret;

	ret = output_->importBuffers(kBufferCount);
	if (ret)
		return ret;

	for (std::unique_ptr<FrameBuffer> &buffer : buffers_)
		availableBuffers_.push(buffer.get());

	ret = output_->streamOn();
	if (ret) {
		freeBuffers();
		return ret;
	}

	return 0;
}

int RZG2LCRU::stop()
{
	int ret;

	csi2_->setFrameStartEnabled(false);

	ret = output_->streamOff();

	freeBuffers();

	return ret;
}

void RZG2LCRU::freeBuffers()
{
	availableBuffers_ = {};
	buffers_.clear();

	if (output_->releaseBuffers())
		LOG(RZG2LCRU, Error) << "Failed to release CRU buffers";
}

int RZG2LCRU::configure(V4L2SubdeviceFormat *subdevFormat, V4L2DeviceFormat *inputFormat)
{
	int ret;

	/*
	 * The sensor and CSI-2 rx have already had their format set by the
	 * CameraData class...all we need to do is propagate it to the remaining
	 * elements of the CRU graph - the CRU subdevice and output video device
	 */

	ret = cru_->setFormat(0, subdevFormat);
	if (ret)
		return ret;

	ret = cru_->getFormat(1, subdevFormat);
	if (ret)
		return ret;

	/*
	 * The capture device needs to be set with a format that can be produced
	 * from the mbus code of the subdevFormat. The CRU and IVC use bayer
	 * order agnostic pixel formats, so all we need to do is find the right
	 * bitdepth and select the appropriate format.
	 */
	BayerFormat bayerFormat = BayerFormat::fromMbusCode(subdevFormat->code);
	if (!bayerFormat.isValid())
		return -EINVAL;

	PixelFormat pixelFormat = bitDepthToFmt.at(bayerFormat.bitDepth);

	V4L2DeviceFormat captureFormat;
	captureFormat.fourcc = output_->toV4L2PixelFormat(pixelFormat);
	captureFormat.size = subdevFormat->size;

	ret = output_->setFormat(&captureFormat);
	if (ret)
		return ret;

	/*
	 * We return the format that we set against the output device, as the
	 * same format will also need to be set against the Input Video Control
	 * Block device.
	 */
	*inputFormat = captureFormat;

	return 0;
}

int RZG2LCRU::init(const MediaDevice *media, MediaEntity **sensorEntity)
{
	int ret;

	MediaEntity *csi2Entity = media->getEntityByName(std::regex("csi-[0-9a-f]{8}.csi2"));
	if (!csi2Entity)
		return -ENODEV;

	const std::vector<MediaPad *> &pads = csi2Entity->pads();
	if (pads.empty())
		return -ENODEV;

	/* The receiver has a single sink pad at index 0 */
	MediaPad *sink = pads[0];
	const std::vector<MediaLink *> &links = sink->links();
	if (links.empty())
		return -ENODEV;

	MediaLink *link = links[0];
	*sensorEntity = link->source()->entity();

	/*
	 * We don't handle the sensor and csi-2 rx subdevice here, as the
	 * CameraData class does that already.
	 *
	 * \todo lose this horrible hack by making modular pipelines.
	 */
	cru_ = V4L2Subdevice::fromEntityName(media, std::regex("cru-ip-[0-9a-f]{8}.cru[0-9]"));
	ret = cru_->open();
	if (ret)
		return ret;

	output_ = V4L2VideoDevice::fromEntityName(media, "CRU output");
	return output_->open();
}

} /* namespace libcamera */
