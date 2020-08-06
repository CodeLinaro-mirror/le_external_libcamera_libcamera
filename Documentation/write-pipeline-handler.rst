Pipeline Handler Writers Guide
==============================

Pipeline handlers are the abstraction layer for device-specific hardware
configuration. They access and control hardware through the V4L2 and
Media Controller kernel interfaces, and implement an internal API to
control the ISP and capture components of a pipeline directly.

A pipeline handler manages most aspects of interacting with a camera
device including:

- Detecting and registering camera devices installed on the system.
- Configuring the image acquisition and manipulation pipeline components to produce images in the request formats and sizes.
- Starting and stopping image streams.
- Applying the application-requested image transformation controls to the image stream, in cooperation with a separate image processing (IPA) module.

Pipeline handlers create Camera device instances based on the devices
they detect and support on the running system.

If you want to add support for a particular device to the libcamera
codebase, you need to create a matching pipeline handler, and
optionally the code for any other features the device supports.

Prerequisite knowledge
----------------------

A pipeline handler uses most of the following libcamera classes, and
below is a brief overview of each of those. The rest of this guide
explains how to use each of them in the context of a pipeline handler.

.. TODO: Convert to sphinx refs

-  `DeviceEnumerator <http://libcamera.org/api-html/classlibcamera_1_1DeviceEnumerator.html>`_:
   Enumerates all media devices attached to the system, and for each
   device found creates an instance of the ``MediaDevice`` class and
   stores it.
-  `DeviceMatch <http://libcamera.org/api-html/classlibcamera_1_1DeviceMatch.html>`_:
   Describes a media device search pattern using entity names, or other
   properties.
-  `MediaDevice <http://libcamera.org/api-html/classlibcamera_1_1MediaDevice.html>`_:
   Instances of this class are associated with a kernel media controller
   device and its connected objects.
-  `V4L2VideoDevice <http://libcamera.org/api-html/classlibcamera_1_1V4L2VideoDevice.html>`_:
   Models an instance of a V4L2 video device constructed with the
   path to a V4L2 video device node.
-  `V4L2SubDevice <http://libcamera.org/api-html/classlibcamera_1_1V4L2Subdevice.html>`_:
   Provides an API to the sub-devices that model the hardware components
   of a V4L2 device.
-  `CameraSensor <http://libcamera.org/api-html/classlibcamera_1_1CameraSensor.html>`_:
   Abstracts camera sensor handling by hiding the details of the V4L2
   subdevice kernel API and caching sensor information.
-  `CameraData <http://libcamera.org/api-html/classlibcamera_1_1CameraData.html>`_:
   Represents device-specific data a pipeline handler associates to a Camera instance.
-  `StreamConfiguration <http://libcamera.org/api-html/structlibcamera_1_1StreamConfiguration.html>`_:
    Models the current configuration of an image stream produced by
    the camera by reporting its format and sizes.
-  `CameraConfiguration <http://libcamera.org/api-html/classlibcamera_1_1CameraConfiguration.html>`_: 
    Represents the current configuration of a camera, which includes a list of stream 
    configurations for each active stream in a capture session. When validated, it is applied 
    to the camera.

Creating a PipelineHandler
--------------------------

This guide walks through the steps to create a simple pipeline handler
called “Vivid” that supports the `V4L2 Virtual Video Test Driver
(vivid) <https://www.kernel.org/doc/html/latest/admin-guide/media/vivid.html>`_.

To use the vivid test driver, you first need to check that the vivid kernel module is loaded,
for example with the ``modprobe vivid`` command.

Create the skeleton file structure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To add a new pipeline handler, create a directory to hold the pipeline code
in the *src/libcamera/pipeline/* directory that matches the name of the
pipeline (in this case *vivid*). Inside the new directory add a *meson.build* file that integrates 
with the libcamera build system, and a *vivid.cpp* file that matches the name of the pipeline.

In the *meson.build* file, add the *vivid.cpp* file as a build source for libcamera by adding it to 
the global meson ``libcamera_sources`` variable:

.. code-block:: meson

   # SPDX-License-Identifier: CC0-1.0

   libcamera_sources += files([
       'vivid.cpp',
   ])

Users of libcamera can selectively enable pipelines while building libcamera using the 
``pipelines`` option. 

For example, to enable only the IPU3, UVC, and VIVID pipelines, specify them as a comma separated
list with ``-Dpipelines`` when generating a build directory:

.. code-block:: shell

    meson build -Dpipelines=ipu3,uvcvideo,vivid'

`Read the Meson build configuration documentation <https://mesonbuild.com/Configuring-a-build-directory.html>`_ for more information.

To add the new pipeline handler to this list of options, add its directory 
name to the libcamera build options in the top level _meson_options.txt_. 

.. code-block:: meson

   option('pipelines',
           type : 'array',
           choices : ['ipu3', 'raspberrypi', 'rkisp1', 'simple', 'uvcvideo', 'vimc', 'vivid'],
           description : 'Select which pipeline handlers to include')


In *vivid.cpp* add the pipeline handler to the ``libcamera`` namespace, define a `PipelineHandler <http://libcamera.org/api-html/classlibcamera_1_1PipelineHandler.html>`_ derived class named 
PipelineHandlerVivid, and add stub methods for the overridden class member.

.. code-block:: cpp

    namespace libcamera {

        class PipelineHandlerVivid : public PipelineHandler
        {
        public:
            PipelineHandlerVivid(CameraManager *manager);

            CameraConfiguration *generateConfiguration(Camera *camera,
                                    const StreamRoles &roles) override;
            int configure(Camera *camera, CameraConfiguration *config) override;

            int exportFrameBuffers(Camera *camera, Stream *stream,
                            std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

            int start(Camera *camera) override;
            void stop(Camera *camera) override;

            int queueRequestDevice(Camera *camera, Request *request) override;

            bool match(DeviceEnumerator *enumerator) override;
        };

        PipelineHandlerVivid::PipelineHandlerVivid(CameraManager *manager)
            : PipelineHandler(manager)
        {
        }

        CameraConfiguration *PipelineHandlerVivid::generateConfiguration(Camera *camera,
                                            const StreamRoles &roles)
        {
            return nullptr;
        }

        int PipelineHandlerVivid::configure(Camera *camera, CameraConfiguration *config)
        {
            return -1;
        }

        int PipelineHandlerVivid::exportFrameBuffers(Camera *camera, Stream *stream,
                                    std::vector<std::unique_ptr<FrameBuffer>> *buffers)
        {
            return -1;
        }

        int PipelineHandlerVivid::start(Camera *camera)
        {
            return -1;
        }

        void PipelineHandlerVivid::stop(Camera *camera)
        {
        }

        int PipelineHandlerVivid::queueRequestDevice(Camera *camera, Request *request)
        {
            return -1;
        }

        bool PipelineHandlerVivid::match(DeviceEnumerator *enumerator)
        {
            return false;
        }
    } /* namespace libcamera */

You must register the ``PipelineHandler`` subclass with the pipeline
handler factory using the
`REGISTER_PIPELINE_HANDLER <http://libcamera.org/api-html/pipeline__handler_8h.html>`_
macro which registers it and creates a global symbol to reference the
class and make it available to try and match devices.

Add the following before the closing curly bracket of the namespace declaration.

.. code-block:: cpp

   REGISTER_PIPELINE_HANDLER(PipelineHandlerVivid);

For debugging and testing a pipeline handler during development, you can
define a log message category for the pipeline handler. The ``LOG_DEFINE_CATEGORY`` macro and 
``LIBCAMERA_LOG_LEVELS`` environment variable help you use
the `inbuilt libcamera logging
infrastructure <http://libcamera.org/api-html/log_8h.html>`_ that allow
for the inspection of internal operations in a user-configurable way.

Add the following before the ``PipelineHandlerVivid`` class declaration.

.. code-block:: cpp

   LOG_DEFINE_CATEGORY(VIVID)

At this point you need the following includes for logging and pipeline handler features.

.. code-block:: cpp

   #include "libcamera/internal/log.h"
   #include "libcamera/internal/pipeline_handler.h"

Run ``meson build`` and ``ninja -C build install`` to build the
libcamera code base, and confirm that the build system found the new
pipeline handler by running
``LIBCAMERA_LOG_LEVELS=Pipeline:0 ./build/src/cam/cam -l``.

And you should see output like the below.

.. code-block:: shell

    DEBUG Pipeline pipeline_handler.cpp:680 Registered pipeline handler "PipelineHandlerVivid"

Matching devices
~~~~~~~~~~~~~~~~

The
`match <http://libcamera.org/api-html/classlibcamera_1_1DeviceMatch.html>`_
class member function is the main entry point of any pipeline handler. When a
libcamera application starts an instance of the camera manager (using
the `start <http://libcamera.org/api-html/classlibcamera_1_1CameraManager.html#a49e322880a2a26013bb0076788b298c5>`_ method), the instantiation calls the
``match`` function with an enumerator of all devices it found on a
system, it acquires the device and creates the resources it needs.

The ``match`` method takes a pointer to the enumerator
as a parameter and should return ``true`` if it matches a device, and
``false`` if not.

The match method should identify if there are suitable devices available in the 
``DeviceEnumerator`` which the pipeline supports, returning ``true`` if it matches a device, and
``false`` if not. To do this, construct the `DeviceMatch <http://libcamera.org/api-html/classlibcamera_1_1DeviceMatch.html>`_
class with the name of the ``MediaController`` device to match. You can specify
the search further by adding specific media entities to the search using the
``.add()`` method on the DeviceMatch.

This example uses search patterns that match vivid, but you should
change this value to suit your device identifier.

Replace the contents of the ``PipelineHandlerVivid::match`` method with the following.

.. code-block:: cpp

   DeviceMatch dm("vivid");
   dm.add("vivid-000-vid-cap");
   return false; // Prevent infinite loops for now

With a device match defined, attempt to acquire exclusive access to the matching media controller device with the
`acquireMediaDevice <http://libcamera.org/api-html/classlibcamera_1_1PipelineHandler.html#a77e424fe704e7b26094164b9189e0f84>`_
method. If the method attempts to acquire a device it has already matched, it returns ``false``.

Add the following below ``dm.add("vivid-000-vid-cap");``.

.. code-block:: cpp

   MediaDevice *media = acquireMediaDevice(enumerator, dm);
   if (!media)
       return false;

The pipeline handler now needs an additional include. Add the following to the existing include block for device enumeration functionality.

.. code-block:: cpp

   #include "libcamera/internal/device_enumerator.h"

At this stage, you should test that the pipeline handler can successfully match
the devices, but have not yet added any code to create a Camera which libcamera
reports to applications.

As a temporary validation step, add a debug print with ``LOG(VIVID, Debug) << "Obtained Vivid Device";``
before the closing ``return false; // Prevent infinite loops for now`` in the ``PipelineHandlerVivid::match`` method for when when the pipeline handler successfully matches the ``MediaDevice`` 
and ``MediaEntity`` names.

Test that the pipeline handler matches and finds a device by rebuilding,
and running
``LIBCAMERA_LOG_LEVELS=Pipeline,VIVID:0 ./build/src/cam/cam -l``.

And you should see output like the below.

.. code-block:: shell
    
    DEBUG VIVID vivid.cpp:74 Obtained Vivid Device

Creating camera data
~~~~~~~~~~~~~~~~~~~~

If the enumerator finds a matching device, the ``match`` method
creates a Camera instance for it. Each Camera has device-specific data
stored using the `CameraData <http://libcamera.org/api-html/classlibcamera_1_1CameraData.html>`_
class, which you extend for the needs of a specific pipeline handler.

Define a ``CameraData`` derived ``VividCameraData()`` class and initialize the base ``CameraData`` 
class using the base ``PipelineHandler`` pointer.

Add the following code after the ``LOG_DEFINE_CATEGORY(VIVID)`` line. 

.. code-block:: cpp

   class VividCameraData : public CameraData
   {
   public:
       VividCameraData(PipelineHandler *pipe, MediaDevice *media)
           : CameraData(pipe), media_(media), video_(nullptr)
       {
       }

       ~VividCameraData()
       {
           delete video_;
       }

       int init();
       void bufferReady(FrameBuffer *buffer);

       MediaDevice *media_;
       V4L2VideoDevice *video_;
       Stream stream_;
   };

Continuing the``CameraData(pipe)``  initializes the base class through it's
constructor, and then the ``VividCameraData`` members. ``media_`` is initialized
with the ``MediaDevice`` passed into this constructor, and the video
capture device is initialized to a ``nullptr`` as it's not yet established.

This example pipeline handler handles a single video device and supports a single
stream, represented by the minimal variable declarations. More complex pipeline handlers might 
register cameras composed of several video devices and sub-devices, or multiple streams per camera 
that represent the several components of the image capture pipeline. You should represent all 
these components in the ``CameraData`` derived class.

The class has a destructor method that deletes the video device when the ``CameraData`` is 
destroyed by an application.

Every camera data class instance needs at least the above, but yours may
add more, in which case you should also remove and clean up anything
extra in the destructor method.

The class then calls a method to initialize the camera data with
``init()``. The base ``CameraData`` class doesn’t define the ``init()``
function, it’s up to pipeline handlers to define how they initialize the
camera and camera data. This method is one of the more device-specific
methods for a pipeline handler, and defines the context of the camera,
and how libcamera should communicate with the camera and store the data
it generates. For real hardware, this includes tasks such as opening the
ISP, or creating a sensor device.

For this example, create an ``init`` method after the ``VividCameraData`` class that creates a new 
V4L2 video device by matching the media entity name of a device using the 
`MediaDevice::getEntityByName <http://libcamera.org/api-html/classlibcamera_1_1MediaDevice.html#ad5d9279329ef4987ceece2694b33e230>`_ helper.

.. code-block:: cpp

   int VividCameraData::init()
   {
       video_ = new V4L2VideoDevice(media_->getEntityByName("vivid-000-vid-cap"));
       if (video_->open())
           return -ENODEV;

       return 0;
   }

Return to the ``match`` method, and remove ``LOG(VIVID, Debug) << "Obtained Vivid Device";`` and
``return false; // Prevent infinite loops for now``, replacing it with the following code.

After a successful device match, the code below creates a new instance of the device-specific 
``CameraData`` class, using a unique pointer to manage the lifetime of the instance.

If the camera data initialization fails, return ``false`` to indicate the failure to the ``match()
`` method and prevent retiring of the pipeline handler.

.. code-block:: cpp

   std::unique_ptr<VividCameraData> data = std::make_unique<VividCameraData>(this, media);

   if (data->init())
       return false;

Create a set of streams for the camera, which for this device is
only one. You create a camera using the static
`Camera::create <http://libcamera.org/api-html/classlibcamera_1_1Camera.html#a453740e0d2a2f495048ae307a85a2574>`_ method,
passing the pipeline handler, the name of the camera, and the streams
available. Then register the camera and its data with the camera manager
using
`registerCamera <http://libcamera.org/api-html/classlibcamera_1_1PipelineHandler.html#adf02a7f1bbd87aca73c0e8d8e0e6c98b>`_.
At the end of the method, return ``true`` to express that a camera was
created successfully.

Add the following below the code added above.

.. code-block:: cpp

   std::set<Stream *> streams{ &data->stream_ };
   std::shared_ptr<Camera> camera = Camera::create(this, data->video_->deviceName(), streams);
   registerCamera(std::move(camera), std::move(data));

   return true;

Add a private ``cameraData`` helper to the ``PipelineHandlerVivid`` class which obtains the camera 
data, and does the necessary casting to convert it to the pipeline-specific ``VividCameraData``. 
This simplifies the process of obtaining the custom camera data, which you need throughout the code 
for the pipeline handler.

.. code-block:: cpp

   private:

       VividCameraData *cameraData(const Camera *camera)
       {
           return static_cast<VividCameraData *>(
               PipelineHandler::cameraData(camera));
       }

At this point, you need to add the following new includes to provide the Camera interface, and 
device interaction interfaces.

.. code-block:: cpp

   #include <libcamera/camera.h>
   #include "libcamera/internal/media_device.h"
   #include "libcamera/internal/v4l2_videodevice.h"

Generating a default configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When a libcamera-based application accesses and controls a camera stream using a pipeline handler,
it generates a default configuration with known valid values. An application can then makes changes 
to the configuration returned, validate those changes with the pipeline handler, and apply them to 
the device.

Validating a configuration requires the pipeline handler to verify that the device 
supports the requested streams and their configurations, that is supports the requested sizes and 
pixel formats, and that system resources are assigned to each stream to avoid any issues. 

Create a `CameraConfiguration <http://libcamera.org/api-html/classlibcamera_1_1CameraConfiguration.html>`_ derived class for the camera device and its empty constructor 
before the ``PipelineHandlerVivid`` class.

.. code-block:: cpp

    class VividCameraConfiguration : public CameraConfiguration
    {
    public:
        VividCameraConfiguration();

        Status validate() override;
    };

   VividCameraConfiguration::VividCameraConfiguration()
       : CameraConfiguration()
   {
   }    

To generate the default configuration, add to the overridden
`generateConfiguration <http://libcamera.org/api-html/classlibcamera_1_1PipelineHandler.html#a7932e87735695500ce1f8c7ae449b65b>`_
method, passing a pointer to the camera device. 

Notice the ``StreamRoles`` type, which libcamera uses to define the predefined ways
an application intends to use a camera (`You can read the full list in the API
documentation <http://libcamera.org/api-html/stream_8h.html#file_a295d1f5e7828d95c0b0aabc0a8baac03>`_).
These are optional hints on how an application intends to use a stream, and a pipeline handler 
should return ideal configuration for each role an application requests.

In the pipeline handler ``generateConfiguration`` implementation, remove the ``return nullptr;``, create a new instance of the ``CameraConfiguration`` derived class,
and assign it to a base class pointer. The function returns the class to applications at
the end of the function.

.. code-block:: cpp

   CameraConfiguration *config = new VividCameraConfiguration();
   VividCameraData *data = cameraData(camera);

A ``CameraConfiguration`` is specific to each pipeline, so you can only
create it from the pipeline handler code path. Application can generate empty configuration and add
desired stream configuration from scratch. To allow for this, add the following beneath the code 
above to return the newly constructed empty configuration in case the application does not pass any 
``StreamRole``s.

.. code-block:: cpp

   if (roles.empty())
       return config;

A production pipeline handler should generate the ``StreamConfiguration``s for all
the appropriate stream roles a camera device supports. For this simpler
example (with only one stream), the pipeline handler always returns the
same configuration. How it does this is reproduced below, but we
recommend you take a look at the Raspberry Pi pipeline handler for a
realistic example.

.. TODO: Add link

To generate a ``StreamConfiguration``, you need a list of pixel formats and
frame sizes supported by the device. You can fetch a map of the
``V4LPixelFormat``s and ``SizeRange``s supported by the device, but the pipeline
handler needs to convert this to a ``libcamera::PixelFormat`` type to pass
to applications. You can do this using ``std::transform`` to convert the
formats and populate a new ``PixelFormat`` map as shown below. Add the following beneath the code 
from above.

.. code-block:: cpp

   std::map<V4L2PixelFormat, std::vector<SizeRange>> v4l2Formats =
   data->video_->formats();
   std::map<PixelFormat, std::vector<SizeRange>> deviceFormats;
   std::transform(v4l2Formats.begin(), v4l2Formats.end(),
          std::inserter(deviceFormats, deviceFormats.begin()),
          [&](const decltype(v4l2Formats)::value_type &format) {
              return decltype(deviceFormats)::value_type{
                  format.first.toPixelFormat(),
                  format.second
              };
          });

The
`StreamFormats <http://libcamera.org/api-html/classlibcamera_1_1StreamFormats.html>`_
class holds information about the pixel formats and frame sizes a stream
supports. The class groups size information by the pixel format, which
can produce it.

The
`StreamConfiguration <http://libcamera.org/api-html/structlibcamera_1_1StreamConfiguration.html>`_
structure models all configurable information for a single video stream.

The code below uses the ``StreamFormats`` class to hold information about the pixel
formats and frame sizes a stream supports, and groups size information by
the pixel format that can support it. It then uses the
``StreamConfiguration`` class to model the information an application
can use to configure a single stream.

Add the following below the code from above.

.. code-block:: cpp

   StreamFormats formats(deviceFormats);
   StreamConfiguration cfg(formats);

Create the default values for pixel formats, sizes, and buffer count
returned by the configuration.

Add the following below the code from above.

.. code-block:: cpp

    cfg.pixelFormat = formats::BGR888;
    cfg.size = { 1280, 720 };
    cfg.bufferCount = 4;

Add each ``StreamConfiguration`` you generate to the ``CameraConfiguration``, validating  
it before returning them to the application.

Add the following below the code from above.

.. code-block:: cpp

    config->addConfiguration(cfg);

    config->validate();

    return config;

To validate any configuration, a pipeline handler must implement the
`validate <http://libcamera.org/api-html/classlibcamera_1_1CameraConfiguration.html#a29f8f263384c6149775b6011c7397093>`_
method that takes any configuration passed to it, can make adjustments to make the configuration 
valid, and returns the validation status.  If changes are made, it marks the configuration as 
``Adjusted``.

Again, this example pipeline handler is simpler, look at the Raspberry Pi pipeline handler for a 
realistic example.

.. TODO: Add link
.. TODO: Can we fit in a description of the checks that are actually done?

Add the following code above ``PipelineHandlerVivid::configure``.

.. code-block:: cpp

   CameraConfiguration::Status VividCameraConfiguration::validate()
   {
       Status status = Valid;

       if (config_.empty())
           return Invalid;

       if (config_.size() > 1) {
           config_.resize(1);
           status = Adjusted;
       }

       StreamConfiguration &cfg = config_[0];

       const std::vector<libcamera::PixelFormat> formats = cfg.formats().pixelformats();
       if (std::find(formats.begin(), formats.end(), cfg.pixelFormat) == formats.end()) {
           cfg.pixelFormat = cfg.formats().pixelformats()[0];
           LOG(VIVID, Debug) << "Adjusting format to " << cfg.pixelFormat.toString();
           status = Adjusted;
       }

       cfg.bufferCount = 4;

       return status;
   }

To handle ``PixelFormat``s, add ``#include <libcamera/formats.h>`` to the include section, rebuild
the codebase, and use ``LIBCAMERA_LOG_LEVELS=Pipeline,VIVID:0 ./build/src/cam/cam -c vivid -I`` to test
the configuration is generated.

You should see the following output.

.. code-block:: shell

    Using camera vivid
    0: 1280x720-BGR888
    * Pixelformat: NV21 (320x180)-(3840x2160)/(+0,+0)
    - 320x180
    - 640x360
    - 640x480
    - 1280x720
    - 1920x1080
    - 3840x2160
    * Pixelformat: NV12 (320x180)-(3840x2160)/(+0,+0)
    - 320x180
    - 640x360
    - 640x480
    - 1280x720
    - 1920x1080
    - 3840x2160
    * Pixelformat: BGRA8888 (320x180)-(3840x2160)/(+0,+0)
    - 320x180
    - 640x360
    - 640x480
    - 1280x720
    - 1920x1080
    - 3840x2160
    * Pixelformat: RGBA8888 (320x180)-(3840x2160)/(+0,+0)
    - 320x180
    - 640x360
    - 640x480
    - 1280x720
    - 1920x1080
    - 3840x2160

Configuring a device
~~~~~~~~~~~~~~~~~~~~

With the configuration generated, and optionally modified and validated, a pipeline handler needs
a method that allows an application to apply a configuration
to a supported device.

The `PipelineHandler::configure() <http://libcamera.org/api-html/classlibcamera_1_1PipelineHandler.html#a930f2a9cdfb51dfb4b9ca3824e84fc29>`_ method receives a valid `CameraConfiguration <http://libcamera.org/api-html/classlibcamera_1_1CameraConfiguration.html>`_ and applies the settings to 
hardware devices, using its content to prepare a device for streaming images.

Replace the contents of the ``PipelineHandlerVivid::configure`` method
with the following that obtains the camera data and stream configuration. This pipeline handler supports only a single stream, so it
directly obtains the first ``StreamConfiguration`` from the camera
configuration. A pipeline handler with multiple streams should handle
requests to configure each of them..

.. code-block:: cpp

    VividCameraData *data = cameraData(camera);
    StreamConfiguration &cfg = config->at(0);
    int ret;

The Vivid capture device is a V4L2 video device, so create a
`V4L2DeviceFormat <http://libcamera.org/api-html/classlibcamera_1_1V4L2DeviceFormat.html>`_
with the fourcc and size attributes to apply directly to the capture device node. The
fourcc attribute is a `V4L2PixelFormat <http://libcamera.org/api-html/classlibcamera_1_1V4L2PixelFormat.html>`_ and differs from the ``libcamera::PixelFormat``.
Converting the format requires knowledge of the plane configuration for
multiplanar formats, so you must explicitly convert it using the
helpers provided by the ``V4LVideoDevice``, in this case ``toV4L2PixelFormat``.

Add the following code beneath the code from above.

.. code-block:: cpp

   V4L2DeviceFormat format = {};
   format.fourcc = data->video_->toV4L2PixelFormat(cfg.pixelFormat);
   format.size = cfg.size;

Set the format defined above using the
`setFormat <http://libcamera.org/api-html/classlibcamera_1_1V4L2VideoDevice.html#ad67b47dd9327ce5df43350b80c083cca>`_
helper method to the Kernel API. You should check if the kernel driver
has adjusted the format, as this shows the pipeline handler has failed
to handle the validation stages correctly, and the configure operation
has also failed.

Add the following code beneath the code from above.

.. code-block:: cpp

       ret = data->video_->setFormat(&format);
       if (ret)
           return ret;

       if (format.size != cfg.size ||
           format.fourcc != data->video_->toV4L2PixelFormat(cfg.pixelFormat))
           return -EINVAL;

Finally, store and set stream-specific data reflecting the state of the
stream. Associate the configuration with the stream by using the
`setStream <http://libcamera.org/api-html/structlibcamera_1_1StreamConfiguration.html#a74a0eb44dad1b00112c7c0443ae54a12>`_
method, and you can also set the values of individual stream
configuration members.

.. NOTE: the cfg.setStream() call here associates the stream to the
   StreamConfiguration however that should quite likely be done as part of
   the validation process. TBD

Add the following code beneath the code from above.

.. code-block:: cpp

       cfg.setStream(&data->stream_);
       cfg.stride = format.planes[0].bpl;

       return 0;

Buffer handling and stream control
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An application using libcamera needs to reserve the memory that it can
write camera data to for each individual stream requested, using
`FrameBuffer <http://libcamera.org/api-html/classlibcamera_1_1FrameBuffer.html>`_
instances to represent frames of data from memory.

An application can create a `FrameBufferAllocator <http://libcamera.org/api-html/classlibcamera_1_1FrameBufferAllocator.html>`_ for a Camera to create
frame buffers suitable for use with that device.

The ``FrameBufferAllocator`` uses the camera
device pipeline handler to export buffers from the underlying device
using the
`exportFrameBuffers <http://libcamera.org/api-html/classlibcamera_1_1PipelineHandler.html#a6312a69da7129c2ed41f9d9f790adf7c>`_
method, that all pipeline handlers must implement.

The ``exportFrameBuffers()`` function uses a Camera and a Stream pointer to
identify the required device to allocate buffers for, and
returns the buffers allocated by adding them to the buffers vector. 

Replace the contents of the ``exportFrameBuffers`` method with the following.

.. code-block:: cpp

    unsigned int count = stream->configuration().bufferCount;
    VividCameraData *data = cameraData(camera);

    return data->video_->exportBuffers(count, buffers);

This example method takes pointers to the camera, the stream, and a
vector of unique pointers to the frame buffers.

The method checks the stream configuration to see how many buffers an application
requested, in the default configuration for this example this is 4, but
an application may have changed this value.

It then uses the ``exportBuffers`` to create the buffers on the
underlying V4L2 video device, which allows a ``FrameBufferAllocator`` to
obtain buffers from the capture device-specific to this stream, and
returns the number created.

When applications obtain buffers through ``exportFrameBuffers``, they are
orphaned and left unassociated with the device, and an application must reimport
them in the pipeline handler ``start`` method. This approach allows you
to use the same interface whether you are using internal or external
buffers for the stream.

Replace the contents of the ``start`` method with the following.

.. code-block:: cpp

    VividCameraData *data = cameraData(camera);
    unsigned int count = data->stream_.configuration().bufferCount;

    int ret = data->video_->importBuffers(count);
    if (ret < 0)
        return ret;

    ret = data->video_->streamOn();
    if (ret < 0) {
        data->video_->releaseBuffers();
        return ret;
    }

    return 0;

The method imports buffers
(`importBuffers <http://libcamera.org/api-html/classlibcamera_1_1V4L2VideoDevice.html#a154f5283d16ebd5e15d63e212745cb64>`_)
to prepare the underlying V4L2 device based on the number requested, and
starts a stream using the
`streamOn <http://libcamera.org/api-html/classlibcamera_1_1V4L2VideoDevice.html#a588a5dc9d6f4c54c61136ac43ff9a8cc>`_
method. If either of the calls fail, the error value is propagated to the caller
and the `releaseBuffers <http://libcamera.org/api-html/classlibcamera_1_1V4L2VideoDevice.html#a191619c152f764e03bc461611f3fcd35>`_
method releases any buffers to leave the device in a consistent state. If your pipeline handler 
uses any image processing algorithms, you should also stop them.

Add the following to the ``stop`` method, which stops the stream
(`streamOff <http://libcamera.org/api-html/classlibcamera_1_1V4L2VideoDevice.html#a61998710615bdf7aa25a046c8565ed66>`_)
and releases the buffers (``releaseBuffers``).

.. code-block:: cpp

    VividCameraData *data = cameraData(camera);
    data->video_->streamOff();
    data->video_->releaseBuffers();

Event handling
~~~~~~~~~~~~~~

Applications use signals and slots (`similar to
Qt <https://doc.qt.io/qt-5/signalsandslots.html>`_) to connect system
events with callbacks to handle those events.

Pipeline handlers `connect <http://libcamera.org/api-html/classlibcamera_1_1Signal.html#aa04db72d5b3091ffbb4920565aeed382>`_ the ``bufferReady`` signal from the capture
devices they support to a member function slot to handle processing of available
frames. When a buffer is ready, the pipeline handler must propagate the
completion of that buffer, and when all buffers have successfully
completed for a request, also complete the Request itself.

In this example, when a buffer completes, the event handler calls the buffer
completion handler of the pipeline handler, and because the device has a
single stream, immediately completes the request.

Returning to the ``int VividCameraData::init()`` method, add the
following above the closing ``return 0;`` that connects the pipeline
handler ``bufferReady`` method to the V4L2 device buffer signaling it is
ready and passing the frame buffer to the class ``bufferReady`` method.

.. code-block:: cpp

   video_->bufferReady.connect(this, &VividCameraData::bufferReady);

The libcamera library follows a familiar streaming request model for
data (frames of camera data). For each frame a camera captures, an
application must queue a request for it to the camera. With libcamera, a
``Request`` is at least one Stream (one source from a Camera), that has
one ``FrameBuffer`` full of image data.

Create the matching ``VividCameraData::bufferReady`` method above the ``REGISTER_PIPELINE_HANDLER(PipelineHandlerVivid);`` line that takes
the frame buffer passed to it as a parameter.

The ``bufferReady`` method obtains the request from the buffer using the
``request`` method, and notifies the pipeline handler that the buffer
and request are completed. In this simpler pipeline handler, there is
only one buffer, so it completes the buffer immediately. You can find a more complex example of 
event handling with supporting multiple streams in the RaspberryPi Pipeline Handler.

.. TODO: Add link

.. code-block:: cpp

   void VividCameraData::bufferReady(FrameBuffer *buffer)
   {
       Request *request = buffer->request();

       pipe_->completeBuffer(camera_, request, buffer);
       pipe_->completeRequest(camera_, request);
   }

Queuing requests between applications and hardware
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When an application sends a request to a pipeline handler, the pipeline
handler must parse the request and identify what actions it should take
to carry out the request on the hardware.

This example pipeline handler identifies the buffer
(`findBuffer <http://libcamera.org/api-html/classlibcamera_1_1Request.html#ac66050aeb9b92c64218945158559c4d4>`_)
from the only supported stream and queues it to the capture device
(`queueBuffer <http://libcamera.org/api-html/classlibcamera_1_1V4L2VideoDevice.html#a594cd594686a8c1cf9ae8dba0b2a8a75>`_).

Replace the contents of ``queueRequestDevice`` with the following.

.. code-block:: cpp

    VividCameraData *data = cameraData(camera);
    FrameBuffer *buffer = request->findBuffer(&data->stream_);
    if (!buffer) {
        LOG(VIVID, Error)
            << "Attempt to queue request with invalid stream";

        return -ENOENT;
    }

    int ret = data->video_->queueBuffer(buffer);
    if (ret < 0)
        return ret;

    return 0;

At this point you can test capture by rebuilding, and using ``LIBCAMERA_LOG_LEVELS=Pipeline,VIVID:0 sudo ./build/src/cam/cam -c vivid -I -C`` which should output frame capture data.

Initializing frame controls
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Controls allow an application using libcamera to control capture
parameters for each stream on a per-frame basis, and a pipeline handler
can define the initial controls to suit the device.

This section is particularly vivid specific as it sets the initial
values of controls to match `the controls that vivid
defines <https://www.kernel.org/doc/html/latest/admin-guide/media/vivid.html#controls>`_.
You won’t need any of the code below for your pipeline handler, but it’s
included as an example of how to implement what your pipeline handler
might need.

Create a list of controls with the
`ControlList <http://libcamera.org/api-html/classlibcamera_1_1ControlList.html>`_
class, and set them using the
`set <http://libcamera.org/api-html/classlibcamera_1_1ControlList.html#a74a1a29abff5243e6e37ace8e24eb4ba>`_
method.

This pipeline handler retains the global state of its controls and shows
an example of creating and setting a control list. In a production
pipeline handler, you typically set controls as part of a request.

.. TODO: Link to example of the above

Create defines beneath the current includes for convenience.

.. code-block:: cpp

   #define VIVID_CID_VIVID_BASE           (0x00f00000 | 0xf000)
   #define VIVID_CID_VIVID_CLASS           (0x00f00000 | 1)
   #define VIVID_CID_TEST_PATTERN          (VIVID_CID_VIVID_BASE  + 0)
   #define VIVID_CID_OSD_TEXT_MODE         (VIVID_CID_VIVID_BASE  + 1)
   #define VIVID_CID_HOR_MOVEMENT          (VIVID_CID_VIVID_BASE  + 2)
   #define VIVID_CID_VERT_MOVEMENT         (VIVID_CID_VIVID_BASE  + 3)
   #define VIVID_CID_SHOW_BORDER           (VIVID_CID_VIVID_BASE  + 4)
   #define VIVID_CID_SHOW_SQUARE           (VIVID_CID_VIVID_BASE  + 5)
   #define VIVID_CID_INSERT_SAV            (VIVID_CID_VIVID_BASE  + 6)
   #define VIVID_CID_INSERT_EAV            (VIVID_CID_VIVID_BASE  + 7)
   #define VIVID_CID_VBI_CAP_INTERLACED    (VIVID_CID_VIVID_BASE  + 8)

In the ``configure`` method, add the below above the
``cfg.setStream(&data->stream_);`` line.

.. code-block:: cpp

    ControlList controls(data->video_->controls());
    controls.set(VIVID_CID_TEST_PATTERN, 0);
    controls.set(VIVID_CID_OSD_TEXT_MODE, 0);

    controls.set(V4L2_CID_BRIGHTNESS, 128);
    controls.set(V4L2_CID_CONTRAST, 128);
    controls.set(V4L2_CID_SATURATION, 128);

    controls.set(VIVID_CID_HOR_MOVEMENT, 5);

    ret = data->video_->setControls(&controls);
    if (ret) {
        LOG(VIVID, Error) << "Failed to set controls: " << ret;
        return ret < 0 ? ret : -EINVAL;
    }

These controls configure VIVID to use a default test pattern, and
enable all on-screen display text, while configuring sensible brightness,
contrast and saturation values. Use the ``controls.set`` method to set individual controls.

Enabling ``HOR_MOVEMENT`` adds movement to the video stream while
capturing, and all controls are set on the vivid capture device through
the ``setControls()`` method below.

Processing controls
~~~~~~~~~~~~~~~~~~~

When constructing the camera, a pipeline handler parses the available
controls on a capture device, and maps supported controls to libcamera
controls, and initializes the defaults.

Create the ``processControls`` method above the ``queueRequestDevice`` method. 
The method loops through the defined control list, and libcamera makes adjustments to the control 
values to convert between libcamera control range definitions and their corresponding values on the 
device.

.. code-block:: cpp

   int PipelineHandlerVivid::processControls(VividCameraData *data, Request *request)
   {
       ControlList controls(data->video_->controls());

       for (auto it : request->controls()) {
           unsigned int id = it.first;
           unsigned int offset;
           uint32_t cid;

           if (id == controls::Brightness) {
               cid = V4L2_CID_BRIGHTNESS;
               offset = 128;
           } else if (id == controls::Contrast) {
               cid = V4L2_CID_CONTRAST;
               offset = 0;
           } else if (id == controls::Saturation) {
               cid = V4L2_CID_SATURATION;
               offset = 0;
           } else {
               continue;
           }

           int32_t value = lroundf(it.second.get<float>() * 128 + offset);
           controls.set(cid, utils::clamp(value, 0, 255));
       }

       for (const auto &ctrl : controls)
           LOG(VIVID, Debug)
               << "Setting control " << utils::hex(ctrl.first)
               << " to " << ctrl.second.toString();

       int ret = data->video_->setControls(&controls);
       if (ret) {
           LOG(VIVID, Error) << "Failed to set controls: " << ret;
           return ret < 0 ? ret : -EINVAL;
       }

       return ret;
   }

Declare the function prototype for the ``processControls`` method within
the private ``PipelineHandlerVivid`` class members, as it is only used internally as a
helper when processing Requests.

.. code-block:: cpp

   private:
       int processControls(VividCameraData *data, Request *request);

A pipeline handler is responsible for applying controls provided in a
Request to the relevant hardware devices. This could be directly on the
capture device, or where appropriate by setting controls on
V4L2Subdevices directly. Each pipeline handler is responsible for
understanding the correct procedure for applying controls to the device they support.

This example pipeline handler applies controls during the
`queueRequestDevice <http://libcamera.org/api-html/classlibcamera_1_1PipelineHandler.html#a106914cca210640c9da9ee1f0419e83c>`_
method for each request, and applies them to the capture device through
the capture node.

In the ``queueRequestDevice`` method, replace the following.

.. code-block:: cpp

    int ret = data->video_->queueBuffer(buffer);
    if (ret < 0)
        return ret;

With the following code.

.. code-block:: cpp

	int ret = processControls(data, request);
	if (ret < 0)
		return ret;

	ret = data->video_->queueBuffer(buffer);
 	if (ret < 0)
 		return ret;

In the ``init`` method, initialize the supported controls beneath the
``video_->bufferReady.connect(this, &VividCameraData::bufferReady);``
line by parsing the available controls on the V4L2 video device, and
creating corresponding libcamera controls, populated with their minimum,
maximum and default values.

.. code-block:: cpp

    const ControlInfoMap &controls = video_->controls();
    ControlInfoMap::Map ctrls;

    for (const auto &ctrl : controls) {
        const ControlId *id;
        ControlInfo info;

        switch (ctrl.first->id()) {
        case V4L2_CID_BRIGHTNESS:
            id = &controls::Brightness;
            info = ControlInfo{ { -1.0f }, { 1.0f }, { 0.0f } };
            break;
        case V4L2_CID_CONTRAST:
            id = &controls::Contrast;
            info = ControlInfo{ { 0.0f }, { 2.0f }, { 1.0f } };
            break;
        case V4L2_CID_SATURATION:
            id = &controls::Saturation;
            info = ControlInfo{ { 0.0f }, { 2.0f }, { 1.0f } };
            break;
        default:
            continue;
        }

        ctrls.emplace(id, info);
    }

    controlInfo_ = std::move(ctrls);

At this point you need to add the following includes to the top of the file for controls handling.

.. code-block:: cpp

    #include <math.h>
    #include <libcamera/controls.h>
    #include <libcamera/control_ids.h>

Testing a pipeline handler
~~~~~~~~~~~~~~~~~~~~~~~~~~

Once you've built the pipeline handler, rebuild the code base, and you
can use the ``LIBCAMERA_LOG_LEVELS=Pipeline,VIVID:0 ./build/src/cam/cam -c vivid -I -C`` command 
to test that the pipeline handler can detect a device, and capture input.

Running the command above outputs (a lot of) information about pixel formats, and then starts capturing frame data.



.. TODO: LIBCAMERA_LOG_LEVELS=Pipeline,VIVID:0 sudo ./build/src/qcam/qcam -c vivid

