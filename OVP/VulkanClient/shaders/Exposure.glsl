// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: auto-exposure measure (design-ground-graphics 6)

layout(binding = 1, row_major, scalar) uniform ExpoPS	// pixel shader constants (IPI.glsl's are binding 0)
{
vec4	vPrm;		// blocks x, blocks y, dark limit, saturation limit
};

layout(binding = 4) uniform sampler2D tSrc;	// the scaled scene, or the blocks

// one 16x9 block: sum of w*log2 L, sum of w, saturated count, pixel count
vec4 PSBlocks()
{
	ivec2 sz = textureSize(tSrc, 0);
	ivec2 b = ivec2(gl_FragCoord.xy);
	ivec2 nb = ivec2(vPrm.xy);
	ivec2 p0 = (b * sz) / nb, p1 = max(((b + 1) * sz) / nb, p0 + 1);
	vec4 acc = vec4(0.0);
	for (int y = p0.y; y < p1.y; y++)
		for (int x = p0.x; x < p1.x; x++) {
			vec3 c = texelFetch(tSrc, ivec2(x, y), 0).rgb;
			float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
			vec2 r = (vec2(x, y) + 0.5) / vec2(sz) * 2.0 - 1.0;
			float w = (L < vPrm.z) ? 0.0 : 1.0 - 0.5 * smoothstep(0.3, 1.0, length(r)); // centre bias 0.5-1
			acc.x += w * log2(max(L, vPrm.z));
			acc.y += w;
			acc.z += max(c.r, max(c.g, c.b)) >= vPrm.w ? 1.0 : 0.0;
			acc.w += 1.0;
		}
	return acc;
}

// the blocks to one texel: mean log2 L, weight fraction, saturated fraction
vec4 PSTotal()
{
	ivec2 sz = textureSize(tSrc, 0);
	vec4 acc = vec4(0.0);
	for (int y = 0; y < sz.y; y++)
		for (int x = 0; x < sz.x; x++) acc += texelFetch(tSrc, ivec2(x, y), 0);
	float n = max(acc.w, 1.0);
	return vec4(acc.y > 0.0 ? acc.x / acc.y : 0.0, acc.y / n, acc.z / n, 1.0);
}

#if defined(PS_PSBlocks) || defined(PS_PSTotal)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_PSBlocks
void main() { oColor = PSBlocks(); }
#endif
#ifdef PS_PSTotal
void main() { oColor = PSTotal(); }
#endif
