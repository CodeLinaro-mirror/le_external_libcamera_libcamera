/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RkISP2 control algorithm interface
 */

#pragma once

#include <linux/rkisp2-config.h>

#include <libipa/algorithm.h>

#include "module.h"

namespace libcamera {

namespace ipa::rkisp2 {

class Algorithm : public libcamera::ipa::Algorithm<Module>
{
};

union rkisp2_params_block {
	const struct v4l2_isp_params_block_header *header;
	const struct rkisp2_params_bls *bls;
	const struct rkisp2_params_awb_gains *awb_gains;
	const struct rkisp2_params_csm *csm;
	const struct rkisp2_params_ccm *ccm;
	const struct rkisp2_params_goc *goc;
	const struct rkisp2_params_lsc *lsc;
	const struct rkisp2_params_ae_lite *ae_lite;
	const struct rkisp2_params_hist_lite *hist_lite;
	const struct rkisp2_params_hist_big *hist_big;
	const struct rkisp2_params_awb_meas *awb_meas;
	const __u8 *data;
};

} /* namespace ipa::rkisp2 */

} /* namespace libcamera */

