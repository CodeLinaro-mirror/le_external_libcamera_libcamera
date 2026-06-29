/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * V4L2 compatibility camera
 */

#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include <libcamera/base/mutex.h>
#include <libcamera/base/semaphore.h>
#include <libcamera/base/shared_fd.h>

#include <libcamera/camera.h>
#include <libcamera/controls.h>
#include <libcamera/framebuffer.h>
#include <libcamera/framebuffer_allocator.h>

class V4L2Camera
{
public:
	struct CompletedBuffer {
		CompletedBuffer(unsigned int index, const libcamera::FrameMetadata &data)
			: index_(index), data_(data)
		{
		}

		unsigned int index_;
		libcamera::FrameMetadata data_;
	};

	V4L2Camera(std::shared_ptr<libcamera::Camera> camera);
	~V4L2Camera();

	int open(libcamera::StreamConfiguration *streamConfig);
	void close();
	void bind(int efd);
	void unbind();

	int configure(libcamera::StreamConfiguration *streamConfigOut,
		      const libcamera::Size &size,
		      const libcamera::PixelFormat &pixelformat,
		      unsigned int bufferCount);
	int validateConfiguration(const libcamera::PixelFormat &pixelformat,
				  const libcamera::Size &size,
				  libcamera::StreamConfiguration *streamConfigOut);

	libcamera::ControlList &controls() { return controls_; }
	const libcamera::ControlInfoMap &controlInfo() { return camera_->controls(); }

	int allocBuffers();
	void freeBuffers();
	int getBufferFd(unsigned int index);

	int streamOn();
	int streamOff();

	int qbuf(unsigned int index);

	std::optional<CompletedBuffer> nextBuffer(bool wait) LIBCAMERA_TSA_EXCLUDES(bufferMutex_);

	bool isRunning();

private:
	void requestComplete(libcamera::Request *request);

	std::shared_ptr<libcamera::Camera> camera_;
	std::unique_ptr<libcamera::CameraConfiguration> config_;

	libcamera::ControlList controls_;

	bool isRunning_;

	std::unique_ptr<libcamera::FrameBufferAllocator> bufferAllocator_;

	std::vector<std::unique_ptr<libcamera::Request>> requestPool_;
	std::vector<libcamera::Request *> freeRequests_
		LIBCAMERA_TSA_GUARDED_BY(bufferMutex_);

	std::deque<libcamera::FrameBuffer *> pendingBuffers_;
	std::deque<CompletedBuffer> completedBuffers_
		LIBCAMERA_TSA_GUARDED_BY(bufferMutex_);

	int efd_;

	libcamera::Mutex bufferMutex_;
	libcamera::ConditionVariable bufferCV_;
};
