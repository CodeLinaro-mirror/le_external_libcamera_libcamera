/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025 Ideas on Board Oy
 *
 * Helper class that handles sync
 */

#pragma once

#include <libcamera/base/utils.h>

#include <libcamera/controls.h>

namespace libcamera {

namespace ipa {

class SyncHelper
{
public:
	static ControlInfo controlInfo(int64_t maxFrameDuration)
	{
		return ControlInfo(static_cast<int32_t>(-maxFrameDuration / 2),
				   static_cast<int32_t>(maxFrameDuration / 2), 0);
	}

	void setSync(int32_t sync, utils::Duration minFrameDuration)
	{
		utils::Duration value = std::chrono::microseconds(sync);
		frameDurationOffset_ = std::clamp(value,
						  -minFrameDuration, minFrameDuration);
	}

	utils::Duration getSync() const { return frameDurationOffset_; }
	void resetSync() { frameDurationOffset_ = utils::Duration(0); }

private:
	utils::Duration frameDurationOffset_ = utils::Duration(0);
};

} /* namespace ipa */

} /* namespace libcamera */
