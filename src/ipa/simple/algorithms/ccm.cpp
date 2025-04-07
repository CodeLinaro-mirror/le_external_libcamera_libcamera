/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 * Copyright (C) 2024-2025, Red Hat Inc.
 *
 * Color correction matrix + saturation
 */

#include "ccm.h"

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>

#include "libcamera/internal/matrix.h"

namespace {

constexpr unsigned int kTemperatureThreshold = 100;

}

namespace libcamera {

namespace ipa::soft::algorithms {

LOG_DEFINE_CATEGORY(IPASoftCcm)

int Ccm::init([[maybe_unused]] IPAContext &context, const YamlObject &tuningData)
{
	int ret = ccm_.readYaml(tuningData["ccms"], "ct", "ccm");
	if (ret < 0) {
		LOG(IPASoftCcm, Error)
			<< "Failed to parse 'ccm' parameter from tuning file.";
		return ret;
	}

	context.ccmEnabled = true;
	context.ctrlMap[&controls::Saturation] = ControlInfo(0.0f, 2.0f, 1.0f);

	return 0;
}

int Ccm::configure(IPAContext &context,
		   [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	context.activeState.knobs.saturation = std::optional<double>();

	return 0;
}

void Ccm::queueRequest(typename Module::Context &context,
		       [[maybe_unused]] const uint32_t frame,
		       [[maybe_unused]] typename Module::FrameContext &frameContext,
		       const ControlList &controls)
{
	const auto &saturation = controls.get(controls::Saturation);
	if (saturation.has_value()) {
		context.activeState.knobs.saturation = saturation;
		LOG(IPASoftCcm, Debug) << "Setting saturation to " << saturation.value();
	}
}

void Ccm::updateSaturation(Matrix<float, 3, 3> &ccm, float saturation)
{
	/*
	 * See https://www.graficaobscura.com/matrix/index.html.
	 * This is applied before gamma thus a matrix for linear RGB must be used.
	 * The saturation range is 0..2, with 1 being an unchanged saturation and 0
	 * no saturation (monochrome).
	 */
	constexpr float r = 0.3086;
	constexpr float g = 0.6094;
	constexpr float b = 0.0820;
	const float s1 = 1.0 - saturation;
	ccm = ccm * Matrix<float, 3, 3>{ { s1 * r + saturation, s1 * g, s1 * b,
					   s1 * r, s1 * g + saturation, s1 * b,
					   s1 * r, s1 * g, s1 * b + saturation } };
}

void Ccm::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, [[maybe_unused]] DebayerParams *params)
{
	auto &saturation = context.activeState.knobs.saturation;

	const unsigned int ct = context.activeState.awb.temperatureK;

	/* Change CCM only on saturation or bigger temperature changes. */
	if (frame > 0 &&
	    utils::abs_diff(ct, lastCt_) < kTemperatureThreshold &&
	    saturation == lastSaturation_) {
		frameContext.ccm.ccm = context.activeState.ccm.ccm;
		context.activeState.ccm.changed = false;
		return;
	}

	lastCt_ = ct;
	lastSaturation_ = saturation;
	Matrix<float, 3, 3> ccm = ccm_.getInterpolated(ct);
	if (saturation)
		updateSaturation(ccm, saturation.value());

	context.activeState.ccm.ccm = ccm;
	frameContext.ccm.ccm = ccm;
	frameContext.saturation = saturation;
	context.activeState.ccm.changed = true;
}

void Ccm::process([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  [[maybe_unused]] const SwIspStats *stats,
		  ControlList &metadata)
{
	metadata.set(controls::ColourCorrectionMatrix, frameContext.ccm.ccm.data());

	const auto &saturation = frameContext.saturation;
	if (saturation)
		metadata.set(controls::Saturation, saturation.value());
}

REGISTER_IPA_ALGORITHM(Ccm, "Ccm")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
