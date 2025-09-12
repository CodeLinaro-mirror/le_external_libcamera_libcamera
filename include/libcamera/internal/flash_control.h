/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, matthias.fend@emfend.at
 *
 * Flash controls helpers for pipeline handlers
 */

#pragma once

#include <libcamera/controls.h>

#include "libcamera/internal/camera_flash.h"

namespace libcamera {

class FlashControl
{
public:
	static void updateFlashControls(CameraFlash *flash, ControlInfoMap::Map &controls);
	static void handleFlashControls(CameraFlash *flash, ControlList &controls, ControlList &metadata);
};

} /* namespace libcamera */
