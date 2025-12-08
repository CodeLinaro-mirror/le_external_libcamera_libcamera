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

template<>
std::optional<rkisp1_cif_isp_dpf_strength_config>
YamlObject::Getter<rkisp1_cif_isp_dpf_strength_config>::get(const YamlObject &obj) const
{
	rkisp1_cif_isp_dpf_strength_config config = {};

	config.r = obj["r"].get<uint8_t>().value_or(64);
	config.g = obj["g"].get<uint8_t>().value_or(64);
	config.b = obj["b"].get<uint8_t>().value_or(64);

	return config;
}

template<>
std::optional<rkisp1_cif_isp_dpf_config>
YamlObject::Getter<rkisp1_cif_isp_dpf_config>::get(const YamlObject &obj) const
{
	rkisp1_cif_isp_dpf_config config = {};
	std::vector<uint8_t> values;

	/*
	 * The domain kernel is configured with a 9x9 kernel for the green
	 * pixels, and a 13x9 or 9x9 kernel for red and blue pixels.
	 */
	const YamlObject &dFObject = obj["filter"];

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
		return std::nullopt;
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
		return std::nullopt;
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
	const YamlObject &rFObject = obj["nll"];

	std::vector<uint16_t> nllValues;
	nllValues = rFObject["coeff"].getList<uint16_t>().value_or(std::vector<uint16_t>{});
	if (nllValues.size() != RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS) {
		return std::nullopt;
	}

	std::copy_n(nllValues.begin(), nllValues.size(),
		    std::begin(config.nll.coeff));

	std::string scaleMode = rFObject["scale-mode"].get<std::string>().value_or("");
	if (scaleMode == "linear") {
		config.nll.scale_mode = RKISP1_CIF_ISP_NLL_SCALE_LINEAR;
	} else if (scaleMode == "logarithmic") {
		config.nll.scale_mode = RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC;
	} else {
		return std::nullopt;
	}

	return config;
}

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
	  noiseReductionModes_({}),
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

	/* Parse modes. */
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
	auto dpfConfig = tuningData.get<rkisp1_cif_isp_dpf_config>();
	if (!dpfConfig)
		return false;

	config = *dpfConfig;

	auto strength = tuningData["strength"].get<rkisp1_cif_isp_dpf_strength_config>();
	if (!strength)
		return false;

	strengthConfig = *strength;

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

void Dpf::logConfigIfChanged(const IPAFrameContext &frameContext)
{
	if (!frameContext.dpf.update) {
		return;
	}

	std::ostringstream ss;

	ss << "DPF config update: ";
	ss << " control mode=" << static_cast<int>(runningMode_);
	ss << ", denoise=" << (frameContext.dpf.denoise ? "enabled" : "disabled, ");

	ss << "rb_fltsize="
	   << (config_.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9 ? "13x9" : "9x9");
	ss << ", nll_scale="
	   << (config_.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC ? "log" : "linear");
	ss << ", gain_mode=" << config_.gain.mode;
	ss << ", strength=" << int(strengthConfig_.r) << ',' << int(strengthConfig_.g) << ',' << int(strengthConfig_.b);

	ss << ", g=[";
	for (size_t i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
		if (i) {
			ss << ',';
		}
		ss << int(config_.g_flt.spatial_coeff[i]);
	}
	ss << "]";

	ss << ", rb=[";
	for (size_t i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
		if (i) {
			ss << ',';
		}
		ss << int(config_.rb_flt.spatial_coeff[i]);
	}
	ss << "]";

	ss << ", nll=[";
	for (size_t i = 0; i < RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS; ++i) {
		if (i) {
			ss << ',';
		}
		ss << int(config_.nll.coeff[i]);
	}
	ss << "]";
	LOG(RkISP1Dpf, Info) << ss.str();
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
	if (denoise) {
		LOG(RkISP1Dpf, Debug) << "Set denoise to " << *denoise;

		runningMode_ = *denoise;
		switch (*denoise) {
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
	if (!frameContext.dpf.update && frame > 0)
		return;

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

void Dpf::prepareEnabledMode(IPAContext &context,
			     [[maybe_unused]] const uint32_t frame,
			     [[maybe_unused]] IPAFrameContext &frameContext,
			     RkISP1Params *params)
{
	auto config = params->block<BlockType::Dpf>();
	config.setEnabled(true);
	*config = config_;

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

	config_.gain.mode = mode;
	auto strengthConfig = params->block<BlockType::DpfStrength>();
	strengthConfig.setEnabled(true);

	*strengthConfig = strengthConfig_;
	logConfigIfChanged(frameContext);
}

REGISTER_IPA_ALGORITHM(Dpf, "Dpf")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
