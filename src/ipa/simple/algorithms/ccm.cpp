/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 * Copyright (C) 2024-2025, Red Hat Inc.
 *
 * Color correction matrix
 */

#include "ccm.h"

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>

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

	return 0;
}

void Ccm::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, [[maybe_unused]] DebayerParams *params)
{
	const unsigned int ct = context.activeState.awb.temperatureK;

	/* Change CCM only on bigger temperature changes. */
	if (frame > 0 &&
	    utils::abs_diff(ct, lastCt_) < kTemperatureThreshold) {
		frameContext.ccm.ccm = context.activeState.ccm.ccm;
		context.activeState.ccm.changed = false;
		return;
	}

	lastCt_ = ct;
	Matrix<double, 3, 3> ccm = ccm_.getInterpolated(ct);

	context.activeState.ccm.ccm = ccm;
	frameContext.ccm.ccm = ccm;
	context.activeState.ccm.changed = true;
}

void Ccm::process([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  [[maybe_unused]] const SwIspStats *stats,
		  ControlList &metadata)
{
	float m[9];
	for (unsigned int i = 0; i < 3; i++) {
		for (unsigned int j = 0; j < 3; j++)
			m[i * 3 + j] = frameContext.ccm.ccm[i][j];
	}
	metadata.set(controls::ColourCorrectionMatrix, m);
}

REGISTER_IPA_ALGORITHM(Ccm, "Ccm")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
