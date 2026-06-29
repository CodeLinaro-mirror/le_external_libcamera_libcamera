/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * V4L2 compatibility camera
 */

#include "v4l2_camera.h"

#include <errno.h>
#include <unistd.h>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

using namespace libcamera;

LOG_DECLARE_CATEGORY(V4L2Compat)

V4L2Camera::V4L2Camera(std::shared_ptr<Camera> camera)
	: camera_(camera), controls_(controls::controls), isRunning_(false),
	  efd_(-1)
{
	camera_->requestCompleted.connect(this, &V4L2Camera::requestComplete);
}

V4L2Camera::~V4L2Camera()
{
	close();
}

int V4L2Camera::open(StreamConfiguration *streamConfig)
{
	if (camera_->acquire() < 0) {
		LOG(V4L2Compat, Error) << "Failed to acquire camera";
		return -EINVAL;
	}

	config_ = camera_->generateConfiguration({ StreamRole::Viewfinder });
	if (!config_) {
		camera_->release();
		return -EINVAL;
	}

	bufferAllocator_ = std::make_unique<FrameBufferAllocator>(camera_);

	*streamConfig = config_->at(0);
	return 0;
}

void V4L2Camera::close()
{
	requestPool_.clear();

	bufferAllocator_.reset();

	camera_->release();
}

void V4L2Camera::bind(int efd)
{
	efd_ = efd;
}

void V4L2Camera::unbind()
{
	efd_ = -1;
}

void V4L2Camera::requestComplete(Request *request)
{
	bool hasBuffer = false;

	/* We only have one stream at the moment. */
	{
		MutexLocker locker(bufferMutex_);
		FrameBuffer *buffer = request->findBuffer(config_->at(0).stream());

		if (buffer) {
			completedBuffers_.emplace_back(buffer->cookie(), buffer->metadata());

			uint64_t data = 1;
			int ret = ::write(efd_, &data, sizeof(data));
			if (ret != sizeof(data))
				LOG(V4L2Compat, Error) << "Failed to signal eventfd POLLIN";

			hasBuffer = true;
		}

		request->reuse();
		freeRequests_.push_back(request);
	}

	if (hasBuffer)
		bufferCV_.notify_all();
}

int V4L2Camera::configure(StreamConfiguration *streamConfigOut,
			  const Size &size, const PixelFormat &pixelformat,
			  unsigned int bufferCount)
{
	StreamConfiguration &streamConfig = config_->at(0);
	streamConfig.size.width = size.width;
	streamConfig.size.height = size.height;
	streamConfig.pixelFormat = pixelformat;
	streamConfig.bufferCount = bufferCount;
	/* \todo memoryType (interval vs external) */

	CameraConfiguration::Status validation = config_->validate();
	if (validation == CameraConfiguration::Invalid) {
		LOG(V4L2Compat, Debug) << "Configuration invalid";
		return -EINVAL;
	}
	if (validation == CameraConfiguration::Adjusted)
		LOG(V4L2Compat, Debug) << "Configuration adjusted";

	LOG(V4L2Compat, Debug) << "Validated configuration is: "
			      << streamConfig.toString();

	int ret = camera_->configure(config_.get());
	if (ret < 0)
		return ret;

	*streamConfigOut = config_->at(0);

	return 0;
}

int V4L2Camera::validateConfiguration(const PixelFormat &pixelFormat,
				      const Size &size,
				      StreamConfiguration *streamConfigOut)
{
	std::unique_ptr<CameraConfiguration> config =
		camera_->generateConfiguration({ StreamRole::Viewfinder });
	StreamConfiguration &cfg = config->at(0);
	cfg.size = size;
	cfg.pixelFormat = pixelFormat;
	cfg.bufferCount = 1;

	CameraConfiguration::Status validation = config->validate();
	if (validation == CameraConfiguration::Invalid)
		return -EINVAL;

	*streamConfigOut = cfg;

	return 0;
}

int V4L2Camera::allocBuffers()
{
	Stream *stream = config_->at(0).stream();

	int ret = bufferAllocator_->allocate(stream);
	if (ret < 0)
		return ret;

	const auto &buffers = bufferAllocator_->buffers(stream);
	MutexLocker locker(bufferMutex_);

	for (size_t i = 0; i < buffers.size(); i++) {
		std::unique_ptr<Request> request = camera_->createRequest(i);
		if (!request) {
			requestPool_.clear();
			return -ENOMEM;
		}
		freeRequests_.push_back(request.get());
		requestPool_.push_back(std::move(request));

		buffers[i]->setCookie(i);
	}

	return buffers.size();
}

void V4L2Camera::freeBuffers()
{
	{
		MutexLocker locker(bufferMutex_);
		freeRequests_.clear();
	}

	requestPool_.clear();

	pendingBuffers_.clear();

	Stream *stream = config_->at(0).stream();
	bufferAllocator_->free(stream);
}

int V4L2Camera::getBufferFd(unsigned int index)
{
	Stream *stream = config_->at(0).stream();
	const std::vector<std::unique_ptr<FrameBuffer>> &buffers =
		bufferAllocator_->buffers(stream);

	if (buffers.size() <= index)
		return -1;

	return buffers[index]->planes()[0].fd.get();
}

int V4L2Camera::streamOn()
{
	if (isRunning_)
		return 0;

	int ret = camera_->start(&controls_);
	if (ret < 0)
		return ret == -EACCES ? -EBUSY : ret;

	controls_.clear();

	isRunning_ = true;

	Stream *stream = config_->at(0).stream();

	MutexLocker locker(bufferMutex_);

	for (FrameBuffer *buffer : pendingBuffers_) {
		ASSERT(!pendingBuffers_.empty());
		Request *req = freeRequests_.back();
		freeRequests_.pop_back();

		req->enableStream(stream, true);

		/* \todo What should we do if this returns -EINVAL? */
		ret = camera_->queueRequest(req);
		if (ret < 0)
			return ret == -EACCES ? -EBUSY : ret;

		/* \todo error handling how? */
		std::ignore = camera_->addBuffer(stream, buffer);
	}

	pendingBuffers_.clear();

	return 0;
}

int V4L2Camera::streamOff()
{
	pendingBuffers_.clear();

	if (!isRunning_) {
		for (std::unique_ptr<Request> &req : requestPool_)
			req->reuse();

		return 0;
	}

	int ret = camera_->stop();
	if (ret < 0)
		return ret == -EACCES ? -EBUSY : ret;

	{
		MutexLocker locker(bufferMutex_);
		isRunning_ = false;
		completedBuffers_.clear();
	}
	bufferCV_.notify_all();

	return 0;
}

int V4L2Camera::qbuf(unsigned int index)
{
	Stream *stream = config_->at(0).stream();
	const auto &buffers = bufferAllocator_->buffers(stream);

	if (index >= buffers.size()) {
		LOG(V4L2Compat, Error) << "Invalid index";
		return -EINVAL;
	}

	FrameBuffer *buffer = buffers[index].get();

	if (!isRunning_) {
		pendingBuffers_.push_back(buffer);
		return 0;
	}

	MutexLocker locker(bufferMutex_);

	if (freeRequests_.empty())
		return -EBUSY;

	Request *request = freeRequests_.back();

	request->controls().merge(std::move(controls_));
	request->enableStream(stream, true);

	int ret = camera_->queueRequest(request);
	if (ret < 0) {
		LOG(V4L2Compat, Error) << "Can't queue request";
		return ret == -EACCES ? -EBUSY : ret;
	}

	freeRequests_.pop_back();

	/* \todo error handling how? */
	std::ignore = camera_->addBuffer(stream, buffer);

	return 0;
}

std::optional<V4L2Camera::CompletedBuffer> V4L2Camera::nextBuffer(bool wait)
{
	MutexLocker locker(bufferMutex_);

	if (wait) {
		bufferCV_.wait(locker, [&]() LIBCAMERA_TSA_REQUIRES(bufferMutex_) {
		       return !completedBuffers_.empty() || !isRunning_;
		});
	}

	if (!isRunning_)
		return {};

	auto buffer = std::move(completedBuffers_.front());
	completedBuffers_.pop_front();

	return buffer;
}

bool V4L2Camera::isRunning()
{
	return isRunning_;
}
