/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Filter control
 */

#include "filter.h"

#include <cmath>
#include <unordered_set>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

/**
 * \file filter.h
 */

namespace libcamera {

namespace ipa::rkisp1::algorithms {

/**
 * \class Filter
 * \brief RkISP1 Filter control
 *
 * Denoise and Sharpness filters will be applied by RkISP1 during the
 * demosaicing step. The denoise filter is responsible for removing noise from
 * the image, while the sharpness filter will enhance its acutance.
 *
 * \todo In current version the denoise and sharpness control is based on user
 * controls. In a future version it should be controlled automatically by the
 * algorithm.
 */

LOG_DEFINE_CATEGORY(RkISP1Filter)

static constexpr uint32_t kFiltLumWeightDefault = 0x00022040;
static constexpr uint32_t kFiltModeDefault = 0x000004f2;

namespace {
const std::unordered_set<std::string> kSharpnessKeyNames = {
	"fac_sh0", "fac_sh1", "fac_mid", "fac_bl0", "fac_bl1"
};

const std::unordered_set<std::string> kFilterKeyNames = {
	"thresh_sh0", "thresh_sh1", "thresh_bl0", "thresh_bl1",
	"mode", "lum_weight", "grn_stage1", "chr_v_mode", "chr_h_mode",
	"fac_sh0", "fac_sh1", "fac_mid", "fac_bl0", "fac_bl1"
};
} /* namespace */

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Filter::init(IPAContext &context,
		 const YamlObject &tuningData)
{
	int ret = parseConfig(tuningData);
	if (ret)
		return ret;

	registerControls(context);

	return 0;
}

int Filter::parseConfig(const YamlObject &tuningData)
{
	if (!tuningData.contains("NoiseReductionModes")) {
		LOG(RkISP1Filter, Error) << "Missing NoiseReductionModes in Filter tuning data";
		return -EINVAL;
	}

	const YamlObject &modesObject = tuningData["NoiseReductionModes"];
	if (!modesObject.isDictionary()) {
		LOG(RkISP1Filter, Error) << "NoiseReductionModes must be a dictionary";
		return -EINVAL;
	}

	for (const auto &[modeName, modeData] : modesObject.asDict()) {
		auto it = controls::draft::NoiseReductionModeNameValueMap.find(modeName);
		if (it == controls::draft::NoiseReductionModeNameValueMap.end()) {
			LOG(RkISP1Filter, Error) << "Unknown mode: " << modeName;
			return -EINVAL;
		}

		int ret = parseModeConfig(modeData, modes_[it->second]);
		if (ret)
			return ret;
	}

	if (!tuningData.contains("Sharpness")) {
		LOG(RkISP1Filter, Error) << "Missing Sharpness in Filter tuning data";
		return -EINVAL;
	}

	const YamlObject &sharpnessObject = tuningData["Sharpness"];
	if (!sharpnessObject.isList()) {
		LOG(RkISP1Filter, Error) << "Sharpness must be a list";
		return -EINVAL;
	}

	const size_t sharpnessLevels = sharpnessObject.size();
	if (!sharpnessLevels) {
		LOG(RkISP1Filter, Error) << "Sharpness must not be empty";
		return -EINVAL;
	}

	sharpness_.assign(sharpnessLevels, {});

	for (size_t i = 0; i < sharpnessLevels; i++) {
		int ret = parseSharpnessConfig(sharpnessObject[i], sharpness_[i]);
		if (ret)
			return ret;
	}

	return 0;
}

int Filter::parseModeConfig(const YamlObject &modeData,
			    std::unordered_map<std::string, uint32_t> &modeParams)
{
	if (!modeData.isList()) {
		LOG(RkISP1Filter, Error) << "Mode config must be a list";
		return -EINVAL;
	}

	for (const auto &entry : modeData.asList()) {
		for (const auto &[key, val] : entry.asDict()) {
			if (kFilterKeyNames.find(key) == kFilterKeyNames.end()) {
				LOG(RkISP1Filter, Error)
					<< "Unknown mode key '" << key << "'";
				return -EINVAL;
			}

			auto v = val.get<uint32_t>();
			if (!v) {
				LOG(RkISP1Filter, Error)
					<< "Invalid value for key '" << key << "'";
				return -EINVAL;
			}
			modeParams[key] = *v;
		}
	}

	return 0;
}

int Filter::parseSharpnessConfig(const YamlObject &data,
				 std::unordered_map<std::string, uint32_t> &sharpParams)
{
	if (!data.isList()) {
		LOG(RkISP1Filter, Error) << "Sharpness entry must be a list";
		return -EINVAL;
	}

	sharpParams.clear();

	for (const auto &entry : data.asList()) {
		for (const auto &[key, val] : entry.asDict()) {
			if (kSharpnessKeyNames.find(key) == kSharpnessKeyNames.end()) {
				LOG(RkISP1Filter, Error)
					<< "Unknown sharpness key '" << key << "'";
				return -EINVAL;
			}

			auto v = val.get<uint32_t>();
			if (!v) {
				LOG(RkISP1Filter, Error)
					<< "Invalid value for key '" << key << "'";
				return -EINVAL;
			}

			sharpParams[key] = *v;
		}
	}

	return 0;
}

void Filter::registerControls([[maybe_unused]] IPAContext &context)
{
	auto &cmap = context.ctrlMap;

	if (modes_.empty() or sharpness_.empty()) {
		LOG(RkISP1Filter, Warning)
			<< "No NoiseReductionModes or Sharpness levels parsed, filter controls not registered";
		return;
	}

	/* Register sharpness control value from tuning data size*/
	const size_t sharpnessLevels = sharpness_.size();
	cmap[&controls::Sharpness] = ControlInfo(0.0f, static_cast<float>(sharpnessLevels - 1), 1.0f);

	const auto manualMode = static_cast<int32_t>(controls::draft::NoiseReductionModeManual);
	auto selectedMode = modes_.find(manualMode);
	if (selectedMode == modes_.end())
		selectedMode = modes_.begin();

	const auto &modeParams = selectedMode->second;

	LOG(RkISP1Filter, Debug)
		<< "Initial NoiseReductionMode set to " << selectedMode->first;

	/* Register rkisp1-specific filter control values. */
	cmap[&controls::rkisp1::FilterDenoiseMode] =
		ControlInfo(0, 255, static_cast<int32_t>(modeParams.at("mode")));
	cmap[&controls::rkisp1::FilterDenoiseLumWeight] =
		ControlInfo(0, 255, static_cast<int32_t>(modeParams.at("lum_weight")));
	cmap[&controls::rkisp1::FilterDenoiseGreenStage1] =
		ControlInfo(0, 7, static_cast<int32_t>(modeParams.at("grn_stage1")));
	cmap[&controls::rkisp1::FilterDenoiseChrVMode] =
		ControlInfo(0, 3, static_cast<int32_t>(modeParams.at("chr_v_mode")));
	cmap[&controls::rkisp1::FilterDenoiseChrHMode] =
		ControlInfo(0, 3, static_cast<int32_t>(modeParams.at("chr_h_mode")));
	cmap[&controls::rkisp1::FilterFacSh0] =
		ControlInfo(0, 63, static_cast<int32_t>(modeParams.at("fac_sh0")));
	cmap[&controls::rkisp1::FilterFacSh1] =
		ControlInfo(0, 63, static_cast<int32_t>(modeParams.at("fac_sh1")));
	cmap[&controls::rkisp1::FilterFacMid] =
		ControlInfo(0, 63, static_cast<int32_t>(modeParams.at("fac_mid")));
	cmap[&controls::rkisp1::FilterFacBl0] =
		ControlInfo(0, 63, static_cast<int32_t>(modeParams.at("fac_bl0")));
	cmap[&controls::rkisp1::FilterFacBl1] =
		ControlInfo(0, 63, static_cast<int32_t>(modeParams.at("fac_bl1")));
	cmap[&controls::rkisp1::FilterThreshSh0] =
		ControlInfo(0, 255, static_cast<int32_t>(modeParams.at("thresh_sh0")));
	cmap[&controls::rkisp1::FilterThreshSh1] =
		ControlInfo(0, 255, static_cast<int32_t>(modeParams.at("thresh_sh1")));
	cmap[&controls::rkisp1::FilterThreshBl0] =
		ControlInfo(0, 255, static_cast<int32_t>(modeParams.at("thresh_bl0")));
	cmap[&controls::rkisp1::FilterThreshBl1] =
		ControlInfo(0, 255, static_cast<int32_t>(modeParams.at("thresh_bl1")));
}

bool Filter::parseControls(const ControlList &controls)
{
	bool updated = false;
	constexpr auto manual = controls::draft::NoiseReductionModeManual;
	auto &params = modes_[manual];

	if (const auto &c = controls.get(controls::rkisp1::FilterFacSh0); c) {
		params["fac_sh0"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterFacSh1); c) {
		params["fac_sh1"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterFacMid); c) {
		params["fac_mid"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterFacBl0); c) {
		params["fac_bl0"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterFacBl1); c) {
		params["fac_bl1"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterThreshSh0); c) {
		params["thresh_sh0"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterThreshSh1); c) {
		params["thresh_sh1"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterThreshBl0); c) {
		params["thresh_bl0"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterThreshBl1); c) {
		params["thresh_bl1"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterDenoiseMode); c) {
		params["mode"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterDenoiseLumWeight); c) {
		params["lum_weight"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterDenoiseGreenStage1); c) {
		params["grn_stage1"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterDenoiseChrVMode); c) {
		params["chr_v_mode"] = *c;
		updated = true;
	}

	if (const auto &c = controls.get(controls::rkisp1::FilterDenoiseChrHMode); c) {
		params["chr_h_mode"] = *c;
		updated = true;
	}

	return updated;
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

	const auto &denoise = controls.get(controls::draft::NoiseReductionMode);
	if (denoise) {
		LOG(RkISP1Filter, Debug) << "Set denoise to " << *denoise;

		uint8_t value = static_cast<uint8_t>(*denoise);
		if (filter.denoise != value) {
			filter.denoise = value;
			update = true;
		}
	}

	if (filter.denoise == controls::draft::NoiseReductionModeManual) {
		update |= parseControls(controls);
	} else {
		const auto &sharpness = controls.get(controls::Sharpness);
		if (sharpness) {
			unsigned int value = std::round(std::clamp(*sharpness, 0.0f, static_cast<float>(sharpness_.size() - 1)));

			if (filter.sharpness != value) {
				filter.sharpness = value;
				update = true;
			}

			LOG(RkISP1Filter, Debug) << "Set sharpness to " << *sharpness;
		}
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
	if (!frameContext.filter.update && frame > 0)
		return;

	uint8_t denoise = frameContext.filter.denoise;
	uint8_t sharpness = frameContext.filter.sharpness;
	auto config = params->block<BlockType::Flt>();
	if (denoise == controls::draft::NoiseReductionModeOff) {
		config.setEnabled(false);
		return;
	}
	config.setEnabled(true);

	auto it = modes_.find(denoise);
	if (it == modes_.end()) {
		LOG(RkISP1Filter, Warning) << "No filter config for mode " << denoise;
		return;
	}
	auto &modeParams = it->second;

	config->thresh_sh0 = modeParams["thresh_sh0"];
	config->thresh_sh1 = modeParams["thresh_sh1"];
	config->thresh_bl0 = modeParams["thresh_bl0"];
	config->thresh_bl1 = modeParams["thresh_bl1"];
	config->mode = modeParams["mode"];
	config->lum_weight = modeParams["lum_weight"];
	config->grn_stage1 = modeParams["grn_stage1"];
	config->chr_v_mode = modeParams["chr_v_mode"];
	config->chr_h_mode = modeParams["chr_h_mode"];
	config->fac_sh0 = modeParams["fac_sh0"];
	config->fac_sh1 = modeParams["fac_sh1"];
	config->fac_mid = modeParams["fac_mid"];
	config->fac_bl0 = modeParams["fac_bl0"];
	config->fac_bl1 = modeParams["fac_bl1"];

	if (sharpness == 0 or sharpness >= sharpness_.size()) {
		LOG(RkISP1Filter, Debug)
			<< "Sharpness value out of range: " << static_cast<int>(sharpness);
		return;
	}

	if (denoise == controls::draft::NoiseReductionModeManual)
		return;

	/* sharpness override filter register .*/
	const auto &sharp = sharpness_[sharpness];
	config->fac_sh0 = sharp.at("fac_sh0");
	config->fac_sh1 = sharp.at("fac_sh1");
	config->fac_mid = sharp.at("fac_mid");
	config->fac_bl0 = sharp.at("fac_bl0");
	config->fac_bl1 = sharp.at("fac_bl1");
}

REGISTER_IPA_ALGORITHM(Filter, "Filter")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
