/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Color Space Conversion control
 */

#include "csc.h"

#include <libcamera/base/log.h>
#include <libcamera/control_ids.h>

/**
 * \file csc.h
 */

namespace libcamera {

namespace ipa::c3isp::algorithms {

/**
 * \class Csc
 * \brief Color Space Conversion algorithm
 */

LOG_DEFINE_CATEGORY(C3ISPCsc)

Csc::Csc()
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Csc::init([[maybe_unused]] IPAContext &context,
	      [[maybe_unused]] const YamlObject &tuningData)
{
	cscCoeff = tuningData["CscCoeff"].getList<int>().value_or(std::vector<int>{});

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Csc::prepare([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext,
		  C3ISPParams *params)
{
	auto CscCfg = params->block<BlockType::Csc>();
	CscCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_ENABLE);

	for (unsigned int i = 0; i < 3; i++) {
		for (unsigned int j = 0; j < 3; j++)
			CscCfg->matrix[i][j] = cscCoeff[i * 3 + j];
	}
}

REGISTER_IPA_ALGORITHM(Csc, "Csc")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
