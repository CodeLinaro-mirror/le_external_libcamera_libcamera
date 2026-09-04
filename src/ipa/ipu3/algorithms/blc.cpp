/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google inc.
 *
 * IPU3 Black Level Correction control
 */

#include "blc.h"

#include <libcamera/base/log.h>

#include "libcamera/internal/value_node.h"

/**
 * \file blc.h
 * \brief IPU3 Black Level Correction control
 */

namespace libcamera {

namespace ipa::ipu3::algorithms {

/**
 * \class BlackLevelCorrection
 * \brief A class to handle black level correction
 *
 * The pixels output by the camera normally include a black level, because
 * sensors do not always report a signal level of '0' for black. Pixels at or
 * below this level should be considered black. To achieve that, the ImgU BLC
 * algorithm subtracts a configurable offset from all pixels.
 *
 * The black level can be measured at runtime from an optical dark region of the
 * camera sensor, or measured during the camera tuning process. The first option
 * isn't currently supported.
 */

LOG_DEFINE_CATEGORY(IPU3Blc)

/*
 * Default optical black level. This matches the OV5670 sensor black level
 * when interpreted as the ImgU obgrid unit (see init()).
 */
static constexpr int16_t kDefaultBlackLevel = 64;

BlackLevelCorrection::BlackLevelCorrection()
	: blackLevel_(kDefaultBlackLevel)
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 *
 * The optional \a blackLevel tuning parameter sets the optical black level
 * subtracted by the ImgU for the four Bayer channels. The ImgU obgrid unit
 * has been measured to be half a 10-bit LSB: with a value of 64 an OV5670
 * (black level 64 in 10-bit) keeps a residual pedestal of ~9/255 in the
 * AWB statistics and in the image, which the colour gains then amplify;
 * with 128 the pedestal disappears. The default is kept at 64 for
 * compatibility with existing tuning files.
 */
int BlackLevelCorrection::init([[maybe_unused]] IPAContext &context,
			       const ValueNode &tuningData)
{
	blackLevel_ = tuningData["blackLevel"].get<int16_t>().value_or(kDefaultBlackLevel);

	LOG(IPU3Blc, Debug) << "Black level " << blackLevel_;

	return 0;
}

/**
 * \brief Fill in the parameter structure, and enable black level correction
 * \param[in] context The shared IPA context
 * \param[in] frame The frame context sequence number
 * \param[in] frameContext The FrameContext for this frame
 * \param[out] params The IPU3 parameters
 *
 * Populate the IPU3 parameter structure with the correction values for each
 * channel and enable the corresponding ImgU block processing.
 */
void BlackLevelCorrection::prepare([[maybe_unused]] IPAContext &context,
				   [[maybe_unused]] const uint32_t frame,
				   [[maybe_unused]] IPAFrameContext &frameContext,
				   ipu3_uapi_params *params)
{
	/*
	 * The Optical Black Level correction values
	 * \todo The correction values should come from sensor specific
	 * tuning processes. This is a first rough approximation.
	 */
	params->obgrid_param.gr = blackLevel_;
	params->obgrid_param.r = blackLevel_;
	params->obgrid_param.b = blackLevel_;
	params->obgrid_param.gb = blackLevel_;

	/* Enable the custom black level correction processing */
	params->use.obgrid = 1;
	params->use.obgrid_param = 1;
}

REGISTER_IPA_ALGORITHM(BlackLevelCorrection, "BlackLevelCorrection")

} /* namespace ipa::ipu3::algorithms */

} /* namespace libcamera */
