/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * AGC/AEC mean-based control algorithm
 */

#include "agc.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>
#include <libcamera/geometry.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include "libcamera/internal/value_node.h"

#include "libipa/fixedpoint.h"
#include "libipa/histogram.h"

/**
 * \file agc.h
 */

namespace libcamera {

namespace ipa::rppx1::algorithms {

using WindowWeightQ = UQ<1, 4>;
using MeasureCoeffQ = UQ<1, 7>;

LOG_DEFINE_CATEGORY(RppX1Agc)

int Agc::parseMeteringModes(IPAContext &context, const ValueNode &tuningData)
{
	if (!tuningData.isDictionary())
		LOG(RppX1Agc, Warning)
			<< "'AeMeteringMode' parameter not found in tuning file";

	for (const auto &[key, value] : tuningData.asDict()) {
		auto it = controls::AeMeteringModeNameValueMap.find(key);
		if (it == controls::AeMeteringModeNameValueMap.end()) {
			LOG(RppX1Agc, Warning)
				<< "Skipping unknown metering mode '" << key << "'";
			continue;
		}

		auto weights = value.get<std::vector<float>>().value_or(utils::defopt);
		if (weights.size() != RPPX1_HIST_WEIGHT_GRIDS_SIZE) {
			LOG(RppX1Agc, Error)
				<< "Invalid 'AeMeteringMode:" << key << "': "
				<< "expected " << RPPX1_HIST_WEIGHT_GRIDS_SIZE << " elements, "
				<< "got " << weights.size();
			return -EINVAL;
		}

		for (const auto &x : weights) {
			if (x < 0 || x > 1) {
				LOG(RppX1Agc, Error)
					<< "Invalid 'AeMeteringMode:" << key << "' values: "
					<< "elements must be in [0;1]";
				return -EINVAL;
			}
		}

		meteringModes_.try_emplace(it->second, std::move(weights));
	}

	if (meteringModes_.empty()) {
		LOG(RppX1Agc, Warning)
			<< "No metering modes read from tuning file; defaulting to matrix";

		meteringModes_.try_emplace(
			controls::MeteringMatrix,
			RPPX1_HIST_WEIGHT_GRIDS_SIZE, 1.0f);
	}

	std::vector<ControlValue> meteringModes;
	std::vector<int> meteringModeKeys = utils::map_keys(meteringModes_);
	std::transform(meteringModeKeys.begin(), meteringModeKeys.end(),
		       std::back_inserter(meteringModes),
		       [](int x) { return ControlValue(x); });
	context.ctrlMap[&controls::AeMeteringMode] = ControlInfo(meteringModes);

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Agc::init(IPAContext &context, const ValueNode &tuningData)
{
	int ret;

	ret = agc_.init(tuningData, context.camHelper.get(), {
		.sensorInfo = context.sensorInfo,
		.sensorControls = context.sensorControls,
		.ctrlMap = context.ctrlMap,
	});
	if (ret)
		return ret;

	const ValueNode &meteringModes = tuningData["AeMeteringMode"];
	ret = parseMeteringModes(context, meteringModes);
	if (ret)
		return ret;

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int Agc::configure(IPAContext &context, const IPACameraSensorInfo &configInfo)
{
	int ret = agc_.configure(context.configuration.agc, context.activeState.agc, {
		.sensorInfo = context.sensorInfo,
		.sensorControls = context.sensorControls,
		.ctrlMap = context.ctrlMap,
	});
	if (ret)
		return ret;

	context.activeState.agc.meteringMode =
		static_cast<controls::AeMeteringModeEnum>(meteringModes_.begin()->first);

	context.configuration.agc.measureWindow.h_offs = 0;
	context.configuration.agc.measureWindow.v_offs = 0;
	context.configuration.agc.measureWindow.h_size = configInfo.outputSize.width;
	context.configuration.agc.measureWindow.v_size = configInfo.outputSize.height;

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Agc::queueRequest(IPAContext &context,
		       [[maybe_unused]] const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	auto &agc = context.activeState.agc;

	agc_.queueRequest(context.configuration.agc, agc, frameContext.agc, controls);

	const auto &meteringMode = controls.get(controls::AeMeteringMode);
	if (meteringMode) {
		frameContext.agc.updateMetering = agc.meteringMode != *meteringMode;
		agc.meteringMode =
			static_cast<controls::AeMeteringModeEnum>(*meteringMode);
	}
	frameContext.agc.meteringMode = agc.meteringMode;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Agc::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, RppX1Params *params)
{
	agc_.prepare(context.activeState.agc, frameContext.agc);

	if (frame > 0 && !frameContext.agc.updateMetering)
		return;

	/*
	 * Configure the AEC measurements. Set the window, measure
	 * continuously, and estimate Y as (R + G + B) x (85/256).
	 */
	auto exmConfig = params->block<BlockType::ExmPre1>();
	exmConfig.setEnabled(true);

	/* Jacopo:
	 * we decided to let the driver calculate the sub-window sizes.
	 *
	 * I'm under the impression that all the rounding that happens in the
	 * driver restrict the window enough to work with the limitation of
	 * of measuring after denoise (Note 4 page 159).
	 */

	exmConfig->wnd = context.configuration.agc.measureWindow;
	exmConfig->mode = RPPX1_EXP_MEASURING_MODE_BAYER;

	/*
	 * Jacopo
	 *
	 * RPPX1: set also channel (after denoise) and gains (x1.0) they were
	 * harcoded in the driver.
	 *
	 * last_line can be programmed but the driver does that.
	 */
	exmConfig->channel_sel = RPPX1_MEAS_CHAN_SEL6;
	exmConfig->coeff_r = MeasureCoeffQ(1.f).quantized();
	exmConfig->coeff_g_gr = MeasureCoeffQ(1.f).quantized();
	exmConfig->coeff_b = MeasureCoeffQ(1.f).quantized();
	exmConfig->coeff_gb = MeasureCoeffQ(1.f).quantized(); /* Unused in RGB mode. */

	auto hst = params->block<BlockType::HistPost>();
	hst.setEnabled(true);

	/*
	 * Configure the histogram measurement. Set the window, produce a
	 * luminance histogram, and set the weights and predivider.
	 *
	 * RPP-X1: we decided to let the driver calculate the sub-window sizes..
	 * Also, we can program last_line, but the driver already does that
	 */
	hst->wnd = context.configuration.agc.measureWindow;

	/*
	 * In RGB mode, a linear combination of the RGB components is used
	 * for the histogram. Use the BT.601 RGB -> Y coefficients.
	 *
	 * \todo is this OK?!
	 */
	hst->mode = RPPX1_HIST_MODE_RGB_COMBINED;
	hst->coeff[0] = MeasureCoeffQ(0.299f).quantized();
	hst->coeff[1] = MeasureCoeffQ(0.587f).quantized();
	hst->coeff[2] = MeasureCoeffQ(0.114f).quantized();

	/*
	 * Jacopo:
	 *
	 * RPP-X1: we also have to select post-debayer as the histogram tap
	 * point as we want to operate on RGB data.
	 * It was hardcoded in the kernel driver.
	 */
	hst->channel_sel = RPPX1_MEAS_CHAN_SEL7;

	std::span<const float> weights = meteringModes_.at(frameContext.agc.meteringMode);
	ASSERT(weights.size() == std::size(hst->weights));
	for (size_t i = 0; i < weights.size(); i++) {
		WindowWeightQ w = weights[i];
		hst->weights[i] = w.quantized();

		LOG(RppX1Agc, Debug)
			<< "weights[" << i << "]: " << w;
	}

	/*
	 * Each bin has a 16-bit integral component. So the maximum number in it
	 * is 65535. The worst case is when all pixels are in the same bin.
	 *
	 * To ensure no overflow even in that case, one pixel may be sampled out
	 * of every `skip` pixel.
	 */
	constexpr uint32_t kBinMax = (1u << 16) - 1;
	const double skip = double(hst->wnd.h_size * hst->wnd.v_size) / kBinMax;

	/*
	 * There is a separate vertical/horizontal step setting,
	 * but let's try to find something square-ish.
	 *
	 * Subsampling by `prediv` vertically and horizontally should
	 * ensure that no more that `kBinMax` pixels are sampled.
	 *
	 * \todo Take weights into account?
	 */
	const auto prediv = std::clamp<uint8_t>(std::ceil(std::sqrt(skip)), 1, 127);
	hst->v_stepsize = prediv;
	hst->h_step_inc = 65536 / prediv;

	/* RPP-X1: sample shift/offset to 0 */
	hst->sample_offs = 0;
	hst->sample_shift = 0;

	LOG(RppX1Agc, Debug)
		<< "window: " << Rectangle(hst->wnd.h_offs, hst->wnd.v_offs, hst->wnd.h_size, hst->wnd.v_size) << ", "
		<< "v_stepsize: " << hst->v_stepsize << ", "
		<< "h_step_inc: " << hst->h_step_inc;
}

namespace {

class AgcTraits final : public AgcMeanLuminance::Traits
{
public:
	AgcTraits(std::span<const uint32_t> expMeans, std::span<const float> weights)
		: expMeans_(expMeans), weights_(weights)
	{
		ASSERT(expMeans_.size() == weights_.size());
	}

	/**
	 * \brief Estimate the relative luminance of the frame with a given gain
	 * \param[in] gain The gain to apply to the frame
	 *
	 * This function estimates the average relative luminance of the frame that
	 * would be output by the sensor if an additional \a gain was applied.
	 *
	 * The estimation is based on the AE statistics for the current frame. Y
	 * averages for all cells are first multiplied by the gain, and then saturated
	 * to approximate the sensor behaviour at high brightness values. The
	 * approximation is quite rough, as it doesn't take into account non-linearities
	 * when approaching saturation. In this case, saturating after the conversion to
	 * YUV doesn't take into account the fact that the R, G and B components
	 * contribute differently to the relative luminance.
	 *
	 * The values are normalized to the [0.0, 1.0] range, where 1.0 corresponds to a
	 * theoretical perfect reflector of 100% reference white.
	 *
	 * More detailed information can be found in:
	 * https://en.wikipedia.org/wiki/Relative_luminance
	 *
	 * \return The relative luminance
	 */
	double estimateLuminance(double gain) const override
	{
		constexpr double kMaxExpMean = (1u << 20) - 1;

		double ySum = 0.0;
		double wSum = 0.0;

		/* Sum the averages, saturated to 255. */
		for (unsigned i = 0; i < expMeans_.size(); i++) {
			double w = weights_[i];
			ySum += std::min<double>(expMeans_[i] * gain, kMaxExpMean) * w;
			wSum += w;
		}

		/* \todo Weight with the AWB gains */

		return ySum / wSum / kMaxExpMean;
	}

private:
	std::span<const uint32_t> expMeans_;
	std::span<const float> weights_;
};

} /* namespace */

/**
 * \brief Process RppX1 statistics, and run AGC operations
 * \param[in] context The shared IPA context
 * \param[in] frame The frame context sequence number
 * \param[in] frameContext The current frame context
 * \param[in] stats The RPP-X1 statistics and ISP results
 * \param[out] metadata Metadata for the frame, to be filled by the algorithm
 *
 * Identify the current image brightness, and use that to estimate the optimal
 * new exposure and gain for the scene.
 */
void Agc::process(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext, const RppX1Stats *stats,
		  ControlList &metadata)
{
	const auto histPost = stats->block<StatsType::HistPost>();
	const auto exmPre1 = stats->block<StatsType::ExmPre1>();

	if (!histPost)
		LOG(RppX1Agc, Error) << "HIST_POST data is missing in statistics";
	if (!exmPre1)
		LOG(RppX1Agc, Error) << "EXM_PRE1 data is missing in statistics";

	if (histPost && exmPre1) {
		static_assert(RPPX1_EXM_NUM_WIN == RPPX1_HIST_WEIGHT_GRIDS_SIZE);

		agc_.process(context.configuration.agc, context.activeState.agc, frameContext.agc, {{
			.traits = AgcTraits{
				exmPre1->exp_mean,
				meteringModes_.at(frameContext.agc.meteringMode),
			},
			.yHist = {
				/* The lower 4 bits are fractional and meant to be discarded. */
				histPost->hist_bins,
				[](uint32_t x) { return x >> 4; },
			},
			.exposure = frameContext.sensor.exposure,
			.gain = frameContext.sensor.gain,
		}}, metadata);
	} else {
		agc_.process(context.configuration.agc, context.activeState.agc, frameContext.agc, {}, metadata);
	}

	metadata.set(controls::AeMeteringMode, frameContext.agc.meteringMode);
}

REGISTER_IPA_ALGORITHM(Agc, "Agc")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
