/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020-2021, Google Inc.
 *
 * Simple capture helper
 */

#include "capture.h"

#include <assert.h>

#include <gtest/gtest.h>

using namespace libcamera;

Capture::Capture(std::shared_ptr<Camera> camera)
	: camera_(std::move(camera)),
	  allocator_(camera_)
{
}

Capture::~Capture()
{
	stop();
}

void Capture::configure(libcamera::Span<const libcamera::StreamRole> roles)
{
	assert(!roles.empty());

	config_ = camera_->generateConfiguration(roles);

	if (!config_) {
		std::cout << "Role not supported by camera" << std::endl;
		GTEST_SKIP();
	}

	/*
	 * Set the buffers count to the largest value across all streams.
	 * \todo: Should all streams from a Camera have the same buffer count ?
	 */
	auto largest =
		std::max_element(config_->begin(), config_->end(),
				 [](const StreamConfiguration &l, const StreamConfiguration &r)
				 { return l.bufferCount < r.bufferCount; });

	for (auto &cfg : *config_)
		cfg.bufferCount = largest->bufferCount;

	if (config_->validate() != CameraConfiguration::Valid) {
		config_.reset();
		FAIL() << "Configuration not valid";
	}

	if (camera_->configure(config_.get())) {
		config_.reset();
		FAIL() << "Failed to configure camera";
	}
}

void Capture::start()
{
	assert(config_);
	assert(!config_->empty());
	assert(!allocator_.allocated());

	for (const auto &cfg : *config_) {
		Stream *stream = cfg.stream();
		int count = allocator_.allocate(stream);

		ASSERT_GE(count, 0) << "Failed to allocate buffers";
		EXPECT_EQ(count, cfg.bufferCount) << "Allocated less buffers than expected";
		ASSERT_EQ(count, allocator_.buffers(stream).size()) << "Unexpected number of buffers in allocator";
	}

	ASSERT_TRUE(allocator_.allocated());

	camera_->requestCompleted.connect(this, &Capture::requestComplete);

	result_.reset();

	ASSERT_EQ(camera_->start(), 0) << "Failed to start camera";
}

void Capture::stop()
{
	if (!config_ || !allocator_.allocated())
		return;

	camera_->stop();

	result_.reset();

	camera_->requestCompleted.disconnect(this);

	requests_.clear();

	for (const auto &cfg : *config_) {
		int res = allocator_.free(cfg.stream());
		ASSERT_EQ(res, 0) << "Failed to free buffers associated with stream";
	}

	ASSERT_FALSE(allocator_.allocated());
}

void Capture::prepareRequests(unsigned int plannedRequests)
{
	assert(config_);
	assert(requests_.empty());

	std::size_t maxBuffers = 0;

	for (const auto &cfg : *config_) {
		const auto &buffers = allocator_.buffers(cfg.stream());
		ASSERT_FALSE(buffers.empty()) << "Zero buffers allocated for stream";

		/* No point in testing less requests then the camera depth. */
		if (plannedRequests < buffers.size()) {
			std::cout << "Camera needs " << buffers.size()
				  << " requests, can't test only "
				  << plannedRequests << std::endl;
			GTEST_SKIP();
		}

		maxBuffers = std::max(maxBuffers, buffers.size());
	}

	for (std::size_t i = 0; i < maxBuffers; i++) {
		std::unique_ptr<Request> request = camera_->createRequest(i);

		for (const auto &cfg : *config_) {
			Stream *stream = cfg.stream();
			const auto &buffers = allocator_.buffers(stream);
			assert(!buffers.empty());

			if (i < buffers.size()) {
				ASSERT_EQ(request->addBuffer(stream, buffers[i].get()), 0)
					<< "Can't add buffer to request";
			}
		}

		requests_.push_back(std::move(request));
	}
}

/* CaptureBalanced */

CaptureBalanced::CaptureBalanced(std::shared_ptr<Camera> camera)
	: Capture(std::move(camera))
{
}

void CaptureBalanced::capture(unsigned int numRequests)
{
	start();

	queueCount_ = 0;
	captureCount_ = 0;
	captureLimit_ = numRequests;

	prepareRequests(numRequests);

	for (const auto &request : requests_)
		queueRequest(request.get());

	/* Run capture session. */
	int status = result_.wait();
	stop();

	ASSERT_EQ(status, 0);
	ASSERT_EQ(captureCount_, captureLimit_);
}

int CaptureBalanced::queueRequest(Request *request)
{
	queueCount_++;
	if (queueCount_ > captureLimit_)
		return 0;

	return camera_->queueRequest(request);
}

void CaptureBalanced::requestComplete(Request *request)
{
	EXPECT_EQ(request->status(), Request::Status::RequestComplete)
		<< "Request didn't complete successfully";

	captureCount_++;
	if (captureCount_ >= captureLimit_) {
		result_.set(0);
		return;
	}

	request->reuse(Request::ReuseBuffers);
	if (queueRequest(request))
		result_.set(-EINVAL);
}

/* CaptureUnbalanced */

CaptureUnbalanced::CaptureUnbalanced(std::shared_ptr<Camera> camera)
	: Capture(std::move(camera))
{
}

void CaptureUnbalanced::capture(unsigned int numRequests)
{
	start();

	captureCount_ = 0;
	captureLimit_ = numRequests;

	prepareRequests(numRequests);

	for (const auto &request : requests_)
		camera_->queueRequest(request.get());

	/* Run capture session. */
	int status = result_.wait();
	stop();

	ASSERT_EQ(status, 0);
}

void CaptureUnbalanced::requestComplete(Request *request)
{
	captureCount_++;
	if (captureCount_ >= captureLimit_) {
		result_.set(0);
		return;
	}

	EXPECT_EQ(request->status(), Request::Status::RequestComplete)
		<< "Request didn't complete successfully";

	request->reuse(Request::ReuseBuffers);
	if (camera_->queueRequest(request))
		result_.set(-EINVAL);
}
