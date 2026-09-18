/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Lens Shading Correction control
 */

#include "lsc.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <string.h>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include "libcamera/internal/value_node.h"

/**
 * \file lsc.h
 */

namespace libcamera {

namespace ipa::rppx1::algorithms {

LOG_DEFINE_CATEGORY(RppX1Lsc)

namespace {

constexpr int kColourTemperatureQuantization = 10;

std::vector<double> parseSizes(const ValueNode &tuningData,
			       const char *prop)
{
	std::vector<double> sizes =
		tuningData[prop].get<std::vector<double>>().value_or(utils::defopt);
	if (sizes.size() != RPPX1_LSC_NUM_SECTORS) {
		LOG(RppX1Lsc, Error)
			<< "Invalid '" << prop << "' values: expected "
			<< RPPX1_LSC_NUM_SECTORS
			<< " elements, got " << sizes.size();
		return {};
	}

	/*
	 * The sum of all elements must satisfy hardware constraints.
	 * Validate it here, allowing a 1% tolerance as rounding errors may
	 * prevent an exact match (further adjustments will be performed in
	 * LensShadingCorrection::prepare()).
	 */
	constexpr double expectedSum = 1.0;

	double sum = std::accumulate(sizes.begin(), sizes.end(), 0.0);
	if (std::abs(sum - expectedSum) > 0.01) {
		LOG(RppX1Lsc, Error)
			<< "Invalid '" << prop << "' values: sum of the elements"
			<< " should be " << expectedSum << ", got " << sum;
		return {};
	}

	const auto &[min, max] = std::minmax_element(sizes.begin(), sizes.end());
	if (*min <= 0 || *max > 1) {
		LOG(RppX1Lsc, Error)
			<< "Invalid '" << prop << "' values: elements must be in (0;1]";
		return {};
	}

	return sizes;
}

std::vector<double> sizesListToPositions(std::span<const double> sizes)
{
	std::vector<double> positions(sizes.size() + 1);

	positions[0] = 0;
	for (size_t i = 0; i < sizes.size(); i++)
		positions[i + 1] = positions[i] + sizes[i];

	return positions;
}

unsigned int quantize(unsigned int value, unsigned int step)
{
	return std::lround(value / static_cast<double>(step)) * step;
}

} /* namespace */

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int LensShadingCorrection::init([[maybe_unused]] IPAContext &context,
				const ValueNode &tuningData)
{
	xSize_ = parseSizes(tuningData, "x-size");
	ySize_ = parseSizes(tuningData, "y-size");

	if (xSize_.empty() || ySize_.empty())
		return -EINVAL;

	xPos_ = sizesListToPositions(xSize_);
	yPos_ = sizesListToPositions(ySize_);

	return lscAlgo_.init(tuningData,  context.ctrlMap, {
				.keys = { "r", "gr", "gb", "b" },
				.numHSamples = RPPX1_LSC_SAMPLES_MAX,
				.numVSamples = RPPX1_LSC_SAMPLES_MAX,
				.sensorSize = context.sensorInfo.activeAreaSize
			     });
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int LensShadingCorrection::configure(IPAContext &context,
				     const IPACameraSensorInfo &configInfo)
{
	const Size &size = configInfo.outputSize;
	Size totalSize{};

	for (unsigned int i = 0; i < RPPX1_LSC_NUM_SECTORS; ++i) {
		xSizes_[i] = xSize_[i] * size.width;
		ySizes_[i] = ySize_[i] * size.height;

		/*
		 * > The sum of all 16 {x,y}-size values must exactly match
		 * > image {width,height}.
		 *
		 * Enforce it by computing the last table value separately to
		 * avoid rounding-induced errors.
		 */
		if (i == RPPX1_LSC_NUM_SECTORS - 1) {
			xSizes_[i] = size.width - totalSize.width;
			ySizes_[i] = size.height - totalSize.height;
		}

		LOG(RppX1Lsc, Debug)
			<< i << ": " << xSizes_[i] << "=" << size.width << '*' << xSize_[i] << ", "
			<< ySizes_[i] << "=" << size.height << '*' << ySize_[i];

		/*
		 * > The sector size in {x,y}-direction must be greater than 7.
		 */
		if (xSizes_[i] < 8 || ySizes_[i] < 8)
			return -EINVAL;
		if (xSizes_[i] > 1023 || ySizes_[i] > 1023)
			return -EINVAL;

		totalSize.width += xSizes_[i];
		totalSize.height += ySizes_[i];

		/*
		 * > The gradient values are related to the sector sizes and can be
		 * > calculated for both x and y direction using the following equation:
		 * >   gradient[i] = int(2^15 / size[i] + 1/2) where i in [0;15]
		 */
		xGrad_[i] = std::lround((1u << 15) / xSizes_[i]);
		yGrad_[i] = std::lround((1u << 15) / ySizes_[i]);
	}

	lastAppliedCt_ = 0;
	lastAppliedQuantizedCt_ = 0;

	return lscAlgo_.configure(context.activeState.lsc, configInfo.analogCrop,
				  xPos_, yPos_);
}

void LensShadingCorrection::setParameters(rppx1_lsc_params &config)
{
	static_assert(sizeof(config.x_sect_size) == sizeof(xSizes_));
	memcpy(config.x_sect_size, xSizes_, sizeof(xSizes_));

	static_assert(sizeof(config.y_sect_size) == sizeof(ySizes_));
	memcpy(config.y_sect_size, ySizes_, sizeof(ySizes_));

	static_assert(sizeof(config.x_grad) == sizeof(xGrad_));
	memcpy(config.x_grad, xGrad_, sizeof(xGrad_));

	static_assert(sizeof(config.y_grad) == sizeof(yGrad_));
	memcpy(config.y_grad, yGrad_, sizeof(yGrad_));
}

void LensShadingCorrection::copyTable(rppx1_lsc_params &config,
				      const lsc::Components<uint16_t> &set)
{
	const auto &r = set.at("r");
	std::copy(r.begin(), r.end(), &config.r_data[0][0]);
	const auto &gr = set.at("gr");
	std::copy(gr.begin(), gr.end(), &config.gr_data[0][0]);
	const auto &gb = set.at("gb");
	std::copy(gb.begin(), gb.end(), &config.gb_data[0][0]);
	const auto &b = set.at("b");
	std::copy(b.begin(), b.end(), &config.b_data[0][0]);
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void LensShadingCorrection::queueRequest(IPAContext &context,
					 [[maybe_unused]] const uint32_t frame,
					 IPAFrameContext &frameContext,
					 const ControlList &controls)
{
	lscAlgo_.queueRequest(context.activeState.lsc, frameContext.lsc,
			      controls);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void LensShadingCorrection::prepare([[maybe_unused]] IPAContext &context,
				    [[maybe_unused]] const uint32_t frame,
				    IPAFrameContext &frameContext,
				    RppX1Params *params)
{
	uint32_t ct = frameContext.awb.colourTemperature;
	unsigned int quantizedCt = quantize(ct, kColourTemperatureQuantization);

	/* Check if we can skip the update. */
	if (!frameContext.lsc.update) {
		if (!frameContext.lsc.enabled)
			return;

		/*
		 * Add a threshold so that oscillations around a quantization
		 * step don't lead to constant changes.
		 */
		if (utils::abs_diff(ct, lastAppliedCt_) < kColourTemperatureQuantization / 2)
			return;

		if (quantizedCt == lastAppliedQuantizedCt_)
			return;
	}

	auto config = params->block<BlockType::LscPre1>();
	config.setEnabled(frameContext.lsc.enabled);

	if (!frameContext.lsc.enabled)
		return;

	setParameters(*config);

	copyTable(*config, lscAlgo_.interpolateComponents(quantizedCt));

	lastAppliedCt_ = ct;
	lastAppliedQuantizedCt_ = quantizedCt;

	LOG(RppX1Lsc, Debug)
		<< "ct is " << ct << ", quantized to "
		<< quantizedCt;
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void LensShadingCorrection::process([[maybe_unused]] IPAContext &context,
				    [[maybe_unused]] const uint32_t frame,
				    IPAFrameContext &frameContext,
				    [[maybe_unused]] const RppX1Stats *stats,
				    ControlList &metadata)
{
	lscAlgo_.process(frameContext.lsc, metadata);
}

REGISTER_IPA_ALGORITHM(LensShadingCorrection, "LensShadingCorrection")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
