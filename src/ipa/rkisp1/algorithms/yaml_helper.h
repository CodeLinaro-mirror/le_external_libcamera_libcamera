/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board
 *
 * RkISP1 YAML parsing helper functions
 */

#pragma once

#include <map>
#include <optional>
#include <vector>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

namespace ipa::rkisp1::algorithms {

namespace yamlHelper {

inline void opt32(const YamlObject &level, const char *key, std::optional<uint32_t> &dst)
{
	dst = level[key].get<uint32_t>();
}

inline void opt16(const YamlObject &level, const char *key, std::optional<uint16_t> &dst)
{
	dst = level[key].get<uint16_t>();
}

inline void opt8(const YamlObject &level, const char *key, std::optional<uint8_t> &dst)
{
	dst = level[key].get<uint8_t>();
}

inline void optbool(const YamlObject &level, const char *key, std::optional<bool> &dst)
{
	dst = level[key].get<bool>();
}

inline void optbool(const YamlObject &level, const char *key, bool &dst, bool defaultVal)
{
	std::optional<bool> val = level[key].get<bool>();
	dst = val.value_or(defaultVal);
}

inline void opt8(const YamlObject &level, const char *key, uint8_t &dst, uint8_t defaultVal)
{
	std::optional<uint8_t> val = level[key].get<uint8_t>();
	dst = val.value_or(defaultVal);
}

inline void opt16(const YamlObject &level, const char *key, uint16_t &dst, uint16_t defaultVal)
{
	std::optional<uint16_t> val = level[key].get<uint16_t>();
	dst = val.value_or(defaultVal);
}

inline void opt32(const YamlObject &level, const char *key, uint32_t &dst, uint32_t defaultVal)
{
	std::optional<uint32_t> val = level[key].get<uint32_t>();
	dst = val.value_or(defaultVal);
}

inline bool optList8(const YamlObject &level, const char *key, std::vector<uint8_t> &dst, size_t expectedSize)
{
	std::optional<std::vector<uint8_t>> val = level[key].getList<uint8_t>();
	if (!val || val->size() != expectedSize)
		return false;
	dst = *val;
	return true;
}

inline bool optList16(const YamlObject &level, const char *key, std::vector<uint16_t> &dst, size_t expectedSize)
{
	std::optional<std::vector<uint16_t>> val = level[key].getList<uint16_t>();
	if (!val || val->size() != expectedSize)
		return false;
	dst = *val;
	return true;
}

template<typename EnumType>
inline bool optEnum(const YamlObject &level, const char *key, EnumType &dst, EnumType defaultVal,
		    const std::map<std::string, EnumType> &enumMap)
{
	std::optional<std::string> val = level[key].get<std::string>();
	if (!val) {
		dst = defaultVal;
		return true;
	}
	auto it = enumMap.find(*val);
	if (it == enumMap.end())
		return false;
	dst = it->second;
	return true;
}

inline bool optFilterCoeffs(const YamlObject &level, const char *key, uint8_t *coeffs, uint32_t &filterSize, size_t maxCoeffs)
{
	std::optional<std::vector<uint8_t>> val = level[key].getList<uint8_t>();
	if (!val)
		return false;

	size_t size = val->size();
	if (size == maxCoeffs - 1) {
		filterSize = 0; // 9x9 filter
	} else if (size == maxCoeffs) {
		filterSize = 1; // 13x9 filter
	} else {
		return false; // Invalid size
	}

	std::copy_n(val->begin(), size, coeffs);
	return true;
}

} /* namespace yamlHelper */

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
