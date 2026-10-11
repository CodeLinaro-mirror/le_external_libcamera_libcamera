/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2026, Alain Cousinié
 * Lightweight vertex shader for unpacked monochrome formats
 */

attribute vec4 vertexIn;
attribute vec2 textureIn;

uniform mat4 proj_matrix;
uniform float stride_factor;

varying vec4            center; /* Restored to original vec4 */

void main(void) {
    center.xy = vec2(textureIn.x * stride_factor, textureIn.y);
    gl_Position = proj_matrix * vertexIn;
}
