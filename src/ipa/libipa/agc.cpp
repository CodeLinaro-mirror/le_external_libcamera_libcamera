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
#include <variant>

#include <linux/v4l2-controls.h>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

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
 * \brief Load tuning data
 */
int AgcAlgorithm::init(const ValueNode &tuningData, CameraSensorHelper *sensor)
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

	return 0;
}

/**
 * \brief Initialize the session configuration and active state
 */
int AgcAlgorithm::configure(agc::Session &session, agc::ActiveState &state,
			    const ConfigurationParams &config)
{
	const auto lineLength = config.sensorInfo.minLineLength;

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
		<< "exposure:[" << minExposure << ',' << maxExposure << ']'
		<< " gain:[" << minGain << ',' << maxGain << ']'
		<< " line-duration:" << session.lineDuration
		<< " sensor-output:" << session.sensor.outputSize;

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
		uint64_t frameSize = uint64_t(lineLength) * frameHeights[i];
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

	/* \todo Move this to the `Camera` class. */
	config.ctrlMap[&controls::AeEnable] = ControlInfo{
		false,
		session.autoAllowed,
		session.autoAllowed,
	};
	config.ctrlMap[&controls::AnalogueGain] = ControlInfo{
		minGain,
		maxGain,
		defGain,
	};
	config.ctrlMap[&controls::ExposureTime] = ControlInfo{
		static_cast<int32_t>(minExposure * lineDurationUs),
		static_cast<int32_t>(maxExposure * lineDurationUs),
		static_cast<int32_t>(defExposure * lineDurationUs),
	};
	config.ctrlMap[&controls::FrameDurationLimits] = ControlInfo{
		frameDurations[0],
		frameDurations[1],
		Span<const int64_t, 2>{ { frameDurations[0], frameDurations[1] } },
	};

	std::visit(utils::overloaded{
		[&](AgcMSV&) {
			/* no constraint/exposure mode support */
			state.constraintMode = controls::AeConstraintModeEnum::ConstraintNormal;
			state.exposureMode = controls::AeExposureModeEnum::ExposureNormal;

			state.automatic.yTarget = (2.5 - 1) / (5 - 1); /* \todo hack? */

			if (session.autoAllowed) {
				config.ctrlMap[&controls::AeConstraintMode] = ControlInfo(
					std::array{ ControlValue(state.constraintMode) }
				);

				config.ctrlMap[&controls::AeExposureMode] = ControlInfo(
					std::array{ ControlValue(state.exposureMode) }
				);
			}
		},
		[&](AgcMeanLuminance& impl) {
			state.constraintMode =
				static_cast<controls::AeConstraintModeEnum>(impl.constraintModes().begin()->first);
			state.exposureMode =
				static_cast<controls::AeExposureModeEnum>(impl.exposureModeHelpers().begin()->first);

			state.automatic.yTarget = impl.effectiveYTarget(0, 1);

			ASSERT(sensor_);
			impl.configure(session.lineDuration, sensor_);
			impl.resetFrameCount();

			if (session.autoAllowed) {
				config.ctrlMap[&controls::ExposureValue] = ControlInfo(-8.0f, 8.0f, 0.0f);

				{
					std::vector<ControlValue> options;
					for (const auto &[id, _] : impl.constraintModes())
						options.emplace_back(id);

					config.ctrlMap[&controls::AeConstraintMode] = ControlInfo(options);
				}

				{
					std::vector<ControlValue> options;
					for (const auto &[id, _] : impl.exposureModeHelpers())
						options.emplace_back(id);

					config.ctrlMap[&controls::AeExposureMode] = ControlInfo(options);
				}
			}
		},
	}, impl_);

	if (!session.autoAllowed) {
		config.ctrlMap.erase(&controls::ExposureValue);
		config.ctrlMap.erase(&controls::AeConstraintMode);
		config.ctrlMap.erase(&controls::AeExposureMode);
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
			session.minFrameDuration, session.maxFrameDuration);

		state.maxFrameDuration = std::clamp<utils::Duration>(
			std::chrono::microseconds((*frameDurationLimits).back()),
			session.minFrameDuration, session.maxFrameDuration);
	}
	frameContext.minFrameDuration = state.minFrameDuration;
	frameContext.maxFrameDuration = state.maxFrameDuration;
}

/**
 * \brief Handle a \a prepare operation
 */
void AgcAlgorithm::prepare(const agc::Session &session, agc::ActiveState &state, agc::FrameContext &frameContext)
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

	/*
	 * Expand the target frame duration so that we do not run faster than
	 * the minimum frame duration when we have short exposures.
	 */
	const auto frameDuration = std::max<uint32_t>(
		frameContext.minFrameDuration / session.lineDuration,
		frameContext.exposure);
	frameContext.vblank = frameDuration - session.sensor.outputSize.height;

	/* Update frame duration accounting for line length quantization. */
	frameContext.frameDuration =
		(session.sensor.outputSize.height + frameContext.vblank) * session.lineDuration;
}

/**
 * \brief Handle a \a process operation
 */
void AgcAlgorithm::process(const agc::Session &session, agc::ActiveState &state,
			   agc::FrameContext &frameContext, std::optional<ProcessParams> &&params,
			   ControlList &metadata)
{
	const utils::Duration &lineDuration = session.lineDuration;

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

		if (state.autoExposureEnabled) {
			minExposureTime = session.minExposureTime;
			maxExposureTime = std::clamp(state.maxFrameDuration,
						     session.minExposureTime,
						     session.maxExposureTime);
		} else {
			minExposureTime = lineDuration * state.manual.exposure;
			maxExposureTime = minExposureTime;
		}

		if (state.autoGainEnabled) {
			minAnalogueGain = session.minAnalogueGain;
			maxAnalogueGain = session.maxAnalogueGain;
		} else {
			minAnalogueGain = state.manual.gain;
			maxAnalogueGain = state.manual.gain;
		}

		std::visit(utils::overloaded{
			[&](AgcMSV& impl) {
				impl.setLimits({
					.exposure = {
						uint32_t(minExposureTime / lineDuration),
						uint32_t(maxExposureTime / lineDuration),
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
				 * The Agc algorithm needs to know the effective exposure value that was
				 * applied to the sensor when the statistics were collected.
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
					.constraintModeIndex = state.constraintMode,
					.exposureModeIndex = state.exposureMode,
					.lux = params->lux,
					.exposureCompensation = pow(2.0, state.exposureValue),
				});

				/* Update the estimated exposure and gain. */
				state.automatic.exposure = newEv.exposureTime / lineDuration;
				state.automatic.gain = newEv.analogueGain;
				state.automatic.quantizationGain = newEv.quantizationGain;
				state.automatic.digitalGain = newEv.digitalGain;
				state.automatic.yTarget = newEv.yTarget;
			},
		}, impl_);

		LOG(Agc, Debug)
			<< "exposure-time:" << utils::Duration(state.automatic.exposure * lineDuration)
			<< " analogue-gain:" << state.automatic.gain
			<< " quantization-gain:" << state.automatic.quantizationGain
			<< " digital-gain:" << state.automatic.digitalGain;
	}

	metadata.set(controls::AnalogueGain, frameContext.gain);
	metadata.set(controls::ExposureTime,
		     utils::Duration(lineDuration * frameContext.exposure).get<std::micro>());
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
