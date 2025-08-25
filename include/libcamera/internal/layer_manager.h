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
#include <libcamera/control_ids.h>
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

#define _ARG_PARAMS2(type1, type2) type1 arg1, type2 arg2
#define _ARG_NAMES2(type1, type2) arg1, arg2

#define _ARG_PARAMS1(type1) type1 arg1
#define _ARG_NAMES1(type1) arg1

#define _ARG_PARAMS0()
#define _ARG_NAMES0()

#define _GET_OVERRIDE(_1, _2, _3, NAME, ...) NAME

#define ARG_PARAMS(...) _GET_OVERRIDE("ignored", __VA_ARGS__ __VA_OPT__(,) \
		    _ARG_PARAMS2, _ARG_PARAMS1, _ARG_PARAMS0)(__VA_ARGS__)

#define ARG_NAMES(...) _GET_OVERRIDE("ignored", __VA_ARGS__ __VA_OPT__(,) \
		    _ARG_NAMES2, _ARG_NAMES1, _ARG_NAMES0)(__VA_ARGS__)

#define LAYER_INSTANCE_CALL(func, ...) \
	void func(ARG_PARAMS(__VA_ARGS__)) \
	{ \
		if (layer->vtable->func) \
			layer->vtable->func(closure __VA_OPT__(,) ARG_NAMES(__VA_ARGS__)); \
	}

struct LayerInstance {
	LayerInstance(const std::shared_ptr<LayerLoaded> &l)
		: layer(l)
	{
	}

	void init(const std::string &id)
	{
		closure = layer->vtable->init(id);
	}

	void terminate()
	{
		layer->vtable->terminate(closure);
	}

	LAYER_INSTANCE_CALL(bufferCompleted, Request *, FrameBuffer *)
	LAYER_INSTANCE_CALL(requestCompleted, Request *)
	LAYER_INSTANCE_CALL(disconnected)
	LAYER_INSTANCE_CALL(acquire)
	LAYER_INSTANCE_CALL(release)
	LAYER_INSTANCE_CALL(configure, const CameraConfiguration *)
	LAYER_INSTANCE_CALL(createRequest, uint64_t, const Request *)
	LAYER_INSTANCE_CALL(queueRequest, Request *)
	LAYER_INSTANCE_CALL(start, ControlList &)
	LAYER_INSTANCE_CALL(stop)

	ControlInfoMap::Map controls(ControlInfoMap &infoMap)
	{
		if (!layer->vtable->controls)
			return ControlInfoMap::Map();
		return layer->vtable->controls(closure, infoMap);
	}

	ControlList properties(ControlList &props)
	{
		if (!layer->vtable->properties)
			return ControlList(controls::controls);
		return layer->vtable->properties(closure, props);
	}

	const std::shared_ptr<LayerLoaded> layer;
	void *closure = nullptr;
};

class LayerController
{
public:
	LayerController(const Camera *camera, const ControlList &properties,
			const ControlInfoMap &controlInfoMap,
			const std::map<std::string, std::shared_ptr<LayerLoaded>> &layers);
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

	std::deque<std::unique_ptr<LayerInstance>> executionQueue_;

	ControlInfoMap controls_;
	ControlList properties_;

	ControlList startControls_ = ControlList(controls::controls);
};

class LayerManager
{
public:
	LayerManager();
	~LayerManager() = default;

	std::unique_ptr<LayerController>
	createController(const Camera *camera,
			 const ControlList &properties,
			 const ControlInfoMap &controlInfoMap) const;

private:
	std::map<std::string, std::shared_ptr<LayerLoaded>> layers_;
};

} /* namespace libcamera */
