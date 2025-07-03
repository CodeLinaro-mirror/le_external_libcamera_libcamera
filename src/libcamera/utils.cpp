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
#include <link.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <vector>

#include <libcamera/base/log.h>

/**
 * \file internal/utils.h
 * \brief Miscellaneous utility functions (internal)
 */

/* musl doesn't declare _DYNAMIC in link.h, declare it manually. */
extern ElfW(Dyn) _DYNAMIC[];

namespace libcamera {

namespace {

template<typename T>
typename std::remove_extent_t<T> *elfPointer(Span<const uint8_t> elf,
					     off_t offset, size_t objSize)
{
	size_t size = offset + objSize;
	if (size > elf.size() || size < objSize)
		return nullptr;

	return reinterpret_cast<typename std::remove_extent_t<T> *>(
		reinterpret_cast<const char *>(elf.data()) + offset);
}

template<typename T>
typename std::remove_extent_t<T> *elfPointer(Span<const uint8_t> elf,
					     off_t offset)
{
	return elfPointer<T>(elf, offset, sizeof(T));
}

const ElfW(Shdr) *elfSection(Span<const uint8_t> elf, const ElfW(Ehdr) *eHdr,
			     ElfW(Half) idx)
{
	if (idx >= eHdr->e_shnum)
		return nullptr;

	off_t offset = eHdr->e_shoff + idx *
				       static_cast<uint32_t>(eHdr->e_shentsize);
	return elfPointer<const ElfW(Shdr)>(elf, offset);
}

} /* namespace */

namespace utils {

LOG_DEFINE_CATEGORY(Utils)

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

int elfVerifyIdent(Span<const uint8_t> elf)
{
	const char *e_ident = elfPointer<const char[EI_NIDENT]>(elf, 0);
	if (!e_ident)
		return -ENOEXEC;

	if (e_ident[EI_MAG0] != ELFMAG0 ||
	    e_ident[EI_MAG1] != ELFMAG1 ||
	    e_ident[EI_MAG2] != ELFMAG2 ||
	    e_ident[EI_MAG3] != ELFMAG3 ||
	    e_ident[EI_VERSION] != EV_CURRENT)
		return -ENOEXEC;

	int bitClass = sizeof(unsigned long) == 4 ? ELFCLASS32 : ELFCLASS64;
	if (e_ident[EI_CLASS] != bitClass)
		return -ENOEXEC;

	int a = 1;
	unsigned char endianness = *reinterpret_cast<char *>(&a) == 1
				 ? ELFDATA2LSB : ELFDATA2MSB;
	if (e_ident[EI_DATA] != endianness)
		return -ENOEXEC;

	return 0;
}

/**
 * \brief Retrieve address and size of a symbol from an mmap'ed ELF file
 * \param[in] elf Address and size of mmap'ed ELF file
 * \param[in] symbol Symbol name
 *
 * \return The memory region storing the symbol on success, or an empty span
 * otherwise
 */
Span<const uint8_t> elfLoadSymbol(Span<const uint8_t> elf, const char *symbol)
{
	const ElfW(Ehdr) *eHdr = elfPointer<const ElfW(Ehdr)>(elf, 0);
	if (!eHdr)
		return {};

	const ElfW(Shdr) *sHdr = elfSection(elf, eHdr, eHdr->e_shstrndx);
	if (!sHdr)
		return {};
	off_t shnameoff = sHdr->sh_offset;

	/* Locate .dynsym section header. */
	const ElfW(Shdr) *dynsym = nullptr;
	for (unsigned int i = 0; i < eHdr->e_shnum; i++) {
		sHdr = elfSection(elf, eHdr, i);
		if (!sHdr)
			return {};

		off_t offset = shnameoff + sHdr->sh_name;
		const char *name = elfPointer<const char[8]>(elf, offset);
		if (!name)
			return {};

		if (sHdr->sh_type == SHT_DYNSYM && !strcmp(name, ".dynsym")) {
			dynsym = sHdr;
			break;
		}
	}

	if (dynsym == nullptr) {
		LOG(Utils, Error) << "ELF has no .dynsym section";
		return {};
	}

	sHdr = elfSection(elf, eHdr, dynsym->sh_link);
	if (!sHdr)
		return {};
	off_t dynsym_nameoff = sHdr->sh_offset;

	/* Locate symbol in the .dynsym section. */
	const ElfW(Sym) *targetSymbol = nullptr;
	unsigned int dynsym_num = dynsym->sh_size / dynsym->sh_entsize;
	for (unsigned int i = 0; i < dynsym_num; i++) {
		off_t offset = dynsym->sh_offset + dynsym->sh_entsize * i;
		const ElfW(Sym) *sym = elfPointer<const ElfW(Sym)>(elf, offset);
		if (!sym)
			return {};

		offset = dynsym_nameoff + sym->st_name;
		const char *name = elfPointer<const char>(elf, offset,
							  strlen(symbol) + 1);
		if (!name)
			return {};

		if (!strcmp(name, symbol) &&
		    sym->st_info & STB_GLOBAL) {
			targetSymbol = sym;
			break;
		}
	}

	if (targetSymbol == nullptr) {
		LOG(Utils, Error) << "Symbol " << symbol << " not found";
		return {};
	}

	/* Locate and return data of symbol. */
	sHdr = elfSection(elf, eHdr, targetSymbol->st_shndx);
	if (!sHdr)
		return {};
	off_t offset = sHdr->sh_offset + (targetSymbol->st_value - sHdr->sh_addr);
	const uint8_t *data = elfPointer<const uint8_t>(elf, offset,
							targetSymbol->st_size);
	if (!data)
		return {};

	return { data, targetSymbol->st_size };
}


} /* namespace utils */

} /* namespace libcamera */
