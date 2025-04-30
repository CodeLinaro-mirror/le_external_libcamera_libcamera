/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, inc.
 *
 * Global configuration handling
 */

#pragma once

#include <optional>
#include <string>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

namespace GlobalConfiguration {

using Configuration = const YamlObject &;

void initialize();

unsigned int version();
Configuration configuration();
std::optional<std::string> option(const std::string &confPath);
std::optional<std::string> envOption(const char *const envVariable,
				     const std::string &confPath);

} /* namespace GlobalConfiguration */

} /* namespace libcamera */
