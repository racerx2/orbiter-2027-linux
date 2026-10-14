
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: ground shadows of the volumetric clouds (design-visual-batch 8, rev 2 M3 M4 S8)
// VolCloud.glsl's VolPS fields and density; its own IPI, so a failure here leaves the volume clouds on

#include "VolCloud.glsl"

layout(binding = 18) uniform sampler2D tShd;	// shadow, half resolution R8

const int SHD_STEPS = 12;
const float SHD_LEN = 6000.0;	// longest sun path through the slab, m
const float SHD_EXT = 0.6;		// extinction scale: light scattered forward into the shadow
const float SHD_MAX = 0.75;		// darkest shadow

vec2 FullUV(vec2 q) { return (floor(q / vTex.xy - 0.25) + 0.5) * vTex.xy; }	// the SceneDepth texel a half resolution texel stands for

float HalfDepth(vec2 q) { return texture(tDepthV, FullUV(q)).r; }

// sun ray from the ground point through the slab: shadow 0..1, faded with distance and the volume weight
vec4 PSVolShadow(float x, float y)
{
	vec2 uv = FullUV(vec2(x, y));
	float zd = texture(tDepthV, uv).r;
	if (zd <= 0.0 || zd >= vFade.y) return vec4(0.0);
	vec3 o = Const.CamPos + PixelRay(uv) * zd;
	vec3 s = Const.toSun;
	float t0, t1;
	if (!Slab(o, s, t0, t1)) return vec4(0.0);
	t1 = min(t1, t0 + SHD_LEN);
	float dt = (t1 - t0) / float(SHD_STEPS), od = 0.0;
	for (int i = 0; i < SHD_STEPS; i++) {
		vec3 p = o + s * (t0 + (float(i) + 0.5) * dt);
		float c = Coverage(p);
		if (c <= 0.0) continue;
		float k = mix(Clump(CloudQ(p)), 1.0, smoothstep(25000.0, 35000.0, length(p - Const.CamPos)));	// VolMarch's clump fade
		od += Density(p, c * k, 0.0) * dt;
	}
	float sh = (1.0 - exp(-od * vFade.w * SHD_EXT)) * (1.0 - smoothstep(vFade.x, vFade.y, zd)) * vMarch.z;
	return vec4(sh);
}

// 3x3 depth-weighted, as VolBlur
vec4 PSVolShdBlur(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d0 = HalfDepth(uv);
	if (d0 <= 0.0) return vec4(0.0);
	float s = 0.0, ws = 0.0;
	for (int j = -1; j <= 1; j++)
		for (int i = -1; i <= 1; i++) {
			vec2 q = uv + vec2(i, j) * vTex.zw;
			float w = ((i == 0 && j == 0) ? 2.0 : 1.0) * DepthW(d0, HalfDepth(q));
			s += texture(tShd, q).r * w;
			ws += w;
		}
	return vec4(s / ws);
}

// camera-relative position of a SceneDepth texel, geocentric axes
vec3 ShdPos(vec2 uv, float d) { return PixelRay(uv) * d; }

// normal from the depth, the nearer neighbour on each axis
vec3 ShdNormal(vec2 uv, vec3 P, float d)
{
	vec2 tx = vec2(vTex.x, 0.0), ty = vec2(0.0, vTex.y);
	float dr = texture(tDepthV, uv + tx).r, dl = texture(tDepthV, uv - tx).r;
	float du = texture(tDepthV, uv - ty).r, dd = texture(tDepthV, uv + ty).r;
	float er = dr > 0.0 ? abs(dr - d) : 1e30, el = dl > 0.0 ? abs(d - dl) : 1e30;
	float eu = du > 0.0 ? abs(d - du) : 1e30, ed = dd > 0.0 ? abs(dd - d) : 1e30;
	vec3 ex = er < el ? ShdPos(uv + tx, dr) - P : (el < 1e30 ? P - ShdPos(uv - tx, dl) : vec3(0.0));
	vec3 ey = ed < eu ? ShdPos(uv + ty, dd) - P : (eu < 1e30 ? P - ShdPos(uv - ty, du) : vec3(0.0));
	vec3 n = cross(ey, ex);
	if (dot(n, n) < 1e-12) return -normalize(P);
	n = normalize(n);
	return dot(n, P) > 0.0 ? -n : n;
}

// depth-aware 2x2 up-sample, as VolComposite; black with alpha over the scene
vec4 PSVolShdComposite(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d = texture(tDepthV, uv).r;
	if (d <= 0.0 || d >= vFade.y) return vec4(0.0);
	vec2 c = (floor(uv / vTex.zw - 0.5) + 0.5) * vTex.zw;
	float s = 0.0, ws = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec2 q = c + vec2(i, j) * vTex.zw;
			vec2 f = 1.0 - abs(uv - q) / vTex.zw;
			float w = max(f.x * f.y, 1e-3) * DepthW(d, HalfDepth(q));
			s += texture(tShd, q).r * w;
			ws += w;
		}
	float sh = s / ws;
	if (sh < 0.5 / 255.0) return vec4(0.0);
	vec3 P = ShdPos(uv, d);
	float lit = clamp(dot(ShdNormal(uv, P, d), Const.toSun), 0.0, 1.0);	// faces turned from the sun are dark already
	vec3 pw = Const.CamPos + P;
	LandOut h = GetLandView(length(pw), normalize(pw));
	float atn = clamp(dot(h.atn.rgb, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);	// the haze in front is not shadowed
	return vec4(0.0, 0.0, 0.0, sh * SHD_MAX * atn * lit);
}

#if defined(PS_PSVolShadow) || defined(PS_PSVolShdBlur) || defined(PS_PSVolShdComposite)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_PSVolShadow
void main() { oColor = PSVolShadow(iX, iY); }
#endif
#ifdef PS_PSVolShdBlur
void main() { oColor = PSVolShdBlur(iX, iY); }
#endif
#ifdef PS_PSVolShdComposite
void main() { oColor = PSVolShdComposite(iX, iY); }
#endif
