/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * libcamera YAML emitter helper
 */

#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>

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
	~YamlEmitter() = default;

	static std::unique_ptr<YamlRoot> root(std::string_view path);

	int emit(YamlEvent *event);

private:
	LIBCAMERA_DISABLE_COPY(YamlEmitter)

	class Emitter
	{
	public:
		Emitter() = default;
		~Emitter();

		void init(File *file);

		int emit(YamlEvent *event);

	private:
		void logError();

		yaml_emitter_t emitter_;
	};

	YamlEmitter() = default;

	void init();

	std::unique_ptr<File> file_;
	Emitter emitter_;
};

class YamlOutput
{
public:
	virtual ~YamlOutput() = default;

	void close()
	{
		closed_ = true;
	}

	std::unique_ptr<YamlScalar> scalar();
	std::unique_ptr<YamlDict> dict();
	std::unique_ptr<YamlList> list();

protected:
	YamlOutput(YamlEmitter *emitter)
		: emitter_(emitter), closed_(false)
	{
	}

	int emitScalar(std::string_view scalar);
	int emitMappingStart();
	int emitMappingEnd();
	int emitSequenceStart();
	int emitSequenceEnd();

	YamlEmitter *emitter_;

	bool closed_;
};

class YamlRoot : public YamlOutput
{
public:
	~YamlRoot();
	void close() {}

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

	void close() {}

	void operator=(const Orientation &orientation);
	void operator=(std::string_view scalar);

private:
	friend class YamlOutput;

	YamlScalar(YamlEmitter *emitter);
};

class YamlList : public YamlOutput
{
public:
	~YamlList();

	void close();

	std::unique_ptr<YamlList> list();
	std::unique_ptr<YamlDict> dict();
	void scalar(std::string_view scalar);

private:
	friend class YamlOutput;

	YamlList(YamlEmitter *emitter);
};

class YamlDict : public YamlOutput,
		 private std::unordered_map<std::string_view,
					    std::unique_ptr<YamlScalar>>
{
public:
	using Map = std::unordered_map<std::string_view, YamlScalar>;

	~YamlDict();

	void close();

	std::unique_ptr<YamlList> list(std::string_view key);
	std::unique_ptr<YamlDict> dict(std::string_view key);

	YamlScalar &operator[](const Map::key_type &key);

private:
	friend class YamlOutput;

	YamlDict(YamlEmitter *emitter);
};

} /* namespace libcamera */
