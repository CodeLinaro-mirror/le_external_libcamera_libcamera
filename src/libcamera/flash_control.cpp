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

	controls[&controls::FlashMode] = ControlInfo(controls::FlashModeValues, controls::FlashModeNone);
	controls[&controls::FlashIntensity] = flash->getFlashIntensityInfo();
	controls[&controls::FlashTimeout] = flash->getFlashTimeoutInfo();
	controls[&controls::FlashStrobeSource] = ControlInfo(controls::FlashStrobeSourceValues, controls::FlashStrobeSourceSoftware);
	controls[&controls::FlashStrobe] = ControlInfo(controls::FlashStrobeValues);
	controls[&controls::FlashTorchIntensity] = flash->getTorchIntensityInfo();
}

void FlashControl::handleFlashControls(CameraFlash *flash, ControlList &controls, ControlList &metadata)
{
	if (!flash)
		return;

	/* Handle controls */

	const auto &flashMode = controls.get(controls::FlashMode);
	if (flashMode) {
		CameraFlash::Mode mode;

		switch (*flashMode) {
		case controls::FlashModeFlash:
			mode = CameraFlash::Mode::Flash;
			break;
		case controls::FlashModeTorch:
			mode = CameraFlash::Mode::Torch;
			break;
		case controls::FlashModeNone:
		default:
			mode = CameraFlash::Mode::None;
			break;
		}

		flash->setMode(mode);
	}

	const auto &flashIntensity = controls.get<int32_t>(controls::FlashIntensity);
	if (flashIntensity)
		flash->setFlashIntensity(*flashIntensity);

	const auto &flashTimeout = controls.get<int32_t>(controls::FlashTimeout);
	if (flashTimeout)
		flash->setFlashTimeout(*flashTimeout);

	const auto &flashStrobeSource = controls.get(controls::FlashStrobeSource);
	if (flashStrobeSource) {
		CameraFlash::StrobeSource source;

		switch (*flashStrobeSource) {
		case controls::FlashStrobeSourceExternal:
			source = CameraFlash::StrobeSource::External;
			break;
		case controls::FlashStrobeSourceSoftware:
		default:
			source = CameraFlash::StrobeSource::Software;
			break;
		}

		flash->setStrobeSource(source);
	}

	const auto &flashStrobe = controls.get(controls::FlashStrobe);
	if (flashStrobe) {
		switch (*flashStrobe) {
		case controls::FlashStrobeStart:
			flash->startStrobe();
			break;
		case controls::FlashStrobeStop:
			flash->stopStrobe();
			break;
		default:
			break;
		}
	}

	const auto &flashTorchIntensity = controls.get<int32_t>(controls::FlashTorchIntensity);
	if (flashTorchIntensity)
		flash->setTorchIntensity(*flashTorchIntensity);

	/* Fill metadata */

	const auto metaFlashMode = flash->getMode();
	if (metaFlashMode) {
		controls::FlashModeEnum mode;

		switch (*metaFlashMode) {
		case CameraFlash::Mode::Flash:
			mode = controls::FlashModeFlash;
			break;
		case CameraFlash::Mode::Torch:
			mode = controls::FlashModeTorch;
			break;
		case CameraFlash::Mode::None:
		default:
			mode = controls::FlashModeNone;
			break;
		}
		metadata.set(controls::FlashMode, mode);
	}

	const auto metaFlashIntensity = flash->getFlashIntensity();
	if (metaFlashIntensity)
		metadata.set(controls::FlashIntensity, *metaFlashIntensity);

	auto metaFlashTimeout = flash->getFlashTimeout();
	if (metaFlashTimeout)
		metadata.set(controls::FlashTimeout, *metaFlashTimeout);

	auto metaStrobeSource = flash->getStrobeSource();
	if (metaStrobeSource) {
		controls::FlashStrobeSourceEnum source;

		switch (*metaStrobeSource) {
		case CameraFlash::StrobeSource::External:
			source = controls::FlashStrobeSourceExternal;
			break;
		case CameraFlash::StrobeSource::Software:
		default:
			source = controls::FlashStrobeSourceSoftware;
			break;
		}
		metadata.set(controls::FlashStrobeSource, source);
	}

	auto metaTorchIntensity = flash->getTorchIntensity();
	if (metaTorchIntensity)
		metadata.set(controls::FlashTorchIntensity, *metaTorchIntensity);
}

} /* namespace libcamera */
