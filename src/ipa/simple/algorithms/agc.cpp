/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Exposure and gain
 */

#include "agc.h"

#include <variant>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libipa/histogram.h>

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftExposure)

namespace ipa::soft::algorithms {

namespace {

class AgcTraits : public AgcMeanLuminance::Traits
{
public:
	AgcTraits(const SwIspStats &stats)
		: stats_(stats)
	{
	}

	double estimateLuminance(double gain) const override
	{
		double sum = 0;
		double count = 0;

		for (const auto &[i, cnt] : utils::enumerate(stats_.yHistogram)) {
			sum += std::min(1.0, gain * i / stats_.yHistogram.size()) * cnt;
			count += cnt;
		}

		return sum / count;
	}

private:
	const SwIspStats &stats_;
};

}

int Agc::init(IPAContext &context, const ValueNode &tuningData)
{
	const AgcAlgorithm::ConfigurationParams config = {
		.sensor = context.camHelper.get(),
		.sensorInfo = context.sensorInfo,
		.sensorControls = context.sensorControls,
		.ctrlMap = context.ctrlMap,
		.autoAllowed = true,
	};

	if (config.sensor)
		agc_.emplace<AgcMeanLuminanceAlgorithm>();
	else
		agc_.emplace<AgcSimpleAlgorithm>();

	return std::visit(utils::overloaded {
		[&](AgcSimpleAlgorithm &impl) {
			return impl.configure(context.configuration.agc.simple,
					      context.activeState.agc.simple,
					      config);
		},
		[&](AgcMeanLuminanceAlgorithm &impl) {
			int ret = impl.init(tuningData);
			if (ret)
				return ret;

			return impl.configure(context.configuration.agc.ml,
					      context.activeState.agc.ml,
					      config);
		},
	}, agc_);
}

int Agc::configure(IPAContext &context, [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	const AgcAlgorithm::ConfigurationParams config = {
		.sensor = context.camHelper.get(),
		.sensorInfo = context.sensorInfo,
		.sensorControls = context.sensorControls,
		.ctrlMap = context.ctrlMap,
		.autoAllowed = true, // \todo if not raw?
	};

	return std::visit(utils::overloaded {
		[&](AgcSimpleAlgorithm &impl) {
			return impl.configure(context.configuration.agc.simple,
					      context.activeState.agc.simple,
					      config);
		},
		[&](AgcMeanLuminanceAlgorithm &impl) {
			return impl.configure(context.configuration.agc.ml,
					      context.activeState.agc.ml,
					      config);
		},
	}, agc_);
}

void Agc::queueRequest(IPAContext &context, [[maybe_unused]] const uint32_t frame, IPAFrameContext &frameContext, const ControlList &controls)
{
	std::visit(utils::overloaded {
		[&](AgcSimpleAlgorithm &impl) {
			impl.queueRequest(context.configuration.agc.simple,
					  context.activeState.agc.simple,
					  frameContext.agc.simple, controls);
		},
		[&](AgcMeanLuminanceAlgorithm &impl) {
			impl.queueRequest(context.configuration.agc.ml,
					  context.activeState.agc.ml,
					  frameContext.agc.ml, controls);
		},
	}, agc_);
}

void Agc::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext, [[maybe_unused]] DebayerParams *params)
{
	std::visit(utils::overloaded {
		[&](AgcSimpleAlgorithm &impl) {
			impl.prepare(context.activeState.agc.simple, frameContext.agc.simple);
		},
		[&](AgcMeanLuminanceAlgorithm &impl) {
			impl.prepare(context.activeState.agc.ml, frameContext.agc.ml);
		},
	}, agc_);
}

void Agc::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const SwIspStats *stats,
		  ControlList &metadata)
{
	std::visit(utils::overloaded {
		[&](AgcSimpleAlgorithm &impl) {
			if (stats->valid) {
				impl.process(context.configuration.agc.simple, context.activeState.agc.simple, frameContext.agc.simple, {{
					.exposure = frameContext.sensor.exposure,
					.gain = frameContext.sensor.gain,
					.stats = *stats,
					.blackLevel = context.activeState.blc.level,
				}}, metadata);
			} else {
				impl.process(context.configuration.agc.simple, context.activeState.agc.simple,
					     frameContext.agc.simple, {}, metadata);
			}

			frameContext.agc.exposure = frameContext.agc.simple.exposure;
			frameContext.agc.gain = frameContext.agc.simple.gain;
		},
		[&](AgcMeanLuminanceAlgorithm &impl) {
			if (stats->valid) {
				Histogram hist(stats->yHistogram);

				impl.process(context.configuration.agc.ml, context.activeState.agc.ml, frameContext.agc.ml, {{
					.traits = AgcTraits(*stats),
					.hist = hist,
					.exposure = uint32_t(frameContext.sensor.exposure),
					.gain = frameContext.sensor.gain,
				}}, metadata);

			} else {
				impl.process(context.configuration.agc.ml, context.activeState.agc.ml,
					     frameContext.agc.ml, {}, metadata);
			}

			frameContext.agc.exposure = frameContext.agc.ml.exposure;
			frameContext.agc.gain = frameContext.agc.ml.gain;
		},
	}, agc_);
}

REGISTER_IPA_ALGORITHM(Agc, "Agc")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
