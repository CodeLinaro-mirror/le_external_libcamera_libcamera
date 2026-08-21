/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2026 Ideas On Board
 *
 * Auto exposure/gain algorithm for implementing the IPA-specific AGC algorithms
 */

#include "agc.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <ratio>
#include <variant>

#include <linux/v4l2-controls.h>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include <libcamera/ipa/core_ipa_interface.h>

namespace libcamera {

namespace ipa {

using namespace std::chrono_literals;

LOG_DEFINE_CATEGORY(Agc)

namespace agc {

/**
 * \fn extractControls()
 * \param[in] controls The controls list to extract from
 * \param[in] sensor The CameraSensorHelper
 *
 * This function extracts \a V4L2_CID_EXPOSURE and \a V4L2_CID_ANALOGUE_GAIN
 * from \a controls and then returns the exposure and gain values. The gain
 * code is mapped to the real gain value if \a sensor is provided, otherwise
 * the gain code is returned.
 *
 * \return A pair of exposure and analogue gain extracted from \a controls
 */

/**
 * \fn prepareControls()
 * \param[out] controls The controls list to extract from
 * \param[in] sensor The CameraSensorHelper
 * \param[in] exposure The exposure (in lines)
 * \param[in] gain The analogue gain
 *
 * This function sets \a V4L2_CID_EXPOSURE and \a V4L2_CID_ANALOGUE_GAIN
 * in \a controls. The gain is mapped to the gain code if \a sensor is provided,
 * otherwise the gain value will be used directly.
 */

} /* namespace agc */

/**
 * \class AgcAlgorithm
 * \brief Auto exposure/gain algorithm for implementing the IPA-specific AGC algorithms
 *
 * This class can be used to implement an automatic exposure/gain algorithm
 * that fits the IPA Algorithm interface with relative ease. Internally
 * it uses either AgcMeanLuminance or AgcMSV depending on whether the
 * sensor is know.
 *
 * \todo DigitalGain, DigitalGainMode
 */

/**
 * \struct agc::Session
 * \brief Session configuration for AgcAlgorithm
 *
 * \var agc::Session::minExposureTime
 * \brief Minimum exposure time for the streaming session
 *
 * \var agc::Session::maxExposureTime
 * \brief Maximum exposure time for the streaming session
 *
 * \var agc::Session::minAnalogueGain
 * \brief Minimum analogue gain for the streaming session
 *
 * \var agc::Session::maxAnalogueGain
 * \brief Maximum analogue gain for the streaming session
 *
 * \var agc::Session::defAnalogueGain
 * \brief Default analogue gain of the configured sensor
 *
 * \var agc::Session::minFrameDuration
 * \brief Minimum frame duration for the streaming session
 *
 * \var agc::Session::maxFrameDuration
 * \brief Maximum frame duration for the streaming session
 *
 * \var agc::Session::lineDuration
 * \brief Line duration for the streaming session
 *
 * \var agc::Session::sensor
 * \brief Details of the sensor configuration
 *
 * \var agc::Session::sensor.outputSize
 * \brief Configured output size of the sensor
 *
 * \var agc::Session::autoAllowed
 * \copybrief AgcAlgorithm::ConfigurationParams::autoAllowed
 * \sa AgcAlgorithm::ConfigurationParams::autoAllowed
 */

/**
 * \struct agc::ActiveState
 * \brief Active state for AgcAlgorithm
 *
 * The \a automatic variables track the latest values computed by algorithm
 * based on the latest processed statistics. All other variables track the
 * consolidated controls requested in queued requests.
 *
 * \var agc::ActiveState::manual
 * \brief Manual exposure time and analog gain (set through requests)
 *
 * \var agc::ActiveState::manual.exposure
 * \brief Manual exposure time expressed as a number of lines as set by the
 * ExposureTime control
 *
 * \var agc::ActiveState::manual.gain
 * \brief Manual analogue gain as set by the AnalogueGain control
 *
 * \var agc::ActiveState::automatic
 * \brief Automatic exposure time and analog gain (computed by the algorithm)
 *
 * \var agc::ActiveState::automatic.exposure
 * \brief Automatic exposure time expressed as a number of lines
 *
 * \var agc::ActiveState::automatic.gain
 * \brief Automatic analogue gain multiplier
 *
 * \var agc::ActiveState::automatic.quantizationGain
 * \brief Automatic quantization gain multiplier
 *
 * \var agc::ActiveState::automatic.digitalGain
 * \brief Automatic digital gain multiplier
 *
 * \var agc::ActiveState::automatic.yTarget
 * \brief Automatically determined luminance target
 *
 * \var agc::ActiveState::autoExposureEnabled
 * \brief Whether automatic exposure control is enabled by the ExposureTimeMode control
 *
 * \var agc::ActiveState::autoGainEnabled
 * \brief Whether automatic gain control is enabled by the AnalogueGainMode control
 *
 * \var agc::ActiveState::exposureValue
 * \brief Exposure value as set by the ExposureValue control
 *
 * \var agc::ActiveState::constraintMode
 * \brief Constraint mode as set by the AeConstraintMode control
 *
 * \var agc::ActiveState::exposureMode
 * \brief Exposure mode as set by the AeExposureMode control
 *
 * \var agc::ActiveState::minFrameDuration
 * \brief Minimum frame duration as set by the FrameDurationLimits control
 *
 * \var agc::ActiveState::maxFrameDuration
 * \brief Maximum frame duration as set by the FrameDurationLimits control
 */

/**
 * \struct agc::FrameContext
 * \brief Per-frame context for AgcAlgorithm
 *
 * \var agc::FrameContext::exposure
 * \brief Exposure time expressed as a number of lines computed by the algorithm
 *
 * \var agc::FrameContext::gain
 * \brief Analogue gain multiplier computed by the algorithm
 *
 * The gain should be translated to the sensor specific gain code before applying.
 *
 * \var agc::FrameContext::quantizationGain
 * \brief Quantization gain multiplier computed by the algorithm
 *
 * \var agc::FrameContext::exposureValue
 * \brief Exposure value as set by the ExposureValue control
 *
 * \var agc::FrameContext::yTarget
 * \brief Luminance target computed by the algorithm
 *
 * \var agc::FrameContext::vblank
 * \brief Vertical blanking parameter computed by the algorithm
 *
 * \var agc::FrameContext::autoExposureEnabled
 * \brief Manual/automatic AGC state (exposure) as set by the ExposureTimeMode control
 *
 * \var agc::FrameContext::autoGainEnabled
 * \brief Manual/automatic AGC state (gain) as set by the AnalogueGainMode control
 *
 * \var agc::FrameContext::constraintMode
 * \brief Constraint mode as set by the AeConstraintMode control
 *
 * \var agc::FrameContext::exposureMode
 * \brief Exposure mode as set by the AeExposureMode control
 *
 * \var agc::FrameContext::minFrameDuration
 * \brief Minimum frame duration as set by the FrameDurationLimits control
 *
 * \var agc::FrameContext::maxFrameDuration
 * \brief Maximum frame duration as set by the FrameDurationLimits control
 *
 * \var agc::FrameContext::frameDuration
 * \brief The actual FrameDuration used by the algorithm for the frame
 *
 * \var agc::FrameContext::autoExposureModeChange
 * \brief Indicate if autoExposureEnabled has changed from true in the previous
 * frame to false in the current frame, and no manual exposure value has been
 * supplied in the current frame
 *
 * \var agc::FrameContext::autoGainModeChange
 * \brief Indicate if autoGainEnabled has changed from true in the previous
 * frame to false in the current frame, and no manual gain value has been
 * supplied in the current frame
 */

/**
 * \struct AgcAlgorithm::ConfigurationParams
 * \brief Parameters for AgcAlgorithm::configure()
 *
 * \var AgcAlgorithm::ConfigurationParams::sensorInfo
 * \brief Current configuration of the sensor
 *
 * \var AgcAlgorithm::ConfigurationParams::sensorControls
 * \brief ControlInfoMap of the sensor
 *
 * \var AgcAlgorithm::ConfigurationParams::ctrlMap
 * \brief ControlInfoMap::Map to update with controls
 *
 * \var AgcAlgorithm::ConfigurationParams::autoAllowed
 * \brief Whether to enable auto controls
 *
 * If \a false, the algorithm is set up for manual exposure and gain
 * control only, without automatic adjustments. In this mode statistics
 * must not be provided to AgcAlgorithm::process(), and ExposureTimeMode
 * and AnalogueGainMode will only advertise manual control.
 */

/**
 * \struct AgcAlgorithm::ProcessParams
 * \brief Parameters for AgcAlgorithm::process()
 *
 * \var AgcAlgorithm::ProcessParams::traits
 * \brief Implementation of AgcMeanLuminance::Traits
 *
 * \var AgcAlgorithm::ProcessParams::yHist
 * \brief Luminance histogram of the frame
 *
 * \var AgcAlgorithm::ProcessParams::exposure
 * \brief Effective exposure of the frame
 *
 * \var AgcAlgorithm::ProcessParams::gain
 * \brief Effective gain of the frame
 *
 * \var AgcAlgorithm::ProcessParams::additionalConstraints
 * \brief Additional AgcMeanLuminance::AgcConstraints to apply
 *
 * \var AgcAlgorithm::ProcessParams::lux
 * \brief Effective lux value of the frame
 */

/**
 * \brief Load tuning data and configure
 * \param[in] tuningData The tuning data
 * \param[in] sensor The camera sensor helper
 * \param[in] session The agc session configuration
 * \param[in] state The agc active state
 * \param[in] config The algorithm configuration
 *
 * This function loads the tuning data and configures the algorithm as if
 * by a call to configure().
 */
int AgcAlgorithm::init(const ValueNode &tuningData, CameraSensorHelper *sensor,
		       agc::Session &session, agc::ActiveState &state,
		       const ConfigurationParams &config)
{
	if (sensor) {
		auto &impl = impl_.emplace<AgcMeanLuminance>();
		int ret = impl.parseTuningData(tuningData);
		if (ret)
			return ret;
	} else {
		impl_.emplace<AgcMSV>();
	}

	sensor_ = sensor;

	return configure(session, state, config);
}

/**
 * \brief Initialize the session configuration and active state
 * \param[in] session The agc session configuration
 * \param[in] state The agc active state
 * \param[in] config The algorithm configuration
 *
 * This function initializes \a session and \a state based on the tuning
 * data loaded by init() and the configuration in \a config.
 */
int AgcAlgorithm::configure(agc::Session &session, agc::ActiveState &state,
			    const ConfigurationParams &config)
{
	const uint32_t lineLength = config.sensorInfo.minLineLength;

	session = {};
	session.autoAllowed = config.autoAllowed;
	session.lineDuration = lineLength * 1.0s / config.sensorInfo.pixelRate;
	session.sensor.outputSize = config.sensorInfo.outputSize;

	const double lineDurationUs = session.lineDuration.get<std::micro>();

	/*
	 * Compute exposure time limits from the V4L2_CID_EXPOSURE control
	 * limits and the line duration.
	 */

	const ControlInfo &v4l2Exposure = config.sensorControls.find(V4L2_CID_EXPOSURE)->second;
	int32_t minExposure = v4l2Exposure.min().get<int32_t>();
	int32_t maxExposure = v4l2Exposure.max().get<int32_t>();
	int32_t defExposure = v4l2Exposure.def().get<int32_t>();

	/* Compute the analogue gain limits. */
	const auto extractGain = [&](const ControlValue &v) {
		auto gainCode = v.get<int32_t>();
		return sensor_ ? sensor_->gain(gainCode) : gainCode;
	};
	const ControlInfo &v4l2Gain = config.sensorControls.find(V4L2_CID_ANALOGUE_GAIN)->second;
	float minGain = extractGain(v4l2Gain.min());
	float maxGain = extractGain(v4l2Gain.max());
	float defGain = extractGain(v4l2Gain.def());

	LOG(Agc, Debug)
		<< "exposure: [" << minExposure << ',' << maxExposure << "], "
		<< "gain: [" << minGain << ',' << maxGain << "], "
		<< "line-duration: " << session.lineDuration << ", "
		<< "sensor-output: " << session.sensor.outputSize;

	/*
	 * Compute the frame duration limits.
	 *
	 * The frame length is computed assuming a fixed line length combined
	 * with the vertical frame sizes.
	 */

	const ControlInfo &v4l2VBlank = config.sensorControls.find(V4L2_CID_VBLANK)->second;
	std::array<uint32_t, 3> frameHeights{
		v4l2VBlank.min().get<int32_t>() + config.sensorInfo.outputSize.height,
		v4l2VBlank.max().get<int32_t>() + config.sensorInfo.outputSize.height,
		v4l2VBlank.def().get<int32_t>() + config.sensorInfo.outputSize.height,
	};

	std::array<int64_t, 3> frameDurations;
	for (unsigned int i = 0; i < frameHeights.size(); ++i) {
		uint64_t frameSize = static_cast<uint64_t>(lineLength) * frameHeights[i];
		frameDurations[i] = frameSize * 1000000U / config.sensorInfo.pixelRate;
	}

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
	session.defAnalogueGain = defGain;
	session.minFrameDuration = std::chrono::microseconds(frameDurations[0]);
	session.maxFrameDuration = std::chrono::microseconds(frameDurations[1]);

	/* Configure the default exposure and gain. */
	state = {};
	state.automatic.gain = session.minAnalogueGain;
	state.automatic.exposure = defExposure;
	state.automatic.quantizationGain = 1;
	state.automatic.digitalGain = 1;
	state.manual.gain = state.automatic.gain;
	state.manual.exposure = state.automatic.exposure;
	state.autoExposureEnabled = session.autoAllowed;
	state.autoGainEnabled = session.autoAllowed;
	state.exposureValue = 0;
	state.minFrameDuration = session.minFrameDuration;
	state.maxFrameDuration = session.maxFrameDuration;

	/* The IPA control maps keep their states, so the removal is necessary. */
	config.ctrlMap.erase(&controls::ExposureValue);
	config.ctrlMap.erase(&controls::AeConstraintMode);
	config.ctrlMap.erase(&controls::AeExposureMode);

	/* \todo Move this to the `Camera` class. */
	config.ctrlMap[&controls::AeEnable] = ControlInfo{
		false, session.autoAllowed, session.autoAllowed
	};
	config.ctrlMap[&controls::AnalogueGain] = ControlInfo{
		minGain, maxGain, defGain
	};
	config.ctrlMap[&controls::ExposureTime] = ControlInfo{
		static_cast<int32_t>(minExposure * lineDurationUs),
		static_cast<int32_t>(maxExposure * lineDurationUs),
		static_cast<int32_t>(defExposure * lineDurationUs),
	};
	config.ctrlMap[&controls::FrameDurationLimits] = ControlInfo{
		frameDurations[0], frameDurations[1],
		Span<const int64_t, 2>{ { frameDurations[0], frameDurations[1] } },
	};

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

	add(controls::ExposureTimeMode,
	    controls::ExposureTimeModeAuto, controls::ExposureTimeModeManual);
	add(controls::AnalogueGainMode,
	    controls::AnalogueGainModeAuto, controls::AnalogueGainModeManual);

	std::visit(utils::overloaded{
		[&](AgcMSV &) {
			/* No constraint/exposure mode support. */
			state.constraintMode = controls::AeConstraintModeEnum::ConstraintNormal;
			state.exposureMode = controls::AeExposureModeEnum::ExposureNormal;

			state.automatic.yTarget = 0; /* Not supported. */

			if (!session.autoAllowed)
				return;

			config.ctrlMap[&controls::AeConstraintMode] = ControlInfo(
				std::array{ ControlValue(state.constraintMode) }
			);

			config.ctrlMap[&controls::AeExposureMode] = ControlInfo(
				std::array{ ControlValue(state.exposureMode) }
			);
		},
		[&](AgcMeanLuminance &impl) {
			state.constraintMode =
				static_cast<controls::AeConstraintModeEnum>(impl.constraintModes().begin()->first);
			state.exposureMode =
				static_cast<controls::AeExposureModeEnum>(impl.exposureModeHelpers().begin()->first);

			state.automatic.yTarget = impl.effectiveYTarget(0, 1);

			impl.configure(session.lineDuration, sensor_);
			impl.resetFrameCount();

			if (!session.autoAllowed)
				return;

			config.ctrlMap[&controls::ExposureValue] = ControlInfo(-8.0f, 8.0f, 0.0f);

			std::vector<ControlValue> options;
			for (const auto &[id, _] : impl.constraintModes())
				options.emplace_back(id);
			config.ctrlMap[&controls::AeConstraintMode] = ControlInfo(options);

			options.clear();
			for (const auto &[id, _] : impl.exposureModeHelpers())
				options.emplace_back(id);
			config.ctrlMap[&controls::AeExposureMode] = ControlInfo(options);
		},
	}, impl_);

	return 0;
}

/**
 * \brief Queue a request
 * \param[in] session The agc session configuration
 * \param[in] state The agc active state
 * \param[in] frameContext The agc frame context
 * \param[in] controls The list of controls associated with a Request
 *
 * This functions processes the agc-related controls in \a controls for the frame
 * denoted by \a frameContext, and updates \a state and \a frameContext accordingly.
 */
void AgcAlgorithm::queueRequest(const agc::Session &session, agc::ActiveState &state,
				agc::FrameContext &frameContext, const ControlList &controls)
{
	if (session.autoAllowed) {
		const auto &aeEnable = controls.get(controls::ExposureTimeMode);
		if (aeEnable &&
		    (*aeEnable == controls::ExposureTimeModeAuto) != state.autoExposureEnabled) {
			state.autoExposureEnabled = (*aeEnable == controls::ExposureTimeModeAuto);

			LOG(Agc, Debug)
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

			LOG(Agc, Debug)
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

		LOG(Agc, Debug) << "Set exposure to " << state.manual.exposure;
	}

	const auto &gain = controls.get(controls::AnalogueGain);
	if (gain && !state.autoGainEnabled) {
		state.manual.gain = *gain;

		LOG(Agc, Debug) << "Set gain to " << state.manual.gain;
	}

	frameContext.autoExposureEnabled = state.autoExposureEnabled;
	frameContext.autoGainEnabled = state.autoGainEnabled;

	if (!frameContext.autoExposureEnabled)
		frameContext.exposure = state.manual.exposure;
	if (!frameContext.autoGainEnabled)
		frameContext.gain = state.manual.gain;

	if (!frameContext.autoExposureEnabled && !frameContext.autoGainEnabled)
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
			session.minFrameDuration, session.maxFrameDuration);

		state.maxFrameDuration = std::clamp<utils::Duration>(
			std::chrono::microseconds((*frameDurationLimits).back()),
			session.minFrameDuration, session.maxFrameDuration);
	}
	frameContext.minFrameDuration = state.minFrameDuration;
	frameContext.maxFrameDuration = state.maxFrameDuration;
}

/**
 * \brief Prepare a frame
 * \param[in] state The agc active state
 * \param[in] frameContext The agc frame context
 *
 * This function prepares the parameters for the frame denoted by \a frameContext.
 * After a call to this function, the values of \ref agc::FrameContext::exposure
 * "frameContext.exposure" and \ref agc::FrameContext::gain "frameContext.gain"
 * will be finalized and may be used by the caller.
 *
 * \todo Finalize \ref agc::FrameContext::vblank "frameContext.vblank" as well
 */
void AgcAlgorithm::prepare(agc::ActiveState &state, agc::FrameContext &frameContext)
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
 * \brief Process frame statistics
 * \param[in] session The agc session configuration
 * \param[in] state The agc active state
 * \param[in] frameContext The agc frame context
 * \param[in] params The algorithm parameters
 * \param[in] metadata The list of metadata
 *
 * Process the statistics for the completed frame denoted by \a frameContext
 * and update \a state appropriately. This function also populates \a metadata.
 */
void AgcAlgorithm::process(const agc::Session &session, agc::ActiveState &state,
			   agc::FrameContext &frameContext, std::optional<ProcessParams> &&params,
			   ControlList &metadata)
{
	if (!params) {
		processFrameDuration(session, frameContext, frameContext.minFrameDuration);
		fillMetadata(session, frameContext, metadata);
		return;
	}

	ASSERT(session.autoAllowed);

	const utils::Duration &lineDuration = session.lineDuration;

	/*
	 * Set the AGC limits using the fixed exposure time and/or gain in
	 * manual mode, or the sensor limits in auto mode.
	 */
	utils::Duration minExposureTime;
	utils::Duration maxExposureTime;
	double minAnalogueGain;
	double maxAnalogueGain;

	/* \todo This uses the configuration from an already completed frame. */

	if (frameContext.autoExposureEnabled) {
		minExposureTime = session.minExposureTime;
		maxExposureTime = std::clamp(frameContext.maxFrameDuration,
					     session.minExposureTime,
					     session.maxExposureTime);
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

	std::visit(utils::overloaded{
		[&](AgcMSV& impl) {
			impl.setLimits({
				.exposure = {
					static_cast<uint32_t>(minExposureTime / lineDuration),
					static_cast<uint32_t>(maxExposureTime / lineDuration),
				},
				.gain = {
					minAnalogueGain,
					maxAnalogueGain,
				},
				/* gain codes -> step size of 1 */
				.gainMinStep = 1,
				/* assume default gain is close to 1.0 */
				.gain1 = session.defAnalogueGain,
			});

			const auto& newEv = impl.calculateNewEv({
				.yHist = params->yHist,
				.exposure = params->exposure,
				.gain = params->gain,
			});

			state.automatic.exposure = newEv.exposure;
			state.automatic.gain = newEv.analogueGain;
		},
		[&](AgcMeanLuminance& impl) {
			/*
			 * The Agc algorithm needs to know the effective exposure
			 * value that was applied to the sensor when the statistics
			 * were collected.
			 */
			utils::Duration effectiveExposureValue =
				lineDuration * params->exposure * params->gain;

			impl.setLimits(minExposureTime, maxExposureTime,
				       minAnalogueGain, maxAnalogueGain,
				       std::move(params->additionalConstraints));

			const auto &newEv = impl.calculateNewEv({
				.traits = params->traits,
				.yHist = params->yHist,
				.effectiveExposureValue = effectiveExposureValue,
				.constraintModeIndex = frameContext.constraintMode,
				.exposureModeIndex = frameContext.exposureMode,
				.lux = params->lux,
				.exposureCompensation = std::pow(2.0, frameContext.exposureValue),
			});

			/* Update the estimated exposure and gain. */
			state.automatic.exposure = newEv.exposureTime / lineDuration;
			state.automatic.gain = newEv.analogueGain;
			state.automatic.quantizationGain = newEv.quantizationGain;
			state.automatic.digitalGain = newEv.digitalGain;
			state.automatic.yTarget = newEv.yTarget;
		},
	}, impl_);

	const utils::Duration newExposureTime = state.automatic.exposure * lineDuration;

	LOG(Agc, Debug)
		<< "exposure-time: " << newExposureTime << ", "
		<< "analogue-gain: " << state.automatic.gain << ", "
		<< "quantization-gain: " << state.automatic.quantizationGain << ", "
		<< "digital-gain: " << state.automatic.digitalGain;

	/*
	 * Expand the target frame duration so that we do not run faster than
	 * the minimum frame duration when we have short exposures.
	 */
	processFrameDuration(session, frameContext,
			     std::max(frameContext.minFrameDuration, newExposureTime));

	fillMetadata(session, frameContext, metadata);
}

/**
 * \brief Process frame duration and compute vblank
 * \param[in] session The session parameters
 * \param[in] frameContext The current frame context
 * \param[in] frameDuration The target frame duration
 *
 * Compute and populate vblank from the target frame duration.
 */
void AgcAlgorithm::processFrameDuration(const agc::Session &session,
					agc::FrameContext &frameContext,
					utils::Duration frameDuration)
{
	const utils::Duration &lineDuration = session.lineDuration;

	frameContext.vblank =
		(frameDuration / lineDuration) - session.sensor.outputSize.height;

	/* Update frame duration accounting for line length quantization. */
	frameContext.frameDuration =
		(session.sensor.outputSize.height + frameContext.vblank) * lineDuration;
}

void AgcAlgorithm::fillMetadata(const agc::Session &session,
				const agc::FrameContext &frameContext,
				ControlList &metadata)
{

	metadata.set(controls::AnalogueGain, frameContext.gain);
	metadata.set(controls::ExposureTime,
		     utils::Duration(session.lineDuration * frameContext.exposure).get<std::micro>());
	metadata.set(controls::FrameDuration, frameContext.frameDuration.get<std::micro>());
	metadata.set(controls::ExposureTimeMode, frameContext.autoExposureEnabled
						 ? controls::ExposureTimeModeAuto
						 : controls::ExposureTimeModeManual);
	metadata.set(controls::AnalogueGainMode, frameContext.autoGainEnabled
						 ? controls::AnalogueGainModeAuto
						 : controls::AnalogueGainModeManual);

	metadata.set(controls::AeExposureMode, frameContext.exposureMode);
	metadata.set(controls::AeConstraintMode, frameContext.constraintMode);
	metadata.set(controls::ExposureValue, frameContext.exposureValue);
}

} /* namespace ipa */

} /* namespace libcamera */
