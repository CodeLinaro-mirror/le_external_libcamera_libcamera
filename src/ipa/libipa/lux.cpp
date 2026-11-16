/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * libIPA lux estimation
 */
#include "lux.h"

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include "libcamera/internal/value_node.h"

#include "histogram.h"

/**
 * \file lux.h
 * \brief libipa implementation of lux estimation
 *
 * Estimating the lux level of an image is a common operation that can for
 * instance be used to adjust the target Y value in AGC or for Bayesian AWB
 * estimation.
 */

namespace libcamera {

using namespace std::literals::chrono_literals;

LOG_DEFINE_CATEGORY(Lux)

namespace ipa {

namespace lux {

/**
 * \class ActiveState
 * \brief The active state for the algorithm
 *
 * \var ActiveState::lux
 * \brief The latest lux estimation
 */

/**
 * \class FrameContext
 * \brief The frame context for the algorithm
 *
 * \var FrameContext::lux
 * \brief The estimated lux value for the given frame
 */

} /* namespace lux */

/**
 * \class LuxAlgorithm
 * \brief Lux estimation algorithm
 *
 * This algorithm provides lux estimation based on tuning data. IPAs that wish to
 * use lux estimation should create a Lux algorithm module that lightly wraps this
 * module by providing the platform-specific luminance histogram. The Lux entry in
 * the tuning file must then precede the algorithms that depend on the estimated lux
 * value.
 */

/**
 * \class LuxAlgorithm::ProcessParams
 * \brief Parameters for LuxAlgorithm::process()
 *
 * \var LuxAlgorithm::ProcessParams::exposureTime
 * \brief Effective exposure time of the frame
 *
 * \var LuxAlgorithm::ProcessParams::analogueGain
 * \brief Effective analogue gain of the frame
 *
 * \var LuxAlgorithm::ProcessParams::digitalGain
 * \brief Effective digital gain of the frame
 *
 * \var LuxAlgorithm::ProcessParams::yHist
 * \brief Luminance histogram of the frame
 */

/**
 * \var LuxAlgorithm::referenceExposureTime_
 * \brief The exposure time of the reference image, in microseconds
 */

/**
 * \var LuxAlgorithm::referenceAnalogueGain_
 * \brief The analogue gain of the reference image
 */

/**
 * \var LuxAlgorithm::referenceDigitalGain_
 * \brief The analogue gain of the reference image
 */

/**
 * \var LuxAlgorithm::referenceY_
 * \brief The measured luminance of the reference image, normalized to 1
 *
 */

/**
 * \var LuxAlgorithm::referenceLux_
 * \brief The estimated lux level of the reference image
 */

/**
 * \brief Initialize algorithm and parse tuning data
 * \param[in] tuningData The ValueNode representing the tuning data
 *
 * This function parses tuning data for the common Lux module. It requires
 * reference exposure time, analogue gain, digital gain, and lux values.
 *
 * \code{.unparsed}
 * algorithms:
 *   - Lux:
 *       referenceExposureTime: 10000
 *       referenceAnalogueGain: 4.0
 *       referenceDigitalGain: 1.0
 *       referenceY: 0.1831
 *       referenceLux: 1000
 * \endcode
 *
 * \return 0 on success or a negative error code
 */
int LuxAlgorithm::init(const ValueNode &tuningData)
{
	auto value = tuningData["referenceExposureTime"].get<double>();
	if (!value) {
		LOG(Lux, Error) << "Missing tuning parameter: "
				<< "'referenceExposureTime'";
		return -EINVAL;
	}
	referenceExposureTime_ = *value * 1.0us;

	value = tuningData["referenceAnalogueGain"].get<double>();
	if (!value) {
		LOG(Lux, Error) << "Missing tuning parameter: "
				<< "'referenceAnalogueGain'";
		return -EINVAL;
	}
	referenceAnalogueGain_ = *value;

	value = tuningData["referenceDigitalGain"].get<double>();
	if (!value) {
		LOG(Lux, Error) << "Missing tuning parameter: "
				<< "'referenceDigitalGain'";
		return -EINVAL;
	}
	referenceDigitalGain_ = *value;

	value = tuningData["referenceY"].get<double>();
	if (!value) {
		LOG(Lux, Error) << "Missing tuning parameter: "
				<< "'referenceY'";
		return -EINVAL;
	}
	referenceY_ = *value;

	value = tuningData["referenceLux"].get<double>();
	if (!value) {
		LOG(Lux, Error) << "Missing tuning parameter: "
				<< "'referenceLux'";
		return -EINVAL;
	}
	referenceLux_ = *value;

	return 0;
}

/**
 * \param[in] state The active state
 * \param[in] frameContext The frame context
 *
 * This function finalizes \ref lux::FrameContext::lux "frameContext.lux"
 * based on the latest estimation.
 */
void LuxAlgorithm::prepare(const lux::ActiveState &state, lux::FrameContext &frameContext)
{
	frameContext.lux = state.lux;
}

/**
 * \param[in] state The active state
 * \param[in] frameContext The frame context
 * \param[in] params The processing parameters
 * \param[in] metadata The metadata
 *
 * This function updates the lux estimation based on the luminance histogram
 * and other parameters of a completed frame, and additionally it also updates
 * \a metadata for the frame.
 */
void LuxAlgorithm::process(lux::ActiveState &state, const lux::FrameContext &frameContext,
			   ControlList &metadata, std::optional<ProcessParams> &&params)
{
	/*
	 * Report the lux level used by algorithms to prepare this frame
	 * not the lux level *of* this frame.
	 */
	metadata.set(controls::Lux, frameContext.lux);

	/* If no statistics are available, keep the latest estimation. */
	if (!params)
		return;

	state.lux = estimateLux(*params);
}

double LuxAlgorithm::estimateLux(const ProcessParams &params) const
{
	double currentY = params.yHist.interQuantileMean(0, 1);
	double exposureTimeRatio = referenceExposureTime_ / params.exposureTime;
	double aGainRatio = referenceAnalogueGain_ / params.analogueGain;
	double dGainRatio = referenceDigitalGain_ / params.digitalGain;
	double yRatio = (currentY / params.yHist.bins()) / referenceY_;

	double estimatedLux = exposureTimeRatio * aGainRatio * dGainRatio *
			      yRatio * referenceLux_;

	LOG(Lux, Debug) << "Estimated lux " << estimatedLux;
	return estimatedLux;
}

} /* namespace ipa */

} /* namespace libcamera */
