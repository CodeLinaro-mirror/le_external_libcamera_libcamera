/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021, Ideas on Board Oy
 *
 * Base Frame Sink Class
 */

#pragma once

#include <vector>

#include <libcamera/base/signal.h>

#include <libcamera/camera.h>
#include <libcamera/stream.h>

namespace libcamera {
class CameraConfiguration;
class FrameBuffer;
class Request;
} /* namespace libcamera */

class FrameSink
{
public:
	virtual ~FrameSink();

	virtual int configure(const libcamera::CameraConfiguration &config);

	virtual void mapBuffer(libcamera::FrameBuffer *buffer);

	virtual int start();
	virtual int stop();

	void addStream(libcamera::Stream *const stream)
	{
		streams_.push_back(stream);
	}

	bool assignedStream(const libcamera::Stream *const stream)
	{
		return std::find(streams_.begin(), streams_.end(), stream) != streams_.end();
	}

	bool empty()
	{
		return streams_.empty();
	}

	virtual bool processRequest(libcamera::Request *request) = 0;
	libcamera::Signal<libcamera::Request *> requestProcessed;

protected:
	const libcamera::StreamConfiguration &findConfiguration(
		const libcamera::CameraConfiguration &config);

	std::vector<libcamera::Stream *> streams_;
};
