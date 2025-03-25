/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * PID Controller
 */
#pragma once

#include <limits>

#include <libcamera/base/log.h>

namespace libcamera {

LOG_DECLARE_CATEGORY(PidController)
class PidController
{
public:
	PidController(double Kp = 1.0, double Ki = 1e6, double Kd = 0.0,
		      double min = std::numeric_limits<double>::lowest(),
		      double max = std::numeric_limits<double>::max());

	void setNormalParameters(double Kp = 1.0, double Ki = 1e6, double Kd = 0.0);
	void setStandardParameters(double Kp = 1.0, double Ti = 1e6, double Td = 0.0);
	void setOutputLimits(double min = std::numeric_limits<double>::lowest(),
			     double max = std::numeric_limits<double>::max());
	void reset();

	void setTarget(double target);
	double process(double value, double dt = 1.0);

private:
	double Kp_;
	double Ki_;
	double Kd_;
	double max_;
	double min_;
	double target_;

	bool clamped_bottom_;
	bool clamped_top_;
	double integral_;
	double last_error_;
};

} /* namespace libcamera */
