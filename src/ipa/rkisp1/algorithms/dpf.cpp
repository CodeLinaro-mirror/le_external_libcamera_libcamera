/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Denoise Pre-Filter control
 */

#include "dpf.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "linux/rkisp1-config.h"

/**
 * \file dpf.h
 */

namespace libcamera {

namespace ipa::rkisp1::algorithms {

/**
 * \class Dpf
 * \brief RkISP1 Denoise Pre-Filter control
 *
 * The denoise pre-filter algorithm is a bilateral filter which combines a
 * range filter and a domain filter. The denoise pre-filter is applied before
 * demosaicing.
 */

LOG_DEFINE_CATEGORY(RkISP1Dpf)

Dpf::Dpf()
	: config_({}), strengthConfig_({}), baseConfig_({}), baseStrengthConfig_({})
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Dpf::init([[maybe_unused]] IPAContext &context,
	      const YamlObject &tuningData)
{
	/* Parse tuning block */
	if (!parseConfig(tuningData))
		return -EINVAL;

	/* Log parsed base tuning (counts are always full-sized for base). */
	LOG(RkISP1Dpf, Info)
		<< "DPF init: base tuning parsed, G coeffs="
		<< RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS
		<< ", RB fltsize="
		<< (config_.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9 ? "13x9" : "9x9")
		<< ", NLL coeffs=" << RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS
		<< ", NLL scale="
		<< (config_.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC ? "log" : "linear")
		<< ", Strength (r,g,b)="
		<< (int)strengthConfig_.r << "," << (int)strengthConfig_.g
		<< "," << (int)strengthConfig_.b;

	/* Preserve base (non-exposure index) YAML configuration for restoration after manual mode. */
	baseConfig_ = config_;
	baseStrengthConfig_ = strengthConfig_;

	/* Optional exposure index-banded tuning */
	if (useExposureIndexLevels_) {
		LOG(RkISP1Dpf, Info)
			<< "DPF init: loaded " << exposureIndexLevels_.size()
			<< " exposureIndex level(s) from tuning";
	}

	/* Optional mode tuning */
	if (!modes_.empty()) {
		LOG(RkISP1Dpf, Info)
			<< "DPF init: loaded " << modes_.size()
			<< " mode(s) from tuning";
	}

	return 0;
}

bool Dpf::parseConfig(const YamlObject &tuningData)
{
	/* Parse base config */
	if (!parseSingleConfig(tuningData, config_, strengthConfig_))
		return false;

	baseConfig_ = config_;
	baseStrengthConfig_ = strengthConfig_;

	/* Optional developer mode flag (default true). If false, only basic manual controls available. */
	bool devMode = tuningData["devmode"].get<bool>().value_or(true);
	setDevMode(devMode);

	/* Parse exposure index levels */
	if (tuningData.contains("ExposureIndexLevels")) {
		useExposureIndexLevels_ = true;
		exposureIndexLevels_.clear();
		for (const auto &entry : tuningData["ExposureIndexLevels"].asList()) {
			std::optional<uint32_t> maxExposureIndexOpt =
				entry["maxExposureIndex"].get<uint32_t>();
			if (!maxExposureIndexOpt) {
				LOG(RkISP1Dpf, Error) << "ExposureIndexLevels entry missing maxExposureIndex";
				continue;
			}
			ExposureIndexLevelConfig lvl{};
			lvl.maxExposureIndex = *maxExposureIndexOpt;
			if (!parseSingleConfig(entry, lvl.dpf, lvl.strength))
				continue;
			exposureIndexLevels_.push_back(lvl);
		}
		std::sort(exposureIndexLevels_.begin(), exposureIndexLevels_.end(),
			  [](const ExposureIndexLevelConfig &a, const ExposureIndexLevelConfig &b) {
				  return a.maxExposureIndex < b.maxExposureIndex;
			  });
	}

	/* Parse modes */
	if (tuningData.contains("NoiseReductionMode")) {
		modes_.clear();
		for (const auto &entry : tuningData["NoiseReductionMode"].asList()) {
			std::optional<std::string> typeOpt =
				entry["type"].get<std::string>();
			if (!typeOpt) {
				LOG(RkISP1Dpf, Error) << "Modes entry missing type";
				continue;
			}

			int32_t modeValue;
			if (*typeOpt == "minimal") {
				modeValue = controls::draft::NoiseReductionModeMinimal;
			} else if (*typeOpt == "highquality") {
				modeValue = controls::draft::NoiseReductionModeHighQuality;
			} else if (*typeOpt == "fast") {
				modeValue = controls::draft::NoiseReductionModeFast;
			} else if (*typeOpt == "zsl") {
				modeValue = controls::draft::NoiseReductionModeZSL;
			} else {
				LOG(RkISP1Dpf, Error) << "Unknown mode type: " << *typeOpt;
				continue;
			}

			ModeConfig mode{};
			mode.modeValue = modeValue;
			if (!parseSingleConfig(entry, mode.dpf, mode.strength))
				continue;
			modes_.push_back(mode);
		}
	}

	return true;
}

bool Dpf::parseSingleConfig(const YamlObject &tuningData,
			    rkisp1_cif_isp_dpf_config &config,
			    rkisp1_cif_isp_dpf_strength_config &strengthConfig)
{
	std::vector<uint8_t> values;
	/*
	 * The domain kernel is configured with a 9x9 kernel for the green
	 * pixels, and a 13x9 or 9x9 kernel for red and blue pixels.
	 */
	if (!tuningData.contains("DomainFilter")) {
		LOG(RkISP1Dpf, Error) << "DomainFilter section missing";
		return false;
	}
	const YamlObject &dFObject = tuningData["DomainFilter"];
	/*
	 * For the green component, we have the 9x9 kernel specified
	 * as 6 coefficients:
	 *    Y
	 *    ^
	 *  4 | 6   5   4   5   6
	 *  3 |   5   3   3   5
	 *  2 | 5   3   2   3   5
	 *  1 |   3   1   1   3
	 *  0 - 4   2   0   2   4
	 * -1 |   3   1   1   3
	 * -2 | 5   3   2   3   5
	 * -3 |   5   3   3   5
	 * -4 | 6   5   4   5   6
	 *    +---------|--------> X
	 *     -4....-1 0 1 2 3 4
	 */
	values = dFObject["g"].getList<uint8_t>().value_or(std::vector<uint8_t>{});
	if (values.size() != RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS) {
		LOG(RkISP1Dpf, Error)
			<< "Invalid 'DomainFilter:g': expected "
			<< RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS
			<< " elements, got " << values.size();
		return false;
	}

	std::copy_n(values.begin(), values.size(),
		    std::begin(config.g_flt.spatial_coeff));

	config.g_flt.gr_enable = true;
	config.g_flt.gb_enable = true;

	/*
	 * For the red and blue components, we have the 13x9 kernel specified
	 * as 6 coefficients:
	 *
	 *    Y
	 *    ^
	 *  4 | 6   5   4   3   4   5   6
	 *    |
	 *  2 | 5   4   2   1   2   4   5
	 *    |
	 *  0 - 5   3   1   0   1   3   5
	 *    |
	 * -2 | 5   4   2   1   2   4   5
	 *    |
	 * -4 | 6   5   4   3   4   5   6
	 *    +-------------|------------> X
	 *     -6  -4  -2   0   2   4   6
	 *
	 * For a 9x9 kernel, columns -6 and 6 are dropped, so coefficient
	 * number 6 is not used.
	 */
	values = dFObject["rb"].getList<uint8_t>().value_or(std::vector<uint8_t>{});
	if (values.size() != RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS &&
	    values.size() != RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS - 1) {
		LOG(RkISP1Dpf, Error)
			<< "Invalid 'DomainFilter:rb': expected "
			<< RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS - 1
			<< " or " << RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS
			<< " elements, got " << values.size();
		return false;
	}

	config.rb_flt.fltsize = values.size() == RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS
					? RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9
					: RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9;

	std::copy_n(values.begin(), values.size(),
		    std::begin(config.rb_flt.spatial_coeff));

	config.rb_flt.r_enable = true;
	config.rb_flt.b_enable = true;

	/*
	 * The range kernel is configured with a noise level lookup table (NLL)
	 * which stores a piecewise linear function that characterizes the
	 * sensor noise profile as a noise level function curve (NLF).
	 */
	if (!tuningData.contains("NoiseLevelFunction")) {
		LOG(RkISP1Dpf, Error) << "NoiseLevelFunction section missing";
		return false;
	}
	const YamlObject &rFObject = tuningData["NoiseLevelFunction"];

	std::vector<uint16_t> nllValues;
	nllValues = rFObject["coeff"].getList<uint16_t>().value_or(std::vector<uint16_t>{});
	if (nllValues.size() != RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS) {
		LOG(RkISP1Dpf, Error)
			<< "Invalid 'RangeFilter:coeff': expected "
			<< RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS
			<< " elements, got " << nllValues.size();
		return false;
	}

	std::copy_n(nllValues.begin(), nllValues.size(),
		    std::begin(config.nll.coeff));

	std::string scaleMode = rFObject["scale-mode"].get<std::string>("");
	if (scaleMode == "linear") {
		config.nll.scale_mode = RKISP1_CIF_ISP_NLL_SCALE_LINEAR;
	} else if (scaleMode == "logarithmic") {
		config.nll.scale_mode = RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC;
	} else {
		LOG(RkISP1Dpf, Error)
			<< "Invalid 'RangeFilter:scale-mode': expected "
			<< "'linear' or 'logarithmic' value, got "
			<< scaleMode;
		return false;
	}

	if (!tuningData.contains("Gain")) {
		LOG(RkISP1Dpf, Error) << "Gain section missing";
		return false;
	}
	const YamlObject &gObject = tuningData["Gain"];

	config.gain.mode =
		gObject["gain_mode"].get<uint32_t>().value_or(
			RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS);
	config.gain.nf_r_gain = gObject["nf_r_gain"].get<uint16_t>().value_or(256);
	config.gain.nf_b_gain = gObject["nf_b_gain"].get<uint16_t>().value_or(256);
	config.gain.nf_gr_gain = gObject["nf_gr_gain"].get<uint16_t>().value_or(256);
	config.gain.nf_gb_gain = gObject["nf_gb_gain"].get<uint16_t>().value_or(256);

	if (!tuningData.contains("FilterStrength")) {
		LOG(RkISP1Dpf, Error) << "FilterStrength section missing";
		return false;
	}
	const YamlObject &fSObject = tuningData["FilterStrength"];

	strengthConfig.r = fSObject["r"].get<uint8_t>().value_or(64);
	strengthConfig.g = fSObject["g"].get<uint8_t>().value_or(64);
	strengthConfig.b = fSObject["b"].get<uint8_t>().value_or(64);
	return true;
}

void Dpf::handleReductionModeControl(const ControlList &controls,
				     IPAFrameContext &frameContext,
				     IPAContext &context,
				     [[maybe_unused]] uint32_t frame)
{
	auto &dpf = context.activeState.dpf;
	const auto &denoise = controls.get(controls::draft::NoiseReductionMode);
	if (!denoise) {
		frameContext.dpf.denoise = dpf.denoise;
		return;
	}
	LOG(RkISP1Dpf, Debug) << "Set denoise mode to " << *denoise;

	const auto requestedMode = static_cast<int32_t>(*denoise);
	if (requestedMode == currentReductionMode_) {
		frameContext.dpf.denoise = dpf.denoise;
		return;
	}

	currentReductionMode_ = requestedMode;
	if (requestedMode == controls::draft::NoiseReductionModeOff) {
		dpf.denoise = false;
		frameContext.dpf.denoise = false;
		return;
	}

	dpf.denoise = true;
	frameContext.dpf.denoise = true;
	frameContext.dpf.update = true;
}
void Dpf::loadReductionModeConfig(IPAFrameContext &frameContext)
{
	/* Find mode config */
	auto it = std::find_if(modes_.begin(), modes_.end(),
			       [this](const ModeConfig &mode) {
				       return mode.modeValue == currentReductionMode_;
			       });
	if (it == modes_.end()) {
		LOG(RkISP1Dpf, Warning)
			<< "No DPF config for reduction mode "
			<< static_cast<int>(currentReductionMode_);
		return;
	}

	/* Apply mode config */
	config_ = it->dpf;
	strengthConfig_ = it->strength;
	frameContext.dpf.update = true;
}

void Dpf::collectManualOverrides(const ControlList &controls)
{
	if (const auto &c = controls.get(controls::rkisp1::DpfChannelStrengths); c) {
		if (c->size() == 3) {
			overrides_.strength = DpfStrengthSettings{
				static_cast<uint16_t>((*c)[0]),
				static_cast<uint16_t>((*c)[1]),
				static_cast<uint16_t>((*c)[2])
			};
		}
	}
	if (!isDevMode())
		return;

	if (const auto &c = controls.get(controls::rkisp1::DpfGreenSpatialCoefficients); c) {
		if (c->size() == RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS) {
			DpfSpatialGreenSettings green;
			std::copy_n(c->begin(), RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS,
				    green.coeffs.begin());
			overrides_.spatialGreen = green;
		}
	}
	if (const auto &c =
		    controls.get(controls::rkisp1::DpfRedBlueSpatialCoefficients);
	    c) {
		if (c->size() == RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS) {
			DpfSpatialRbSettings rb;
			std::copy_n(c->begin(), RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS,
				    rb.coeffs.begin());
			rb.size = (config_.rb_flt.fltsize ==
				   RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9)
					  ? 1
					  : 0;
			overrides_.spatialRb = rb;
		}
	}
	if (const auto &c = controls.get(controls::rkisp1::DpfRbFilterSize); c) {
		overrides_.rbSize = *c ? 1 : 0;
	}
	if (const auto &c = controls.get(controls::rkisp1::DpfNoiseLevelLookupCoefficients); c) {
		if (c->size() == RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS) {
			DpfNllSettings nll;
			std::copy_n(c->begin(), RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS,
				    nll.coeffs.begin());
			nll.scaleMode = (config_.nll.scale_mode ==
					 RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC)
						? 1
						: 0;
			overrides_.nll = nll;
		}
	}
	if (const auto &c = controls.get(controls::rkisp1::DpfNoiseLevelLookupScaleMode); c) {
		if (overrides_.nll) {
			overrides_.nll->scaleMode = *c ? 1 : 0;
		} else {
			DpfNllSettings nll;
			std::copy_n(std::begin(config_.nll.coeff),
				    RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS,
				    nll.coeffs.begin());
			nll.scaleMode = *c ? 1 : 0;
			overrides_.nll = nll;
		}
	}
}

bool Dpf::checkOverridesChanged()
{
	/* Check strength always (not dev-mode specific) */
	if (overrides_.strength) {
		if (overrides_.strength->r != strengthConfig_.r ||
		    overrides_.strength->g != strengthConfig_.g ||
		    overrides_.strength->b != strengthConfig_.b) {
			return true;
		}
	}

	if (!isDevMode())
		return false;

	if (overrides_.spatialGreen &&
	    !std::equal(overrides_.spatialGreen->coeffs.begin(), overrides_.spatialGreen->coeffs.end(),
			config_.g_flt.spatial_coeff)) {
		return true;
	}
	if (overrides_.spatialRb &&
	    !std::equal(overrides_.spatialRb->coeffs.begin(), overrides_.spatialRb->coeffs.end(),
			config_.rb_flt.spatial_coeff)) {
		return true;
	}
	if (overrides_.rbSize) {
		bool currentRbSize =
			(config_.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9)
				? 1
				: 0;
		if (*overrides_.rbSize != currentRbSize) {
			return true;
		}
	}
	if (overrides_.nll) {
		bool coeffsChanged =
			!std::equal(overrides_.nll->coeffs.begin(),
				    overrides_.nll->coeffs.end(),
				    config_.nll.coeff);
		bool scaleChanged =
			overrides_.nll->scaleMode !=
			(config_.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC
				 ? 1
				 : 0);
		if (coeffsChanged || scaleChanged) {
			return true;
		}
	}
	return false;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Dpf::queueRequest(IPAContext &context,
		       [[maybe_unused]] const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	frameContext.dpf.update = false;
	auto currentRunnungMode = getRunningMode();
	handleReductionModeControl(controls, frameContext, context, frame);

	if (currentRunnungMode == controls::rkisp1::DenoiseModeReduction && currentReductionMode_ != controls::draft::NoiseReductionModeOff) {
		loadReductionModeConfig(frameContext);
		return;
	}
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Dpf::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, RkISP1Params *params)
{
	if (!frameContext.dpf.update && frame > 0)
		return;

	auto config = params->block<BlockType::Dpf>();
	config.setEnabled(frameContext.dpf.denoise);

	auto strengthConfig = params->block<BlockType::DpfStrength>();
	strengthConfig.setEnabled(frameContext.dpf.denoise);

	if (frameContext.dpf.denoise) {
		*config = config_;
		*strengthConfig = strengthConfig_;

		const auto &awb = context.configuration.awb;
		const auto &lsc = context.configuration.lsc;

		auto &mode = config->gain.mode;

		/*
		 * The DPF needs to take into account the total amount of
		 * digital gain, which comes from the AWB and LSC modules. The
		 * DPF hardware can be programmed with a digital gain value
		 * manually, but can also use the gains supplied by the AWB and
		 * LSC modules automatically when they are enabled. Use that
		 * mode of operation as it simplifies control of the DPF.
		 */
		if (awb.enabled && lsc.enabled)
			mode = RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS;
		else if (awb.enabled)
			mode = RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS;
		else if (lsc.enabled)
			mode = RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS;
		else
			mode = RKISP1_CIF_ISP_DPF_GAIN_USAGE_DISABLED;
	}
}

REGISTER_IPA_ALGORITHM(Dpf, "Dpf")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
