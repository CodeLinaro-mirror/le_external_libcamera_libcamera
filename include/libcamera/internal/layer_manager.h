/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer manager interface
 */

#pragma once

#include <deque>
#include <dlfcn.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include <libcamera/camera.h>
#include <libcamera/controls.h>
#include <libcamera/framebuffer.h>
#include <libcamera/layer.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>

namespace libcamera {

LOG_DECLARE_CATEGORY(LayerLoaded)
LOG_DECLARE_CATEGORY(LayerController)
LOG_DECLARE_CATEGORY(LayerManager)

/* Extend the layer with information specific to load-handling */
struct LayerLoaded {
	LayerLoaded() = default;

	LayerLoaded(const std::string &file);

	LayerLoaded(LayerLoaded &&other)
		: info(other.info), vtable(other.vtable),
		  dlHandle(other.dlHandle), valid(other.valid)
	{
		other.dlHandle = nullptr;
	}

	LayerLoaded &operator=(LayerLoaded &&other)
	{
		info = other.info;
		vtable = other.vtable;
		dlHandle = other.dlHandle;
		other.dlHandle = nullptr;
		valid = other.valid;
		return *this;
	}

	~LayerLoaded()
	{
		if (dlHandle)
			dlclose(dlHandle);
	}

	LayerInfo *info = nullptr;
	LayerInterface *vtable = nullptr;
	void *dlHandle = nullptr;
	bool valid = false;

private:
	LIBCAMERA_DISABLE_COPY(LayerLoaded)
};

class LayerController
{
public:
	LayerController(const Camera *camera, const ControlList &properties,
			const ControlInfoMap &controlInfoMap,
			std::map<std::string, std::shared_ptr<LayerLoaded>> &layers);
	~LayerController();

	void bufferCompleted(Request *request, FrameBuffer *buffer);
	void requestCompleted(Request *request);
	void disconnected();

	void acquire();
	void release();

	const ControlInfoMap &controls() const { return controls_; }
	const ControlList &properties() const { return properties_; }

	void configure(const CameraConfiguration *config,
		       const ControlInfoMap &controlInfoMap);

	void createRequest(uint64_t cookie, const Request *request);

	void queueRequest(Request *request);

	ControlList *start(const ControlList *controls);
	void stop();

private:
	void updateProperties(const ControlList &properties);
	void updateControls(const ControlInfoMap &controlInfoMap);

	std::deque<std::shared_ptr<LayerLoaded>> executionQueue_;
	std::map<LayerLoaded *, void *> closures_;

	ControlInfoMap controls_;
	ControlList properties_;

	std::unique_ptr<ControlList> startControls_;
};

class LayerManager
{
public:
	LayerManager();
	~LayerManager() = default;

	std::unique_ptr<LayerController>
	createController(const Camera *camera,
			 const ControlList &properties,
			 const ControlInfoMap &controlInfoMap);

private:
	std::map<std::string, std::shared_ptr<LayerLoaded>> layers_;
};

} /* namespace libcamera */
