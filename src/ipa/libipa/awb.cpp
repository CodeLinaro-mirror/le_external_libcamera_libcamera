/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Ideas on Board Oy
 *
 * libIPA Awb algorithms
 */

#include "awb.h"

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "awb_bayes.h"
#include "awb_grey.h"

constexpr int32_t kMinColourTemperature = 2500;
constexpr int32_t kMaxColourTemperature = 10000;
constexpr int32_t kDefaultColourTemperature = 5000;

/**
 * \file awb.h
 * \brief libipa awb implementation
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(Awb)

namespace ipa {

namespace awb {

/**
 * \struct Context
 * \brief Awb gains and colour temperature
 *
 * \var Context::gains
 * \brief The white balance gains
 *
 * \var Context::temperatureK
 * \brief The colour temperature, in Kelvin
 */

/**
 * \struct ActiveState
 * \brief Active awb state shared across frames
 *
 * \var ActiveState::manual
 * \brief The most recent manually requested awb state
 *
 * \var ActiveState::automatic
 * \brief The most recent automatically calculated awb state
 *
 * \var ActiveState::autoEnabled
 * \brief True when automatic awb is selected
 */

/**
 * \struct FrameContext
 * \brief Per-frame awb state
 *
 * \var FrameContext::autoEnabled
 * \brief True when automatic awb is in use
 */

} /* namespace awb */

/**
 * \class AwbStats
 * \brief An abstraction class wrapping hardware-specific awb statistics
 *
 * IPA modules shall provide a derived implementation of this class to give the
 * awb algorithm access to the hardware-specific statistics data in a common
 * format.
 */

/**
 * \fn AwbStats::AwbStats()
 * \brief Construct an empty AwbStat
 */

/**
 * AwbStat::~AwbStat
 * \brief Virtual class destructor
 */

/**
 * \fn AwbStats::computeColourError()
 * \brief Compute an error value for when the given gains would be applied
 * \param[in] gains The gains to apply
 *
 * Compute an error value (non-greyness) assuming the given \a gains would be
 * applied. To keep the actual implementations computationally inexpensive,
 * the squared colour error shall be returned.
 *
 * If the awb statistics provide multiple zones, the average of the individual
 * squared errors shall be returned. Averaging/normalizing is necessary so that
 * the numeric dimensions are the same on all hardware platforms.
 *
 * \return The computed error value
 */

/**
 * \fn AwbStats::valid()
 * \brief Retrieve if the awb statistics are valid
 *
 * If the colour mean values are too small, the AwbAlgorithm class doesn't have
 * enough information to meaningfully calculate white-balance gains. Freeze the
 * algorithm in that case.
 *
 * \return True if the awb statistics are valid, false otherwise
 */

/**
 * \fn AwbStats::rgbMeans
 * \brief Retrieve the RGB means values
 *
 * Fetch the RGB means from the statistics in the [0, 1] range.
 *
 * \return The RGB means
 */

/**
 * \var AwbStats::rgbMeans_
 * \brief Mean values of colour channels
 */

/**
 * \var AwbStats::rg_
 * \brief Red/Green ratio
 */

/**
 * \var AwbStats::bg_
 * \brief Blue/Green ratio
 */

/**
 * \class AwbImplementation
 * \brief Pure virtual base class for awb algorithms implementations
 *
 * The AwbImplementation class defines the interface for the awb algorithm
 * implementations.
 *
 * It is currently implemented by the AwbGrey and AwbBayes classes.
 *
 * The interface defines an init() function to initialize the algorithm with the
 * content of the tuning file and two functions to compute colour gains
 * according to the algorithm operating mode (auto or manual) in use.
 *
 * The calculateAwb() function calculates colour gains given a set of statistics
 * provided by the IPA module. It is used when the algorithm operates in auto
 * mode and gains are dynamically computed given a new set of statistics from
 * the awb engine.
 *
 * The gainsFromColourTemperature() function instead computes the white balance
 * gains from a colour temperature. This function does not take any statistics
 * into account. It is used to compute the colour gains when the user manually
 * specifies a colour temperature.
 */

/**
 * \struct AwbImplementation::Result
 * \brief The result of an awb computation
 *
 * \var AwbImplementation::Result::gains
 * \brief The computed white balance gains
 *
 * \var AwbImplementation::Result::temperatureK
 * \brief The computed colour temperature, in Kelvin
 */

/**
 * \fn AwbImplementation::~AwbImplementation
 * \brief Virtual class destructor
 */

/**
 * \fn AwbImplementation::init()
 * \param[in] tuningData
 * \brief Initialize the algorithm by parsing \a tuningData
 */

/**
 * \fn AwbImplementation::calculateAwb()
 * \param[in] stats The awb statistics
 * \param[in] lux The estimated lux level
 * \param[in] range The colour temperature search limits
 *
 * Calculate a new set of colour gains and a colour temperature given a new
 * set of statistics \a stats, an estimated luminance \a lux and a range of
 * colour temperature to limit the search.
 *
 * \return A Result computed using the new statistics
 */
/**
 * \fn AwbImplementation::gainsFromColourTemperature()
 * \param[in] temperatureK Colour temperature in Kelvin
 *
 * Calculate a new set of colour gains for a new manually specified colour
 * temperature \a temperatureK.
 *
 * \return The RGB gain values adjusted to \a temperatureK or std::nullopt
 */

/**
 * \class AwbAlgorithmBase
 * \brief Base class for AwbAlgorithm for non-templated functions implementation
 *
 * Base class for AwbAlgorithm where non-templated functions are implemented.
 * IPA implementations shall use AwbAlgorithm and not this class.
 */

/**
 * \brief Initialize the algorithm with the given tuning data
 * \param[in] tuningData The tuning data to use for the algorithm
 *
 * Parse \a tuningData to initialize the awb algorithm and register controls.
 * IPA modules are expected to call this function as part of their
 * implementation of Algorithm::init().
 *
 * \return 0 on success, a negative error code otherwise
 */
int AwbAlgorithmBase::init(const ValueNode &tuningData)
{
	bayes_ = false;

	if (!tuningData.contains("algorithm"))
		LOG(Awb, Info) << "No Awb algorithm specified, using grey world";

	auto mode = tuningData["algorithm"].get<std::string>("grey");
	if (mode == "grey") {
		impl_ = std::make_unique<AwbGrey>();
	} else if (mode == "bayes") {
		impl_ = std::make_unique<AwbBayes>();
		bayes_ = true;
	} else {
		LOG(Awb, Error) << "Unknown Awb algorithm: " << mode;
		return -EINVAL;
	}

	LOG(Awb, Debug) << "Using Awb algorithm: " << mode;

	int ret = impl_->init(tuningData);
	if (ret)
		return ret;

	controls_[&controls::ColourTemperature] =
		ControlInfo(kMinColourTemperature, kMaxColourTemperature,
			    kDefaultColourTemperature);
	controls_[&controls::AwbEnable] = ControlInfo(false, true);

	return parseModeConfigs(tuningData, controls::AwbAuto);
}

/**
 * \brief Configure the awb algorithm
 * \param[in] state The awb active state
 * \return 0 if successful, an error code otherwise
 */
int AwbAlgorithmBase::configure(awb::ActiveState &state)
{
	state.manual.gains = RGB<double>{ 1.0 };
	auto gains = impl_->gainsFromColourTemperature(kDefaultColourTemperature);
	if (gains)
		state.automatic.gains = *gains;
	else
		state.automatic.gains = RGB<double>{ 1.0 };

	state.autoEnabled = true;
	state.manual.temperatureK = kDefaultColourTemperature;
	state.automatic.temperatureK = kDefaultColourTemperature;

	return 0;
}

/**
 * \brief Queue a Request to the awb algorithm
 * \param[in] state The awb active state
 * \param[in] frame The frame number
 * \param[in] frameContext The awb frame context
 * \param[in] controls The list of controls part of the Request
 *
 * Queue a new Request to the awb algorithm and modify its behaviour according
 * to the provided controls.
 *
 * The currently handled controls are:
 * - controls::AwbEnable
 * - controls::AwbMode
 * - controls::ColourGains
 * - controls::ColourTemperature
 */
void AwbAlgorithmBase::queueRequest(awb::ActiveState &state,
				    [[maybe_unused]] const uint32_t frame,
				    awb::FrameContext &frameContext,
				    const ControlList &controls)
{
	const auto &awbEnable = controls.get(controls::AwbEnable);
	if (awbEnable && *awbEnable != state.autoEnabled) {
		state.autoEnabled = *awbEnable;

		LOG(Awb, Debug)
			<< (*awbEnable ? "Enabling" : "Disabling") << " Awb";
	}

	auto mode = controls.get(controls::AwbMode);
	if (mode) {
		auto it = modes_.find(static_cast<controls::AwbModeEnum>(*mode));
		if (it == modes_.end()) {
			LOG(Awb, Error) << "Unsupported Awb mode " << *mode;
			return;
		}

		currentMode_ = &it->second;
	}

	frameContext.autoEnabled = state.autoEnabled;

	if (frameContext.autoEnabled)
		return;

	const auto &colourGains = controls.get(controls::ColourGains);
	const auto &colourTemperature = controls.get(controls::ColourTemperature);
	bool update = false;
	if (colourGains) {
		state.manual.gains.r() = (*colourGains)[0];
		state.manual.gains.b() = (*colourGains)[1];
		/*
		 * \todo Colour temperature reported in metadata is now
		 * incorrect, as we can't deduce the temperature from the gains.
		 * This will be fixed with the bayes AWB algorithm.
		 */
		update = true;
	} else if (colourTemperature) {
		state.manual.temperatureK = *colourTemperature;
		const auto &gains = impl_->gainsFromColourTemperature(*colourTemperature);
		if (gains) {
			state.manual.gains.r() = gains->r();
			state.manual.gains.b() = gains->b();
			update = true;
		}
	}

	if (update)
		LOG(Awb, Debug)
			<< "Set colour gains to " << state.manual.gains;

	frameContext.gains = state.manual.gains;
	frameContext.temperatureK = state.manual.temperatureK;
}

/**
 * \brief Set the gains and colour temperature values in \a frameContext
 * \param[in] state The awb active state
 * \param[in] frameContext The awb frame context
 *
 * If auto mode is enabled, take the most recently computed gains and use them
 * for the current frame. Otherwise, if in manual mode, gains and colour
 * temperature for a frame are set at queueRequest() time.
 */
void AwbAlgorithmBase::prepare(awb::ActiveState &state,
			       awb::FrameContext &frameContext)
{
	if (frameContext.autoEnabled) {
		frameContext.gains = state.automatic.gains;
		frameContext.temperatureK = state.automatic.temperatureK;
	}
}

/**
 * \brief Process awb statistics to calculate gains and populate metadata
 * \param[in] state The awb active state
 * \param[in] frameContext The awb frame context
 * \param[in] stats The awb statistics
 * \param[in] lux The lux value as estimated by the IPA module
 * \param[out] metadata The metadata list
 *
 * Process \a stats to calculate new gains and colour temperature and populate
 * \a metadata with the results.
 */
void AwbAlgorithmBase::process(awb::ActiveState &state,
			       awb::FrameContext &frameContext,
			       const AwbStats &stats, unsigned int lux,
			       ControlList &metadata)
{
	if (!stats.valid())
		return;

	auto awbResult = impl_->calculateAwb(stats, lux, { currentMode_->ctLo,
							   currentMode_->ctHi });

	/*
	 * Clamp the gain values to the hardware, according to the gainMin_
	 * and gainMax_ values.
	 */
	awbResult.gains = awbResult.gains.clamp(gainMin_, gainMax_);

	/* Smooth color gains adjustments. */
	double speed = 0.2;
	double ct = awbResult.temperatureK;
	ct = ct * speed + state.automatic.temperatureK * (1 - speed);

	state.automatic.temperatureK = awbResult.temperatureK;
	state.automatic.gains = awbResult.gains * speed +
				state.automatic.gains * (1 - speed);

	/* Populate metadata. */
	metadata.set(controls::AwbEnable, frameContext.autoEnabled);
	metadata.set(controls::ColourGains, { static_cast<float>(frameContext.gains.r()),
					      static_cast<float>(frameContext.gains.b()) });
	metadata.set(controls::ColourTemperature, frameContext.temperatureK);

	LOG(Awb, Debug) << std::showpoint << "Means " << stats.rgbMeans()
			<< ", gains " << state.automatic.gains
			<< ", temp " << state.automatic.temperatureK << "K";
}

/*
 * \brief Parse the mode configurations from the tuning data
 * \param[in] tuningData the ValueNode representing the tuning data
 * \param[in] def The default value for the AwbMode control
 *
 * Utility function to parse the tuning data for an AwbMode entry and read all
 * provided modes. It adds controls::AwbMode to AwbAlgorithmBase::controls_ and
 * populates AwbAlgorithmBase::modes_. For a list of possible modes see \ref
 * controls::AwbModeEnum.
 *
 * Each mode entry must contain a "lo" and "hi" key to specify the lower and
 * upper colour temperature of that mode. For example:
 *
 * \code{.unparsed}
 * algorithms:
 *   - Awb:
 *     AwbMode:
 *       AwbAuto:
 *         lo: 2500
 *         hi: 8000
 *       AwbIncandescent:
 *         lo: 2500
 *         hi: 3000
 *       ...
 * \endcode
 *
 * If \a def is supplied but not contained in the \a tuningData, -EINVAL is
 * returned.
 *
 * AwbModes are only used by the AwbBayes implementation.
 *
 * \sa controls::AwbModeEnum
 * \return Zero on success, negative error code otherwise
 */
int AwbAlgorithmBase::parseModeConfigs(const ValueNode &tuningData,
				       const ControlValue &def)
{
	if (!bayes_) {
		/* AwbGrey does not support and does not use modes. */
		currentMode_ = &AwbGreyMode;
		return 0;
	}

	std::vector<ControlValue> availableModes;

	const ValueNode &modes = tuningData[controls::AwbMode.name()];
	if (!modes.isDictionary()) {
		LOG(Awb, Error)
			<< "AwbModes must be a dictionary.";
		return -EINVAL;
	}

	for (const auto &[modeName, modeDict] : modes.asDict()) {
		if (controls::AwbModeNameValueMap.find(modeName) ==
		    controls::AwbModeNameValueMap.end()) {
			LOG(Awb, Warning)
				<< "Skipping unknown AWB mode '"
				<< modeName << "'";
			continue;
		}

		if (!modeDict.isDictionary()) {
			LOG(Awb, Error)
				<< "Invalid AWB mode '" << modeName << "'";
			return -EINVAL;
		}

		const auto &modeValue = static_cast<controls::AwbModeEnum>(
			controls::AwbModeNameValueMap.at(modeName));

		ModeConfig &config = modes_[modeValue];

		auto hi = modeDict["hi"].get<double>();
		if (!hi) {
			LOG(Awb, Error) << "Failed to read hi param of mode "
					<< modeName;
			return -EINVAL;
		}
		config.ctHi = *hi;

		auto lo = modeDict["lo"].get<double>();
		if (!lo) {
			LOG(Awb, Error) << "Failed to read low param of mode "
					<< modeName;
			return -EINVAL;
		}
		config.ctLo = *lo;

		availableModes.push_back(modeValue);
	}

	if (modes_.empty()) {
		LOG(Awb, Error) << "No AWB modes configured";
		return -EINVAL;
	}

	if (!def.isNone() &&
	    modes_.find(def.get<controls::AwbModeEnum>()) == modes_.end()) {
		const auto &names = controls::AwbMode.enumerators();
		LOG(Awb, Error) << names.at(def.get<controls::AwbModeEnum>())
				<< " mode is missing in the configuration.";
		return -EINVAL;
	}

	controls_[&controls::AwbMode] = ControlInfo(availableModes, def);
	currentMode_ = &modes_[controls::AwbAuto];

	return 0;
}

/**
 * \var AwbAlgorithmBase::controls_
 * \brief Controls info map for the controls registered by the awb algorithm
 */

/**
 * \var AwbAlgorithmBase::gainMin_
 * \brief The minimum supported gain value
 *
 * Minimum gain value used to clamp the awb algorithm calculation results in the
 * range supported by the platform awb engine.
 *
 * The min and max gain values are initialized by AwbAlgorithm::init().
 */

/**
 * \var AwbAlgorithmBase::gainMax_
 * \brief The maximum supported gain value
 *
 * Maximum gain value used to clamp the awb algorithm calculation results in the
 * range supported by the platform awb engine.
 *
 * The min and max gain values are initialized by AwbAlgorithm::init().
 */

/*
 * \class AwbAlgorithmBase::ModeConfig
 * \brief Holds the configuration of a single awb mode
 *
 * AWB modes limit the regulation of the awb algorithm to a specific range of
 * colour temperatures. Use by AwbBayes only.
 */

/*
 * \var AwbAlgorithmBase::ModeConfig::ctLo
 * \brief The lowest valid colour temperature of that mode
 */

/*
 * \var AwbAlgorithmBase::ModeConfig::ctHi
 * \brief The highest valid colour temperature of that mode
 */

/*
 * \var AwbAlgorithmBase::modes_
 * \brief Map of all configured modes
 * \sa AwbAlgorithmBase::parseModeConfigs
 */

/**
 * \class AwbAlgorithm
 * \brief The libipa awb algorithm
 * \tparam Q The fixedpoint register representation of gain values
 *
 * Implement the awb algorithm for libipa.
 *
 * The AwbAlgorithm class implements an interface similar in spirit to the one
 * of the Algorithm class. IPA modules are expected to store an instance of
 * AwbAlgorithm as class member, template it with the awb engine gain register
 * representation and call its function in their implementations of the
 * Algorithm interface.
 *
 * The AwbAlgorithm instantiates an AwbImplementation implementation (AwbGrey or
 * AwbBayes) at init() time by parsing the tuning data and uses it to compute
 * the RGB gains and estimate a colour temperature given a set of statistics
 * in the form of a AwbStats derived implementation.
 */

/**
 * \fn AwbAlgorithm::init()
 * \param[in] controls The info map of the IPA controls
 * \copydoc AwbAlgorithmBase::init()
 */

} /* namespace ipa */

} /* namespace libcamera */
