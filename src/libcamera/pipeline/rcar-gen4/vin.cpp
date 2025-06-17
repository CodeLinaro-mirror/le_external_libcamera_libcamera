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

	/*
	 * The current kernel driver connects each sensor to a particular VIN
	 * device, so that one has to be used, even though technically any
	 * VIN device could be used if proper link handling is implemented.
	 */

	/* Locate IPS Channel Selector, e.g. rcar_isp fed00000.isp */
	auto csisp = V4L2Subdevice::fromEntityName(media, pipeId);
	if (!csisp) {
		LOG(RCar4, Error) << "Failed to find Channel Selector " << pipeId;
		return -EINVAL;
	}

	/* Use the Channel Selector links to find CSI-2 Rx and Sensor. */
	entity = csisp->entity();
	pad = entity->getPadByIndex(0);
	next = pad->links()[0]->source();
	auto csi2 = V4L2Subdevice::fromEntityName(media, next->entity()->name());
	if (!csi2) {
		LOG(RCar4, Error) << "Failed to find CSI-2 Rx entity";
		return -EINVAL;
	}

	entity = csi2->entity();
	pad = entity->getPadByIndex(0);
	next = pad->links()[0]->source();
	auto sensor = CameraSensorFactoryBase::create(next->entity());
	if (!sensor) {
		LOG(RCar4, Error) << "Failed to find sensor entity";
		return -EINVAL;
	}

	/*
	 * Use the Channel Selector links to find VIN. Currently the first
	 * VIN device is used unconditionally.
	 */
	entity = csisp->entity();
	pad = entity->getPadByIndex(1);
	next = pad->links()[0]->sink();
	auto output = V4L2VideoDevice::fromEntityName(media, next->entity()->name());
	if (!output) {
		LOG(RCar4, Error) << "Failed to find VIN entity";
		return -EINVAL;
	}

	/* Open all devices. */
	int ret = csi2->open();
	if (ret)
		return ret;

	ret = csisp->open();
	if (ret)
		return ret;

	ret = output->open();
	if (ret)
		return ret;

	csi2_ = std::move(csi2);
	csisp_ = std::move(csisp);
	sensor_ = std::move(sensor);
	output_ = std::move(output);

	return 0;
}

int RCarVINDevice::configure(const V4L2SubdeviceFormat &format, Transform transform,
			     V4L2DeviceFormat *outputFormat)
{
	auto sensorFormat = format;

	/* Configure sensor */
	int ret = sensor_->setFormat(&sensorFormat, transform);
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
	utils::scope_exit stopGuard([&] { stop(); });

	int ret = output_->importBuffers(bufferCount);
	if (ret) {
		LOG(RCar4, Error) << "Failed to import VIN buffers";
		return ret;
	}

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
