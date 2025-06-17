/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 ISP pipeline
 */

#include "isp.h"

#include <linux/videodev2.h>

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
	auto input = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!input) {
		LOG(RCar4, Error) << "Failed to find ISP input entity";
		return -EINVAL;
	}

	pad = entity->getPadByIndex(1);
	next = pad->links()[0]->source();
	auto param = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!param) {
		LOG(RCar4, Error) << "Failed to find ISP param entity";
		return -EINVAL;
	}

	pad = entity->getPadByIndex(2);
	next = pad->links()[0]->sink();
	auto stat = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!stat) {
		LOG(RCar4, Error) << "Failed to find ISP stat entity";
		return -EINVAL;
	}

	pad = entity->getPadByIndex(3);
	next = pad->links()[0]->sink();
	auto output = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!output) {
		LOG(RCar4, Error) << "Failed to find ISP output entity";
		return -EINVAL;
	}

	/* Open all devices. */
	int ret = input->open();
	if (ret)
		return ret;

	ret = param->open();
	if (ret)
		return ret;

	ret = stat->open();
	if (ret)
		return ret;

	ret = output->open();
	if (ret)
		return ret;

	input_ = std::move(input);
	param_ = std::move(param);
	stat_ = std::move(stat);
	output_ = std::move(output);

	return 0;
}

int RCarISPDevice::configure(const V4L2DeviceFormat &sensorFormat,
			     const PixelFormat &outputPixelFormat)
{
	auto inputFormat = sensorFormat;

	/* Configure the RAW input. */
	int ret = input_->setFormat(&inputFormat);
	if (ret)
		return ret;

	if (inputFormat.fourcc != sensorFormat.fourcc || inputFormat.size != sensorFormat.size)
		return -EINVAL;

	/* Configure the image output. */
	V4L2DeviceFormat outputFormat = {};
	V4L2PixelFormat outputPf = output_->toV4L2PixelFormat(outputPixelFormat);
	outputFormat.fourcc = outputPf;
	outputFormat.size = inputFormat.size;
	ret = output_->setFormat(&outputFormat);
	if (ret)
		return ret;

	if (outputFormat.fourcc != outputPf || outputFormat.size != inputFormat.size)
		return -EINVAL;

	/* Configure paramaters. */
	V4L2DeviceFormat paramFormat = {};
	paramFormat.fourcc = V4L2PixelFormat(V4L2_META_FMT_RPPX1_PARAMS);
	ret = param_->setFormat(&paramFormat);
	if (ret)
		return ret;

	/* Configure statistics. */
	V4L2DeviceFormat statFormat = {};
	statFormat.fourcc = V4L2PixelFormat(V4L2_META_FMT_RPPX1_STATS);
	ret = stat_->setFormat(&statFormat);
	if (ret)
		return ret;

	return 0;
}

int RCarISPDevice::start(unsigned int bufferCount)
{
	utils::scope_exit stopGuard([&] { stop(); });

	int ret = input_->importBuffers(bufferCount);
	if (ret) {
		LOG(RCar4, Error) << "Failed to import ISP input buffers";
		return ret;
	}

	ret = output_->importBuffers(bufferCount);
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

	stopGuard.release();
	return 0;
}

void RCarISPDevice::stop()
{
	input_->streamOff();
	stat_->streamOff();
	param_->streamOff();
	output_->streamOff();

	if (output_->releaseBuffers())
		LOG(RCar4, Error) << "Failed to release ISP output buffers";

	if (input_->releaseBuffers())
		LOG(RCar4, Error) << "Failed to release ISP input buffers";
}

} /* namespace libcamera */
