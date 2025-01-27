/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board
 *
 * RkISP1 Wide Dynamic Range control
 */

#include "wdr.h"

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

#include <libipa/histogram.h>
#include <libipa/pwl.h>

#include "linux/rkisp1-config.h"

/**
 * \file wdr.h
 */

namespace libcamera {

namespace ipa::rkisp1::algorithms {

/**
 * \class WideDynamicRange
 * \brief RkISP1 Wide Dynamic Range algorithm
 *
 * This algorithm implements automatic global tone mapping for the RkISP1.
 * Global tone mapping is done by the GWDR hardware block and applies
 * a global tone mapping curve to the image to increase the perceived dynamic
 * range. Imagine a indoor scene with bright outside visible through the windows.
 * With normal exposure settings, the windwos will be completely saturated and
 * no structure (sky/clouds) will be visible because the AEGC has to increase
 * overall exposure to reach a certain level of mean brightness. In WDR mode,
 * the algorithm will artifically reduce the exposure time so that the texture
 * and colours become visible in the former saturated areas. Then the global
 * tone mapping curve is applied to mitigate the loss of brightness.
 *
 * Calculating that tone mapping curve is the most difficult part. This
 * algorithm implements four tone mapping strategies:
 * - Linear: The tone mapping curve is a combination of two linear functions
 * with one kneepoint
 * - Power: The tone mapping curve follows a power function
 * - Exponential: The tone mapping curve follows an exponential function
 * - HistogramEqualization: The tone mapping curve tries to equalize the
 * histogram
 *
 * The overall strategy is the same in all cases: A negative exposure value is
 * applied to the AEGC regulation until the number of nearly saturated pixels go
 * below a given threshold (controllable via WdrMaxBrightPixels, default is 2%)
 * or the MinExposureValue specified in the tuning file is reached.
 *
 * The global tone mapping curve is then calculated so that it accounts for the
 * reduction of brightness due to the negative exposure value. As the result of
 * tone mapping is very difficult to quantize and as it is by definition a
 * lossy process there is not a single "correct" solution.
 *
 * The approach taken here is based on the sinmple linear model. The kneepoint
 * of the curve is calculated so that a 50% grey pixel in the normal exposed
 * image ends up at 50% grey in the WDR image again. So the first section of the
 * curve has a gain of pow(2, - WDR-EV) up to 50% level. The second part of the
 * curve compresses the remaining range. Using the WdrStrength control, the gain
 * of that initial section can be further adjusted.
 *
 * In the Power and Exponential modes, the curves are calculated so that they
 * pass through that kneepoint.
 *
 * The histogram equalization mode tries to equalize the histogram of the
 * image and acts independently of the calculated exposure value.
 *
 * \code{.unparsed}
 * algorithms:
 *   - WideDynamicRange:
 *       ExposureConstraint:
 *         MaxBrightPixels: 0.02
 *         yTarget: 0.95
 *       MinExposureValue: -4.0
 * \endcode
 */

LOG_DEFINE_CATEGORY(RkISP1Wdr)

static constexpr unsigned int kTonecurveXIntervals = RKISP1_CIF_ISP_WDR_CURVE_NUM_INTERV;

/*
 * Increasing interval sizes. The intervals are crafted so that they sum
 * up to 4096. This results in better fitting curves than the constant intervals
 * (all entries are 4)
 */
static constexpr std::array<int, kTonecurveXIntervals> kLoglikeIntervals = {
	{ 0, 0, 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4,
	  4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6 }
};

WideDynamicRange::WideDynamicRange()
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int WideDynamicRange::init([[maybe_unused]] IPAContext &context,
			   [[maybe_unused]] const YamlObject &tuningData)
{
	toneCurveIntervalValues_ = kLoglikeIntervals;

	/* Calculate a list of normed x values */
	toneCurveX_[0] = 0.0;
	int lastValue = 0;
	for (unsigned int i = 1; i < toneCurveX_.size(); i++) {
		lastValue += std::pow(2, toneCurveIntervalValues_[i - 1] + 3);
		lastValue = std::min(lastValue, 4096);
		toneCurveX_[i] = lastValue / 4096.0;
	}

	strength_ = 1.0;
	mode_ = controls::draft::WdrOff;
	exposureConstraintMaxBrightPixels_ = 0.02;
	exposureConstraintY_ = 0.95;
	minExposureValue_ = -4.0;

	const auto &constraint = tuningData["ExposureConstraint"];
	if (!constraint.isDictionary()) {
		LOG(RkISP1Wdr, Warning)
			<< "ExposureConstraint not found in tuning data."
			   "Using default values MaxBrightPixels: "
			<< exposureConstraintMaxBrightPixels_
			<< " yTarget: " << exposureConstraintY_;
	} else {
		exposureConstraintMaxBrightPixels_ =
			constraint["MaxBrightPixels"].get<double>().value_or(0.98);
		exposureConstraintY_ = constraint["yTarget"].get<double>().value_or(0.95);
	}

	const auto &minExp = tuningData["MinExposureValue"];
	minExposureValue_ = minExp.get<double>().value_or(minExposureValue_);
	if (!minExp) {
		LOG(RkISP1Wdr, Warning)
			<< "MinExposureValue not found in tuning data."
			   "Using default value "
			<< minExposureValue_;
	}

	context.ctrlMap[&controls::draft::WdrMode] =
		ControlInfo(controls::draft::WdrModeValues, controls::draft::WdrOff);
	context.ctrlMap[&controls::draft::WdrStrength] =
		ControlInfo(0.0f, 2.0f, static_cast<float>(strength_));
	context.ctrlMap[&controls::draft::WdrMaxBrightPixels] =
		ControlInfo(0.0f, 1.0f, static_cast<float>(exposureConstraintMaxBrightPixels_));

	applyCompensationLinear(1.0, 0.0);

	return 0;
}

void WideDynamicRange::applyHistogramEqualization(double strength)
{
	if (hist_.empty())
		return;

	strength *= 0.65;

	/*
	 * In a fully equalized histogram, all bins have the same value. Try
	 * to equalize the histogram by applying a gain or damping depending on
	 * the distance of the actual bin value from that norm.
	 */
	std::vector<double> gains;
	gains.resize(hist_.size());
	double sum = 0;
	double norm = 1.0 / (gains.size());
	for (unsigned i = 0; i < hist_.size(); i++) {
		double diff = 1.0 + strength * (hist_[i] - norm) / norm;
		/* Todo: fix for large intensity */
		//diff = std::max(diff, 0.0);
		gains[i] = diff;
		sum += diff;
	}
	/* Never amplify the last entry. */
	gains.back() = std::max(gains.back(), 1.0);

	double scale = gains.size() / sum;
	for (auto &v : gains)
		v *= scale;

	Pwl pwl;
	double step = 1.0 / gains.size();
	double lastX = 0;
	double lastY = 0;

	pwl.append(lastX, lastY);
	for (unsigned int i = 0; i < gains.size() - 1; i++) {
		lastY += gains[i] * step;
		lastX += step;
		pwl.append(lastX, lastY);
	}
	pwl.append(1.0, 1.0);

	for (unsigned int i = 0; i < toneCurveX_.size(); i++) {
		toneCurveY_[i] = pwl.eval(toneCurveX_[i]);
	}
}

Vector<double, 2> WideDynamicRange::kneePoint(double gain, double strength)
{
	gain = std::pow(gain, strength);
	double y = 0.5;
	double x = y / gain;
	//double x = 1 / (gain+1);
	//double y = gain*x;

	return { { x, y } };
}

void WideDynamicRange::applyCompensationLinear(double gain, double strength)
{
	auto kp = kneePoint(gain, strength);
	double g1 = kp[1] / kp[0];
	double g2 = (kp.y() - 1) / (kp.x() - 1);

	for (unsigned int i = 0; i < toneCurveX_.size(); i++) {
		double x = toneCurveX_[i];
		double y;
		if (x < kp.x()) {
			y = g1 * x;
		} else {
			y = g2 * x + 1 - g2;
		}
		toneCurveY_[i] = y;
	}
}

void WideDynamicRange::applyCompensationPower(double gain, double strength)
{
	double e = 1.0;
	if (strength > 1e-6) {
		auto kp = kneePoint(gain, strength);
		/* Calculate an exponent to go through the knee point. */
		e = log(kp[1]) / log(kp[0]);
	}

	for (unsigned int i = 0; i < toneCurveX_.size(); i++) {
		toneCurveY_[i] = std::pow(toneCurveX_[i], e);
	}
}

void WideDynamicRange::applyCompensationExponential(double gain, double strength)
{
	double k = 0.1;
	auto kp = kneePoint(gain, strength);
	double kx = kp[0];
	double ky = kp[1];

	if (kx > ky) {
		LOG(RkISP1Wdr, Warning) << "Invalid knee point: " << kp;
		kx = ky;
	}

	/*
	 * The exponential curve is based on the function proposed by Glozman
	 * et al. in
	 * S. Glozman, T. Kats, and O. Yadid-Pecht, "Exponent Operator Based
	 * Tone Mapping Algorithm for Color Wide Dynamic Range Images," 2011.
	 *
	 * That function uses a k factor as parameter for the WDR compression
	 * curve:
	 * k=0: maximum compression
	 * k=infinity: linear curve
	 *
	 * To calculate a k factor that results in a curve that passes through
	 * the kneepoint, the equation needs to be solved for k after inserting
	 * the kneepoint.  This can be formulated as search for a zero point.
	 * Unfortunately there is no closed solution for that transformation.
	 * Using newton's method to approximate the value is numerically
	 * unstable.
	 *
	 * Luckily the function only crosses the x axis once and for the set of
	 * possible kneepoints, a negative and a positive point can be guessed.
	 * The approximation is then implemented using bisection.
	 */
	if (std::abs(kx - ky) < 0.001) {
		k = 1e8;
	} else {
		double kl = 0.0001;
		double kh = 1000;

		auto fk = [=](double v) {
			return exp(-kx / v) - ky * exp(-1.0 / v) + ky - 1.0;
		};

		ASSERT(fk(kl) < 0);
		ASSERT(fk(kh) > 0);

		k = kh / 10.0;
		while (fk(k) > 0) {
			kh = k;
			k /= 10.0;
		}

		do {
			k = (kl + kh) / 2;
			if (fk(k) < 0)
				kl = k;
			else
				kh = k;
		} while (std::abs(kh - kl) > 1e-3);
	}

	double a = 1.0 / (1.0 - std::exp(-1.0 / k));
	for (unsigned int i = 0; i < toneCurveX_.size(); i++) {
		toneCurveY_[i] = a * (1.0 - std::exp(-toneCurveX_[i] / k));
	}
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void WideDynamicRange::queueRequest([[maybe_unused]] IPAContext &context,
				    [[maybe_unused]] const uint32_t frame,
				    IPAFrameContext &frameContext,
				    const ControlList &controls)
{
	strength_ = controls.get(controls::draft::WdrStrength).value_or(strength_);
	exposureConstraintMaxBrightPixels_ =
		controls.get(controls::draft::WdrMaxBrightPixels)
			.value_or(exposureConstraintMaxBrightPixels_);

	const auto &mode = controls.get(controls::draft::WdrMode);
	if (mode) {
		mode_ = static_cast<controls::draft::WdrModeEnum>(*mode);
	}

	frameContext.wdr.mode = mode_;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void WideDynamicRange::prepare(IPAContext &context,
			       [[maybe_unused]] const uint32_t frame,
			       IPAFrameContext &frameContext,
			       RkISP1Params *params)
{
	if (!params) {
		LOG(RkISP1Wdr, Warning) << "Params is null";
		return;
	}

	auto config = params->block<BlockType::Wdr>();
	if (!config) {
		LOG(RkISP1Wdr, Warning) << "Wdr block is invalid";
		return;
	}

	auto mode = frameContext.wdr.mode;

	config.setEnabled(mode != controls::draft::WdrOff);

	double comp = 0;

	/*
	 * Todo: This overwrites frameContext.agc.exposureValue so that
	 * in the next call to Agc::process() that exposureValue get's
	 * applied. In the future it is planned to move the exposure
	 * calculations from Agc::process() to Agc::prepare(). In this
	 * case, we need to ensure that this code get's called early enough.
	 */
	if (mode != controls::draft::WdrOff) {
		frameContext.wdr.wdrExposureValue = context.activeState.wdr.exposureValue;
		frameContext.wdr.agcExposureValue = frameContext.agc.exposureValue;

		/*
		 * When WDR is enabled, the maxBrightPixels constraint is always
		 * active. This is problematic when the user sets a positive
		 * exposureValue. As this would mean that the negative WDR
		 * exposure value and the user provided positive value should
		 * cancel each other out. But as the regulation is not absolute
		 * and only dependent on the measured changes in the histogram,
		 * reducing the wdr EV would only lead to an even stronger pull
		 * from regulation. Positive user supplied EVs must therefore be
		 * handled using the wdr curve.
		 */
		if (frameContext.wdr.wdrExposureValue < 0) {
			frameContext.agc.exposureValue =
				std::min(frameContext.agc.exposureValue,
					 frameContext.wdr.wdrExposureValue);
			comp = frameContext.agc.exposureValue - frameContext.wdr.agcExposureValue;
		}
	}

	/* Calculate how much EV we need to compensate with the WDR curve. */
	double gain = pow(2.0, -comp);

	if (mode == controls::draft::WdrOff) {
		applyCompensationLinear(1.0, 0.0);
	} else if (mode == controls::draft::WdrLinear) {
		applyCompensationLinear(gain, strength_);
	} else if (mode == controls::draft::WdrPower) {
		applyCompensationPower(gain, strength_);
	} else if (mode == controls::draft::WdrExponential) {
		applyCompensationExponential(gain, strength_);
	} else if (mode == controls::draft::WdrHistogramEqualization) {
		applyHistogramEqualization(strength_);
	}

	//reset value
	config->dmin_strength = 0x10;
	config->dmin_thresh = 0;

	for (unsigned int i = 0; i < kTonecurveXIntervals; i++) {
		int v = toneCurveIntervalValues_[i];
		config->tone_curve.dY[i / 8] |= (v & 0x07) << ((i % 8) * 4);
	}

	std::vector<Point> debugCurve;

	/*
	 * Fix the curve to adhere to the hardware constraints. Don't apply a
	 * constraint on the first element, which is most likely zero anyways.
	 */
	int lastY = toneCurveY_[0] * 4096.0;
	for (unsigned int i = 0; i < toneCurveX_.size(); i++) {
		int diff = static_cast<int>(toneCurveY_[i] * 4096.0) - lastY;
		diff = std::clamp(diff, -2048, 2048);
		lastY = lastY + diff;
		config->tone_curve.ym[i] = lastY;
		debugCurve.push_back({ static_cast<int>(toneCurveX_[i] * 4096.0),
				       lastY });
	}

	context.debugMetadata.set<Span<const Point>>(controls::debug::WdrCurve, debugCurve);
}

void WideDynamicRange::process(IPAContext &context, const uint32_t frame,
			       IPAFrameContext &frameContext,
			       const rkisp1_stat_buffer *stats,
			       ControlList &metadata)
{
	if (!stats || !(stats->meas_type & RKISP1_CIF_ISP_STAT_HIST)) {
		LOG(RkISP1Wdr, Error) << "No histogram data in statistics";
		return;
	}

	if (frame == 0) {
		pid_.setStandardParameters(1, 5.0, 3.0);
		pid_.setOutputLimits(minExposureValue_, 0.0);
		context.activeState.wdr.exposureValue = 0.0;
	}

	pid_.setTarget(exposureConstraintY_);

	const rkisp1_cif_isp_stat *params = &stats->params;
	auto mode = frameContext.wdr.mode;

	metadata.set(controls::draft::WdrMode, mode);

	Histogram cumHist({ params->hist.hist_bins, context.hw->numHistogramBins },
			  [](uint32_t x) { return x >> 4; });

	std::vector<double> hist;
	double sum = 0;
	for (unsigned i = 0; i < context.hw->numHistogramBins; i++) {
		double v = params->hist.hist_bins[i] >> 4;
		hist.push_back(v);
		sum += v;
	}

	/* Scale so that the entries sum up to 1. */
	double scale = 1.0 / sum;
	for (auto &v : hist)
		v *= scale;
	hist_.swap(hist);

	double mean = cumHist.quantile(1.0 - exposureConstraintMaxBrightPixels_) /
		      cumHist.bins();
	LOG(RkISP1Wdr, Debug) << "Mean y of bright pixels: " << mean;

	if (mode == controls::draft::WdrOff) {
		metadata.set(controls::draft::WdrExposureValue, 0);
		return;
	}

	context.activeState.wdr.exposureValue = pid_.process(mean);
	LOG(RkISP1Wdr, Debug) << "Active state WDR ev: " << context.activeState.wdr.exposureValue;
	metadata.set(controls::draft::WdrExposureValue, frameContext.wdr.wdrExposureValue);

	/*
	 * We overwrote agc exposure value in prepare() to create an
	 * underexposure for WDR. Report the original exposure value in metadata.
	 */
	metadata.set(controls::ExposureValue, frameContext.wdr.agcExposureValue);
}

REGISTER_IPA_ALGORITHM(WideDynamicRange, "WideDynamicRange")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
