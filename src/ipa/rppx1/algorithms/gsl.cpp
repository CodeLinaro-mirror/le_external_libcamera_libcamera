/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Gamma Sensor Linearization control
 */

#include "gsl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include "libcamera/internal/value_node.h"

/**
 * \file gsl.h
 */

namespace libcamera {

namespace ipa::rppx1::algorithms {

LOG_DEFINE_CATEGORY(RppX1Gsl)

/* `LinPre1` only */
constexpr uint32_t kDxOffset = 8;
constexpr uint32_t kMaxDx = (1u << 4) - 1;
constexpr uint32_t kResolution = 1u << 24;

namespace {

[[nodiscard]]
bool
parseYLut(const ValueNode &yObject, std::string_view key, uint32_t scale,
	  std::span<uint32_t, RPPX1_LIN_DEGAMMA_CURVE_NUM> res)
{
	auto vals = yObject[key].get<std::vector<float>>().value_or(utils::defopt);
	if (vals.size() != res.size()) {
		LOG(RppX1Gsl, Error)
			<< "Invalid 'y:" << key << "': expected "
			<< res.size() << " elements, got " << vals.size();
		return false;
	}

	for (const auto &[i, y] : utils::enumerate(vals)) {
		if (y < 0 || y > 1) {
			LOG(RppX1Gsl, Error)
				<< "Invalid 'y:" << key << "': elements must be in [0; 1]";
			return false;
		}

		res[i] = std::lround(y * scale);
	}

	return true;
}

} /* namespace */

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int GammaSensorLinearization::init([[maybe_unused]] IPAContext &context,
				   const ValueNode &tuningData)
{
	/*
	 * \todo `LinPre2` has different ranges:
	 *   * kDxOffset = 4
	 *   * kMaxDx = 7
	 *   * kResolution = 2^12
	 * Maybe this parameter could be reworked to be compatible with both.
	 */
	gammaDx_ = tuningData["x-intervals"].get<std::vector<uint8_t>>().value_or(utils::defopt);
	if (gammaDx_.size() != RPPX1_LIN_SAMPLE_POINTS_NUM) {
		LOG(RppX1Gsl, Error)
			<< "Invalid 'x-intervals': expected "
			<< RPPX1_LIN_SAMPLE_POINTS_NUM << " elements, got "
			<< gammaDx_.size();

		return -EINVAL;
	}

	uint32_t xSum = 0;
	for (const auto &x : gammaDx_) {
		if (x > kMaxDx) {
			LOG(RppX1Gsl, Error)
				<< "Invalid 'x-intervals': must be at most " << kMaxDx
				<< ", got " << x;
			return -EINVAL;
		}

		xSum += 1u << (x + kDxOffset);
	}

	/*
	 * > Typically, the accumulated sum of dx[i] should cover the complete data
	 * > input range of [0; 2^12) or [0; 2^24).
	 */
	if (xSum != kResolution) {
		LOG(RppX1Gsl, Error)
			<< "Invalid 'x-intervals': must cover full input range of " << kResolution
			<< ", got " << xSum;
		return -EINVAL;
	}

	const ValueNode &yObject = tuningData["y"];
	if (!yObject.isDictionary()) {
		LOG(RppX1Gsl, Error)
			<< "Invalid 'y': must be a dictionary";
		return -EINVAL;
	}

	if (!parseYLut(yObject, "red", kResolution - 1, curveYr_) ||
	    !parseYLut(yObject, "green", kResolution - 1, curveYg_) ||
	    !parseYLut(yObject, "blue", kResolution - 1, curveYb_))
		return -EINVAL;

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void GammaSensorLinearization::prepare([[maybe_unused]] IPAContext &context,
				       const uint32_t frame,
				       [[maybe_unused]] IPAFrameContext &frameContext,
				       RppX1Params *params)
{
	if (frame > 0)
		return;

	auto config = params->block<BlockType::LinPre1>();
	config.setEnabled(true);

	std::copy(gammaDx_.begin(), gammaDx_.end(), config->dx);
	std::copy(curveYr_.begin(), curveYr_.end(), config->curve_r);
	std::copy(curveYg_.begin(), curveYg_.end(), config->curve_g);
	std::copy(curveYb_.begin(), curveYb_.end(), config->curve_b);
}

REGISTER_IPA_ALGORITHM(GammaSensorLinearization, "GammaSensorLinearization")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
