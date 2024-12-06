/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, inc.
 *
 * Global configuration handling
 */

#pragma once

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

namespace GlobalConfiguration {

using Configuration = const YamlObject &;

void initialize();

unsigned int version();
Configuration configuration();

} /* namespace GlobalConfiguration */

} /* namespace libcamera */
