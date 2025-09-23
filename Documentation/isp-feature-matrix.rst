.. SPDX-License-Identifier: CC-BY-SA-4.0

libcamera ISP Feature Support Matrix
=====================================

The following table shows the current status of ISP feature support across
different platforms in libcamera.

.. list-table:: ISP Feature Support by Platform
   :header-rows: 1
   :stub-columns: 1

   * - Feature
     - i.MX8MP (RKISP)
     - BCM2711 (Raspberry Pi 4)
     - BCM2712 (Raspberry Pi 5)
     - Intel IPU3
     - Mali C55
     - Software ISP (CPU)

   * - Auto Gain Control Stats (AGC)
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅

   * - Auto Focus (AF)
     - 🛠️ (CDAF)
     - ✅ (PDAF with IMX708)
     - ✅ (PDAF with IMX708)
     - ✅ (CDAF)
     - 🚫
     - ❓

   * - HDR Stitching
     - ✅
     - ✅
     - ✅
     - ❓
     - 🛠️
     - ❓

   * - Companding
     - ✅
     - 🚫
     - 🚫
     - ❓
     - ❓
     - ❓

   * - Black Level Subtraction (BLS)
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅

   * - Lens Shading Correction (LSC)
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ❓

   * - Auto Whitebalance (AWB)
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅

   * - Defective Pixel Correction (DPC)
     - ✅
     - ❓
     - ✅
     - 🚫
     - ✅
     - ❓

   * - 2D Noise Filtering
     - ✅
     - ✅
     - ✅
     - ❓
     - 🛠️
     - ❓

   * - Local Tone Mapping
     - 🛠️
     - 🚫
     - 🚫
     - 🚫
     - 🚫
     - ❓

   * - Demosaicing
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅

   * - Chromatic Aberration Correction (CAC)
     - 🛠️
     - 🚫
     - ✅
     - ❓
     - 🛠️
     - ❓

   * - Sharpening
     - ✅
     - ✅
     - ✅
     - ❓
     - 🛠️
     - ❓

   * - Chroma Noise Reduction
     - 🚫
     - ❓
     - ❓
     - ❓
     - ❓
     - ❓

   * - 3D Noise Filtering
     - 🚫
     - 🚫
     - 🚫
     - ❓
     - 🛠️
     - ❓

   * - Color Correction Matrix (CCM)
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅
     - ✅

   * - Global Tone Mapping
     - 🛠️
     - ✅
     - ✅
     - ✅
     - 🛠️
     - ❓

   * - Gamma Correction
     - ✅
     - ✅
     - ✅
     - ❓
     - 🛠️
     - ✅

   * - Color Space Conversion
     - ✅
     - ✅
     - ✅
     - ❓
     - ❓
     - ✅

   * - Image Stabilization
     - 🛠️
     - ❓
     - ❓
     - ❓
     - ❓
     - ❓

   * - Scaling
     - ✅
     - ✅
     - ✅
     - ❓
     - ✅
     - ✅

   * - Multi-context
     - 🚫
     - 🛠️
     - 🛠️
     - ❓
     - 🛠️
     - ❓

Status Definitions
------------------

✅ **Supported**
    Feature is fully implemented and working in libcamera.

🛠️ **Needs Development**
    Feature is planned or being developed and may have partial or no
    functionality.

🚫 **No Hardware**
    The underlying hardware does not support this feature.

❓ **Unknown**
    Support status is unclear or has not been determined yet.

Platform Notes
--------------

* **i.MX8MP (RKISP)**: Uses the Rockchip ISP IP core integrated in the NXP
  i.MX8MP SoC
* **BCM2711/BCM2712**: Broadcom SoCs used in Raspberry Pi 4 and 5 respectively
* **Intel IPU3**: Intel Image Processing Unit version 3
* **Mali C55**: ARM's camera ISP IP core
* **Software ISP (CPU)**: CPU-based image processing pipeline for platforms
  without hardware ISP

Contributing
------------

This feature matrix is maintained by the libcamera community and is updated as
new features are implemented or new platforms are supported.

**Updating the Matrix**
    When adding support for a new ISP feature or platform (SoC), please update
    this matrix as part of the same patch or series. Ensure that the feature
    support status for all relevant platforms reflects the changes introduced.

**Reporting Updates**
    If you notice any inaccuracies or outdated information in this matrix,
    please open an issue or submit a patch. Refer to the contributing
    guidelines in the :doc:`contributing` guide.
