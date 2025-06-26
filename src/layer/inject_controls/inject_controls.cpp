/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer implementation for injecting controls
 */

#include "inject_controls.h"

#include <algorithm>
#include <set>

#include <libcamera/layer.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/request.h>

namespace layer {

namespace inject_controls {

libcamera::ControlInfoMap::Map controls(libcamera::ControlInfoMap &ctrls)
{
	auto it = ctrls.find(&libcamera::controls::ExposureTimeMode);
	if (it != ctrls.end()) {
		for (auto entry : it->second.values()) {
			if (entry == libcamera::ControlValue(libcamera::controls::ExposureTimeModeAuto))
				aeAvailable_ = true;
			if (entry == libcamera::ControlValue(libcamera::controls::ExposureTimeModeManual))
				meAvailable_ = true;
		}
	}

	it = ctrls.find(&libcamera::controls::AnalogueGainMode);
	if (it != ctrls.end()) {
		for (auto entry : it->second.values()) {
			if (entry == libcamera::ControlValue(libcamera::controls::AnalogueGainModeAuto))
				agAvailable_ = true;
			if (entry == libcamera::ControlValue(libcamera::controls::AnalogueGainModeManual))
				mgAvailable_ = true;
		}
	}

	std::set<bool> values;
	if (aeAvailable_ || agAvailable_)
		values.insert(true);
	if (meAvailable_ || mgAvailable_)
		values.insert(false);

	if (values.empty())
		return {};

	if (values.size() == 1) {
		bool value = *values.begin();
		return { { &libcamera::controls::AeEnable,
			   libcamera::ControlInfo(value, value, value) } };
	}

	return { { &libcamera::controls::AeEnable, libcamera::ControlInfo(false, true, true) } };
}

void queueRequest(libcamera::Request *request)
{
	libcamera::ControlList &ctrls = request->controls();
	auto aeEnable = ctrls.get<bool>(libcamera::controls::AeEnable);
	if (!aeEnable)
		return;

	if (*aeEnable) {
		if (aeAvailable_) {
			ctrls.set(libcamera::controls::ExposureTimeMode,
				  libcamera::controls::ExposureTimeModeAuto);
		}

		if (agAvailable_) {
			ctrls.set(libcamera::controls::AnalogueGainMode,
				  libcamera::controls::AnalogueGainModeAuto);
		}
	} else {
		if (meAvailable_) {
			ctrls.set(libcamera::controls::ExposureTimeMode,
				  libcamera::controls::ExposureTimeModeManual);
		}

		if (mgAvailable_) {
			ctrls.set(libcamera::controls::AnalogueGainMode,
				  libcamera::controls::AnalogueGainModeManual);
		}
	}
}

void requestCompleted(libcamera::Request *request)
{
	libcamera::ControlList &metadata = request->metadata();

	auto eMode = metadata.get<int>(libcamera::controls::ExposureTimeMode);
	auto aMode = metadata.get<int>(libcamera::controls::AnalogueGainMode);

	if (!eMode && !aMode)
		return;

	bool ae = eMode && eMode == libcamera::controls::ExposureTimeModeAuto;
	bool me = eMode && eMode == libcamera::controls::ExposureTimeModeManual;
	bool ag = aMode && aMode == libcamera::controls::AnalogueGainModeAuto;
	bool mg = aMode && aMode == libcamera::controls::AnalogueGainModeManual;

	/* Exposure time not reported at all; use gain only */
	if (!ae && !me) {
		metadata.set(libcamera::controls::AeEnable, ag);
		return;
	}

	/* Analogue gain not reported at all; use exposure time only */
	if (!ag && !mg) {
		metadata.set(libcamera::controls::AeEnable, ae);
		return;
	}

	/*
	 * Gain mode and exposure mode are not equal; therefore at least one is
	 * manual, so set AeEnable to false
	 */
	if (ag != ae) {
		metadata.set(libcamera::controls::AeEnable, false);
		return;
	}

	/* ag and ae are equal, so just choose one */
	metadata.set(libcamera::controls::AeEnable, ag);
	return;
}

} /* namespace inject_controls */

} /* namespace layer */

namespace libcamera {

extern "C" {

struct Layer layerInfo {
	.name = "inject_controls",
	.layerAPIVersion = 1,
	.init = nullptr,
	.bufferCompleted = nullptr,
	.requestCompleted = layer::inject_controls::requestCompleted,
	.disconnected = nullptr,
	.acquire = nullptr,
	.release = nullptr,
	.controls = layer::inject_controls::controls,
	.properties = nullptr,
	.streams = nullptr,
	.generateConfiguration = nullptr,
	.configure = nullptr,
	.createRequest = nullptr,
	.queueRequest = layer::inject_controls::queueRequest,
	.start = nullptr,
	.stop = nullptr,
};

}

} /* namespace libcamera */
