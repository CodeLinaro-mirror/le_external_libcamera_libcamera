/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Exposure and gain
 */

#include "agc.h"

#include <libcamera/base/log.h>

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftExposure)

namespace ipa::soft::algorithms {

int Agc::init(IPAContext &context, [[maybe_unused]] const ValueNode &tuningData)
{
	return agc_.configure(context.configuration.agc.simple, context.activeState.agc.simple, {
		.sensor = context.camHelper.get(),
		.sensorInfo = context.sensorInfo,
		.sensorControls = context.sensorControls,
		.ctrlMap = context.ctrlMap,
		.autoAllowed = true,
	});
}

int Agc::configure(IPAContext &context, [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	return agc_.configure(context.configuration.agc.simple, context.activeState.agc.simple, {
		.sensor = context.camHelper.get(),
		.sensorInfo = context.sensorInfo,
		.sensorControls = context.sensorControls,
		.ctrlMap = context.ctrlMap,
		.autoAllowed = true, // \todo not if raw?
	});
}

void Agc::queueRequest(IPAContext &context, [[maybe_unused]] const uint32_t frame, IPAFrameContext &frameContext, const ControlList &controls)
{
	agc_.queueRequest(context.configuration.agc.simple, context.activeState.agc.simple,
			  frameContext.agc.simple, controls);
}

void Agc::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext, [[maybe_unused]] DebayerParams *params)
{
	agc_.prepare(context.activeState.agc.simple, frameContext.agc.simple);
}

void Agc::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const SwIspStats *stats,
		  ControlList &metadata)
{
	if (stats->valid) {
		agc_.process(context.configuration.agc.simple, context.activeState.agc.simple, frameContext.agc.simple, {{
			.exposure = frameContext.sensor.exposure,
			.gain = frameContext.sensor.gain,
			.stats = *stats,
			.blackLevel = context.activeState.blc.level,
		}}, metadata);
	} else {
		agc_.process(context.configuration.agc.simple, context.activeState.agc.simple,
			     frameContext.agc.simple, {}, metadata);
	}

	frameContext.agc.exposure = frameContext.agc.simple.exposure;
	frameContext.agc.gain = frameContext.agc.simple.gain;
}

REGISTER_IPA_ALGORITHM(Agc, "Agc")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
