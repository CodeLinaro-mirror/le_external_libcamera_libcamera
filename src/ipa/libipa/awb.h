/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Ideas on Board Oy
 *
 * libIPA AWB algorithms
 */

#pragma once

#include <array>
#include <map>
#include <optional>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include "libcamera/internal/value_node.h"
#include "libcamera/internal/vector.h"

namespace libcamera {

namespace ipa {

namespace awb {

struct Session {
	bool enabled;
};

struct ActiveState {
	struct AwbState {
		RGB<double> gains;
		unsigned int temperatureK;
	};

	AwbState manual;
	AwbState automatic;

	bool autoEnabled;
};

struct FrameContext {
	RGB<double> gains;
	bool autoEnabled;
	unsigned int temperatureK;
};

} /* namespace awb */

struct AwbStats {
	AwbStats() = default;
	AwbStats(const RGB<double> &means);
	virtual ~AwbStats() = default;

	bool valid() const;

	virtual double rgRatio() const { return rg_; }
	virtual double bgRatio() const { return bg_; }
	virtual double computeColourError(const RGB<double> &gains) const;
	virtual RGB<double> rgbMeans() const;

protected:
	virtual double minColourValue() const = 0;

	RGB<double> rgbMeans_;
	double rg_;
	double bg_;
};

class AwbImplementation
{
public:
	struct AwbResult {
		RGB<double> gains;
		double colourTemperature;
	};

	virtual ~AwbImplementation() = default;
	virtual int init(const ValueNode &tuningData) = 0;
	virtual AwbResult calculateAwb(const AwbStats &stats, unsigned int lux,
				       std::array<double, 2> ranges) = 0;
	virtual std::optional<RGB<double>>
	gainsFromColourTemperature(double temperatureK) = 0;
};

class AwbAlgorithmBase
{
public:
	int init(const ValueNode &tuningData);

	int configure(awb::ActiveState &state, awb::Session &session);

	void queueRequest(awb::ActiveState &state,
			  const uint32_t frame,
			  awb::FrameContext &frameContext,
			  const ControlList &controls);

	void prepare(awb::ActiveState &state, awb::FrameContext &frameContext);

	void process(awb::ActiveState &state, awb::FrameContext &frameContext,
		     const AwbStats &stats, unsigned int lux,
		     ControlList &metadata);

protected:
	AwbAlgorithmBase() = default;

	ControlInfoMap::Map controls_;
	float gainmin_;
	float gainmax_;

private:
	struct ModeConfig {
		double ctHi;
		double ctLo;
	};

	/* AwbGrey does not support modes; */
	static constexpr ModeConfig AwbGreyMode = { 0.0, 0.0 };

	int parseModeConfigs(const ValueNode &tuningData,
			     const ControlValue &def = {});

	std::map<controls::AwbModeEnum, AwbAlgorithmBase::ModeConfig> modes_;
	const ModeConfig *currentMode_ = nullptr;
	std::unique_ptr<AwbImplementation> impl_;
	bool bayes_ = false;
};

template<typename Q>
class AwbAlgorithm : public AwbAlgorithmBase
{
public:
	int init(const ValueNode &tuningData, ControlInfoMap::Map &controls)
	{
		AwbAlgorithmBase::init(tuningData);

		gainmin_ = std::max(Q::TraitsType::min, 1.0f);
		gainmax_ = Q::TraitsType::max;

		controls_[&controls::ColourGains] =
			ControlInfo(gainmin_, gainmax_,
				    Span<const float, 2>{ { 1.0f, 1.0f } });

		controls.insert(controls_.begin(), controls_.end());

		return 0;
	}
};

} /* namespace ipa */

} /* namespace libcamera */
