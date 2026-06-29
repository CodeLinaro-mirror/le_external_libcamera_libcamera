/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021, Google Inc.
 *
 * Fence test
 */

#include <iostream>
#include <memory>
#include <sys/eventfd.h>
#include <unistd.h>

#include <libcamera/base/event_dispatcher.h>
#include <libcamera/base/thread.h>
#include <libcamera/base/timer.h>
#include <libcamera/base/unique_fd.h>
#include <libcamera/base/utils.h>

#include <libcamera/fence.h>
#include <libcamera/framebuffer_allocator.h>

#include "camera_test.h"
#include "test.h"

using namespace libcamera;
using namespace std;
using namespace std::chrono_literals;

class FenceTest : public CameraTest, public Test
{
public:
	FenceTest();

protected:
	int init() override;
	int run() override;

private:
	int validateExpiredBuffer(FrameBuffer *buffer);
	int validateRequest(Request *request);
	void requestComplete(Request *request);

	void signalFence();

	EventDispatcher *dispatcher_;
	UniqueFD eventFd_;
	UniqueFD eventFd2_;
	Timer fenceTimer_;

	std::vector<std::unique_ptr<Request>> requests_;
	std::unique_ptr<CameraConfiguration> config_;
	std::unique_ptr<FrameBufferAllocator> allocator_;

	Stream *stream_;

	bool expectedCompletionResult_ = true;

	unsigned int completedRequestId_;
	unsigned int queuedRequests_ = 0;
	FrameBuffer *testBuffer_ = nullptr;
	unsigned testBufferSeen_ = 0;
	unsigned int nbuffers_;

	int efd2_;
	int efd_;
};

FenceTest::FenceTest()
	: CameraTest("platform/vimc.0 Sensor B")
{
}

int FenceTest::init()
{
	/* Make sure the CameraTest constructor succeeded. */
	if (status_ != TestPass)
		return status_;

	dispatcher_ = Thread::current()->eventDispatcher();

	/*
	 * Create two eventfds to model the fences. This is enough to support the
	 * needs of libcamera which only needs to wait for read events through
	 * poll(). Once native support for fences will be available in the
	 * backend kernel APIs this will need to be replaced by a sw_sync fence,
	 * but that requires debugfs.
	 */
	eventFd_ = UniqueFD(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
	eventFd2_ = UniqueFD(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
	if (!eventFd_.isValid() || !eventFd2_.isValid()) {
		cerr << "Unable to create eventfd" << endl;
		return TestFail;
	}

	efd_ = eventFd_.get();
	efd2_ = eventFd2_.get();

	config_ = camera_->generateConfiguration({ StreamRole::Viewfinder });
	if (!config_ || config_->size() != 1) {
		cerr << "Failed to generate default configuration" << endl;
		return TestFail;
	}

	if (camera_->acquire()) {
		cerr << "Failed to acquire the camera" << endl;
		return TestFail;
	}

	if (camera_->configure(config_.get())) {
		cerr << "Failed to set default configuration" << endl;
		return TestFail;
	}

	StreamConfiguration &cfg = config_->at(0);
	stream_ = cfg.stream();

	allocator_ = std::make_unique<FrameBufferAllocator>(camera_);
	if (allocator_->allocate(stream_) < 0)
		return TestFail;

	const auto &buffers = allocator_->buffers(stream_);
	nbuffers_ = buffers.size();
	if (nbuffers_ < 2) {
		cerr << "Not enough buffers available" << endl;
		return TestFail;
	}

	completedRequestId_ = 0;
	queuedRequests_ = 0;

	/*
	 * The buffer to use for testing. It will be queued 3 times:
	 *   * first, without any fence
	 *   * second, with a fence that is signalled
	 *   * third,with a fence that won't be signalled
	 */
	testBuffer_ = buffers.front().get();
	testBufferSeen_ = 0;

	return TestPass;
}

int FenceTest::validateExpiredBuffer(FrameBuffer *buffer)
{
	std::unique_ptr<Fence> fence = buffer->releaseFence();
	if (!fence) {
		cerr << "The expired fence should be present" << endl;
		return TestFail;
	}

	if (!fence->isValid()) {
		cerr << "The expired fence should be valid" << endl;
		return TestFail;
	}

	UniqueFD fd = fence->release();
	if (fd.get() != efd_) {
		cerr << "The expired fence file descriptor should not change" << endl;
		return TestFail;
	}

	return TestPass;
}

int FenceTest::validateRequest(Request *request)
{
	uint64_t cookie = request->cookie();

	/* All requests but the last are expected to succeed. */
	if (request->status() != Request::RequestComplete) {
		cerr << "Unexpected request failure: " << cookie << endl;
		return TestFail;
	}

	/* A successfully completed request should have the Fence closed. */
	const Request::BufferMap &buffers = request->buffers();
	FrameBuffer *buffer = buffers.begin()->second;

	std::unique_ptr<Fence> bufferFence = buffer->releaseFence();
	if (bufferFence) {
		cerr << "Unexpected valid fence in completed request" << endl;
		return TestFail;
	}

	return TestPass;
}

void FenceTest::requestComplete(Request *request)
{
	const Request::BufferMap &buffers = request->buffers();
	const Stream *stream = buffers.begin()->first;
	FrameBuffer *buffer = buffers.begin()->second;

	completedRequestId_ += 1;

	if (buffer == testBuffer_)
		testBufferSeen_ += 1;

	cout << "completedRequestId:" << completedRequestId_ << " "
	     << "buffer:" << buffer << " "
	     << "testBufferSeen:" << testBufferSeen_
	     << endl;

	/* Validate all other requests. */
	if (validateRequest(request) != TestPass) {
		expectedCompletionResult_ = false;

		dispatcher_->interrupt();
		return;
	}

	if (completedRequestId_ % nbuffers_ == 0) {
		for (const auto &b : allocator_->buffers(stream_)) {
			std::unique_ptr<Fence> fence;

			if (b.get() == testBuffer_) {
				if (testBufferSeen_ == 1) {
					/* This fence will be signalled. */
					assert(eventFd2_.isValid());
					fence = std::make_unique<Fence>(std::move(eventFd2_));
				} else if (testBufferSeen_ == 2) {
					/* This fence won't be signalled. */
					assert(eventFd_.isValid());
					fence = std::make_unique<Fence>(std::move(eventFd_));
				}
			}

			cout << "adding buffer:" << b.get() << " fence:" << (fence ? fence->fd().get() : -1) << endl;
			camera_->addBuffer(stream_, b.get(), std::move(fence));
		}
	}

	if (testBufferSeen_ == 1 && completedRequestId_ == 2 * nbuffers_ - 1) {
		cout << "signalling fence:" << efd2_ << endl;
		signalFence();
	}

	request->reuse();

	if (queuedRequests_ < 3 * nbuffers_ - 1) {
		cout << "queueing request:" << request << endl;
		request->enableStream(stream, true);
		camera_->queueRequest(request);
		queuedRequests_ += 1;
	}

	dispatcher_->interrupt();
}

/* Callback to signal a fence waiting on the eventfd file descriptor. */
void FenceTest::signalFence()
{
	uint64_t value = 1;
	int ret;

	ret = write(efd2_, &value, sizeof(value));
	if (ret != sizeof(value))
		cerr << "Failed to signal fence" << endl;
}

int FenceTest::run()
{
	for (const auto &[i, buffer] : utils::enumerate(allocator_->buffers(stream_))) {
		std::unique_ptr<Request> request = camera_->createRequest(i);
		if (!request) {
			cerr << "Failed to create request" << endl;
			return TestFail;
		}

		request->enableStream(stream_, true);

		requests_.push_back(std::move(request));
	}

	camera_->requestCompleted.connect(this, &FenceTest::requestComplete);

	if (camera_->start()) {
		cerr << "Failed to start camera" << endl;
		return TestFail;
	}

	for (const auto &[i, buffer] : utils::enumerate(allocator_->buffers(stream_))) {
		int ret = camera_->addBuffer(stream_, buffer.get());
		if (ret) {
			cerr << "Failed to associate buffer with request" << endl;
			return TestFail;
		}

		if (camera_->queueRequest(requests_[i].get())) {
			cerr << "Failed to queue request" << endl;
			return TestFail;
		}
	}

	expectedCompletionResult_ = true;

	/*
	 * Loop long enough for all requests to complete, allowing 500ms per
	 * request.
	 */
	Timer timer;
	timer.start(500ms * (3 + 1) * nbuffers_);
	for (;;) {
		if (!timer.isRunning() || !expectedCompletionResult_)
			break;

		if (completedRequestId_ == 3 * nbuffers_ - 1 && testBufferSeen_ == 2)
			break;

		dispatcher_->processEvents();
	}

	camera_->requestCompleted.disconnect();

	bool testBufferFound = false;
	camera_->bufferCompleted.connect(this, [&](Request *request, [[maybe_unused]] const Stream *stream, FrameBuffer *buffer) {
		if (request)
			return;

		if (buffer == testBuffer_) {
			if (validateExpiredBuffer(buffer) != TestPass)
				expectedCompletionResult_ = false;
			testBufferFound = true;
		}
	});

	int ret = camera_->stop();
	camera_->bufferCompleted.disconnect(this);

	if (ret) {
		cerr << "Failed to stop camera" << endl;
		return TestFail;
	}

	if (testBufferSeen_ != 2) {
		cerr << "Test buffer not seen exactly twice" << endl;
		return TestFail;
	}

	if (completedRequestId_ != 3 * nbuffers_ - 1) {
		cerr << "Test buffer not seen exactly twice" << endl;
		return TestFail;
	}

	if (!testBufferFound) {
		cerr << "Buffer with non-signalled fence not returned" << endl;
		return TestFail;
	}

	return expectedCompletionResult_ ? TestPass : TestFail;
}

TEST_REGISTER(FenceTest)
