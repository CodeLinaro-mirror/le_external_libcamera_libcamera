/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic Inc.
 *
 * C3ISP IPA Context
 */

#include "ipa_context.h"

/**
 * \file ipa_context.h
 * \brief Context and state information shared between the algorithms
 */

namespace libcamera::ipa::c3isp {

/**
 * \struct IPASessionConfiguration
 * \brief Session configuration for the IPA module
 *
 * The session configuration contains all IPA configuration parameters that
 * remain constant during the capture session, from IPA module start to stop.
 * It is typically set during the configure() operation of the IPA module, but
 * may also be updated in the start() operation.
 */

/**
 * \var IPASessionConfiguration::sensor
 * \brief Sensor-specific configuration of the IPA
 *
 * \var IPASessionConfiguration::sensor.minShutterSpeed
 * \brief Minimum shutter speed supported with the sensor
 *
 * \var IPASessionConfiguration::sensor.maxShutterSpeed
 * \brief Maximum shutter speed supported with the sensor
 *
 * \var IPASessionConfiguration::sensor.minAnalogueGain
 * \brief Minimum analogue gain supported with the sensor
 *
 * \var IPASessionConfiguration::sensor.maxAnalogueGain
 * \brief Maximum analogue gain supported with the sensor
 *
 * \var IPASessionConfiguration::sensor.defVBlank
 * \brief The default vblank value of the sensor
 *
 * \var IPASessionConfiguration::sensor.lineDuration
 * \brief Line duration in microseconds
 *
 * \var IPASessionConfiguration::sensor.size
 * \brief Sensor output resolution
 */

/**
 * \struct IPAActiveState
 * \brief Active state for algorithms
 *
 * The active state contains all algorithm-specific data that needs to be
 * maintained by algorithms across frames. Unlike the session configuration,
 * the active state is mutable and constantly updated by algorithms. The active
 * state is accessible through the IPAContext structure.
 *
 * The active state stores two distinct categories of information:
 *
 *  - The consolidated value of all algorithm controls. Requests passed to
 *    the queueRequest() function store values for controls that the
 *    application wants to modify for that particular frame, and the
 *    queueRequest() function updates the active state with those values.
 *    The active state thus contains a consolidated view of the value of all
 *    controls handled by the algorithm.
 *
 *  - The value of parameters computed by the algorithm when running in auto
 *    mode. Algorithms running in auto mode compute new parameters every
 *    time statistics buffers are received (either synchronously, or
 *    possibly in a background thread). The latest computed value of those
 *    parameters is stored in the active state in the process() function.
 *
 * Each of the members in the active state belongs to a specific algorithm. A
 * member may be read by any algorithm, but shall only be written by its owner.
 */

/**
 * \var IPAActiveState::agc
 * \brief State for the Automatic Gain Control algorithm
 *
 * The \a automatic variables track the latest values computed by algorithm
 * based on the latest processed statistics. All other variables track the
 * consolidated controls requested in queued requests.
 *
 * \var IPAActiveState::agc.exposure
 * \brief Automatic exposure time expressed as a number of lines
 *
 * \var IPAActiveState::agc.gain
 * \brief Automatic analogue gain multiplier
 *
 * \var IPAActiveState::agc.constraintMode
 * \brief Constraint mode as set by the AeConstraintMode control
 *
 * \var IPAActiveState::agc.exposureMode
 * \brief Exposure mode as set by the AeExposureMode control
 */

/**
 * \var IPAActiveState::awb
 * \brief State for the Automatic White Balance algorithm
 *
 * \struct IPAActiveState::awb.gains
 * \brief White balance gains
 *
 * \struct IPAActiveState::awb.gains.manual
 * \brief Manual white balance gains (set through requests)
 *
 * \var IPAActiveState::awb.gains.manual.red
 * \brief Manual white balance gain for R channel
 *
 * \var IPAActiveState::awb.gains.manual.green
 * \brief Manual white balance gain for G channel
 *
 * \var IPAActiveState::awb.gains.manual.blue
 * \brief Manual white balance gain for B channel
 *
 * \struct IPAActiveState::awb.gains.automatic
 * \brief Automatic white balance gains (computed by the algorithm)
 *
 * \var IPAActiveState::awb.gains.automatic.red
 * \brief Automatic white balance gain for R channel
 *
 * \var IPAActiveState::awb.gains.automatic.green
 * \brief Automatic white balance gain for G channel
 *
 * \var IPAActiveState::awb.gains.automatic.blue
 * \brief Automatic white balance gain for B channel
 *
 * \var IPAActiveState::awb.temperatureK
 * \brief Estimated color temperature
 *
 * \var IPAActiveState::awb.autoEnabled
 * \brief Whether the Auto White Balance algorithm is enabled
 */

/**
 * \struct IPAFrameContext
 * \brief Per-frame context for algorithms
 *
 * The frame context stores two distinct categories of information:
 *
 * - The value of the controls to be applied to the frame. These values are
 *   typically set in the queueRequest() function, from the consolidated
 *   control values stored in the active state. The frame context thus stores
 *   values for all controls related to the algorithm, not limited to the
 *   controls specified in the corresponding request, but consolidated from all
 *   requests that have been queued so far.
 *
 *   For controls that can be set manually or computed by an algorithm
 *   (depending on the algorithm operation mode), such as for instance the
 *   colour gains for the AWB algorithm, the control value will be stored in
 *   the frame context in the queueRequest() function only when operating in
 *   manual mode. When operating in auto mode, the values are computed by the
 *   algorithm in process(), stored in the active state, and copied to the
 *   frame context in prepare(), just before being stored in the ISP parameters
 *   buffer.
 *
 *   The queueRequest() function can also store ancillary data in the frame
 *   context, such as flags to indicate if (and what) control values have
 *   changed compared to the previous request.
 *
 * - Status information computed by the algorithm for a frame. For instance,
 *   the colour temperature estimated by the AWB algorithm from ISP statistics
 *   calculated on a frame is stored in the frame context for that frame in
 *   the process() function.
 */

/**
 * \var IPAFrameContext::agc
 * \brief Automatic Gain Control parameters for this frame
 *
 * The exposure and gain are provided by the AGC algorithm, and are to be
 * applied to the sensor in order to take effect for this frame.
 *
 * \var IPAFrameContext::agc.exposure
 * \brief Exposure time expressed as a number of lines computed by the algorithm
 *
 * \var IPAFrameContext::agc.gain
 * \brief Analogue gain multiplier computed by the algorithm
 *
 * The gain should be adapted to the sensor specific gain code before applying.
 *
 * \var IPAFrameContext::agc.autoEnabled
 * \brief Manual/automatic AGC state as set by the AeEnable control
 *
 * \var IPAFrameContext::agc.constraintMode
 * \brief Constraint mode as set by the AeConstraintMode control
 *
 * \var IPAFrameContext::agc.exposureMode
 * \brief Exposure mode as set by the AeExposureMode control
 *
 * \var IPAFrameContext::agc.meteringMode
 * \brief Metering mode as set by the AeMeteringMode control
 *
 * \var IPAFrameContext::agc.maxFrameDuration
 * \brief Maximum frame duration as set by the FrameDurationLimits control
 *
 * \var IPAFrameContext::agc.updateMetering
 * \brief Indicate if new ISP AGC metering parameters need to be applied
 */

/**
 * \var IPAFrameContext::awb
 * \brief Automatic White Balance parameters for this frame
 *
 * \struct IPAFrameContext::awb.gains
 * \brief White balance gains
 *
 * \var IPAFrameContext::awb.gains.red
 * \brief White balance gain for R channel
 *
 * \var IPAFrameContext::awb.gains.green
 * \brief White balance gain for G channel
 *
 * \var IPAFrameContext::awb.gains.blue
 * \brief White balance gain for B channel
 *
 * \var IPAFrameContext::awb.autoEnabled
 * \brief Whether the Auto White Balance algorithm is enabled
 */

/**
 * \var IPAFrameContext::sensor
 * \brief Sensor configuration that used been used for this frame
 *
 * \var IPAFrameContext::sensor.exposure
 * \brief Exposure time expressed as a number of lines
 *
 * \var IPAFrameContext::sensor.gain
 * \brief Analogue gain multiplier
 */

/**
 * \struct IPAContext
 * \brief Global IPA context data shared between all algorithms
 *
 * \var IPAContext::configuration
 * \brief The IPA session configuration, immutable during the session
 *
 * \var IPAContext::activeState
 * \brief The IPA active state, storing the latest state for all algorithms
 *
 * \var IPAContext::frameContexts
 * \brief Ring buffer of per-frame contexts
 */

} /* namespace libcamera::ipa::c3isp */
