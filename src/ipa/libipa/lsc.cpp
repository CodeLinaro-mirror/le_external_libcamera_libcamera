/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * libIPA Lsc algorithms
 */

#include "lsc.h"

/**
 * \file lsc.h
 * \brief libipa lsc algorithm
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(Lsc)

namespace ipa {

namespace lsc {

/**
 * \struct ActiveState
 * \brief The lsc active state
 *
 * \var ActiveState::enabled
 * \brief Boolean flag for the LscAlgorithm enable status
 */

/**
 * \struct FrameContext
 * \brief The lsc frame context
 *
 * \var FrameContext::enabled
 * \brief Boolean flag for the LscAlgorithm enable status
 *
 * \var FrameContext::update
 * \brief Boolean flag for the LscAlgorithm updated status
 */

} /* namespace lsc */

/**
 * \class LscAlgorithmBase
 * \brief Base class for LscAlgorithm
 *
 * Base class for LscAlgorithm for non-templated functions implementation
 */

/**
 * \brief Queue a request to the lsc algorithm
 * \param[in] state The lsc active state
 * \param[in] context The lsc frame context
 * \param[in] controls The list of controls associated with a Request
 *
 * Queue a new list of \a controls to the lsc algorithm.
 * The only supported control is controls::LensShadingCorrectionEnable.
 */
void LscAlgorithmBase::queueRequest(lsc::ActiveState &state,
				    lsc::FrameContext &context,
				    const ControlList &controls)
{
	const auto &lscEnable = controls.get(controls::LensShadingCorrectionEnable);
	if (lscEnable && *lscEnable != state.enabled) {
		state.enabled = *lscEnable;

		LOG(Lsc, Debug)
			<< (state.enabled ? "Enabling" : "Disabling") << " Lsc";

		context.update = true;
	}

	context.enabled = state.enabled;
}

/**
 * \brief Populate the list of lsc metadata
 * \param[in] context The lsc frame context
 * \param[in] metadata The list of metadata
 *
 * Populates the list of \a metadata with controls handled by the LscAlgorithm
 * class. The only supported metadata is controls::LensShadingCorrectionEnable.
 */
void LscAlgorithmBase::process(lsc::FrameContext &context, ControlList &metadata)
{
	metadata.set(controls::LensShadingCorrectionEnable, context.enabled);
}

/**
 * \class LscAlgorithm
 * \brief libIPA lsc algorithm implementation
 * \tparam U The fixedpoint lsc engine register format
 *
 * Due to the optical characteristics of the lens, the light intensity received
 * by the sensor is not uniform. The Lens Shading Correction algorithm applies
 * multipliers to all pixels to compensate for the lens shading effect.
 *
 * The LscAlgorithm implements the libipa Lens Shading Correction algorithm
 * using an implementation of the LscImplementation interface.
 *
 * It provides support for parsing the tuning file content and generates tables
 * of per-colour temperature gains that IPA algorithms can use to program their
 * lsc engine.
 *
 * The init() function parses the tuning file and loads the gain tables either
 * in tabular form (LscTable) or as radial polynomials (LscPolynomial). The gain
 * tables are organized per-colour temperature with per-colour components gain
 * vectors or polynomial coefficients.
 *
 * The colour components names are IPA-implementation specific and depend on the
 * ISP lsc engine design. Some lsc engine support 4 colour components (r, gr,
 * gb, b), some only support 3 colour components (r, g, b). The name (and
 * number) of the expected colour components shall be provided to
 * LscAlgorithm::init() using the LscDescriptor::keys field.
 *
 * Example of a tabular lens shading tuning file with 'r', 'g' and 'b' colour
 * components. The gain table has been here omitted, but the expected number
 * of entries has to be equal to
 * LscDescriptor::numHCells * LscDescriptor::numVCells.
 *
 * \code{.yaml}
 * - Lsc:
 *    sets:
 *      - ct: 2500
 *        r: [
 *        	.. gains table omitted..
 *        ]
 *        g: [
 *        	.. gains table omitted..
 *        ]
 *        b: [
 *        	.. gains table omitted..
 *        ]
 *      - ct: 6500
 *        r: [
 *        	.. gains table omitted..
 *        ]
 *        g: [
 *        	.. gains table omitted..
 *        ]
 *        b: [
 *        	.. gains table omitted..
 *        ]
 * \endcode
 *
 * Example of a polynomial lens shading tuning file with 'r', 'gr', 'gb' and 'b'
 * colour components:
 *
 * \code{.yaml}
 * - Lsc:
 *    type: "polynomial"
 *    sets:
 *      - ct: 2500
 *        r:
 *          cx: 0.5006571711950275
 *          cy: 0.510093737499277
 *          k0: 1.5393282208428813
 *          k1: -1.1434559757908016
 *          k2: 4.332602305814554
 *          k3: 0.0
 *          k4: 0.0
 *        gr:
 *          cx: 0.5009320529087338
 *          cy: 0.511208038949085
 *          k0: 1.5634738574805407
 *          k1: -1.5623484259968348
 *          k2: 4.846686073656501
 *          k3: 0.0
 *          k4: 0.0
 *        gb:
 *          cx: 0.5012013290343839
 *          cy: 0.5128251541578288
 *          k0: 1.526147944919103
 *          k1: -1.4316976083689723
 *          k2: 4.792604063222728
 *          k3: 0.0
 *          k4: 0.0
 *        b:
 *          cx: 0.49864139511067784
 *          cy: 0.5162095081739346
 *          k0: 1.0405245474038738
 *          k1: 0.05618339879447103
 *          k2: 1.8792813594001752
 *          k3: 0.0
 *          k4: 0.0
 *      - ct: 6000
 *        r:
 *          cx: 0.5006202239353942
 *          cy: 0.5099531318307661
 *          k0: 1.4702946023945032
 *          k1: -0.8893767547927631
 *          k2: 3.920547732201387
 *          k3: 0.0
 *          k4: 0.0
 *        gr:
 *          cx: 0.500907874178317
 *          cy: 0.511084916024106
 *          k0: 1.5336172760559457
 *          k1: -1.39964026514435
 *          k2: 4.565487728954618
 *          k3: 0.0
 *          k4: 0.0
 *        gb:
 *          cx: 0.5011898608900477
 *          cy: 0.5126797906745105
 *          k0: 1.5013145790354843
 *          k1: -1.2747407173754124
 *          k2: 4.514682876897286
 *          k3: 0.0
 *          k4: 0.0
 *        b:
 *          cx: 0.4987561413116136
 *          cy: 0.5159619420778772
 *          k0: 1.0102986422191802
 *          k1: 0.13263449763985727
 *          k2: 1.686556107316064
 *          k3: 0.0
 *          k4: 0.0
 * \endcode
 *
 * The lsc tables or the polynomial definition are generated at tuning time
 * using an image of known resolution which needs to be specified in
 * LscDescriptor::sensorSize.
 *
 * At LscAlgorithm::configure() time the lsc tables are re-sampled on the
 * sensor's crop rectangle in use to adapt them to the configuration in use for
 * a streaming session. Polynomial lsc tables support re-sampling and can be
 * applied to any sensor configuration. Grid-based lsc tables cannot currently
 * be re-sampled and the configuration as parsed from the tuning file is used
 * for all sensor configurations providing best-effort results.
 *
 * \todo: Implement grid based re-sampling
 *
 * When the IPA algorithms wants to get access to the (re-sampled) tables to
 * program its lsc engine, it uses LscAlgorithm::interpolateComponents() to get
 * an lsc table interpolated by the LscAlgorithm class for the specified colour
 * temperature. If the algorithm wants to access the non-interpolated tables it
 * can retrieve them using LscAlgorithm::getComponents().
 */

/**
 * \fn LscAlgorithm::init()
 * \param[in] tuningData The tuning data
 * \param[in] controls The IPA list of supported controls
 * \param[in] descriptor The lsc engine descriptor
 *
 * Parse \a tuningData according to the settings specified in \a descriptor to
 * populate the lsc data and registers lsc controls in \a controls.
 *
 * \return 0 on success, a negative error code otherwise
 */

/**
 * \fn LscAlgorithm::configure()
 * \param[in] state The lsc active state
 * \param[in] analogCrop The current sensor analog crop rectangle
 * \param[in] xPos List of horizontal positions of the LSC grid nodes
 * \param[in] yPos List of vertical positions of the LSC grid nodes
 *
 * Re-sample the lsc data for an \a analogCrop.
 *
 * Lsc tables are generated at tuning time using a known sensor configuration.
 * When a new streaming session is started, it might use a different sensor
 * configuration for which the lsc tables need to be adjusted to.
 *
 * This function re-generates the lsc tables to adapt them to a new sensor
 * configuration, specifically it re-samples the lsc data for a new \a
 * analogCrop on a grid specified by \a xPos and \a yPos. Re-sampling of
 * lsc data is currently supported by polynomial-based lsc tables.
 *
 * \sa LscImplementation::resampleLscData
 *
 * \return 0 on success, a negative error code otherwise
 */

/**
 * \fn LscAlgorithm::interpolateComponents
 * \brief Interpolate the lsc tables for a given colour temperature
 * \param[in] ct The colour temperature
 *
 * Lsc tables are generated using different colour temperatures during the
 * tuning phase.
 *
 * This function returns the interpolated lsc data for a given \a ct
 * colour temperature.
 *
 * IPA algorithm can use this function to obtain a list of gains per-colour
 * component to program their lsc engines with every time a significant enough
 * change in colour temperature is detected.
 *
 * Calling this function is only valid after LscAlgorithm::configure() has been
 * called. An empty components list is returned otherwise.
 *
 * \return The lsc gains table interpolated for temperature \a ct
 */

/**
 * \fn LscAlgorithm::getComponents
 *
 * Return the map of lsc data per colour temperature.
 *
 * Calling this function is only valid after LscAlgorithm::configure() has been
 * called. An empty components list is returned otherwise.
 *
 * \return The map of lsc gains tables per colour-temperature
 */

} /* namespace ipa */

} /* namespace libcamera */
