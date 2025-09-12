/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, matthias.fend@emfend.at
 *
 * Camera flash support
 */

#include "libcamera/internal/camera_flash.h"

#include <libcamera/base/utils.h>

#include "libcamera/internal/v4l2_subdevice.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(CameraFlash)

CameraFlash::CameraFlash(const MediaEntity *entity)
	: entity_(entity)
{
}

CameraFlash::~CameraFlash() = default;

int CameraFlash::init()
{
	if (entity_->function() != MEDIA_ENT_F_FLASH) {
		LOG(CameraFlash, Error)
			<< "Invalid flash function "
			<< utils::hex(entity_->function());
		return -EINVAL;
	}

	subdev_ = std::make_unique<V4L2Subdevice>(entity_);
	int ret = subdev_->open();
	if (ret < 0)
		return ret;

	controlInfoMap_ = &subdev_->controls();

	ret = validateDriver();
	if (ret)
		return ret;

	model_ = subdev_->model();

	return 0;
}

std::optional<CameraFlash::Mode> CameraFlash::getMode() const
{
	Mode mode;

	auto v4l2Mode = getSubdevControl(V4L2_CID_FLASH_LED_MODE);
	if (!v4l2Mode)
		return std::nullopt;

	switch (*v4l2Mode) {
	case V4L2_FLASH_LED_MODE_FLASH:
		mode = Mode::Flash;
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		mode = Mode::Torch;
		break;
	case V4L2_FLASH_LED_MODE_NONE:
	default:
		mode = Mode::None;
		break;
	}

	return mode;
}

int CameraFlash::setMode(Mode mode)
{
	int32_t v4l2Mode;

	switch (mode) {
	case Mode::Flash:
		v4l2Mode = V4L2_FLASH_LED_MODE_FLASH;
		break;
	case Mode::Torch:
		v4l2Mode = V4L2_FLASH_LED_MODE_TORCH;
		break;
	case Mode::None:
		v4l2Mode = V4L2_FLASH_LED_MODE_NONE;
		break;
	default:
		return -EINVAL;
	}

	return setSubdevControl(V4L2_CID_FLASH_LED_MODE, v4l2Mode);
}

const ControlInfo &CameraFlash::getFlashIntensityInfo() const
{
	return controlInfoMap_->find(V4L2_CID_FLASH_INTENSITY)->second;
}

std::optional<int32_t> CameraFlash::getFlashIntensity() const
{
	return getSubdevControl(V4L2_CID_FLASH_INTENSITY);
}

int CameraFlash::setFlashIntensity(int32_t intensity)
{
	return setSubdevControl(V4L2_CID_FLASH_INTENSITY, intensity);
}

const ControlInfo &CameraFlash::getFlashTimeoutInfo() const
{
	return controlInfoMap_->find(V4L2_CID_FLASH_TIMEOUT)->second;
}

std::optional<int32_t> CameraFlash::getFlashTimeout() const
{
	return getSubdevControl(V4L2_CID_FLASH_TIMEOUT);
}

int CameraFlash::setFlashTimeout(int32_t timeout)
{
	return setSubdevControl(V4L2_CID_FLASH_TIMEOUT, timeout);
}

std::optional<CameraFlash::StrobeSource> CameraFlash::getStrobeSource() const
{
	StrobeSource source;

	auto v4l2Source = getSubdevControl(V4L2_CID_FLASH_STROBE_SOURCE);
	if (!v4l2Source)
		return std::nullopt;

	switch (*v4l2Source) {
	case V4L2_FLASH_STROBE_SOURCE_EXTERNAL:
		source = StrobeSource::External;
		break;
	case V4L2_FLASH_STROBE_SOURCE_SOFTWARE:
	default:
		source = StrobeSource::Software;
		break;
	}

	return source;
}

int CameraFlash::setStrobeSource(StrobeSource source)
{
	int32_t v4l2Source;

	switch (source) {
	case StrobeSource::External:
		v4l2Source = V4L2_FLASH_STROBE_SOURCE_EXTERNAL;
		break;
	case StrobeSource::Software:
		v4l2Source = V4L2_FLASH_STROBE_SOURCE_SOFTWARE;
		break;
	default:
		return -EINVAL;
	}

	return setSubdevControl(V4L2_CID_FLASH_STROBE_SOURCE, v4l2Source);
}

int CameraFlash::startStrobe()
{
	return setSubdevControl(V4L2_CID_FLASH_STROBE, 1);
}

int CameraFlash::stopStrobe()
{
	return setSubdevControl(V4L2_CID_FLASH_STROBE_STOP, 1);
}

const ControlInfo &CameraFlash::getTorchIntensityInfo() const
{
	return controlInfoMap_->find(V4L2_CID_FLASH_TORCH_INTENSITY)->second;
}

std::optional<int32_t> CameraFlash::getTorchIntensity() const
{
	return getSubdevControl(V4L2_CID_FLASH_TORCH_INTENSITY);
}

int CameraFlash::setTorchIntensity(int32_t intensity)
{
	return setSubdevControl(V4L2_CID_FLASH_TORCH_INTENSITY, intensity);
}

std::string CameraFlash::logPrefix() const
{
	return "'" + entity_->name() + "'";
}

std::optional<int32_t> CameraFlash::getSubdevControl(uint32_t id) const
{
	ControlList controlList = subdev_->getControls(std::array{ id });

	if (controlList.contains(id))
		return std::nullopt;

	return controlList.get(id).get<int32_t>();
}

int CameraFlash::setSubdevControl(uint32_t id, int32_t value)
{
	ControlList flashCtrls(*controlInfoMap_);

	flashCtrls.set(id, value);

	if (subdev_->setControls(&flashCtrls))
		return -EINVAL;

	return 0;
}

int CameraFlash::validateDriver()
{
	int ret = 0;
	static constexpr uint32_t mandatoryControls[] = {
		V4L2_CID_FLASH_LED_MODE,
		V4L2_CID_FLASH_STROBE_SOURCE,
		V4L2_CID_FLASH_STROBE,
		V4L2_CID_FLASH_TIMEOUT,
		V4L2_CID_FLASH_INTENSITY,
		V4L2_CID_FLASH_TORCH_INTENSITY,
	};

	for (uint32_t ctrl : mandatoryControls) {
		if (!controlInfoMap_->count(ctrl)) {
			LOG(CameraFlash, Error)
				<< "Mandatory V4L2 control " << utils::hex(ctrl)
				<< " not available";
			ret = -EINVAL;
		}
	}

	if (ret) {
		LOG(CameraFlash, Error)
			<< "The flash kernel driver needs to be fixed";
		LOG(CameraFlash, Error)
			<< "See Documentation/flash_driver_requirements.rst in"
			<< " the libcamera sources for more information";
		return ret;
	}

	return ret;
}

} /* namespace libcamera */
