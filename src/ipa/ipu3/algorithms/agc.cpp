/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Ideas On Board
 *
 * ipu3_agc.cpp - AGC/AEC mean-based control algorithm
 */

#include "agc.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>
#include <libcamera/ipa/core_ipa_interface.h>

#include "libipa/histogram.h"

/**
 * \file agc.h
 */

namespace libcamera {

using namespace std::literals::chrono_literals;

namespace ipa::ipu3::algorithms {

/**
 * \class Agc
 * \brief A mean-based auto-exposure algorithm
 *
 * This algorithm calculates a shutter time and an analogue gain so that the
 * average value of the green channel of the brightest 2% of pixels approaches
 * 0.5. The AWB gains are not used here, and all cells in the grid have the same
 * weight, like an average-metering case. In this metering mode, the camera uses
 * light information from the entire scene and creates an average for the final
 * exposure setting, giving no weighting to any particular portion of the
 * metered area.
 *
 * Reference: Battiato, Messina & Castorina. (2008). Exposure
 * Correction for Imaging Devices: An Overview. 10.1201/9781420054538.ch12.
 */

LOG_DEFINE_CATEGORY(IPU3Agc)

/* Minimum limit for analogue gain value */
static constexpr double kMinAnalogueGain = 1.0;

/* \todo Honour the FrameDurationLimits control instead of hardcoding a limit */
static constexpr utils::Duration kMaxShutterSpeed = 60ms;

/* Histogram constants */
static constexpr uint32_t knumHistogramBins = 256;

/* Target value to reach for the top 2% of the histogram */
static constexpr double kEvGainTarget = 0.5;

/* Number of frames to wait before calculating stats on minimum exposure */
static constexpr uint32_t kNumStartupFrames = 10;

/*
 * Relative luminance target.
 *
 * It's a number that's chosen so that, when the camera points at a grey
 * target, the resulting image brightness is considered right.
 */
static constexpr double kRelativeLuminanceTarget = 0.16;

Agc::Agc()
	: minShutterSpeed_(0s), maxShutterSpeed_(0s), context_(nullptr)
{
}

/**
 * \brief Initialise the AGC algorith from tuning files
 *
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

	parseRelativeLuminanceTarget(tuningData);

	ret = parseConstraintModes(tuningData);
	if (ret)
		return ret;

	ret = parseExposureModes(tuningData);
	if (ret)
		return ret;

	context.ctrlMap.merge(controls());
	context_ = &context;

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
		   [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	const IPASessionConfiguration &configuration = context.configuration;
	IPAActiveState &activeState = context.activeState;

	stride_ = configuration.grid.stride;

	minShutterSpeed_ = configuration.agc.minShutterSpeed;
	maxShutterSpeed_ = std::min(configuration.agc.maxShutterSpeed,
				    kMaxShutterSpeed);

	minAnalogueGain_ = std::max(configuration.agc.minAnalogueGain, kMinAnalogueGain);
	maxAnalogueGain_ = configuration.agc.maxAnalogueGain;

	/* Configure the default exposure and gain. */
	activeState.agc.gain = minAnalogueGain_;
	activeState.agc.exposure = 10ms / configuration.sensor.lineDuration;

	/*
	 * \todo We should use the first available mode rather than assume that
	 * the "Normal" modes are present in tuning data.
	 */
	context.activeState.agc.constraintMode = controls::ConstraintNormal;
	context.activeState.agc.exposureMode = controls::ExposureNormal;

	for (auto &[id, helper] : exposureModeHelpers()) {
		/* \todo Run this again when FrameDurationLimits is passed in */
		helper->configure(minShutterSpeed_, maxShutterSpeed_,
				  minAnalogueGain_, maxAnalogueGain_);
	}

	return 0;
}

void Agc::parseStatistics(const ipu3_uapi_stats_3a *stats,
			  const ipu3_uapi_grid_config &grid)
{
	uint32_t hist[knumHistogramBins] = { 0 };

	reds_.clear();
	greens_.clear();
	blues_.clear();

	for (unsigned int cellY = 0; cellY < grid.height; cellY++) {
		for (unsigned int cellX = 0; cellX < grid.width; cellX++) {
			uint32_t cellPosition = cellY * stride_ + cellX;

			const ipu3_uapi_awb_set_item *cell =
				reinterpret_cast<const ipu3_uapi_awb_set_item *>(
					&stats->awb_raw_buffer.meta_data[cellPosition]);

			reds_.push_back(cell->R_avg);
			greens_.push_back((cell->Gr_avg + cell->Gb_avg) / 2);
			blues_.push_back(cell->B_avg);

			/*
			 * Store the average green value to estimate the
			 * brightness. Even the overexposed pixels are
			 * taken into account.
			 */
			hist[(cell->Gr_avg + cell->Gb_avg) / 2]++;
		}
	}

	hist_ = Histogram(Span<uint32_t>(hist));
}

double Agc::estimateLuminance(double gain)
{
	ASSERT(reds_.size() == greens_.size());
	ASSERT(greens_.size() == blues_.size());
	const ipu3_uapi_grid_config &grid = context_->configuration.grid.bdsGrid;
	double redSum = 0, greenSum = 0, blueSum = 0;

	for (unsigned int i = 0; i < reds_.size(); i++) {
		redSum += std::min(reds_[i] * gain, 255.0);
		greenSum += std::min(greens_[i] * gain, 255.0);
		blueSum += std::min(blues_[i] * gain, 255.0);
	}

	double ySum = redSum * context_->activeState.awb.gains.red * 0.299
		+ greenSum * context_->activeState.awb.gains.green * 0.587
		+ blueSum * context_->activeState.awb.gains.blue * 0.114;

	return ySum / (grid.height * grid.width) / 255;
}

/**
 * \brief Process IPU3 statistics, and run AGC operations
 * \param[in] context The shared IPA context
 * \param[in] frame The current frame sequence number
 * \param[in] frameContext The current frame context
 * \param[in] stats The IPU3 statistics and ISP results
 * \param[out] metadata Metadata for the frame, to be filled by the algorithm
 *
 * Identify the current image brightness, and use that to estimate the optimal
 * new exposure and gain for the scene.
 */
void Agc::process(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const ipu3_uapi_stats_3a *stats,
		  ControlList &metadata)
{
	parseStatistics(stats, context.configuration.grid.bdsGrid);

	/*
	 * The Agc algorithm needs to know the effective exposure value that was
	 * applied to the sensor when the statistics were collected.
	 */
	utils::Duration exposureTime = context.configuration.sensor.lineDuration
				     * frameContext.sensor.exposure;
	double analogueGain = frameContext.sensor.gain;
	utils::Duration effectiveExposureValue = exposureTime * analogueGain;

	utils::Duration shutterTime;
	double aGain, dGain;
	std::tie(shutterTime, aGain, dGain) =
		calculateNewEv(context.activeState.agc.constraintMode,
			       context.activeState.agc.exposureMode, hist_,
			       effectiveExposureValue);

	LOG(IPU3Agc, Debug)
		<< "Divided up shutter, analogue gain and digital gain are "
		<< shutterTime << ", " << aGain << " and " << dGain;

	IPAActiveState &activeState = context.activeState;
	/* Update the estimated exposure and gain. */
	activeState.agc.exposure = shutterTime / context.configuration.sensor.lineDuration;
	activeState.agc.gain = aGain;

	metadata.set(controls::AnalogueGain, frameContext.sensor.gain);
	metadata.set(controls::ExposureTime, exposureTime.get<std::micro>());

	/* \todo Use VBlank value calculated from each frame exposure. */
	uint32_t vTotal = context.configuration.sensor.size.height
			+ context.configuration.sensor.defVBlank;
	utils::Duration frameDuration = context.configuration.sensor.lineDuration
				      * vTotal;
	metadata.set(controls::FrameDuration, frameDuration.get<std::micro>());

}

REGISTER_IPA_ALGORITHM(Agc, "Agc")

} /* namespace ipa::ipu3::algorithms */

} /* namespace libcamera */
