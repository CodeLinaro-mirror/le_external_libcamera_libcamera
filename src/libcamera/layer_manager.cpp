/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer manager
 */

#include "libcamera/internal/layer_manager.h"

#include <algorithm>
#include <dirent.h>
#include <dlfcn.h>
#include <map>
#include <memory>
#include <set>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <tuple>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>
#include <libcamera/base/span.h>

#include <libcamera/control_ids.h>
#include <libcamera/layer.h>

#include "libcamera/internal/utils.h"

/**
 * \file layer_manager.h
 * \brief Layer manager
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(LayerManager)

/**
 * \class LayerManager
 * \brief Layer manager
 *
 * The Layer manager discovers layer implementations from disk, and creates
 * execution queues for every function that is implemented by each layer and
 * executes them. A layer is a layer that sits between libcamera and the
 * application, and hooks into the public Camera interface.
 */

/**
 * \brief Construct a LayerManager instance
 *
 * The LayerManager class is meant be instantiated by the Camera.
 */
LayerManager::LayerManager()
{
	std::map<std::string, LayerLoaded> layers;

	/* \todo Implement built-in layers */

	/* This returns the number of "modules" successfully loaded */
	std::function<int(const std::string &)> addDirHandler =
	[this, &layers](const std::string &file) {
		LayerManager::LayerLoaded layer = createLayer(file);
		if (!layer.info)
			return 0;

		LOG(LayerManager, Debug) << "Loaded layer '" << file << "'";

		layers.emplace(std::string(layer.info->name), std::move(layer));

		return 1;
	};

	/* User-specified paths take precedence. */
	/* \todo Document this */
	const char *layerPaths = utils::secure_getenv("LIBCAMERA_LAYER_PATH");
	if (layerPaths) {
		for (const auto &dir : utils::split(layerPaths, ":")) {
			if (dir.empty())
				continue;

			/*
			 * \todo Move the shared objects into one directory
			 * instead of each in their own subdir
			 */
			utils::addDir(dir.c_str(), 1, addDirHandler);
		}
	}

	/*
	 * When libcamera is used before it is installed, load layers from the
	 * same build directory as the libcamera library itself.
	 */
	std::string root = utils::libcameraBuildPath();
	if (!root.empty()) {
		std::string layerBuildPath = root + "src/layer";
		constexpr int maxDepth = 2;

		LOG(LayerManager, Info)
			<< "libcamera is not installed. Adding '"
			<< layerBuildPath << "' to the layer search path";

		utils::addDir(layerBuildPath.c_str(), maxDepth, addDirHandler);
	}

	/* Finally try to load layers from the installed system path. */
	utils::addDir(LAYER_DIR, 1, addDirHandler);

	/* Order the layers */
	/* \todo Document this. First is closer to application, last is closer to libcamera */
	const char *layerList = utils::secure_getenv("LIBCAMERA_LAYERS_ENABLE");
	if (layerList) {
		for (const auto &layerName : utils::split(layerList, ":")) {
			if (layerName.empty())
				continue;

			const auto &it = layers.find(layerName);
			if (it == layers.end())
				continue;

			executionQueue_.push_back(std::move(it->second));
		}
	}
}

void LayerManager::init(const Camera *camera, const ControlList &properties,
			const ControlInfoMap &controlInfoMap)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		void *closure = layer.vtable->init(camera->id());
		closures_[std::make_tuple(camera, &layer)] = closure;
	}

	/*
	 * We need to iterate over the layers individually to merge all of
	 * their controls, so we'll factor out updateControls() as it needs to be
	 * run again at configure().
	 */
	updateProperties(camera, properties);
	updateControls(camera, controlInfoMap);
}

void LayerManager::terminate(const Camera *camera)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		void *closure = closures_.at(std::make_tuple(camera, &layer));
		layer.vtable->terminate(closure);
	}
}

LayerManager::LayerLoaded LayerManager::createLayer(const std::string &filename)
{
	LayerLoaded layer;

	File file{ filename };
	if (!file.open(File::OpenModeFlag::ReadOnly)) {
		LOG(LayerManager, Error) << "Failed to open layer: "
					 << strerror(-file.error());
		return layer;
	}

	Span<const uint8_t> data = file.map();
	int ret = utils::elfVerifyIdent(data);
	if (ret) {
		LOG(LayerManager, Error) << "Layer is not an ELF file";
		return layer;
	}

	Span<const uint8_t> info = utils::elfLoadSymbol(data, "layerInfo");
	if (info.size() < sizeof(LayerInfo)) {
		LOG(LayerManager, Error) << "Layer has no valid info";
		return layer;
	}

	void *dlHandle = dlopen(file.fileName().c_str(), RTLD_LAZY);
	if (!dlHandle) {
		LOG(LayerManager, Error)
			<< "Failed to open layer shared object: "
			<< dlerror();
		return layer;
	}

	void *layerInfo = dlsym(dlHandle, "layerInfo");
	if (!layerInfo) {
		LOG(LayerManager, Error)
			<< "Failed to load layerInfo from layer shared object: "
			<< dlerror();
		dlclose(dlHandle);
		return layer;
	}

	void *vtable = dlsym(dlHandle, "layerInterface");
	if (!vtable) {
		LOG(LayerManager, Error)
			<< "Failed to load layerInterface from layer shared object: "
			<< dlerror();
		dlclose(dlHandle);
		return layer;
	}

	layer.info = static_cast<LayerInfo *>(layerInfo);
	layer.vtable = static_cast<LayerInterface *>(vtable);
	layer.dlHandle = dlHandle;

	/*
	 * No need to dlclose after this as the LayerLoaded deconstructor will
	 * handle it
	 */

	/* \todo Implement this. It should come from the libcamera version */
	if (layer.info->layerAPIVersion != 1) {
		LOG(LayerManager, Error) << "Layer API version mismatch";
		layer.info = nullptr;
		return layer;
	}

	/* \todo Document these requirements */
	if (!layer.vtable->init) {
		LOG(LayerManager, Error) << "Layer doesn't implement init";
		layer.info = nullptr;
		return layer;
	}

	/* \todo Document these requirements */
	if (!layer.vtable->terminate) {
		LOG(LayerManager, Error) << "Layer doesn't implement terminate";
		layer.info = nullptr;
		return layer;
	}

	/* \todo Validate the layer name. */

	return layer;
}

void LayerManager::bufferCompleted(const Camera *camera, Request *request, FrameBuffer *buffer)
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it).vtable->bufferCompleted) {
			void *closure = closures_.at(std::make_tuple(camera, &(*it)));
			(*it).vtable->bufferCompleted(closure, request, buffer);
		}
	}
}

void LayerManager::requestCompleted(const Camera *camera, Request *request)
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it).vtable->requestCompleted) {
			void *closure = closures_.at(std::make_tuple(camera, &(*it)));
			(*it).vtable->requestCompleted(closure, request);
		}
	}
}

void LayerManager::disconnected(const Camera *camera)
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it).vtable->disconnected) {
			void *closure = closures_.at(std::make_tuple(camera, &(*it)));
			(*it).vtable->disconnected(closure);
		}
	}
}

void LayerManager::acquire(const Camera *camera)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->acquire) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			layer.vtable->acquire(closure);
		}
	}
}

void LayerManager::release(const Camera *camera)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->release) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			layer.vtable->release(closure);
		}
	}
}

void LayerManager::updateProperties(const Camera *camera,
				    const ControlList &properties)
{
	ControlList props = properties;
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->properties) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			ControlList ret = layer.vtable->properties(closure, props);
			props.merge(ret, ControlList::MergePolicy::OverwriteExisting);
		}
	}
	properties_[camera] = props;
}

void LayerManager::updateControls(const Camera *camera,
				  const ControlInfoMap &controlInfoMap)
{
	ControlInfoMap infoMap = controlInfoMap;
	/* \todo Simplify this once ControlInfoMaps become easier to modify */
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->controls) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			ControlInfoMap::Map ret = layer.vtable->controls(closure, infoMap);
			ControlInfoMap::Map map;
			/* Merge the layer's ret later so that layers can overwrite */
			for (auto &pair : infoMap)
				map.insert(pair);
			for (auto &pair : ret)
				map.insert(pair);
			infoMap = ControlInfoMap(std::move(map),
						 libcamera::controls::controls);
		}
	}
	controls_[camera] = infoMap;
}

void LayerManager::configure(const Camera *camera,
			     const CameraConfiguration *config,
			     const ControlInfoMap &controlInfoMap)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->configure) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			layer.vtable->configure(closure, config);
		}
	}

	updateControls(camera, controlInfoMap);
}

void LayerManager::createRequest(const Camera *camera, uint64_t cookie, const Request *request)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->createRequest) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			layer.vtable->createRequest(closure, cookie, request);
		}
	}
}

void LayerManager::queueRequest(const Camera *camera, Request *request)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->queueRequest) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			layer.vtable->queueRequest(closure, request);
		}
	}
}

void LayerManager::start(const Camera *camera, const ControlList *controls)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->start) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			layer.vtable->start(closure, controls);
		}
	}
}

void LayerManager::stop(const Camera *camera)
{
	for (LayerManager::LayerLoaded &layer : executionQueue_) {
		if (layer.vtable->stop) {
			void *closure = closures_.at(std::make_tuple(camera, &layer));
			layer.vtable->stop(closure);
		}
	}
}

} /* namespace libcamera */
