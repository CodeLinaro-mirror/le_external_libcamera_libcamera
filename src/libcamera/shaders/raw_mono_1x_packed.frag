/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2026, Alain Cousinié
 * raw_mono_1x_packed.frag - Fragment shader for packed monochrome formats (R10_CSI2P)
 */

#ifdef GL_ES
precision highp float;
#endif

#define BPP		1.25

varying vec2 textureOut;

uniform vec2 tex_size;
uniform sampler2D tex_y;
uniform float gamma;
uniform float contrastExp;

float apply_contrast(float value)
{
	if (value < 0.5)
		return 0.5 * pow(value / 0.5, contrastExp);
	else
		return 1.0 - 0.5 * pow((1.0 - value) / 0.5, contrastExp);
}

void main(void)
{
	vec2 pixel_coords = floor(textureOut * tex_size);

	vec2 center_pixel = pixel_coords + vec2(0.5);
	vec2 center_bytes;
	center_bytes.y = center_pixel.y;
	center_bytes.x = floor(BPP * center_pixel.x);

	center_bytes = center_bytes / tex_size;

	/* Linear sampling: GPU merges byte transitions. */
	float C = texture2D(tex_y, center_bytes).r;
	vec3 rgb = vec3(C, C, C);

	rgb = clamp(rgb, 0.0, 1.0);
	rgb.r = apply_contrast(rgb.r);
	rgb.g = apply_contrast(rgb.g);
	rgb.b = apply_contrast(rgb.b);
	rgb = pow(rgb, vec3(gamma));

	gl_FragColor = vec4(rgb, 1.0);
}
