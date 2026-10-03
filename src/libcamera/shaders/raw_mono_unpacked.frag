/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2026, Alain Cousinié
 * raw_mono_unpacked.frag - Fragment shader for raw unpacked monochrome formats (R10)
 */

#extension GL_EXT_texture_rg : enable

#ifdef GL_ES
precision highp float;
#endif

varying vec2 textureOut;

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
	/* GL_RG format sampling (16 bits):
	 * The .r component contains the 8 MSBs,
	 * The .g component contains the remaining LSBs.
	 */
	vec4 texel = texture2D(tex_y, textureOut);
	float C = texel.r + texel.g / 256.0;

	vec3 rgb = vec3(C, C, C);

	/* Generic rendering processing */
	rgb = clamp(rgb, 0.0, 1.0);
	rgb.r = apply_contrast(rgb.r);
	rgb.g = apply_contrast(rgb.g);
	rgb.b = apply_contrast(rgb.b);
	rgb = pow(rgb, vec3(gamma));

	gl_FragColor = vec4(rgb, 1.0);
}

