/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024-2025 Red Hat, inc.
 *
 * Global configuration handling
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

class GlobalConfiguration
{
public:
	using Configuration = const YamlObject &;

	GlobalConfiguration();

	unsigned int version() const;
	Configuration configuration() const;

#ifndef __DOXYGEN__
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
#else
	template<typename T>
#endif
	std::optional<T> option(const std::string &confPath) const
	{
		const YamlObject *c = &configuration();
		for (auto part : utils::split(confPath, ".")) {
			c = &(*c)[part];
			if (!*c)
				return {};
		}
		return c->get<T>();
	}

	std::vector<std::string> listOption(const std::string &confPath) const;
	std::optional<std::string> envOption(const char *const envVariable,
					     const std::string &confPath) const;
	std::vector<std::string> envListOption(
		const char *const envVariable,
		const std::string &confPath) const;

private:
	bool loadFile(const std::filesystem::path &fileName);
	void load();
	Configuration get() const;

	std::unique_ptr<YamlObject> yamlConfiguration;
};

} /* namespace libcamera */
