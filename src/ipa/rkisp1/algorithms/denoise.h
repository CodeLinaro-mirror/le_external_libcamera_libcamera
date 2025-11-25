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
	virtual void collectManualOverrides([[maybe_unused]] const ControlList &controls)
	{
	}
	virtual bool processModeChange([[maybe_unused]] const ControlList &controls,
				       [[maybe_unused]] uint32_t currentFrame)
	{
		return false;
	}
	virtual void snapshotCurrentToOverrides()
	{
	}
	virtual void restoreAutoConfig([[maybe_unused]] IPAContext &context, [[maybe_unused]] IPAFrameContext &frameContext)
	{
	}
	virtual void handleReductionModeControl([[maybe_unused]] const ControlList &controls,
						[[maybe_unused]] IPAFrameContext &frameContext,
						[[maybe_unused]] IPAContext &context,
						[[maybe_unused]] uint32_t frame)
	{
	}
	virtual void handleDisableMode([[maybe_unused]] IPAFrameContext &frameContext,
				       [[maybe_unused]] IPAContext &context)
	{
	}
	virtual int32_t getRunningMode() const { return currentRunMode_; }
	virtual void setRunningMode(int32_t mode) { currentRunMode_ = mode; }
	virtual void prepareDisabledMode([[maybe_unused]] IPAContext &context,
					 [[maybe_unused]] const uint32_t frame,
					 [[maybe_unused]] IPAFrameContext &frameContext,
					 [[maybe_unused]] RkISP1Params *params)
	{
	}
	virtual void prepareEnabledMode([[maybe_unused]] IPAContext &context,
					[[maybe_unused]] const uint32_t frame,
					[[maybe_unused]] IPAFrameContext &frameContext,
					[[maybe_unused]] RkISP1Params *params)
	{
	}
	virtual ControlInfoMap::Map getControlMap() const;
	virtual void fillMetadata(IPAContext &context,
				  IPAFrameContext &frameContext,
				  ControlList &metadata);

private:
	/**< Developer mode state for advanced controls */
	bool devMode_ = false;
	/**< Current denoise running mode */
	int32_t currentRunMode_ = controls::rkisp1::DenoiseModeDisabled;
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

inline ControlInfoMap::Map DenoiseBaseAlgorithm::getControlMap() const
{
	ControlInfoMap::Map map;
	map[&controls::rkisp1::DenoiseMode] =
		ControlInfo(controls::rkisp1::DenoiseModeValues);
	map[&controls::rkisp1::ExposureGainIndex] = ControlInfo(0, 6400, 0);
	return map;
}

inline void DenoiseBaseAlgorithm::fillMetadata(IPAContext &context,
					       IPAFrameContext &frameContext,
					       ControlList &metadata)
{
	uint32_t exposureIndex = computeExposureIndex(context, frameContext);
	metadata.set(controls::rkisp1::ExposureGainIndex, static_cast<int32_t>(exposureIndex));

	auto currentMode = getRunningMode();
	metadata.set(controls::rkisp1::DenoiseMode, currentMode);
}

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
