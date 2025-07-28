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

LOG_DEFINE_CATEGORY(LayerLoaded)
LOG_DEFINE_CATEGORY(LayerController)
LOG_DEFINE_CATEGORY(LayerManager)

/**
 * \class LayerLoaded
 * \brief A wrapper class for a Layer shared object that has been loaded
 *
 * This class wraps a Layer shared object that has been loaded, managing the
 * lifetime and management of the dlopened handle as well as organizing access
 * to the function table and layer info.
 */

/**
 * \var LayerLoaded::info
 * \brief Information about the Layer
 */

/**
 * \var LayerLoaded::vtable
 * \brief The function table of the layer
 */

/**
 * \var LayerLoaded::dlHandle
 * \brief The handle as returned by dlopen for the layer shared object
 */

/**
 * \var LayerLoaded::valid
 * \brief Whether or not the loaded layer is valid
 *
 * Instances that failed to load due to error, or that were constructed with no
 * parameters will be invalid.
 */

/**
 * \brief Load a Layer from a shared object file
 */
LayerLoaded::LayerLoaded(const std::string &filename)
{
	File file{ filename };
	if (!file.open(File::OpenModeFlag::ReadOnly)) {
		LOG(LayerLoaded, Error) << "Failed to open layer: "
					<< strerror(-file.error());
		return;
	}

	Span<const uint8_t> data = file.map();
	int ret = utils::elfVerifyIdent(data);
	if (ret) {
		LOG(LayerLoaded, Error) << "Layer is not an ELF file";
		return;
	}

	Span<const uint8_t> layerInfoSym = utils::elfLoadSymbol(data, "layerInfo");
	if (layerInfoSym.size() < sizeof(LayerInfo)) {
		LOG(LayerLoaded, Error) << "Layer has no valid layerInfoSym";
		return;
	}

	void *dlh = dlopen(file.fileName().c_str(), RTLD_LAZY);
	if (!dlh) {
		LOG(LayerLoaded, Error)
			<< "Failed to open layer shared object: "
			<< dlerror();
		return;
	}

	/* No need to dlclose as the deconstructor will handle it */

	void *layerInfoDl = dlsym(dlh, "layerInfo");
	if (!layerInfoDl) {
		LOG(LayerLoaded, Error)
			<< "Failed to load layerInfo from layer shared object: "
			<< dlerror();
		return;
	}

	void *vtableSym = dlsym(dlh, "layerInterface");
	if (!vtableSym) {
		LOG(LayerLoaded, Error)
			<< "Failed to load layerInterface from layer shared object: "
			<< dlerror();
		return;
	}

	info = static_cast<LayerInfo *>(layerInfoDl);
	vtable = static_cast<LayerInterface *>(vtableSym);
	dlHandle = dlh;

	/* \todo Implement this. It should come from the libcamera version */
	if (info->layerAPIVersion != 1) {
		LOG(LayerLoaded, Error) << "Layer '" << info->name
					<< "' API version mismatch";
		return;
	}

	/* \todo Document these requirements */
	if (!vtable->init) {
		LOG(LayerLoaded, Error) << "Layer '" << info->name
					<< "' doesn't implement init";
		return;
	}

	/* \todo Document these requirements */
	if (!vtable->terminate) {
		LOG(LayerLoaded, Error) << "Layer '" << info->name
					<< "' doesn't implement terminate";
		return;
	}

	/* \todo Validate the layer name. */

	valid = true;

	return;
}

/**
 * \fn LayerLoaded::LayerLoaded(LayerLoaded &&other)
 * \brief Move constructor
 */

/**
 * \fn LayerLoaded &LayerLoaded::operator=(LayerLoaded &&other)
 * \brief Move assignment operator
 */

/**
 * \class LayerController
 * \brief Per-Camera instance of a layer manager
 *
 * Conceptually this class is an instantiation of the LayerManager for each
 * Camera instance. It contains the closure for of each layer specific to the
 * Camera, as well as the queue of layers to execute for each Camera.
 */

/**
 * \brief Initialize the Layers
 * \param[in] camera The Camera for whom to initialize layers
 * \param[in] properties The Camera properties
 * \param[in] controlInfoMap The Camera controls
 * \param[in] layers Map of available layers
 *
 * This is called by the Camera at construction time via
 * LayerManager::createController. The LayerManager feeds the list of layers
 * that are available, and the LayerController can then create its own
 * execution queue and initialize all the layers for its Camera.
 *
 * \a properties and \a controlInfoMap are passed in so that the Layers can
 * modify them, although they will be cached in an internal copy that can be
 * efficiently returned at properties() and controls(), respectively.
 */
LayerController::LayerController(const Camera *camera,
				 const ControlList &properties,
				 const ControlInfoMap &controlInfoMap,
				 std::map<std::string, std::shared_ptr<LayerLoaded>> &layers)
{
	/* Order the layers */
	/* \todo Document this. First is closer to application, last is closer to libcamera */
	/* \todo Get this from configuration file */
	const char *layerList = utils::secure_getenv("LIBCAMERA_LAYERS_ENABLE");
	if (layerList) {
		for (const auto &layerName : utils::split(layerList, ",")) {
			if (layerName.empty())
				continue;

			const auto &it = layers.find(layerName);
			if (it == layers.end()) {
				LOG(LayerController, Warning)
					<< "Requested layer '" << layerName
					<< "' not found";
				continue;
			}

			executionQueue_.push_back(it->second);
		}
	}

	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		void *closure = layer->vtable->init(camera->id());
		closures_[layer.get()] = closure;
	}

	/*
	 * We need to iterate over the layers individually to merge all of
	 * their controls, so we'll factor out updateControls() as it needs to be
	 * run again at configure().
	 */
	updateProperties(properties);
	updateControls(controlInfoMap);
}

/**
 * \brief Terminate the Layers
 *
 * This is called by the Camera at deconstruction time. The LayerController
 * instructs all Layer instances to release the resources that they allocated
 * for this specific \a camera.
 */
LayerController::~LayerController()
{
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		void *closure = closures_.at(layer.get());
		layer->vtable->terminate(closure);
	}
}

/**
 * \brief Hook for Camera::bufferCompleted
 * \param[in] request The request whose buffer completed
 * \param[in] buffer The buffer that completed
 */
void LayerController::bufferCompleted(Request *request, FrameBuffer *buffer)
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it)->vtable->bufferCompleted) {
			void *closure = closures_.at((*it).get());
			(*it)->vtable->bufferCompleted(closure, request, buffer);
		}
	}
}

/**
 * \brief Hook for Camera::requestCompleted
 * \param[in] request The request that completed
 */
void LayerController::requestCompleted(Request *request)
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it)->vtable->requestCompleted) {
			void *closure = closures_.at((*it).get());
			(*it)->vtable->requestCompleted(closure, request);
		}
	}
}

/**
 * \brief Hook for Camera::disconnected
 */
void LayerController::disconnected()
{
	/* Reverse order because this comes from a Signal emission */
	for (auto it = executionQueue_.rbegin();
	     it != executionQueue_.rend(); it++) {
		if ((*it)->vtable->disconnected) {
			void *closure = closures_.at((*it).get());
			(*it)->vtable->disconnected(closure);
		}
	}
}

/**
 * \brief Hook for Camera::acquire
 */
void LayerController::acquire()
{
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->acquire) {
			void *closure = closures_.at(layer.get());
			layer->vtable->acquire(closure);
		}
	}
}

/**
 * \brief Hook for Camera::release
 */
void LayerController::release()
{
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->release) {
			void *closure = closures_.at(layer.get());
			layer->vtable->release(closure);
		}
	}
}

/**
 * \fn LayerController::controls
 * \brief Hook for Camera::controls
 * \return A ControlInfoMap that merges the Camera's controls() with the ones
 * declared by the layers
 */

/**
 * \fn LayerController::properties
 * \brief Hook for Camera::properties
 * \return A properties list that merges the Camera's properties() with the
 * ones declared by the layers
 */

void LayerController::updateProperties(const ControlList &properties)
{
	ControlList props = properties;
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->properties) {
			void *closure = closures_.at(layer.get());
			ControlList ret = layer->vtable->properties(closure, props);
			props.merge(ret, ControlList::MergePolicy::OverwriteExisting);
		}
	}
	properties_ = props;
}

void LayerController::updateControls(const ControlInfoMap &controlInfoMap)
{
	ControlInfoMap infoMap = controlInfoMap;
	/* \todo Simplify this once ControlInfoMaps become easier to modify */
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->controls) {
			void *closure = closures_.at(layer.get());
			ControlInfoMap::Map ret = layer->vtable->controls(closure, infoMap);
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
	controls_ = infoMap;
}

/**
 * \brief Hook for Camera::configure
 * \param[in] config The configuration
 * \param[in] controlInfoMap The ControlInfoMap of the controls that \a camera supports
 *
 * \a controlInfoMap is passed in as this is a potential point where the limits
 * of controls could change, so this gives a chance for the Layers to update
 * the ControlInfoMap that will be returned by LayerController::controls().
 */
void LayerController::configure(const CameraConfiguration *config,
				const ControlInfoMap &controlInfoMap)
{
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->configure) {
			void *closure = closures_.at(layer.get());
			layer->vtable->configure(closure, config);
		}
	}

	updateControls(controlInfoMap);
}

/**
 * \brief Hook for Camera::createRequest
 * \param[in] cookie An opaque cookie for the application
 * \param[in] request The request that was created
 */
void LayerController::createRequest(uint64_t cookie, const Request *request)
{
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->createRequest) {
			void *closure = closures_.at(layer.get());
			layer->vtable->createRequest(closure, cookie, request);
		}
	}
}

/**
 * \brief Hook for Camera::queueRequest
 * \param[in] request The request that is being queued
 */
void LayerController::queueRequest(Request *request)
{
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->queueRequest) {
			void *closure = closures_.at(layer.get());
			layer->vtable->queueRequest(closure, request);
		}
	}
}

/**
 * \brief Hook for Camera::start
 * \param[in] controls The controls to be applied before starting the capture
 * \return A ControlList that merges controls set by the layers and \a controls
 */
ControlList *LayerController::start(const ControlList *controls)
{
	if (controls) {
		startControls_ = std::make_unique<ControlList>(*controls->idMap());
		startControls_->merge(*controls);
	} else {
		startControls_ = std::make_unique<ControlList>(controls::controls);
	}

	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->start) {
			void *closure = closures_.at(layer.get());
			layer->vtable->start(closure, *startControls_);
		}
	}

	return startControls_.get();
}

/**
 * \brief Hook for Camera::stop
 */
void LayerController::stop()
{
	for (std::shared_ptr<LayerLoaded> &layer : executionQueue_) {
		if (layer->vtable->stop) {
			void *closure = closures_.at(layer.get());
			layer->vtable->stop(closure);
		}
	}
}

/**
 * \class LayerManager
 * \brief Layer manager
 *
 * The Layer manager discovers layer implementations from disk, and creates
 * execution queues for every function that is implemented by each layer and
 * executes them. A layer is a layer that sits between libcamera and the
 * application, and hooks into the public Camera interface.
 *
 * The LayerManager itself is instantiated by the CameraManager, and each
 * Camera interacts with the LayerManager by passing itself it. The
 * LayerManager internally maps each Camera to a list of Layer instances that
 * it calls sequentially for each hook.
 */

/**
 * \brief Construct a LayerManager instance
 *
 * The LayerManager class is meant be instantiated by the CameraManager.
 *
 * This function simply loads all available layers and stores them. The
 * LayerController is responsible for organizing them into queues to be
 * executed and for managing closures, for each Camera that they belong to.
 */
LayerManager::LayerManager()
{
	/* This is so that we can capture it in the lambda below */
	std::map<std::string, std::shared_ptr<LayerLoaded>> &layers = layers_;

	/* \todo Implement built-in layers */

	/* This returns the number of "modules" successfully loaded */
	std::function<int(const std::string &)> soHandler =
	[this, &layers](const std::string &file) {
		std::shared_ptr<LayerLoaded> layer = std::make_unique<LayerLoaded>(file);
		if (!layer->valid)
			return 0;

		LOG(LayerManager, Debug) << "Loaded layer '" << file << "'";

		layers.emplace(std::string(layer->info->name), layer);

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
			utils::findSharedObjects(dir.c_str(), 1, soHandler);
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

		utils::findSharedObjects(layerBuildPath.c_str(), maxDepth, soHandler);
	}

	/* Finally try to load layers from the installed system path. */
	utils::findSharedObjects(LAYER_DIR, 1, soHandler);
}

/**
 * \brief Create a LayerController instance
 * \param[in] camera The Camera instance for whom to create a LayerController
 * \param[in] properties The Camera properties
 * \param[in] controlInfoMap The Camera controls
 */
std::unique_ptr<LayerController>
LayerManager::createController(const Camera *camera,
			       const ControlList &properties,
			       const ControlInfoMap &controlInfoMap)
{
	return std::make_unique<LayerController>(camera, properties, controlInfoMap, layers_);
}

} /* namespace libcamera */
