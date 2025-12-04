/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Denoise Pre-Filter control
 */

#include "dpf.h"

#include <algorithm>
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
	: config_({}), strengthConfig_({}),
	  runningMode_(controls::draft::NoiseReductionModeOff)
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Dpf::init([[maybe_unused]] IPAContext &context,
	      const YamlObject &tuningData)
{
	/* Parse tuning block. */
	if (!parseConfig(tuningData)) {
		return -EINVAL;
	}

	return 0;
}
bool Dpf::parseConfig(const YamlObject &tuningData)
{
	/* Parse base config. */
	if (!parseSingleConfig(tuningData, config_, strengthConfig_)) {
		return false;
	}
	if (!parseModes(tuningData)) {
		return false;
	}
	return true;
}

bool Dpf::parseModes(const YamlObject &tuningData)
{
	/* Parse noise reduction modes. */
	if (!tuningData.contains("modes")) {
		return true;
	}

	noiseReductionModes_.clear();
	for (const auto &entry : tuningData["modes"].asList()) {
		std::optional<std::string> typeOpt =
			entry["type"].get<std::string>();
		if (!typeOpt) {
			LOG(RkISP1Dpf, Error) << "Modes entry missing type";
			return false;
		}

		int32_t modeValue = controls::draft::NoiseReductionModeOff;
		if (*typeOpt == "minimal") {
			modeValue = controls::draft::NoiseReductionModeMinimal;
		} else if (*typeOpt == "fast") {
			modeValue = controls::draft::NoiseReductionModeFast;
		} else if (*typeOpt == "highquality") {
			modeValue = controls::draft::NoiseReductionModeHighQuality;
		} else if (*typeOpt == "zsl") {
			modeValue = controls::draft::NoiseReductionModeZSL;
		} else {
			LOG(RkISP1Dpf, Error) << "Unknown mode type: " << *typeOpt;
			return false;
		}

		ModeConfig mode{};
		mode.modeValue = modeValue;
		if (!parseSingleConfig(entry, mode.dpf, mode.strength)) {
			return false;
		}
		noiseReductionModes_.push_back(mode);
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
	const YamlObject &dFObject = tuningData["filter"];
	if (!dFObject) {
		LOG(RkISP1Dpf, Error) << "filter section missing";
		return false;
	}

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
			<< "Invalid 'filter:g': expected "
			<< RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS
			<< " elements, got " << values.size();
		return false;
	}

	std::copy_n(values.begin(), values.size(),
		    std::begin(config_.g_flt.spatial_coeff));

	config_.g_flt.gr_enable = true;
	config_.g_flt.gb_enable = true;

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
			<< "Invalid 'filter:rb': expected "
			<< RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS - 1
			<< " or " << RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS
			<< " elements, got " << values.size();
		return false;
	}

	config_.rb_flt.fltsize = values.size() == RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS
					 ? RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9
					 : RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9;

	std::copy_n(values.begin(), values.size(),
		    std::begin(config_.rb_flt.spatial_coeff));

	config_.rb_flt.r_enable = true;
	config_.rb_flt.b_enable = true;

	/*
	 * The range kernel is configured with a noise level lookup table (NLL)
	 * which stores a piecewise linear function that characterizes the
	 * sensor noise profile as a noise level function curve (NLF).
	 */
	const YamlObject &rFObject = tuningData["nll"];
	if (!rFObject) {
		LOG(RkISP1Dpf, Error) << "nll section missing";
		return false;
	}

	const auto nllValues =
		rFObject["coeff"].getList<uint16_t>().value_or(std::vector<uint16_t>{});
	if (nllValues.size() != RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS) {
		LOG(RkISP1Dpf, Error)
			<< "Invalid 'nll:coeff': expected "
			<< RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS
			<< " elements, got " << nllValues.size();
		return false;
	}

	std::copy_n(nllValues.begin(), nllValues.size(),
		    std::begin(config_.nll.coeff));

	const auto scaleMode = rFObject["scale-mode"].get<std::string>("");
	if (scaleMode == "linear") {
		config_.nll.scale_mode = RKISP1_CIF_ISP_NLL_SCALE_LINEAR;
	} else if (scaleMode == "logarithmic") {
		config_.nll.scale_mode = RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC;
	} else {
		LOG(RkISP1Dpf, Error)
			<< "Invalid 'nll:scale-mode': expected "
			<< "'linear' or 'logarithmic' value, got "
			<< scaleMode;
		return false;
	}

	const YamlObject &gObject = tuningData["gain"];
	if (!gObject) {
		LOG(RkISP1Dpf, Error) << "gain section missing";
		return false;
	}

	config.gain.mode =
		gObject["gain_mode"].get<uint32_t>().value_or(
			RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS);
	config.gain.nf_r_gain = gObject["r"].get<uint16_t>().value_or(256);
	config.gain.nf_b_gain = gObject["b"].get<uint16_t>().value_or(256);
	config.gain.nf_gr_gain = gObject["gr"].get<uint16_t>().value_or(256);
	config.gain.nf_gb_gain = gObject["gb"].get<uint16_t>().value_or(256);

	const YamlObject &fSObject = tuningData["strength"];
	if (!fSObject) {
		LOG(RkISP1Dpf, Error) << "strength section missing";
		return false;
	}

	strengthConfig.r = fSObject["r"].get<uint8_t>().value_or(64);
	strengthConfig.g = fSObject["g"].get<uint8_t>().value_or(64);
	strengthConfig.b = fSObject["b"].get<uint8_t>().value_or(64);
	return true;
}

bool Dpf::loadReductionConfig(int32_t mode)
{
	auto it = std::find_if(noiseReductionModes_.begin(), noiseReductionModes_.end(),
			       [mode](const ModeConfig &m) {
				       return m.modeValue == mode;
			       });
	if (it == noiseReductionModes_.end()) {
		LOG(RkISP1Dpf, Warning)
			<< "No DPF config for reduction mode "
			<< static_cast<int>(mode);
		return false;
	}

	config_ = it->dpf;
	strengthConfig_ = it->strength;

	LOG(RkISP1Dpf, Info)
		<< "DPF mode=Reduction (config loaded)"
		<< " mode=" << static_cast<int>(mode);

	return true;
}
/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Dpf::queueRequest(IPAContext &context,
		       [[maybe_unused]] const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	auto &dpf = context.activeState.dpf;
	bool update = false;

	const auto &denoise = controls.get(controls::draft::NoiseReductionMode);
	if (!denoise) {
		return;
	}
	runningMode_ = *denoise;
	switch (runningMode_) {
	case controls::draft::NoiseReductionModeOff:
		if (dpf.denoise) {
			dpf.denoise = false;
			update = true;
		}
		break;
	case controls::draft::NoiseReductionModeMinimal:
	case controls::draft::NoiseReductionModeHighQuality:
	case controls::draft::NoiseReductionModeFast:
	case controls::draft::NoiseReductionModeZSL:
		if (loadReductionConfig(runningMode_)) {
			update = true;
			dpf.denoise = true;
		} else {
			dpf.denoise = false;
			update = true;
		}
		break;
	default:
		LOG(RkISP1Dpf, Error)
			<< "Unsupported denoise value "
			<< *denoise;
		break;
	}

	frameContext.dpf.denoise = dpf.denoise;
	frameContext.dpf.update = update;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Dpf::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, RkISP1Params *params)
{
	if (!frameContext.dpf.update && frame > 0) {
		return;
	}

	if (!frameContext.dpf.denoise) {
		prepareDisabledMode(context, frame, frameContext, params);
		return;
	}

	prepareEnabledMode(context, frame, frameContext, params);
}

void Dpf::prepareDisabledMode([[maybe_unused]] IPAContext &context,
			      [[maybe_unused]] const uint32_t frame,
			      [[maybe_unused]] IPAFrameContext &frameContext,
			      RkISP1Params *params)
{
	frameContext.dpf.denoise = false;
	auto dpfConfig = params->block<BlockType::Dpf>();
	dpfConfig.setEnabled(false);
	auto dpfStrength = params->block<BlockType::DpfStrength>();
	dpfStrength.setEnabled(false);
}

void Dpf::prepareEnabledMode(IPAContext &context, [[maybe_unused]] const uint32_t frame,
			     [[maybe_unused]] IPAFrameContext &frameContext, RkISP1Params *params)
{
	auto dpfConfig = params->block<BlockType::Dpf>();
	dpfConfig.setEnabled(true);
	*dpfConfig = config_;

	/*
	 * The DPF needs to take into account the total amount of
	 * digital gain, which comes from the AWB and LSC modules. The
	 * DPF hardware can be programmed with a digital gain value
	 * manually, but can also use the gains supplied by the AWB and
	 * LSC modules automatically when they are enabled. Use that
	 * mode of operation as it simplifies control of the DPF.
	 */
	const auto &awb = context.configuration.awb;
	const auto &lsc = context.configuration.lsc;
	auto &mode = dpfConfig->gain.mode;

	if (awb.enabled && lsc.enabled) {
		mode = RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS;
	} else if (awb.enabled) {
		mode = RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS;
	} else if (lsc.enabled) {
		mode = RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS;
	} else {
		mode = RKISP1_CIF_ISP_DPF_GAIN_USAGE_DISABLED;
	}
	config_.gain.mode = mode;

	auto dpfStrength = params->block<BlockType::DpfStrength>();
	dpfStrength.setEnabled(true);

	*dpfStrength = strengthConfig_;
}

REGISTER_IPA_ALGORITHM(Dpf, "Dpf")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
