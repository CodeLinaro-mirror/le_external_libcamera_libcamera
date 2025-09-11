/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 VIN pipeline
 */

#include "vin.h"

#include <cmath>
#include <limits>

#include <linux/media-bus-format.h>

#include <libcamera/formats.h>
#include <libcamera/geometry.h>
#include <libcamera/stream.h>
#include <libcamera/transform.h>

#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/v4l2_subdevice.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(RCar4)

namespace {

const std::map<uint32_t, PixelFormat> mbusCodesToPixelFormat = {
	{ MEDIA_BUS_FMT_SBGGR10_1X10, formats::SBGGR10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, formats::SGBRG10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, formats::SGRBG10 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, formats::SRGGB10 },
};

} /* namespace */

/**
 * \brief Retrieve the list of supported PixelFormats
 *
 * Retrieve the list of supported pixel formats by matching the sensor produced
 * media bus codes with the formats supported by the VIN unit.
 *
 * \return The list of supported PixelFormat
 */
std::vector<PixelFormat> RCarVINDevice::formats() const
{
	if (!sensor_)
		return {};

	std::vector<PixelFormat> formats;
	for (unsigned int code : sensor_->mbusCodes()) {
		auto it = mbusCodesToPixelFormat.find(code);
		if (it != mbusCodesToPixelFormat.end())
			formats.push_back(it->second);
	}

	return formats;
}

/**
 * \brief Retrieve the list of supported size ranges
 * \param[in] format The pixel format
 *
 * Retrieve the list of supported sizes for a particular \a format by matching
 * the sensor produced media bus codes formats supported by the VIN unit.
 *
 * \return A list of supported sizes for the \a format or an empty list
 * otherwise
 */
std::vector<SizeRange> RCarVINDevice::sizes(const PixelFormat &format) const
{
	int mbusCode = -1;

	if (!sensor_)
		return {};

	std::vector<SizeRange> sizes;
	for (const auto &iter : mbusCodesToPixelFormat) {
		if (iter.second != format)
			continue;

		mbusCode = iter.first;
		break;
	}

	if (mbusCode == -1)
		return {};

	for (const Size &sz : sensor_->sizes(mbusCode))
		sizes.emplace_back(sz);

	return sizes;
}

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

	/*
	 * Make sure the sensor produces at least one format compatible with
	 * the VIN requirements.
	 */
	std::vector<unsigned int> vinCodes = utils::map_keys(mbusCodesToPixelFormat);
	const std::vector<unsigned int> &sensorCodes = sensor_->mbusCodes();
	if (!utils::set_overlap(sensorCodes.begin(), sensorCodes.end(),
				vinCodes.begin(), vinCodes.end())) {
		LOG(RCar4, Error)
			<< "Sensor " << sensor_->entity()->name()
			<< " has not format compatible with the VIN";
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

int RCarVINDevice::configure(const Size &size, const Transform &transform,
			     V4L2DeviceFormat *outputFormat)
{
	V4L2SubdeviceFormat sensorFormat;
	int ret;

	/* Configure sensor */
	std::vector<unsigned int> mbusCodes = utils::map_keys(mbusCodesToPixelFormat);
	sensorFormat = getSensorFormat(mbusCodes, size);
	ret = sensor_->setFormat(&sensorFormat, transform);
	if (ret)
		return ret;

	/* Configure CSI-2 */
	ret = csi2_->setFormat(0, &sensorFormat);
	if (ret)
		return ret;

	if (mbusCodesToPixelFormat.find(sensorFormat.code) == mbusCodesToPixelFormat.end())
		return -EINVAL;

	/* Configure Channel selector. */
	ret = csisp_->setFormat(0, &sensorFormat);
	if (ret)
		return ret;

	if (mbusCodesToPixelFormat.find(sensorFormat.code) == mbusCodesToPixelFormat.end())
		return -EINVAL;

	/* Configure VIN */
	const auto &itInfo = mbusCodesToPixelFormat.find(sensorFormat.code);
	outputFormat->fourcc = output_->toV4L2PixelFormat(itInfo->second);
	outputFormat->size = sensorFormat.size;
	outputFormat->planesCount = 1;

	ret = output_->setFormat(outputFormat);
	if (ret)
		return ret;

	LOG(RCar4, Debug) << "VIN output format " << *outputFormat;

	return 0;
}

StreamConfiguration RCarVINDevice::generateConfiguration(Size size) const
{
	StreamConfiguration cfg;

	/* If no desired size use the sensor resolution. */
	if (size.isNull())
		size = sensor_->resolution();

	/* Query the sensor static information for closest match. */
	std::vector<unsigned int> mbusCodes = utils::map_keys(mbusCodesToPixelFormat);
	V4L2SubdeviceFormat sensorFormat = getSensorFormat(mbusCodes, size);
	if (!sensorFormat.code) {
		LOG(RCar4, Error) << "Sensor does not support mbus code";
		return {};
	}

	cfg.size = sensorFormat.size;
	cfg.pixelFormat = mbusCodesToPixelFormat.at(sensorFormat.code);
	cfg.bufferCount = kBufferCount;

	/* Get stride and frame size from device. */
	V4L2DeviceFormat fmt;
	fmt.fourcc = output_->toV4L2PixelFormat(cfg.pixelFormat);
	fmt.size = cfg.size;

	int ret = output_->tryFormat(&fmt);
	if (ret)
		return {};

	cfg.stride = fmt.planes[0].bpl;
	cfg.frameSize = fmt.planes[0].size;

	return cfg;
}

/**
 * \brief Retrieve the best sensor format for a desired output
 * \param[in] mbusCodes The list of acceptable media bus codes
 * \param[in] size The desired size
 *
 * Media bus codes are selected from \a mbusCodes, which lists all acceptable
 * codes in decreasing order of preference. Media bus codes supported by the
 * sensor but not listed in \a mbusCodes are ignored. If none of the desired
 * codes is supported, it returns an error.
 *
 * \a size indicates the desired size at the output of the sensor. This method
 * selects the best media bus code and size supported by the sensor according
 * to the following criteria.
 *
 * - The desired \a size shall fit in the sensor output size to avoid the need
 *   to up-scale.
 * - The aspect ratio of sensor output size shall be as close as possible to
 *   the sensor's native resolution field of view.
 * - The sensor output size shall be as small as possible to lower the required
 *   bandwidth.
 * - The desired \a size shall be supported by one of the media bus code listed
 *   in \a mbusCodes.
 *
 * When multiple media bus codes can produce the same size, the code at the
 * lowest position in \a mbusCodes is selected.
 *
 * The returned sensor output format is guaranteed to be acceptable by the
 * setFormat() method without any modification.
 *
 * \return The best sensor output format matching the desired media bus codes
 * and size on success, or an empty format otherwise.
 */
V4L2SubdeviceFormat RCarVINDevice::getSensorFormat(const std::vector<unsigned int> &mbusCodes,
						   const Size &size) const
{
	unsigned int desiredArea = size.width * size.height;
	unsigned int bestArea = std::numeric_limits<unsigned int>::max();
	const Size &resolution = sensor_->resolution();
	float desiredRatio = static_cast<float>(resolution.width) /
			     resolution.height;
	float bestRatio = std::numeric_limits<float>::max();
	Size bestSize;
	uint32_t bestCode = 0;

	for (unsigned int code : mbusCodes) {
		const auto sizes = sensor_->sizes(code);
		if (!sizes.size())
			continue;

		for (const Size &sz : sizes) {
			/* No need to check ratios if we have an exact match. */
			if (sz == size) {
				bestRatio = 0;
				bestArea = 0;
				bestSize = sz;
				bestCode = code;
				break;
			}

			if (sz.width < size.width || sz.height < size.height)
				continue;

			float ratio = static_cast<float>(sz.width) / sz.height;
			/*
			 * Ratios can differ by small mantissa difference which
			 * can affect the selection of the sensor output size
			 * wildly. We are interested in selection of the closest
			 * size with respect to the desired output size, hence
			 * comparing it with a single precision digit is enough.
			 */
			ratio = static_cast<unsigned int>(ratio * 10) / 10.0;
			float ratioDiff = std::abs(ratio - desiredRatio);
			unsigned int area = sz.width * sz.height;
			unsigned int areaDiff = area - desiredArea;

			if (ratioDiff > bestRatio)
				continue;

			if (ratioDiff < bestRatio || areaDiff < bestArea) {
				bestRatio = ratioDiff;
				bestArea = areaDiff;
				bestSize = sz;
				bestCode = code;
			}
		}
	}

	if (bestSize.isNull()) {
		LOG(RCar4, Debug) << "No supported format or size found";
		return {};
	}

	V4L2SubdeviceFormat format{};
	format.code = bestCode;
	format.size = bestSize;

	return format;
}

int RCarVINDevice::start()
{
	int ret;

	ret = output_->importBuffers(kBufferCount);
	if (ret) {
		LOG(RCar4, Error) << "Failed to import VIN buffers";
		return ret;
	}

	ret = output_->streamOn();
	if (ret) {
		LOG(RCar4, Error) << "Failed to start VIN";
		stop();
		return ret;
	}

	ret = output_->setFrameStartEnabled(true);
	if (ret) {
		LOG(RCar4, Error) << "Failed to enable Frame Start";
		stop();
		return ret;
	}

	return 0;
}

void RCarVINDevice::stop()
{
	output_->setFrameStartEnabled(false);

	output_->streamOff();

	if (output_->releaseBuffers())
		LOG(RCar4, Error) << "Failed to release VIN buffers";
}

int RCarVINDevice::queueBuffer(FrameBuffer *buffer)
{
	return output_->queueBuffer(buffer);
}

} /* namespace libcamera */
