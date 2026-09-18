/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 VIN pipeline
 */

#include "vin.h"

#include <linux/media-bus-format.h>

#include <libcamera/base/utils.h>

#include <libcamera/formats.h>
#include <libcamera/geometry.h>
#include <libcamera/stream.h>
#include <libcamera/transform.h>

#include "libcamera/internal/bayer_format.h"
#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/v4l2_subdevice.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(RCar4)

int RCarVINDevice::init(const MediaDevice *media, const std::string &pipeId)
{
	const MediaEntity *entity;
	const MediaPad *pad, *next;
	int ret;

	/* Locate IPS Channel Selector, e.g. rcar_isp fed00000.isp */
	csisp_ = V4L2Subdevice::fromEntityName(media, pipeId);
	if (!csisp_) {
		LOG(RCar4, Error) << "Failed to find Channel Selector " << pipeId;
		return -EINVAL;
	}

	/* Use the Channel Selector links to find CSI-2 Rx and Sensor. */
	entity = csisp_->entity();
	pad = entity->getPadByIndex(0);
	next = pad->links()[0]->source();
	csi2_ = V4L2Subdevice::fromEntityName(media, next->entity()->name());
	if (!csi2_) {
		LOG(RCar4, Error) << "Failed to find CSI-2 Rx entity";
		return -EINVAL;
	}

	entity = csi2_->entity();
	pad = entity->getPadByIndex(0);
	next = pad->links()[0]->source();
	sensor_ = CameraSensorFactoryBase::create(next->entity());
	if (!sensor_) {
		LOG(RCar4, Error) << "Failed to find sensor entity";
		return -EINVAL;
	}

	/* Use the Channel Selector links to find VIN. */
	entity = csisp_->entity();
	pad = entity->getPadByIndex(1);
	next = pad->links()[0]->sink();
	output_ = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!output_) {
		LOG(RCar4, Error) << "Failed to find VIN entity";
		return -EINVAL;
	}

	/* Open all devices. */
	ret = csi2_->open();
	if (ret)
		return ret;

	ret = csisp_->open();
	if (ret)
		return ret;

	ret = output_->open();
	if (ret)
		return ret;

	return 0;
}

int RCarVINDevice::configure(const V4L2SubdeviceFormat &format, Transform transform,
			     V4L2DeviceFormat *outputFormat)
{
	auto sensorFormat = format;
	int ret;

	/* Configure sensor */
	ret = sensor_->setFormat(&sensorFormat, transform);
	if (ret)
		return ret;

	/* Configure CSI-2 */
	ret = csi2_->setFormat(0, &sensorFormat);
	if (ret)
		return ret;

	/* Configure Channel selector. */
	ret = csisp_->setFormat(0, &sensorFormat);
	if (ret)
		return ret;

	auto bayerFormat = BayerFormat::fromMbusCode(sensorFormat.code);
	if (!bayerFormat.isValid())
		return -ENOTSUP;

	/* Transform already applied to format by `CameraSensor::setFormat()`. */
	auto v4pf = bayerFormat.toV4L2PixelFormat();

	/* Configure VIN */
	outputFormat->fourcc = v4pf;
	outputFormat->size = sensorFormat.size;
	outputFormat->planesCount = 1;
	outputFormat->colorSpace = sensorFormat.colorSpace;

	ret = output_->setFormat(outputFormat);
	if (ret)
		return ret;

	LOG(RCar4, Debug)
		<< "sensor: " << sensorFormat << ", "
		<< "VIN: " << *outputFormat;

	if (outputFormat->size != format.size || outputFormat->fourcc != v4pf)
		return -EINVAL;

	return 0;
}

int RCarVINDevice::start(unsigned int bufferCount)
{
	int ret;

	ret = output_->importBuffers(bufferCount);
	if (ret) {
		LOG(RCar4, Error) << "Failed to import VIN buffers";
		return ret;
	}

	utils::scope_exit stopGuard([&] { stop(); });

	ret = output_->streamOn();
	if (ret) {
		LOG(RCar4, Error) << "Failed to start VIN";
		return ret;
	}

	ret = output_->setFrameStartEnabled(true);
	if (ret) {
		LOG(RCar4, Error) << "Failed to enable Frame Start";
		return ret;
	}

	stopGuard.release();
	return 0;
}

void RCarVINDevice::stop()
{
	output_->setFrameStartEnabled(false);

	output_->streamOff();

	if (output_->releaseBuffers())
		LOG(RCar4, Error) << "Failed to release VIN buffers";
}

} /* namespace libcamera */
