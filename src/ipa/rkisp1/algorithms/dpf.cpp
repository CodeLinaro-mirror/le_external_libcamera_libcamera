/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Denoise Pre-Filter control
 */

#include "dpf.h"

#include <algorithm>
#include <array>
#include <sstream>
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
	: config_({}), strengthConfig_({})
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

	/* Preserve base (non-ISO) YAML configuration for restoration after manual mode. */
	baseConfig_ = config_;
	baseStrengthConfig_ = strengthConfig_;

	/* Optional ISO-banded tuning */
	if (useIsoLevels_) {
		LOG(RkISP1Dpf, Info)
			<< "DPF init: loaded " << isoLevels_.size()
			<< " ISO level(s) from tuning";
	}
	return 0;
}

bool Dpf::parseConfig(const YamlObject &tuningData)
{
	// Parse base config
	if (!parseSingleConfig(tuningData, config_, strengthConfig_))
		return false;

	baseConfig_ = config_;
	baseStrengthConfig_ = strengthConfig_;

	/* Optional master enable flag (default true). If false, we parse but won't program. */
	yamlHelper::optbool(tuningData, "enable", enableDpf_, true);

	/* Optional developer mode flag (default true). If false, only basic manual controls available. */
	bool devMode = true;
	yamlHelper::optbool(tuningData, "devmode", devMode, true);
	setDevMode(devMode);

	// Parse ISO levels
	if (tuningData.contains("IsoLevels")) {
		useIsoLevels_ = true;
		isoLevels_.clear();
		for (const auto &entry : tuningData["IsoLevels"].asList()) {
			std::optional<unsigned int> maxIsoOpt = entry["maxIso"].get<unsigned int>();
			if (!maxIsoOpt) {
				LOG(RkISP1Dpf, Error) << "IsoLevels entry missing maxIso";
				continue;
			}
			IsoLevelConfig lvl{};
			lvl.maxIso = *maxIsoOpt;
			if (!parseSingleConfig(entry, lvl.dpf, lvl.strength))
				continue;
			isoLevels_.push_back(lvl);
		}
		std::sort(isoLevels_.begin(), isoLevels_.end(),
			  [](const IsoLevelConfig &a, const IsoLevelConfig &b) {
				  return a.maxIso < b.maxIso;
			  });
	}

	return true;
}

bool Dpf::parseSingleConfig(const YamlObject &config,
			    rkisp1_cif_isp_dpf_config &cfg,
			    rkisp1_cif_isp_dpf_strength_config &strength)
{
	/*
	 * The domain kernel is configured with a 9x9 kernel for the green
	 * pixels, and a 13x9 or 9x9 kernel for red and blue pixels.
	 *
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
	 *
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
	 *    +----------|-------------|---> X
	 *              -6...........-1 0 1......6
	 *
	 * For a 9x9 kernel, columns -6 and 6 are dropped, so coefficient
	 * number 6 is not used.
	 */

	if (!config.contains("DomainFilter")) {
		LOG(RkISP1Dpf, Error) << "DomainFilter section missing";
		return false;
	}
	const YamlObject &dFObject = config["DomainFilter"];

	std::vector<uint8_t> gCoeffs;
	if (!yamlHelper::optList8(dFObject, "g", gCoeffs, RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS))
		return false;
	std::copy_n(gCoeffs.begin(), gCoeffs.size(), std::begin(cfg.g_flt.spatial_coeff));
	cfg.g_flt.gr_enable = true;
	cfg.g_flt.gb_enable = true;

	uint32_t rbFilterSize;
	if (!yamlHelper::optFilterCoeffs(dFObject, "rb", cfg.rb_flt.spatial_coeff, rbFilterSize, RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS))
		return false;
	cfg.rb_flt.fltsize = rbFilterSize ? RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9 : RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9;
	cfg.rb_flt.r_enable = true;
	cfg.rb_flt.b_enable = true;

	if (!config.contains("NoiseLevelFunction")) {
		LOG(RkISP1Dpf, Error) << "NoiseLevelFunction section missing";
		return false;
	}
	const YamlObject &rFObject = config["NoiseLevelFunction"];

	std::vector<uint16_t> nllCoeffs;
	if (!yamlHelper::optList16(rFObject, "coeff", nllCoeffs, RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS))
		return false;
	std::copy_n(nllCoeffs.begin(), nllCoeffs.size(), std::begin(cfg.nll.coeff));

	const std::map<std::string, uint32_t> scaleModeMap = {
		{ "linear", RKISP1_CIF_ISP_NLL_SCALE_LINEAR },
		{ "logarithmic", RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC }
	};
	if (!yamlHelper::optEnum<uint32_t>(rFObject, "scale-mode", cfg.nll.scale_mode, RKISP1_CIF_ISP_NLL_SCALE_LINEAR, scaleModeMap)) {
		LOG(RkISP1Dpf, Error) << "NoiseLevelFunction:scale-mode expected 'linear' or 'logarithmic'";
		return false;
	}

	if (!config.contains("Gain")) {
		LOG(RkISP1Dpf, Error) << "Gain section missing";
		return false;
	}
	const YamlObject &gObject = config["Gain"];

	yamlHelper::opt32(gObject, "gain_mode", cfg.gain.mode, RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS);
	yamlHelper::opt16(gObject, "nf_r_gain", cfg.gain.nf_r_gain, 256);
	yamlHelper::opt16(gObject, "nf_b_gain", cfg.gain.nf_b_gain, 256);
	yamlHelper::opt16(gObject, "nf_gr_gain", cfg.gain.nf_gr_gain, 256);
	yamlHelper::opt16(gObject, "nf_gb_gain", cfg.gain.nf_gb_gain, 256);

	if (!config.contains("FilterStrength")) {
		LOG(RkISP1Dpf, Error) << "FilterStrength section missing";
		return false;
	}
	const YamlObject &fSObject = config["FilterStrength"];

	yamlHelper::opt8(fSObject, "r", strength.r, 64);
	yamlHelper::opt8(fSObject, "g", strength.g, 64);
	yamlHelper::opt8(fSObject, "b", strength.b, 64);
	return true;
}

void Dpf::handleEnableControl(const ControlList &controls, IPAFrameContext &frameContext, IPAContext &context)
{
	if (const auto &c = controls.get(controls::rkisp1::DpfEnable); c) {
		bool requested = *c != 0;
		if (requested != enableDpf_) {
			enableDpf_ = requested;
			frameContext.dpf.update = true;
			LOG(RkISP1Dpf, Info) << "DPF global " << (enableDpf_ ? "enabled" : "disabled");
		}
	}
	context.activeState.dpf.denoise = enableDpf_;
	frameContext.dpf.denoise = enableDpf_;
}

void Dpf::collectManualOverrides(const ControlList &controls)
{
	if (const auto &c = controls.get(controls::rkisp1::DpfChannelStrengths); c) {
		if (c->size() == 3) {
			overrides_.strength = DpfStrengthSettings{ static_cast<uint16_t>((*c)[0]), static_cast<uint16_t>((*c)[1]), static_cast<uint16_t>((*c)[2]) };
		}
	}
	if (isDevMode()) {
		if (const auto &c = controls.get(controls::rkisp1::DpfGreenSpatialCoefficients); c) {
			if (c->size() == RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS) {
				DpfSpatialGreenSettings green;
				std::copy_n(c->begin(), RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS, green.coeffs.begin());
				overrides_.spatialGreen = green;
			}
		}
		if (const auto &c = controls.get(controls::rkisp1::DpfRedBlueSpatialCoefficients); c) {
			if (c->size() == RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS) {
				DpfSpatialRbSettings rb;
				std::copy_n(c->begin(), RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS, rb.coeffs.begin());
				rb.size = (config_.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9) ? 1 : 0;
				overrides_.spatialRb = rb;
			}
		}
		if (const auto &c = controls.get(controls::rkisp1::DpfRbFilterSize); c) {
			overrides_.rbSize = *c ? 1 : 0;
		}
		if (const auto &c = controls.get(controls::rkisp1::DpfNoiseLevelLookupCoefficients); c) {
			if (c->size() == RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS) {
				DpfNllSettings nll;
				std::copy_n(c->begin(), RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS, nll.coeffs.begin());
				nll.scaleMode = (config_.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC) ? 1 : 0;
				overrides_.nll = nll;
			}
		}
		if (const auto &c = controls.get(controls::rkisp1::DpfNoiseLevelLookupScaleMode); c) {
			if (overrides_.nll) {
				overrides_.nll->scaleMode = *c ? 1 : 0;
			} else {
				DpfNllSettings nll;
				std::copy_n(std::begin(config_.nll.coeff), RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS, nll.coeffs.begin());
				nll.scaleMode = *c ? 1 : 0;
				overrides_.nll = nll;
			}
		}
	}
}

bool Dpf::checkDevModeOverridesChanged()
{
	if (!isDevMode())
		return false;

	bool changed = false;
	if (overrides_.spatialGreen) {
		bool coeffsChanged = false;
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
			if (overrides_.spatialGreen->coeffs[i] != config_.g_flt.spatial_coeff[i]) {
				coeffsChanged = true;
				break;
			}
		}
		if (coeffsChanged) {
			changed = true;
		}
	}
	if (overrides_.spatialRb) {
		bool coeffsChanged = false;
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
			if (overrides_.spatialRb->coeffs[i] != config_.rb_flt.spatial_coeff[i]) {
				coeffsChanged = true;
				break;
			}
		}
		if (coeffsChanged) {
			changed = true;
		}
	}
	if (overrides_.rbSize && *overrides_.rbSize != (config_.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9 ? 1 : 0)) {
		changed = true;
	}
	if (overrides_.nll) {
		bool coeffsChanged = false;
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS; ++i) {
			if (overrides_.nll->coeffs[i] != config_.nll.coeff[i]) {
				coeffsChanged = true;
				break;
			}
		}
		if (coeffsChanged || overrides_.nll->scaleMode != (config_.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC ? 1 : 0)) {
			changed = true;
		}
	}
	return changed;
}

void Dpf::snapshotCurrentToOverrides()
{
	overrides_.clear();
	overrides_.strength = DpfStrengthSettings{ strengthConfig_.r, strengthConfig_.g, strengthConfig_.b };
	if (isDevMode()) {
		DpfSpatialGreenSettings green;
		std::copy_n(std::begin(config_.g_flt.spatial_coeff), RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS, green.coeffs.begin());
		overrides_.spatialGreen = green;
		DpfSpatialRbSettings rb;
		std::copy_n(std::begin(config_.rb_flt.spatial_coeff), RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS, rb.coeffs.begin());
		rb.size = (config_.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9) ? 1 : 0;
		overrides_.spatialRb = rb;
		overrides_.rbSize = rb.size;
		DpfNllSettings nll;
		std::copy_n(std::begin(config_.nll.coeff), RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS, nll.coeffs.begin());
		nll.scaleMode = (config_.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC) ? 1 : 0;
		overrides_.nll = nll;
	}
}

void Dpf::restoreAutoConfig(IPAContext &context, IPAFrameContext &frameContext)
{
	overrides_.clear();
	if (useIsoLevels_) {
		unsigned iso = computeIso(context, frameContext);
		int idx = DenoiseBaseAlgorithm::selectIsoBand(iso, isoLevels_);
		if (idx >= 0) {
			config_ = isoLevels_[idx].dpf;
			strengthConfig_ = isoLevels_[idx].strength;
			lastIsoIndex_ = idx;
		}
	} else {
		config_ = baseConfig_;
		strengthConfig_ = baseStrengthConfig_;
		lastIsoIndex_ = -1;
	}
	frameContext.dpf.update = true;
}

bool Dpf::processModeChange(const ControlList &controls, uint32_t currentFrame)
{
	const auto &cMode = controls.get(controls::rkisp1::DpfMode);
	if (!cMode)
		return false;

	bool requested = (*cMode == controls::rkisp1::DpfModeManual);
	if (requested == isManualMode())
		return false;

	// Prevent rapid mode changes (hysteresis to avoid application bugs)
	uint32_t framesSinceLastChange = currentFrame - lastModeChangeFrame_;
	if (framesSinceLastChange < kMinModeChangeInterval && lastModeChangeFrame_ != 0) {
		LOG(RkISP1Dpf, Debug) << "Ignoring rapid mode change (hysteresis): requested="
				      << (requested ? "manual" : "auto")
				      << ", current=" << (isManualMode() ? "manual" : "auto")
				      << ", framesSinceLast=" << framesSinceLastChange;
		return false;
	}

	setManualMode(requested);
	// Reset overrides if switching to auto mode , make sure the config will apply to next frame
	lastModeChangeFrame_ = currentFrame;
	return true;
}

void Dpf::applyOverridesTo(rkisp1_cif_isp_dpf_config &cfg,
			   rkisp1_cif_isp_dpf_strength_config &str,
			   bool &anyOverride)
{
	if (!isManualMode())
		return; /* only apply overrides in manual mode */

	if (overrides_.strength) {
		str.r = overrides_.strength->r;
		str.g = overrides_.strength->g;
		str.b = overrides_.strength->b;
		anyOverride = true;
	}
	if (overrides_.spatialGreen) {
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
			cfg.g_flt.spatial_coeff[i] = overrides_.spatialGreen->coeffs[i];
		}
		anyOverride = true;
	}
	if (overrides_.spatialRb) {
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
			cfg.rb_flt.spatial_coeff[i] = overrides_.spatialRb->coeffs[i];
		}
		anyOverride = true;
	}
	if (overrides_.rbSize) {
		cfg.rb_flt.fltsize = *overrides_.rbSize ? RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9
							: RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9;
		anyOverride = true;
	}
	if (overrides_.nll) {
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS; ++i) {
			cfg.nll.coeff[i] = overrides_.nll->coeffs[i];
		}
		cfg.nll.scale_mode = overrides_.nll->scaleMode ? RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC
							       : RKISP1_CIF_ISP_NLL_SCALE_LINEAR;
		anyOverride = true;
	}
	if (anyOverride) {
		config_ = cfg;
		strengthConfig_ = str;
		LOG(RkISP1Dpf, Info)
			<< "DPF manual overrides applied: strength="
			<< (int)strengthConfig_.r << "," << (int)strengthConfig_.g << "," << (int)strengthConfig_.b
			<< (overrides_.spatialGreen ? " gKernel" : "")
			<< (overrides_.spatialRb ? " rbKernel" : "")
			<< (overrides_.rbSize ? " rbSize" : "")
			<< (overrides_.nll ? " nll" : "");
	}
}

void Dpf::logConfigIfChanged(unsigned iso, int isoIndex, bool anyOverride, const IPAFrameContext &frameContext)
{
	static rkisp1_cif_isp_dpf_config lastCfg{};
	static rkisp1_cif_isp_dpf_strength_config lastStr{};
	static bool haveLast = false;
	bool cfgChanged = !haveLast || memcmp(&lastCfg, &config_, sizeof(config_)) != 0;
	bool strChanged = !haveLast || memcmp(&lastStr, &strengthConfig_, sizeof(strengthConfig_)) != 0;
	if (!(cfgChanged || strChanged))
		return;
	std::ostringstream gs, rbs, nll;
	gs << '[';
	rbs << '[';
	nll << '[';
	for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i) {
		if (i) {
			gs << ',';
			rbs << ',';
		}
		gs << (int)config_.g_flt.spatial_coeff[i];
		rbs << (int)config_.rb_flt.spatial_coeff[i];
	}
	for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS; ++i) {
		if (i)
			nll << ',';
		nll << config_.nll.coeff[i];
	}
	gs << ']';
	rbs << ']';
	nll << ']';
	const char *modeStr =
		config_.gain.mode == RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS ? "AWB+LSC" : config_.gain.mode == RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS ? "AWB"
										       : config_.gain.mode == RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS	    ? "LSC"
										       : config_.gain.mode == RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_GAINS	    ? "NF"
										       : config_.gain.mode == RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_LSC_GAINS    ? "NF+LSC"
																			    : "disabled";
	LOG(RkISP1Dpf, Info) << "DPF config update: rb_fltsize="
			     << (config_.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9 ? "13x9" : "9x9")
			     << ", nll_scale="
			     << (config_.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC ? "log" : "linear")
			     << ", gain_mode=" << modeStr
			     << ", strength=" << (int)strengthConfig_.r << "," << (int)strengthConfig_.g << "," << (int)strengthConfig_.b
			     << ", g=" << gs.str() << ", rb=" << rbs.str() << ", nll=" << nll.str()
			     << ", iso=" << iso
			     << (useIsoLevels_ && isoIndex >= 0 ? (", iso_band=" + std::to_string(isoIndex) + (lastIsoIndex_ == isoIndex ? "" : "(new)")) : "")
			     << ", control mode=" << (isManualMode() ? "manual" : "auto")
			     << ", denoise=" << (frameContext.dpf.denoise ? "enabled" : "disabled")
			     << (anyOverride ? " (overrides applied)" : "");
	lastCfg = config_;
	lastStr = strengthConfig_;
	haveLast = true;
}

ControlInfoMap::Map Dpf::getControlMap() const
{
	ControlInfoMap::Map map;
	map[&controls::rkisp1::DpfEnable] = ControlInfo(false, true, enableDpf_);
	map[&controls::rkisp1::DpfMode] = ControlInfo(controls::rkisp1::DpfModeValues, ControlValue(controls::rkisp1::DpfModeAuto));
	std::array<int32_t, 3> strengthDefault = { static_cast<int32_t>(baseStrengthConfig_.r), static_cast<int32_t>(baseStrengthConfig_.g), static_cast<int32_t>(baseStrengthConfig_.b) };
	map[&controls::rkisp1::DpfChannelStrengths] = ControlInfo(0, 255, Span<const int32_t, 3>(strengthDefault));
	if (isDevMode()) {
		std::array<int32_t, RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS> greenCoeffs;
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i)
			greenCoeffs[i] = baseConfig_.g_flt.spatial_coeff[i];
		map[&controls::rkisp1::DpfGreenSpatialCoefficients] = ControlInfo(0, 63, Span<const int32_t, RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS>(greenCoeffs));
		std::array<int32_t, RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS> rbCoeffs;
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS; ++i)
			rbCoeffs[i] = baseConfig_.rb_flt.spatial_coeff[i];
		map[&controls::rkisp1::DpfRedBlueSpatialCoefficients] = ControlInfo(0, 63, Span<const int32_t, RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS>(rbCoeffs));
		int32_t rbSizeDefault = (baseConfig_.rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9) ? 1 : 0;
		map[&controls::rkisp1::DpfRbFilterSize] = ControlInfo(controls::rkisp1::DpfRbFilterSizeValues, ControlValue(rbSizeDefault));
		std::array<int32_t, RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS> nllCoeffs;
		for (unsigned i = 0; i < RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS; ++i)
			nllCoeffs[i] = baseConfig_.nll.coeff[i];
		map[&controls::rkisp1::DpfNoiseLevelLookupCoefficients] = ControlInfo(0, 1023, Span<const int32_t, RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS>(nllCoeffs));
		int32_t scaleModeDefault = (baseConfig_.nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC) ? 1 : 0;
		map[&controls::rkisp1::DpfNoiseLevelLookupScaleMode] = ControlInfo(controls::rkisp1::DpfNoiseLevelLookupScaleModeValues, ControlValue(scaleModeDefault));
	}
	map[&controls::rkisp1::DpfIso] = ControlInfo(0, 3200, 0);
	return map;
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
	handleEnableControl(controls, frameContext, context);
	bool modeChanged = processModeChange(controls, frame);
	if (modeChanged) {
		if (isManualMode()) {
			snapshotCurrentToOverrides();
			LOG(RkISP1Dpf, Info) << "DPF mode=Manual (snapshot captured)";
		} else {
			restoreAutoConfig(context, frameContext);
			LOG(RkISP1Dpf, Info) << "DPF mode=Auto (restored auto config)";
		}
		frameContext.dpf.update = true;
	}

	if (isManualMode()) {
		collectManualOverrides(controls);
		// Check if manual overrides have changed and trigger update
		if (overrides_.strength &&
		    (overrides_.strength->r != strengthConfig_.r ||
		     overrides_.strength->g != strengthConfig_.g ||
		     overrides_.strength->b != strengthConfig_.b)) {
			frameContext.dpf.update = true;
		}
		if (checkDevModeOverridesChanged()) {
			frameContext.dpf.update = true;
		}
	}

	LOG(RkISP1Dpf, Debug) << "queueRequest: denoise=" << frameContext.dpf.denoise
			      << ", update=" << frameContext.dpf.update
			      << (enableDpf_ ? "" : " (DPF disabled)")
			      << (modeChanged ? " (mode change)" : "");
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Dpf::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, RkISP1Params *params)
{
	// check if master denoise toggle on
	if (!frameContext.dpf.denoise) {
		auto cfg = params->block<BlockType::Dpf>();
		cfg.setEnabled(false);
		auto str = params->block<BlockType::DpfStrength>();
		str.setEnabled(false);
		return;
	}

	if (isManualMode())
		prepareManualMode(context, frame, frameContext, params);
	else
		prepareAutoMode(context, frame, frameContext, params);
}

void Dpf::prepareAutoMode(IPAContext &context, const uint32_t frame,
			  IPAFrameContext &frameContext, RkISP1Params *params)
{
	unsigned iso = computeIso(context, frameContext);
	bool baseIsoSkip = iso <= 100;

	// Select different dpf config determined by iso Level
	if (useIsoLevels_) {
		int idx = DenoiseBaseAlgorithm::selectIsoBand(iso, isoLevels_);
		if (idx >= 0 && idx != lastIsoIndex_) {
			config_ = isoLevels_[idx].dpf;
			strengthConfig_ = isoLevels_[idx].strength;
			frameContext.dpf.update = true;
			lastIsoIndex_ = idx;
		}
	}
	// Disable dpf denoise due to high light
	if (baseIsoSkip) {
		auto cfg = params->block<BlockType::Dpf>();
		cfg.setEnabled(false);
		auto str = params->block<BlockType::DpfStrength>();
		str.setEnabled(false);
		return;
	}

	if (!frameContext.dpf.update && frame > 0)
		return;

	auto cfgBlock = params->block<BlockType::Dpf>();
	cfgBlock.setEnabled(true);
	*cfgBlock = config_;

	cfgBlock->gain.mode = config_.gain.mode;

	if (frameContext.dpf.update) {
		auto strBlock = params->block<BlockType::DpfStrength>();
		strBlock.setEnabled(true);
		*strBlock = strengthConfig_;
		logConfigIfChanged(iso, lastIsoIndex_, false, frameContext);
	}
}

void Dpf::prepareManualMode(IPAContext &context, const uint32_t frame,
			    IPAFrameContext &frameContext, RkISP1Params *params)
{
	unsigned iso = computeIso(context, frameContext);

	if (!frameContext.dpf.update && frame > 0)
		return;

	auto cfgBlock = params->block<BlockType::Dpf>();
	cfgBlock.setEnabled(true);
	*cfgBlock = config_;

	cfgBlock->gain.mode = config_.gain.mode;

	if (frame == 0 || frameContext.dpf.update) {
		auto strBlock = params->block<BlockType::DpfStrength>();
		strBlock.setEnabled(true);
		*strBlock = strengthConfig_;
		bool anyOverride = false;
		applyOverridesTo(*cfgBlock, *strBlock, anyOverride);
		logConfigIfChanged(iso, lastIsoIndex_, anyOverride, frameContext);
	}
}

REGISTER_IPA_ALGORITHM(Dpf, "Dpf")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
