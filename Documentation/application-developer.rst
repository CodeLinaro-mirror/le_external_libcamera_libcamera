Using libcamera in a C++ application
====================================

This tutorial shows how to create a C++ application that uses libcamera
to connect to a camera on a system, capture frames from it for 3
seconds, and write metadata about the frames to standard out.

.. TODO: Check how much of the example code runs before camera start etc?

Application skeleton
--------------------

Most of the code in this tutorial runs in the ``int main()`` function
with a separate global function to handle events. The two functions need
to share data, which are stored in global variables for simplicity. A
production-ready application would organize the various objects created
in classes, and the event handler would be a class member function to
provide context data without requiring global variables.

.. code:: cpp

   // Global variables here

   int main()
   {
       // Code to follow
   }

Camera Manager
--------------

Every libcamera-based application needs an instance of a
`CameraManager <http://libcamera.org/api-html/classlibcamera_1_1CameraManager.html>`_
that runs for the life of the application. When the Camera Manager
starts, it finds all the cameras available to the current system. Behind
the scenes, libcamera abstracts and manages the complex pipelines that
kernel drivers expose through the `Linux Media
Controller <https://www.kernel.org/doc/html/latest/media/uapi/mediactl/media-controller-intro.html>`_
and `Video for Linux (V4L2) <https://www.linuxtv.org/docs.php>`_ APIs,
meaning that an application doesn’t need to handle device or driver
specifics.

Create a Camera Manager instance, and then start it. An application
should only create one Camera Manager instance.

.. code:: cpp

   CameraManager *cm = new CameraManager();
   cm->start();

When the application runs, it starts the Camera Manager, which
identifies all supported devices and creates cameras the application can
interact with.

Before the ``int main()`` function, create a global shared pointer
variable for the camera:

.. code:: cpp

   std::shared_ptr<Camera> camera;

   int main()
   {
       // Code to follow
   }

Add the code below that lists all available cameras, and for this
example, writes them to standard output:

.. code:: cpp

   for (auto const &camera : cm->cameras())
       std::cout << camera->name() << std::endl;

For example, the output on a Linux machine with a connected USB webcam
is ``UVC Camera (046d:080a)``. When running Ubuntu in a VM on macOS, the
output is ``FaceTime HD Camera (Built-in):``.

Create and acquire a camera
---------------------------

What libcamera considers a camera
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The libcamera library supports fixed and hot-pluggable cameras,
including cameras, plugged and unplugged after starting an application.
It supports point-and-shoot still image and video capture, either
controlled directly by the CPU or exposed through an internal USB bus as
a UVC device designed for video conferencing usage. The libcamera
library considers any device that includes independent camera sensors,
such as front and back sensors, as multiple camera devices.

This example application uses a single camera (the first camera) that
the Camera Manager reports as available to applications.

Application code can access cameras by index or by name. The code below
retrieves the name of the first available camera and gets the camera by
name from the Camera Manager.

.. code:: cpp

   std::string cameraName = cm->cameras()[0]->name();
   camera = cm->get(cameraName);

Once you know what camera you want to use, an application needs to
acquire an exclusive lock to it so no other application can use it.

.. code:: cpp

   camera->acquire();

Configure the camera
--------------------

Before the application can do anything with the camera, it needs to know
what its capabilities are. These capabilities include resolutions,
supported pixel formats, and more. The libcamera library uses
a ``StreamRole`` to define four predefined ways an application intends
to use a camera; these are:

To find out if how an application wants to use the camera is possible,
generate a new configuration using a vector of ``StreamRole``\s, and
send that vector to the camera. To do this, create a new configuration
variable and use the ``generateConfiguration`` function to produce a
``CameraConfiguration``. If the camera can handle the configuration, it
returns a full ``CameraConfiguration``, and if it can't, a null pointer.

.. code:: cpp

   std::unique_ptr<CameraConfiguration> config = camera->generateConfiguration( { StreamRole::Viewfinder } );

A ``CameraConfiguration`` has a ``StreamConfiguration`` instance for
each ``StreamRole`` the application requested, and that the camera can
support. Each of these has a default size and format that the camera
assigned, depending on the ``StreamRole`` requested.

The code below creates a new ``StreamConfiguration`` variable and
populates it with the value of the first (and only) ``StreamRole`` in
the camera configuration. It then outputs the value to standard out.

.. code:: cpp

   StreamConfiguration &streamConfig = config->at(0);
   std::cout << "Default viewfinder configuration is: " << streamConfig.toString() << std::endl;

This outputs something like
``Default viewfinder configuration is: 1280x720-0x56595559``.

Change and validate the configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

With a ``StreamConfiguration`` defined, an application can make changes
to the parameters it contains, for example, to change the width and
height, use the following code:

.. code:: cpp

   streamConfig.size.width = 640;
   streamConfig.size.height = 480;

If an application changes any parameters, validate them before applying
them to the camera using the ``validate`` function. If the new values
are invalid, the validation process adjusts the parameter to what it
considers a valid value. An application should check that the adjusted
configuration is something expected. The ``validate`` method returns a
`Status <http://libcamera.org/api-html/classlibcamera_1_1CameraConfiguration.html#a64163f21db2fe1ce0a6af5a6f6847744>`_
enum an application can check to see if the Pipeline Handler adjusted
the configuration.

For example, the code above set the width and height to 640x480, but if
the camera cannot produce an image that large, it might return the
configuration with a new size of 320x240 and a status of ``Adjusted``.

For this example application, the code below prints the adjusted values
to standard out.

.. code:: cpp

   config->validate();
   std::cout << "Validated viewfinder configuration is: " << streamConfig.toString() << std::endl;

For example, the output might be something like
``Validated viewfinder configuration is: 1280x720-0x56595559``.

With a validated ``CameraConfiguration``, send it to the camera to
confirm the new configuration:

.. code:: cpp

   camera->configure(config.get());

If an application doesn’t first validate the configuration before
calling ``configure``, there’s a chance that calling the function fails.

Allocate FrameBuffers
---------------------

An application needs to reserve the memory that libcamera can write
incoming camera data to, and that the application can then read data for
each frame from. The libcamera library uses ``FrameBuffer`` instances to
buffer frames of data from memory. An application should reserve enough
memory for the ``FrameBuffer``\s that streams need based on the
configured sizes and formats.

The libcamera library consumes buffers provided by applications as
``FrameBuffer`` instances, which makes libcamera a consumer of buffers
exported by other devices (such as displays or video encoders), or
allocated from an external allocator (such as ION on Android).

In some situations, applications do not have any means to allocate or
get hold of suitable buffers, for instance, when no other device is
involved, or on Linux platforms that lack a centralized allocator. The
``FrameBufferAllocator`` class provides a buffer allocator an
application can use in these situations.

An application doesn’t have to use the default ``FrameBufferAllocator``
that libcamera provides. It can instead allocate memory manually and
pass the buffers in ``Request``\s (read more about ``Request`` in
`the frame capture section <#frame-capture>`_ of this guide). The
example in this guide covers using the ``FrameBufferAllocator`` that
libcamera provides.

Using the libcamera ``FrameBufferAllocator``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As the camera manager knows what configuration is available, it can
allocate all the resources with a single method, and de-allocate with
another.

Applications create a ``FrameBufferAllocator`` for a Camera and use it
to allocate buffers for streams of a ``CameraConfiguration`` with the
``allocate()`` function.

.. code:: cpp

   FrameBufferAllocator *allocator = new FrameBufferAllocator(camera);

   for (StreamConfiguration &cfg : *config) {
       int ret = allocator->allocate(cfg.stream());
       if (ret < 0) {
           std::cerr << "Can't allocate buffers" << std::endl;
           return -ENOMEM;
       }

       unsigned int allocated = allocator->buffers(cfg.stream()).size();
       std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
   }

For the example camera above with ``1280x720-0x56595559`` configuration,
libcamera reserves **4** buffers for the stream.

Frame Capture
~~~~~~~~~~~~~

The libcamera library follows a familiar streaming request model for
data (frames of camera data). For each frame a camera captures, an
application must queue a request for it to the camera. With libcamera, a
``Request`` is at least one Stream (one source from a Camera), that has
one ``FrameBuffer`` full of image data.

First, create an instance of a ``Stream`` from the ``StreamConfig``,
assign a vector of ``FrameBuffer``\s to the allocation created above,
and create a vector of the requests the application will make.

.. code:: cpp

   Stream *stream = streamConfig.stream();
   const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator->buffers(stream);
   std::vector<Request *> requests;

Create ``Request``s for the size of the ``FrameBuffer`` by using the
``createRequest`` function and adding all the requests created to a
vector. For each request add a buffer to it with the ``addBuffer``
function, passing the stream the buffer belongs to, and the FrameBuffer.

.. code:: cpp

       for (unsigned int i = 0; i < buffers.size(); ++i) {
           Request *request = camera->createRequest();
           if (!request)
           {
               std::cerr << "Can't create request" << std::endl;
               return -ENOMEM;
           }

           const std::unique_ptr<FrameBuffer> &buffer = buffers[i];
           int ret = request->addBuffer(stream, buffer.get());
           if (ret < 0)
           {
               std::cerr << "Can't set buffer for request"
                     << std::endl;
               return ret;
           }

           requests.push_back(request);
       }

.. TODO: Controls

.. TODO: A request can also have controls or parameters that you can apply to the image.

Start the camera
----------------

With the code to handle processing camera data in place, start the
camera to begin capturing frames and queuing requests to it.

.. code:: cpp

   camera->start();
   for (Request *request : requests)
       camera->queueRequest(request);

Event handling and callbacks
----------------------------

The libcamera library uses the concept of signals and slots (`similar to Qt <https://doc.qt.io/qt-5/signalsandslots.html>`_) to connect events
with callbacks to handle those events.

Signals
~~~~~~~

A camera class instance emits a signal when the buffer has been
completed (image data written into). Because a Request can contain
multiple streams, libcamera emits the Request completed signal when all
streams within the request complete.

To receive these signals, connect a slot function to the signal an
application should act on.

.. code:: cpp

   camera->requestCompleted.connect(requestComplete);

Slots
~~~~~

Every time the camera request completes, it emits a signal, and the
connected slot invoked, passing the Request as a parameter.

For this example application, the matching ``requestComplete`` slot
method outputs information about the FrameBuffer to standard out, but
the callback is typically where an application accesses the image data
from the camera and does something with it.

Signals operate in the libcamera ``CameraManager`` thread context, so it
is important not to block the thread for a long time, as this blocks
internal processing of the camera pipelines, and can affect realtime
performance, with skipped frames etc.

Start an event loop
~~~~~~~~~~~~~~~~~~~

To emit signals that slots can respond to, an application needs an event
loop. An application can use the ``EventDispatcher`` class as an event
loop (inspired by `the Qt event system <https://doc.qt.io/qt-5/eventsandfilters.html>`_) for an
application to listen to signals from resources libcamera handles.

The libcamera library does this by creating instances of the
``EventNotifier`` class, which models a file descriptor event source an
application can monitor and registers them with the ``EventDispatcher``.
Whenever the ``EventDispatcher`` detects an event, it is monitoring, and
it emits an ``EventNotifier::activated signal``. The ``Timer`` class
controls the length event loops run for that an application can register
with a dispatcher with the ``registerTimer`` function.

The code below creates a new instance of the ``EventDispatcher`` class,
adds it to the camera manager, creates a timer to run for 3 seconds, and
during the length of that timer, the ``EventDispatcher`` processes
events that occur, and calls the relevant signals.

.. code:: cpp

   EventDispatcher *dispatcher = cm->eventDispatcher();
   Timer timer;
   timer.start(3000);
   while (timer.isRunning())
       dispatcher->processEvents();

Create a matching slot method
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Create the ``requestComplete`` function that matches the slot:

.. code:: cpp

   static void requestComplete(Request *request)
   {
       // Code to follow
   }

The signal/slot flow is the only way to pass requests and buffers from
libcamera back to the application. There are times when a request can
emit a ``requestComplete`` signal, but this request is actually
canceled, for example, by application shutdown. To avoid an application
processing image data that doesn’t exist, it’s worth checking that the
request is still in an expected state (You can find `a full list of the completion statuses in the documentation <https://www.libcamera.org/api-html/classlibcamera_1_1Request.html#a2209ba8d51af8167b25f6e3e94d5c45b>`_).

.. code:: cpp

   if (request->status() == Request::RequestCancelled) return;

When the request completes, an application can access the buffers from
the request using the ``buffers()`` function, which returns a map of
each buffer, and the stream it is associated with.

.. code:: cpp

   const std::map<Stream *, FrameBuffer *> &buffers = request->buffers();

Iterating through the map allows you to inspect each buffer from each
stream completed in this request, and access the metadata for each frame
the camera captured. The buffer metadata contains information such as
capture status, a timestamp, and the bytes used.

.. code:: cpp

   for (auto bufferPair : buffers) {
       FrameBuffer *buffer = bufferPair.second;
       const FrameMetadata &metadata = buffer->metadata();
   }

The buffer describes the image data, but a buffer can consist of more
than one image plane that in memory hold the image data in the Frames.
For example, the Y, U, and V components of a YUV-encoded image are
described by a plane.

For this example application, inside the ``for`` loop from above, print
the Frame sequence number and details of the planes.

.. code:: cpp

   std::cout << " seq: " << std::setw(6) << std::setfill('0') << metadata.sequence << " bytesused: ";

   unsigned int nplane = 0;
   for (const FrameMetadata::Plane &plane : metadata.planes)
   {
       std::cout << plane.bytesused;
       if (++nplane < metadata.planes.size()) std::cout << "/";
   }

   std::cout << std::endl;

The expected output shows each monotonically increasing frame sequence
number and the bytes used by planes.

.. code:: text

   seq: 000000 bytesused: 1843200
   seq: 000002 bytesused: 1843200
   seq: 000004 bytesused: 1843200
   seq: 000006 bytesused: 1843200
   seq: 000008 bytesused: 1843200
   seq: 000010 bytesused: 1843200
   seq: 000012 bytesused: 1843200
   seq: 000014 bytesused: 1843200
   seq: 000016 bytesused: 1843200
   seq: 000018 bytesused: 1843200
   seq: 000020 bytesused: 1843200
   seq: 000022 bytesused: 1843200
   seq: 000024 bytesused: 1843200
   seq: 000026 bytesused: 1843200
   seq: 000028 bytesused: 1843200
   seq: 000030 bytesused: 1843200
   seq: 000032 bytesused: 1843200
   seq: 000034 bytesused: 1843200
   seq: 000036 bytesused: 1843200
   seq: 000038 bytesused: 1843200
   seq: 000040 bytesused: 1843200
   seq: 000042 bytesused: 1843200

With the handling of this request complete, reuse the buffer by adding
it back to the request with its matching stream, and create a new
request using the ``createRequest`` function.

.. code:: cpp

   request = camera->createRequest();
   if (!request)
   {
       std::cerr << "Can't create request" << std::endl;
       return;
   }

   for (auto it = buffers.begin(); it != buffers.end(); ++it)
   {
       Stream *stream = it->first;
       FrameBuffer *buffer = it->second;

       request->addBuffer(stream, buffer);
   }

   camera->queueRequest(request);

Clean up and stop the application
---------------------------------

The application is now finished with the camera and the resources the
camera uses, so needs to do the following:

-  stop the camera
-  free the stream from the FrameBufferAllocator
-  delete the FrameBufferAllocator
-  release the lock on the camera and reset the pointer to it
-  stop the camera manager
-  exit the application

.. code:: cpp

   camera->stop();
   allocator->free(stream);
   delete allocator;
   camera->release();
   camera.reset();
   cm->stop();

   return 0;