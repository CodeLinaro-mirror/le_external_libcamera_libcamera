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
#include <libcamera/ipa/core_ipa_interface.h>

#include "libcamera/internal/yaml_parser.h"
#include "libipa/interpolator.h"

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
	ccmCoeff = tuningData["CcmCoeff"].getList<int>().value_or(std::vector<int>{});

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Ccm::prepare([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext, C3ISPParams *params)
{
	auto CcmCfg = params->block<BlockType::Ccm>();
	CcmCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_ENABLE);

	for (unsigned int i = 0; i < 3; i++) {
		for (unsigned int j = 0; j < 3; j++) {
			CcmCfg->matrix[i][j] = ccmCoeff[i * 3 + j];
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
			m[i * 3 + j] = ccmCoeff[i * 3 + j];
	}
	metadata.set(controls::ColourCorrectionMatrix, m);
}

REGISTER_IPA_ALGORITHM(Ccm, "Ccm")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
