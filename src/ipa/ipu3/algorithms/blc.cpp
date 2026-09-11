/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google inc.
 *
 * IPU3 Black Level Correction control
 */

#include "blc.h"

#include <libcamera/base/log.h>

#include "libipa/camera_sensor_helper.h"

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

/* Historical value, used when the camera sensor helper has no black level. */
static constexpr uint16_t kDefaultBlackLevel = 64;

BlackLevelCorrection::BlackLevelCorrection()
	: blackLevel_(kDefaultBlackLevel)
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int BlackLevelCorrection::init(IPAContext &context,
			       [[maybe_unused]] const ValueNode &tuningData)
{
	std::optional<int16_t> blackLevel = context.camHelper->blackLevel();
	if (!blackLevel) {
		LOG(IPU3Blc, Warning)
			<< "No black level provided by camera sensor helper"
			<< ", please fix";
		blackLevel_ = kDefaultBlackLevel;
	} else {
		/*
		 * CameraSensorHelper reports the black level as a 16-bit
		 * value, while experimentation has shown that the ImgU OB grid
		 * expects it in units of half a 10-bit pixel value: to cancel a
		 * data pedestal of 16 the ISP has to be configured with 32, and
		 * to cancel a pedestal of 64 with 128. The 16-bit value is
		 * therefore shifted right by 5.
		 */
		blackLevel_ = *blackLevel >> 5;
	}

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
	/* The Optical Black Level correction values */
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
