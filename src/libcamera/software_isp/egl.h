/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Linaro Ltd.
 *
 * Authors:
 * Bryan O'Donoghue <bryan.odonoghue@linaro.org>
 *
 * egl_context.cpp - Helper class for managing eGL interactions.
 */

#pragma once

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include <libcamera/base/log.h>

#include <unistd.h> // close

#include "gbm.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(eGL)

class eGLImage {
public:
	eGLImage(uint32_t width, uint32_t height, uint32_t bpp, GLenum texture_unit, uint32_t texture_unit_uniform_id) {
		image_ = EGL_NO_IMAGE_KHR;
		width_ = width;
		height_ = height;
		bpp_ = bpp;
		stride_ = width_ * bpp_ / 4;
		framesize_ = stride_ * height_;
		texture_unit_ = texture_unit;
		texture_unit_uniform_id_ = texture_unit_uniform_id;

		glGenTextures(1, &texture_);
	}

	~eGLImage() {
		glDeleteTextures(1, &texture_);
	};

	uint32_t width_;
	uint32_t height_;
	uint32_t stride_;
	uint32_t offset_;
	uint32_t framesize_;
	uint32_t bpp_;
	uint32_t texture_unit_uniform_id_;
	GLenum texture_unit_;
	GLuint texture_;
	EGLImageKHR image_;
};

class eGL
{
public:
	eGL();
	~eGL();

	int initEGLContext(GBM *gbmContext);
	int createDMABufTexture2D(eGLImage *eglImage, int fd);
	void destroyDMABufTexture(eGLImage *eglImage);
	void createTexture2D(eGLImage *eglImage, uint32_t width, uint32_t height, void *data);
	void createTexture1D(eGLImage *eglImage, uint32_t width, void *data);

	void pushEnv(std::vector<std::string> &shaderEnv, const char *str);
	void makeCurrent();
	void swapBuffers();

	int compileVertexShader(GLuint &shaderId, unsigned char *shaderData,
			  	unsigned int shaderDataLen,
			  	std::vector<std::string> shaderEnv);
	int compileFragmentShader(GLuint &shaderId, unsigned char *shaderData,
			  	unsigned int shaderDataLen,
			  	std::vector<std::string> shaderEnv);
	int linkProgram(GLuint &programIdd, GLuint fragmentshaderId, GLuint vertexshaderId);
	void dumpShaderSource(GLuint shaderId);
	void useProgram(GLuint programId);

private:
	int fd_;

	EGLDisplay display_;
	EGLContext context_;
	EGLSurface surface_;

	int compileShader(int shaderType, GLuint &shaderId, unsigned char *shaderData,
			  unsigned int shaderDataLen,
			  std::vector<std::string> shaderEnv);

	PFNEGLEXPORTDMABUFIMAGEMESAPROC eglExportDMABUFImageMESA;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

	PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
	PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;

	PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
	PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
protected:

};

};
