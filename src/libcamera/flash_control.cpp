/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, matthias.fend@emfend.at
 *
 * Flash controls helpers for pipeline handlers
 */

#include "libcamera/internal/flash_control.h"

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

namespace libcamera {

void FlashControl::updateFlashControls(CameraFlash *flash, ControlInfoMap::Map &controls)
{
	if (!flash)
		return;

	controls[&controls::draft::FlashMode] = ControlInfo(controls::draft::FlashModeValues, controls::draft::FlashModeNone);
	controls[&controls::draft::FlashIntensity] = flash->getFlashIntensityInfo();
	controls[&controls::draft::FlashTimeout] = flash->getFlashTimeoutInfo();
	controls[&controls::draft::FlashStrobeSource] = ControlInfo(controls::draft::FlashStrobeSourceValues, controls::draft::FlashStrobeSourceSoftware);
	controls[&controls::draft::FlashStrobe] = ControlInfo(controls::draft::FlashStrobeValues);
	controls[&controls::draft::FlashTorchIntensity] = flash->getTorchIntensityInfo();
}

void FlashControl::handleFlashControls(CameraFlash *flash, ControlList &controls, ControlList &metadata)
{
	if (!flash)
		return;

	const auto &flashMode = controls.get(controls::draft::FlashMode);
	if (flashMode) {
		CameraFlash::Mode mode;

		switch (*flashMode) {
		case controls::draft::FlashModeFlash:
			mode = CameraFlash::Mode::Flash;
			break;
		case controls::draft::FlashModeTorch:
			mode = CameraFlash::Mode::Torch;
			break;
		case controls::draft::FlashModeNone:
		default:
			mode = CameraFlash::Mode::None;
			break;
		}

		flash->setMode(mode);
	}

	const auto &flashIntensity = controls.get<int32_t>(controls::draft::FlashIntensity);
	if (flashIntensity)
		flash->setFlashIntensity(*flashIntensity);

	const auto &flashTimeout = controls.get<int32_t>(controls::draft::FlashTimeout);
	if (flashTimeout)
		flash->setFlashTimeout(*flashTimeout);

	const auto &flashStrobeSource = controls.get(controls::draft::FlashStrobeSource);
	if (flashStrobeSource) {
		CameraFlash::StrobeSource source;

		switch (*flashStrobeSource) {
		case controls::draft::FlashStrobeSourceExternal:
			source = CameraFlash::StrobeSource::External;
			break;
		case controls::draft::FlashStrobeSourceSoftware:
		default:
			source = CameraFlash::StrobeSource::Software;
			break;
		}

		flash->setStrobeSource(source);
	}

	const auto &flashStrobe = controls.get(controls::draft::FlashStrobe);
	if (flashStrobe) {
		switch (*flashStrobe) {
		case controls::draft::FlashStrobeEnum::FlashStrobeStart:
			flash->startStrobe();
			break;
		case controls::draft::FlashStrobeEnum::FlashStrobeStop:
			flash->stopStrobe();
			break;
		default:
			break;
		}
	}

	const auto &flashTorchIntensity = controls.get<int32_t>(controls::draft::FlashTorchIntensity);
	if (flashTorchIntensity)
		flash->setTorchIntensity(*flashTorchIntensity);

	metadata.set(controls::draft::FlashMode, flash->getMode());
	metadata.set(controls::draft::FlashIntensity, flash->getFlashIntensity());
	metadata.set(controls::draft::FlashTimeout, flash->getFlashTimeout());
	metadata.set(controls::draft::FlashStrobeSource, flash->getStrobeSource());
	metadata.set(controls::draft::FlashTorchIntensity, flash->getTorchIntensity());
}

} /* namespace libcamera */
