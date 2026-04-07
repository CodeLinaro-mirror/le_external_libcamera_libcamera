/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024-2026 Red Hat Inc.
 *
 * Auto white balance
 */

#include "awb.h"

#include <numeric>
#include <stdint.h>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "libipa/awb_bayes.h"
#include "libipa/awb_grey.h"
#include "libipa/colours.h"
#include "simple/ipa_context.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftAwb)

namespace ipa::soft::algorithms {

constexpr int32_t kMinColourTemperature = 2500;
constexpr int32_t kMaxColourTemperature = 10000;
constexpr int32_t kDefaultColourTemperature = 5000;

/* Identical to RKISP1AwbStats ... why ? */
class SimpleAwbStats final : public AwbStats
{
public:
	SimpleAwbStats(const RGB<double> &rgbMeans)
		: rgbMeans_(rgbMeans)
	{
		rg_ = rgbMeans_.r() / rgbMeans_.g();
		bg_ = rgbMeans_.b() / rgbMeans_.g();
	}

	double computeColourError(const RGB<double> &gains) const override
	{
		/*
		 * Compute the sum of the squared colour error (non-greyness) as
		 * it appears in the log likelihood equation.
		 */
		double deltaR = gains.r() * rg_ - 1.0;
		double deltaB = gains.b() * bg_ - 1.0;
		double delta2 = deltaR * deltaR + deltaB * deltaB;

		return delta2;
	}

	RGB<double> rgbMeans() const override
	{
		return rgbMeans_;
	}

private:
	RGB<double> rgbMeans_;
	double rg_;
	double bg_;
};

int Awb::init(IPAContext &context, const YamlObject &tuningData)
{
	auto &cmap = context.ctrlMap;

	cmap[&controls::ColourTemperature] = ControlInfo(kMinColourTemperature,
							 kMaxColourTemperature,
							 kDefaultColourTemperature);

	cmap[&controls::AwbEnable] = ControlInfo(false, true);
	cmap[&controls::ColourGains] = ControlInfo(0.0f, 3.996f,
						   Span<const float, 2>{ { 1.0f, 1.0f } });

	if (!tuningData.contains("algorithm"))
		LOG(IPASoftAwb, Info) << "No AWB algorithm specified."
				      << " Default to grey world";

	auto mode = tuningData["algorithm"].get<std::string>("grey");
	if (mode == "grey") {
		awbAlgo_ = std::make_unique<AwbGrey>();
	} else if (mode == "bayes") {
		awbAlgo_ = std::make_unique<AwbBayes>();
	} else {
		LOG(IPASoftAwb, Error) << "Unknown AWB algorithm: " << mode;
		return -EINVAL;
	}
	LOG(IPASoftAwb, Debug) << "Using AWB algorithm: " << mode;

	int ret = awbAlgo_->init(tuningData);
	if (ret) {
		LOG(IPASoftAwb, Error) << "Failed to init AWB algorithm";
		return ret;
	}

	const auto &src = awbAlgo_->controls();
	cmap.insert(src.begin(), src.end());

	return 0;
}

int Awb::configure(IPAContext &context,
		   [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	return awbAlgo_->configure(context.activeState.awb,
				   context.configuration.awb);
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Awb::queueRequest(IPAContext &context,
		       const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	awbAlgo_->queueRequest(context.activeState.awb,
			       frame, frameContext.awb,
			       controls);
}

void Awb::prepare(IPAContext &context,
		  const uint32_t frame,
		  IPAFrameContext &frameContext,
		  DebayerParams *params)
{
	awbAlgo_->prepare(context.activeState.awb,
			  frame, frameContext.awb);

	params->gains = frameContext.awb.gains;
}

void Awb::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const SwIspStats *stats,
		  ControlList &metadata)
{
	IPAActiveState &activeState = context.activeState;
	RGB<float> gains = frameContext.awb.gains;

	metadata.set(controls::AwbEnable, frameContext.awb.autoEnabled);
	metadata.set(controls::ColourGains, { gains.r(), gains.b() });
	metadata.set(controls::ColourTemperature, frameContext.awb.temperatureK);

	if (!stats->valid)
		return;

	const SwIspStats::Histogram &histogram = stats->yHistogram;
	const uint8_t blackLevel = context.activeState.blc.level;

	/*
	 * Black level must be subtracted to get the correct AWB ratios, they
	 * would be off if they were computed from the whole brightness range
	 * rather than from the sensor range.
	 */
	const uint64_t nPixels = std::accumulate(
		histogram.begin(), histogram.end(), uint64_t(0));
	const uint64_t offset = blackLevel * nPixels;
	const uint64_t minValid = 1;

	/*
	 * Make sure the sums are at least minValid, while preventing unsigned
	 * integer underflow.
	 */
	const RGB<uint64_t> sum = stats->sum_.max(offset + minValid) - offset;

	RGB<double> rgbMeans = { { static_cast<double>(sum.r() / nPixels),
				   static_cast<double>(sum.g() / nPixels),
				   static_cast<double>(sum.b() / nPixels) } };

	/*
	 * Todo: Determine the minimum allowed thresholds from the mean
	 * but we currently have the sum - not the mean value!
	 */
	SimpleAwbStats awbStats{ rgbMeans };

	AwbResult awbResult = awbAlgo_->calculateAwb(awbStats, frameContext.lux.lux);

	/* Todo: Check if clamping required */

	/* Filter the values to avoid oscillations. */
	double speed = 0.2;
	double ct = awbResult.colourTemperature;
	ct = ct * speed + activeState.awb.automatic.temperatureK * (1 - speed);
	awbResult.gains = awbResult.gains * speed +
			  activeState.awb.automatic.gains * (1 - speed);

	activeState.awb.automatic.temperatureK = static_cast<unsigned int>(ct);
	activeState.awb.automatic.gains = awbResult.gains;

	LOG(IPASoftAwb, Debug)
		<< std::showpoint
		<< "Means " << rgbMeans << ", gains "
		<< activeState.awb.automatic.gains << ", temp "
		<< activeState.awb.automatic.temperatureK << "K";
}

REGISTER_IPA_ALGORITHM(Awb, "Awb")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
