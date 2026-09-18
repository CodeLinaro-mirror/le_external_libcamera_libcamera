/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Black Level Correction control
 */

#include "blc.h"

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "libcamera/internal/value_node.h"

/**
 * \file blc.h
 */

namespace libcamera {

namespace ipa::rppx1::algorithms {

LOG_DEFINE_CATEGORY(RppX1Blc)

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int BlackLevelCorrection::init(IPAContext &context, [[maybe_unused]] const ValueNode &tuningData)
{
	auto blackLevel = context.camHelper->blackLevel();
	if (!blackLevel) {
		LOG(RppX1Blc, Error)
			<< "No black levels provided by camera sensor helper";
		return -ENOTSUP;
	}

	blackLevelRed_ = *blackLevel;
	blackLevelGreenR_ = *blackLevel;
	blackLevelGreenB_ = *blackLevel;
	blackLevelBlue_ = *blackLevel;

	LOG(RppX1Blc, Debug)
		<< "Black levels: red " << blackLevelRed_
		<< ", green (red) " << blackLevelGreenR_
		<< ", green (blue) " << blackLevelGreenB_
		<< ", blue " << blackLevelBlue_;

	return 0;
}

int BlackLevelCorrection::configure([[maybe_unused]] IPAContext &context,
				    [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void BlackLevelCorrection::prepare([[maybe_unused]] IPAContext &context,
				   const uint32_t frame,
				   [[maybe_unused]] IPAFrameContext &frameContext,
				   RppX1Params *params)
{
	if (frame > 0)
		return;

	auto config = params->block<BlockType::BlsPre1>();
	config.setEnabled(true);

	config->mode = RPPX1_BLS_MODE_FIXED;
	config->en_windows = RPPX1_BLS_WIN_EN_OFF;

	/*
	 * RPP-X1: the rppx1 specifies fixed BLS values per-color component
	 * matching on the input port cropping configuration.
	 * We have the same BLS value for all channels, so this is not relevant
	 * for now, but should probably be handled properly.
	 *
	 * Black level values are 16 bits, scale to the RPP-X1 PRE1 pipe
	 * 24-bit-domain.
	 */

	config->fixed.a = static_cast<int32_t>(blackLevelRed_) << 8;
	config->fixed.b = static_cast<int32_t>(blackLevelGreenR_) << 8;
	config->fixed.c = static_cast<int32_t>(blackLevelGreenB_) << 8;
	config->fixed.d = static_cast<int32_t>(blackLevelBlue_) << 8;
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void BlackLevelCorrection::process([[maybe_unused]] IPAContext &context,
				   [[maybe_unused]] const uint32_t frame,
				   [[maybe_unused]] IPAFrameContext &frameContext,
				   [[maybe_unused]] const RppX1Stats *stats,
				   ControlList &metadata)
{
	metadata.set(controls::SensorBlackLevels,
		     { static_cast<int32_t>(blackLevelRed_),
		       static_cast<int32_t>(blackLevelGreenR_),
		       static_cast<int32_t>(blackLevelGreenB_),
		       static_cast<int32_t>(blackLevelBlue_) });
}

REGISTER_IPA_ALGORITHM(BlackLevelCorrection, "BlackLevelCorrection")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
