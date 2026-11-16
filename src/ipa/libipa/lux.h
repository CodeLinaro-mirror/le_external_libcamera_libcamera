/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Helper class that implements lux estimation
 */

#pragma once

#include <optional>

#include <libcamera/base/utils.h>

namespace libcamera {

class ControlList;
class ValueNode;

namespace ipa {

class Histogram;

namespace lux {

struct ActiveState {
	double lux;
};

struct FrameContext {
	double lux;
};

} /* namespace lux */

class LuxAlgorithm
{
public:
	struct ProcessParams {
		utils::Duration exposureTime;
		double analogueGain;
		double digitalGain;
		const Histogram &yHist;
	};

	int init(const ValueNode &tuningData);
	void prepare(const lux::ActiveState &state, lux::FrameContext &frameContext);
	void process(lux::ActiveState &state, const lux::FrameContext &frameContext,
		     ControlList &metadata, std::optional<ProcessParams> &&params);

private:
	double estimateLux(const ProcessParams &params) const;

	utils::Duration referenceExposureTime_;
	double referenceAnalogueGain_;
	double referenceDigitalGain_;
	double referenceY_;
	double referenceLux_;
};

} /* namespace ipa */

} /* namespace libcamera */
