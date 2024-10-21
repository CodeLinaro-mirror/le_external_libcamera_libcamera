/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * libcamera YAML emitter helper
 */

#pragma once

#include <memory>
#include <string_view>

#include <libcamera/base/class.h>
#include <libcamera/base/file.h>
#include <libcamera/orientation.h>

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

	static YamlRoot root(std::string_view path);

	int emit();
	yaml_event_t *event() { return &event_; }

private:
	LIBCAMERA_DISABLE_COPY(YamlEmitter)

	class Emitter
	{
	public:
		~Emitter();

		void init(File *file);

		int emit(yaml_event_t *event);

	private:
		void logError();

		yaml_emitter_t emitter_;
	};

	YamlEmitter() = default;

	void init();

	File file_;
	yaml_event_t event_;
	Emitter emitter_;
};

class YamlOutput
{
public:
	virtual ~YamlOutput() {};

	YamlOutput() = default;

	YamlOutput(YamlOutput &&other)
	{
		emitter_ = other.emitter_;
		other.emitter_ = nullptr;
	}

	bool initialized() { return !!emitter_; }

	YamlScalar scalar();
	YamlDict dict();
	YamlList list();

protected:
	YamlOutput(YamlEmitter *emitter)
		: emitter_(emitter)
	{
	}

	YamlOutput &operator=(YamlOutput &&other)
	{
		emitter_ = other.emitter_;
		other.emitter_ = nullptr;

		return *this;
	}

	int emitScalar(std::string_view scalar);
	int emitMappingStart();
	int emitMappingEnd();
	int emitSequenceStart();
	int emitSequenceEnd();

	YamlEmitter *emitter_ = nullptr;
	yaml_event_t event_;
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
	void scalar(std::string_view scalar);

private:
	friend class YamlEmitter;

	YamlRoot(std::unique_ptr<YamlEmitter> emitter)
		: YamlOutput(emitter.get()), emitterRoot_(std::move(emitter))
	{
	}

	std::unique_ptr<YamlEmitter> emitterRoot_;
};

class YamlScalar : public YamlOutput
{
public:
	~YamlScalar() = default;

	void operator=(std::string_view scalar);

private:
	friend class YamlOutput;

	YamlScalar(YamlEmitter *emitter);
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

	YamlList(YamlEmitter *emitter);
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

	YamlScalar operator[](std::string_view key);

private:
	friend class YamlOutput;

	YamlDict(YamlEmitter *emitter);
};

} /* namespace libcamera */
