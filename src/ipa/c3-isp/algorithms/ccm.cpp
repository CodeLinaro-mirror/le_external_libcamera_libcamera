/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Color Correction Matrix control
 */

#include "ccm.h"

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>

#include "libcamera/internal/yaml_parser.h"

/**
 * \file ccm.h
 */

namespace libcamera {

namespace ipa::c3isp::algorithms {

/**
 * \class Ccm
 * \brief A color correction matrix algorithm
 */

LOG_DEFINE_CATEGORY(C3ISPCcm)

Ccm::Ccm()
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Ccm::init([[maybe_unused]] IPAContext &context, const YamlObject &tuningData)
{
	ccmCoeff_ = tuningData["ccmCoeff"].getList<int16_t>().value_or(std::vector<int16_t>{});
	if (ccmCoeff_.size() != 9) {
		LOG(C3ISPCcm, Error) << "Invalid CCM coeff size";
		return -EINVAL;
	}

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Ccm::prepare([[maybe_unused]] IPAContext &context,
		  const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext, C3ISPParams *params)
{
	if (frame > 0)
		return;

	auto CcmCfg = params->block<BlockType::Ccm>();
	CcmCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_ENABLE);

	for (unsigned int i = 0; i < 3; i++) {
		for (unsigned int j = 0; j < 3; j++) {
			CcmCfg->matrix[i][j] = ccmCoeff_[i * 3 + j];
		}
	}
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Ccm::process([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext,
		  [[maybe_unused]] const c3_isp_stats_info *stats,
		  ControlList &metadata)
{
	float m[9];
	for (unsigned int i = 0; i < 3; i++) {
		for (unsigned int j = 0; j < 3; j++)
			m[i * 3 + j] = ccmCoeff_[i * 3 + j] / 256.0;
	}

	metadata.set(controls::ColourCorrectionMatrix, m);
}

REGISTER_IPA_ALGORITHM(Ccm, "Ccm")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
