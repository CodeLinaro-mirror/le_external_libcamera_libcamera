/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 * Copyright (C) 2021, Collabora Ltd.
 *
 * Test camera capture
 */

#include "capture.h"

#include <semaphore>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "test_base.h"

namespace {

using namespace libcamera;

class SimpleCapture : public testing::TestWithParam<std::tuple<std::vector<StreamRole>, int>>, public CameraHolder
{
public:
	static std::string nameParameters(const testing::TestParamInfo<SimpleCapture::ParamType> &info);

protected:
	void SetUp() override;
	void TearDown() override;
};

/*
 * We use gtest's SetUp() and TearDown() instead of constructor and destructor
 * in order to be able to assert on them.
 */
void SimpleCapture::SetUp()
{
	acquireCamera();
}

void SimpleCapture::TearDown()
{
	releaseCamera();
}

std::string SimpleCapture::nameParameters(const testing::TestParamInfo<SimpleCapture::ParamType> &info)
{
	const auto &[roles, numRequests] = info.param;
	std::ostringstream ss;

	for (StreamRole r : roles)
		ss << r << '_';

	ss << '_' << numRequests;

	return ss.str();
}

/*
 * Test single capture cycles
 *
 * Makes sure the camera completes the exact number of requests queued. Example
 * failure is a camera that completes less requests than the number of requests
 * queued.
 */
TEST_P(SimpleCapture, Capture)
{
	const auto &[roles, numRequests] = GetParam();

	Capture capture(camera_);

	capture.configure(roles);

	capture.run(numRequests, numRequests);
}

/*
 * Test multiple start/stop cycles
 *
 * Makes sure the camera supports multiple start/stop cycles. Example failure is
 * a camera that does not clean up correctly in its error path but is only
 * tested by single-capture applications.
 */
TEST_P(SimpleCapture, CaptureStartStop)
{
	const auto &[roles, numRequests] = GetParam();
	unsigned int numRepeats = 3;

	Capture capture(camera_);

	capture.configure(roles);

	for (unsigned int starts = 0; starts < numRepeats; starts++)
		capture.run(numRequests, numRequests);
}

/*
 * Test unbalanced stop
 *
 * Makes sure the camera supports a stop with requests queued. Example failure
 * is a camera that does not handle cancelation of buffers coming back from the
 * video device while stopping.
 */
TEST_P(SimpleCapture, UnbalancedStop)
{
	const auto &[roles, numRequests] = GetParam();

	Capture capture(camera_);

	capture.configure(roles);

	capture.run(numRequests);
}

const int NUMREQUESTS[] = { 1, 2, 3, 5, 8, 89 };

const std::vector<StreamRole> SINGLEROLES[] = {
	{ StreamRole::Raw, },
	{ StreamRole::StillCapture, },
	{ StreamRole::VideoRecording, },
	{ StreamRole::Viewfinder, },
};

const std::vector<StreamRole> MULTIROLES[] = {
	{ StreamRole::Raw, StreamRole::StillCapture },
	{ StreamRole::Raw, StreamRole::VideoRecording },
	{ StreamRole::StillCapture, StreamRole::VideoRecording },
	{ StreamRole::VideoRecording, StreamRole::VideoRecording },
};

INSTANTIATE_TEST_SUITE_P(SingleStream,
			 SimpleCapture,
			 testing::Combine(testing::ValuesIn(SINGLEROLES),
					  testing::ValuesIn(NUMREQUESTS)),
			 SimpleCapture::nameParameters);

INSTANTIATE_TEST_SUITE_P(MultiStream,
			 SimpleCapture,
			 testing::Combine(testing::ValuesIn(MULTIROLES),
					  testing::ValuesIn(NUMREQUESTS)),
			 SimpleCapture::nameParameters);

class TestWithCamera : public testing::Test, public CameraHolder
{
protected:
	void SetUp() override { acquireCamera(); }
	void TearDown() override { releaseCamera(); }
};

class BufferPool : public TestWithCamera
{
protected:
	void SetUp() override
	{
		TestWithCamera::SetUp();

		config_ = camera_->generateConfiguration({ StreamRole::Viewfinder });
		ASSERT_TRUE(config_);
		ASSERT_FALSE(config_->empty());

		ASSERT_EQ(camera_->configure(config_.get()), 0);
	}

	void TearDown() override
	{
		config_.reset();
		TestWithCamera::TearDown();
	}

	std::vector<std::unique_ptr<Request>>
	createRequests(std::size_t count)
	{
		std::vector<std::unique_ptr<Request>> requests;

		for (std::size_t i = 0; i < count; i++) {
			auto request = camera_->createRequest(i);
			[&] { ASSERT_TRUE(request); }();

			for (const auto &cfg : *config_)
				request->enableStream(cfg.stream(), true);

			requests.push_back(std::move(request));
		}

		return requests;
	}

	std::unique_ptr<CameraConfiguration> config_;
};

template<typename T, typename... Args>
struct ScopedSignalReceiver {
	Signal<Args...> &signal;
	T *object = nullptr;

	template<typename Func>
	ScopedSignalReceiver(Signal<Args...> &s, T *o, Func f)
		: signal(s), object(o)
	{
		signal.connect(o, std::move(f));
	}

	~ScopedSignalReceiver()
	{
		signal.disconnect(object);
	}
};

template<typename T, typename... Args>
ScopedSignalReceiver(Signal<Args...> &, T *) -> ScopedSignalReceiver<T, Args...>;

struct ScopedCameraStart {
	Camera &camera;

	ScopedCameraStart(Camera &c)
		: camera(c)
	{
		[&]() { ASSERT_EQ(camera.start(), 0); }();
	}

	~ScopedCameraStart()
	{
		EXPECT_EQ(camera.stop(), 0);
	}
};

TEST_F(BufferPool, BufferBeforeRequest)
{
	FrameBufferAllocator fba(camera_);

	std::size_t buffers = -1;
	for (const auto &cfg : *config_) {
		ASSERT_GT(fba.allocate(cfg.stream()), 0);
		buffers = std::min(buffers, fba.buffers(cfg.stream()).size());
	}

	auto requests = createRequests(buffers);

	std::counting_semaphore<> requestCompletedSemaphore(0);
	bool requestCompletedOk = true;
	unsigned int requestCompleted = 0;
	ScopedSignalReceiver rcr(camera_->requestCompleted, this, [&](Request *request) {
		if (requestCompleted < requests.size())
			requestCompletedOk &= request == requests[requestCompleted].get();

		requestCompletedSemaphore.release(1);
		requestCompleted += 1;
	});

	{
		ScopedCameraStart scs(*camera_);

		for (const auto &cfg : *config_) {
			Stream *stream = cfg.stream();
			for (const auto &buffer : fba.buffers(stream))
				EXPECT_EQ(camera_->addBuffer(stream, buffer.get()), 0);
		}

		for (const auto &request : requests)
			ASSERT_EQ(camera_->queueRequest(request.get()), 0);

		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(buffers);

		for (size_t i = 0; i < buffers; i++) {
			if (!requestCompletedSemaphore.try_acquire_until(deadline))
				break;
		}
	}

	EXPECT_TRUE(requestCompletedOk);
	ASSERT_FALSE(requestCompletedSemaphore.try_acquire());
	EXPECT_EQ(requestCompleted, buffers);
}

TEST_F(BufferPool, RequestBeforeBuffer)
{
	FrameBufferAllocator fba(camera_);

	std::size_t buffers = -1;
	for (const auto &cfg : *config_) {
		ASSERT_GT(fba.allocate(cfg.stream()), 0);
		buffers = std::min(buffers, fba.buffers(cfg.stream()).size());
	}

	auto requests = createRequests(buffers);

	std::counting_semaphore<> requestCompletedSemaphore(0);
	bool requestCompletedOk = true;
	unsigned int requestCompleted = 0;
	ScopedSignalReceiver rcr(camera_->requestCompleted, this, [&](Request *request) {
		if (requestCompleted < requests.size())
			requestCompletedOk &= request == requests[requestCompleted].get();

		requestCompletedSemaphore.release(1);
		requestCompleted += 1;
	});

	{
		ScopedCameraStart scs(*camera_);

		for (const auto &request : requests)
			ASSERT_EQ(camera_->queueRequest(request.get()), 0);

		std::this_thread::sleep_for(std::chrono::seconds(2));
		ASSERT_FALSE(requestCompletedSemaphore.try_acquire());
		ASSERT_EQ(requestCompleted, 0);

		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(buffers);

		for (const auto &cfg : *config_) {
			Stream *stream = cfg.stream();
			for (const auto &buffer : fba.buffers(stream))
				EXPECT_EQ(camera_->addBuffer(stream, buffer.get()), 0);
		}

		for (size_t i = 0; i < buffers; i++) {
			if (!requestCompletedSemaphore.try_acquire_until(deadline))
				break;
		}
	}

	EXPECT_TRUE(requestCompletedOk);
	ASSERT_FALSE(requestCompletedSemaphore.try_acquire());
	EXPECT_EQ(requestCompleted, buffers);
}

TEST_F(BufferPool, BufferWithoutRequest)
{
	FrameBufferAllocator fba(camera_);
	std::set<std::pair<const Stream *, FrameBuffer *>> buffers;

	for (const auto &cfg : *config_) {
		Stream *stream = cfg.stream();
		ASSERT_GT(fba.allocate(stream), 0);

		for (const auto &buffer : fba.buffers(stream))
			buffers.insert({ stream, buffer.get() });
	}

	unsigned int requestCompleted = 0;
	ScopedSignalReceiver rcr(camera_->requestCompleted, this, [&](Request *) {
		requestCompleted += 1;
	});

	bool bufferCompletedOk = true;
	ScopedSignalReceiver bcr(camera_->bufferCompleted, this, [&](Request *request, const Stream *stream, FrameBuffer *buffer) {
		bufferCompletedOk &= !request;
		bufferCompletedOk &= buffer->metadata().status == libcamera::FrameMetadata::FrameCancelled;
		bufferCompletedOk &= buffers.erase({ stream, buffer });
	});

	{
		ScopedCameraStart scs(*camera_);

		for (const auto &cfg : *config_) {
			Stream *stream = cfg.stream();
			for (const auto &buffer : fba.buffers(stream))
				EXPECT_EQ(camera_->addBuffer(stream, buffer.get()), 0);
		}
	}

	EXPECT_EQ(requestCompleted, 0);
	EXPECT_EQ(buffers.size(), 0);
	EXPECT_TRUE(bufferCompletedOk);
}

TEST_F(BufferPool, RequestWithoutBuffers)
{
	constexpr std::size_t kRequestCount = 128;
	auto requests = createRequests(kRequestCount);

	unsigned int requestCompleted = 0;
	bool requestCompletedOk = true;
	ScopedSignalReceiver rcr(camera_->requestCompleted, this, [&](Request *request) {
		if (requestCompleted < requests.size()) {
			requestCompletedOk &= request == requests[requestCompleted].get();
			requestCompletedOk &= request->status() == Request::Status::RequestCancelled;
		}

		requestCompleted += 1;
	});

	unsigned int bufferCompleted = 0;
	ScopedSignalReceiver bcr(camera_->bufferCompleted, this, [&](Request *, const Stream *, FrameBuffer *) {
		bufferCompleted += 1;
	});

	{
		ScopedCameraStart scs(*camera_);

		for (const auto &request : requests)
			ASSERT_EQ(camera_->queueRequest(request.get()), 0);

		std::this_thread::sleep_for(std::chrono::seconds(2));
	}

	EXPECT_EQ(requestCompleted, requests.size());
	EXPECT_TRUE(requestCompletedOk);
	EXPECT_EQ(bufferCompleted, 0);
}

} /* namespace */
