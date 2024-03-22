/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * exposure_mode_helper.h - Helper class that performs computations relating to exposure
 */
#include "exposure_mode_helper.h"

#include <algorithm>

#include <libcamera/base/log.h>

/**
 * \file exposure_mode_helper.h
 * \brief Helper class that performs computations relating to exposure
 *
 * Exposure modes contain a list of shutter and gain values to determine how to
 * split the supplied exposure time into shutter and gain. As this function is
 * expected to be replicated in various IPAs, this helper class factors it
 * away.
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(ExposureModeHelper)

namespace ipa {

/**
 * \class ExposureModeHelper
 * \brief Class for splitting exposure time into shutter and gain
 */

/**
 * \brief Initialize an ExposureModeHelper instance
 */
ExposureModeHelper::ExposureModeHelper()
	: minShutter_(0), maxShutter_(0), minGain_(0), maxGain_(0)
{
}

ExposureModeHelper::~ExposureModeHelper()
{
}

/**
 * \brief Initialize an ExposureModeHelper instance
 * \param[in] shutter The list of shutter values
 * \param[in] gain The list of gain values
 *
 * When splitting an exposure time into shutter and gain, the shutter will be
 * increased first before increasing the gain. This is done in stages, where
 * each stage is an index into both lists. Both lists consequently need to be
 * the same length.
 *
 * \return Zero on success, negative error code otherwise
 */
int ExposureModeHelper::init(std::vector<utils::Duration> &shutter, std::vector<double> &gain)
{
	if (shutter.size() != gain.size()) {
		LOG(ExposureModeHelper, Error)
			<< "Invalid exposure mode:"
			<< " expected size of 'shutter' and 'gain to be equal,"
			<< " got " << shutter.size() << " and " << gain.size()
			<< " respectively";
		return -EINVAL;
	}

	std::copy(shutter.begin(), shutter.end(), std::back_inserter(shutters_));
	std::copy(gain.begin(), gain.end(), std::back_inserter(gains_));

	/*
	 * Initialize the max shutter and gain if they aren't initialized yet.
	 * This is to protect against the event that configure() is not called
	 * before splitExposure().
	 */
	if (!maxShutter_) {
		if (shutters_.size() > 0)
			maxShutter_ = shutter.back();
	}

	if (!maxGain_) {
		if (gains_.size() > 0)
			maxGain_ = gain.back();
	}

	return 0;
}

/**
 * \brief Configure the ExposureModeHelper
 * \param[in] minShutter The minimum shutter time
 * \param[in] maxShutter The maximum shutter time
 * \param[in] minGain The minimum gain
 * \param[in] maxGain The maximum gain
 *
 * Note that the ExposureModeHelper needs to be reconfigured when
 * FrameDurationLimits is passed, and not just at IPA configuration time.
 *
 * This function configures the maximum shutter and maximum gain.
 */
void ExposureModeHelper::configure(utils::Duration minShutter,
				   utils::Duration maxShutter,
				   double minGain,
				   double maxGain)
{
	minShutter_ = minShutter;
	maxShutter_ = maxShutter;
	minGain_ = minGain;
	maxGain_ = maxGain;
}

utils::Duration ExposureModeHelper::clampShutter(utils::Duration shutter)
{
	return std::clamp(shutter, minShutter_, maxShutter_);
}

double ExposureModeHelper::clampGain(double gain)
{
	return std::clamp(gain, minGain_, maxGain_);
}

std::tuple<utils::Duration, double, double>
ExposureModeHelper::splitExposure(utils::Duration exposure,
				  utils::Duration shutter, bool shutterFixed,
				  double gain, bool gainFixed)
{
	shutter = clampShutter(shutter);
	gain = clampGain(gain);

	/* Not sure why you'd want to do this... */
	if (shutterFixed && gainFixed)
		return { shutter, gain, 1 };

	/* Initial shutter and gain settings are sufficient */
	if (shutter * gain >= exposure) {
		/* Both shutter and gain cannot go lower */
		if (shutter == minShutter_ && gain == minGain_)
			return { shutter, gain, 1 };

		/* Shutter cannot go lower */
		if (shutter == minShutter_ || shutterFixed)
			return { shutter,
				 gainFixed ? gain : clampGain(exposure / shutter),
				 1 };

		/* Gain cannot go lower */
		if (gain == minGain_ || gainFixed)
			return {
				shutterFixed ? shutter : clampShutter(exposure / gain),
				gain,
				1
			};

		/* Both can go lower */
		return { clampShutter(exposure / minGain_),
			 exposure / clampShutter(exposure / minGain_),
			 1 };
	}

	unsigned int stage;
	utils::Duration stageShutter;
	double stageGain;
	double lastStageGain;

	/* We've already done stage 0 above so we start at 1 */
	for (stage = 1; stage < gains_.size(); stage++) {
		stageShutter = shutterFixed ? shutter : clampShutter(shutters_[stage]);
		stageGain = gainFixed ? gain : clampGain(gains_[stage]);
		lastStageGain = gainFixed ? gain : clampGain(gains_[stage - 1]);

		/*
		 * If the product of the new stage shutter and the old stage
		 * gain is sufficient and we can change the shutter, reduce it.
		 */
		if (!shutterFixed && stageShutter * lastStageGain >= exposure)
			return { clampShutter(exposure / lastStageGain), lastStageGain, 1 };

		/*
		 * The new stage shutter with old stage gain were insufficient,
		 * so try the new stage shutter with new stage gain. If it is
		 * sufficient and we can change the shutter, reduce it.
		 */
		if (!shutterFixed && stageShutter * stageGain >= exposure)
			return { clampShutter(exposure / stageGain), stageGain, 1 };

		/*
		 * Same as above, but we can't change the shutter, so change
		 * the gain instead.
		 *
		 * Note that at least one of !shutterFixed and !gainFixed is
		 * guaranteed.
		 */
		if (!gainFixed && stageShutter * stageGain >= exposure)
			return { stageShutter, clampGain(exposure / stageShutter), 1 };
	}

	/* From here on we're going to try to max out shutter then gain */
	shutter = shutterFixed ? shutter : maxShutter_;
	gain = gainFixed ? gain : maxGain_;

	/*
	 * We probably don't want to use the actual maximum analogue gain (as
	 * it'll be unreasonably high), so we'll at least try to max out the
	 * shutter, which is expected to be a bit more reasonable, as it is
	 * limited by FrameDurationLimits and/or the sensor configuration.
	 */
	if (!shutterFixed && shutter * stageGain >= exposure)
		return { clampShutter(exposure / stageGain), stageGain, 1 };

	/*
	 * If that's still not enough exposure, or if shutter is fixed, then
	 * we'll max out the analogue gain before using digital gain.
	 */
	if (!gainFixed && shutter * gain >= exposure)
		return { shutter, clampGain(exposure / shutter), 1 };

	/*
	 * We're out of shutter time and analogue gain; send the rest of the
	 * exposure time to digital gain.
	 */
	return { shutter, gain, exposure / (shutter * gain) };
}

/**
 * \brief Split exposure time into shutter and gain
 * \param[in] exposure Exposure time
 * \return Tuple of shutter time, analogue gain, and digital gain
 */
std::tuple<utils::Duration, double, double>
ExposureModeHelper::splitExposure(utils::Duration exposure)
{
	ASSERT(maxShutter_);
	ASSERT(maxGain_);
	utils::Duration shutter;
	double gain;

	if (shutters_.size()) {
		shutter = shutters_.at(0);
		gain = gains_.at(0);
	} else {
		shutter = maxShutter_;
		gain = maxGain_;
	}

	return splitExposure(exposure, shutter, false, gain, false);
}

/**
 * \brief Split exposure time into shutter and gain, with fixed shutter
 * \param[in] exposure Exposure time
 * \param[in] fixedShutter Fixed shutter time
 *
 * Same as the base splitExposure, but with a fixed shutter (aka "shutter priority").
 *
 * \return Tuple of shutter time, analogue gain, and digital gain
 */
std::tuple<utils::Duration, double, double>
ExposureModeHelper::splitExposure(utils::Duration exposure, utils::Duration fixedShutter)
{
	ASSERT(maxGain_);
	double gain = gains_.size() ? gains_.at(0) : maxGain_;

	return splitExposure(exposure, fixedShutter, true, gain, false);
}

/**
 * \brief Split exposure time into shutter and gain, with fixed gain
 * \param[in] exposure Exposure time
 * \param[in] fixedGain Fixed gain
 *
 * Same as the base splitExposure, but with a fixed gain (aka "gain priority").
 *
 * \return Tuple of shutter time, analogue gain, and digital gain
 */
std::tuple<utils::Duration, double, double>
ExposureModeHelper::splitExposure(utils::Duration exposure, double fixedGain)
{
	ASSERT(maxShutter_);
	utils::Duration shutter = shutters_.size() ? shutters_.at(0) : maxShutter_;

	return splitExposure(exposure, shutter, false, fixedGain, true);
}

/**
 * \fn ExposureModeHelper::minShutter()
 * \brief Retrieve the configured minimum shutter time
 */

/**
 * \fn ExposureModeHelper::maxShutter()
 * \brief Retrieve the configured maximum shutter time
 */

/**
 * \fn ExposureModeHelper::minGain()
 * \brief Retrieve the configured minimum gain
 */

/**
 * \fn ExposureModeHelper::maxGain()
 * \brief Retrieve the configured maximum gain
 */

} /* namespace ipa */

} /* namespace libcamera */
