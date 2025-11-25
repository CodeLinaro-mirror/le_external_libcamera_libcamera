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
	void process(IPAContext &context, const uint32_t frame [[maybe_unused]],
		     IPAFrameContext &frameContext,
		     const rkisp1_stat_buffer *stats [[maybe_unused]],
		     ControlList &metadata) override;

private:
	struct rkisp1_cif_isp_dpf_config config_;
	struct rkisp1_cif_isp_dpf_strength_config strengthConfig_;
	struct rkisp1_cif_isp_dpf_config baseConfig_;
	struct rkisp1_cif_isp_dpf_strength_config baseStrengthConfig_;
	struct DpfStrengthSettings {
		uint16_t r, g, b;
	};
	struct DpfSpatialGreenSettings {
		std::array<uint8_t, RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS> coeffs;
	};
	struct DpfSpatialRbSettings {
		std::array<uint8_t, RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS> coeffs;
		uint8_t size; /* 0=9x9, 1=13x9 */
	};
	struct DpfNllSettings {
		std::array<uint16_t, RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS> coeffs;
		uint8_t scaleMode; /* 0 linear, 1 log */
	};
	struct ExposureIndexLevelConfig {
		uint32_t maxExposureIndex; /* inclusive upper bound */
		struct rkisp1_cif_isp_dpf_config dpf;
		struct rkisp1_cif_isp_dpf_strength_config strength;
	};
	struct ModeConfig {
		int32_t modeValue;
		struct rkisp1_cif_isp_dpf_config dpf;
		struct rkisp1_cif_isp_dpf_strength_config strength;
	};
	struct Overrides {
		std::optional<DpfStrengthSettings> strength;
		std::optional<DpfSpatialGreenSettings> spatialGreen;
		std::optional<DpfSpatialRbSettings> spatialRb;
		std::optional<uint8_t> rbSize;
		std::optional<DpfNllSettings> nll;
		void clear() { *this = Overrides{}; }
	} overrides_;

	std::vector<ExposureIndexLevelConfig> exposureIndexLevels_;
	std::vector<ModeConfig> modes_;
	bool useExposureIndexLevels_ = false;
	int32_t lastExposureGainIndex_ = -1;
	int32_t currentReductionMode_ = controls::draft::NoiseReductionModeOff;

	void handleReductionModeControl(const ControlList &controls,
					IPAFrameContext &frameContext,
					IPAContext &context,
					uint32_t frame) override;
	void handleDisableMode(IPAFrameContext &frameContext, IPAContext &context) override;
	void loadReductionModeConfig(IPAFrameContext &frameContext);
	void collectManualOverrides(const ControlList &controls) override;
	bool checkOverridesChanged();
	bool parseConfig(const YamlObject &tuningData) override;
	bool parseSingleConfig(const YamlObject &tuningData,
			       rkisp1_cif_isp_dpf_config &config,
			       rkisp1_cif_isp_dpf_strength_config &strengthConfig);

	bool processModeChange(const ControlList &controls, uint32_t currentFrame) override;
	void snapshotCurrentToOverrides() override;
	void restoreAutoConfig(IPAContext &context, IPAFrameContext &frameContext) override;
	void applyOverridesTo(rkisp1_cif_isp_dpf_config &config,
			      rkisp1_cif_isp_dpf_strength_config &strengthConfig,
			      bool &anyOverride);
	void logConfigIfChanged(uint32_t exposureGainIndex,
				int32_t exposureBandIndex,
				bool anyOverride,
				const IPAFrameContext &frameContext);
	void prepareDisabledMode(IPAContext &context, const uint32_t frame,
				 IPAFrameContext &frameContext,
				 RkISP1Params *params) override;
	void prepareEnabledMode(IPAContext &context, const uint32_t frame,
				IPAFrameContext &frameContext, RkISP1Params *params) override;
	ControlInfoMap::Map getControlMap() const override;
	void fillMetadata(IPAContext &context,
			  IPAFrameContext &frameContext,
			  ControlList &metadata) override;
};

} /* namespace ipa::rkisp1::algorithms */
} /* namespace libcamera */
