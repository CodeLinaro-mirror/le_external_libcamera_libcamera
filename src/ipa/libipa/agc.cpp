/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, 2026 Ideas On Board
 *
 * Generic AGC algorithm
 */

#include "agc.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>

#include <linux/v4l2-controls.h>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

namespace libcamera {

namespace ipa {

using namespace std::chrono_literals;

LOG_DEFINE_CATEGORY(Agc)

/**
 * \class AgcAlgorithm
 * \brief AgcMeanLuminance wrapper for implementing the Algorithm interface
 *
 * \todo DigitalGain, DigitalGainMode
 */

/**
 * \struct agc::Session
 * \brief Session configuration for AgcAlgorithm
 *
 * \var agc::Session::minExposure
 * \brief Minimum exposure (in lines) supported with the configured sensor
 *
 * \var agc::Session::maxExposure
 * \brief Maximum exposure (in lines) supported with the configured sensor
 *
 * \var agc::Session::minExposureTime
 * \brief Minimum exposure time supported with the configured sensor
 *
 * \var agc::Session::maxExposureTime
 * \brief Maximum exposure time supported with the configured sensor
 *
 * \var agc::Session::minAnalogueGain
 * \brief Minimum analogue gain supported with the configured sensor
 *
 * \var agc::Session::maxAnalogueGain
 * \brief Maximum analogue gain supported with the configured sensor
 *
 * \var agc::Session::minFrameDuration
 * \brief Minimum frame duration supported with the configured sensor
 *
 * \var agc::Session::maxFrameDuration
 * \brief Maximum frame duration supported with the configured sensor
 *
 * \var agc::Session::lineDuration
 * \brief Line duration with the configured sensor and output size
 *
 * \var agc::Session::sensor
 * \brief Details of the sensor configuration
 *
 * \var agc::Session::sensor.outputSize
 * \brief Configured output size of the sensor
 *
 * \var agc::Session::autoAllowed
 * \brief Whether automatic controls are allowed
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
 * \var agc::ActiveState::automatic.yTarget
 * \brief Automatically determined luminance target
 *
 * \var agc::ActiveState::autoExposureEnabled
 * \brief Manual/automatic AGC state (exposure) as set by the ExposureTimeMode control
 *
 * \var agc::ActiveState::autoGainEnabled
 * \brief Manual/automatic AGC state (gain) as set by the AnalogueGainMode control
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
 * The gain should be adapted to the sensor specific gain code before applying.
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
 * supplied in the current frame.
 *
 * \var agc::FrameContext::autoGainModeChange
 * \brief Indicate if autoGainEnabled has changed from true in the previous
 * frame to false in the current frame, and no manual gain value has been
 * supplied in the current frame.
 */

/**
 * \struct AgcAlgorithm::ConfigurationParams
 * \brief Parameters for AgcAlgorithm::configure()
 *
 * \var AgcAlgorithm::ConfigurationParams::sensor
 * \brief CameraSensorHelper for the sensor
 *
 * \var AgcAlgorithm::ConfigurationParams::sensorInfo
 * \brief Details of the sensor
 *
 * \var AgcAlgorithm::ConfigurationParams::sensorControls
 * \brief ControlInfoMap of the sensor
 *
 * \var AgcAlgorithm::ConfigurationParams::ctrlMap
 * \brief ControlMap to update with controls
 *
 * \var AgcAlgorithm::ConfigurationParams::autoAllowed
 * \brief Whether to enable auto controls
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

namespace {

[[nodiscard]] uint32_t clampExposure(utils::Duration exposureTime, const agc::Session &session)
{
	return std::clamp<uint32_t>(
		exposureTime / session.lineDuration,
		session.minExposure,
		session.maxExposure
	);
}

} /* namespace */

/**
 * \brief Load tuning data
 */
int AgcAlgorithm::init(const ValueNode &tuningData)
{
	int ret = impl_.parseTuningData(tuningData);
	if (ret)
		return ret;

	return 0;
}

/**
 * \brief Initialize the session configuration and active state
 */
int AgcAlgorithm::configure(agc::Session &session, agc::ActiveState &state, const ConfigurationParams &config)
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
	float minGain = config.sensor->gain(v4l2Gain.min().get<int32_t>());
	float maxGain = config.sensor->gain(v4l2Gain.max().get<int32_t>());
	float defGain = config.sensor->gain(v4l2Gain.def().get<int32_t>());
	config.ctrlMap[&controls::AnalogueGain] = ControlInfo{
		minGain,
		maxGain,
		defGain,
	};

	LOG(Agc, Debug)
		<< "exposure:[" << minExposure << ',' << maxExposure << ']'
		<< " gain: [" << minGain << ',' << maxGain << ']'
		<< " line-duration:" << session.lineDuration
		<< " sensor-output:" << session.sensor.outputSize
	;

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
		Span<const int64_t, 2>{ { frameDurations[0], frameDurations[1] } },
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
	session.minExposure = minExposure;
	session.maxExposure = maxExposure;
	session.minExposureTime = minExposure * session.lineDuration;
	session.maxExposureTime = maxExposure * session.lineDuration;
	session.minAnalogueGain = minGain;
	session.maxAnalogueGain = maxGain;

	impl_.configure(session.lineDuration, config.sensor);
	impl_.setLimits(session.minExposureTime, session.maxExposureTime,
			session.minAnalogueGain, session.maxAnalogueGain,
			{});
	impl_.resetFrameCount();

	/* Configure the default exposure and gain. */
	state = {};
	state.automatic.gain = session.minAnalogueGain;
	state.automatic.exposure = clampExposure(defExposure * session.lineDuration, session);
	state.automatic.quantizationGain = 1;
	state.automatic.yTarget = impl_.effectiveYTarget(0, 1);
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

	if (session.autoAllowed) {
		config.ctrlMap[&controls::ExposureValue] = ControlInfo(-8.0f, 8.0f, 0.0f);

		for (const auto &[id, info] : impl_.controls())
			config.ctrlMap[id] = info;
	} else {
		config.ctrlMap.erase(&controls::ExposureValue);

		for (const auto &[id, info] : impl_.controls())
			config.ctrlMap.erase(id);
	}


	return 0;
}

/**
 * \brief Handle a \a queueRequest operation
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
		state.manual.exposure = clampExposure(*exposure * 1us, session);

		LOG(Agc, Debug)
			<< "Set exposure to " << state.manual.exposure;
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
 * \brief Handle a \a process operation
 */
void AgcAlgorithm::process(const agc::Session &session, agc::ActiveState &state,
			   agc::FrameContext &frameContext, std::optional<ProcessParams> &&params,
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

		const auto &newEv = impl_.calculateNewEv({
			.traits = params->traits,
			.yHist = params->yHist,
			.effectiveExposureValue = effectiveExposureValue,
			.constraintModeIndex = frameContext.constraintMode,
			.exposureModeIndex = frameContext.exposureMode,
			.lux = params->lux,
			.exposureCompensation = pow(2.0, frameContext.exposureValue),
		});

		LOG(Agc, Debug)
			<< "exposure-time:" << newEv.exposureTime
			<< " analogue-gain" << newEv.analogueGain
			<< " quantization-gain" << newEv.quantizationGain
			<< " digital-gain: " << newEv.digitalGain
		;

		/* Update the estimated exposure and gain. */
		state.automatic.exposure = clampExposure(newEv.exposureTime, session);
		state.automatic.gain = newEv.analogueGain;
		state.automatic.quantizationGain = newEv.quantizationGain;
		state.automatic.yTarget = newEv.yTarget;

		newExposureTime = newEv.exposureTime;
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
