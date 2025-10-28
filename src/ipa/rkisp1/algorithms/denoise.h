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
	struct EnableState {
		bool enabled = true; /**< Current enable state */
		bool lastEnabled = true; /**< Previous enable state for change detection */
	};
	bool processEnableToggle(bool value, EnableState &state);

	void setManualMode(bool manual) { manualMode_ = manual; }

	void setDevMode(bool dev) { devMode_ = dev; }

	bool isManualMode() const { return manualMode_; }
	bool isDevMode() const { return devMode_; }
	unsigned computeIso(const IPAContext &context,
			    const IPAFrameContext &frameContext) const;
	template<typename LevelContainer>
	int selectIsoBand(unsigned iso, const LevelContainer &levels) const;
	virtual bool parseConfig(const YamlObject &tuningData) = 0;
	virtual void handleEnableControl(const ControlList &controls, IPAFrameContext &frameContext, IPAContext &context) = 0;
	virtual void collectManualOverrides(const ControlList &controls) = 0;

private:
	bool manualMode_ = false; /**< Current manual/auto mode state */
	bool devMode_ = false; /**< Developer mode state for advanced controls */
};

inline bool DenoiseBaseAlgorithm::processEnableToggle(bool value, EnableState &state)
{
	state.lastEnabled = state.enabled;
	state.enabled = value;
	return state.enabled != state.lastEnabled;
}

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
