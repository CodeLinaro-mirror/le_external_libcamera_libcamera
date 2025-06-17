/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 VIN pipeline
 */

#pragma once

#include <map>
#include <memory>
#include <queue>
#include <vector>

#include <libcamera/base/signal.h>

#include <libcamera/controls.h>
#include <libcamera/stream.h>

#include <libcamera/ipa/core_ipa_interface.h>
#include <libcamera/ipa/rkisp1_ipa_interface.h>
#include <libcamera/ipa/rkisp1_ipa_proxy.h>

#include "isp.h"

namespace libcamera {

class FrameBuffer;
class Request;

class RCar4Frames
{
public:
	struct Info {
		unsigned int frame;
		Request *request;

		FrameBuffer *inputBuffer;
		FrameBuffer *paramBuffer;
		FrameBuffer *statBuffer;
		FrameBuffer *outputBuffer;

		ControlList effectiveSensorControls;

		bool rawDequeued;
		bool paramDequeued;
		bool metadataProcessed;
		bool outputDequeued;
	};

	RCar4Frames();

	int start(class RCarISPDevice *isp, class ipa::rkisp1::IPAProxyRkISP1 *ipa);
	void stop(class RCarISPDevice *isp, class ipa::rkisp1::IPAProxyRkISP1 *ipa);

	Info *create(Request *request);
	void remove(Info *info);
	bool tryComplete(Info *info);

	Info *find(unsigned int frame);
	Info *find(FrameBuffer *buffer);

	Signal<> bufferAvailable;

	Stream rawStream_;
	Stream outputStream_;
private:
	std::map<unsigned int, std::unique_ptr<Info>> frameInfo_;

	/* Buffers for internal use, if none is provided in request. */
	std::vector<std::unique_ptr<FrameBuffer>> inputBuffers_;
	std::vector<std::unique_ptr<FrameBuffer>> paramBuffers_;
	std::vector<std::unique_ptr<FrameBuffer>> statBuffers_;
	std::vector<std::unique_ptr<FrameBuffer>> outputBuffers_;

	/* Queues of available internal buffers. */
	std::queue<FrameBuffer *> availableInputBuffers_;
	std::queue<FrameBuffer *> availableParamBuffers_;
	std::queue<FrameBuffer *> availableStatBuffers_;
	std::queue<FrameBuffer *> availableOutputBuffers_;

	/* Buffers mapped and shared with IPA. */
	std::vector<IPABuffer> ipaBuffers_;
};

} /* namespace libcamera */
