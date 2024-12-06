/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, inc.
 *
 * Global configuration handling
 */

#pragma once

#include <optional>
#include <string>

#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

namespace GlobalConfiguration {

using Configuration = const YamlObject &;

void initialize();

unsigned int version();
Configuration configuration();

template<typename T,
	 std::enable_if_t<
		 std::is_same_v<bool, T> ||
		 std::is_same_v<double, T> ||
		 std::is_same_v<int8_t, T> ||
		 std::is_same_v<uint8_t, T> ||
		 std::is_same_v<int16_t, T> ||
		 std::is_same_v<uint16_t, T> ||
		 std::is_same_v<int32_t, T> ||
		 std::is_same_v<uint32_t, T> ||
		 std::is_same_v<std::string, T> ||
		 std::is_same_v<Size, T>> * = nullptr>
std::optional<T> option(const std::string &confPath)
{
	const YamlObject *c = &configuration();
	for (auto part : utils::split(confPath, ".")) {
		c = &(*c)[part];
		if (!*c)
			return {};
	}
	return c->get<T>();
}

std::optional<std::string> envOption(const char *const envVariable,
				     const std::string &confPath);

} /* namespace GlobalConfiguration */

} /* namespace libcamera */
