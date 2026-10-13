// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen space ambient occlusion on the SceneDepth distance (design-ground-graphics 7)
// AO term after McGuire et al. 2011 (Alchemy AO), pixel noise after Jimenez 2014 (interleaved gradient noise)

layout(binding = 1, row_major, scalar) uniform SSAOPS	// uniform extern: pixel shader constants (IPI.glsl's are binding 0)
{
vec4	vProj;		// tan of the half apertures x, y; 1/width, 1/height of SceneDepth
vec4	vPrm;		// radius m, radius growth per m, max radius m, strength
vec4	vFade;		// fade start m, fade end m, frame number, samples
vec4	vSun;		// sun direction in camera space, sun up 0..1
vec2	vDir;		// blur step in uv
vec2	vAOTexel;	// 1/width, 1/height of the AO buffer
};

layout(binding = 4) uniform sampler2D tDepth;	// camera distance, 0 none
layout(binding = 5) uniform sampler2D tAO;		// AO, blurred

const int NS = 12;

float Dist(vec2 uv) { return texture(tDepth, uv).r; }

vec2 Snap(vec2 uv) { return (floor(uv / vProj.zw) + 0.5) * vProj.zw; }	// depth texel centre, so the position matches the sample

vec3 ViewPos(vec2 uv, float d)
{
	vec3 r = vec3((uv.x * 2.0 - 1.0) * vProj.x, (1.0 - uv.y * 2.0) * vProj.y, 1.0);
	return normalize(r) * d;
}

// normal from the depth, the nearer neighbour on each axis
vec3 ViewNormal(vec2 uv, vec3 P)
{
	vec2 tx = vec2(vProj.z, 0.0), ty = vec2(0.0, vProj.w);
	float dr = Dist(uv + tx), dl = Dist(uv - tx), du = Dist(uv - ty), dd = Dist(uv + ty);
	vec3 Pr = dr > 0.0 ? ViewPos(uv + tx, dr) : P, Pl = dl > 0.0 ? ViewPos(uv - tx, dl) : P;
	vec3 Pu = du > 0.0 ? ViewPos(uv - ty, du) : P, Pd = dd > 0.0 ? ViewPos(uv + ty, dd) : P;
	vec3 ex = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
	vec3 ey = abs(Pd.z - P.z) < abs(P.z - Pu.z) ? Pd - P : P - Pu;
	vec3 n = cross(ey, ex);
	if (dot(n, n) < 1e-12) return -normalize(P);
	n = normalize(n);
	return dot(n, P) > 0.0 ? -n : n;
}

float IGNoise(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

vec4 PSAO(float x, float y)
{
	vec2 uv = Snap(vec2(x, y));
	float d = Dist(uv);
	if (d <= 0.0 || d >= vFade.y) return vec4(1.0);
	vec3 P = ViewPos(uv, d);
	vec3 n = ViewNormal(uv, P);
	float r = min(vPrm.x + vPrm.y * d, vPrm.z);
	float ruv = r / (d * vProj.y) * 0.5;							// radius in uv, vertical
	ruv = clamp(ruv, 2.0 * vAOTexel.y, 60.0 * vAOTexel.y);
	float a0 = IGNoise(gl_FragCoord.xy + vFade.z * 5.588238) * 6.2831853;
	float sum = 0.0;
	int ns = 0;
	for (int i = 0; i < NS; i++) {
		float t = (float(i) + 0.5) / float(NS);
		float a = a0 + float(i) * 2.3999632;						// golden angle
		vec2 q = Snap(uv + vec2(cos(a) * vProj.z / vProj.w, sin(a)) * ruv * sqrt(t)); // same pixel radius both ways
		if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0) continue; // off screen: unknown, not the clamped edge
		ns++;
		float dq = Dist(q);
		if (dq <= 0.0) continue;
		vec3 v = ViewPos(q, dq) - P;
		float vv = dot(v, v);
		float w = clamp(1.0 - sqrt(vv) / (2.0 * r), 0.0, 1.0);	// range check
		sum += w * max(0.0, dot(v, n) - 0.002 * d) * inversesqrt(vv + 0.01 * r * r);	// sigma scaled by 1/|v|: unitless
	}
	float ao = ns > 0 ? clamp(1.0 - 2.0 * sum / float(ns), 0.0, 1.0) : 1.0; // the samples seen: no fade at the screen edge
	return vec4(ao, ao, ao, 1.0);
}

vec4 PSBlur(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d0 = Dist(Snap(uv)); // the texel PSAO read
	if (d0 <= 0.0) return vec4(1.0);
	float s = 0.0, ws = 0.0;
	for (int i = -4; i <= 4; i++) {
		vec2 q = uv + vDir * float(i);
		float dq = Dist(Snap(q));
		float w = exp(-float(i * i) / 8.0) * (dq > 0.0 ? exp(-abs(dq - d0) / (0.02 * d0 + 0.05)) : 0.0);
		s += texture(tAO, q).r * w;
		ws += w;
	}
	float ao = ws > 0.0 ? s / ws : 1.0;
	return vec4(ao, ao, ao, 1.0);
}

vec4 PSComposite(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d = Dist(uv);
	if (d <= 0.0 || d >= vFade.y) return vec4(0.0);
	// depth-aware up-sample from the half resolution AO
	vec2 c = (floor(uv / vAOTexel - 0.5) + 0.5) * vAOTexel;
	float s = 0.0, ws = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec2 q = c + vec2(i, j) * vAOTexel;
			float dq = Dist(Snap(q)); // the texel PSAO read for that AO texel
			vec2 f = 1.0 - abs(uv - q) / vAOTexel;
			float w = max(f.x * f.y, 1e-3) * (dq > 0.0 ? exp(-abs(dq - d) / (0.02 * d + 0.05)) : 1e-3);
			s += texture(tAO, q).r * w;
			ws += w;
		}
	float ao = s / ws;
	vec3 P = ViewPos(uv, d);
	vec3 n = ViewNormal(uv, P);
	float lit = clamp(dot(n, vSun.xyz), 0.0, 1.0) * vSun.w;
	float k = vPrm.w * (1.0 - smoothstep(vFade.x, vFade.y, d)) * (1.0 - 0.6 * lit);
	return vec4(0.0, 0.0, 0.0, k * (1.0 - ao));
}

#if defined(PS_PSAO) || defined(PS_PSBlur) || defined(PS_PSComposite)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_PSAO
void main() { oColor = PSAO(iX, iY); }
#endif
#ifdef PS_PSBlur
void main() { oColor = PSBlur(iX, iY); }
#endif
#ifdef PS_PSComposite
void main() { oColor = PSComposite(iX, iY); }
#endif
