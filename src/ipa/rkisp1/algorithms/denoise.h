/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board
 *
 * RkISP1 Denoising Algorithms Base Class
 */

#pragma once

#include "../ipa_context.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa::rkisp1::algorithms {

class DenoiseBaseAlgorithm : public ipa::rkisp1::Algorithm
{
protected:
	DenoiseBaseAlgorithm() = default;
	~DenoiseBaseAlgorithm() = default;
	virtual void setDevMode(bool dev) { devMode_ = dev; }
	virtual bool isDevMode() const { return devMode_; }
	virtual uint32_t computeExposureIndex(const IPAContext &context,
					      const IPAFrameContext &frameContext) const;
	template<typename LevelContainer>
	uint32_t selectExposureIndexBand(unsigned exposureIndex,
					 const LevelContainer &levels) const;
	virtual bool parseConfig([[maybe_unused]] const YamlObject &tuningData)
	{
		return true;
	}

private:
	/**< Developer mode state for advanced controls */
	bool devMode_ = false;
};

inline unsigned DenoiseBaseAlgorithm::computeExposureIndex(const IPAContext &context,
							   const IPAFrameContext &frameContext) const
{
	double ag = frameContext.agc.gain ? frameContext.agc.gain
					  : context.activeState.agc.automatic.gain;
	return static_cast<unsigned>(ag * 100.0 + 0.5);
}

template<typename LevelContainer>
uint32_t DenoiseBaseAlgorithm::selectExposureIndexBand(unsigned exposureIndex,
						       const LevelContainer &levels) const
{
	if (levels.empty())
		return -1;
	uint32_t idx = 0;
	while (idx < static_cast<uint32_t>(levels.size()) && exposureIndex > levels[idx].maxExposureIndex)
		++idx;
	if (idx >= static_cast<uint32_t>(levels.size()))
		idx = static_cast<uint32_t>(levels.size()) - 1;
	return idx;
}

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
