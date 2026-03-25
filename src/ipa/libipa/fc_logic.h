/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2022, Google Inc.
 *
 * IPA Frame context queue
 */

#pragma once

#include <list>
#include <stdint.h>
#include <vector>

#include <libcamera/base/log.h>
#include <libcamera/controls.h>

#include "algorithm.h"
#include "fc_queue.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(FCLogic)

namespace ipa {

template<typename FrameContext>
class FCLogic;

template<typename _Module>
class FCLogic
{
public:
	using Module = _Module;
	using Algo = Algorithm<Module>;

	FCLogic(unsigned int size, Module *module, typename Module::Context &context)
		: module_(module), contexts_(size), context_(context)
	{
		clear();
	}

	void clear()
	{
		for (typename Module::FrameContext &ctx : contexts_) {
			ctx.frame_ = 0;
		}
		initialized_ = false;
		lastPrepared_ = 0;
	}

	typename Module::FrameContext &initFrameContext(unsigned int frame, const ControlList &controls = {})
	{
		typename Module::FrameContext &fc = contexts_[frame % contexts_.size()];
		FrameContext &frameContext = fc;

		/*
		 * If the IPA algorithms try to access a frame context slot which
		 * has been already overwritten by a newer context, it means the
		 * frame context queue has overflowed and the desired context
		 * has been forever lost. The pipeline handler shall avoid
		 * queueing more requests to the IPA than the frame context
		 * queue size.
		 */
		if (frame < frameContext.frame_)
			LOG(FCLogic, Fatal) << "Frame context for " << frame
					    << " has been overwritten by "
					    << frameContext.frame_;

		if (initialized_ && frame == frameContext.frame_) {
			if (!controls.empty()) {
				/* Too late to apply the controls. Store them for later. */
				LOG(FCLogic, Warning)
					<< "Request underrun. Controls for frame "
					<< frame << " are delayed ";
				controlsToApply_.merge(controls,
						       ControlList::MergePolicy::OverwriteExisting);
			}

			return fc;
		}

		const ControlList *controls2 = &controls;
		if (!controlsToApply_.empty()) {
			LOG(FCLogic, Debug) << "Applied late controls on frame" << frame;
			controlsToApply_.merge(controls, ControlList::MergePolicy::OverwriteExisting);
			controls2 = &controlsToApply_;
		}

		fc = {};
		frameContext.frame_ = frame;
		for (auto const &a : module_->algorithms()) {
			Algo *algo = static_cast<Algo *>(a.get());
			if (!algo->enabled())
				continue;
			algo->queueRequest(context_, fc.frame(), fc, *controls2);
		}

		initialized_ = true;
		controlsToApply_.clear();

		return fc;
	}

	void prepareFrame(const uint32_t frame, typename Module::Params *params)
	{
		while (lastPrepared_ + 1 < frame) {
			uint32_t f = lastPrepared_ + 1;
			LOG(FCLogic, Warning) << "Collect skipped params for frame " << f;
			prepareFrame(f, params);
		}

		typename Module::FrameContext &frameContext = initFrameContext(frame);
		for (auto const &algo : module_->algorithms())
			algo->prepare(context_, frameContext.frame(), frameContext, params);

		lastPrepared_ = std::max(lastPrepared_, frame);
	}

	void processStats(const uint32_t frame, const typename Module::Stats *stats, ControlList &metadata)
	{
		/*
		 * If we apply stats on an uninitialized frame the activeState
		 * might end up very wrong (imagine exposureTime initialized to
		 * 0 but used together with the agc stats to upadet activeState.
		 */
		if (!isInitialize(frame))
			LOG(FCLogic, Error) << "Process stats called on an uninitialized FC " << frame;

		typename Module::FrameContext &frameContext = initFrameContext(frame);
		for (auto const &algo : module_->algorithms())
			algo->process(context_, frameContext.frame(), frameContext, stats, metadata);
	}

private:
	bool isInitialize(const uint32_t frame)
	{
		FrameContext &frameContext = contexts_[frame % contexts_.size()];
		return initialized_ && frame == frameContext.frame_;
	}

	const Module *module_;
	std::vector<typename Module::FrameContext> contexts_;
	typename Module::Context &context_;
	ControlList controlsToApply_;
	uint32_t lastPrepared_;
	bool initialized_;
};

} /* namespace ipa */

} /* namespace libcamera */
