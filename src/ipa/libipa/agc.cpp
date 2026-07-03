/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 */

#include "agc.h"

#include <chrono>

#include <linux/v4l2-controls.h>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

namespace libcamera {

namespace ipa {

using namespace std::chrono_literals;

LOG_DEFINE_CATEGORY(Agc)

/**
 * \class AgcAlgorithm
 * \brief Commong handling of AGC related controls and calculation
 *
 * \todo DigitalGain, DigitalGainMode
 */

/**
 * \struct AgcAlgorithm::Session
 * \brief Session configuration for AgcAlgorithm
 *
 * \var AgcAlgorithm::Session::minExposureTime
 * \brief Minimum exposure time supported with the configured sensor
 *
 * \var AgcAlgorithm::Session::maxExposureTime
 * \brief Maximum exposure time supported with the configured sensor
 *
 * \var AgcAlgorithm::Session::minAnalogueGain
 * \brief Minimum analogue gain supported with the configured sensor
 *
 * \var AgcAlgorithm::Session::maxAnalogueGain
 * \brief Maximum analogue gain supported with the configured sensor
 *
 * \var AgcAlgorithm::Session::minFrameDuration
 * \brief Minimum frame duration supported with the configured sensor
 *
 * \var AgcAlgorithm::Session::maxFrameDuration
 * \brief Maximum frame duration supported with the configured sensor
 *
 * \var AgcAlgorithm::Session::lineDuration
 * \brief Line duration with the configured sensor and output size
 *
 * \var AgcAlgorithm::Session::sensor
 * \brief Details of the sensor configuration
 *
 * \var AgcAlgorithm::Session::sensor.outputSize
 * \brief Configured output size of the sensor
 *
 * \var AgcAlgorithm::Session::autoAllowed
 * \brief Whether automatic controls are allowed
 */

/**
 * \struct AgcAlgorithm::ActiveState
 * \brief Active state for AgcAlgorithm
 *
 * The \a automatic variables track the latest values computed by algorithm
 * based on the latest processed statistics. All other variables track the
 * consolidated controls requested in queued requests.
 *
 * \var AgcAlgorithm::ActiveState::manual
 * \brief Manual exposure time and analog gain (set through requests)
 *
 * \var AgcAlgorithm::ActiveState::manual.exposure
 * \brief Manual exposure time expressed as a number of lines as set by the
 * ExposureTime control
 *
 * \var AgcAlgorithm::ActiveState::manual.gain
 * \brief Manual analogue gain as set by the AnalogueGain control
 *
 * \var AgcAlgorithm::ActiveState::automatic
 * \brief Automatic exposure time and analog gain (computed by the algorithm)
 *
 * \var AgcAlgorithm::ActiveState::automatic.exposure
 * \brief Automatic exposure time expressed as a number of lines
 *
 * \var AgcAlgorithm::ActiveState::automatic.gain
 * \brief Automatic analogue gain multiplier
 *
 * \var AgcAlgorithm::ActiveState::autoExposureEnabled
 * \brief Manual/automatic AGC state (exposure) as set by the ExposureTimeMode control
 *
 * \var AgcAlgorithm::ActiveState::autoGainEnabled
 * \brief Manual/automatic AGC state (gain) as set by the AnalogueGainMode control
 *
 * \var AgcAlgorithm::ActiveState::minFrameDuration
 * \brief Minimum frame duration as set by the FrameDurationLimits control
 *
 * \var AgcAlgorithm::ActiveState::maxFrameDuration
 * \brief Maximum frame duration as set by the FrameDurationLimits control
 */

/**
 * \struct AgcAlgorithm::FrameContext
 * \brief Per-frame context for AgcAlgorithm
 *
 * \var AgcAlgorithm::FrameContext::exposure
 * \brief Exposure time expressed as a number of lines computed by the algorithm
 *
 * \var AgcAlgorithm::FrameContext::gain
 * \brief Analogue gain multiplier computed by the algorithm
 *
 * The gain should be adapted to the sensor specific gain code before applying.
 *
 * \var AgcAlgorithm::FrameContext::vblank
 * \brief Vertical blanking parameter computed by the algorithm
 *
 * \var AgcAlgorithm::FrameContext::autoExposureEnabled
 * \brief Manual/automatic AGC state (exposure) as set by the ExposureTimeMode control
 *
 * \var AgcAlgorithm::FrameContext::autoGainEnabled
 * \brief Manual/automatic AGC state (gain) as set by the AnalogueGainMode control
 *
 * \var AgcAlgorithm::FrameContext::minFrameDuration
 * \brief Minimum frame duration as set by the FrameDurationLimits control
 *
 * \var AgcAlgorithm::FrameContext::maxFrameDuration
 * \brief Maximum frame duration as set by the FrameDurationLimits control
 *
 * \var AgcAlgorithm::FrameContext::frameDuration
 * \brief The actual FrameDuration used by the algorithm for the frame
 *
 * \var AgcAlgorithm::FrameContext::autoExposureModeChange
 * \brief Indicate if autoExposureEnabled has changed from true in the previous
 * frame to false in the current frame, and no manual exposure value has been
 * supplied in the current frame.
 *
 * \var AgcAlgorithm::FrameContext::autoGainModeChange
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
 * \struct AgcAlgorithm::Limits
 * \brief Limits of AGC parameters
 *
 * This structure contains the limits to consider for the actual
 * AGC implementation.
 *
 * \var AgcAlgorithm::Limits::exposure
 * \brief Limits of exposure time
 *
 * \var AgcAlgorithm::Limits::gain
 * \brief Limits of analogue gain
 */

/**
 * \brief Initialize the session configuration and active state
 */
int AgcAlgorithm::configure(Session &session, ActiveState &state, const ConfigurationParams &config)
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
	const auto mapGain = [&](const ControlValue &v) {
		auto code = v.get<int32_t>();
		return config.sensor ? config.sensor->gain(code) : code;
	};

	const ControlInfo &v4l2Gain = config.sensorControls.find(V4L2_CID_ANALOGUE_GAIN)->second;
	float minGain = mapGain(v4l2Gain.min());
	float maxGain = mapGain(v4l2Gain.max());
	float defGain = mapGain(v4l2Gain.def());
	config.ctrlMap[&controls::AnalogueGain] = ControlInfo{
		minGain,
		maxGain,
		defGain,
	};

	LOG(Agc, Debug)
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

	/* Configure the default exposure and gain. */
	state = {};
	state.automatic.gain = session.minAnalogueGain;
	state.automatic.exposure = 10ms / session.lineDuration;
	state.manual.gain = state.automatic.gain;
	state.manual.exposure = state.automatic.exposure;
	state.autoExposureEnabled = session.autoAllowed;
	state.autoGainEnabled = session.autoAllowed;
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

	return 0;
}

/**
 * \brief Handle a \a queueRequest operation
 */
void AgcAlgorithm::queueRequest(const Session &session, ActiveState &state,
				FrameContext &frameContext, const ControlList &controls)
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
void AgcAlgorithm::prepare(ActiveState &state, FrameContext &frameContext)
{
	uint32_t activeAutoExposure = state.automatic.exposure;
	double activeAutoGain = state.automatic.gain;

	/* Populate exposure and gain in auto mode */
	if (frameContext.autoExposureEnabled)
		frameContext.exposure = activeAutoExposure;
	if (frameContext.autoGainEnabled)
		frameContext.gain = activeAutoGain;

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
	}
}

/**
 * \brief Calculate the AGC limits for the given frame
 */
AgcAlgorithm::Limits AgcAlgorithm::calculateLimits(const Session &session, const FrameContext &frameContext)
{
	/*
	 * Set the AGC limits using the fixed exposure time and/or gain in
	 * manual mode, or the sensor limits in auto mode.
	 */
	Limits result;

	if (frameContext.autoExposureEnabled) {
		result.exposure = {
			session.minExposureTime,
			std::clamp(frameContext.maxFrameDuration, session.minExposureTime, session.maxExposureTime),
		};
	} else {
		result.exposure.first = session.lineDuration * frameContext.exposure;
		result.exposure.second = result.exposure.first;
	}

	if (frameContext.autoGainEnabled)
		result.gain = { session.minAnalogueGain, session.maxAnalogueGain };
	else
		result.gain = { frameContext.gain, frameContext.gain };

	return result;
}

/**
 * \brief Handle a \a process operation
 */
void AgcAlgorithm::process(const Session& session, FrameContext& frameContext,
			   utils::Duration newExposureTime, ControlList &metadata)
{
	/*
	 * Expand the target frame duration so that we do not run faster than
	 * the minimum frame duration when we have short exposures.
	 */
	const auto frameDuration = std::max(frameContext.minFrameDuration, newExposureTime);
	frameContext.vblank = (frameDuration / session.lineDuration) - session.sensor.outputSize.height;

	/* Update frame duration accounting for line length quantization. */
	frameContext.frameDuration = (session.sensor.outputSize.height + frameContext.vblank) * session.lineDuration;

	metadata.set(controls::AnalogueGain, frameContext.gain);
	metadata.set(controls::ExposureTime, utils::Duration(session.lineDuration * frameContext.exposure).get<std::micro>());
	metadata.set(controls::FrameDuration, frameContext.frameDuration.get<std::micro>());
	metadata.set(controls::ExposureTimeMode,
		     frameContext.autoExposureEnabled
		     ? controls::ExposureTimeModeAuto
		     : controls::ExposureTimeModeManual);
	metadata.set(controls::AnalogueGainMode,
		     frameContext.autoGainEnabled
		     ? controls::AnalogueGainModeAuto
		     : controls::AnalogueGainModeManual);
}

} /* namespace ipa */

} /* namespace libcamera */
