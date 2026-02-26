/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025 Vasiliy Doylov <nekodevelopper@gmail.com>
 *
 * Auto focus
 */

#include "af.h"

#include <stdint.h>

#include <libcamera/base/log.h>

#include "control_ids.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftAutoFocus)

namespace ipa::soft::algorithms {

Af::Af()
{
}

int Af::init(IPAContext &context,
	     [[maybe_unused]] const YamlObject &tuningData)
{
	context.ctrlMap[&controls::LensPosition] = ControlInfo(0.0f, 100.0f, 50.0f);
	return 0;
}

int Af::configure(IPAContext &context,
		  [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	context.activeState.knobs.focus_pos = std::optional<double>();

	return 0;
}

void Af::queueRequest([[maybe_unused]] typename Module::Context &context,
		      [[maybe_unused]] const uint32_t frame,
		      [[maybe_unused]] typename Module::FrameContext &frameContext,
		      const ControlList &controls)
{
	const auto &focus_pos = controls.get(controls::LensPosition);
	if (focus_pos.has_value()) {
		context.activeState.knobs.focus_pos = focus_pos;
		LOG(IPASoftAutoFocus, Debug) << "Setting focus position to " << focus_pos.value();
	}
}

void Af::updateFocus([[maybe_unused]] IPAContext &context, [[maybe_unused]] IPAFrameContext &frameContext, [[maybe_unused]] double exposureMSV)
{
	frameContext.lens.focus_pos = context.activeState.knobs.focus_pos.value_or(50.0) / 100.0 * (context.configuration.focus.focus_max - context.configuration.focus.focus_min);
}

void Af::process([[maybe_unused]] IPAContext &context,
		 [[maybe_unused]] const uint32_t frame,
		 [[maybe_unused]] IPAFrameContext &frameContext,
		 [[maybe_unused]] const SwIspStats *stats,
		 [[maybe_unused]] ControlList &metadata)
{
	updateFocus(context, frameContext, 0);
}

REGISTER_IPA_ALGORITHM(Af, "Af")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
