.. SPDX-License-Identifier: CC-BY-SA-4.0

Camera Sensor Helper Guide
==========================

This guide explains how to add a ``CameraSensorHelper`` for a new sensor and how
to measure the analogue gain model and black level when a public datasheet is
not available.

Background
----------

The software ISP and other IPA modules need to know how the sensor maps
analogue gain control codes to linear gain factors, and which digital number
corresponds to optical black. That information lives in
``src/ipa/libipa/camera_sensor_helper.cpp`` as a small helper class registered
with ``REGISTER_CAMERA_SENSOR_HELPER``.

Static metadata such as unit cell size and test pattern mode maps belong in
``src/libcamera/sensor/camera_sensor_properties.cpp``. Sensor control application
delays should only be listed there when they have been measured or documented;
leave ``sensorDelays`` empty to use libcamera defaults.

Adding a helper
---------------

1. Confirm the kernel driver name (for example ``imx471``) and the V4L2 control
   ranges for exposure and analogue gain (``v4l2-ctl -d /dev/v4l-subdevX
   --list-ctrls``).
2. Prefer values from a datasheet when available. Document the source in the
   commit message and, if short, a brief code comment (for example
   ``/* From datasheet: 64 at 10bits. */``).
3. If no datasheet is available, measure gain response and black level as
   described below. Put the measurement summary in the **commit message**; keep
   in-code comments short.
4. Register the helper under the same string the kernel uses for the subdev
   name (without bus address suffixes).

Common Sony sensors program a linear code ``c`` such that:

.. math::

   G = \frac{k}{k - c}

with ``k = 1024`` for many IMX models. The maximum V4L2 code may still be lower
than ``k - 1`` (for example codes ``0..800``), which yields a modest maximum
gain even when the model is correct.

Measuring analogue gain
-----------------------

The ``utils/measure-analogue-gain.py`` helper automates a fixed-exposure gain
sweep using the libcamera ``cam`` tool and ``v4l2-ctl``.

Dependencies:

* ``cam`` (libcamera tools), or set ``LIBCAMERA_CAM`` to a wrapper/command
* ``v4l2-ctl`` from v4l-utils
* Python 3.10+

High-level procedure:

1. Capture **raw** Bayer frames (``cam --stream role=raw,...``), not processed
   RGB. Soft ISP AGC is avoided so the V4L2 codes you set stay put.
2. Lock exposure (and digital gain if present) on the sensor subdev.
3. Step ``V4L2_CID_ANALOGUE_GAIN`` across the driver range.
4. Compute the mean of active-area samples (useful bit depth, for example
   10-bit values carried in 16-bit words).
5. Black-subtract using a low percentile at minimum gain, or a dark frame.
6. Fit measured brightness ratios to ``G = k/(k-code)`` and compare with the
   model you intend to hard-code (often ``k = 1024``).

Terminology used by the tool:

* **p1** — first percentile of the sample histogram (near-black floor)
* **p50** — median
* **signal** — ``mean - black`` after black subtraction
* **ratio** — ``signal(code) / signal(0)``

Example (Sony IMX471 on an IPU7 laptop, 1928×1088, stride 3904):

.. code-block:: shell

   ./utils/measure-analogue-gain.py \
       --sensor-name imx471 \
       --width 1928 --height 1088 --stride 3904 --bit-depth 10 \
       --exposure 200 --digital-gain 256 \
       --gains 0,50,100,150,200,300,400,500,600,700,800 \
       --model-k 1024 \
       --out /tmp/gain-measure

The tool writes ``results.csv`` and ``results.json`` including relative error
versus the reference model and a best-fit ``k`` for ``G=k/(k-code)``.

Choose an exposure short enough that the highest gain code does not saturate
(watch the reported ``p95`` / ``max`` columns). If the scene is too dark,
increase exposure carefully and re-run.

Measuring black level
---------------------

Prefer a **covered lens** dark frame at minimum analogue gain. If that is not
practical, a very short exposure at minimum gain is a useful approximation.

.. code-block:: shell

   # Cover the lens if possible, then:
   ./utils/measure-analogue-gain.py --sensor-name imx471 --dark \
       --width 1928 --height 1088 --stride 3904 --bit-depth 10 \
       --out /tmp/gain-measure

For a 10-bit sensor pedestal of ``B`` DN, the 16-bit black level used by
helpers is typically ``B << 6`` (for example ``64`` → ``4096``).

Do not invent a pedestal solely because another sensor in the same vendor
family uses that value; measure when the datasheet is missing.

Sensor geometry and properties
------------------------------

``camera_sensor_properties.cpp`` entries should list:

* ``unitCellSize`` in nanometres when known
* ``testPatternModes`` mapped to the modes the kernel driver registers
* ``sensorDelays`` only when verified

Frame sizes and crop rectangles still come from the V4L2 subdev format and
selection API; properties do not replace a complete kernel driver.

Submitting the result
---------------------

* Use ``git format-patch`` and ``git send-email`` so maintainers can ``git am``
  the series without MUA line wrapping (see :doc:`/contributing`).
* Split helper registration and static properties into separate commits when
  both change.
* Put measurement methodology and tables in the commit message (and this guide
  when adding or improving the tool), not large duplicated comments in the
  helper constructor.

Related reading
---------------

* :doc:`/sensor_driver_requirements`
* :doc:`/camera-sensor-model`
* :doc:`/guides/ipa`
