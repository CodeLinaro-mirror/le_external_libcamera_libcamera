/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Black Level Correction control
 */

#include "blc.h"

#include <libcamera/base/log.h>
#include <libcamera/control_ids.h>

#include "libcamera/internal/yaml_parser.h"

/**
 * \file blc.h
 */

namespace libcamera {

namespace ipa::c3isp::algorithms {

/**
 * \class Blc
 * \brief C3 ISP Black Level Correction control
 *
 * The pixels output by the camera normally include a black level, because
 * sensors do not always report a signal level of '0' for black. Pixels at or
 * below this level should be considered black. To achieve that, the C3 ISP BLC
 * algorithm subtracts a configurable offset from all pixels.
 *
 * The black level can be measured at runtime from an optical dark region of the
 * camera sensor, or measured during the camera tuning process. The first option
 * isn't currently supported.
 */

LOG_DEFINE_CATEGORY(C3ISPBlc)

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Blc::init([[maybe_unused]] IPAContext &context, const YamlObject &tuningData)
{
	std::optional<int16_t> levelRed = tuningData["BlcR"].get<int16_t>();
	std::optional<int16_t> levelGreenR = tuningData["BlcGr"].get<int16_t>();
	std::optional<int16_t> levelGreenB = tuningData["BlcGb"].get<int16_t>();
	std::optional<int16_t> levelBlue = tuningData["BlcB"].get<int16_t>();

	blackLevelRed_ = levelRed.value_or(4096);
	blackLevelGreenR_ = levelGreenR.value_or(4096);
	blackLevelGreenB_ = levelGreenB.value_or(4096);
	blackLevelBlue_ = levelBlue.value_or(4096);

	LOG(C3ISPBlc, Debug)
		<< "Black Levels: red " << blackLevelRed_
		<< ", green (red) " << blackLevelGreenR_
		<< ", green (blue) " << blackLevelGreenB_
		<< ", blue " << blackLevelBlue_;

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Blc::prepare([[maybe_unused]] IPAContext &context, const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext,
		  C3ISPParams *params)
{
	if (frame)
		return;

	auto blcCfg = params->block<BlockType::Blc>();
	blcCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_ENABLE);

	blcCfg->gr_ofst = blackLevelGreenR_;
	blcCfg->r_ofst = blackLevelRed_;
	blcCfg->b_ofst = blackLevelBlue_;
	blcCfg->gb_ofst = blackLevelGreenB_;
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Blc::process([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext,
		  [[maybe_unused]] const c3_isp_stats_info *stats,
		  ControlList &metadata)
{
	metadata.set(controls::SensorBlackLevels,
		     { static_cast<int32_t>(blackLevelRed_),
		       static_cast<int32_t>(blackLevelGreenR_),
		       static_cast<int32_t>(blackLevelGreenB_),
		       static_cast<int32_t>(blackLevelBlue_) });
}

REGISTER_IPA_ALGORITHM(Blc, "Blc")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
