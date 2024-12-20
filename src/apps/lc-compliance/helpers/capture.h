/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020-2021, Google Inc.
 *
 * Simple capture helper
 */

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

#include <libcamera/libcamera.h>

class Capture
{
public:
	void configure(libcamera::StreamRole role);

protected:
	Capture(std::shared_ptr<libcamera::Camera> camera);
	virtual ~Capture();

	void start();
	void stop();

	void prepareRequests(unsigned int plannedRequests);

	virtual void requestComplete(libcamera::Request *request) = 0;

	std::shared_ptr<libcamera::Camera> camera_;
	libcamera::FrameBufferAllocator allocator_;
	std::unique_ptr<libcamera::CameraConfiguration> config_;
	std::vector<std::unique_ptr<libcamera::Request>> requests_;

	struct
	{
	private:
		std::mutex mutex_;
		std::condition_variable cv_;
		std::optional<int> value_;

	public:
		int wait()
		{
			std::unique_lock guard(mutex_);

			cv_.wait(guard, [&] {
				return value_.has_value();
			});

			return *value_;
		}

		void set(int value)
		{
			std::unique_lock guard(mutex_);

			if (!value_)
				value_ = value;

			cv_.notify_all();
		}

		void reset()
		{
			value_.reset();
		}
	} result_;
};

class CaptureBalanced : public Capture
{
public:
	CaptureBalanced(std::shared_ptr<libcamera::Camera> camera);

	void capture(unsigned int numRequests);

private:
	int queueRequest(libcamera::Request *request);
	void requestComplete(libcamera::Request *request) override;

	unsigned int queueCount_;
	unsigned int captureCount_;
	unsigned int captureLimit_;
};

class CaptureUnbalanced : public Capture
{
public:
	CaptureUnbalanced(std::shared_ptr<libcamera::Camera> camera);

	void capture(unsigned int numRequests);

private:
	void requestComplete(libcamera::Request *request) override;

	unsigned int captureCount_;
	unsigned int captureLimit_;
};
