/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 *
 * PID controller
 */

#include "libcamera/internal/pid_controller.h"

#include <algorithm>

#include <libcamera/base/log.h>

/**
 * \file pid_controller.h
 * \brief PID controller class
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(PidController)
/**
 * \class PidController
 * \brief Implementation of a PID controller
 *
 * Implementation of a Proportional Integral Derivative controller.
 * See https://en.wikipedia.org/wiki/Proportional-integral-derivative_controller
 * for the underlying details.
 *
 */

/**
 * \brief Construct a PidController with optional normal parameters
 * \param[in] Kp Proportional gain
 * \param[in] Ki Integral gain
 * \param[in] Kd Derivative gain
 * \param[in] min Minimum output value
 * \param[in] max Maximum output value
 *
 * For the parameters \see setNormalParameters() and \see setOutputLimits().
 */
PidController::PidController(double Kp, double Ki, double Kd, double min, double max)
{
	reset();
	setNormalParameters(Kp, Ki, Kd);
	setOutputLimits(min, max);
}

/**
 * \brief Set the normal parameters
 * \param[in] Kp Proportional gain
 * \param[in] Ki Integral gain
 * \param[in] Kd Derivative gain
 *
 * Set the normal parameters of the PID controller.
 */
void PidController::setNormalParameters(double Kp, double Ki, double Kd)
{
	Kp_ = Kp;
	Ki_ = Ki;
	Kd_ = Kd;
}

/**
 * \brief Set the standard parameters
 * \param[in] Kp Proportional gain
 * \param[in] Ti Integration time constant
 * \param[in] Td Derivative time constant
 *
 * Set the standard parameters of the PID controller. Functionally it is
 * identical to the normal parameters but has the added benefit that it is
 * easier to understand the values. The \a Ti parameter specifies the time the
 * controller will tolerate the output value to be away from the target. Td is
 * the time in which the controller tries to approach the target value.
 *
 * \see https://en.wikipedia.org/wiki/Proportional-integral-derivative_controller#Standard_form
 */
void PidController::setStandardParameters(double Kp, double Ti, double Td)
{
	Kp_ = Kp;
	Ki_ = Kp / Ti;
	Kd_ = Kp / Td;
}

/**
 * \brief Set the output limits
 * \param[in] min Minimum output value
 * \param[in] max Maximum output value
 *
 * Set the minimum and maximum output values of the controller. The controller
 * will clamp the output to these values and ensure that no windup of the
 * integral part occurs.
 */
void PidController::setOutputLimits(double min, double max)
{
	min_ = min;
	max_ = max;
}

/**
 * \brief Reset the controller
 *
 * Reset the internal state of the controller.
 */
void PidController::reset()
{
	last_error_ = 0;
	integral_ = 0;
	clamped_bottom_ = false;
	clamped_top_ = false;
}

/**
 * \brief Set the target value
 * \param[in] target Target value
 *
 * Set the target value the controller shall reach. In controller theory this is
 * usually called the set point.
 */
void PidController::setTarget(double target)
{
	target_ = target;
}

/**
 * \brief Run a regulation step
 * \param[in] value Measured value
 * \param[in] dt Time since last call
 * \return Output value
 *
 * Process the last measurement (also called process variable PV) and return the
 * new regulation value. The \a dt parameter specifies the time since the last
 * call. It defaults to 1.0, so in cases that are not time but frame based, it
 * can be left out.
 */
double PidController::process(double value, double dt)
{
	double error = target_ - value;
	double derivative = (error - last_error_) / dt;

	/* If we hit a limit disable the integrative part in that direction */
	if ((error * Ki_ > 0 && !clamped_top_) ||
	    (error * Ki_ < 0 && !clamped_bottom_))
		integral_ += error * dt;
	else
		LOG(PidController, Debug) << "Disable integrative part: "
					  << ", clamped_top_: " << clamped_top_
					  << ", clamped_bottom_: " << clamped_bottom_;
	double ret = Kp_ * error + Ki_ * integral_ + Kd_ * derivative;

	clamped_top_ = false;
	if (ret >= max_) {
		clamped_top_ = true;
		ret = max_;
	}

	clamped_bottom_ = false;
	if (ret <= min_) {
		clamped_bottom_ = true;
		ret = min_;
	}

	LOG(PidController, Debug) << "Value: " << value << ", Target: " << target_
				  << ", Error: " << error << ", Integral: " << integral_
				  << ", Derivative: " << derivative << ", Output: " << ret;

	last_error_ = error;

	return ret;
}

} /* namespace libcamera */
