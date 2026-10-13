// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the SSR trace every host shares (design-ssr 5; McGuire & Mara 2014, Uludag 2014, Stachowiak 2015, Heitz 2018, Jimenez 2014, Wronski 2014, Karis 2014); the host declares gSsr, tSsrCol, tSsrHiZ

#ifndef SSR_GLSL
#define SSR_GLSL

#if defined(_LIGHTGLOW)
#define SSR_CURVE false
#else
#define SSR_CURVE true
#endif

struct SsrHit { vec2 uv; float conf; float lod; bool sky; };

SsrHit SsrNone () { return SsrHit(vec2(0.0), 0.0, 0.0, false); }

float SsrIgn (vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

// a GGX visible normal (Heitz 2018) around N for the view V
vec3 SsrVndf (vec3 V, vec3 N, float a, vec2 u)
{
	vec3 T1 = abs(N.z) < 0.999 ? normalize(cross(vec3(0.0, 0.0, 1.0), N)) : normalize(cross(vec3(1.0, 0.0, 0.0), N));
	vec3 T2 = cross(N, T1);
	vec3 Vh = normalize(vec3(a * dot(V, T1), a * dot(V, T2), dot(V, N)));
	float l2 = dot(Vh.xy, Vh.xy);
	vec3 A1 = l2 > 0.0 ? vec3(-Vh.y, Vh.x, 0.0) * inversesqrt(l2) : vec3(1.0, 0.0, 0.0);
	vec3 A2 = cross(Vh, A1);
	float r = sqrt(u.x), ph = 6.2831853 * u.y;
	float t1 = r * cos(ph), t2 = r * sin(ph);
	float s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;
	vec3 Nh = t1 * A1 + t2 * A2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
	vec3 Hl = normalize(vec3(a * Nh.x, a * Nh.y, max(1e-6, Nh.z)));
	return T1 * Hl.x + T2 * Hl.y + N * Hl.z;
}

// the mirror ray, or one visible-normal sample a frame while TAA averages them (smooth surfaces stay the mirror)
vec3 SsrDir (vec3 N, vec3 V, float a, vec2 frag)
{
	vec3 R = reflect(-V, N);
	if (gSsr.vMode.y < 0.5 || a < 0.05) return R;
	vec2 u = vec2(SsrIgn(frag + 5.588238 * gSsr.vMode.z), SsrIgn(frag + vec2(37.0, 17.0) + 7.919 * gSsr.vMode.z));
	vec3 Rs = reflect(-V, SsrVndf(V, N, a, u));
	return dot(Rs, N) > 0.0 ? Rs : R;
}

// R bent to at least c above the geometric surface (waves and normal maps can point it inside)
vec3 SsrKeepAbove (vec3 R, vec3 Ng, float c)
{
	float d = dot(R, Ng);
	return d >= c ? R : normalize(R + Ng * (c - d));
}

float SsrFade (float a) { return gSsr.vMode.x * (1.0 - smoothstep(gSsr.vThick.z, gSsr.vThick.w, a)); } // validity and the roughness fade

float SsrEdge (vec2 uv)
{
	vec2 e = min(uv, 1.0 - uv);
	return smoothstep(0.0, gSsr.vDecode.w, e.x * gSsr.vHiZ.x / gSsr.vHiZ.y) * smoothstep(0.0, gSsr.vDecode.w, e.y);
}

// P camera-relative position, Ng geometric normal, R direction, a GGX alpha of the lobe (cone level, roughness fade)
SsrHit SsrTrace (vec3 P, vec3 Ng, vec3 R, float a)
{
	SsrHit h = SsrNone();
	float fade = SsrFade(a);
	if (fade <= 0.0) return h;
	P += Ng * (gSsr.vJit.z * length(P)); // off its own surface by more than a pixel centre's depth error
	vec4 c0 = vec4(P, 1.0) * gSsr.mPrev, d = vec4(R, 0.0) * gSsr.mPrev;
	float nearW = gSsr.vJit.w;
	if (c0.w < nearW) return h;
	float L = 1e6;
	bool clipped = false;
	if (c0.w + L * d.w < nearW) { L = (nearW - c0.w) / d.w; clipped = true; } // toward the camera: ends on the near plane
	vec4 c1 = c0 + d * L;
	vec2 S = gSsr.vHiZ.xy;
	vec2 X0 = vec2(c0.x / c0.w * 0.5 + 0.5, 0.5 - c0.y / c0.w * 0.5) * S;
	vec2 X1 = vec2(c1.x / c1.w * 0.5 + 0.5, 0.5 - c1.y / c1.w * 0.5) * S;
	float s0 = 1.0 / c0.w, ds = 1.0 / c1.w - s0;
	vec2 D = X1 - X0;
	float tA = 0.0, tB = 1.0;
	if (abs(D.x) < 1e-6) { if (X0.x < 0.0 || X0.x >= S.x) return h; }
	else { float ta = -X0.x / D.x, tb = (S.x - X0.x) / D.x; tA = max(tA, min(ta, tb)); tB = min(tB, max(ta, tb)); }
	if (abs(D.y) < 1e-6) { if (X0.y < 0.0 || X0.y >= S.y) return h; }
	else { float ta = -X0.y / D.y, tb = (S.y - X0.y) / D.y; tA = max(tA, min(ta, tb)); tB = min(tB, max(ta, tb)); }
	if (tA >= tB) return h;
	bool full = tB >= 1.0 && !clipped;
	float len = length(D);
	vec2 jit = gSsr.vJit.xy;
	int top = int(gSsr.vHiZ.z), nIt = int(gSsr.vHiZ.w);
	ivec2 size0 = textureSize(tSsrHiZ, 0);
	bool hit = false, end = full;
	float tHit = 0.0;
	int it = 0;
	if (len >= 1.0) {
		float t = max(tA, 1.0 / len), eps = 0.02 / len;
		int lvl = min(1, top);
		while (it < nIt && t < tB) {
			it++;
			vec2 X = X0 + t * D;
			float sz = float(1 << lvl);
			vec2 c = floor((X + jit) / sz);
			float Sc = texelFetch(tSsrHiZ, clamp(ivec2(c), ivec2(0), (size0 >> lvl) - 1), lvl).r;
			vec2 b = vec2(D.x > 0.0 ? c.x + 1.0 : c.x, D.y > 0.0 ? c.y + 1.0 : c.y) * sz - jit - X0; // the cell's far sides
			float tx = max(min(D.x != 0.0 ? b.x / D.x : 1e30, D.y != 0.0 ? b.y / D.y : 1e30), t) + eps;
			float te = min(tx, tB);
			float sA = s0 + t * ds, sB = s0 + te * ds;
			if (min(sA, sB) > Sc) { t = tx; lvl = min(lvl + 1, top); continue; } // in front of everything in the cell
			if (lvl > 0) {
				if (ds < 0.0) t = clamp((Sc - s0) / ds, t, te); // away from the camera: on to the cell's nearest plane
				lvl--;
				continue;
			}
			float zs = 1.0 / Sc;
			if (1.0 / max(sA, sB) <= zs + gSsr.vThick.x + gSsr.vThick.y * zs) {
				hit = true;
				tHit = ds < 0.0 ? clamp((Sc - s0) / ds, t, te) : t;
				break;
			}
			t = tx; // behind a thin object: on
		}
		end = !hit && full && t >= tB;
	}
	float k = gSsr.vMode.y > 0.5 ? 0.3 : 1.0; // the sampled direction already spreads the lobe
	if (hit) {
		h.uv = (X0 + tHit * D) / S;
		h.conf = fade * SsrEdge(h.uv) * (1.0 - smoothstep(0.75 * float(nIt), float(nIt), float(it)));
		h.lod = clamp(log2(max(1.0, tHit * len * a * k)), 0.0, gSsr.vDecode.z);
		return h;
	}
	if (end && gSsr.vMode.w > 0.0) { // sky at infinity: the pixel R points at, when nothing is drawn there
		vec4 v = vec4(R, 0.0) * gSsr.mSky;
		if (v.w > 1e-6) {
			vec2 Xv = vec2(v.x / v.w * 0.5 + 0.5, 0.5 - v.y / v.w * 0.5) * S;
			if (all(greaterThanEqual(Xv, vec2(0.0))) && all(lessThan(Xv, S)) && texelFetch(tSsrHiZ, clamp(ivec2(floor(Xv + jit)), ivec2(0), size0 - 1), 0).r <= 0.0) {
				h.uv = Xv / S;
				h.sky = true;
				h.conf = fade * gSsr.vMode.w * SsrEdge(h.uv);
				h.lod = clamp(log2(max(1.0, length(Xv - X0) * a * k)), 0.0, gSsr.vDecode.z);
			}
		}
	}
	return h;
}

vec3 SsrFetch (SsrHit h) { return textureLod(tSsrCol, h.uv, h.lod).rgb; }

// the split-sum directional albedo, fitted (Karis 2014): the rough surface's reflected share
float SsrEnvBRDF (float f0, float perc, float NoV)
{
	vec4 r = perc * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
	float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
	return f0 * ab.x + ab.y;
}

#if !defined(SSR_PLANET)
// a captured display value back to what a mesh host adds before exposure, its curve and its haze (PbrMesh Transmitted's chain, e of the source)
vec3 SsrMeshDecode (vec3 y, bool curve)
{
	vec3 c = (y - gSun.Inscatter) / max(gSun.Transmission, vec3(1e-3));
	if (curve) {
		c = clamp(c, 0.0, 0.99);
		c = c / pow(vec3(1.0) - c * c * c * c, vec3(0.25));
	}
	return max(c, vec3(0.0)) * gSsr.vDecode.y;
}

// base surfaces (kind 1): the rough split-sum weight of the traced radiance over the lit colour; nothing where the ray misses (design-ssr Rev 2 M4)
float SsrBase (vec3 N, vec3 V, vec3 P, vec2 frag, out SsrHit h, out float alb)
{
	h = SsrNone();
	alb = 1.0;
	if (gSsr.vMode.x < 0.5 || abs(gSsr.vSurf.x - 1.0) > 0.5) return 0.0;
	alb = gSsr.vSurf.w;
	float a = gSsr.vSurf.y;
	float F = SsrEnvBRDF(gSsr.vSurf.z, sqrt(a), clamp(dot(N, V), 0.0, 1.0));
	if (F * SsrFade(a) < 0.03) return 0.0; // the most the blend can reach: grazing views only on dry ground
	h = SsrTrace(P, N, SsrKeepAbove(SsrDir(N, V, a, frag), N, 0.01), a);
	return F * h.conf;
}
#endif

#endif
