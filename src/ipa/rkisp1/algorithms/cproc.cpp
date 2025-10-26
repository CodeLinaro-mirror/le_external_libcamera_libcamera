/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Color Processing control
 */

#include "cproc.h"

#include <algorithm>
#include <cmath>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "libipa/quantized.h"

/**
 * \file cproc.h
 */

namespace libcamera {

namespace ipa::rkisp1::algorithms {

/**
 * \class ColorProcessing
 * \brief RkISP1 Color Processing control
 *
 * The ColorProcessing algorithm is responsible for applying brightness,
 * contrast and saturation corrections. The values are directly provided
 * through requests by the corresponding controls.
 */

LOG_DEFINE_CATEGORY(RkISP1CProc)

namespace {

constexpr float kDefaultBrightness = 0.0f;
constexpr float kDefaultContrast = 1.0f;
constexpr float kDefaultHue = 0.0f;
constexpr float kDefaultSaturation = 1.0f;

class BrightnessQ : public Quantizer<int8_t>
{
public:
	BrightnessQ(const float val) { set(val); }
	BrightnessQ(const int8_t val) { set(val); }

	int8_t fromFloat(float v) const override
	{
		int quantized = std::lround(v * 128.0f);
		return static_cast<int8_t>(std::clamp<int>(quantized, -128, 127));
	}

	float toFloat(int8_t v) const override
	{
		return static_cast<float>(v) / 128.0f;
	}
};

class HueQ : public Quantizer<int8_t>
{
public:
	HueQ(float val) { set(val); }
	HueQ(int8_t val) { set(val); }

	static constexpr float MaxDegrees = 90;
	static constexpr float q = 128.0f / MaxDegrees;

	int8_t fromFloat(float v) const override
	{
		int quantized = std::lround(v * q);
		return static_cast<int8_t>(std::clamp<int>(quantized, -128, 127));
	}

	float toFloat(int8_t v) const override
	{
		return static_cast<float>(v) / q;
	}
};

class ContrastSaturationQuantizer : public Quantizer<uint8_t>
{
public:
	ContrastSaturationQuantizer(const float val) { set(val); }
	ContrastSaturationQuantizer(const uint8_t val) { set(val); }

	uint8_t fromFloat(const float v) const override
	{
		int quantized = std::lround(v * 128.0f);
		return static_cast<uint8_t>(std::clamp<int>(quantized, 0, 255));
	}

	float toFloat(const uint8_t v) const override
	{
		return static_cast<float>(v) / 128.0f;
	}
};

using ContrastQ = ContrastSaturationQuantizer;
using SaturationQ = ContrastSaturationQuantizer;

} /* namespace */

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int ColorProcessing::init(IPAContext &context,
			  [[maybe_unused]] const YamlObject &tuningData)
{
	auto &cmap = context.ctrlMap;

	cmap[&controls::Brightness] = ControlInfo(-1.0f, 0.993f, kDefaultBrightness);
	cmap[&controls::Contrast] = ControlInfo(0.0f, 1.993f, kDefaultContrast);
	cmap[&controls::Hue] = ControlInfo(-90.0f, 90.0f, kDefaultHue);
	cmap[&controls::Saturation] = ControlInfo(0.0f, 1.993f, kDefaultSaturation);

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int ColorProcessing::configure(IPAContext &context,
			       [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	auto &cproc = context.activeState.cproc;

	cproc.brightness = BrightnessQ(kDefaultBrightness);
	cproc.contrast = ContrastQ(kDefaultContrast);
	cproc.hue = HueQ(kDefaultHue);
	cproc.saturation = SaturationQ(kDefaultSaturation);

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void ColorProcessing::queueRequest(IPAContext &context,
				   const uint32_t frame,
				   IPAFrameContext &frameContext,
				   const ControlList &controls)
{
	auto &cproc = context.activeState.cproc;
	bool update = false;

	if (frame == 0)
		update = true;

	const auto &brightness = controls.get(controls::Brightness);
	if (brightness) {
		BrightnessQ value = *brightness;
		if (cproc.brightness != value) {
			cproc.brightness = value;
			update = true;
		}

		LOG(RkISP1CProc, Debug) << "Set brightness to " << value.value();
	}

	const auto &contrast = controls.get(controls::Contrast);
	if (contrast) {
		ContrastQ value = *contrast;
		if (cproc.contrast != value) {
			cproc.contrast = value;
			update = true;
		}

		LOG(RkISP1CProc, Debug) << "Set contrast to " << value.value();
	}

	const auto &hue = controls.get(controls::Hue);
	if (hue) {
		HueQ value = *hue;
		if (cproc.hue != value) {
			cproc.hue = value;
			update = true;
		}

		LOG(RkISP1CProc, Debug) << "Set hue to " << value.value();
	}

	const auto saturation = controls.get(controls::Saturation);
	if (saturation) {
		SaturationQ value = *saturation;
		if (cproc.saturation != value) {
			cproc.saturation = value;
			update = true;
		}

		LOG(RkISP1CProc, Debug) << "Set saturation to " << value.value();
	}

	frameContext.cproc.brightness = cproc.brightness;
	frameContext.cproc.contrast = cproc.contrast;
	frameContext.cproc.hue = cproc.hue;
	frameContext.cproc.saturation = cproc.saturation;
	frameContext.cproc.update = update;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void ColorProcessing::prepare([[maybe_unused]] IPAContext &context,
			      [[maybe_unused]] const uint32_t frame,
			      IPAFrameContext &frameContext,
			      RkISP1Params *params)
{
	/* Check if the algorithm configuration has been updated. */
	if (!frameContext.cproc.update)
		return;

	auto config = params->block<BlockType::Cproc>();
	config.setEnabled(true);
	config->brightness = frameContext.cproc.brightness.quantized();
	config->contrast = frameContext.cproc.contrast.quantized();
	config->hue = frameContext.cproc.hue.quantized();
	config->sat = frameContext.cproc.saturation.quantized();
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void ColorProcessing::process([[maybe_unused]] IPAContext &context, [[maybe_unused]] const uint32_t frame,
			      IPAFrameContext &frameContext, [[maybe_unused]] const rkisp1_stat_buffer *stats,
			      ControlList &metadata)
{
	metadata.set(controls::Brightness, frameContext.cproc.brightness.value());
	metadata.set(controls::Contrast, frameContext.cproc.contrast.value());
	metadata.set(controls::Hue, frameContext.cproc.hue.value());
	metadata.set(controls::Saturation, frameContext.cproc.saturation.value());
}

REGISTER_IPA_ALGORITHM(ColorProcessing, "ColorProcessing")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
