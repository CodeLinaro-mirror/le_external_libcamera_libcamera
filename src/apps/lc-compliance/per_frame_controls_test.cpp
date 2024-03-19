/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * per_frame_controls.cpp - Tests for per frame controls
 */
#include <gtest/gtest.h>

#include "environment.h"
#include "simple_capture.h"
#include "time_sheet.h"

using namespace libcamera;

class PerFrameControlTests : public testing::Test
{
protected:
	void SetUp() override;
	void TearDown() override;

	std::shared_ptr<Camera> camera_;
};

/*
 * We use gtest's SetUp() and TearDown() instead of constructor and destructor
 * in order to be able to assert on them.
 */
void PerFrameControlTests::SetUp()
{
	Environment *env = Environment::get();

	camera_ = env->cm()->get(env->cameraId());

	ASSERT_EQ(camera_->acquire(), 0);
}

void PerFrameControlTests::TearDown()
{
	if (!camera_)
		return;

	camera_->release();
	camera_.reset();
}

class PerFrameControlsCapture : public SimpleCapture
{
public:
	PerFrameControlsCapture(std::shared_ptr<libcamera::Camera> camera);

	std::shared_ptr<TimeSheet>
	startCaptureWithTimeSheet(unsigned int framesToCapture,
				  const libcamera::ControlList *controls = nullptr);

	void runCaptureSession();
	int queueRequest(libcamera::Request *request);
	void requestComplete(libcamera::Request *request) override;

	unsigned int queueCount_;
	unsigned int captureCount_;
	unsigned int captureLimit_;

	std::weak_ptr<TimeSheet> timeSheet_;
};

static const bool doImageTests = true;

PerFrameControlsCapture::PerFrameControlsCapture(std::shared_ptr<Camera> camera)
	: SimpleCapture(camera)
{
}

std::shared_ptr<TimeSheet>
PerFrameControlsCapture::startCaptureWithTimeSheet(unsigned int framesToCapture,
						   const ControlList *controls)
{
	ControlList ctrls(camera_->controls().idmap());

	/* Ensure defined default values */
	ctrls.set(controls::AeEnable, false);
	ctrls.set(controls::AeExposureMode, controls::ExposureCustom);
	ctrls.set(controls::ExposureTime, 10000);
	ctrls.set(controls::AnalogueGain, 1.0);

	if (controls)
		ctrls.merge(*controls, ControlList::MergePolicy::OverwriteExisting);

	start(&ctrls);

	queueCount_ = 0;
	captureCount_ = 0;
	captureLimit_ = framesToCapture;

	auto timeSheet = std::make_shared<TimeSheet>(captureLimit_,
						     camera_->controls().idmap());
	timeSheet_ = timeSheet;
	return timeSheet;
}

int PerFrameControlsCapture::queueRequest(Request *request)
{
	queueCount_++;
	if (queueCount_ > captureLimit_)
		return 0;

	auto ts = timeSheet_.lock();
	if (ts)
		ts->prepareForQueue(request, queueCount_ - 1);

	return camera_->queueRequest(request);
}

void PerFrameControlsCapture::requestComplete(Request *request)
{
	auto ts = timeSheet_.lock();
	if (ts)
		ts->handleCompleteRequest(request);

	captureCount_++;
	if (captureCount_ >= captureLimit_) {
		loop_->exit(0);
		return;
	}

	request->reuse(Request::ReuseBuffers);
	if (queueRequest(request))
		loop_->exit(-EINVAL);
}

void PerFrameControlsCapture::runCaptureSession()
{
	Stream *stream = config_->at(0).stream();
	const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator_->buffers(stream);

	/* Queue the recommended number of requests. */
	for (const std::unique_ptr<FrameBuffer> &buffer : buffers) {
		std::unique_ptr<Request> request = camera_->createRequest();
		request->addBuffer(stream, buffer.get());
		queueRequest(request.get());
		requests_.push_back(std::move(request));
	}

	/* Run capture session. */
	loop_ = new EventLoop();
	loop_->exec();
	stop();
	delete loop_;
}

TEST_F(PerFrameControlTests, testExposureGainChangeOnSameFrame)
{
	PerFrameControlsCapture capture(camera_);
	capture.configure(StreamRole::VideoRecording);

	ControlList startValues;
	startValues.set(controls::ExposureTime, 5000);
	startValues.set(controls::AnalogueGain, 1.0);

	auto timeSheet = capture.startCaptureWithTimeSheet(10, &startValues);
	auto &ts = *timeSheet;

	/* wait a few frames to settle */
	ts[7].controls().set(controls::ExposureTime, 10000);
	ts[7].controls().set(controls::AnalogueGain, 4.0);

	capture.runCaptureSession();

	ASSERT_TRUE(ts[5].metadata().contains(controls::ExposureTime.id()))
		<< "Required metadata entry is missing";
	ASSERT_TRUE(ts[5].metadata().contains(controls::AnalogueGain.id()))
		<< "Required metadata entry is missing";

	EXPECT_NEAR(ts[3].metadata().get(controls::ExposureTime).value(), 5000, 20);
	EXPECT_NEAR(ts[3].metadata().get(controls::AnalogueGain).value(), 1.0, 0.05);

	/* find the frame with the changes */
	int exposureChangeIndex = 0;
	for (unsigned i = 3; i < ts.size(); i++) {
		if (ts[i].metadata().get(controls::ExposureTime).value() > 7500) {
			exposureChangeIndex = i;
			break;
		}
	}

	int gainChangeIndex = 0;
	for (unsigned i = 3; i < ts.size(); i++) {
		if (ts[i].metadata().get(controls::AnalogueGain).value() > 2.0) {
			gainChangeIndex = i;
			break;
		}
	}

	EXPECT_NE(exposureChangeIndex, 0) << "Exposure change not found in metadata";
	EXPECT_NE(gainChangeIndex, 0) << "Gain change not found in metadata";
	EXPECT_EQ(exposureChangeIndex, gainChangeIndex)
		<< "Metadata contained gain and exposure changes on different frames";

	if (doImageTests) {
		int brightnessChangeIndex = 0;
		for (unsigned i = 3; i < ts.size(); i++) {
			if (ts[i].brightnessChange() > 1.3) {
				EXPECT_EQ(brightnessChangeIndex, 0)
					<< "Detected multiple frames with brightness increase"
					<< " (Wrong control delays?)";

				if (!brightnessChangeIndex)
					brightnessChangeIndex = i;
			}
		}

		EXPECT_EQ(exposureChangeIndex, brightnessChangeIndex)
			<< "Exposure change and measured brightness change were not on same"
			<< " frame. (Wrong control delay?, Start frame event too late?)";
		EXPECT_EQ(exposureChangeIndex, gainChangeIndex)
			<< "Gain change and measured brightness change were not on same "
			<< " frame. (Wrong control delay?, Start frame event too late?)";
	}
}

TEST_F(PerFrameControlTests, testFramePreciseExposureChange)
{
	PerFrameControlsCapture capture(camera_);
	capture.configure(StreamRole::VideoRecording);

	auto timeSheet = capture.startCaptureWithTimeSheet(10);
	auto &ts = *timeSheet;

	ts[3].controls().set(controls::ExposureTime, 5000);
	/* wait a few frames to settle */
	ts[6].controls().set(controls::ExposureTime, 20000);

	capture.runCaptureSession();

	ASSERT_TRUE(ts[5].metadata().contains(controls::ExposureTime.id()))
		<< "Required metadata entry is missing";

	EXPECT_NEAR(ts[5].metadata().get(controls::ExposureTime).value(), 5000, 20);
	EXPECT_NEAR(ts[6].metadata().get(controls::ExposureTime).value(), 20000, 20);

	if (doImageTests) {
		/* No increase just before setting exposure */
		EXPECT_NEAR(ts[5].brightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much before the expected time of change"
			<< " (control delay too high?).";
		/*
		 * \todo The change is brightness was a bit low
		 * (Exposure time increase by 4x resulted in a brightness increase of < 2).
		 * This should be investigated.
		 */
		EXPECT_GT(ts[6].brightnessChange(), 1.3)
			<< "Brightness in frame " << 6 << " did not increase as expected"
			<< " (reference: " << ts[3].spotBrightness() << " current: "
			<< ts[6].spotBrightness() << " )" << std::endl;

		/* No increase just after setting exposure */
		EXPECT_NEAR(ts[7].brightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much after the expected time of change"
			<< " (control delay too low?).";

		/* No increase just after setting exposure */
		EXPECT_NEAR(ts[8].brightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much 2 frames after the expected time"
			<< " of change (control delay too low?).";
	}
}

TEST_F(PerFrameControlTests, testFramePreciseGainChange)
{
	PerFrameControlsCapture capture(camera_);
	capture.configure(StreamRole::VideoRecording);

	auto timeSheet = capture.startCaptureWithTimeSheet(10);
	auto &ts = *timeSheet;

	ts[3].controls().set(controls::AnalogueGain, 1.0);
	/* wait a few frames to settle */
	ts[6].controls().set(controls::AnalogueGain, 4.0);

	capture.runCaptureSession();

	ASSERT_TRUE(ts[5].metadata().contains(controls::AnalogueGain.id()))
		<< "Required metadata entry is missing";

	EXPECT_NEAR(ts[5].metadata().get(controls::AnalogueGain).value(), 1.0, 0.1);
	EXPECT_NEAR(ts[6].metadata().get(controls::AnalogueGain).value(), 4.0, 0.1);

	if (doImageTests) {
		/* No increase just before setting gain */
		EXPECT_NEAR(ts[5].brightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much before the expected time of change"
			<< " (control delay too high?).";
		/*
		 * \todo I see a brightness change of roughly half the expected one.
		 * This is not yet understood and needs investigation
		 */
		EXPECT_GT(ts[6].brightnessChange(), 1.7)
			<< "Brightness in frame " << 6 << " did not increase as expected"
			<< " (reference: " << ts[5].spotBrightness()
			<< " current: " << ts[6].spotBrightness() << ")" << std::endl;

		/* No increase just after setting gain */
		EXPECT_NEAR(ts[7].brightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much after the expected time of change"
			<< " (control delay too low?).";

		/* No increase just after setting gain */
		EXPECT_NEAR(ts[8].brightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much after the expected time of change"
			<< " (control delay too low?).";
	}
}

TEST_F(PerFrameControlTests, testExposureGainFromFirstRequestGetsApplied)
{
	PerFrameControlsCapture capture(camera_);
	capture.configure(StreamRole::VideoRecording);

	auto timeSheet = capture.startCaptureWithTimeSheet(5);
	auto &ts = *timeSheet;

	ts[0].controls().set(controls::ExposureTime, 10000);
	ts[0].controls().set(controls::AnalogueGain, 4.0);

	capture.runCaptureSession();

	ASSERT_TRUE(ts[4].metadata().contains(controls::ExposureTime.id()))
		<< "Required metadata entry is missing";
	ASSERT_TRUE(ts[4].metadata().contains(controls::AnalogueGain.id()))
		<< "Required metadata entry is missing";

	/* We expect it to be applied after 3 frames, the latest*/
	EXPECT_NEAR(ts[4].metadata().get(controls::ExposureTime).value(), 10000, 20);
	EXPECT_NEAR(ts[4].metadata().get(controls::AnalogueGain).value(), 4.0, 0.1);
}

TEST_F(PerFrameControlTests, testExposureGainFromFirstAndSecondRequestGetsApplied)
{
	PerFrameControlsCapture capture(camera_);
	capture.configure(StreamRole::VideoRecording);

	auto timeSheet = capture.startCaptureWithTimeSheet(5);
	auto &ts = *timeSheet;

	ts[0].controls().set(controls::ExposureTime, 8000);
	ts[0].controls().set(controls::AnalogueGain, 2.0);
	ts[1].controls().set(controls::ExposureTime, 10000);
	ts[1].controls().set(controls::AnalogueGain, 4.0);

	capture.runCaptureSession();

	ASSERT_TRUE(ts[4].metadata().contains(controls::ExposureTime.id()))
		<< "Required metadata entry is missing";
	ASSERT_TRUE(ts[4].metadata().contains(controls::AnalogueGain.id()))
		<< "Required metadata entry is missing";

	/* We expect it to be applied after 3 frames, the latest */
	EXPECT_NEAR(ts[4].metadata().get(controls::ExposureTime).value(), 10000, 20);
	EXPECT_NEAR(ts[4].metadata().get(controls::AnalogueGain).value(), 4.0, 0.1);
}

TEST_F(PerFrameControlTests, testExposureGainIsAppliedOnFirstFrame)
{
	PerFrameControlsCapture capture(camera_);
	capture.configure(StreamRole::VideoRecording);

	ControlList startValues;
	startValues.set(controls::ExposureTime, 5000);
	startValues.set(controls::AnalogueGain, 1.0);

	auto ts1 = capture.startCaptureWithTimeSheet(3, &startValues);

	capture.runCaptureSession();

	ASSERT_TRUE((*ts1)[0].metadata().contains(controls::ExposureTime.id()))
		<< "Required metadata entry is missing";
	ASSERT_TRUE((*ts1)[0].metadata().contains(controls::AnalogueGain.id()))
		<< "Required metadata entry is missing";

	EXPECT_NEAR((*ts1)[0].metadata().get(controls::ExposureTime).value(), 5000, 20);
	EXPECT_NEAR((*ts1)[0].metadata().get(controls::AnalogueGain).value(), 1.0, 0.02);

	/* Second capture with different values to ensure we don't hit default/old values */
	startValues.set(controls::ExposureTime, 15000);
	startValues.set(controls::AnalogueGain, 4.0);

	auto ts2 = capture.startCaptureWithTimeSheet(3, &startValues);

	capture.runCaptureSession();

	EXPECT_NEAR((*ts2)[0].metadata().get(controls::ExposureTime).value(), 15000, 20);
	EXPECT_NEAR((*ts2)[0].metadata().get(controls::AnalogueGain).value(), 4.0, 0.02);

	if (doImageTests) {
		/* With 3x exposure and 4x gain we could expect a brightness increase of 2x */
		double brightnessChange = ts2->get(1).spotBrightness() / ts1->get(1).spotBrightness();
		EXPECT_GT(brightnessChange, 2.0);
	}
}
