/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2018, Google Inc.
 *
 * Camera management
 */

#include "libcamera/internal/camera_manager.h"

#include <algorithm>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/camera.h>
#include <libcamera/camera_descriptor.h>
#include <libcamera/property_ids.h>

#include "libcamera/internal/camera.h"
#include "libcamera/internal/camera_descriptor.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/global_configuration.h"
#include "libcamera/internal/ipa_manager.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/pipeline_handler.h"

/**
 * \file libcamera/camera_manager.h
 * \brief The camera manager
 */

/**
 * \internal
 * \file libcamera/internal/camera_manager.h
 * \brief Internal camera manager support
 */

/**
 * \brief Top-level libcamera namespace
 */
namespace libcamera {

LOG_DEFINE_CATEGORY(Camera)

#ifndef __DOXYGEN_PUBLIC__
CameraManager::Private::Private()
	: Thread("CameraManager"), initialized_(false)
{
	/*
	 * Bind this Object to its own thread, so that work can be marshalled
	 * onto the camera manager thread with invokeMethod().
	 */
	moveToThread(this);
}

int CameraManager::Private::start()
{
	int ret = startThread();
	if (ret)
		return ret;

	/*
	 * Create every camera in the system by enumerating followed by
	 * initialisation. Cameras already initialised from a descriptor are
	 * left untouched.
	 */
	invokeMethod(&Private::createCameras, ConnectionTypeBlocking);

	return 0;
}

/*
 * Start the camera manager thread if not started yet, and wait for its
 * initialization to complete. Returns the initialization status.
 */
int CameraManager::Private::startThread()
{
	bool start = false;

	{
		MutexLocker locker(mutex_);

		if (!started_) {
			started_ = true;
			initialized_ = false;
			start = true;
		}
	}

	if (start)
		Thread::start();

	/*
	 * Wait for initialization to complete, whether this call started the
	 * thread or another one did, as the caller may otherwise proceed before
	 * the thread is ready.
	 */
	int status;

	{
		MutexLocker locker(mutex_);
		cv_.wait(locker, [&]() LIBCAMERA_TSA_REQUIRES(mutex_) {
			return initialized_;
		});
		status = status_;
	}

	/* If a failure happened during initialization, stop the thread. */
	if (status < 0) {
		exit();
		wait();
		return status;
	}

	return 0;
}

/*
 * Enumerate the cameras in the system without initialising them, starting the
 * camera manager thread on first use. Called from the application thread.
 */
std::vector<std::shared_ptr<CameraDescriptor>> CameraManager::Private::enumerate()
{
	int ret = startThread();
	if (ret)
		return {};

	return invokeMethod(&Private::surveyThread, ConnectionTypeBlocking);
}

/*
 * Initialise the camera described by \a descriptor. Called from the
 * application thread.
 */
std::shared_ptr<Camera>
CameraManager::Private::initialize(const std::shared_ptr<CameraDescriptor> &descriptor)
{
	if (!descriptor)
		return nullptr;

	{
		MutexLocker locker(mutex_);
		if (!started_ || !initialized_ || status_ < 0)
			return nullptr;
	}

	return invokeMethod(&Private::initializeThread, ConnectionTypeBlocking,
			    descriptor);
}

void CameraManager::Private::run()
{
	LOG(Camera, Debug) << "Starting camera manager";

	int ret = init();

	mutex_.lock();
	status_ = ret;
	initialized_ = true;
	mutex_.unlock();
	cv_.notify_all();

	if (ret < 0) {
		cleanup();
		return;
	}

	/* Now start processing events and messages. */
	exec();

	cleanup();
}

/*
 * Retrieve the IPA manager, constructing it on first use. IPA modules are
 * only needed once a camera is initialised, so this defers the module scan
 * until that point.
 */
IPAManager *CameraManager::Private::ipaManager()
{
	ASSERT(Thread::current() == this);

	if (!ipaManager_) {
		CameraManager *const o = LIBCAMERA_O_PTR();
		ipaManager_ = std::make_unique<IPAManager>(*o);
	}

	return ipaManager_.get();
}

int CameraManager::Private::init()
{
	enumerator_ = DeviceEnumerator::create();
	if (!enumerator_ || enumerator_->enumerate())
		return -ENODEV;

	enumerator_->devicesAdded.connect(this, &Private::createCameras);

	return 0;
}

/*
 * Retrieve the pipeline handler factories to consider, in match order. When a
 * list of preferred pipelines is defined in the configuration, the ordered list
 * is exclusive. Otherwise all factories are returned in registration order.
 */
std::vector<const PipelineHandlerFactoryBase *> CameraManager::Private::pipelineFactories() const
{
	std::vector<const PipelineHandlerFactoryBase *> selected;

	/*
	 * \todo Try to read handlers and order from configuration
	 * file and only fallback on environment variable or all handlers, if
	 * there is no configuration file.
	 */
	const auto pipesList =
		configuration().listOption({ "pipelines_match_list" });

	/* The configured list is exclusive, skip all other factories. */
	if (pipesList.has_value()) {
		for (const auto &pipeName : pipesList.value()) {
			const PipelineHandlerFactoryBase *factory;
			factory = PipelineHandlerFactoryBase::getFactoryByName(pipeName);
			if (!factory)
				continue;

			LOG(Camera, Debug)
				<< "Found listed pipeline handler '"
				<< pipeName << "'";
			selected.push_back(factory);
		}

		return selected;
	}

	const std::vector<PipelineHandlerFactoryBase *> &factories =
		PipelineHandlerFactoryBase::factories();

	/* Match all the registered pipeline handlers. */
	for (const PipelineHandlerFactoryBase *factory : factories) {
		LOG(Camera, Debug)
			<< "Found registered pipeline handler '"
			<< factory->name() << "'";
		selected.push_back(factory);
	}

	return selected;
}

void CameraManager::Private::pipelineFactoryMatch(const PipelineHandlerFactoryBase *factory)
{
	CameraManager *const o = LIBCAMERA_O_PTR();

	/* Provide as many matching pipelines as possible. */
	while (1) {
		std::shared_ptr<PipelineHandler> pipe = factory->create(o);
		if (!pipe->match(enumerator_.get()))
			break;

		LOG(Camera, Debug)
			<< "Pipeline handler \"" << factory->name()
			<< "\" matched";

		pipes_.push_back(pipe);
	}
}

/*
 * Find the active pipeline handler instance that has acquired the media device,
 * if any. Expired entries are pruned from the registry as a side effect. Called
 * from the CameraManager thread only.
 */
std::shared_ptr<PipelineHandler>
CameraManager::Private::findMatchingHandler(const MediaDevice *media)
{
	ASSERT(Thread::current() == this);

	std::shared_ptr<PipelineHandler> match;

	for (auto it = pipes_.begin(); it != pipes_.end();) {
		std::shared_ptr<PipelineHandler> pipe = it->lock();
		if (!pipe) {
			it = pipes_.erase(it);
			continue;
		}

		if (!match && pipe->usesMediaDevice(media))
			match = std::move(pipe);

		++it;
	}

	return match;
}

/*
 * Survey all pipeline handler factories and return the known camera
 * descriptors. Descriptors are cached because later surveys skip the media
 * devices in use by live pipeline handler instances, and would otherwise drop
 * the cameras already initialised. Called on the CM thread.
 */
std::vector<std::shared_ptr<CameraDescriptor>> CameraManager::Private::surveyThread()
{
	ASSERT(Thread::current() == this);

	for (const PipelineHandlerFactoryBase *factory : pipelineFactories())
		surveyFactory(factory);

	return descriptors_;
}

/*
 * Survey the cameras of a single pipeline handler factory and cache their
 * descriptors. Returns the result of the survey, -ENOTSUP if the pipeline
 * handler does not support surveying. Called on the CM thread.
 */
int CameraManager::Private::surveyFactory(const PipelineHandlerFactoryBase *factory)
{
	ASSERT(Thread::current() == this);

	CameraManager *const o = LIBCAMERA_O_PTR();

	std::shared_ptr<PipelineHandler> pipe = factory->create(o);
	std::vector<std::shared_ptr<CameraDescriptor>> descriptors;
	int ret = pipe->survey(enumerator_.get(), &descriptors);
	if (ret == -ENOTSUP) {
		/* The pipeline handler does not support surveying. */
		return ret;
	} else if (ret < 0) {
		LOG(Camera, Error)
			<< "Failed to survey cameras for pipeline handler "
			<< factory->name() << ": " << strerror(-ret);
		return ret;
	}

	for (std::shared_ptr<CameraDescriptor> &descriptor : descriptors) {
		/*
		 * Record the factory that produced the descriptor, so that
		 * initialize() can create a pipeline handler for it.
		 */
		descriptor->_d()->factory_ = factory;

		auto match = [&](const auto &d) {
			return d->id() == descriptor->id();
		};
		if (std::any_of(descriptors_.begin(), descriptors_.end(), match))
			continue;

		descriptors_.push_back(std::move(descriptor));
	}

	return 0;
}

/*
 * Create every camera in the system. For each pipeline handler, enumerate its
 * cameras and initialise them, or match it when it does not support surveying.
 * Looping through one pipeline handler at a time keeps the cameras in the same
 * order as repeated match() calls would create them. Cameras that already exist
 * are left untouched. Called on the CM thread.
 */
void CameraManager::Private::createCameras()
{
	ASSERT(Thread::current() == this);

	for (const PipelineHandlerFactoryBase *factory : pipelineFactories()) {
		int ret = surveyFactory(factory);
		if (ret == -ENOTSUP) {
			pipelineFactoryMatch(factory);
			continue;
		} else if (ret < 0) {
			continue;
		}

		for (const std::shared_ptr<CameraDescriptor> &descriptor : descriptors_) {
			if (descriptor->_d()->factory_ == factory)
				initializeThread(descriptor);
		}
	}
}

/*
 * Create and register the camera described by \a descriptor. The camera is
 * created by the live pipeline handler instance that has already acquired the
 * camera's media device (if any), so that cameras sharing a pipeline instance
 * join it. Otherwise a new pipeline handler instance is created. If the camera
 * has already been initialised, the existing instance is returned. Called on
 * the CM thread.
 */
std::shared_ptr<Camera>
CameraManager::Private::initializeThread(std::shared_ptr<CameraDescriptor> descriptor)
{
	ASSERT(Thread::current() == this);

	CameraManager *const o = LIBCAMERA_O_PTR();

	/* If the camera has already been created, return it. */
	std::shared_ptr<Camera> camera = o->get(descriptor->id());
	if (camera)
		return camera;

	const CameraDescriptor::Private *dPriv = descriptor->_d();
	const std::vector<std::shared_ptr<MediaDevice>> &mediaDevices =
		dPriv->mediaDevices_;
	if (mediaDevices.empty())
		return nullptr;

	/*
	 * Route the descriptor to the live pipeline handler instance holding
	 * the media device, or create a new instance.
	 */
	std::shared_ptr<PipelineHandler> pipe =
		findMatchingHandler(mediaDevices.front().get());
	bool joined = !!pipe;

	if (!pipe)
		pipe = dPriv->factory_->create(o);

	int ret = pipe->createCamera(descriptor.get());
	if (ret) {
		LOG(Camera, Error)
			<< "Failed to create camera '" << descriptor->id()
			<< "': " << strerror(-ret);
		return nullptr;
	}

	if (!joined)
		pipes_.push_back(pipe);

	return o->get(descriptor->id());
}

void CameraManager::Private::cleanup()
{
	enumerator_->devicesAdded.disconnect(this);

	{
		MutexLocker locker(mutex_);
		started_ = false;
	}

	descriptors_.clear();
	pipes_.clear();

	/*
	 * Release all references to cameras to ensure they all get destroyed
	 * before the device enumerator deletes the media devices. Cameras are
	 * destroyed via Object::deleteLater() API, hence we need to explicitly
	 * process deletion requests from the thread's message queue as the event
	 * loop is not in action here.
	 */
	{
		MutexLocker locker(mutex_);
		cameras_.clear();
	}

	dispatchMessages(Message::Type::DeferredDelete);

	enumerator_.reset(nullptr);
}

/**
 * \brief Add a camera to the camera manager
 * \param[in] camera The camera to be added
 *
 * This function is called by pipeline handlers to register the cameras they
 * handle with the camera manager. Registered cameras are immediately made
 * available to the system.
 *
 * Device numbers from the SystemDevices property are used by the V4L2
 * compatibility layer to map V4L2 device nodes to Camera instances.
 *
 * \context This function shall be called from the CameraManager thread.
 */
void CameraManager::Private::addCamera(std::shared_ptr<Camera> camera)
{
	ASSERT(Thread::current() == this);

	{
		MutexLocker locker(mutex_);

		for (const std::shared_ptr<Camera> &c : cameras_) {
			if (c->id() == camera->id()) {
				LOG(Camera, Fatal)
					<< "Trying to register a camera with a duplicated ID '"
					<< camera->id() << "'";
				return;
			}
		}

		cameras_.push_back(camera);
	}

	LOG(Camera, Info)
		<< "Adding camera '" << camera->id() << "' for pipeline handler "
		<< camera->_d()->pipe()->name();

	/* Report the addition to the public signal */
	CameraManager *const o = LIBCAMERA_O_PTR();
	o->cameraAdded.emit(camera);
}

/**
 * \brief Remove a camera from the camera manager
 * \param[in] camera The camera to be removed
 *
 * This function is called by pipeline handlers to unregister cameras from the
 * camera manager. Unregistered cameras won't be reported anymore by the
 * cameras() and get() calls, but references may still exist in applications.
 *
 * \context This function shall be called from the CameraManager thread.
 */
void CameraManager::Private::removeCamera(std::shared_ptr<Camera> camera)
{
	ASSERT(Thread::current() == this);

	{
		MutexLocker locker(mutex_);

		auto iter = std::find(cameras_.begin(), cameras_.end(), camera);
		if (iter == cameras_.end())
			return;

		cameras_.erase(iter);
	}

	LOG(Camera, Debug)
		<< "Unregistering camera '" << camera->id() << "'";

	/* Report the removal to the public signal */
	CameraManager *const o = LIBCAMERA_O_PTR();
	o->cameraRemoved.emit(camera);
}

/**
 * \fn const GlobalConfiguration &CameraManager::Private::configuration() const
 * \brief Get global configuration bound to the camera manager
 *
 * \return Reference to the configuration
 */

/**
 * \fn CameraManager::Private::ipaManager() const
 * \brief Retrieve the IPAManager, constructing it on first use
 * \context This function shall be called from the CameraManager thread.
 * \return The IPAManager for this CameraManager
 */
#endif /* __DOXYGEN_PUBLIC__ */

/**
 * \class CameraManager
 * \brief Provide access and manage all cameras in the system
 *
 * The camera manager is the entry point to libcamera. It enumerates devices,
 * associates them with pipeline managers, and provides access to the cameras
 * in the system to applications. The manager owns all Camera objects and
 * handles hot-plugging and hot-unplugging to manage the lifetime of cameras.
 *
 * To interact with libcamera, an application starts by creating a camera
 * manager instance. Only a single instance of the camera manager may exist at
 * a time. Attempting to create a second instance without first deleting the
 * existing instance results in undefined behaviour.
 *
 * The manager is initially stopped, and shall be started with start(). This
 * will enumerate all the cameras present in the system, which can then be
 * listed with list() and retrieved with get().
 *
 * Applications that do not need every camera in the system can instead use
 * enumerate(), which reports a CameraDescriptor for each camera found without
 * initialising any of them, followed by initialize() for the cameras required.
 *
 * Cameras are shared through std::shared_ptr<>, ensuring that a camera will
 * stay valid until the last reference is released without requiring any special
 * action from the application. Once the application has released all the
 * references it held to cameras, the camera manager can be stopped with
 * stop().
 */

CameraManager *CameraManager::self_ = nullptr;

CameraManager::CameraManager()
	: Extensible(std::make_unique<CameraManager::Private>())
{
	if (self_)
		LOG(Camera, Fatal)
			<< "Multiple CameraManager objects are not allowed";

	self_ = this;
}

/**
 * \brief Destroy the camera manager
 *
 * Destroying the camera manager stops it if it is currently running.
 */
CameraManager::~CameraManager()
{
	stop();

	self_ = nullptr;
}

/**
 * \brief Start the camera manager
 *
 * Start the camera manager and enumerate all devices in the system. Once
 * the start has been confirmed the user is free to list and otherwise
 * interact with cameras in the system until either the camera manager
 * is stopped or the camera is unplugged from the system.
 *
 * \return 0 on success or a negative error code otherwise
 */
int CameraManager::start()
{
	LOG(Camera, Info) << "libcamera " << version_;

	int ret = _d()->start();
	if (ret)
		LOG(Camera, Error) << "Failed to start camera manager: "
				   << strerror(-ret);

	return ret;
}

/**
 * \brief Stop the camera manager
 *
 * Before stopping the camera manager the caller is responsible for making
 * sure all cameras provided by the manager are returned to the manager.
 *
 * After the manager has been stopped no resource provided by the camera
 * manager should be consider valid or functional even if they for one
 * reason or another have yet to be deleted. This includes the camera
 * descriptors returned by enumerate(), which can no longer be initialised.
 */
void CameraManager::stop()
{
	Private *const d = _d();
	d->exit();
	d->wait();
}

/**
 * \brief Enumerate the cameras in the system without initialising them
 *
 * Enumerate the devices in the system and return a descriptor for every
 * camera found. A camera can then be initialised from its descriptor with
 * initialize(), avoiding the cost of initialising cameras the application
 * will not use.
 *
 * Only cameras of pipeline handlers that support surveying are reported.
 * Cameras of other pipeline handlers are created by start() and reported by
 * cameras() as before.
 *
 * Descriptors are reported in match order, grouped by pipeline handler.
 * Descriptors of cameras removed from the system remain listed until the
 * camera manager is stopped.
 *
 * This function starts the camera manager if it is not yet running.
 *
 * \return A list of descriptors for the cameras found in the system
 */
std::vector<std::shared_ptr<CameraDescriptor>> CameraManager::enumerate()
{
	return _d()->enumerate();
}

/**
 * \brief Initialise the camera described by \a descriptor
 * \param[in] descriptor The descriptor of the camera to initialise
 *
 * Create and initialise the camera described by a \a descriptor returned by
 * enumerate(). The returned camera is fully initialised, identical to a camera
 * created by start(), and is also reported through cameras(), get() and the
 * cameraAdded signal.
 *
 * If the camera has already been initialised, the existing instance is
 * returned.
 *
 * \context This function may be called from any thread, but shall not be
 * called concurrently with start() or stop().
 *
 * \return A shared pointer to the initialised Camera, or nullptr if the
 * camera could not be initialised.
 */
std::shared_ptr<Camera> CameraManager::initialize(const std::shared_ptr<CameraDescriptor> &descriptor)
{
	return _d()->initialize(descriptor);
}

/**
 * \fn CameraManager::cameras()
 * \brief Retrieve all available cameras
 *
 * Before calling this function the caller is responsible for ensuring that
 * the camera manager is running.
 *
 * \context This function is \threadsafe.
 *
 * \return List of all available cameras
 */
std::vector<std::shared_ptr<Camera>> CameraManager::cameras() const
{
	const Private *const d = _d();

	MutexLocker locker(d->mutex_);

	return d->cameras_;
}

/**
 * \brief Get a camera based on ID
 * \param[in] id ID of camera to get
 *
 * Before calling this function the caller is responsible for ensuring that
 * the camera manager is running.
 *
 * \context This function is \threadsafe.
 *
 * \return Shared pointer to Camera object or nullptr if camera not found
 */
std::shared_ptr<Camera> CameraManager::get(std::string_view id)
{
	Private *const d = _d();

	MutexLocker locker(d->mutex_);

	for (const std::shared_ptr<Camera> &camera : d->cameras_) {
		if (camera->id() == id)
			return camera;
	}

	return nullptr;
}

/**
 * \var CameraManager::cameraAdded
 * \brief Notify of a new camera added to the system
 *
 * This signal is emitted when a new camera is detected and successfully handled
 * by the camera manager. The notification occurs alike for cameras detected
 * when the manager is started with start() or when new cameras are later
 * connected to the system. When the signal is emitted the new camera is already
 * available from the list of cameras().
 *
 * The signal is emitted from the CameraManager thread. Applications shall
 * minimize the time spent in the signal handler and shall in particular not
 * perform any blocking operation.
 */

/**
 * \var CameraManager::cameraRemoved
 * \brief Notify of a new camera removed from the system
 *
 * This signal is emitted when a camera is removed from the system. When the
 * signal is emitted the camera is not available from the list of cameras()
 * anymore.
 *
 * The signal is emitted from the CameraManager thread. Applications shall
 * minimize the time spent in the signal handler and shall in particular not
 * perform any blocking operation.
 */

/**
 * \fn const std::string &CameraManager::version()
 * \brief Retrieve the libcamera version string
 * \context This function is \threadsafe.
 * \return The libcamera version string
 */

} /* namespace libcamera */
