/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2024 Ideas On Board Oy
 *
 * camera helper for imx258 sensor
 * based on Raspberry Pi's imx290 helper
 */

#include <cmath>

#include "cam_helper.h"

using namespace RPiController;

class CamHelperImx258 : public CamHelper
{
public:
	CamHelperImx258();
	uint32_t gainCode(double gain) const override;
	double gain(uint32_t gainCode) const override;
	void getDelays(int &exposureDelay, int &gainDelay,
		       int &vblankDelay, int &hblankDelay) const override;
private:
	/*
	 * Smallest difference between the frame length and integration time,
	 * in units of lines.
	 */
	static constexpr int frameIntegrationDiff = 2;
};

CamHelperImx258::CamHelperImx258()
	: CamHelper({}, frameIntegrationDiff)
{
}

uint32_t CamHelperImx258::gainCode(double gain) const
{
	return static_cast<uint32_t>(512 - 512 / gain);
}

double CamHelperImx258::gain(uint32_t gainCode) const
{
	return 512.0 / (512.0 - gainCode);
}

void CamHelperImx258::getDelays(int &exposureDelay, int &gainDelay,
				int &vblankDelay, int &hblankDelay) const
{
	exposureDelay = 2;
	gainDelay = 2;
	vblankDelay = 2;
	hblankDelay = 2;
}

static CamHelper *create()
{
	return new CamHelperImx258();
}

static RegisterCamHelper reg("imx258", &create);
