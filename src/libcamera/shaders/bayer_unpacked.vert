/* SPDX-License-Identifier: BSD-2-Clause */
/*
From http://jgt.akpeters.com/papers/McGuire08/

Efficient, High-Quality Bayer Demosaic Filtering on GPUs

Morgan McGuire

This paper appears in issue Volume 13, Number 4.
---------------------------------------------------------
Copyright (c) 2008, Morgan McGuire. All rights reserved.

Modified by Linaro Ltd to integrate it into libcamera.
Copyright (C) 2021, Linaro
*/

//Vertex Shader

attribute vec4 vertexIn;
attribute vec2 textureIn;

uniform mat4 proj_matrix;

uniform vec2 tex_size;  /* The texture size in pixels */

/** Position of the pixel being sampled, in image pixels. */
varying vec2            pixelPos;

void main(void) {
    pixelPos = textureIn * tex_size;

    gl_Position = proj_matrix * vertexIn;
}
