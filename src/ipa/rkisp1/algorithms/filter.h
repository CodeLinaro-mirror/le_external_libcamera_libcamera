/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Filter control
 */

#pragma once

#include <sys/types.h>

#include "algorithm.h"

namespace libcamera {

namespace ipa::rkisp1::algorithms {

class Filter : public Algorithm
{
public:
	Filter() = default;
	~Filter() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	void queueRequest(IPAContext &context, const uint32_t frame,
			  IPAFrameContext &frameContext,
			  const ControlList &controls) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     RkISP1Params *params) override;

private:
	int parseConfig(const YamlObject &tuningData);
	int parseModeConfig(const YamlObject &modeData,
			    std::unordered_map<std::string, uint32_t> &modeParams);
	int parseSharpnessConfig(const YamlObject &data,
				 std::unordered_map<std::string, uint32_t> &sharpParams);

	std::unordered_map<int32_t, std::unordered_map<std::string, uint32_t>> modes_;
	std::vector<std::unordered_map<std::string, uint32_t>> sharpness_;
};

} /* namespace ipa::rkisp1::algorithms */
} /* namespace libcamera */
