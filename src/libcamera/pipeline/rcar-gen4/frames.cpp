/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 VIN pipeline
 */

#include "frames.h"

#include <libcamera/base/log.h>

#include <libcamera/framebuffer.h>
#include <libcamera/request.h>

#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/pipeline_handler.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(RCar4)

int RCar4Frames::start(class RCarISPDevice *isp, class ipa::rkisp1::IPAProxyRkISP1 *ipa)
{
	unsigned int ipaBufferId = 1;
	unsigned int bufferCount;
	int ret;

	auto pushBuffers = [&](const std::vector<std::unique_ptr<FrameBuffer>> &buffers,
			       std::queue<FrameBuffer *> &queue) {
		for (const std::unique_ptr<FrameBuffer> &buffer : buffers) {
			Span<const FrameBuffer::Plane> planes = buffer->planes();

			buffer->setCookie(ipaBufferId++);
			ipaBuffers_.emplace_back(buffer->cookie(),
						 std::vector<FrameBuffer::Plane>{ planes.begin(),
										  planes.end() });
			queue.push(buffer.get());
		}
	};

	frameInfo_.clear();

	bufferCount = std::max({
		rawStream_.configuration().bufferCount,
		outputStream_.configuration().bufferCount,
	});

	ret = isp->input_->exportBuffers(bufferCount, &inputBuffers_);
	if (ret < 0) {
		LOG(RCar4, Error) << "Failed to allocate ISP input buffers";
		goto error;
	}

	ret = isp->param_->allocateBuffers(bufferCount, &paramBuffers_);
	if (ret < 0) {
		LOG(RCar4, Error) << "Failed to allocate ISP param buffers";
		goto error;
	}

	ret = isp->stat_->allocateBuffers(bufferCount, &statBuffers_);
	if (ret < 0) {
		LOG(RCar4, Error) << "Failed to allocate ISP stat buffers";
		goto error;
	}

	ret = isp->output_->exportBuffers(bufferCount, &outputBuffers_);
	if (ret < 0) {
		LOG(RCar4, Error) << "Failed to allocate ISP output buffers";
		goto error;
	}

	for (const std::unique_ptr<FrameBuffer> &buffer : inputBuffers_)
		availableInputBuffers_.push(buffer.get());

	pushBuffers(paramBuffers_, availableParamBuffers_);
	pushBuffers(statBuffers_, availableStatBuffers_);

	for (const std::unique_ptr<FrameBuffer> &buffer : outputBuffers_)
		availableOutputBuffers_.push(buffer.get());

	ipa->mapBuffers(ipaBuffers_);

	return 0;
error:
	stop(isp, ipa);
	return ret;
}

void RCar4Frames::stop(class RCarISPDevice *isp, class ipa::rkisp1::IPAProxyRkISP1 *ipa)
{
	std::vector<unsigned int> ids;

	availableInputBuffers_ = {};
	availableParamBuffers_ = {};
	availableStatBuffers_ = {};
	availableOutputBuffers_ = {};

	outputBuffers_.clear();
	statBuffers_.clear();
	paramBuffers_.clear();
	inputBuffers_.clear();

	for (IPABuffer &ipabuf : ipaBuffers_)
		ids.push_back(ipabuf.id);

	ipa->unmapBuffers(ids);
	ipaBuffers_.clear();

	if (isp->output_->releaseBuffers())
		LOG(RCar4, Error) << "Failed to release ISP output buffers";

	if (isp->stat_->releaseBuffers())
		LOG(RCar4, Error) << "Failed to release ISP stat buffers";

	if (isp->param_->releaseBuffers())
		LOG(RCar4, Error) << "Failed to release ISP param buffers";

	if (isp->input_->releaseBuffers())
		LOG(RCar4, Error) << "Failed to release ISP input buffers";
}

RCar4Frames::Info *RCar4Frames::create(Request *request)
{
	unsigned int frame = request->sequence();

	/* Try to get input and output buffers from request. */
	FrameBuffer *inputBuffer = request->findBuffer(&rawStream_);
	FrameBuffer *outputBuffer = request->findBuffer(&outputStream_);

	/* Make sure we have enough internal buffers. */
	if (!inputBuffer && availableInputBuffers_.empty()) {
		LOG(RCar4, Debug) << "Input buffer underrun";
		return nullptr;
	}

	if (availableParamBuffers_.empty()) {
		LOG(RCar4, Debug) << "Parameters buffer underrun";
		return nullptr;
	}

	if (availableStatBuffers_.empty()) {
		LOG(RCar4, Debug) << "Statistics buffer underrun";
		return nullptr;
	}

	if (!outputBuffer && availableOutputBuffers_.empty()) {
		LOG(RCar4, Debug) << "Output buffer underrun";
		return nullptr;
	}

	/* Select buffers to use. */
	if (!inputBuffer) {
		inputBuffer = availableInputBuffers_.front();
		availableInputBuffers_.pop();
		inputBuffer->_d()->setRequest(request);
	}

	FrameBuffer *paramBuffer = availableParamBuffers_.front();
	availableParamBuffers_.pop();
	paramBuffer->_d()->setRequest(request);

	FrameBuffer *statBuffer = availableStatBuffers_.front();
	availableStatBuffers_.pop();
	statBuffer->_d()->setRequest(request);

	if (!outputBuffer) {
		outputBuffer = availableOutputBuffers_.front();
		availableOutputBuffers_.pop();
		outputBuffer->_d()->setRequest(request);
	}

	/* Recored the info needed to process one frame. */
	auto [it, inserted] = frameInfo_.try_emplace(frame);
	if (!inserted)
		return nullptr;

	auto &info = it->second;

	info.frame = frame;
	info.request = request;
	info.inputBuffer = inputBuffer;
	info.paramBuffer = paramBuffer;
	info.statBuffer = statBuffer;
	info.outputBuffer = outputBuffer;
	info.rawDequeued = false;
	info.paramDequeued = false;
	info.metadataProcessed = false;
	info.outputDequeued = false;

	return &info;
}

void RCar4Frames::remove(RCar4Frames::Info *info)
{
	/* If internal input buffer used, return for reuse. */
	for (const std::unique_ptr<FrameBuffer> &buf : inputBuffers_) {
		if (info->inputBuffer == buf.get()) {
			availableInputBuffers_.push(info->inputBuffer);
			break;
		}
	}

	/* Return param and stat buffer for reuse. */
	availableParamBuffers_.push(info->paramBuffer);
	availableStatBuffers_.push(info->statBuffer);

	/* If internal output buffer used, return for reuse. */
	for (const std::unique_ptr<FrameBuffer> &buf : outputBuffers_) {
		if (info->outputBuffer == buf.get()) {
			availableOutputBuffers_.push(info->outputBuffer);
			break;
		}
	}

	/* Delete the extended frame information. */
	frameInfo_.erase(info->frame);
}

bool RCar4Frames::tryComplete(RCar4Frames::Info *info)
{
	Request *request = info->request;

	if (request->hasPendingBuffers())
		return false;

	if (!info->rawDequeued)
		return false;

	if (!info->metadataProcessed)
		return false;

	if (!info->paramDequeued)
		return false;

	if (!info->outputDequeued)
		return false;

	remove(info);

	/* Signal new internal buffers available. */
	bufferAvailable.emit();

	return true;
}

RCar4Frames::Info *RCar4Frames::find(unsigned int frame)
{
	const auto &itInfo = frameInfo_.find(frame);

	if (itInfo != frameInfo_.end())
		return &itInfo->second;

	LOG(RCar4, Fatal) << "Can't find tracking information for frame " << frame;

	return nullptr;
}

RCar4Frames::Info *RCar4Frames::find(FrameBuffer *buffer)
{
	for (auto &itInfo : frameInfo_) {
		Info *info = &itInfo.second;

		for (auto const itBuffers : info->request->buffers())
			if (itBuffers.second == buffer)
				return info;

		if (info->inputBuffer == buffer ||
		    info->paramBuffer == buffer ||
		    info->statBuffer == buffer ||
		    info->outputBuffer == buffer)
			return info;
	}

	LOG(RCar4, Fatal) << "Can't find tracking information from buffer";

	return nullptr;
}

} /* namespace libcamera */
