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

	static std::unique_ptr<YamlRoot> root(std::string_view path);

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

	std::unique_ptr<File> file_;
	yaml_event_t event_;
	Emitter emitter_;
};

class YamlOutput
{
public:
	virtual ~YamlOutput() {};

	YamlOutput(YamlOutput &&other)
	{
		emitter_ = other.emitter_;
		other.emitter_ = nullptr;
	}

	YamlScalar scalar();
	std::unique_ptr<YamlDict> dict();
	std::unique_ptr<YamlList> list();

protected:
	YamlOutput(YamlEmitter *emitter)
		: emitter_(emitter)
	{
	}

	int emitScalar(std::string_view scalar);
	int emitMappingStart();
	int emitMappingEnd();
	int emitSequenceStart();
	int emitSequenceEnd();

	YamlEmitter *emitter_;
	yaml_event_t event_;
};

class YamlRoot : public YamlOutput
{
public:
	~YamlRoot();

	std::unique_ptr<YamlList> list();
	std::unique_ptr<YamlDict> dict();
	void scalar(std::string_view scalar);

private:
	friend class YamlEmitter;

	YamlRoot(YamlEmitter *emitter)
		: YamlOutput(emitter)
	{
		emitterRoot_ = std::unique_ptr<YamlEmitter>(emitter);
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
	YamlList(YamlList &&other) = default;
	~YamlList();

	std::unique_ptr<YamlList> list();
	std::unique_ptr<YamlDict> dict();
	void scalar(std::string_view scalar);

private:
	friend class YamlOutput;

	YamlList(YamlEmitter *emitter);
};

class YamlDict : public YamlOutput
{
public:
	YamlDict(YamlDict &&other) = default;
	~YamlDict();

	std::unique_ptr<YamlList> list(std::string_view key);
	std::unique_ptr<YamlDict> dict(std::string_view key);

	YamlScalar operator[](std::string_view key);

private:
	friend class YamlOutput;

	YamlDict(YamlEmitter *emitter);
};

} /* namespace libcamera */
