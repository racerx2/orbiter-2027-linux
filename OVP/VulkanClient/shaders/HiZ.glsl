// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: what SSR traces (design-ssr 3): the max-of-1/z pyramid of SceneDepth (Uludag 2014) and the fog-free colour copy

layout(binding = 1, row_major, scalar) uniform HiZPS	// IPI.glsl's VS block is binding 0
{
vec4	vCamX;		// camera axes, global (xyz); tan of the half apertures (vCamX.w, vCamY.w); SceneFx::CameraRays
vec4	vCamY;
vec4	vCamZ;		// forward with the TAA jitter folded in: the ray of the jittered pixel
vec4	vFwd;		// the view's z axis
vec4	vSrc;		// screen W, H; source level of PSHiZDown
};

layout(binding = 4) uniform sampler2D tDepth;	// camera distance, 0 none
layout(binding = 5) uniform sampler2D tHiZ;		// the pyramid so far
layout(binding = 6) uniform sampler2D tSrc;		// PSUnfog: the scene copy
layout(binding = 7) uniform sampler2D tFog;		// PSUnfog: the fog over it, premultiplied (SceneFx::FogTex)

// level 0, padded: 1/z of the view, 0 for nothing drawn and outside the screen
vec4 PSHiZ0(float x, float y)
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	if (p.x >= int(vSrc.x) || p.y >= int(vSrc.y)) return vec4(0.0);
	float d = texelFetch(tDepth, p, 0).r;
	if (d <= 0.0) return vec4(0.0);
	vec2 uv = (vec2(p) + 0.5) / vSrc.xy;
	vec3 r = normalize(vCamX.xyz * ((uv.x * 2.0 - 1.0) * vCamX.w) + vCamY.xyz * ((1.0 - uv.y * 2.0) * vCamY.w) + vCamZ.xyz);
	float z = d * dot(r, vFwd.xyz);
	return vec4(z > 1e-3 ? 1.0 / z : 0.0);
}

// level k from k - 1: the max of each 2x2 (the padding makes every level an exact half)
vec4 PSHiZDown(float x, float y)
{
	ivec2 p = ivec2(gl_FragCoord.xy) * 2;
	int l = int(vSrc.z);
	float a = texelFetch(tHiZ, p, l).r, b = texelFetch(tHiZ, p + ivec2(1, 0), l).r;
	float c = texelFetch(tHiZ, p + ivec2(0, 1), l).r, e = texelFetch(tHiZ, p + ivec2(1, 1), l).r;
	return vec4(max(max(a, b), max(c, e)));
}

// the depth weight of the fog's own composite (HeightFog.glsl DepthW, written again here)
float UnfogW(float a, float b)
{
	if (a <= 0.0 && b <= 0.0) return 1.0;
	if (a <= 0.0 || b <= 0.0) return 0.02;
	return exp(-abs(a - b) / (0.02 * a + 1.0));
}

// the fog's over-blend undone, c' = c (1 - a) + f, with the composite's own depth-aware 2x2 weights; vSrc.zw = 1/size of the fog (ue5-integration INT-5)
vec4 PSUnfog(float x, float y)
{
	vec2 uv = vec2(x, y), tq = vSrc.zw, td = 1.0 / vec2(textureSize(tDepth, 0));
	float d = textureLod(tDepth, uv, 0.0).r;
	vec2 c0 = (floor(uv / tq - 0.5) + 0.5) * tq;
	vec4 s = vec4(0.0);
	float ws = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec2 q = c0 + vec2(i, j) * tq;
			vec2 f = 1.0 - abs(uv - q) / tq;
			float w = max(f.x * f.y, 1e-3) * UnfogW(d, textureLod(tDepth, (floor(q / td - 0.25) + 0.5) * td, 0.0).r);
			s += textureLod(tFog, q, 0.0) * w;
			ws += w;
		}
	vec4 v = s / ws;
	vec3 cf = textureLod(tSrc, uv, 0.0).rgb;
	if (v.a < 1e-3) return vec4(cf, 1.0); // the composite drew nothing here
	vec3 c = max(cf - v.rgb, vec3(0.0)) / max(1.0 - v.a, 0.05);
	return vec4(mix(c, cf, smoothstep(0.75, 0.95, v.a)), 1.0); // dense fog: the fogged colour, not a 20x gain on the TAA residual
}

#ifdef PS_PSHiZ0
layout(location = 0) in float iX;
layout(location = 1) in float iY;
layout(location = 0) out vec4 oColor;
void main() { oColor = PSHiZ0(iX, iY); }
#endif

#ifdef PS_PSHiZDown
layout(location = 0) in float iX;
layout(location = 1) in float iY;
layout(location = 0) out vec4 oColor;
void main() { oColor = PSHiZDown(iX, iY); }
#endif

#ifdef PS_PSUnfog
layout(location = 0) in float iX;
layout(location = 1) in float iY;
layout(location = 0) out vec4 oColor;
void main() { oColor = PSUnfog(iX, iY); }
#endif
