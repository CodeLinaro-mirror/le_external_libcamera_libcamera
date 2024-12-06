/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2024, Raspberry Pi Ltd
 *
 * Camera recovery algorithm
 */
#pragma once

#include <stdint.h>

namespace libcamera {

class ClockRecovery
{
public:
	ClockRecovery();

	/* Set configuration parameters. */
	void configure(unsigned int numPts = 100, unsigned int maxJitter = 2000, unsigned int minPts = 10,
		       unsigned int errorThreshold = 50000);
	/* Erase all history and restart the fitting process. */
	void reset();

	/*
	 * Add a new input clock / output clock sample, taking the input from the Linux
	 * CLOCK_BOOTTIME and the output from the CLOCK_REALTIME.
	 */
	void addSample();
	/*
	 * Add a new input clock / output clock sample, specifying the clock times exactly. Use this
	 * when you want to use clocks other than the ones described above.
	 */
	void addSample(uint64_t input, uint64_t output);
	/* Calculate the output clock value for this input. */
	uint64_t getOutput(uint64_t input);

private:
	unsigned int numPts_; /* how many samples contribute to the history */
	unsigned int maxJitter_; /* smooth out any jitter larger than this immediately */
	unsigned int minPts_; /* number of samples below which we treat clocks as 1:1 */
	unsigned int errorThreshold_; /* reset everything when the error exceeds this */

	unsigned int count_; /* how many samples seen (up to numPts_) */
	uint64_t inputBase_; /* subtract this from all input values, just to make the numbers easier */
	uint64_t outputBase_; /* as above, for the output */

	uint64_t lastInput_; /* the previous input sample */
	uint64_t lastOutput_; /* the previous output sample */

	/*
	 * We do a linear regression of y against x, where:
	 * x is the value input - inputBase_, and
	 * y is the value output - outputBase_ - x.
	 * We additionally subtract x from y so that y "should" be zero, again making the numnbers easier.
	 */
	double xAve_; /* average x value seen so far */
	double yAve_; /* average y value seen so far */
	double x2Ave_; /* average x^2 value seen so far */
	double xyAve_; /* average x*y value seen so far */

	/*
	 * Once we've seen more than minPts_ samples, we recalculate the slope and offset according
	 * to the linear regression normal equations.
	 */
	double slope_; /* latest slope value */
	double offset_; /* latest offset value */

	/* We use this cumulative error to monitor spontaneous system clock updates. */
	double error_;
};

} /* namespace libcamera */
