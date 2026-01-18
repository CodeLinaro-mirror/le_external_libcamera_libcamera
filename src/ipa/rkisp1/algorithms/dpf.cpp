/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Denoise Pre-Filter control
 */

#include "dpf.h"

#include <algorithm>
#include <map>
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

namespace {

const std::map<std::string, int32_t> kModesMap = {
	{ "ReductionMinimal", controls::draft::NoiseReductionModeMinimal },
	{ "ReductionFast", controls::draft::NoiseReductionModeFast },
	{ "ReductionHighQuality", controls::draft::NoiseReductionModeHighQuality },
	{ "ReductionZSL", controls::draft::NoiseReductionModeZSL },
	{ "ReductionOff", controls::draft::NoiseReductionModeOff },
};

std::string modeName(int32_t mode)
{
	auto it = std::find_if(kModesMap.begin(), kModesMap.end(),
			       [mode](const auto &pair) {
				       return pair.second == mode;
			       });

	if (it != kModesMap.end())
		return it->first;

	return "ReductionUnknown";
}
} /* namespace */

Dpf::Dpf()
	: noiseReductionModes_({}), activeMode_(noiseReductionModes_.end())
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Dpf::init([[maybe_unused]] IPAContext &context,
	      const YamlObject &tuningData)
{
	/* Parse tuning block. */
	int ret = parseConfig(tuningData);
	if (ret)
		return ret;

	return 0;
}

int Dpf::parseConfig(const YamlObject &tuningData)
{
	/* Parse noise reduction modes. */
	if (!tuningData.contains("NoiseReductionModes")) {
		LOG(RkISP1Dpf, Error) << "Missing modes in DPF tuning data";
		return -EINVAL;
	}

	noiseReductionModes_.clear();
	for (const auto &entry : tuningData["NoiseReductionModes"].asList()) {
		std::optional<std::string> typeOpt =
			entry["type"].get<std::string>();
		if (!typeOpt) {
			LOG(RkISP1Dpf, Error) << "Modes entry missing type";
			return -EINVAL;
		}

		ModeConfig mode;
		auto it = kModesMap.find(*typeOpt);
		if (it == kModesMap.end()) {
			LOG(RkISP1Dpf, Error) << "Unknown mode type: " << *typeOpt;
			return -EINVAL;
		}

		mode.modeValue = it->second;
		int ret = parseSingleConfig(entry, mode.dpf, mode.strength);
		if (ret) {
			LOG(RkISP1Dpf, Error) << "Failed to parse mode: " << *typeOpt;
			return ret;
		}

		noiseReductionModes_.push_back(mode);
	}

	/*
	 * Parse the optional ActiveMode.
	 * If not present, default to "ReductionOff".
	 */
	std::string activeMode = tuningData["ActiveMode"].get<std::string>().value_or("ReductionOff");
	auto it = kModesMap.find(activeMode);
	if (it == kModesMap.end()) {
		LOG(RkISP1Dpf, Warning) << "Invalid ActiveMode: " << activeMode;
		activeMode_ = noiseReductionModes_.end();
		return 0;
	}

	if (!loadConfig(it->second)) {
		/* If the default "ReductionOff" mode is requested but not configured, disable DPF. */
		if (it->second == controls::draft::NoiseReductionModeOff)
			activeMode_ = noiseReductionModes_.end();
		else
			return -EINVAL;
	}

	return 0;
}

int Dpf::parseSingleConfig(const YamlObject &tuningData,
			   rkisp1_cif_isp_dpf_config &config,
			   rkisp1_cif_isp_dpf_strength_config &strengthConfig)
{
	std::vector<uint8_t> values;

	/*
	 * The domain kernel is configured with a 9x9 kernel for the green
	 * pixels, and a 13x9 or 9x9 kernel for red and blue pixels.
	 */
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
		return -EINVAL;
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
		return -EINVAL;
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
	const YamlObject &rFObject = tuningData["NoiseLevelFunction"];

	std::vector<uint16_t> nllValues;
	nllValues = rFObject["coeff"].getList<uint16_t>().value_or(std::vector<uint16_t>{});
	if (nllValues.size() != RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS) {
		LOG(RkISP1Dpf, Error)
			<< "Invalid 'RangeFilter:coeff': expected "
			<< RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS
			<< " elements, got " << nllValues.size();
		return -EINVAL;
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
		return -EINVAL;
	}

	const YamlObject &fSObject = tuningData["FilterStrength"];

	strengthConfig.r = fSObject["r"].get<uint8_t>().value_or(64);
	strengthConfig.g = fSObject["g"].get<uint8_t>().value_or(64);
	strengthConfig.b = fSObject["b"].get<uint8_t>().value_or(64);

	return 0;
}

bool Dpf::loadConfig(int32_t mode)
{
	auto it = std::find_if(noiseReductionModes_.begin(), noiseReductionModes_.end(),
			       [mode](const ModeConfig &m) {
				       return m.modeValue == mode;
			       });
	if (it == noiseReductionModes_.end()) {
		LOG(RkISP1Dpf, Warning)
			<< "No DPF config for reduction mode: " << modeName(mode);
		return false;
	}

	activeMode_ = it;

	LOG(RkISP1Dpf, Debug)
		<< "DPF mode=Reduction (config loaded)"
		<< " mode= " << modeName(mode);

	return true;
}

void Dpf::logConfig(const IPAFrameContext &frameContext,
		    const struct rkisp1_cif_isp_dpf_config &config,
		    const struct rkisp1_cif_isp_dpf_strength_config &strengthConfig) const
{
	std::ostringstream ss;

	ss << "DPF config update: ";
	ss << " control mode=" << modeName(activeMode_->modeValue);
	ss << ", denoise=" << (frameContext.dpf.denoise ? "enabled" : "disabled");

	ss << ", rb_fltsize="
	   << (config.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9 ? "13x9" : "9x9");
	ss << ", nll_scale="
	   << (config.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC ? "log" : "linear");
	ss << ", gain_mode=" << config.gain.mode;
	ss << ", strength=" << int(strengthConfig.r) << ',' << int(strengthConfig.g) << ',' << int(strengthConfig.b);

	ss << ", g=[";
	for (size_t i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
		if (i)
			ss << ',';
		ss << int(config.g_flt.spatial_coeff[i]);
	}
	ss << "]";

	ss << ", rb=[";
	for (size_t i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
		if (i)
			ss << ',';
		ss << int(config.rb_flt.spatial_coeff[i]);
	}
	ss << "]";

	ss << ", nll=[";
	for (size_t i = 0; i < RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS; ++i) {
		if (i)
			ss << ',';
		ss << int(config.nll.coeff[i]);
	}
	ss << "]";
	LOG(RkISP1Dpf, Debug) << ss.str();
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
			if (loadConfig(*denoise)) {
				update = true;
				dpf.denoise = true;
			}
			break;
		default:
			LOG(RkISP1Dpf, Error)
				<< "Unsupported denoise value "
				<< *denoise;
			break;
		}
		if (update)
			LOG(RkISP1Dpf, Debug) << "Set denoise to " << modeName(*denoise);
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
		prepareDisabledMode(params);
		return;
	}

	prepareEnabledMode(context, frameContext, params);
}

void Dpf::prepareDisabledMode(RkISP1Params *params)
{
	auto dpfConfig = params->block<BlockType::Dpf>();
	dpfConfig.setEnabled(false);
	auto dpfStrength = params->block<BlockType::DpfStrength>();
	dpfStrength.setEnabled(false);
}

void Dpf::prepareEnabledMode(IPAContext &context, IPAFrameContext &frameContext,
			     RkISP1Params *params)
{
	if (activeMode_ == noiseReductionModes_.end())
		return;

	const ModeConfig &modeConfig = *activeMode_;

	auto config = params->block<BlockType::Dpf>();
	config.setEnabled(true);
	*config = modeConfig.dpf;

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

	auto strengthConfig = params->block<BlockType::DpfStrength>();
	strengthConfig.setEnabled(true);
	*strengthConfig = modeConfig.strength;

	if (frameContext.dpf.update)
		logConfig(frameContext, *config, *strengthConfig);
}

REGISTER_IPA_ALGORITHM(Dpf, "Dpf")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
