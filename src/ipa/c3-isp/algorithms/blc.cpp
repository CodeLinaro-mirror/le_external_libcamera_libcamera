/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Black Level Correction control
 */

#include "blc.h"

#include <cmath>

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

Blc::Blc()
	: tuningBlc_(false)
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Blc::init([[maybe_unused]] IPAContext &context, const YamlObject &tuningData)
{
	offsetR_ = tuningData["offsetR"].get<uint32_t>(0);
	offsetGr_ = tuningData["offsetGr"].get<uint32_t>(0);
	offsetGb_ = tuningData["offsetGb"].get<uint32_t>(0);
	offsetB_ = tuningData["offsetB"].get<uint32_t>(0);

	if (!(offsetR_ + offsetGr_ + offsetGb_ + offsetB_)) {
		LOG(C3ISPBlc, Debug) << "All black level offsets are empty";
		tuningBlc_ = false;
	} else {
		tuningBlc_ = true;
	}

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int Blc::configure(IPAContext &context,
		   [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	uint16_t blackLevel = offsetCompress(context.configuration.sensor.blackLevel);

	if (blackLevel && !tuningBlc_) {
		offsetR_ = context.configuration.sensor.blackLevel;
		offsetGr_ = context.configuration.sensor.blackLevel;
		offsetGb_ = context.configuration.sensor.blackLevel;
		offsetB_ = context.configuration.sensor.blackLevel;
	}

	return 0;
}

uint16_t Blc::offsetCompress(uint32_t blackLevel)
{
	uint8_t shift = 0;
	uint32_t integer = 0;
	uint8_t highestBitPos = static_cast<uint8_t>(log2(blackLevel));

	/* The compressed offset is stored in Q4.12 format */
	if (highestBitPos < 12) {
		shift = 0;
		integer = blackLevel;
	} else {
		shift = highestBitPos - 11;
		integer = blackLevel >> shift;
	}

	shift = (shift > 0xf) ? 0xf : shift;

	return (shift & 0xf) << 12 | (integer & 0xfff);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Blc::prepare([[maybe_unused]] IPAContext &context, const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext,
		  C3ISPParams *params)
{
	if (frame > 0)
		return;

	if (!tuningBlc_ && !context.configuration.sensor.blackLevel)
		return;

	auto blcCfg = params->block<BlockType::Blc>();
	blcCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_ENABLE);

	blcCfg->gr_ofst = offsetCompress(offsetGr_);
	blcCfg->r_ofst = offsetCompress(offsetR_);
	blcCfg->b_ofst = offsetCompress(offsetB_);
	blcCfg->gb_ofst = offsetCompress(offsetGb_);
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
	/*
	 * Black Level Offsets in tuning data need to be 20-bit, whereas the
	 * metadata expects values from a 16-bit range. Right-shift to remove
	 * the 4 least significant bits.
	 */
	metadata.set(controls::SensorBlackLevels,
		     { static_cast<int32_t>(offsetR_ >> 4),
		       static_cast<int32_t>(offsetGr_ >> 4),
		       static_cast<int32_t>(offsetGb_ >> 4),
		       static_cast<int32_t>(offsetB_ >> 4) });
}

REGISTER_IPA_ALGORITHM(Blc, "Blc")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
