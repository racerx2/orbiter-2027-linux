// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: volumetric clouds near the camera (design-ground-graphics 4.4, 4.5)
// Shape remap after Schneider 2015, step integral after Hillaire 2015, both written here from the published maths.

#include "Scatter.glsl"

layout(binding = 2, row_major, scalar) uniform VolPS	// Scatter.glsl's Const is binding 1
{
vec4	vCamX;		// camera axes, geocentric (xyz); tan of the half aperture (w)
vec4	vCamY;
vec4	vCamZ;		// forward
vec4	vMapE;		// map east, north and up (xyz) at the map centre, geocentric; 1/half map side (vMapE.w)
vec4	vMapN;
vec4	vMapU;
vec4	vClA;		// geocentric to the cloud layer's frame, rows (noise coordinates)
vec4	vClB;
vec4	vClC;
vec4	vSlab;		// base radius, top radius, planet radius, base altitude
vec4	vMarch;		// steps, frame, volume weight, side (0 below the slab .. 1 above)
vec4	vFade;		// far fade start, end, max distance (m), extinction (1/m)
vec4	vNoise;		// shape 1/m, detail 1/m, detail erosion, coverage gain
vec4	vTex;		// 1/w, 1/h of SceneDepth; 1/w, 1/h of the march buffer
};

layout(binding = 13) uniform sampler2D tDepthV;	// camera distance, 0 none
layout(binding = 14) uniform sampler2D tCov;	// coverage map, row 0 north
layout(binding = 15) uniform sampler3D tShape;
layout(binding = 16) uniform sampler3D tDetail;
layout(binding = 17) uniform sampler2D tVol;	// march result

float IGNv(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

vec2 SphereHit(vec3 o, vec3 d, float r)	// near and far t; x > y: missed
{
	float b = dot(o, d), c = dot(o, o) - r * r, h = b * b - c;
	if (h < 0.0) return vec2(1e30, -1e30);
	h = sqrt(h);
	return vec2(-b - h, -b + h);
}

// the first stretch of the ray inside the slab, stopped by the planet
bool Slab(vec3 o, vec3 d, out float t0, out float t1)
{
	float r0 = length(o);
	vec2 hb = SphereHit(o, d, vSlab.x), ht = SphereHit(o, d, vSlab.y), hp = SphereHit(o, d, vSlab.z);
	bool base = hb.x <= hb.y && hb.x > 0.0;
	if (r0 < vSlab.x) { t0 = hb.y; t1 = ht.y; }
	else if (r0 <= vSlab.y) { t0 = 0.0; t1 = base ? hb.x : ht.y; }
	else {
		if (ht.x > ht.y || ht.y < 0.0) { t0 = t1 = 0.0; return false; }
		t0 = max(ht.x, 0.0);
		t1 = base ? hb.x : ht.y;
	}
	if (hp.x <= hp.y && hp.x > 0.0) t1 = min(t1, hp.x);
	return t1 > t0;
}

float Coverage(vec3 p)
{
	vec2 m = vec2(dot(p, vMapE.xyz), dot(p, vMapN.xyz)) / dot(p, vMapU.xyz) * vSlab.z;	// gnomonic, m
	vec2 uv = vec2(m.x, -m.y) * vMapE.w * 0.5 + 0.5;
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 0.0;
	return texture(tCov, uv).r;
}

float Density(vec3 p, float c, bool detail)
{
	c = clamp(c * vNoise.w, 0.0, 1.0);
	float h = length(p) - vSlab.x;
	float top = 500.0 + 2500.0 * c;
	if (c <= 0.01 || h < 0.0 || h > top) return 0.0;
	float hf = h / top;
	float prof = clamp(h / 150.0, 0.0, 1.0) * (1.0 - smoothstep(0.65, 1.0, hf));
	vec3 q = vec3(dot(p, vClA.xyz), dot(p, vClB.xyz), dot(p, vClC.xyz));
	vec4 s = texture(tShape, q * vNoise.x);
	float fbm = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
	float base = clamp((s.r - (fbm - 1.0)) / (2.0 - fbm), 0.0, 1.0) * prof;
	float d = clamp((base - (1.0 - c)) / c, 0.0, 1.0) * c;	// coverage remap
	if (detail && d > 0.0) {
		vec3 t = texture(tDetail, q * vNoise.y).rgb;
		float dn = t.r * 0.625 + t.g * 0.25 + t.b * 0.125;
		dn = mix(dn, 1.0 - dn, clamp(hf * 4.0, 0.0, 1.0));	// wisps at the base, billows above
		float e = dn * vNoise.z;
		d = clamp((d - e) / (1.0 - e), 0.0, 1.0);
	}
	return d;
}

float HG(float c, float g) { float g2 = g * g; return (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * c, 1.5); } // 4 pi times Henyey-Greenstein

float LightDepth(vec3 p, vec3 s) // optical depth toward the sun
{
	float od = 0.0, step = 60.0, t = 0.0;
	for (int i = 0; i < 5; i++) {
		vec3 q = p + s * (t + 0.5 * step);
		od += Density(q, Coverage(q), false) * step;
		t += step;
		step *= 2.0;
	}
	return od * vFade.w;
}

float DepthW(float a, float b)
{
	if (a <= 0.0 && b <= 0.0) return 1.0;
	if (a <= 0.0 || b <= 0.0) return 0.02;
	return exp(-abs(a - b) / (0.02 * a + 1.0));
}

vec4 VolMarch(float x, float y)
{
	vec2 uv = vec2(x, y);
	vec3 d = normalize(vCamX.xyz * ((uv.x * 2.0 - 1.0) * vCamX.w) + vCamY.xyz * ((1.0 - uv.y * 2.0) * vCamY.w) + vCamZ.xyz);
	vec3 o = Const.CamPos;
	float t0, t1;
	if (!Slab(o, d, t0, t1)) return vec4(0.0);
	float zd = texture(tDepthV, (floor(uv / vTex.xy) + 0.5) * vTex.xy).r;
	if (zd > 0.0) t1 = min(t1, zd);
	t1 = min(t1, vFade.z);
	if (t1 <= t0) return vec4(0.0);

	vec3 s = Const.toSun;
	float cosT = dot(d, s);
	float ph = 0.7 * HG(cosT, 0.6) + 0.3 * HG(cosT, -0.3);
	float n = vMarch.x;
	float dtMin = 6000.0 / n, grow = 2.5 / n;
	float t = t0 + dtMin * IGNv(gl_FragCoord.xy + vMarch.y * 5.588238);
	vec3 L = vec3(0.0);
	float T = 1.0, tw = 0.0, ts = 0.0;
	for (int i = 0; i < 288; i++) {
		if (float(i) >= 3.0 * n || t >= t1 || T < 0.01) break;
		float dt = max(dtMin, grow * t);
		vec3 p = o + d * t;
		float dens = Density(p, Coverage(p), true) * (1.0 - smoothstep(vFade.x, vFade.y, t)) * vMarch.z;
		if (dens <= 0.0) { t += 2.0 * dt; continue; } // empty: bigger steps
		vec3 nr = normalize(p);
		float od = LightDepth(p, s);
		float beer = max(exp(-od), 0.7 * exp(-0.25 * od));	// some multiple scattering
		float powder = 1.0 - 0.4 * exp(-2.0 * od) * (0.5 - 0.5 * cosT);
		vec3 sunC = GetSunColor(dot(nr, s), length(p) - vSlab.z) * Const.cSun * Const.Clouds;
		vec4 amb = AmbientApprox(nr);
		float hf = clamp((length(p) - vSlab.x) / (vSlab.y - vSlab.x), 0.0, 1.0);
		vec3 S = sunC * (ph * beer * powder) + amb.rgb * amb.a * (0.25 + 0.35 * hf);
		float a = 1.0 - exp(-dens * vFade.w * dt);
		L += T * a * S;
		tw += T * a * t;
		ts += T * a;
		T *= 1.0 - a;
		t += dt;
	}
	float alpha = 1.0 - T;
	if (alpha < 1e-3) return vec4(0.0);
	vec3 col = L / alpha;
	vec3 pm = o + d * (tw / max(ts, 1e-6));	// haze to the cloud's mean distance, as the 2D layer
	LandOut sct = GetLandView(length(pm), normalize(pm));
	col = col * sct.atn.rgb + sct.ray.rgb * mix(2.0, 1.0, vMarch.w);
	col = mix(HDR(col), sqr(HDR(col * 4.0)), vMarch.w); // the 2D layer's curves, from below and from above
	return vec4(col * alpha, alpha);
}

vec4 VolBlur(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d0 = texture(tDepthV, uv).r;
	vec4 s = vec4(0.0);
	float ws = 0.0;
	for (int j = -1; j <= 1; j++)
		for (int i = -1; i <= 1; i++) {
			vec2 q = uv + vec2(i, j) * vTex.zw;
			float w = ((i == 0 && j == 0) ? 2.0 : 1.0) * DepthW(d0, texture(tDepthV, q).r);
			s += texture(tVol, q) * w;
			ws += w;
		}
	return s / ws;
}

vec4 VolComposite(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d = texture(tDepthV, uv).r;
	vec2 c = (floor(uv / vTex.zw - 0.5) + 0.5) * vTex.zw;
	vec4 s = vec4(0.0);
	float ws = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec2 q = c + vec2(i, j) * vTex.zw;
			vec2 f = 1.0 - abs(uv - q) / vTex.zw;
			float w = max(f.x * f.y, 1e-3) * DepthW(d, texture(tDepthV, q).r);
			s += texture(tVol, q) * w;
			ws += w;
		}
	vec4 v = s / ws;
	if (v.a < 1e-3) return vec4(0.0);
	return vec4(v.rgb / v.a, v.a);
}

#if defined(PS_VolMarch) || defined(PS_VolBlur) || defined(PS_VolComposite)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_VolMarch
void main() { oColor = VolMarch(iX, iY); }
#endif
#ifdef PS_VolBlur
void main() { oColor = VolBlur(iX, iY); }
#endif
#ifdef PS_VolComposite
void main() { oColor = VolComposite(iX, iY); }
#endif
