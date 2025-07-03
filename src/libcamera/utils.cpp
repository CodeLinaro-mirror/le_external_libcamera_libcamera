/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * Miscellaneous utility functions (internal)
 */

#include "libcamera/internal/utils.h"

#include <algorithm>
#include <dirent.h>
#include <functional>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <vector>

/**
 * \file internal/utils.h
 * \brief Miscellaneous utility functions (internal)
 */

namespace libcamera {

namespace utils {

/**
 * \brief Identify shared library objects within a directory
 * \param[in] libDir The directory to search for shared objects
 * \param[in] maxDepth The maximum depth of sub-directories to parse
 * \param[out] files A vector of paths to shared object library files
 *
 * Search a directory for .so files, allowing recursion down to sub-directories
 * no further than the depth specified by \a maxDepth.
 *
 * Discovered shared objects are added to the \a files vector.
 */
void parseDir(const char *libDir, unsigned int maxDepth,
	      std::vector<std::string> &files)
{
	struct dirent *ent;
	DIR *dir;

	dir = opendir(libDir);
	if (!dir)
		return;

	while ((ent = readdir(dir)) != nullptr) {
		if (ent->d_type == DT_DIR && maxDepth) {
			if (strcmp(ent->d_name, ".") == 0 ||
			    strcmp(ent->d_name, "..") == 0)
				continue;

			std::string subdir = std::string(libDir) + "/" + ent->d_name;

			/* Recursion is limited to maxDepth. */
			parseDir(subdir.c_str(), maxDepth - 1, files);

			continue;
		}

		int offset = strlen(ent->d_name) - 3;
		if (offset < 0)
			continue;
		if (strcmp(&ent->d_name[offset], ".so"))
			continue;

		files.push_back(std::string(libDir) + "/" + ent->d_name);
	}

	closedir(dir);
}

/**
 * \brief Execute some function on shared object files from a directory
 * \param[in] libDir The directory to search for shared objects
 * \param[in] maxDepth The maximum depth of sub-directories to search
 * \param[in] func The function to execute on every shared object
 *
 * This function tries to execute the given function \a func for every shared
 * object found in \a libDir.
 *
 * Sub-directories are searched up to a depth of \a maxDepth. A \a maxDepth
 * value of 0 only searches the directory specified in \a libDir.
 *
 * \return Number of shared objects loaded by this call
 */
unsigned int addDir(const char *libDir, unsigned int maxDepth,
		    std::function<int(const std::string &)> func)
{
	std::vector<std::string> files;

	parseDir(libDir, maxDepth, files);

	/* Ensure a stable ordering of modules. */
	std::sort(files.begin(), files.end());

	unsigned int count = 0;
	for (const std::string &file : files)
		count += func(file);

	return count;
}

} /* namespace utils */

} /* namespace libcamera */
