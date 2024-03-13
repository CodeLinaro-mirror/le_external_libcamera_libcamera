/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * per_frame_controls.cpp - Tests for per frame controls
 */
#include "per_frame_controls.h"

#include <gtest/gtest.h>

#include "time_sheet.h"

using namespace libcamera;

static const bool doImageTests = true;

PerFrameControls::PerFrameControls(std::shared_ptr<Camera> camera)
	: SimpleCapture(camera)
{
}

std::shared_ptr<TimeSheet>
PerFrameControls::startCaptureWithTimeSheet(unsigned int framesToCapture, const ControlList *controls)
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

	auto timeSheet = std::make_shared<TimeSheet>(captureLimit_, camera_->controls().idmap());
	timeSheet_ = timeSheet;
	return timeSheet;
}

int PerFrameControls::queueRequest(Request *request)
{
	queueCount_++;
	if (queueCount_ > captureLimit_)
		return 0;

	auto ts = timeSheet_.lock();
	if (ts)
		ts->prepareForQueue(request, queueCount_ - 1);

	return camera_->queueRequest(request);
}

void PerFrameControls::requestComplete(Request *request)
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

void PerFrameControls::runCaptureSession()
{
	Stream *stream = config_->at(0).stream();
	const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator_->buffers(stream);

	/* Queue the recommended number of reqeuests. */
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

void PerFrameControls::testExposureGainChangeOnSameFrame()
{
	ControlList startValues;
	startValues.set(controls::ExposureTime, 5000);
	startValues.set(controls::AnalogueGain, 1.0);

	auto timeSheet = startCaptureWithTimeSheet(10, &startValues);
	auto &ts = *timeSheet;

	/* wait a few frames to settle */
	ts[7].controls().set(controls::ExposureTime, 10000);
	ts[7].controls().set(controls::AnalogueGain, 4.0);

	runCaptureSession();

	/* Uncomment this to debug the test */
	/* ts.printAllInfos(); */

	ASSERT_TRUE(ts[5].metadata().contains(controls::ExposureTime.id())) << "Required metadata entry is missing";
	ASSERT_TRUE(ts[5].metadata().contains(controls::AnalogueGain.id())) << "Required metadata entry is missing";

	EXPECT_NEAR(ts[3].metadata().get(controls::ExposureTime).value(), 5000, 20);
	EXPECT_NEAR(ts[3].metadata().get(controls::AnalogueGain).value(), 1.0, 0.05);

	//find the frame with the changes
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
			if (ts[i].getBrightnessChange() > 1.3) {
				EXPECT_EQ(brightnessChangeIndex, 0)
					<< "Detected multiple frames with brightness increase (Wrong control delays?)";

				if (!brightnessChangeIndex)
					brightnessChangeIndex = i;
			}
		}

		EXPECT_EQ(exposureChangeIndex, brightnessChangeIndex)
			<< "Exposure change and mesaured brightness change were not on same frame. "
			<< "(Wrong control delay?, Start frame event too late?)";
		EXPECT_EQ(exposureChangeIndex, gainChangeIndex)
			<< "Gain change and mesaured brightness change were not on same frame. "
			<< "(Wrong control delay?, Start frame event too late?)";
	}
}

void PerFrameControls::testFramePreciseExposureChange()
{
	auto timeSheet = startCaptureWithTimeSheet(10);
	auto &ts = *timeSheet;

	ts[3].controls().set(controls::ExposureTime, 5000);
	/* wait a few frames to settle */
	ts[6].controls().set(controls::ExposureTime, 20000);

	runCaptureSession();

	/* Uncomment this to debug the test */
	/* ts.printAllInfos(); */

	ASSERT_TRUE(ts[5].metadata().contains(controls::ExposureTime.id())) << "Required metadata entry is missing";

	EXPECT_NEAR(ts[5].metadata().get(controls::ExposureTime).value(), 5000, 20);
	EXPECT_NEAR(ts[6].metadata().get(controls::ExposureTime).value(), 20000, 20);

	if (doImageTests) {
		/* No increase just before setting exposure */
		EXPECT_NEAR(ts[5].getBrightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much before the expected time of change (control delay too high?).";
		/*
		* Todo: The change is brightness was a bit low
		* (Exposure time increase by 4x resulted in a brightness increase of < 2).
		* This should be investigated.
		*/
		EXPECT_GT(ts[6].getBrightnessChange(), 1.3)
			<< "Brightness in frame " << 6 << " did not increase as expected (reference: "
			<< ts[3].getSpotBrightness() << " current: " << ts[6].getSpotBrightness() << " )" << std::endl;

		/* No increase just after setting exposure */
		EXPECT_NEAR(ts[7].getBrightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much after the expected time of change (control delay too low?).";

		/* No increase just after setting exposure */
		EXPECT_NEAR(ts[8].getBrightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much 2 frames after the expected time of change (control delay too low?).";
	}
}

void PerFrameControls::testFramePreciseGainChange()
{
	auto timeSheet = startCaptureWithTimeSheet(10);
	auto &ts = *timeSheet;

	ts[3].controls().set(controls::AnalogueGain, 1.0);
	/* wait a few frames to settle */
	ts[6].controls().set(controls::AnalogueGain, 4.0);

	runCaptureSession();

	/* Uncomment this, to debug the test */
	/* ts.printAllInfos(); */

	ASSERT_TRUE(ts[5].metadata().contains(controls::AnalogueGain.id())) << "Required metadata entry is missing";

	EXPECT_NEAR(ts[5].metadata().get(controls::AnalogueGain).value(), 1.0, 0.1);
	EXPECT_NEAR(ts[6].metadata().get(controls::AnalogueGain).value(), 4.0, 0.1);

	if (doImageTests) {
		/* No increase just before setting gain */
		EXPECT_NEAR(ts[5].getBrightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much before the expected time of change (control delay too high?).";
		/*
		* Todo: I see a brightness change of roughly half the expected one.
		* This is not yet understood and needs investigation
		*/
		EXPECT_GT(ts[6].getBrightnessChange(), 1.7)
			<< "Brightness in frame " << 6 << " did not increase as expected (reference: "
			<< ts[5].getSpotBrightness() << " current: " << ts[6].getSpotBrightness() << " )" << std::endl;

		/* No increase just after setting gain */
		EXPECT_NEAR(ts[7].getBrightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much after the expected time of change (control delay too low?).";

		/* No increase just after setting gain */
		EXPECT_NEAR(ts[8].getBrightnessChange(), 1.0, 0.05)
			<< "Brightness changed too much after the expected time of change (control delay too low?).";
	}
}

void PerFrameControls::testExposureGainFromFirstRequestGetsApplied()
{
	auto timeSheet = startCaptureWithTimeSheet(5);
	auto &ts = *timeSheet;

	ts[0].controls().set(controls::ExposureTime, 10000);
	ts[0].controls().set(controls::AnalogueGain, 4.0);

	runCaptureSession();

	ASSERT_TRUE(ts[4].metadata().contains(controls::ExposureTime.id())) << "Required metadata entry is missing";
	ASSERT_TRUE(ts[4].metadata().contains(controls::AnalogueGain.id())) << "Required metadata entry is missing";

	/* We expect it to be applied after 3 frames, the latest*/
	EXPECT_NEAR(ts[4].metadata().get(controls::ExposureTime).value(), 10000, 20);
	EXPECT_NEAR(ts[4].metadata().get(controls::AnalogueGain).value(), 4.0, 0.1);
}

void PerFrameControls::testExposureGainFromFirstAndSecondRequestGetsApplied()
{
	auto timeSheet = startCaptureWithTimeSheet(5);
	auto &ts = *timeSheet;

	ts[0].controls().set(controls::ExposureTime, 8000);
	ts[0].controls().set(controls::AnalogueGain, 2.0);
	ts[1].controls().set(controls::ExposureTime, 10000);
	ts[1].controls().set(controls::AnalogueGain, 4.0);

	runCaptureSession();

	ASSERT_TRUE(ts[4].metadata().contains(controls::ExposureTime.id())) << "Required metadata entry is missing";
	ASSERT_TRUE(ts[4].metadata().contains(controls::AnalogueGain.id())) << "Required metadata entry is missing";

	/* We expect it to be applied after 3 frames, the latest */
	EXPECT_NEAR(ts[4].metadata().get(controls::ExposureTime).value(), 10000, 20);
	EXPECT_NEAR(ts[4].metadata().get(controls::AnalogueGain).value(), 4.0, 0.1);
}

void PerFrameControls::testExposureGainIsAppliedOnFirstFrame()
{
	ControlList startValues;
	startValues.set(controls::ExposureTime, 5000);
	startValues.set(controls::AnalogueGain, 1.0);

	auto ts1 = startCaptureWithTimeSheet(3, &startValues);

	runCaptureSession();

	ASSERT_TRUE((*ts1)[0].metadata().contains(controls::ExposureTime.id())) << "Required metadata entry is missing";
	ASSERT_TRUE((*ts1)[0].metadata().contains(controls::AnalogueGain.id())) << "Required metadata entry is missing";

	EXPECT_NEAR((*ts1)[0].metadata().get(controls::ExposureTime).value(), 5000, 20);
	EXPECT_NEAR((*ts1)[0].metadata().get(controls::AnalogueGain).value(), 1.0, 0.02);

	/* Second capture with different values to ensure we don't hit default/old values */
	startValues.set(controls::ExposureTime, 15000);
	startValues.set(controls::AnalogueGain, 4.0);

	auto ts2 = startCaptureWithTimeSheet(3, &startValues);

	runCaptureSession();

	EXPECT_NEAR((*ts2)[0].metadata().get(controls::ExposureTime).value(), 15000, 20);
	EXPECT_NEAR((*ts2)[0].metadata().get(controls::AnalogueGain).value(), 4.0, 0.02);

	if (doImageTests) {
		/* With 3x exposure and 4x gain we could expect a brightness increase of 2x */
		double brightnessChange = ts2->get(1).getSpotBrightness() / ts1->get(1).getSpotBrightness();
		EXPECT_GT(brightnessChange, 2.0);
	}
}
