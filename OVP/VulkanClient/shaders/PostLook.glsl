// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: heat haze, light shafts, lens flare, sharpening and vignette (design-visual-batch 4-7)
// shafts after Mitchell 2007 (radial blur toward the sun), sharpening after the published contrast-adaptive idea

layout(binding = 1, row_major, scalar) uniform LookPS	// uniform extern: pixel shader constants (IPI.glsl's are binding 0)
{
vec4	vTex;		// 1/width, 1/height, width/height, time s
vec4	vSun;		// sun uv; shaft strength; flare strength (CPU visibility in)
vec4	vSunCol;	// sun colour, max 1
vec4	vFlags;		// valid: camera distance, vessel depth, volume clouds, sun visibility texel
vec4	vSharp;		// sharpening amount
vec4	vRect;		// haze: drawn rect, uv x, y, w, h
vec4	vHaze;		// haze: segments, time s, camera distance valid
vec4	vSeg[96];	// haze: per segment p0 p1 (px); half-width at p0, p1 (px), strength, nozzle distance (m); t0, t1 of the plume
};

layout(binding = 4) uniform sampler2D tScene;	// copy of the target
layout(binding = 5) uniform sampler2D tDepthL;	// SceneFx camera distance, 0 none
layout(binding = 6) uniform sampler2D tGbuf;	// vessel and cockpit normal and distance, .a 0 none
layout(binding = 7) uniform sampler2D tShaft;	// half resolution shaft mask or rays
layout(binding = 8) uniform sampler2D tVis;		// sun and light visibility, texel 0 the sun
layout(binding = 9) uniform sampler2D tVolL;	// volume clouds, premultiplied

float Sq (float x) { return x * x; }

// integer hash: Teschner et al. 2003 primes, Murmur3 finaliser
float Hash (ivec2 p)
{
	uint h = uint(p.x) * 73856093u ^ uint(p.y) * 19349663u;
	h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
	return float(h) * (1.0 / 4294967296.0);
}

float ValueNoise (vec2 p)
{
	vec2 i = floor(p), f = p - i;
	vec2 u = f * f * (3.0 - 2.0 * f);
	ivec2 k = ivec2(i);
	float a = Hash(k), b = Hash(k + ivec2(1, 0)), c = Hash(k + ivec2(0, 1)), d = Hash(k + ivec2(1, 1));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float IGNoise (vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

// ---------------------------------------------------------------------------------------------
// heat haze: the strongest exhaust segment bends the scene behind it

vec4 PSHaze (float x, float y)
{
	vec2 uv = vRect.xy + vec2(x, y) * vRect.zw;
	vec2 px = uv / vTex.xy;
	float best = 0.0, dn = 0.0;
	vec2 q = vec2(0.0);
	int n = int(vHaze.x);
	for (int i = 0; i < 32; i++) {
		if (i >= n) break;
		vec4 A = vSeg[3 * i], B = vSeg[3 * i + 1], C = vSeg[3 * i + 2];
		vec2 e = A.zw - A.xy, r = px - A.xy;
		float ll = dot(e, e);
		float ts = ll > 1e-6 ? clamp(dot(r, e) / ll, 0.0, 1.0) : 0.0;
		float w = mix(B.x, B.y, ts);
		float d = length(r - e * ts);
		if (d >= w) continue;
		float t = mix(C.x, C.y, ts);
		float s = B.z * Sq(1.0 - d / w) * sqrt(max(0.0, 1.0 - t));
		if (s <= best) continue;
		best = s;
		dn = B.w;
		vec2 ax = ll > 1e-6 ? e * inversesqrt(ll) : vec2(1.0, 0.0);
		q = vec2(dot(r, ax), dot(r, vec2(-ax.y, ax.x))) / max(w, 1.0); // along and across, in half-widths
	}
	vec3 c0 = texture(tScene, uv).rgb;
	if (best <= 0.0) return vec4(c0, 1.0);
	float margin = 0.02 * dn + 1.0;
	if (vHaze.z > 0.5) {
		float dp = textureLod(tDepthL, uv, 0.0).r;
		if (dp > 0.0 && dp < dn - margin) return vec4(c0, 1.0); // in front of the plume
	}
	vec2 nq = vec2(q.x * 1.2 - vHaze.y * 3.0, q.y * 2.5); // scrolled along the plume
	vec2 o = vec2(ValueNoise(nq), ValueNoise(nq + vec2(17.31, 41.73))) - 0.5;
	vec2 uv2 = clamp(uv + best * 0.006 * o * vec2(vTex.x / vTex.y, 1.0), vec2(0.0), vec2(1.0)); // screen heights to uv: x times H/W
	if (vHaze.z > 0.5) {
		float d2 = textureLod(tDepthL, uv2, 0.0).r;
		if (d2 > 0.0 && d2 < dn - margin) uv2 = uv; // nearer than the nozzle: not pulled into the plume
	}
	return vec4(textureLod(tScene, uv2, 0.0).rgb, 1.0);
}

// ---------------------------------------------------------------------------------------------
// light shafts: bright sky near the sun, blurred toward it (half resolution)

vec4 PSShaftMask (float x, float y)
{
	vec2 uv = vec2(x, y);
	vec3 c = texture(tScene, uv).rgb;
	float m = smoothstep(0.6, 0.95, dot(c, vec3(0.2126, 0.7152, 0.0722)));
	vec2 uq = uv - 0.25 * vTex.xy; // a full resolution texel
	if (vFlags.x > 0.5 && texture(tDepthL, uq).r > 0.0) m = 0.0;
	if (vFlags.y > 0.5 && texture(tGbuf, uq).a > 0.0) m = 0.0;
	m *= Sq(1.0 - smoothstep(0.0, 0.3, length((uv - vSun.xy) * vec2(vTex.z, 1.0)))); // sky near the sun
	return vec4(m, m, m, 1.0);
}

vec4 PSShaftBlur (float x, float y)
{
	vec2 uv = vec2(x, y);
	vec2 dv = vSun.xy - uv;
	float len = length(dv * vec2(vTex.z, 1.0));
	vec2 st = dv * (min(0.9, 1.0 / max(len, 1e-4)) / 48.0); // 90 % of the way, at most a screen height
	vec2 p = uv + st * IGNoise(gl_FragCoord.xy);
	float s = 0.0, ws = 0.0, wk = 1.0;
	for (int i = 0; i < 48; i++) {
		bool on = p.x >= 0.0 && p.y >= 0.0 && p.x <= 1.0 && p.y <= 1.0;
		s += on ? textureLod(tShaft, p, 0.0).r * wk : 0.0;
		ws += wk;
		wk *= 0.965;
		p += st;
	}
	float r = s / ws;
	return vec4(r, r, r, 1.0);
}

// ---------------------------------------------------------------------------------------------
// lens flare: ghosts on the line through the sun and the centre, and a halo

float SunVisGPU ()
{
	float v = 1.0;
	if (vFlags.w > 0.5) v *= smoothstep(0.5, 1.0, textureLod(tVis, vec2(0.5 / 32.0, 0.5), 0.0).r); // vessels and cockpit, as GlareVS
	if (vFlags.x > 0.5) { // buildings, trees and terrain in the camera distance
		const vec2 o[5] = vec2[5](vec2(0.0), vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
		float k = 0.0;
		for (int i = 0; i < 5; i++) k += texture(tDepthL, clamp(vSun.xy + o[i] * vec2(0.006 / vTex.z, 0.006), vec2(0.0), vec2(1.0))).r > 0.0 ? 0.0 : 1.0;
		v *= k * 0.2;
	}
	if (vFlags.z > 0.5) v *= 1.0 - clamp(texture(tVolL, clamp(vSun.xy, vec2(0.0), vec2(1.0))).a, 0.0, 1.0);
	return v;
}

vec3 Flare (vec2 uv)
{
	const float T[6] = float[6](-0.6, -0.3, 0.2, 0.45, 0.8, 1.1);	// t = 0 the centre, 1 the sun's mirror
	const float R[6] = float[6](0.05, 0.02, 0.07, 0.035, 0.09, 0.045);	// screen heights
	const float B[6] = float[6](0.35, 0.5, 0.25, 0.4, 0.2, 0.3);
	const vec3 K[6] = vec3[6](vec3(1.0, 0.8, 0.5), vec3(0.6, 1.0, 0.7), vec3(0.5, 0.7, 1.0), vec3(1.0, 0.6, 0.8), vec3(0.6, 0.8, 1.0), vec3(0.9, 1.0, 0.6));
	vec2 a = vec2(vTex.z, 1.0);
	vec2 q = (uv - 0.5) * a, s = (vSun.xy - 0.5) * a;
	vec3 f = vec3(0.0);
	for (int k = 0; k < 6; k++) {
		vec2 g = -T[k] * s;
		vec3 d = vec3(length(q - g * 1.015), length(q - g), length(q - g * 0.985)); // colour fringes
		vec3 disc = 1.0 - smoothstep(vec3(R[k] * 0.7), vec3(R[k]), d);
		f += disc * (0.7 + 0.3 * d / R[k]) * K[k] * B[k];
	}
	float ds = length(s), lq = length(q);
	float hw = smoothstep(0.1, 0.3, ds) * (1.0 - smoothstep(0.6, 0.9, ds));
	if (hw > 0.0) {
		vec3 dr = (vec3(lq) - 0.42 * vec3(1.015, 1.0, 0.985)) / 0.02; // radius 0.42, fringed
		vec3 ring = exp(-dr * dr);
		float side = 0.35 + 0.65 * max(0.0, dot(q, s) / max(lq * ds, 1e-5)); // brighter on the sun's side
		f += ring * (hw * side * 0.3);
	}
	return f * vSunCol.rgb;
}

// ---------------------------------------------------------------------------------------------
// the final pass: shafts, flare, sharpening, vignette

vec4 PSFinal (float x, float y)
{
	vec2 uv = vec2(x, y);
	vec3 c = texture(tScene, uv).rgb;
#ifdef _SHARPEN
	{	// before the smooth additions: the same as after them, without their slopes in the weight
		vec3 a = texture(tScene, uv - vec2(0.0, vTex.y)).rgb, b = texture(tScene, uv - vec2(vTex.x, 0.0)).rgb;
		vec3 d = texture(tScene, uv + vec2(vTex.x, 0.0)).rgb, e = texture(tScene, uv + vec2(0.0, vTex.y)).rgb;
		vec3 mn = min(c, min(min(a, b), min(d, e))), mx = max(c, max(max(a, b), max(d, e)));
		vec3 w = -vSharp.x * sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));
		c = max((c + w * (a + b + d + e)) / (1.0 + 4.0 * w), vec3(0.0));
	}
#endif
#ifdef _SHAFTS
	if (vSun.z > 0.0) c += texture(tShaft, uv).r * vSun.z * vSunCol.rgb * max(1.0 - c, 0.0); // added as a screen: bright sky does not clip
#endif
#ifdef _FLARE
	if (vSun.w > 0.0) c += Flare(uv) * (vSun.w * SunVisGPU());
#endif
#ifdef _VIGNETTE
	float r = length((uv - 0.5) * vec2(vTex.z, 1.0)) / length(vec2(0.5 * vTex.z, 0.5));
	c *= 1.0 - 0.18 * smoothstep(0.45, 1.05, r);
#endif
	return vec4(c, 1.0);
}

#if defined(PS_PSHaze) || defined(PS_PSShaftMask) || defined(PS_PSShaftBlur) || defined(PS_PSFinal)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_PSHaze
void main() { oColor = PSHaze(iX, iY); }
#endif
#ifdef PS_PSShaftMask
void main() { oColor = PSShaftMask(iX, iY); }
#endif
#ifdef PS_PSShaftBlur
void main() { oColor = PSShaftBlur(iX, iY); }
#endif
#ifdef PS_PSFinal
void main() { oColor = PSFinal(iX, iY); }
#endif
