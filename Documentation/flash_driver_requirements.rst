.. SPDX-License-Identifier: CC-BY-SA-4.0

.. include:: documentation-contents.rst

.. _flash-driver-requirements:

Flash Driver Requirements
=========================

libcamera handles V4L2 flash devices in the CameraFlash class and defines
a consistent interface through its API towards other library components.

The CameraFlash class uses the V4L2 subdev kernel API to interface with the
camera flash through a sub-device exposed to userspace by the V4L2 flash driver.

In order for libcamera to be fully operational and provide all the required
information to interface with the camera flash to applications and pipeline
handlers, the driver must support a set of mandatory features.

Mandatory Requirements
----------------------

The flash driver is assumed to be fully compliant with the V4L2 specification.

The flash driver shall support the following V4L2 controls:

* `V4L2_CID_FLASH_LED_MODE`_
* `V4L2_CID_FLASH_STROBE_SOURCE`_
* `V4L2_CID_FLASH_STROBE`_
* `V4L2_CID_FLASH_TIMEOUT`_
* `V4L2_CID_FLASH_INTENSITY`_
* `V4L2_CID_FLASH_TORCH_INTENSITY`_

.. _V4L2_CID_FLASH_LED_MODE: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-flash.html
.. _V4L2_CID_FLASH_STROBE_SOURCE: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-flash.html
.. _V4L2_CID_FLASH_STROBE: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-flash.html
.. _V4L2_CID_FLASH_TIMEOUT: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-flash.html
.. _V4L2_CID_FLASH_INTENSITY: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-flash.html
.. _V4L2_CID_FLASH_TORCH_INTENSITY: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-flash.html

