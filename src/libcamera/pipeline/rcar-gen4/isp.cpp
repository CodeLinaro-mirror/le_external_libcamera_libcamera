/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 ISP pipeline
 */

#include "isp.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <linux/media-bus-format.h>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/formats.h>
#include <libcamera/stream.h>

#include "libcamera/internal/media_device.h"
#include "libcamera/internal/v4l2_subdevice.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(RCar4)

int RCarISPDevice::init(const MediaDevice *media, const std::string &pipeId)
{
	const MediaEntity *entity;
	const MediaPad *pad, *next;
	int ret;

	/* Locate IPSCORE, e.g. rcar_isp fed00000.isp core */
	std::unique_ptr<V4L2Subdevice> core =
		V4L2Subdevice::fromEntityName(media, pipeId + " core");
	if (!core) {
		LOG(RCar4, Error) << "Failed to find ISPCORE " << pipeId;
		return -EINVAL;
	}

	entity = core->entity();

	/* Use the media links to find all video devices. */
	pad = entity->getPadByIndex(0);
	next = pad->links()[0]->source();
	input_ = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!input_) {
		LOG(RCar4, Error) << "Failed to find ISP input entity";
		return -EINVAL;
	}

	pad = entity->getPadByIndex(1);
	next = pad->links()[0]->source();
	param_ = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!param_) {
		LOG(RCar4, Error) << "Failed to find ISP param entity";
		return -EINVAL;
	}

	pad = entity->getPadByIndex(2);
	next = pad->links()[0]->sink();
	stat_ = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!stat_) {
		LOG(RCar4, Error) << "Failed to find ISP stat entity";
		return -EINVAL;
	}

	pad = entity->getPadByIndex(3);
	next = pad->links()[0]->sink();
	output_ = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!output_) {
		LOG(RCar4, Error) << "Failed to find ISP output entity";
		return -EINVAL;
	}

	/* Open all devices. */
	ret = input_->open();
	if (ret)
		return ret;

	ret = param_->open();
	if (ret)
		return ret;

	ret = stat_->open();
	if (ret)
		return ret;

	ret = output_->open();
	if (ret)
		return ret;

	return 0;
}

std::vector<PixelFormat> RCarISPDevice::formats() const
{
	std::vector<PixelFormat> formats;
	for (const auto &[format, sizes] : output_->formats())
		formats.push_back(format.toPixelFormat());

	return formats;
}

StreamConfiguration
RCarISPDevice::generateConfiguration(PixelFormat format, Size size) const
{
	StreamConfiguration cfg;

	bool found = false;
	for (const auto &pixelFormat : formats()) {
		if (pixelFormat == format)
			found = true;
	}

	cfg.size = size;
	cfg.bufferCount = kBufferCount;
	cfg.pixelFormat = found ? format : formats::XRGB8888;

	/* Get stride and frame size from device. */
	V4L2DeviceFormat fmt;
	fmt.fourcc = output_->toV4L2PixelFormat(cfg.pixelFormat);
	fmt.size = cfg.size;

	if (output_->tryFormat(&fmt))
		return {};

	cfg.stride = fmt.planes[0].bpl;
	cfg.frameSize = fmt.planes[0].size;

	return cfg;
}

int RCarISPDevice::configure(V4L2DeviceFormat *inputFormat,
			     const PixelFormat &outputPixelFormat)
{
	int ret;

	/* Configure the RAW input. */
	ret = input_->setFormat(inputFormat);
	if (ret)
		return ret;

	LOG(RCar4, Debug) << "ISP input format = " << *inputFormat;

	/* Configure the image output. */
	V4L2DeviceFormat outputFormat;
	outputFormat.fourcc = output_->toV4L2PixelFormat(outputPixelFormat);
	outputFormat.size = inputFormat->size;
	ret = output_->setFormat(&outputFormat);
	if (ret)
		return ret;

	LOG(RCar4, Debug) << "ISP output format = " << outputFormat;

	/* Configure paramaters. */
	V4L2DeviceFormat paramFormat;
	paramFormat.fourcc = V4L2PixelFormat(V4L2_META_FMT_RK_ISP1_EXT_PARAMS);
	ret = param_->setFormat(&paramFormat);
	if (ret)
		return ret;

	/* Configure statistics. */
	V4L2DeviceFormat statFormat;
	statFormat.fourcc = V4L2PixelFormat(V4L2_META_FMT_RK_ISP1_STAT_3A);
	ret = stat_->setFormat(&statFormat);
	if (ret)
		return ret;

	return 0;
}

int RCarISPDevice::start()
{
	int ret;

	ret = input_->importBuffers(kBufferCount);
	if (ret) {
		LOG(RCar4, Error) << "Failed to import ISP input buffers";
		return ret;
	}

	ret = output_->importBuffers(kBufferCount);
	if (ret) {
		LOG(RCar4, Error) << "Failed to import ISP output buffers";
		return ret;
	}

	ret = output_->streamOn();
	if (ret) {
		LOG(RCar4, Error) << "Failed to start ISP output";
		return ret;
	}

	ret = param_->streamOn();
	if (ret) {
		LOG(RCar4, Error) << "Failed to start ISP param";
		return ret;
	}

	ret = stat_->streamOn();
	if (ret) {
		LOG(RCar4, Error) << "Failed to start ISP stat";
		return ret;
	}

	ret = input_->streamOn();
	if (ret) {
		LOG(RCar4, Error) << "Failed to start ISP input";
		return ret;
	}

	return 0;
}

void RCarISPDevice::stop()
{
	output_->streamOff();
	param_->streamOff();
	stat_->streamOff();
	input_->streamOff();
}

} /* namespace libcamera */
