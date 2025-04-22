/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Bryan O'Donoghue.
 *
 * Authors:
 * Bryan O'Donoghue <bryan.odonoghue@linaro.org>
 *
 * debayer_opengl.h - EGL debayer header
 */

#pragma once

#include <memory>
#include <stdint.h>
#include <vector>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include <libcamera/base/object.h>

#include "debayer.h"
#include "egl.h"

#include "libcamera/internal/bayer_format.h"
#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/mapped_framebuffer.h"
#include "libcamera/internal/software_isp/benchmark.h"
#include "libcamera/internal/software_isp/swstats_cpu.h"

namespace libcamera {

/**
 * \class DebayerEGL
 * \brief Class for debayering using an EGL Shader
 *
 * Implements an EGL shader based debayering solution.
 */
class DebayerEGL : public Debayer
{
public:
	/**
	 * \brief Constructs a DebayerEGL object.
	 * \param[in] stats Pointer to the stats object to use.
	 */
	DebayerEGL(std::unique_ptr<SwStatsCpu> stats);
	~DebayerEGL();

	/*
	 * Setup the Debayer object according to the passed in parameters.
	 * Return 0 on success, a negative errno value on failure
	 * (unsupported parameters).
	 */
	int configure(const StreamConfiguration &inputCfg,
		      const std::vector<std::reference_wrapper<StreamConfiguration>> &outputCfgs,
		      bool ccmEnabled);

	/*
	 * Get width and height at which the bayer-pattern repeats.
	 * Return pattern-size or an empty Size for an unsupported inputFormat.
	 */
	Size patternSize(PixelFormat inputFormat);

	std::vector<PixelFormat> formats(PixelFormat input);
	std::tuple<unsigned int, unsigned int>
		strideAndFrameSize(const PixelFormat &outputFormat, const Size &size);

	void process(uint32_t frame, FrameBuffer *input, FrameBuffer *output, DebayerParams params);

	/**
	 * \brief Get the file descriptor for the statistics.
	 *
	 * \return the file descriptor pointing to the statistics.
	 */
	const SharedFD &getStatsFD() { return stats_->getStatsFD(); }

	/**
	 * \brief Get the output frame size.
	 *
	 * \return The output frame size.
	 */
	unsigned int frameSize() { return outputConfig_.frameSize; }

	SizeRange sizes(PixelFormat inputFormat, const Size &inputSize);

	/**
	 * \brief EGL context is associated with a PID so don't moveToThread
	 *
	 * \return nothing
	 */
	void moveToThread(Thread *thread) { (void)thread; return; }

private:

	int getInputConfig(PixelFormat inputFormat, DebayerInputConfig &config);
	int getOutputConfig(PixelFormat outputFormat, DebayerOutputConfig &config);
	int setupStandardBayerOrder(BayerFormat::Order order);
	void pushEnv(std::vector<std::string> &shaderEnv, const char *str);
	int initBayerShaders(PixelFormat inputFormat, PixelFormat outputFormat);
	int initEGLContext();
	int generateTextures();
	int compileShaderProgram(GLuint &shaderId, GLenum shaderType,
				 unsigned char *shaderData, int shaderDataLen,
				 std::vector<std::string> shaderEnv);
	int linkShaderProgram(void);
	int getShaderVariableLocations();
	void setShaderVariableValues(void);
	void configureTexture(GLuint &texture);
	void debayerGPU(MappedFrameBuffer &in, MappedFrameBuffer &out, DebayerParams &params);

	// Shader program identifiers
	GLuint vertexShaderId_;
	GLuint fragmentShaderId_;
	GLuint programId_;
	enum {
		BAYER_INPUT_INDEX = 0,
		BAYER_OUTPUT_INDEX,
		BAYER_BUF_NUM,
	};

	// Pointer to object representing input texture
	eGLImage *eglImageBayerIn_;

	eGLImage *eglImageRedLookup_;
	eGLImage *eglImageGreenLookup_;
	eGLImage *eglImageBlueLookup_;

	// Shader parameters
	float firstRed_x_;
	float firstRed_y_;
	GLint attributeVertex_;
	GLint attributeTexture_;
	GLint textureUniformStep_;
	GLint textureUniformSize_;
	GLint textureUniformStrideFactor_;
	GLint textureUniformBayerFirstRed_;
	GLint textureUniformProjMatrix_;

	#define DEBAYER_EGL_MIN_TEXTURE_UNITS 4
	GLint textureUniformBayerDataIn_;
	GLint textureUniformRedLookupDataIn_;
	GLint textureUniformGreenLookupDataIn_;
	GLint textureUniformBlueLookupDataIn_;

	Rectangle window_;
	std::unique_ptr<SwStatsCpu> stats_;
	eGL egl_;
	GBM gbmSurface_;
	uint32_t width_;
	uint32_t height_;

	#define DEBAYER_OPENGL_COORDS 4

	GLfloat vcoordinates[DEBAYER_OPENGL_COORDS][2] = {
	/* Vertex coordinates */
		-1.0f, -1.0f,
		-1.0f, +1.0f,
		+1.0f, +1.0f,
		+1.0f, -1.0f,
	};

	GLfloat tcoordinates[DEBAYER_OPENGL_COORDS][2] = {
		/* Texture coordinates */
		0.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 0.0f,
		1.0f, 1.0f,
	};
};

} /* namespace libcamera */
