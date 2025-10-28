/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021-2022, Ideas On Board
 *
 * RkISP1 Denoise Pre-Filter control
 */

#pragma once

#include <sys/types.h>

#include "algorithm.h"
#include "denoise.h"
#include "yaml_helper.h"

namespace libcamera {

namespace ipa::rkisp1::algorithms {

class Dpf : public DenoiseBaseAlgorithm
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
	struct rkisp1_cif_isp_dpf_config config_ {
	};
	struct rkisp1_cif_isp_dpf_strength_config strengthConfig_ {
	};
	struct rkisp1_cif_isp_dpf_config baseConfig_ {
	};
	struct rkisp1_cif_isp_dpf_strength_config baseStrengthConfig_ {
	};
	struct IsoLevelConfig {
		unsigned int maxIso; /* inclusive upper bound */
		struct rkisp1_cif_isp_dpf_config dpf;
		struct rkisp1_cif_isp_dpf_strength_config strength;
	};

	std::vector<IsoLevelConfig> isoLevels_;
	bool useIsoLevels_ = false;
	bool enableDpf_ = true; /* YAML master enable */

	void handleEnableControl(const ControlList &controls, IPAFrameContext &frameContext, IPAContext &context) override;
	bool parseConfig(const YamlObject &tuningData) override;
	bool parseSingleConfig(const YamlObject &config,
			       rkisp1_cif_isp_dpf_config &cfg,
			       rkisp1_cif_isp_dpf_strength_config &strength);
};

} /* namespace ipa::rkisp1::algorithms */
} /* namespace libcamera */
