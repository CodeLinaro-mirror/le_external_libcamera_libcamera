/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * libIPA Lsc algorithms
 */

#include "lsc.h"

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "lsc_polynomial.h"
#include "lsc_table.h"

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
 * \class LscAlgorithm
 * \brief libIPA lsc algorithm implementation
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
 * \param[in] tuningData The tuning data
 * \param[in] controls The IPA list of supported controls
 * \param[in] descriptor The lsc engine descriptor
 *
 * Parse \a tuningData according to the settings specified in \a descriptor to
 * populate the lsc data and registers lsc controls in \a controls.
 *
 * \return 0 on success, a negative error code otherwise
 */
int LscAlgorithm::init(const ValueNode &tuningData, ControlInfoMap::Map &controls,
		       const LscDescriptor &descriptor)
{
	polynomial_ = false;

	std::string type = tuningData["type"].get<std::string>("table");
	if (type == "table") {
		impl_ = std::make_unique<LscTable>();
		LOG(Lsc, Debug) << "Using table-based Lsc";
	} else if (type == "polynomial") {
		/*
		 * \todo: Most likely the reference frame should be native_size.
		 * Let's wait how the internal discussions progress.
		 */
		impl_ = std::make_unique<LscPolynomial>(descriptor.sensorSize);
		polynomial_ = true;
		LOG(Lsc, Debug) << "Using polynomial Lsc";
	} else {
		LOG(Lsc, Error) << "Unsupported Lsc algorithm '"
				<< type << "'";
		return -EINVAL;
	}

	const ValueNode &yamlSets = tuningData["sets"];
	if (!yamlSets.isList()) {
		LOG(Lsc, Error) << "'sets' parameter not found in tuning file";
		return -EINVAL;
	}

	int ret = impl_->parseLscData(yamlSets, descriptor);
	if (ret)
		return ret;

	controls[&controls::LensShadingCorrectionEnable] =
		ControlInfo(false, true, true);

	return 0;
}

/**
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
 * \sa LscImplementation::sampleForCrop
 *
 * \return 0 on success, a negative error code otherwise
 */
int LscAlgorithm::configure(lsc::ActiveState &state, const Rectangle &analogCrop,
			    const std::vector<double> &xPos,
			    const std::vector<double> &yPos)
{
	LOG(Lsc, Debug) << "Sample Lsc data for " << analogCrop;
	lsc::ComponentsMap lscData =
		impl_->sampleForCrop(analogCrop, xPos, yPos);

	/*
	 * Retain a copy of the components table.
	 *
	 * We could avoid a copy here if getComponents() could
	 * return sets_.data() but I wasn't able to work around the
	 * compiler refusing it.
	 */
	lscData_ = lscData;

	sets_.setData(std::move(lscData));
	state.enabled = true;

	return 0;
}

/**
 * \brief Queue a request to the lsc algorithm
 * \param[in] state The lsc active state
 * \param[in] context The lsc frame context
 * \param[in] controls The list of controls associated with a Request
 *
 * Queue a new list of \a controls to the lsc algorithm.
 * The only supported control is controls::LensShadingCorrectionEnable.
 */
void LscAlgorithm::queueRequest(lsc::ActiveState &state,
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
void LscAlgorithm::process(lsc::FrameContext &context, ControlList &metadata)
{
	metadata.set(controls::LensShadingCorrectionEnable, context.enabled);
}

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
