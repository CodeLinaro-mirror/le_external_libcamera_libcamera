/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, matthias.fend@emfend.at
 *
 * Camera flash support
 */
#pragma once

#include <memory>
#include <stdint.h>
#include <string>

#include <libcamera/base/class.h>
#include <libcamera/base/log.h>

#include <libcamera/controls.h>

namespace libcamera {

class MediaEntity;
class V4L2Subdevice;

class CameraFlash : protected Loggable
{
public:
	enum class Mode {
		None,
		Flash,
		Torch,
	};

	enum class StrobeSource {
		Software,
		External,
	};

	explicit CameraFlash(const MediaEntity *entity);
	~CameraFlash();
	int init();
	std::optional<Mode> getMode() const;
	int setMode(Mode mode);
	const ControlInfo &getFlashIntensityInfo() const;
	std::optional<int32_t> getFlashIntensity() const;
	int setFlashIntensity(int32_t intensity);
	const ControlInfo &getFlashTimeoutInfo() const;
	std::optional<int32_t> getFlashTimeout() const;
	int setFlashTimeout(int32_t timeout_us);
	std::optional<StrobeSource> getStrobeSource() const;
	int setStrobeSource(StrobeSource source);
	int startStrobe();
	int stopStrobe();
	const ControlInfo &getTorchIntensityInfo() const;
	std::optional<int32_t> getTorchIntensity() const;
	int setTorchIntensity(int32_t intensity);

protected:
	std::string logPrefix() const override;

private:
	LIBCAMERA_DISABLE_COPY_AND_MOVE(CameraFlash)

	std::optional<int32_t> getSubdevControl(uint32_t id) const;
	int setSubdevControl(uint32_t id, int32_t value);
	int validateDriver();

	const MediaEntity *entity_;
	std::unique_ptr<V4L2Subdevice> subdev_;
	std::string model_;
	const ControlInfoMap *controlInfoMap_ = nullptr;
};

} /* namespace libcamera */
