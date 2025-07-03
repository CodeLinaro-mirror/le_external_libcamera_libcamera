/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer implementation for injecting controls
 */

#include "inject_controls.h"

#include <algorithm>
#include <set>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/layer.h>
#include <libcamera/request.h>

void *init([[maybe_unused]] const std::string &id)
{
	InjectControls *closure = new InjectControls;
	*closure = {};
	return static_cast<void *>(closure);
}

void terminate(void *closure)
{
	InjectControls *obj = static_cast<InjectControls *>(closure);
	delete obj;
}

libcamera::ControlInfoMap::Map updateControls(void *closure, libcamera::ControlInfoMap &ctrls)
{
	InjectControls *obj = static_cast<InjectControls *>(closure);

	auto it = ctrls.find(&libcamera::controls::ExposureTimeMode);
	if (it != ctrls.end()) {
		for (auto entry : it->second.values()) {
			if (entry == libcamera::ControlValue(libcamera::controls::ExposureTimeModeAuto))
				obj->aeAvailable = true;
			if (entry == libcamera::ControlValue(libcamera::controls::ExposureTimeModeManual))
				obj->meAvailable = true;
		}
	}

	it = ctrls.find(&libcamera::controls::AnalogueGainMode);
	if (it != ctrls.end()) {
		for (auto entry : it->second.values()) {
			if (entry == libcamera::ControlValue(libcamera::controls::AnalogueGainModeAuto))
				obj->agAvailable = true;
			if (entry == libcamera::ControlValue(libcamera::controls::AnalogueGainModeManual))
				obj->mgAvailable = true;
		}
	}

	std::set<bool> values;
	if (obj->aeAvailable || obj->agAvailable)
		values.insert(true);
	if (obj->meAvailable || obj->mgAvailable)
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

void queueRequest(void *closure, libcamera::Request *request)
{
	InjectControls *obj = static_cast<InjectControls *>(closure);

	libcamera::ControlList &ctrls = request->controls();
	auto aeEnable = ctrls.get<bool>(libcamera::controls::AeEnable);
	if (!aeEnable)
		return;

	if (*aeEnable) {
		if (obj->aeAvailable) {
			ctrls.set(libcamera::controls::ExposureTimeMode,
				  libcamera::controls::ExposureTimeModeAuto);
		}

		if (obj->agAvailable) {
			ctrls.set(libcamera::controls::AnalogueGainMode,
				  libcamera::controls::AnalogueGainModeAuto);
		}
	} else {
		if (obj->meAvailable) {
			ctrls.set(libcamera::controls::ExposureTimeMode,
				  libcamera::controls::ExposureTimeModeManual);
		}

		if (obj->mgAvailable) {
			ctrls.set(libcamera::controls::AnalogueGainMode,
				  libcamera::controls::AnalogueGainModeManual);
		}
	}
}

void requestCompleted([[maybe_unused]] void *closure, libcamera::Request *request)
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

namespace libcamera {

extern "C" {

[[gnu::visibility("default")]]
struct LayerInfo layerInfo{
	.name = "inject_controls",
	.layerAPIVersion = 1,
};

[[gnu::visibility("default")]]
struct LayerInterface layerInterface{
	.init = init,
	.terminate = terminate,
	.bufferCompleted = nullptr,
	.requestCompleted = requestCompleted,
	.disconnected = nullptr,
	.acquire = nullptr,
	.release = nullptr,
	.controls = updateControls,
	.properties = nullptr,
	.configure = nullptr,
	.createRequest = nullptr,
	.queueRequest = queueRequest,
	.start = nullptr,
	.stop = nullptr,
};

}

} /* namespace libcamera */
