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
	Filter();
	~Filter() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	void queueRequest(IPAContext &context, const uint32_t frame,
			  IPAFrameContext &frameContext,
			  const ControlList &controls) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     RkISP1Params *params) override;

private:
	struct ModeConfig {
		int32_t modeValue;
		rkisp1_cif_isp_flt_config config;
	};

	int parseConfig(const YamlObject &tuningData);
	int parseSingleConfig(const YamlObject &tuningData,
			      struct rkisp1_cif_isp_flt_config &config);

	bool loadConfig(int32_t mode);

	void logConfig(const struct rkisp1_cif_isp_flt_config &config);
	void prepareDisabledMode(RkISP1Params *params);
	void prepareEnabledMode(const uint32_t frame,
				IPAFrameContext &frameContext,
				RkISP1Params *params);

	std::vector<ModeConfig> noiseReductionModes_;
	std::vector<ModeConfig>::const_iterator activeMode_;
};

} /* namespace ipa::rkisp1::algorithms */
} /* namespace libcamera */
