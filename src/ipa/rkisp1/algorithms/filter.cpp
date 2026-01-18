/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Filter control
 */

#include "filter.h"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "linux/rkisp1-config.h"

/**
 * \file filter.h
 */

namespace libcamera {

namespace ipa::rkisp1::algorithms {

LOG_DEFINE_CATEGORY(RkISP1Filter)

namespace {

static constexpr uint32_t kFiltLumWeightDefault = 0x00022040;
static constexpr uint32_t kFiltModeDefault = 0x000004f2;

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

struct SharpnessPreset {
	uint32_t fac_sh0;
	uint32_t fac_sh1;
	uint32_t fac_mid;
	uint32_t fac_bl0;
	uint32_t fac_bl1;
};

/*
 * Sharpness presets
 * The presets are based on the following table.
 */
static constexpr SharpnessPreset kSharpnessPresets[] = {
	{ 0x04, 0x04, 0x04, 0x02, 0x00 }, /* Level 0 */
	{ 0x07, 0x08, 0x06, 0x02, 0x00 }, /* Level 1 */
	{ 0x0a, 0x0c, 0x08, 0x04, 0x00 }, /* Level 2 */
	{ 0x0c, 0x10, 0x0a, 0x06, 0x02 }, /* Level 3 */
	{ 0x10, 0x16, 0x0c, 0x08, 0x04 }, /* Level 4 */
	{ 0x14, 0x1b, 0x10, 0x0a, 0x04 }, /* Level 5 */
	{ 0x1a, 0x20, 0x13, 0x0c, 0x06 }, /* Level 6 */
	{ 0x1e, 0x26, 0x17, 0x10, 0x08 }, /* Level 7 */
	{ 0x24, 0x2c, 0x1d, 0x15, 0x0d }, /* Level 8 */
	{ 0x2a, 0x30, 0x22, 0x1a, 0x14 }, /* Level 9 */
	{ 0x30, 0x3f, 0x28, 0x24, 0x20 }, /* Level 10 */
};
} /* namespace */

/**
 * \class Filter
 * \brief RkISP1 Filter control
 *
 * Denoise and Sharpness filters will be applied by RkISP1 during the
 * demosaicing step. The denoise filter is responsible for removing noise from
 * the image, while the sharpness filter will enhance its acutance.
 */

Filter::Filter()
	: noiseReductionModes_({}),
	  activeMode_(noiseReductionModes_.end())
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Filter::init(IPAContext &context,
		 const YamlObject &tuningData)
{
	auto &cmap = context.ctrlMap;
	cmap[&controls::Sharpness] = ControlInfo(0.0f, 10.0f, 1.0f);

	/* Parse tuning block. */
	int ret = parseConfig(tuningData);
	if (ret)
		return ret;

	return 0;
}

int Filter::parseConfig(const YamlObject &tuningData)
{
	/* Parse noise reduction modes. */
	if (!tuningData.contains("NoiseReductionModes")) {
		LOG(RkISP1Filter, Error) << "Missing modes in Filter tuning data";
		return -EINVAL;
	}

	noiseReductionModes_.clear();
	for (const auto &entry : tuningData["NoiseReductionModes"].asList()) {
		std::optional<std::string> typeOpt =
			entry["type"].get<std::string>();
		if (!typeOpt) {
			LOG(RkISP1Filter, Error) << "Modes entry missing type";
			return -EINVAL;
		}

		ModeConfig mode;
		auto it = kModesMap.find(*typeOpt);
		if (it == kModesMap.end()) {
			LOG(RkISP1Filter, Error) << "Unknown mode type: " << *typeOpt;
			return -EINVAL;
		}

		mode.modeValue = it->second;
		int ret = parseSingleConfig(entry, mode.config);
		if (ret) {
			LOG(RkISP1Filter, Error) << "Failed to parse mode: " << *typeOpt;
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
		LOG(RkISP1Filter, Warning) << "Invalid ActiveMode: " << activeMode;
		activeMode_ = noiseReductionModes_.end();
		return 0;
	}

	if (!loadConfig(it->second)) {
		/* If the default "ReductionOff" mode is requested but not configured, disable Filter. */
		if (it->second == controls::draft::NoiseReductionModeOff)
			activeMode_ = noiseReductionModes_.end();
		else
			return -EINVAL;
	}

	return 0;
}
int Filter::parseSingleConfig(const YamlObject &tuningData,
			      struct rkisp1_cif_isp_flt_config &config)
{
	if (!tuningData.contains("mode") || !tuningData.contains("lum_weight")) {
		LOG(RkISP1Filter, Error) << "Modes entry missing mode or lum_weight";
		return -EINVAL;
	}

	config.mode = tuningData["mode"].get<uint32_t>().value_or(kFiltModeDefault);
	config.lum_weight = tuningData["lum_weight"].get<uint32_t>().value_or(kFiltLumWeightDefault);
	config.grn_stage1 = tuningData["grn_stage1"].get<uint8_t>().value_or(config.grn_stage1);
	config.chr_h_mode = tuningData["chr_h_mode"].get<uint8_t>().value_or(config.chr_h_mode);
	config.chr_v_mode = tuningData["chr_v_mode"].get<uint8_t>().value_or(config.chr_v_mode);

	config.thresh_bl0 = tuningData["thresh_bl0"].get<uint32_t>().value_or(config.thresh_bl0);
	config.thresh_bl1 = tuningData["thresh_bl1"].get<uint32_t>().value_or(config.thresh_bl1);
	config.thresh_sh0 = tuningData["thresh_sh0"].get<uint32_t>().value_or(config.thresh_sh0);
	config.thresh_sh1 = tuningData["thresh_sh1"].get<uint32_t>().value_or(config.thresh_sh1);

	config.fac_sh0 = tuningData["fac_sh0"].get<uint32_t>().value_or(config.fac_sh0);
	config.fac_sh1 = tuningData["fac_sh1"].get<uint32_t>().value_or(config.fac_sh1);
	config.fac_mid = tuningData["fac_mid"].get<uint32_t>().value_or(config.fac_mid);
	config.fac_bl0 = tuningData["fac_bl0"].get<uint32_t>().value_or(config.fac_bl0);
	config.fac_bl1 = tuningData["fac_bl1"].get<uint32_t>().value_or(config.fac_bl1);

	return 0;
}

bool Filter::loadConfig(int32_t mode)
{
	auto it = std::find_if(noiseReductionModes_.begin(), noiseReductionModes_.end(),
			       [mode](const ModeConfig &m) {
				       return m.modeValue == mode;
			       });
	if (it == noiseReductionModes_.end()) {
		LOG(RkISP1Filter, Warning)
			<< "No Filter config for reduction mode: " << modeName(mode);
		return false;
	}

	activeMode_ = it;

	LOG(RkISP1Filter, Debug)
		<< "Filter mode=Reduction (config loaded)"
		<< " mode= " << modeName(mode);

	return true;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Filter::queueRequest(IPAContext &context,
			  [[maybe_unused]] const uint32_t frame,
			  IPAFrameContext &frameContext,
			  const ControlList &controls)
{
	auto &filter = context.activeState.filter;
	bool update = false;

	const auto &sharpness = controls.get(controls::Sharpness);
	if (sharpness) {
		unsigned int value = std::round(std::clamp(*sharpness, 0.0f, 10.0f));

		if (filter.sharpness != value) {
			filter.sharpness = value;
			update = true;
		}

		LOG(RkISP1Filter, Debug) << "Set sharpness to " << *sharpness;
	}

	const auto &denoise = controls.get(controls::draft::NoiseReductionMode);
	if (denoise) {
		switch (*denoise) {
		case controls::draft::NoiseReductionModeOff:
			if (filter.denoise) {
				filter.denoise = false;
				update = true;
			}
			break;
		case controls::draft::NoiseReductionModeMinimal:
		case controls::draft::NoiseReductionModeHighQuality:
		case controls::draft::NoiseReductionModeFast:
		case controls::draft::NoiseReductionModeZSL:
			if (loadConfig(*denoise)) {
				update = true;
				filter.denoise = true;
			}
			break;
		default:
			LOG(RkISP1Filter, Error)
				<< "Unsupported denoise value "
				<< *denoise;
			break;
		}
		if (update)
			LOG(RkISP1Filter, Debug) << "Set denoise to " << modeName(*denoise);
	}

	frameContext.filter.denoise = filter.denoise;
	frameContext.filter.sharpness = filter.sharpness;
	frameContext.filter.update = update;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Filter::prepare([[maybe_unused]] IPAContext &context,
		     const uint32_t frame,
		     IPAFrameContext &frameContext, RkISP1Params *params)
{
	/* Check if the algorithm configuration has been updated. */
	if (!frameContext.filter.update)
		return;

	if (!frameContext.filter.denoise && !frameContext.filter.sharpness) {
		prepareDisabledMode(params);
		return;
	}

	prepareEnabledMode(frame, frameContext, params);
}

void Filter::prepareDisabledMode(RkISP1Params *params)
{
	auto config = params->block<BlockType::Flt>();
	config.setEnabled(false);
}

void Filter::prepareEnabledMode(const uint32_t frame,
				IPAFrameContext &frameContext,
				RkISP1Params *params)
{
	/* Ensure we have a valid mode configuration */
	if (activeMode_ == noiseReductionModes_.end() && frameContext.filter.sharpness == 0)
		return;

	const ModeConfig &modeConfig = *activeMode_;

	auto config = params->block<BlockType::Flt>();
	config.setEnabled(true);

	/* Load the base configuration from the active noise reduction mode */
	*config = modeConfig.config;

	/*
	 * Apply sharpness override if configured.
	 * Sharpness control modulates the sharpening factors on top of
	 * the base noise reduction mode configuration.
	 */
	uint8_t sharpness = frameContext.filter.sharpness;
	if (sharpness > 0 && sharpness < std::size(kSharpnessPresets)) {
		const SharpnessPreset &preset = kSharpnessPresets[sharpness];
		config->fac_sh0 = preset.fac_sh0;
		config->fac_sh1 = preset.fac_sh1;
		config->fac_mid = preset.fac_mid;
		config->fac_bl0 = preset.fac_bl0;
		config->fac_bl1 = preset.fac_bl1;

		/* Set mode to default if no active mode is configured */
		if (activeMode_ == noiseReductionModes_.end())
			config->mode = kFiltModeDefault;
	}

	if (frameContext.filter.update || frame == 0)
		logConfig(*config);
}

void Filter::logConfig(const struct rkisp1_cif_isp_flt_config &config)
{
	LOG(RkISP1Filter, Debug)
		<< "Filter config: mode=" << config.mode
		<< " lum_weight=" << config.lum_weight
		<< " grn_stage1=" << (int)config.grn_stage1
		<< " chr_h_mode=" << (int)config.chr_h_mode
		<< " chr_v_mode=" << (int)config.chr_v_mode
		<< " thresh_bl0=" << config.thresh_bl0
		<< " thresh_bl1=" << config.thresh_bl1
		<< " thresh_sh0=" << config.thresh_sh0
		<< " thresh_sh1=" << config.thresh_sh1
		<< " fac_sh1=" << config.fac_sh1
		<< " fac_sh0=" << config.fac_sh0
		<< " fac_mid=" << config.fac_mid
		<< " fac_bl0=" << config.fac_bl0
		<< " fac_bl1=" << config.fac_bl1;
}

REGISTER_IPA_ALGORITHM(Filter, "Filter")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
