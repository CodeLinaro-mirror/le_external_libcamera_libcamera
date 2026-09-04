/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google inc.
 *
 * IPU3 ToneMapping and Gamma control
 */

#include "tone_mapping.h"

#include <algorithm>
#include <cmath>
#include <string.h>

#include <libcamera/base/log.h>

#include "libcamera/internal/value_node.h"

/**
 * \file tone_mapping.h
 */

namespace libcamera {

namespace ipa::ipu3::algorithms {

/**
 * \class ToneMapping
 * \brief A class to handle tone mapping based on gamma
 *
 * This algorithm improves the image dynamic using a look-up table which is
 * generated based on a gamma parameter.
 */

LOG_DEFINE_CATEGORY(IPU3ToneMapping)

/* Historical default, kept for existing tuning files. */
static constexpr double kDefaultGamma = 1.1;

ToneMapping::ToneMapping()
	: gamma_(1.0), tunedGamma_(kDefaultGamma)
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 *
 * The optional \a gamma tuning parameter sets the exponent of the encoding
 * curve programmed in the ImgU gamma correction LUT, output = input^(1/gamma).
 * The default is 1.1.
 *
 * Note that on the IPU3 firmware currently distributed for Linux
 * (irci_irci_ecr-master_20161208_0213_20170112_1500) the video pipe applies
 * a fixed curve when the gamma block is programmed and ignores the LUT
 * contents: values of 0.5, 1.1 and 3.0 produce identical output, while not
 * programming the block yields a linear ramp. The parameter is still useful
 * to document the intent and for firmware that honours the LUT.
 */
int ToneMapping::init([[maybe_unused]] IPAContext &context,
		      const ValueNode &tuningData)
{
	tunedGamma_ = std::clamp(tuningData["gamma"].get<double>().value_or(kDefaultGamma),
				 0.5, 4.0);

	LOG(IPU3ToneMapping, Debug) << "Gamma " << tunedGamma_;

	return 0;
}

/**
 * \brief Configure the tone mapping given a configInfo
 * \param[in] context The shared IPA context
 * \param[in] configInfo The IPA configuration data
 *
 * \return 0
 */
int ToneMapping::configure(IPAContext &context,
			   [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	/* Initialise tone mapping gamma value. */
	context.activeState.toneMapping.gamma = 0.0;

	return 0;
}

/**
 * \brief Fill in the parameter structure, and enable gamma control
 * \param[in] context The shared IPA context
 * \param[in] frame The frame context sequence number
 * \param[in] frameContext The FrameContext for this frame
 * \param[out] params The IPU3 parameters
 *
 * Populate the IPU3 parameter structure with our tone mapping look up table and
 * enable the gamma control module in the processing blocks.
 */
void ToneMapping::prepare([[maybe_unused]] IPAContext &context,
			  [[maybe_unused]] const uint32_t frame,
			  [[maybe_unused]] IPAFrameContext &frameContext,
			  ipu3_uapi_params *params)
{
	/* Copy the calculated LUT into the parameters buffer. */
	memcpy(params->acc_param.gamma.gc_lut.lut,
	       context.activeState.toneMapping.gammaCorrection.lut,
	       IPU3_UAPI_GAMMA_CORR_LUT_ENTRIES *
	       sizeof(params->acc_param.gamma.gc_lut.lut[0]));

	/* Enable the custom gamma table. */
	params->use.acc_gamma = 1;
	params->acc_param.gamma.gc_ctrl.enable = 1;
}

/**
 * \brief Calculate the tone mapping look up table
 * \param[in] context The shared IPA context
 * \param[in] frame The current frame sequence number
 * \param[in] frameContext The current frame context
 * \param[in] stats The IPU3 statistics and ISP results
 * \param[out] metadata Metadata for the frame, to be filled by the algorithm
 *
 * The tone mapping look up table is generated as an inverse power curve from
 * our gamma setting.
 */
void ToneMapping::process(IPAContext &context, [[maybe_unused]] const uint32_t frame,
			  [[maybe_unused]] IPAFrameContext &frameContext,
			  [[maybe_unused]] const ipu3_uapi_stats_3a *stats,
			  [[maybe_unused]] ControlList &metadata)
{
	/*
	 * Hardcode gamma to 1.1 as a default for now.
	 *
	 * \todo Expose gamma control setting through the libcamera control API
	 */
	gamma_ = 1.1;

	if (context.activeState.toneMapping.gamma == gamma_)
		return;

	struct ipu3_uapi_gamma_corr_lut &lut =
		context.activeState.toneMapping.gammaCorrection;

	for (uint32_t i = 0; i < std::size(lut.lut); i++) {
		double j = static_cast<double>(i) / (std::size(lut.lut) - 1);
		double gamma = std::pow(j, 1.0 / gamma_);

		/* The output value is expressed on 13 bits. */
		lut.lut[i] = gamma * 8191;
	}

	context.activeState.toneMapping.gamma = gamma_;
}

REGISTER_IPA_ALGORITHM(ToneMapping, "ToneMapping")

} /* namespace ipa::ipu3::algorithms */

} /* namespace libcamera */
