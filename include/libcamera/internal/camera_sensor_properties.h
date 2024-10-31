/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google Inc.
 *
 * Database of camera sensor properties
 */

#pragma once

#include <map>
#include <string>

#include <stdint.h>

#include <libcamera/control_ids.h>
#include <libcamera/geometry.h>

namespace libcamera {

struct CameraSensorProperties {
	static const CameraSensorProperties *get(const std::string &sensor);

	Size unitCellSize;
	std::map<controls::draft::TestPatternModeEnum, int32_t> testPatternModes;

	/*
	 * These values are correct for many sensors. Other sensors will need to
	 * have the defaults overwritten in their CameraSensorProperties entry.
	 */
	struct {
		uint8_t exposureDelay;
		uint8_t gainDelay;
		uint8_t vblankDelay;
		uint8_t hblankDelay;
	} sensorDelays;
};

} /* namespace libcamera */
