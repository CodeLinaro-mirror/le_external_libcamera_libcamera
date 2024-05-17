# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (C) 2019, Raspberry Pi Ltd
# Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
#
# rkisp1.py - Ccm module for tuning rkisp1

from .ccm import CCM

import libtuning as lt
import libtuning.utils as utils

from numbers import Number
import numpy as np


class CCMRkISP1(CCM):
    hr_name = 'Crosstalk Correction (RkISP1)'
    out_name = 'Ccm'
    # \todo Not sure if this is useful. Probably will remove later.
    compatible = ['rkisp1']

    def __init__(self, **kwargs):
        super().__init__(**kwargs)

    # We don't actually need anything from the config file
    def validate_config(self, config: dict) -> bool:
        return True

    def _generate_ccms(self) -> dict:
        ccms = [
                {
                    'ct': 2860,
                    'ccm': [ 2.12089, -0.52461, -0.59629,
                            -0.85342,  2.80445, -0.95103,
                            -0.26897, -1.14788,  2.41685 ],
                    'offsets': [ 0, 0, 0 ]
                },

                {
                    'ct': 2960,
                    'ccm': [ 2.26962, -0.54174, -0.72789,
                            -0.77008,  2.60271, -0.83262,
                            -0.26036, -1.51254,  2.77289 ],
                    'offsets': [ 0, 0, 0 ]
                },

                {
                    'ct': 3603,
                    'ccm': [ 2.18644, -0.66148, -0.52496,
                            -0.77828,  2.69474, -0.91645,
                            -0.25239, -0.83059,  2.08298 ],
                    'offsets': [ 0, 0, 0 ]
                },

                {
                    'ct': 4650,
                    'ccm': [ 2.18174, -0.70887, -0.47287,
                            -0.70196,  2.76426, -1.06231,
                            -0.25157, -0.71978,  1.97135 ],
                    'offsets': [ 0, 0, 0 ]
                },

                {
                    'ct': 5858,
                    'ccm': [ 2.32392, -0.88421, -0.43971,
                            -0.63821,  2.58348, -0.94527,
                            -0.28541, -0.54112,  1.82653 ],
                    'offsets': [ 0, 0, 0 ]
                },

                {
                    'ct': 7580,
                    'ccm': [ 2.21175, -0.53242, -0.67933,
                            -0.57875,  3.07922, -1.50047,
                            -0.27709, -0.73338,  2.01048 ],
                    'offsets': [ 0, 0, 0 ]
                },
        ]

        return ccms

    def process(self, config: dict, images: list, outputs: dict) -> dict:
        output = {}

        output['ccms'] = self._generate_ccms()
        # \todo Debug functionality

        return output
