/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Ideas on Board Oy
 *
 * Base class for mean luminance AGC algorithms
 */

#include "agc_mean_luminance.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <linux/v4l2-controls.h>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>

#include "exposure_mode_helper.h"

using namespace libcamera::controls;

/**
 * \file agc_mean_luminance.h
 * \brief Class implementing mean luminance AEGC
 */

namespace libcamera {

using namespace std::literals::chrono_literals;

LOG_DEFINE_CATEGORY(AgcMeanLuminance)

namespace ipa {

/*
 * Number of frames for which to run the algorithm at full speed, before slowing
 * down to prevent large and jarring changes in exposure from frame to frame.
 */
static constexpr uint32_t kNumStartupFrames = 10;

/*
 * Default relative luminance target
 *
 * This value should be chosen so that when the camera points at a grey target,
 * the resulting image brightness looks "right". Custom values can be passed
 * as the relativeLuminanceTarget value in sensor tuning files.
 */
static constexpr double kDefaultRelativeLuminanceTarget = 0.16;

/*
 * Maximum relative luminance target
 *
 * This value limits the relative luminance target after applying the exposure
 * compensation. Targeting a value above this limit results in saturation
 * and the inability to regulate properly.
 */
static constexpr double kMaxRelativeLuminanceTarget = 0.95;

/*
 * Default lux level
 *
 * If no lux level or a zero lux level is specified, but PWLs are used to
 * specify luminance targets, this default level is used.
 */
static constexpr unsigned int kDefaultLuxLevel = 500;

/**
 * \struct AgcMeanLuminance::AgcConstraint
 * \brief The boundaries and target for an AeConstraintMode constraint
 *
 * This structure describes an AeConstraintMode constraint for the purposes of
 * this algorithm. These constraints are expressed as a pair of quantile
 * boundaries for a histogram, along with a luminance target and a bounds-type.
 * The algorithm uses the constraints by ensuring that the defined portion of a
 * luminance histogram (I.E. lying between the two quantiles) is above or below
 * the given luminance value.
 */

/**
 * \enum AgcMeanLuminance::AgcConstraint::Bound
 * \brief Specify whether the constraint defines a lower or upper bound
 * \var AgcMeanLuminance::AgcConstraint::Lower
 * \brief The constraint defines a lower bound
 * \var AgcMeanLuminance::AgcConstraint::Upper
 * \brief The constraint defines an upper bound
 */

/**
 * \var AgcMeanLuminance::AgcConstraint::bound
 * \brief The type of constraint bound
 */

/**
 * \var AgcMeanLuminance::AgcConstraint::qLo
 * \brief The lower quantile to use for the constraint
 */

/**
 * \var AgcMeanLuminance::AgcConstraint::qHi
 * \brief The upper quantile to use for the constraint
 */

/**
 * \var AgcMeanLuminance::AgcConstraint::yTarget
 * \brief The luminance target for the constraint
 */

/**
 * \class AgcMeanLuminance::Traits
 * \brief A collection of callbacks
 *
 * This type contains virtual methods that provide the necessary pieces of
 * information for the algorithm, and are to be implemented by the user.
 */

 /**
 * \fn AgcMeanLuminance::Traits::estimateLuminance(double gain)
 * \brief Estimate the luminance of an image, adjusted by a given gain
 * \param[in] gain The gain with which to adjust the luminance estimate
 *
 * This function estimates the average relative luminance of the frame that
 * would be output by the sensor if an additional \a gain was applied. It is a
 * pure virtual function because estimation of luminance is a hardware-specific
 * operation, which depends wholly on the format of the stats that are delivered
 * to libcamera from the ISP. Derived classes must override this function with
 * one that calculates the normalised mean luminance value across the entire
 * image.
 *
 * \return The normalised relative luminance of the image
 */

/**
 * \class AgcMeanLuminance
 * \brief A mean-based auto-exposure algorithm
 *
 * This algorithm calculates an exposure time, analogue and digital gain such
 * that the normalised mean luminance value of an image is driven towards a
 * target, which itself is discovered from tuning data. The algorithm is a
 * two-stage process.
 *
 * In the first stage, an initial gain value is derived by iteratively comparing
 * the gain-adjusted mean luminance across the entire image against a target,
 * and selecting a value which pushes it as closely as possible towards the
 * target.
 *
 * In the second stage we calculate the gain required to drive the average of a
 * section of a histogram to a target value, where the target and the boundaries
 * of the section of the histogram used in the calculation are taken from the
 * values defined for the currently configured AeConstraintMode within the
 * tuning data. This class provides a helper function to parse those tuning data
 * to discover the constraints, and so requires a specific format for those
 * data which is described in \ref parseTuningData(). The gain from the first
 * stage is then clamped to the gain from this stage.
 *
 * The final gain is used to adjust the effective exposure value of the image,
 * and that new exposure value is divided into exposure time, analogue gain and
 * digital gain according to the selected AeExposureMode. This class uses the
 * \ref ExposureModeHelper class to assist in that division, and expects the
 * data needed to initialise that class to be present in tuning data in a
 * format described in \ref parseTuningData().
 *
 * In order to be able to use this algorithm an IPA module needs to be able to
 * do the following:
 *
 * 1. Provide a luminance estimation across an entire image.
 * 2. Provide a luminance Histogram for the image to use in calculating
 *    constraint compliance. The precision of the Histogram that is available
 *    will determine the supportable precision of the constraints.
 *
 * IPA modules that want to use this class to implement their AEGC algorithm
 * should derive it and provide an overriding estimateLuminance() function for
 * this class to use. They must call parseTuningData() in init(), and must also
 * call setLimits() and resetFrameCounter() in configure(). They may then use
 * calculateNewEv() in process(). If the limits passed to setLimits() change for
 * any reason (for example, in response to a FrameDurationLimit control being
 * passed in queueRequest()) then setLimits() must be called again with the new
 * values.
 */

AgcMeanLuminance::AgcMeanLuminance()
	: filteredExposure_(0s), luxWarningEnabled_(true),
	  exposureCompensation_(1.0), frameCount_(0), lux_(0)
{
}

AgcMeanLuminance::~AgcMeanLuminance() = default;

int AgcMeanLuminance::parseRelativeLuminanceTarget(const ValueNode &tuningData)
{
	auto &target = tuningData["relativeLuminanceTarget"];
	if (!target) {
		relativeLuminanceTarget_ = { { { { 0.0, kDefaultRelativeLuminanceTarget } } } };
		return 0;
	}

	auto pwl = target.get<Pwl>();
	if (!pwl) {
		LOG(AgcMeanLuminance, Error)
			<< "Failed to load relative luminance target.";
		return -EINVAL;
	}

	relativeLuminanceTarget_ = std::move(*pwl);
	return 0;
}

int AgcMeanLuminance::parseConstraint(const ValueNode &modeDict, int32_t id)
{
	for (const auto &[boundName, content] : modeDict.asDict()) {
		if (boundName != "upper" && boundName != "lower") {
			LOG(AgcMeanLuminance, Warning)
				<< "Ignoring unknown constraint bound '" << boundName << "'";
			continue;
		}

		unsigned int idx = static_cast<unsigned int>(boundName == "upper");
		AgcConstraint::Bound bound = static_cast<AgcConstraint::Bound>(idx);
		double qLo = content["qLo"].get<double>().value_or(0.98);
		double qHi = content["qHi"].get<double>().value_or(1.0);
		auto yTarget = content["yTarget"].get<Pwl>();
		if (!yTarget) {
			LOG(AgcMeanLuminance, Error)
				<< "Failed to parse yTarget";
			return -EINVAL;
		}

		AgcConstraint constraint = { bound, qLo, qHi, std::move(*yTarget) };

		if (!constraintModes_.count(id))
			constraintModes_[id] = {};

		if (idx)
			constraintModes_[id].push_back(constraint);
		else
			constraintModes_[id].insert(constraintModes_[id].begin(), constraint);
	}

	return 0;
}

int AgcMeanLuminance::parseConstraintModes(const ValueNode &tuningData)
{
	std::vector<ControlValue> availableConstraintModes;

	const ValueNode &constraintModes = tuningData[controls::AeConstraintMode.name()];
	if (constraintModes.isDictionary()) {
		for (const auto &[modeName, modeDict] : constraintModes.asDict()) {
			if (AeConstraintModeNameValueMap.find(modeName) ==
			    AeConstraintModeNameValueMap.end()) {
				LOG(AgcMeanLuminance, Warning)
					<< "Skipping unknown constraint mode '" << modeName << "'";
				continue;
			}

			if (!modeDict.isDictionary()) {
				LOG(AgcMeanLuminance, Error)
					<< "Invalid constraint mode '" << modeName << "'";
				return -EINVAL;
			}

			int ret = parseConstraint(modeDict,
						  AeConstraintModeNameValueMap.at(modeName));
			if (ret)
				return ret;

			availableConstraintModes.push_back(
				AeConstraintModeNameValueMap.at(modeName));
		}
	}

	/*
	 * If the tuning data file contains no constraints then we use the
	 * default constraint that the IPU3/RkISP1 Agc algorithms were adhering
	 * to anyway before centralisation; this constraint forces the top 2% of
	 * the histogram to be at least 0.5.
	 */
	if (constraintModes_.empty()) {
		AgcConstraint constraint = {
			AgcConstraint::Bound::Lower,
			0.98,
			1.0,
			Pwl({ { { 0.0, 0.5 } } })
		};

		constraintModes_[controls::ConstraintNormal].insert(
			constraintModes_[controls::ConstraintNormal].begin(),
			constraint);
		availableConstraintModes.push_back(controls::ConstraintNormal);
	}

	controls_[&controls::AeConstraintMode] = ControlInfo(availableConstraintModes);

	return 0;
}

int AgcMeanLuminance::parseExposureModes(const ValueNode &tuningData)
{
	std::vector<ControlValue> availableExposureModes;

	const ValueNode &exposureModes = tuningData[controls::AeExposureMode.name()];
	if (exposureModes.isDictionary()) {
		for (const auto &[modeName, modeValues] : exposureModes.asDict()) {
			if (AeExposureModeNameValueMap.find(modeName) ==
			    AeExposureModeNameValueMap.end()) {
				LOG(AgcMeanLuminance, Warning)
					<< "Skipping unknown exposure mode '" << modeName << "'";
				continue;
			}

			if (!modeValues.isDictionary()) {
				LOG(AgcMeanLuminance, Error)
					<< "Invalid exposure mode '" << modeName << "'";
				return -EINVAL;
			}

			std::vector<uint32_t> exposureTimes =
				modeValues["exposureTime"].get<std::vector<uint32_t>>().value_or(utils::defopt);
			std::vector<double> gains =
				modeValues["gain"].get<std::vector<double>>().value_or(utils::defopt);

			if (exposureTimes.size() != gains.size()) {
				LOG(AgcMeanLuminance, Error)
					<< "Exposure time and gain array sizes unequal";
				return -EINVAL;
			}

			if (exposureTimes.empty()) {
				LOG(AgcMeanLuminance, Error)
					<< "Exposure time and gain arrays are empty";
				return -EINVAL;
			}

			std::vector<std::pair<utils::Duration, double>> stages;
			for (unsigned int i = 0; i < exposureTimes.size(); i++) {
				stages.push_back({
					std::chrono::microseconds(exposureTimes[i]),
					gains[i]
				});
			}

			std::shared_ptr<ExposureModeHelper> helper =
				std::make_shared<ExposureModeHelper>(stages);

			exposureModeHelpers_[AeExposureModeNameValueMap.at(modeName)] = helper;
			availableExposureModes.push_back(AeExposureModeNameValueMap.at(modeName));
		}
	}

	/*
	 * If we don't have any exposure modes in the tuning data we create an
	 * ExposureModeHelper using an empty vector of stages. This will result
	 * in the ExposureModeHelper simply driving the exposure time as high as
	 * possible before touching gain.
	 */
	if (availableExposureModes.empty()) {
		int32_t exposureModeId = controls::ExposureNormal;
		std::vector<std::pair<utils::Duration, double>> stages = { };

		std::shared_ptr<ExposureModeHelper> helper =
			std::make_shared<ExposureModeHelper>(stages);

		exposureModeHelpers_[exposureModeId] = helper;
		availableExposureModes.push_back(exposureModeId);
	}

	controls_[&controls::AeExposureMode] = ControlInfo(availableExposureModes);

	return 0;
}

/**
 * \brief Configure the exposure mode helpers
 * \param[in] lineDuration The sensor line length
 * \param[in] sensorHelper The sensor helper
 *
 * This function configures the exposure mode helpers so they can correctly
 * take quantization effects into account.
 */
void AgcMeanLuminance::configure(utils::Duration lineDuration,
				 const CameraSensorHelper *sensorHelper)
{
	for (auto &[id, helper] : exposureModeHelpers_)
		helper->configure(lineDuration, sensorHelper);

	luxWarningEnabled_ = true;
}

/**
 * \brief Parse tuning data for AeConstraintMode and AeExposureMode controls
 * \param[in] tuningData the ValueNode representing the tuning data
 *
 * This function parses tuning data to build the list of allowed values for the
 * AeConstraintMode and AeExposureMode controls. Those tuning data must provide
 * the data in a specific format; the Agc algorithm's tuning data should contain
 * a dictionary called AeConstraintMode containing per-mode setting dictionaries
 * with the key being a value from \ref controls::AeConstraintModeNameValueMap.
 * The yTarget can either be provided as single value or as array in which case
 * it is interpreted as a PWL mapping lux levels to yTarget values. Each mode
 * dict may contain either a "lower" or "upper" key or both, for example:
 *
 * \code{.unparsed}
 * algorithms:
 *   - Agc:
 *       AeConstraintMode:
 *         ConstraintNormal:
 *           lower:
 *             qLo: 0.98
 *             qHi: 1.0
 *             yTarget: 0.5
 *         ConstraintHighlight:
 *           lower:
 *             qLo: 0.98
 *             qHi: 1.0
 *             yTarget: 0.5
 *           upper:
 *             qLo: 0.98
 *             qHi: 1.0
 *             yTarget: [ 100, 0.8, 20000, 0.5 ]
 *
 * \endcode
 *
 * For the AeExposureMode control the data should contain a dictionary called
 * AeExposureMode containing per-mode setting dictionaries with the key being a
 * value from \ref controls::AeExposureModeNameValueMap. Each mode dict should
 * contain an array of exposure times with the key "exposureTime" and an array
 * of gain values with the key "gain", in this format:
 *
 * \code{.unparsed}
 * algorithms:
 *   - Agc:
 *       AeExposureMode:
 *         ExposureNormal:
 *           exposureTime: [ 100, 10000, 30000, 60000, 120000 ]
 *           gain: [ 2.0, 4.0, 6.0, 8.0, 10.0 ]
 *         ExposureShort:
 *           exposureTime: [ 100, 10000, 30000, 60000, 120000 ]
 *           gain: [ 2.0, 4.0, 6.0, 8.0, 10.0 ]
 *
 * \endcode
 *
 * \return 0 on success or a negative error code
 */
int AgcMeanLuminance::parseTuningData(const ValueNode &tuningData)
{
	int ret;

	ret = parseRelativeLuminanceTarget(tuningData);
	if (ret)
		return ret;

	ret = parseConstraintModes(tuningData);
	if (ret)
		return ret;

	return parseExposureModes(tuningData);
}

/**
 * \fn AgcMeanLuminance::setExposureCompensation()
 * \brief Set the exposure compensation value
 * \param[in] gain The exposure compensation gain
 *
 * This function sets the exposure compensation value to be used in the
 * AGC calculations. It is expressed as gain instead of EV.
 */

/**
 * \fn AgcMeanLuminance::setLux(int lux)
 * \brief Set the lux level
 * \param[in] lux The lux level
 *
 * This function sets the lux level to be used in the AGC calculations. A value
 * of 0 means no measurement and a default value of \a kDefaultLuxLevel is used
 * if necessary.
 */

/**
 * \brief Set the ExposureModeHelper limits for this class
 * \param[in] minExposureTime Minimum exposure time to allow
 * \param[in] maxExposureTime Maximum ewposure time to allow
 * \param[in] minGain Minimum gain to allow
 * \param[in] maxGain Maximum gain to allow
 * \param[in] constraints Additional constraints to apply
 *
 * This function calls \ref ExposureModeHelper::setLimits() for each
 * ExposureModeHelper that has been created for this class.
 */
void AgcMeanLuminance::setLimits(utils::Duration minExposureTime,
				 utils::Duration maxExposureTime,
				 double minGain, double maxGain,
				 std::vector<AgcMeanLuminance::AgcConstraint> constraints)
{
	for (auto &[id, helper] : exposureModeHelpers_)
		helper->setLimits(minExposureTime, maxExposureTime, minGain, maxGain);

	additionalConstraints_ = std::move(constraints);
}

/**
 * \fn AgcMeanLuminance::constraintModes()
 * \brief Get the constraint modes that have been parsed from tuning data
 */

/**
 * \fn AgcMeanLuminance::exposureModeHelpers()
 * \brief Get the ExposureModeHelpers that have been parsed from tuning data
 */

/**
 * \fn AgcMeanLuminance::controls()
 * \brief Get the controls that have been generated after parsing tuning data
 */

/**
 * \brief Estimate the initial gain needed to achieve a relative luminance
 * target
 * \return The calculated initial gain
 */
double AgcMeanLuminance::estimateInitialGain(const Traits &traits) const
{
	double yTarget = effectiveYTarget();
	double yGain = 1.0;

	/*
	* To account for non-linearity caused by saturation, the value needs to
	* be estimated in an iterative process, as multiplying by a gain will
	* not increase the relative luminance by the same factor if some image
	* regions are saturated.
	*/
	for (unsigned int i = 0; i < 8; i++) {
		double yValue = traits.estimateLuminance(yGain);
		double extra_gain = std::min(10.0, yTarget / (yValue + .001));

		yGain *= extra_gain;
		LOG(AgcMeanLuminance, Debug) << "Y value: " << yValue
				<< ", Y target: " << yTarget
				<< ", gives gain " << yGain;

		if (utils::abs_diff(extra_gain, 1.0) < 0.01)
			break;
	}

	return yGain;
}

/**
 * \brief Clamp gain within the bounds of a defined constraint
 * \param[in] constraintModeIndex The index of the constraint to adhere to
 * \param[in] hist A histogram over which to calculate inter-quantile means
 * \param[in] gain The gain to clamp
 *
 * \return The gain clamped within the constraint bounds
 */
double AgcMeanLuminance::constraintClampGain(uint32_t constraintModeIndex,
					     const Histogram &hist,
					     double gain)
{
	auto applyConstraint = [this, &gain, &hist](const AgcConstraint &constraint) {
		double lux = lux_;

		if (relativeLuminanceTarget_.size() > 1 && lux_ == 0)
			lux = kDefaultLuxLevel;

		double target = constraint.yTarget.eval(
			constraint.yTarget.domain().clamp(lux));
		double newGain = target * hist.bins() /
				 hist.interQuantileMean(constraint.qLo, constraint.qHi);

		if (constraint.bound == AgcConstraint::Bound::Lower &&
		    newGain > gain) {
			LOG(AgcMeanLuminance, Debug)
				<< "Apply lower bound: " << gain << " to "
				<< newGain;
			gain = newGain;
		}

		if (constraint.bound == AgcConstraint::Bound::Upper &&
		    newGain < gain) {
			LOG(AgcMeanLuminance, Debug)
				<< "Apply upper bound: " << gain << " to "
				<< newGain;
			gain = newGain;
		}
	};

	std::vector<AgcConstraint> &constraints = constraintModes_[constraintModeIndex];
	std::for_each(constraints.begin(), constraints.end(), applyConstraint);

	std::for_each(additionalConstraints_.begin(), additionalConstraints_.end(), applyConstraint);

	return gain;
}

/**
 * \brief Get the currently effective y target
 *
 * This function returns the current y target including exposure compensation.
 *
 * \return The y target value
 */
double AgcMeanLuminance::effectiveYTarget() const
{
	double lux = lux_;
	if (relativeLuminanceTarget_.size() > 1 && lux_ == 0) {
		/*
		 * Warn after a few frames if there is still no lux measurement
		 * available. The number of 10 is chosen a bit arbitrarily. It
		 * is big enough to skip the frames that get queued on start
		 * (and therefore are expected to have no valid lux value) and
		 * small enough to show up quickly.
		 */
		if (frameCount_ > 10 && luxWarningEnabled_) {
			luxWarningEnabled_ = false;
			LOG(AgcMeanLuminance, Warning)
				<< "Missing lux value for luminance target "
				   "calculation, default to "
				<< kDefaultLuxLevel
				<< ". Note that the Lux algorithm must be "
				   "included before the Agc algorithm.";
		}

		lux = kDefaultLuxLevel;
	}

	double luminanceTarget = relativeLuminanceTarget_.eval(
		relativeLuminanceTarget_.domain().clamp(lux));

	return std::min(luminanceTarget * exposureCompensation_,
			kMaxRelativeLuminanceTarget);
}

/**
 * \brief Apply a filter on the exposure value to limit the speed of changes
 * \param[in] exposureValue The target exposure from the AGC algorithm
 *
 * The speed of the filter is adaptive, and will produce the target quicker
 * during startup, or when the target exposure is within 20% of the most recent
 * filter output.
 *
 * \return The filtered exposure
 */
utils::Duration AgcMeanLuminance::filterExposure(utils::Duration exposureValue)
{
	double speed = 0.2;

	/* Adapt instantly if we are in startup phase. */
	if (frameCount_ < kNumStartupFrames)
		speed = 1.0;

	/*
	 * If we are close to the desired result, go faster to avoid making
	 * multiple micro-adjustments.
	 * \todo Make this customisable?
	 */
	if (filteredExposure_ < 1.2 * exposureValue &&
	    filteredExposure_ > 0.8 * exposureValue)
		speed = sqrt(speed);

	filteredExposure_ = speed * exposureValue +
			    filteredExposure_ * (1.0 - speed);

	return filteredExposure_;
}

/**
 * \brief Calculate the new exposure value and splut it between exposure time
 * and gain
 * \param[in] constraintModeIndex The index of the current constraint mode
 * \param[in] exposureModeIndex The index of the current exposure mode
 * \param[in] yHist A Histogram from the ISP statistics to use in constraining
 * the calculated gain
 * \param[in] effectiveExposureValue The EV applied to the frame from which the
 * statistics in use derive
 * \param[in] traits The traits object implementing the necessary functions
 *
 * Calculate a new exposure value to try to obtain the target. The calculated
 * exposure value is filtered to prevent rapid changes from frame to frame, and
 * divided into exposure time, analogue, quantization and digital gain.
 *
 * \return Tuple of exposure time, analogue gain, quantization gain and digital
 * gain
 */
std::tuple<utils::Duration, double, double, double>
AgcMeanLuminance::calculateNewEv(uint32_t constraintModeIndex,
				 uint32_t exposureModeIndex,
				 const Histogram &yHist,
				 utils::Duration effectiveExposureValue,
				 const Traits &traits)
{
	/*
	 * The pipeline handler should validate that we have received an allowed
	 * value for AeExposureMode.
	 */
	std::shared_ptr<ExposureModeHelper> exposureModeHelper =
		exposureModeHelpers_.at(exposureModeIndex);

	if (effectiveExposureValue == 0s) {
		LOG(AgcMeanLuminance, Error)
			<< "Effective exposure value is 0. This is a bug in AGC "
			   "and must be fixed for proper operation.";
		/*
		 * Return an arbitrary exposure time > 0 to ensure regulation
		 * doesn't get stuck with 0 in case the sensor driver allows a
		 * min exposure of 0.
		 */
		return exposureModeHelper->splitExposure(10ms);
	}

	double gain = estimateInitialGain(traits);
	gain = constraintClampGain(constraintModeIndex, yHist, gain);

	/*
	 * We don't check whether we're already close to the target, because
	 * even if the effective exposure value is the same as the last frame's
	 * we could have switched to an exposure mode that would require a new
	 * pass through the splitExposure() function.
	 */

	utils::Duration newExposureValue = effectiveExposureValue * gain;

	/*
	 * We filter the exposure value to make sure changes are not too jarring
	 * from frame to frame.
	 */
	newExposureValue = filterExposure(newExposureValue);

	frameCount_++;
	return exposureModeHelper->splitExposure(newExposureValue);
}

/**
 * \fn AgcMeanLuminance::resetFrameCount()
 * \brief Reset the frame counter
 *
 * This function resets the internal frame counter, which exists to help the
 * algorithm decide whether it should respond instantly or not. The expectation
 * is for derived classes to call this function before each camera start call in
 * their configure() function.
 */

/**
 * \class AgcMeanLuminanceAlgorithm
 * \brief AgcMeanLuminance wrapper for implementing the Algorithm interface
 *
 * \todo DigitalGain, DigitalGainMode
 */

/**
 * \struct AgcMeanLuminanceAlgorithm::Session
 * \brief Session configuration for AgcMeanLuminanceAlgorithm
 *
 * \var AgcMeanLuminanceAlgorithm::Session::minExposureTime
 * \brief Minimum exposure time supported with the configured sensor
 *
 * \var AgcMeanLuminanceAlgorithm::Session::maxExposureTime
 * \brief Maximum exposure time supported with the configured sensor
 *
 * \var AgcMeanLuminanceAlgorithm::Session::minAnalogueGain
 * \brief Minimum analogue gain supported with the configured sensor
 *
 * \var AgcMeanLuminanceAlgorithm::Session::maxAnalogueGain
 * \brief Maximum analogue gain supported with the configured sensor
 *
 * \var AgcMeanLuminanceAlgorithm::Session::minFrameDuration
 * \brief Minimum frame duration supported with the configured sensor
 *
 * \var AgcMeanLuminanceAlgorithm::Session::maxFrameDuration
 * \brief Maximum frame duration supported with the configured sensor
 *
 * \var AgcMeanLuminanceAlgorithm::Session::lineDuration
 * \brief Line duration with the configured sensor and output size
 *
 * \var AgcMeanLuminanceAlgorithm::Session::sensor
 * \brief Details of the sensor configuration
 *
 * \var AgcMeanLuminanceAlgorithm::Session::sensor.outputSize
 * \brief Configured output size of the sensor
 *
 * \var AgcMeanLuminanceAlgorithm::Session::autoAllowed
 * \brief Whether automatic controls are allowed
 */

/**
 * \struct AgcMeanLuminanceAlgorithm::ActiveState
 * \brief Active state for AgcMeanLuminanceAlgorithm
 *
 * The \a automatic variables track the latest values computed by algorithm
 * based on the latest processed statistics. All other variables track the
 * consolidated controls requested in queued requests.
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::manual
 * \brief Manual exposure time and analog gain (set through requests)
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::manual.exposure
 * \brief Manual exposure time expressed as a number of lines as set by the
 * ExposureTime control
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::manual.gain
 * \brief Manual analogue gain as set by the AnalogueGain control
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::automatic
 * \brief Automatic exposure time and analog gain (computed by the algorithm)
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::automatic.exposure
 * \brief Automatic exposure time expressed as a number of lines
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::automatic.gain
 * \brief Automatic analogue gain multiplier
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::automatic.quantizationGain
 * \brief Automatic quantization gain multiplier
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::automatic.yTarget
 * \brief Automatically determined luminance target
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::autoExposureEnabled
 * \brief Manual/automatic AGC state (exposure) as set by the ExposureTimeMode control
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::autoGainEnabled
 * \brief Manual/automatic AGC state (gain) as set by the AnalogueGainMode control
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::exposureValue
 * \brief Exposure value as set by the ExposureValue control
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::constraintMode
 * \brief Constraint mode as set by the AeConstraintMode control
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::exposureMode
 * \brief Exposure mode as set by the AeExposureMode control
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::minFrameDuration
 * \brief Minimum frame duration as set by the FrameDurationLimits control
 *
 * \var AgcMeanLuminanceAlgorithm::ActiveState::maxFrameDuration
 * \brief Maximum frame duration as set by the FrameDurationLimits control
 */

/**
 * \struct AgcMeanLuminanceAlgorithm::FrameContext
 * \brief Per-frame context for AgcMeanLuminanceAlgorithm
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::exposure
 * \brief Exposure time expressed as a number of lines computed by the algorithm
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::gain
 * \brief Analogue gain multiplier computed by the algorithm
 *
 * The gain should be adapted to the sensor specific gain code before applying.
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::quantizationGain
 * \brief Quantization gain multiplier computed by the algorithm
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::exposureValue
 * \brief Exposure value as set by the ExposureValue control
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::yTarget
 * \brief Luminance target computed by the algorithm
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::vblank
 * \brief Vertical blanking parameter computed by the algorithm
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::autoExposureEnabled
 * \brief Manual/automatic AGC state (exposure) as set by the ExposureTimeMode control
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::autoGainEnabled
 * \brief Manual/automatic AGC state (gain) as set by the AnalogueGainMode control
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::constraintMode
 * \brief Constraint mode as set by the AeConstraintMode control
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::exposureMode
 * \brief Exposure mode as set by the AeExposureMode control
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::minFrameDuration
 * \brief Minimum frame duration as set by the FrameDurationLimits control
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::maxFrameDuration
 * \brief Maximum frame duration as set by the FrameDurationLimits control
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::frameDuration
 * \brief The actual FrameDuration used by the algorithm for the frame
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::autoExposureModeChange
 * \brief Indicate if autoExposureEnabled has changed from true in the previous
 * frame to false in the current frame, and no manual exposure value has been
 * supplied in the current frame.
 *
 * \var AgcMeanLuminanceAlgorithm::FrameContext::autoGainModeChange
 * \brief Indicate if autoGainEnabled has changed from true in the previous
 * frame to false in the current frame, and no manual gain value has been
 * supplied in the current frame.
 */

/**
 * \struct AgcMeanLuminanceAlgorithm::ConfigurationParams
 * \brief Parameters for AgcMeanLuminanceAlgorithm::configure()
 *
 * \var AgcMeanLuminanceAlgorithm::ConfigurationParams::sensor
 * \brief CameraSensorHelper for the sensor
 *
 * \var AgcMeanLuminanceAlgorithm::ConfigurationParams::sensorInfo
 * \brief Details of the sensor
 *
 * \var AgcMeanLuminanceAlgorithm::ConfigurationParams::sensorControls
 * \brief ControlInfoMap of the sensor
 *
 * \var AgcMeanLuminanceAlgorithm::ConfigurationParams::ctrlMap
 * \brief ControlMap to update with controls
 *
 * \var AgcMeanLuminanceAlgorithm::ConfigurationParams::autoAllowed
 * \brief Whether to enable auto controls
 */

/**
 * \struct AgcMeanLuminanceAlgorithm::ProcessParams
 * \brief Parameters for AgcMeanLuminanceAlgorithm::process()
 *
 * \var AgcMeanLuminanceAlgorithm::ProcessParams::traits
 * \brief Implementation of AgcMeanLuminance::Traits
 *
 * \var AgcMeanLuminanceAlgorithm::ProcessParams::hist
 * \brief Luminance histogram of the frame
 *
 * \var AgcMeanLuminanceAlgorithm::ProcessParams::exposure
 * \brief Effective exposure of the frame
 *
 * \var AgcMeanLuminanceAlgorithm::ProcessParams::gain
 * \brief Effective gain of the frame
 *
 * \var AgcMeanLuminanceAlgorithm::ProcessParams::additionalConstraints
 * \brief Addition AgcMeanLuminance::AgcConstraints to apply
 *
 * \var AgcMeanLuminanceAlgorithm::ProcessParams::lux
 * \brief Lux value for the frame
 */

/**
 * \brief Load tuning data
 */
int AgcMeanLuminanceAlgorithm::init(const ValueNode &tuningData)
{
	int ret = impl_.parseTuningData(tuningData);
	if (ret)
		return ret;

	return 0;
}

/**
 * \brief Initialize the session configuration and active state
 */
int AgcMeanLuminanceAlgorithm::configure(Session &session, ActiveState &state, const ConfigurationParams &config)
{
	session = {};
	session.lineDuration = config.sensorInfo.minLineLength * 1.0s
		/ config.sensorInfo.pixelRate;
	session.sensor.outputSize = config.sensorInfo.outputSize;
	session.autoAllowed = config.autoAllowed;

	const double lineDurationUs = session.lineDuration.get<std::micro>();

	/*
	 * Compute exposure time limits from the V4L2_CID_EXPOSURE control
	 * limits and the line duration.
	 */

	const ControlInfo &v4l2Exposure = config.sensorControls.find(V4L2_CID_EXPOSURE)->second;
	int32_t minExposure = v4l2Exposure.min().get<int32_t>();
	int32_t maxExposure = v4l2Exposure.max().get<int32_t>();
	int32_t defExposure = v4l2Exposure.def().get<int32_t>();
	config.ctrlMap[&controls::ExposureTime] = ControlInfo{
		static_cast<int32_t>(minExposure * lineDurationUs),
		static_cast<int32_t>(maxExposure * lineDurationUs),
		static_cast<int32_t>(defExposure * lineDurationUs),
	};

	/* Compute the analogue gain limits. */
	const ControlInfo &v4l2Gain = config.sensorControls.find(V4L2_CID_ANALOGUE_GAIN)->second;
	float minGain = config.sensor.gain(v4l2Gain.min().get<int32_t>());
	float maxGain = config.sensor.gain(v4l2Gain.max().get<int32_t>());
	float defGain = config.sensor.gain(v4l2Gain.def().get<int32_t>());
	config.ctrlMap[&controls::AnalogueGain] = ControlInfo{
		minGain,
		maxGain,
		defGain,
	};

	LOG(AgcMeanLuminance, Debug)
		<< "Exposure: [" << minExposure << ", " << maxExposure
		<< "], gain: [" << minGain << ", " << maxGain << "]";

	/*
	* Compute the frame duration limits.
	*
	* The frame length is computed assuming a fixed line length combined
	* with the vertical frame sizes.
	*/
	const ControlInfo &v4l2HBlank = config.sensorControls.find(V4L2_CID_HBLANK)->second;
	uint32_t hblank = v4l2HBlank.def().get<int32_t>();
	uint32_t lineLength = config.sensorInfo.outputSize.width + hblank;

	const ControlInfo &v4l2VBlank = config.sensorControls.find(V4L2_CID_VBLANK)->second;
	std::array<uint32_t, 3> frameHeights{
		v4l2VBlank.min().get<int32_t>() + config.sensorInfo.outputSize.height,
		v4l2VBlank.max().get<int32_t>() + config.sensorInfo.outputSize.height,
		v4l2VBlank.def().get<int32_t>() + config.sensorInfo.outputSize.height,
	};

	std::array<int64_t, 3> frameDurations;
	for (unsigned int i = 0; i < frameHeights.size(); ++i) {
		uint64_t frameSize = lineLength * frameHeights[i];
		frameDurations[i] = frameSize / (config.sensorInfo.pixelRate / 1000000U);
	}

	config.ctrlMap[&controls::FrameDurationLimits] = ControlInfo{
		frameDurations[0],
		frameDurations[1],
		Span<const int64_t, 2>{ { frameDurations[2], frameDurations[2] } },
	};

	session.minFrameDuration = std::chrono::microseconds(frameDurations[0]);
	session.maxFrameDuration = std::chrono::microseconds(frameDurations[1]);

	/*
	 * When the AGC computes the new exposure values for a frame, it needs
	 * to know the limits for exposure time and analogue gain. As it depends
	 * on the sensor, update it with the controls.
	 *
	 * \todo take VBLANK into account for maximum exposure time
	 */
	session.minExposureTime = minExposure * session.lineDuration;
	session.maxExposureTime = maxExposure * session.lineDuration;
	session.minAnalogueGain = minGain;
	session.maxAnalogueGain = maxGain;

	impl_.configure(session.lineDuration, &config.sensor);
	impl_.setLimits(session.minExposureTime, session.maxExposureTime,
			session.minAnalogueGain, session.maxAnalogueGain,
			{});
	impl_.resetFrameCount();

	/* Configure the default exposure and gain. */
	state = {};
	state.automatic.gain = session.minAnalogueGain;
	state.automatic.exposure = 10ms / session.lineDuration;
	state.automatic.quantizationGain = 1;
	state.automatic.yTarget = impl_.effectiveYTarget();
	state.manual.gain = state.automatic.gain;
	state.manual.exposure = state.automatic.exposure;
	state.autoExposureEnabled = session.autoAllowed;
	state.autoGainEnabled = session.autoAllowed;
	state.exposureValue = 0;

	state.constraintMode =
		static_cast<controls::AeConstraintModeEnum>(impl_.constraintModes().begin()->first);
	state.exposureMode =
		static_cast<controls::AeExposureModeEnum>(impl_.exposureModeHelpers().begin()->first);

	state.minFrameDuration = session.minFrameDuration;
	state.maxFrameDuration = session.maxFrameDuration;

	const auto add = [&](const ControlId &cid, const auto &automatic, const auto &manual) {
		std::array<ControlValue, 2> values;
		size_t count = 0;

		if (session.autoAllowed)
			values[count++] = ControlValue(automatic);

		values[count++] = ControlValue(manual);

		config.ctrlMap[&cid] = ControlInfo{
			{ values.data(), count },
			ControlValue(session.autoAllowed ? automatic : manual),
		};
	};

	add(controls::ExposureTimeMode, controls::ExposureTimeModeAuto, controls::ExposureTimeModeManual);
	add(controls::AnalogueGainMode, controls::AnalogueGainModeAuto, controls::AnalogueGainModeManual);

	/* \todo Move this to the `Camera` class. */
	config.ctrlMap[&controls::AeEnable] = ControlInfo{
		false,
		session.autoAllowed,
		session.autoAllowed,
	};

	// \todo Should these be added/removed based on `session.autoAllowed` ?
	config.ctrlMap[&controls::ExposureValue] = ControlInfo(-8.0f, 8.0f, 0.0f);

	for (const auto &[id, info] : impl_.controls())
		config.ctrlMap[id] = info;

	return 0;
}

/**
 * \brief Handle a \a queueRequest operation
 */
void AgcMeanLuminanceAlgorithm::queueRequest(const Session &session, ActiveState &state,
				FrameContext &frameContext, const ControlList &controls)
{
	if (session.autoAllowed) {
		const auto &aeEnable = controls.get(controls::ExposureTimeMode);
		if (aeEnable &&
		    (*aeEnable == controls::ExposureTimeModeAuto) != state.autoExposureEnabled) {
			state.autoExposureEnabled = (*aeEnable == controls::ExposureTimeModeAuto);

			LOG(AgcMeanLuminance, Debug)
				<< (state.autoExposureEnabled ? "Enabling" : "Disabling")
				<< " AGC (exposure)";

			/*
			 * If we go from auto -> manual with no manual control
			 * set, use the last computed value, which we don't
			 * know until prepare() so save this information.
			 *
			 * \todo Check the previous frame at prepare() time
			 * instead of saving a flag here
			 */
			if (!state.autoExposureEnabled && !controls.get(controls::ExposureTime))
				frameContext.autoExposureModeChange = true;
		}

		const auto &agEnable = controls.get(controls::AnalogueGainMode);
		if (agEnable &&
		    (*agEnable == controls::AnalogueGainModeAuto) != state.autoGainEnabled) {
			state.autoGainEnabled = (*agEnable == controls::AnalogueGainModeAuto);

			LOG(AgcMeanLuminance, Debug)
				<< (state.autoGainEnabled ? "Enabling" : "Disabling")
				<< " AGC (gain)";
			/*
			 * If we go from auto -> manual with no manual control
			 * set, use the last computed value, which we don't
			 * know until prepare() so save this information.
			 */
			if (!state.autoGainEnabled && !controls.get(controls::AnalogueGain))
				frameContext.autoGainModeChange = true;
		}
	}

	const auto &exposure = controls.get(controls::ExposureTime);
	if (exposure && !state.autoExposureEnabled) {
		state.manual.exposure = *exposure * 1.0us / session.lineDuration;

		LOG(AgcMeanLuminance, Debug)
			<< "Set exposure to " << state.manual.exposure;
	}

	const auto &gain = controls.get(controls::AnalogueGain);
	if (gain && !state.autoGainEnabled) {
		state.manual.gain = *gain;

		LOG(AgcMeanLuminance, Debug) << "Set gain to " << state.manual.gain;
	}

	frameContext.autoExposureEnabled = state.autoExposureEnabled;
	frameContext.autoGainEnabled = state.autoGainEnabled;

	if (!frameContext.autoExposureEnabled)
		frameContext.exposure = state.manual.exposure;
	if (!frameContext.autoGainEnabled)
		frameContext.gain = state.manual.gain;

	if (!frameContext.autoExposureEnabled &&
	    !frameContext.autoGainEnabled)
		frameContext.quantizationGain = 1.0;

	const auto &exposureMode = controls.get(controls::AeExposureMode);
	if (exposureMode)
		state.exposureMode =
			static_cast<controls::AeExposureModeEnum>(*exposureMode);
	frameContext.exposureMode = state.exposureMode;

	const auto &constraintMode = controls.get(controls::AeConstraintMode);
	if (constraintMode)
		state.constraintMode =
			static_cast<controls::AeConstraintModeEnum>(*constraintMode);
	frameContext.constraintMode = state.constraintMode;

	const auto &exposureValue = controls.get(controls::ExposureValue);
	if (exposureValue)
		state.exposureValue = *exposureValue;
	frameContext.exposureValue = state.exposureValue;

	const auto &frameDurationLimits = controls.get(controls::FrameDurationLimits);
	if (frameDurationLimits) {
		/* Limit the control value to the limits in ControlInfo */
		state.minFrameDuration = std::clamp<utils::Duration>(
			std::chrono::microseconds((*frameDurationLimits).front()),
			session.minFrameDuration,
			session.maxFrameDuration
		);

		state.maxFrameDuration = std::clamp<utils::Duration>(
			std::chrono::microseconds((*frameDurationLimits).back()),
			session.minFrameDuration,
			session.maxFrameDuration
		);
	}
	frameContext.minFrameDuration = state.minFrameDuration;
	frameContext.maxFrameDuration = state.maxFrameDuration;
}

/**
 * \brief Handle a \a prepare operation
 */
void AgcMeanLuminanceAlgorithm::prepare(ActiveState &state, FrameContext &frameContext)
{
	uint32_t activeAutoExposure = state.automatic.exposure;
	double activeAutoGain = state.automatic.gain;
	double activeAutoQGain = state.automatic.quantizationGain;

	/* Populate exposure and gain in auto mode */
	if (frameContext.autoExposureEnabled) {
		frameContext.exposure = activeAutoExposure;
		frameContext.quantizationGain = activeAutoQGain;
	}
	if (frameContext.autoGainEnabled) {
		frameContext.gain = activeAutoGain;
		frameContext.quantizationGain = activeAutoQGain;
	}

	/*
	 * Populate manual exposure and gain from the active auto values when
	 * transitioning from auto to manual
	 */
	if (!frameContext.autoExposureEnabled && frameContext.autoExposureModeChange) {
		state.manual.exposure = activeAutoExposure;
		frameContext.exposure = activeAutoExposure;
	}
	if (!frameContext.autoGainEnabled && frameContext.autoGainModeChange) {
		state.manual.gain = activeAutoGain;
		frameContext.gain = activeAutoGain;
		frameContext.quantizationGain = activeAutoQGain;
	}

	frameContext.yTarget = state.automatic.yTarget;
}

/**
 * \brief Handle a \a process operation
 */
void AgcMeanLuminanceAlgorithm::process(const Session &session, ActiveState &state,
					FrameContext &frameContext, std::optional<ProcessParams> &&params,
					ControlList &metadata)
{
	const utils::Duration &lineDuration = session.lineDuration;
	utils::Duration newExposureTime = {};

	if (params) {
		ASSERT(session.autoAllowed);

		/*
		* Set the AGC limits using the fixed exposure time and/or gain in
		* manual mode, or the sensor limits in auto mode.
		*/
		utils::Duration minExposureTime;
		utils::Duration maxExposureTime;
		double minAnalogueGain;
		double maxAnalogueGain;

		if (frameContext.autoExposureEnabled) {
			minExposureTime = session.minExposureTime;
			maxExposureTime = std::clamp(frameContext.maxFrameDuration, session.minExposureTime, session.maxExposureTime);
		} else {
			minExposureTime = lineDuration * frameContext.exposure;
			maxExposureTime = minExposureTime;
		}

		if (frameContext.autoGainEnabled) {
			minAnalogueGain = session.minAnalogueGain;
			maxAnalogueGain = session.maxAnalogueGain;
		} else {
			minAnalogueGain = frameContext.gain;
			maxAnalogueGain = frameContext.gain;
		}

		/*
		* The Agc algorithm needs to know the effective exposure value that was
		* applied to the sensor when the statistics were collected.
		*/
		utils::Duration effectiveExposureValue =
			lineDuration * params->exposure * params->gain;

		impl_.setLimits(minExposureTime, maxExposureTime,
				minAnalogueGain, maxAnalogueGain,
				std::move(params->additionalConstraints));

		impl_.setExposureCompensation(pow(2.0, frameContext.exposureValue));
		impl_.setLux(params->lux);

		double aGain, qGain, dGain;
		std::tie(newExposureTime, aGain, qGain, dGain) =
			impl_.calculateNewEv(frameContext.constraintMode, frameContext.exposureMode,
					     params->hist, effectiveExposureValue, params->traits);

		LOG(AgcMeanLuminance, Debug)
			<< "Divided up exposure time, analogue gain, quantization gain"
			<< " and digital gain are " << newExposureTime << ", " << aGain
			<< ", " << qGain << " and " << dGain;

		/* Update the estimated exposure and gain. */
		state.automatic.exposure = newExposureTime / lineDuration;
		state.automatic.gain = aGain;
		state.automatic.quantizationGain = qGain;
		state.automatic.yTarget = impl_.effectiveYTarget();
	}

	/*
	 * Expand the target frame duration so that we do not run faster than
	 * the minimum frame duration when we have short exposures.
	 */
	const auto frameDuration = std::max(frameContext.minFrameDuration, newExposureTime);
	frameContext.vblank = (frameDuration / lineDuration) - session.sensor.outputSize.height;

	/* Update frame duration accounting for line length quantization. */
	frameContext.frameDuration = (session.sensor.outputSize.height + frameContext.vblank) * lineDuration;

	metadata.set(controls::AnalogueGain, frameContext.gain);
	metadata.set(controls::ExposureTime, utils::Duration(lineDuration * frameContext.exposure).get<std::micro>());
	metadata.set(controls::FrameDuration, frameContext.frameDuration.get<std::micro>());
	metadata.set(controls::ExposureTimeMode,
		     frameContext.autoExposureEnabled
		     ? controls::ExposureTimeModeAuto
		     : controls::ExposureTimeModeManual);
	metadata.set(controls::AnalogueGainMode,
		     frameContext.autoGainEnabled
		     ? controls::AnalogueGainModeAuto
		     : controls::AnalogueGainModeManual);

	metadata.set(controls::AeExposureMode, frameContext.exposureMode);
	metadata.set(controls::AeConstraintMode, frameContext.constraintMode);
	metadata.set(controls::ExposureValue, frameContext.exposureValue);
}

} /* namespace ipa */

} /* namespace libcamera */
