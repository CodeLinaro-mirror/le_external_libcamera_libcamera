/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer manager interface
 */

#pragma once

#include <deque>
#include <memory>
#include <set>
#include <string>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include <libcamera/camera.h>
#include <libcamera/controls.h>
#include <libcamera/framebuffer.h>
#include <libcamera/layer.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>

namespace libcamera {

LOG_DECLARE_CATEGORY(LayerManager)

class LayerManager
{
public:
	LayerManager();
	~LayerManager();

	void bufferCompleted(Request *request, FrameBuffer *buffer);
	void requestCompleted(Request *request);
	void disconnected();

	void acquire();
	void release();

	const ControlInfoMap &controls(const ControlInfoMap &controlInfoMap);
	const ControlList &properties(const ControlList &properties);
	const std::set<Stream *> &streams(const std::set<Stream *> &streams);

	void generateConfiguration(Span<const StreamRole> &roles,
				   CameraConfiguration *config);

	void configure(CameraConfiguration *config);

	void createRequest(uint64_t cookie, Request *request);

	void queueRequest(Request *request);

	void start(const ControlList *controls);
	void stop();

private:
	/* Extend the layer with information specific to load-handling */
	struct LayerLoaded
	{
		Layer layer;
		void *dlHandle;
	};

	std::unique_ptr<LayerLoaded> createLayer(const std::string &file);
	std::deque<std::unique_ptr<LayerLoaded>> executionQueue_;

	ControlInfoMap controlInfoMap_;
	ControlList properties_;
	std::set<Stream *> streams_;
};

} /* namespace libcamera */
