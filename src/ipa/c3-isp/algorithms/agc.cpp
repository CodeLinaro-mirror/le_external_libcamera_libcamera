/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP AGC/AEC mean-based control algorithm
 */

#include "agc.h"

#include <cmath>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>
#include <libcamera/property_ids.h>

#include "libipa/histogram.h"

/**
 * \file agc.h
 */

namespace libcamera {

using namespace std::literals::chrono_literals;

namespace ipa::c3isp::algorithms {

/**
 * \class Agc
 * \brief A mean-based auto-exposure algorithm
 */

LOG_DEFINE_CATEGORY(C3ISPAgc)

Agc::Agc()
{
}

/**
 * \brief Initialise the AGC algorithm from tuning files
 * \param[in] context The shared IPA context
 * \param[in] tuningData The YamlObject containing Agc tuning data
 *
 * This function calls the base class' tuningData parsers to discover which
 * control values are supported.
 *
 * \return 0 on success or errors from the base class
 */
int Agc::init(IPAContext &context, const YamlObject &tuningData)
{
	int ret;

	ret = parseTuningData(tuningData);
	if (ret)
		return ret;

	context.ctrlMap[&controls::AeEnable] = ControlInfo(false, true);

	context.ctrlMap.merge(controls());

	return 0;
}

/**
 * \brief Configure the AGC given a configInfo
 * \param[in] context The shared IPA context
 * \param[in] configInfo The IPA configuration data
 *
 * \return 0
 */
int Agc::configure(IPAContext &context,
		   [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	const IPASessionConfiguration &configuration = context.configuration;
	IPAActiveState &activeState = context.activeState;

	/* Configure the default gain and exposure */
	activeState.agc.autoEnabled = true;
	activeState.agc.automatic.sensorGain = configuration.agc.minAnalogueGain;
	activeState.agc.automatic.exposure = configuration.agc.defaultExposure;
	activeState.agc.manual.sensorGain = configuration.agc.minAnalogueGain;
	activeState.agc.manual.exposure = configuration.agc.defaultExposure;
	activeState.agc.constraintMode = constraintModes().begin()->first;
	activeState.agc.exposureMode = exposureModeHelpers().begin()->first;

	setLimits(configuration.agc.minShutterSpeed,
		  configuration.agc.maxShutterSpeed,
		  configuration.agc.minAnalogueGain,
		  configuration.agc.maxAnalogueGain);

	resetFrameCount();

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Agc::queueRequest(IPAContext &context, const uint32_t frame,
		       [[maybe_unused]] IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	auto &agc = context.activeState.agc;

	const auto &constraintMode = controls.get(controls::AeConstraintMode);
	agc.constraintMode = constraintMode.value_or(agc.constraintMode);

	const auto &exposureMode = controls.get(controls::AeExposureMode);
	agc.exposureMode = exposureMode.value_or(agc.exposureMode);

	const auto &agcEnable = controls.get(controls::AeEnable);
	if (agcEnable && *agcEnable != agc.autoEnabled) {
		agc.autoEnabled = *agcEnable;

		LOG(C3ISPAgc, Info)
			<< (agc.autoEnabled ? "Enabling" : "Disablin")
			<< " AGC";
	}

	if (agc.autoEnabled)
		return;

	const auto &exposure = controls.get(controls::ExposureTime);
	if (exposure) {
		agc.manual.exposure = *exposure * 1.0us /
				      context.configuration.sensor.lineDuration;

		LOG(C3ISPAgc, Debug)
			<< "Exposure set to " << agc.manual.exposure
			<< " on request sequence " << frame;
	}

	const auto &analogueGain = controls.get(controls::AnalogueGain);
	if (analogueGain) {
		agc.manual.sensorGain = *analogueGain;

		LOG(C3ISPAgc, Debug)
			<< "Analogue gain set to " << agc.manual.sensorGain
			<< " on request sequence " << frame;
	}
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Agc::prepare(IPAContext &context, const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext,
		  C3ISPParams *params)
{
	if (frame > 0)
		return;

	auto AeCfg = params->block<BlockType::AEConfig>();
	AeCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_ENABLE);

	AeCfg->tap_point = C3_ISP_AE_STATS_TAP_MLS;

	/* A 17x15 grid */
	AeCfg->horiz_zones_num = 17;
	AeCfg->vert_zones_num = 15;

	Span<uint8_t> weights{ AeCfg->zone_weight, C3_ISP_AE_MAX_ZONES };
	std::fill(weights.begin(), weights.end(), 1);

	Size sensorSize = context.configuration.sensor.size;
	uint8_t maxPointNum =
		std::max(AeCfg->horiz_zones_num, AeCfg->vert_zones_num) + 1;

	for (unsigned int i = 0; i < maxPointNum; i++) {
		uint16_t hidx = i * sensorSize.width / AeCfg->horiz_zones_num;

		/* Aligned with 2 */
		hidx = hidx / 2 * 2;
		AeCfg->horiz_coord[i] =
			std::min(hidx, (uint16_t)sensorSize.width);

		uint16_t vidx = i * sensorSize.height / AeCfg->vert_zones_num;

		/* Aligned with 2 */
		vidx = vidx / 2 * 2;
		AeCfg->vert_coord[i] =
			std::min(vidx, (uint16_t)sensorSize.height);
	}
}

Histogram Agc::parseStatistics(const c3_isp_stats_info *stats)
{
	const struct c3_isp_ae_stats *info = &stats->ae;
	std::vector<uint8_t> means(C3_ISP_AE_MAX_ZONES, 0);

	/*
	 * Each zone has a 5-bin histogram and the total sum is normalized to
	 * 65535. For the convenience of calculation, we use the centre of the
	 * 5-bin as target area. So it can be assumed that:
	 * hist1 represents the number of brightness 16,
	 * hist2 represents the number of brightness 72,
	 * hist3 represents the number of brightness 128,
	 *
	 * Finally, the average brightness of a zone can be calculated.
	 */
	for (unsigned int i = 0; i < C3_ISP_AE_MAX_ZONES; i++) {
		uint16_t hist2 = 65535 - info->stats[i].hist0 -
				 info->stats[i].hist1 -
				 info->stats[i].hist3 -
				 info->stats[i].hist4;

		uint32_t lumaSum = info->stats[i].hist1 * 16 +
				   hist2 * 72 +
				   info->stats[i].hist3 * 128;

		uint32_t histSum = info->stats[i].hist1 +
				   hist2 +
				   info->stats[i].hist3;

		means[i] = lumaSum / histSum;
	}

	lumaMeans_ = means;

	/* This is a 1024-bin histogram */
	uint32_t *hist = const_cast<uint32_t *>(info->hist);

	return Histogram(Span<uint32_t>(hist, std::size(info->hist)));
}

/**
 * \brief Estimate the relative luminance of the frame with a given gain
 * \param[in] gain The gain to apply in estimating luminance
 *
 * The estimation is based on the average value of the zone. Each
 * average value is multiplied by the gain, and then saturated to
 * to approximate the sensor behaviour at high brightness values.
 * The approximation is quite rough, as it doesn't take into account
 * non-linearities when approaching saturation.
 *
 * The values are normalized to the [0.0, 1.0] range, where 1.0 corresponds
 * to a theoretical perfect reflector of 100% reference white.
 *
 * More detailed information can be found in:
 * https://en.wikipedia.org/wiki/Relative_luminance
 *
 * \return The relative luminance of the frame
 */
double Agc::estimateLuminance(double gain) const
{
	double sum = 0.0;

	for (unsigned int i = 0; i < lumaMeans_.size(); i++)
		sum += std::min(lumaMeans_[i] * gain, 128.0);

	return sum / lumaMeans_.size() / 128;
}

/**
 * \brief Process C3 ISP statistics, and run AGC operations
 * \param[in] context The shared IPA context
 * \param[in] frame The current frame sequence number
 * \param[in] frameContext The current frame context
 * \param[in] stats The C3 ISP statistics and ISP results
 * \param[out] metadata Metadata for the frame, to be filled by the algorithm
 *
 * Identify the current image brightness, and use that to estimate the optimal
 * new exposure and gain for the scene.
 */
void Agc::process(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const c3_isp_stats_info *stats,
		  ControlList &metadata)
{
	Histogram hist = parseStatistics(stats);

	utils::Duration shutterTime;
	double aGain, dGain;

	/*
	 * The Agc algorithm needs to know the effective exposure value that was
	 * applied to the sensor when the statistics were collected.
	 */
	utils::Duration exposureTime = context.configuration.sensor.lineDuration *
				       frameContext.agc.exposure;
	double analogueGain = frameContext.agc.sensorGain;
	utils::Duration effectiveExposureValue = exposureTime * analogueGain;

	std::tie(shutterTime, aGain, dGain) =
		calculateNewEv(context.activeState.agc.constraintMode,
			       context.activeState.agc.exposureMode, hist,
			       effectiveExposureValue);

	LOG(C3ISPAgc, Debug)
		<< "Shutter time, analogue gain and digital gain are "
		<< shutterTime << ", " << aGain << " and " << dGain;

	IPAActiveState &activeState = context.activeState;

	activeState.agc.automatic.exposure = shutterTime / context.configuration.sensor.lineDuration;
	activeState.agc.automatic.sensorGain = aGain;

	metadata.set(controls::AnalogueGain, frameContext.agc.sensorGain);
	metadata.set(controls::ExposureTime, exposureTime.get<std::micro>());

	lumaMeans_ = {};
}

REGISTER_IPA_ALGORITHM(Agc, "Agc")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
