/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 AWB control algorithm
 */


#include "awb.h"

#include <libcamera/base/log.h>
#include <libcamera/internal/vector.h>

/**
 * \file awb.h
 * \brief RPP-X1 Auto White Balance
 */

namespace libcamera {

namespace ipa::rppx1::algorithms {

/* \todo RPP-X1 doesn't support the Lux algorithm. */
static constexpr unsigned int kDefaultLux = 500;

LOG_DEFINE_CATEGORY(RppX1Awb)

namespace {

/* Quantize a gain to the UQ6.12 register format. */
constexpr uint32_t quantizeGain(double gain)
{
	return UQ<6, 12>(static_cast<float>(gain)).quantized();
}

}; /* namespace */

/*
 * The RPP-X1 ISP has a bus width of 24 bits and all the measurement limits are
 * expressed as 24 bit values. In order to make it simpler to express limits
 * in code use an 8-bit value and left-shift by 16 to match the 24 bit width of
 * the ISP processing pipeline.
 */
constexpr unsigned int kRPPX1BusWidthShift = 16;

class RppX1AwbStats final : public AwbStats
{
public:
	RppX1AwbStats() {}
	RppX1AwbStats(const RGB<double> &rgbMeans)
		: AwbStats(rgbMeans)
	{
	}

	/* Minimum mean value below which AWB can't operate. */
	double minColourValue() const override
	{
		return 2.0;
	}
};

/**
 * \class Awb
 * \brief RPP-X1 white balance correction algorithm
 */

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Awb::init(IPAContext &context, const ValueNode &tuningData)
{
	return awbAlgo_.init(tuningData, context.ctrlMap);
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int Awb::configure(IPAContext &context,
		   const IPACameraSensorInfo &configInfo)
{
	awbAlgo_.configure(context.activeState.awb);

	/*
	 * Define the measurement window for AWB as a centered rectangle
	 * covering 3/4 of the image width and height.
	 */
	context.configuration.awb.measureWindow.h_offs = configInfo.outputSize.width / 8;
	context.configuration.awb.measureWindow.v_offs = configInfo.outputSize.height / 8;
	context.configuration.awb.measureWindow.h_size = 3 * configInfo.outputSize.width / 4;
	context.configuration.awb.measureWindow.v_size = 3 * configInfo.outputSize.height / 4;

	context.configuration.awb.enabled = true;

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Awb::queueRequest(IPAContext &context, const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	awbAlgo_.queueRequest(context.activeState.awb, frame, frameContext.awb,
			      controls);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Awb::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, RppX1Params *params)
{
	awbAlgo_.prepare(context.activeState.awb, frameContext.awb);

	auto gainConfig = params->block<BlockType::AwbgPre1>();
	gainConfig.setEnabled(true);

	gainConfig->gain_green_b = quantizeGain(frameContext.awb.gains.g());
	gainConfig->gain_blue = quantizeGain(frameContext.awb.gains.b());
	gainConfig->gain_red = quantizeGain(frameContext.awb.gains.r());
	gainConfig->gain_green_r = quantizeGain(frameContext.awb.gains.g());

	if (frame > 0)
		return;

	/*
	 * If this is the first frame, program the white balance measurement
	 * engine.
	 */

	auto awbConfig = params->block<BlockType::WbmeasPost>();
	awbConfig.setEnabled(true);

	/* Configure the measure window for AWB. */
	awbConfig->wnd = context.configuration.awb.measureWindow;

	/* Number of frames to use to estimate the means (0 means 1 frame). */
	awbConfig->frames = 0;

	/* Use YCbCr measurement mode. */
	awbConfig->mode = RPPX1_WBMEAS_MODE_YCBCR;

	/* Set the reference Cr and Cb (AWB target) to white. */
	awbConfig->ref_cb_max_b = 128 << kRPPX1BusWidthShift;
	awbConfig->ref_cr_max_r = 128 << kRPPX1BusWidthShift;

	/*
	 * Filter out pixels based on luminance and chrominance values.
	 * The acceptable luma values are specified as a [16, 250]
	 * range, while the acceptable chroma values are specified
	 * with a minimum of 16 and a maximum Cb+Cr sum of 250;
	 */
	awbConfig->min_y_max_g = 16 << kRPPX1BusWidthShift;
	awbConfig->max_y = 250 << kRPPX1BusWidthShift;
	awbConfig->min_c = 16 << kRPPX1BusWidthShift;
	awbConfig->max_csum = 250 << kRPPX1BusWidthShift;

	/* \todo Disable ymax_cmp even if we program 'max_y' */
	awbConfig->ymax_cmp = 0;

	/*
	 * Program coefficients and offsets to perform RGB-to-YCbCr
	 * conversion according to the BT.601 specification for limited
	 * range YUV.
	 *
	 *  Y = 16 + 0.2500 R + 0.5000 G + 0.1094 B
	 *  Cb = 128 - 0.1406 R - 0.2969 G + 0.4375 B
	 *  Cr = 128 + 0.4375 R - 0.3750 G - 0.0625 B
	 *
	 * which resuls in the following register values:
	 *
	 * - coefficients are signed Q4.12 format
	 * - their numerical value is then 'reg / 2^12'
	 * - negative numbers need to reverse the 2's complement
	 *   (!(reg & !BIT(16)) + 1) / 2^12
	 *
	 * coeff G0 0x00000800; = 0.500
	 * coeff B0 0x000001c0; = 0.1094
	 * coeff R0 0x00000400; = 0.2500
	 * coeff G1 0x0000fb40; = -0.2969
	 * coeff B1 0x00000700; = 0.4375
	 * coeff R1 0x0000fdc0; = -0.1406
	 * coeff G2 0x0000fa00; = -0.3750
	 * coeff B2 0x0000ff00; = -0.0625
	 * coeff R2 0x00000700; = 0.4375
	 * offset R 0x00100000; offset_r = (16 << 16)
	 * offset G 0x00800000; offset_g = (128 << 16)
	 * offset B 0x00800000; offset_b = (128 << 16)
	 *
	 * Use the inverse of this matrix in calculateRgbMeans() to
	 * reverse the colorspace conversion.
	 */
	awbConfig->ccor_coeff[0][0] = 0x00000800;
	awbConfig->ccor_coeff[0][1] = 0x000001c0;
	awbConfig->ccor_coeff[0][2] = 0x00000400;
	awbConfig->ccor_coeff[1][0] = 0x0000fb40;
	awbConfig->ccor_coeff[1][1] = 0x00000700;
	awbConfig->ccor_coeff[1][2] = 0x0000fdc0;
	awbConfig->ccor_coeff[2][0] = 0x0000fa00;
	awbConfig->ccor_coeff[2][1] = 0x0000ff00;
	awbConfig->ccor_coeff[2][2] = 0x00000700;

	awbConfig->ccor_offs[0] = 16 << kRPPX1BusWidthShift;
	awbConfig->ccor_offs[1] = 128 << kRPPX1BusWidthShift;
	awbConfig->ccor_offs[2] = 128 << kRPPX1BusWidthShift;
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Awb::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const RppX1Stats *stats,
		  ControlList &metadata)
{
	RppX1AwbStats awbStats = calculateRgbMeans(frameContext, stats);

	awbAlgo_.process(context.activeState.awb, frameContext.awb, awbStats,
			 kDefaultLux, metadata);
}

RppX1AwbStats Awb::calculateRgbMeans(const IPAFrameContext &frameContext,
				     const RppX1Stats *stats) const
{
	if (!stats)
		return {};

	const auto awb = stats->block<StatsType::WbmeasPost>();
	if (!awb)
		return {};

	if (awb->cnt == 0) {
		LOG(RppX1Awb, Debug) << "AWB statistics are empty";
		return {};
	}

	Vector<double, 3> rgbMeans;

	/* Get the YCbCr mean values: we use YCbCr measurement mode. */
	Vector<double, 3> yuvMeans({
		static_cast<double>(awb->mean_y_or_g),
		static_cast<double>(awb->mean_cb_or_b),
		static_cast<double>(awb->mean_cr_or_r)
	});

	/*
	 * Convert from YCbCr to RGB. The statistics engine has been
	 * programmed with the following matrix:
	 *
	 * Y  =  16 + 0.2500 R + 0.5000 G + 0.1094 B
	 * Cb = 128 - 0.1406 R - 0.2969 G + 0.4375 B
	 * Cr = 128 + 0.4375 R - 0.3750 G - 0.0625 B
	 *
	 * Use the inverse matrix here.
	 */
	static const Matrix<double, 3, 3> yuv2rgbMatrix({
		1.1636, -0.0623,  1.6008,
		1.1636, -0.4045, -0.7949,
		1.1636,  1.9912, -0.0250
	});
	static const Vector<double, 3> yuv2rgbOffset({
		16, 128, 128
	});

	rgbMeans = yuv2rgbMatrix * (yuvMeans - yuv2rgbOffset);

	/*
	 * Due to hardware rounding errors in the YCbCr means, the
	 * calculated RGB means may be negative. This would lead to
	 * negative gains, messing up calculation. Prevent this by
	 * clamping the means to positive values.
	 */
	rgbMeans = rgbMeans.max(0.0);

	/*
	 * \todo
	 * The ISP computes the AWB means after applying the CCM. Apply
	 * the inverse as we want to get the raw means before the colour gains.
	 * rgbMeans = frameContext.ccm.ccm.inverse() * rgbMeans;
	 */

	/*
	 * The ISP computes the AWB means after applying the colour gains,
	 * divide by the gains that were used to get the raw means from the
	 * sensor. Apply a minimum value to avoid divisions by near-zero.
	 */
	rgbMeans /= frameContext.awb.gains.max(0.01);

	return RppX1AwbStats(rgbMeans);
}

REGISTER_IPA_ALGORITHM(Awb, "Awb")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
