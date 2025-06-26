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
	std::map<std::string, std::unique_ptr<LayerLoaded>> layers;

	/* This returns the number of "modules" successfully loaded */
	std::function<int(const std::string &)> addDirHandler =
	[this, &layers](const std::string &file) {
		std::unique_ptr<LayerManager::LayerLoaded> layer = createLayer(file);
		if (!layer)
			return 0;

		LOG(LayerManager, Debug) << "Loaded layer '" << file << "'";

		layers.insert({std::string(layer->layer.name), std::move(layer)});

		return 1;
	};

	/* User-specified paths take precedence. */
	const char *layerPaths = utils::secure_getenv("LIBCAMERA_LAYER_PATH");
	if (layerPaths) {
		for (const auto &dir : utils::split(layerPaths, ":")) {
			if (dir.empty())
				continue;

			utils::addDir(dir.c_str(), 0, addDirHandler);
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
	utils::addDir(LAYER_DIR, 0, addDirHandler);

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

LayerManager::~LayerManager()
{
	for (auto &layer : executionQueue_)
		dlclose(layer->dlHandle);
}

std::unique_ptr<LayerManager::LayerLoaded> LayerManager::createLayer(const std::string &filename)
{
	File file{ filename };
	if (!file.open(File::OpenModeFlag::ReadOnly)) {
		LOG(LayerManager, Error) << "Failed to open layer: "
					 << strerror(-file.error());
		return nullptr;
	}

	Span<const uint8_t> data = file.map();
	int ret = utils::elfVerifyIdent(data);
	if (ret) {
		LOG(LayerManager, Error) << "Layer is not an ELF file";
		return nullptr;
	}

	Span<const uint8_t> info = utils::elfLoadSymbol(data, "layerInfo");
	if (info.size() < sizeof(Layer)) {
		LOG(LayerManager, Error) << "Layer has no valid info";
		return nullptr;
	}

	void *dlHandle = dlopen(file.fileName().c_str(), RTLD_LAZY);
	if (!dlHandle) {
		LOG(LayerManager, Error)
			<< "Failed to open layer shared object: "
			<< dlerror();
		return nullptr;
	}

	void *symbol = dlsym(dlHandle, "layerInfo");
	if (!symbol) {
		LOG(LayerManager, Error)
			<< "Failed to load layerInfo from layer shared object: "
			<< dlerror();
		dlclose(dlHandle);
		dlHandle = nullptr;
		return nullptr;
	}

	std::unique_ptr<LayerManager::LayerLoaded> layer =
		std::make_unique<LayerManager::LayerLoaded>();
	layer->layer = *reinterpret_cast<Layer *>(symbol);

	/* \todo Implement this. It should come from the libcamera version */
	if (layer->layer.layerAPIVersion != 1) {
		LOG(LayerManager, Error) << "Layer API version mismatch";
		return nullptr;
	}

	/* \todo Validate the layer name. */

	layer->dlHandle = dlHandle;

	return layer;
}

void LayerManager::bufferCompleted(Request *request, FrameBuffer *buffer)
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it)->layer.bufferCompleted)
			(*it)->layer.bufferCompleted(request, buffer);
	}
}

void LayerManager::requestCompleted(Request *request)
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it)->layer.requestCompleted)
			(*it)->layer.requestCompleted(request);
	}
}

void LayerManager::disconnected()
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it)->layer.disconnected)
			(*it)->layer.disconnected();
	}
}

void LayerManager::acquire()
{
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_)
		if (layer->layer.acquire)
			layer->layer.acquire();
}

void LayerManager::release()
{
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_)
		if (layer->layer.release)
			layer->layer.release();
}

const ControlInfoMap &LayerManager::controls(const ControlInfoMap &controlInfoMap)
{
	controlInfoMap_ = controlInfoMap;

	/* \todo Simplify this once ControlInfoMaps become easier to modify */
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_) {
		if (layer->layer.controls) {
			ControlInfoMap::Map ret = layer->layer.controls(controlInfoMap_);
			ControlInfoMap::Map map;
			/* Merge the layer's ret later so that layers can overwrite */
			for (auto &pair : controlInfoMap_)
				map.insert(pair);
			for (auto &pair : ret)
				map.insert(pair);
			controlInfoMap_ = ControlInfoMap(std::move(map),
							 libcamera::controls::controls);
		}
	}
	return controlInfoMap_;
}

const ControlList &LayerManager::properties(const ControlList &properties)
{
	properties_ = properties;
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_) {
		if (layer->layer.properties) {
			ControlList ret = layer->layer.properties(properties_);
			properties_.merge(ret, ControlList::MergePolicy::OverwriteExisting);
		}
	}
	return properties_;
}

const std::set<Stream *> &LayerManager::streams(const std::set<Stream *> &streams)
{
	streams_ = streams;
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_) {
		if (layer->layer.streams) {
			std::set<Stream *> ret = layer->layer.streams(streams_);
			streams_.insert(ret.begin(), ret.end());
		}
	}
	return streams_;
}

void LayerManager::generateConfiguration(Span<const StreamRole> &roles,
					 CameraConfiguration *config)
{
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_)
		if (layer->layer.generateConfiguration)
			layer->layer.generateConfiguration(roles, config);
}

void LayerManager::configure(CameraConfiguration *config)
{
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_)
		if (layer->layer.configure)
			layer->layer.configure(config);
}

void LayerManager::createRequest(uint64_t cookie, Request *request)
{
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_)
		if (layer->layer.createRequest)
			layer->layer.createRequest(cookie, request);
}

void LayerManager::queueRequest(Request *request)
{
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_)
		if (layer->layer.queueRequest)
			layer->layer.queueRequest(request);
}

void LayerManager::start(const ControlList *controls)
{
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_)
		if (layer->layer.start)
			layer->layer.start(controls);
}

void LayerManager::stop()
{
	for (std::unique_ptr<LayerManager::LayerLoaded> &layer : executionQueue_)
		if (layer->layer.stop)
			layer->layer.stop();
}

} /* namespace libcamera */
