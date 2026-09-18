/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Color Correction Matrix control algorithm
 */

#include "ccm.h"

#include <libcamera/base/log.h>

/**
 * \file ccm.h
 */

namespace libcamera {

namespace ipa::rppx1::algorithms {

LOG_DEFINE_CATEGORY(RppX1Ccm)

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Ccm::init([[maybe_unused]] IPAContext &context, const ValueNode &tuningData)
{
	return ccmAlgo_.init(tuningData, context.ctrlMap);
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int Ccm::configure(IPAContext &context,
		   [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	return ccmAlgo_.configure(context.activeState.ccm,
				  context.activeState.awb.automatic.colourTemperature);
}

void Ccm::queueRequest(IPAContext &context,
		       [[maybe_unused]] const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	/* Nothing to do here, the ccm will be calculated in prepare() */
	if (frameContext.awb.autoEnabled)
		return;

	ccmAlgo_.queueRequest(context.activeState.ccm, frameContext.ccm, controls);
}

void Ccm::setParameters(rppx1_ccor_params &config, IPAFrameContext &context)
{
	const Matrix<float, 3, 3> &matrix = context.ccm.ccm;
	const Matrix<int16_t, 3, 1> &offsets = context.ccm.offsets;

	/*
	 * RPP-X1 coefficients are Q4.12 signed fixed-point ranging from
	 * -8 (0x8000) to +7.9996 (0x7fff). x1 = 0x1000.
	 */
	for (unsigned int i = 0; i < 3; i++) {
		for (unsigned int j = 0; j < 3; j++)
			config.coeff[i][j] = Q<4, 12>(matrix[i][j]).quantized();
	}

	/* RPP-X1 offsets are 25 bits 2's complement.
	 * \todo: Better investigate how negative offsets are handled in the
	 * offsets interpolation and if the shift is correct.
	 */

	for (unsigned int i = 0; i < 3; i++)
		config.offset[i] = static_cast<int32_t>(offsets[i][0]) << 8;

	LOG(RppX1Ccm, Debug) << "Setting matrix " << matrix;
	LOG(RppX1Ccm, Debug) << "Setting offsets " << offsets;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Ccm::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, RppX1Params *params)
{
	if (frameContext.awb.autoEnabled)
		ccmAlgo_.prepare(context.activeState.ccm, frameContext.ccm,
				 frame, frameContext.awb.colourTemperature);

	auto config = params->block<BlockType::CcorPost>();
	config.setEnabled(true);

	setParameters(*config, frameContext);
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Ccm::process([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  [[maybe_unused]] const RppX1Stats *stats,
		  ControlList &metadata)
{
	ccmAlgo_.process(frameContext.ccm, metadata);
}

REGISTER_IPA_ALGORITHM(Ccm, "Ccm")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
