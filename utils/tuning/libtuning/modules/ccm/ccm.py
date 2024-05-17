# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (C) 2019, Raspberry Pi Ltd
# Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>

from ..module import Module

import libtuning as lt
import libtuning.utils as utils

import numpy as np

class CCM(Module):
    type = 'ccm'
    hr_name = 'CCM (Base)'
    out_name = 'GenericCCM'

    def __init__(self, *,
                 debug: list):
        super().__init__()

        self.debug = debug
