/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Denoise Pre-Filter control
 */

#pragma once

#include <sys/types.h>

#include "algorithm.h"

namespace libcamera {

namespace ipa::rkisp1::algorithms {

class Dpf : public Algorithm
{
public:
	Dpf();
	~Dpf() = default;

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
		rkisp1_cif_isp_dpf_config dpf;
		rkisp1_cif_isp_dpf_strength_config strength;
	};

	bool parseConfig(const YamlObject &tuningData);
	bool parseModes(const YamlObject &tuningData);
	bool parseSingleConfig(const YamlObject &tuningData,
			       rkisp1_cif_isp_dpf_config &config,
			       rkisp1_cif_isp_dpf_strength_config &strengthConfig);

	bool loadReductionConfig(int32_t mode);
	void logConfigIfChanged(const IPAFrameContext &frameContext);

	void prepareDisabledMode(IPAContext &context,
				 const uint32_t frame,
				 IPAFrameContext &frameContext,
				 RkISP1Params *params);
	void prepareEnabledMode(IPAContext &context,
				const uint32_t frame,
				IPAFrameContext &frameContext,
				RkISP1Params *params);

	struct rkisp1_cif_isp_dpf_config config_;
	struct rkisp1_cif_isp_dpf_strength_config strengthConfig_;
	std::vector<ModeConfig> noiseReductionModes_;
	int32_t runningMode_;
};

} /* namespace ipa::rkisp1::algorithms */
} /* namespace libcamera */
