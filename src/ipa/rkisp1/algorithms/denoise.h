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

/**
 * \class DenoiseBaseAlgorithm
 * \brief Base class for RkISP1 denoising algorithms
 *
 * This abstract base class provides common functionality for denoising algorithms
 * in the RkISP1 Image Processing Algorithm (IPA).
 *
 * Derived classes must implement algorithm-specific behavior.
 */
class DenoiseBaseAlgorithm : public ipa::rkisp1::Algorithm
{
protected:
	DenoiseBaseAlgorithm() = default;
	~DenoiseBaseAlgorithm() = default;

	unsigned computeIso(const IPAContext &context,
			    const IPAFrameContext &frameContext) const;
	template<typename LevelContainer>
	int selectIsoBand(unsigned iso, const LevelContainer &levels) const;
};

inline unsigned DenoiseBaseAlgorithm::computeIso(const IPAContext &context,
						 const IPAFrameContext &frameContext) const
{
	double ag = frameContext.agc.gain ? frameContext.agc.gain
					  : context.activeState.agc.automatic.gain;
	return static_cast<unsigned>(ag * 100.0 + 0.5);
}

template<typename LevelContainer>
int DenoiseBaseAlgorithm::selectIsoBand(unsigned iso, const LevelContainer &levels) const
{
	if (levels.empty())
		return -1;
	int idx = 0;
	while (idx < static_cast<int>(levels.size()) && iso > levels[idx].maxIso)
		++idx;
	if (idx >= static_cast<int>(levels.size()))
		idx = static_cast<int>(levels.size()) - 1;
	return idx;
}

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
