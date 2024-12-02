/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * libcamera YAML emitter helper
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <libcamera/base/class.h>
#include <libcamera/base/file.h>

#include <yaml.h>

namespace libcamera {

class YamlDict;
class YamlEvent;
class YamlList;
class YamlRoot;
class YamlScalar;

class YamlEmitter final
{
public:
	~YamlEmitter();

	static YamlRoot root(const std::string &path);

private:
	friend class YamlOutput;
	friend class YamlRoot;

	LIBCAMERA_DISABLE_COPY(YamlEmitter)

	YamlEmitter(const std::string &path);

	void logError();
	void init();
	int emit();

	File file_;
	yaml_event_t event_;
	yaml_emitter_t emitter_;
};

class YamlOutput
{
public:
	bool valid() const { return !!emitter_; }

protected:
	YamlOutput() = default;
	YamlOutput(YamlOutput &&other);
	YamlOutput(YamlEmitter *emitter, YamlOutput *parent);

	virtual ~YamlOutput();

	YamlOutput &operator=(YamlOutput &&other);

	int emitScalar(std::string_view scalar);
	int emitMappingStart();
	int emitMappingEnd();
	int emitSequenceStart();
	int emitSequenceEnd();

	YamlDict dict(YamlOutput *parent);
	YamlList list(YamlOutput *parent);

	YamlEmitter *emitter_ = nullptr;
	yaml_event_t event_;

	YamlOutput *parent_ = nullptr;
	YamlOutput *child_ = nullptr;

private:
	LIBCAMERA_DISABLE_COPY(YamlOutput)

	void unlinkChildren(YamlOutput *node);
};

class YamlRoot : public YamlOutput
{
public:
	YamlRoot() = default;
	YamlRoot(YamlRoot &&other) = default;
	~YamlRoot();

	YamlRoot &operator=(YamlRoot &&other) = default;

	YamlList list();
	YamlDict dict();

private:
	LIBCAMERA_DISABLE_COPY(YamlRoot)

	friend class YamlEmitter;

	YamlRoot(std::unique_ptr<YamlEmitter> emitter)
		: YamlOutput(emitter.get(), nullptr), emitterRoot_(std::move(emitter))
	{
	}

	std::unique_ptr<YamlEmitter> emitterRoot_;
};

class YamlList : public YamlOutput
{
public:
	YamlList() = default;
	YamlList(YamlList &&other) = default;
	~YamlList();

	YamlList &operator=(YamlList &&other) = default;

	YamlList list();
	YamlDict dict();
	void scalar(std::string_view scalar);

private:
	friend class YamlOutput;

	YamlList(YamlEmitter *emitter, YamlOutput *parent);
};

class YamlDict : public YamlOutput
{
public:
	YamlDict() = default;
	YamlDict(YamlDict &&other) = default;
	~YamlDict();

	YamlDict &operator=(YamlDict &&other) = default;

	YamlList list(std::string_view key);
	YamlDict dict(std::string_view key);
	void scalar(std::string_view key, std::string_view scalar);

private:
	friend class YamlOutput;

	YamlDict(YamlEmitter *emitter, YamlOutput *parent);
};

} /* namespace libcamera */
