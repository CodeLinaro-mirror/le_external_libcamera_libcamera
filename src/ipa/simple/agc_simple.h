/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Exposure and gain
 */

#pragma once

#include <optional>

#include <libipa/agc.h>

#include "libcamera/internal/software_isp/swisp_stats.h"

namespace libcamera {

namespace ipa::soft {

class AgcSimpleAlgorithm : public AgcAlgorithm
{
public:
	struct Session : AgcAlgorithm::Session {
		double gain10;
		double gainMinStep;
	};

	struct ProcessParams {
		int32_t exposure;
		double gain;
		const SwIspStats &stats;
		unsigned int blackLevel;
	};

	int configure(Session &session, ActiveState &state, const ConfigurationParams &config);

	void prepare(ActiveState &state, FrameContext &frameContext)
	{
		return AgcAlgorithm::prepare(state, frameContext);
	}

	void queueRequest(const Session &session, ActiveState &state,
			  FrameContext &frameContext, const ControlList &controls)
	{
		return AgcAlgorithm::queueRequest(session, state, frameContext, controls);
	}

	void process(const Session &session, ActiveState &state,
		     FrameContext &frameContext, std::optional<ProcessParams> &&params,
		     ControlList &metadata);

private:
	void updateExposure(const Session &session, ActiveState &state, FrameContext &frameContext,
			    const ProcessParams &params, double exposureMSV);
};

} /* namepsace ipa::soft */

} /* namespace libcamera */
