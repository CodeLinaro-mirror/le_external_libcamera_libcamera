/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * delayed_controls.cpp - libcamera delayed controls test
 */

#include <iostream>

#include "libcamera/internal/delayed_controls.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/v4l2_videodevice.h"

#include "test.h"

using namespace std;
using namespace libcamera;

class DelayedControlsTest : public Test
{
public:
	DelayedControlsTest()
	{
	}

protected:
	int init() override
	{
		enumerator_ = DeviceEnumerator::create();
		if (!enumerator_) {
			cerr << "Failed to create device enumerator" << endl;
			return TestFail;
		}

		if (enumerator_->enumerate()) {
			cerr << "Failed to enumerate media devices" << endl;
			return TestFail;
		}

		DeviceMatch dm("vivid");
		dm.add("vivid-000-vid-cap");

		media_ = enumerator_->search(dm);
		if (!media_) {
			cerr << "vivid video device found" << endl;
			return TestSkip;
		}

		dev_ = V4L2VideoDevice::fromEntityName(media_.get(), "vivid-000-vid-cap");
		if (dev_->open()) {
			cerr << "Failed to open video device" << endl;
			return TestFail;
		}

		const ControlInfoMap &infoMap = dev_->controls();

		/* Make sure the controls we require are present. */
		if (infoMap.empty()) {
			cerr << "Failed to enumerate controls" << endl;
			return TestFail;
		}

		if (infoMap.find(V4L2_CID_BRIGHTNESS) == infoMap.end() ||
		    infoMap.find(V4L2_CID_CONTRAST) == infoMap.end()) {
			cerr << "Missing controls" << endl;
			return TestFail;
		}

		return TestPass;
	}

	int singleControlNoDelay()
	{
		std::unordered_map<uint32_t, DelayedControls::ControlParams> delays = {
			{ V4L2_CID_BRIGHTNESS, { 0, false } },
		};
		std::unique_ptr<DelayedControls> delayed =
			std::make_unique<DelayedControls>(dev_.get(), delays);
		ControlList ctrls;

		/* Reset control to value not used in test. */
		ctrls.set(V4L2_CID_BRIGHTNESS, 1);
		dev_->setControls(&ctrls);
		delayed->reset();

		/* Test control without delay are set at once. */
		for (unsigned int i = 0; i < 100; i++) {
			int32_t value = 100 + i;

			ctrls.set(V4L2_CID_BRIGHTNESS, value);
			delayed->push(ctrls);

			delayed->applyControls(i);

			ControlList result = delayed->get(i);
			int32_t brightness = result.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
			if (brightness != value) {
				cerr << "Failed single control without delay"
				     << " frame " << i
				     << " expected " << value
				     << " got " << brightness
				     << endl;
				return TestFail;
			}
		}

		return TestPass;
	}

	int singleControlWithDelay()
	{
		std::unordered_map<uint32_t, DelayedControls::ControlParams> delays = {
			{ V4L2_CID_BRIGHTNESS, { 1, false } },
		};
		std::unique_ptr<DelayedControls> delayed =
			std::make_unique<DelayedControls>(dev_.get(), delays);
		ControlList ctrls;

		/* Reset control to value that will be first in test. */
		int32_t initial = 4;
		ctrls.set(V4L2_CID_BRIGHTNESS, initial);
		dev_->setControls(&ctrls);
		delayed->reset();

		/* Push a request for frame 0 */
		ctrls.set(V4L2_CID_BRIGHTNESS, 10);
		delayed->push(ctrls);

		/* Test single control with delay. */
		for (unsigned int i = 0; i < 100; i++) {
			int32_t value = 10 + i;
			int32_t expected = i < 1 ? initial : value;

			/* push the request for frame i+1 */
			ctrls.set(V4L2_CID_BRIGHTNESS, value + 1);
			delayed->push(ctrls);

			delayed->applyControls(i);

			ControlList result = delayed->get(i);
			int32_t brightness = result.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
			ControlList ctrlsV4L = dev_->getControls({ V4L2_CID_BRIGHTNESS });
			int32_t brightnessV4L = ctrlsV4L.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
			if (brightness != expected) {
				cerr << "Failed single control with delay"
				     << " frame " << i
				     << " expected " << expected
				     << " got " << brightness
				     << endl;
				return TestFail;
			}

			if (i > 0 && brightnessV4L != value + 1) {
				cerr << "Failed single control with delay"
				     << " frame " << i
				     << " expected V4L " << value + 1
				     << " got " << brightnessV4L
				     << endl;
				return TestFail;
			}
		}

		return TestPass;
	}

	/* This fails on the old delayed controls implementation */
	int singleControlWithDelayStartUp()
	{
		std::unordered_map<uint32_t, DelayedControls::ControlParams> delays = {
			{ V4L2_CID_BRIGHTNESS, { 1, false } },
		};
		std::unique_ptr<DelayedControls> delayed =
			std::make_unique<DelayedControls>(dev_.get(), delays);
		ControlList ctrls;

		/* Reset control to value that will be first in test. */
		int32_t initial = 4;
		ctrls.set(V4L2_CID_BRIGHTNESS, initial);
		dev_->setControls(&ctrls);
		delayed->reset();

		/* push a request for frame 0 */
		ctrls.set(V4L2_CID_BRIGHTNESS, 10);
		delayed->push(ctrls);

		/* Test single control with delay. */
		for (unsigned int i = 0; i < 100; i++) {
			int32_t value = 10 + i;
			int32_t expected = i < 1 ? initial : value;

			/* push the request for frame i+1 */
			ctrls.set(V4L2_CID_BRIGHTNESS, value + 1);
			delayed->push(ctrls);

			delayed->applyControls(i);

			ControlList result = delayed->get(i);
			int32_t brightness = result.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
			ControlList ctrlsV4L = dev_->getControls({ V4L2_CID_BRIGHTNESS });
			int32_t brightnessV4L = ctrlsV4L.get(V4L2_CID_BRIGHTNESS).get<int32_t>();

			if (brightness != expected) {
				cerr << "Failed single control with delay start up"
				     << " frame " << i
				     << " expected " << expected
				     << " got " << brightness
				     << endl;
				return TestFail;
			}

			if (i > 0 && brightnessV4L != value + 1) {
				cerr << "Failed single control with delay start up"
				     << " frame " << i
				     << " expected V4L " << value + 1
				     << " got " << brightnessV4L
				     << endl;
				return TestFail;
			}
		}

		return TestPass;
	}

	/* This fails on the old delayed controls implementation */
	int doNotLoseFirstRequest()
	{
		/* no delay at all */
		std::unordered_map<uint32_t, DelayedControls::ControlParams> delays = {
			{ V4L2_CID_BRIGHTNESS, { 0, false } },
		};
		std::unique_ptr<DelayedControls> delayed =
			std::make_unique<DelayedControls>(dev_.get(), delays);
		ControlList ctrls;

		/* Reset control to value that will be first in test. */
		int32_t initial = 4;
		ctrls.set(V4L2_CID_BRIGHTNESS, initial);
		dev_->setControls(&ctrls);
		delayed->reset();

		/* push a request for frame 0 */
		int32_t expected = 10;
		ctrls.set(V4L2_CID_BRIGHTNESS, expected);
		delayed->push(ctrls);
		delayed->applyControls(0);

		ControlList result = delayed->get(0);
		int32_t brightness = result.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
		ControlList ctrlsV4L = dev_->getControls({ V4L2_CID_BRIGHTNESS });
		int32_t brightnessV4L = ctrlsV4L.get(V4L2_CID_BRIGHTNESS).get<int32_t>();

		if (brightness != expected) {
			cerr << "Failed doNotLoseFirstRequest"
			     << " frame " << 0
			     << " expected " << expected
			     << " got " << brightness
			     << endl;
			return TestFail;
		}

		if (brightnessV4L != expected) {
			cerr << "Failed doNotLoseFirstRequest"
			     << " frame " << 0
			     << " expected V4L " << expected
			     << " got " << brightnessV4L
			     << endl;
			return TestFail;
		}

		return TestPass;
	}

	int updateTooLateMustSometimesBeIgnored()
	{
		std::unordered_map<uint32_t, DelayedControls::ControlParams> delays = {
			{ V4L2_CID_BRIGHTNESS, { 2, false } },
		};
		std::unique_ptr<DelayedControls> delayed =
			std::make_unique<DelayedControls>(dev_.get(), delays);
		ControlList ctrls;

		/* Reset control to value that will be first in test. */
		int32_t initial = 4;
		ctrls.set(V4L2_CID_BRIGHTNESS, initial);
		dev_->setControls(&ctrls);
		delayed->reset();

		int32_t expected = 10;

		delayed->push({}, 0);
		delayed->push({}, 1);
		ctrls.set(V4L2_CID_BRIGHTNESS, expected);
		delayed->push(ctrls, 2);
		delayed->applyControls(0); /* puts 10 on the bus */

		/*
		 * Post an update for frame 1. It's too late to fulfill that request,
		 * delayed controls will therefore try to delay it to frame 3. But as
		 * frame 2 is already queued, the update must be dropped.
		 */
		ctrls.set(V4L2_CID_BRIGHTNESS, 20);
		delayed->push(ctrls, 1);
		delayed->applyControls(1);
		delayed->applyControls(2);
		delayed->applyControls(3);

		int frame = 3;

		ControlList result = delayed->get(frame);
		int32_t brightness = result.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
		ControlList ctrlsV4L = dev_->getControls({ V4L2_CID_BRIGHTNESS });
		int32_t brightnessV4L = ctrlsV4L.get(V4L2_CID_BRIGHTNESS).get<int32_t>();

		if (brightness != expected) {
			cerr << "Failed " << __func__
			     << " frame " << frame
			     << " expected " << expected
			     << " got " << brightness
			     << endl;
			return TestFail;
		}

		if (brightnessV4L != expected) {
			cerr << "Failed " << __func__
			     << " frame " << frame
			     << " expected V4L " << expected
			     << " got " << brightnessV4L
			     << endl;
			return TestFail;
		}

		return TestPass;
	}

	int updateTooLateGetsDelayed()
	{
		std::unordered_map<uint32_t, DelayedControls::ControlParams> delays = {
			{ V4L2_CID_BRIGHTNESS, { 2, false } },
		};
		std::unique_ptr<DelayedControls> delayed =
			std::make_unique<DelayedControls>(dev_.get(), delays);
		ControlList ctrls;

		/* Reset control to value that will be first in test. */
		int32_t initial = 4;
		ctrls.set(V4L2_CID_BRIGHTNESS, initial);
		dev_->setControls(&ctrls);
		delayed->reset();

		int32_t expected = 10;

		delayed->push({}, 0);
		delayed->push({}, 1);
		/* push a request for frame 2 */
		ctrls.set(V4L2_CID_BRIGHTNESS, 40);
		delayed->push(ctrls, 2);

		delayed->applyControls(0);
		delayed->applyControls(1);
		/*
		 * update frame 2 to the correct value. But it is too late, delayed
		 * controls will delay and the value shall be available on frame 4
		 */
		ctrls.set(V4L2_CID_BRIGHTNESS, expected);
		delayed->push(ctrls, 2);
		delayed->applyControls(2);
		delayed->applyControls(3);
		delayed->applyControls(4);

		int frame = 4;

		ControlList result = delayed->get(frame);
		int32_t brightness = result.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
		ControlList ctrlsV4L = dev_->getControls({ V4L2_CID_BRIGHTNESS });
		int32_t brightnessV4L = ctrlsV4L.get(V4L2_CID_BRIGHTNESS).get<int32_t>();

		if (brightness != expected) {
			cerr << "Failed " << __func__
			     << " frame " << frame
			     << " expected " << expected
			     << " got " << brightness
			     << endl;
			return TestFail;
		}

		if (brightnessV4L != expected) {
			cerr << "Failed " << __func__
			     << " frame " << frame
			     << " expected V4L " << expected
			     << " got " << brightnessV4L
			     << endl;
			return TestFail;
		}

		return TestPass;
	}

	int dualControlsWithDelay()
	{
		static const int maxDelay = 2;

		std::unordered_map<uint32_t, DelayedControls::ControlParams> delays = {
			{ V4L2_CID_BRIGHTNESS, { 1, false } },
			{ V4L2_CID_CONTRAST, { maxDelay, false } },
		};
		std::unique_ptr<DelayedControls> delayed =
			std::make_unique<DelayedControls>(dev_.get(), delays);
		ControlList ctrls;

		/* Reset control to value that will be first two frames in test. */
		int32_t expected = 200;
		ctrls.set(V4L2_CID_BRIGHTNESS, expected);
		ctrls.set(V4L2_CID_CONTRAST, expected + 1);
		dev_->setControls(&ctrls);
		delayed->reset();

		/* push two requests into the queue */
		ctrls.set(V4L2_CID_BRIGHTNESS, 10);
		ctrls.set(V4L2_CID_CONTRAST, 10);
		delayed->push(ctrls);
		ctrls.set(V4L2_CID_BRIGHTNESS, 11);
		ctrls.set(V4L2_CID_CONTRAST, 11);
		delayed->push(ctrls);

		/* Test dual control with delay. */
		for (unsigned int i = 0; i < 100; i++) {
			int32_t value = 10 + i;

			/* we are pushing 2 frames ahead */
			ctrls.set(V4L2_CID_BRIGHTNESS, value + 2);
			ctrls.set(V4L2_CID_CONTRAST, value + 2);
			delayed->push(ctrls);

			delayed->applyControls(i);

			ControlList result = delayed->get(i);
			int32_t brightness = result.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
			int32_t contrast = result.get(V4L2_CID_CONTRAST).get<int32_t>();

			ControlList ctrlsV4L = dev_->getControls({ V4L2_CID_BRIGHTNESS, V4L2_CID_CONTRAST });
			int32_t brightnessV4L = ctrlsV4L.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
			int32_t contrastV4L = ctrlsV4L.get(V4L2_CID_CONTRAST).get<int32_t>();

			if (i > maxDelay) {
				if (brightness != value || contrast != value) {
					cerr << "Failed dual controls"
					     << " frame " << i
					     << " brightness " << brightness
					     << " contrast " << contrast
					     << " expected " << value
					     << endl;
					return TestFail;
				}
				if (brightnessV4L != value + 1 || contrastV4L != value + maxDelay) {
					cerr << "Failed dual controls"
					     << " frame " << i
					     << " brightnessV4L " << brightnessV4L
					     << " expected " << value + 1
					     << " contrastV4L " << contrastV4L
					     << " expected " << value + maxDelay
					     << endl;
					return TestFail;
				}
			}
		}

		return TestPass;
	}

	int dualControlsMultiQueue()
	{
		static const int maxDelay = 2;

		std::unordered_map<uint32_t, DelayedControls::ControlParams> delays = {
			{ V4L2_CID_BRIGHTNESS, { 1, false } },
			{ V4L2_CID_CONTRAST, { maxDelay, false } }
		};
		std::unique_ptr<DelayedControls> delayed =
			std::make_unique<DelayedControls>(dev_.get(), delays);
		ControlList ctrls;

		/* Reset control to a value that will be first two frames in test. */
		int32_t initial = 100;
		ctrls.set(V4L2_CID_BRIGHTNESS, initial);
		ctrls.set(V4L2_CID_CONTRAST, initial);
		dev_->setControls(&ctrls);
		delayed->reset();

		/*
		 * Queue all controls before applyControls(). Note we
		 * can't queue up more then the delayed controls history size
		 * which is 16.
		 */
		for (unsigned int i = 0; i < 14; i++) {
			int32_t value = 10 + i;

			ctrls.set(V4L2_CID_BRIGHTNESS, value);
			ctrls.set(V4L2_CID_CONTRAST, value);
			delayed->push(ctrls);
		}

		/* Process 2 frames less than queued, so that the V4L controls are correct */
		for (unsigned int i = 0; i < 12; i++) {
			int32_t expected = i < maxDelay ? initial : 10 + i;

			delayed->applyControls(i);

			ControlList result = delayed->get(i);

			int32_t brightness = result.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
			int32_t contrast = result.get(V4L2_CID_CONTRAST).get<int32_t>();

			ControlList ctrlsV4L = dev_->getControls({ V4L2_CID_BRIGHTNESS, V4L2_CID_CONTRAST });
			int32_t brightnessV4L = ctrlsV4L.get(V4L2_CID_BRIGHTNESS).get<int32_t>();
			int32_t contrastV4L = ctrlsV4L.get(V4L2_CID_CONTRAST).get<int32_t>();

			if (brightness != expected || contrast != expected) {
				cerr << "Failed multi queue"
				     << " frame " << i
				     << " brightness " << brightness
				     << " contrast " << contrast
				     << " expected " << expected
				     << endl;
				return TestFail;
			}

			/* Check v4l values after they've settled */
			if (i >= maxDelay && (brightnessV4L != expected + 1 || contrastV4L != expected + maxDelay)) {
				cerr << "Failed multi queue"
				     << " frame " << i
				     << " brightnessV4L " << brightnessV4L
				     << " expected " << expected + 1
				     << " contrastV4L " << contrastV4L
				     << " expected " << expected + maxDelay
				     << endl;
				return TestFail;
			}
		}

		return TestPass;
	}

	int run() override
	{
		int ret = 0;
		bool failed = false;

		/* Test single control without delay. */
		ret = singleControlNoDelay();
		if (ret)
			failed = true;

		/* Test single control with delay. */
		ret = singleControlWithDelay();
		if (ret)
			failed = true;

		/* Test single control with delay. */
		ret = singleControlWithDelayStartUp();
		if (ret)
			failed = true;

		ret = doNotLoseFirstRequest();
		if (ret)
			failed = true;

		ret = updateTooLateMustSometimesBeIgnored();
		if (ret)
			failed = true;

		ret = updateTooLateGetsDelayed();
		if (ret)
			failed = true;

		/* Test dual controls with different delays. */
		ret = dualControlsWithDelay();
		if (ret)
			failed = true;

		/* Test control values produced faster than consumed. */
		ret = dualControlsMultiQueue();
		if (ret)
			failed = true;

		if (failed)
			return TestFail;

		return TestPass;
	}

private:
	std::unique_ptr<DeviceEnumerator> enumerator_;
	std::shared_ptr<MediaDevice> media_;
	std::unique_ptr<V4L2VideoDevice> dev_;
};

TEST_REGISTER(DelayedControlsTest)
