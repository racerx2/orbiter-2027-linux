// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ray-traced GI's probe passes: trace, change, state, irradiance and distance updates (design-ddgi 4, 5; Rev 2)
// after Majercik et al. 2019 (DDGI) and 2021 (probe states, relocation, variable hysteresis); compiled with _DDGI

#extension GL_EXT_ray_query : require
#extension GL_EXT_ray_tracing_position_fetch : require

#include "SSGI.glsl"

#define DG_SKIP 1e4			// a ray texel of a probe not traced this frame
#define DG_RESET 0x10000u	// schedule entry flags, the probe in the low 16 bits
#define DG_FAST 0x20000u
#define DG_RECHECK 0x40000u
#define DG_CLEAR 0x80000u

struct DgRow { vec4 a; vec4 u; vec4 v; };	// kind (0 terrain, 1 mesh), atlas cell or table slot; the terrain's uv as planes

layout(set = 1, binding = 0) uniform accelerationStructureEXT tTlas;
layout(set = 1, binding = 1, scalar) readonly buffer DgRowBuf { DgRow rows[]; };
layout(set = 1, binding = 2, std430) readonly buffer DgSchedBuf { uint sched[]; };

layout(binding = 27) uniform sampler2D tAlbAtl;	// terrain albedo, 64 x 64 cells
layout(binding = 28) uniform sampler2D tRayD;	// radiance (rgb), distance [cells] (a): DG_RAYS texels per probe, Dg.nl.y probes per row
layout(binding = 29) uniform sampler2D tAlphaD;	// blend weight of each scheduled probe
layout(binding = 30) uniform sampler2D tStateP;	// the state before this frame's pass

vec3 DgRayDir(int r)	// spherical Fibonacci, turned by this frame's rotation
{
	float i = float(r);
	float phi = 6.28318530718 * fract(i * 0.6180339887498949);
	float cz = 1.0 - (2.0 * i + 1.0) / float(DG_RAYS), sz = sqrt(max(0.0, 1.0 - cz * cz));
	vec3 d = vec3(cos(phi) * sz, sin(phi) * sz, cz);
	return vec3(dot(Dg.rot[0].xyz, d), dot(Dg.rot[1].xyz, d), dot(Dg.rot[2].xyz, d));
}

ivec2 DgRayTexel(int i, int r) { return ivec2((i % Dg.nl.y) * DG_RAYS + r, i / Dg.nl.y); }
ivec2 DgEntryTexel(int i) { return ivec2(i % Dg.nl.y, i / Dg.nl.y); }

bool DgSunBlocked(vec3 P)	// the body itself, below the local horizon of the traced region
{
	vec3 q = P - Dg.ctr.xyz;
	float rs = Dg.ctr.w - 1000.0;
	float b = dot(q, Dg.sun.xyz), c = dot(q, q) - rs * rs;
	return b < 0.0 && b * b > c;
}

vec3 DgLocal(vec3 P, vec3 n)	// NewPlanet's LocalLights, unshadowed
{
	vec3 sum = vec3(0.0);
	for (int i = 0; i < Dg.nl.x; i++) {
		vec3 p = P - Dg.lpos[i].xyz;
		float d2 = max(dot(p, p), 1e-6), d = sqrt(d2);
		p /= d;
		float att = 1.0 / max(dot(Dg.latt[i].xyz, vec3(1.0, d, d2)), 1e-6);
		float spt = Dg.ldir[i].w > 0.5 ? clamp((dot(p, Dg.ldir[i].xyz) - Dg.lprm[i].w) * Dg.lprm[i].z, 0.0, 1.0) : 1.0;
		sum += Dg.ldif[i].rgb * clamp(dot(-p, n), 0.0, 1.0) * att * spt;
	}
	return sum;
}

// light leaving a hit toward the probe: its albedo times the sun, the local lights and the probes' irradiance there (light units)
vec3 DgShade(vec3 P, vec3 n, vec3 v, int row, int geo)
{
	DgRow R = rows[row];
	bool terr = R.a.x < 0.5;
	vec3 rho;
	if (terr) {
		vec2 uv = clamp(vec2(dot(P, R.u.xyz) + R.u.w, dot(P, R.v.xyz) + R.v.w), 0.0, 1.0);
		int cell = int(R.a.y);
		vec2 tx = vec2(cell % 32, cell / 32) * 64.0 + clamp(uv * 64.0, vec2(0.5), vec2(63.5));
		vec3 t = textureLod(tAlbAtl, tx / 2048.0, 0.0).rgb;
		rho = pow(clamp(t, 0.0, 1.0), vec3(Dg.alb.x)) * Dg.alb.y;
	}
	else {
		int slot = int(R.a.y) + geo;
		rho = texelFetch(tAlbTab, ivec2(slot % 256, slot / 256), 0).rgb;
	}
	vec3 Pg = DgGeo(P);
	vec3 L = Dg.sun.xyz;
	float nl = dot(n, L);
	vec3 sun = vec3(0.0);
	if (nl > 0.0 && !DgSunBlocked(P)) {
		rayQueryEXT q;
		rayQueryInitializeEXT(q, tTlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, P + n * (0.02 + 2e-5 * length(P)), 0.0, L, Dg.prm.y);
		while (rayQueryProceedEXT(q)) {}
		if (rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT) sun = SunAt(Pg) * nl * (terr ? 2.0 : 1.0);
	}
	float cover;
	vec3 E = DgIrradiance(P, n, v, cover);
	vec3 sky = terr ? TerrSky(Pg) : MeshSky(Pg);
	return rho * (sun + E * cover + sky * (1.0 - cover) + DgLocal(P, n));
}

// beyond the traced region: the sky above the horizon, the far ground below it
vec3 DgMiss(vec3 o, vec3 d)
{
	vec3 up = normalize(o - Dg.ctr.xyz);
	if (dot(d, up) >= 0.0) return TerrSky(DgGeo(o));
	return DgFarGround();
}

// ---------------------------------------------------------------------------------------------
// one ray of one scheduled probe: radiance (rgb), distance in cells (a), negative for a back face
vec4 PSDgTrace(ivec2 px)
{
	int K = Dg.nl.y;
	int i = px.y * K + px.x / DG_RAYS, r = px.x % DG_RAYS;
	if (i >= int(Dg.prm.w)) return vec4(0.0, 0.0, 0.0, DG_SKIP);
	uint e = sched[i];
	int p = int(e & 0xFFFFu), c = p / DG_NPC;
	vec4 st = texelFetch(tStateD, DgBlock(p), 0);
	bool fresh = (e & DG_RESET) != 0u || st.w < 0.5;
	if (!fresh && (e & DG_RECHECK) == 0u && st.w > 1.5) return vec4(0.0, 0.0, 0.0, DG_SKIP);	// open air or shut in: only on a recheck
	float s = DgSpacing(c);
	vec3 o = (vec3(DgCell(p % DG_NPC, c)) + 0.5 + (fresh ? vec3(0.0) : st.xyz)) * s;
	vec3 d = DgRayDir(r);
	rayQueryEXT q;
	rayQueryInitializeEXT(q, tTlas, gl_RayFlagsOpaqueEXT, 0xFF, o, 0.0, d, Dg.prm.y);
	while (rayQueryProceedEXT(q)) {}
	if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionTriangleEXT) return vec4(Fin(DgMiss(o, d)), Dg.prm.z);
	float t = rayQueryGetIntersectionTEXT(q, true);
	float dc = min(t / s, Dg.prm.z);
	if (!rayQueryGetIntersectionFrontFaceEXT(q, true)) return vec4(0.0, 0.0, 0.0, -max(dc, 1e-3));
	vec3 pos[3];
	rayQueryGetIntersectionTriangleVertexPositionsEXT(q, true, pos);
	mat4x3 w2o = rayQueryGetIntersectionWorldToObjectEXT(q, true);
	vec3 n = cross(pos[1] - pos[0], pos[2] - pos[0]) * mat3(w2o);
	n = dot(n, n) > 1e-20 ? normalize(n) : -d;
	if (dot(n, d) > 0.0) n = -n;
	int row = rayQueryGetIntersectionInstanceCustomIndexEXT(q, true);
	int geo = rayQueryGetIntersectionGeometryIndexEXT(q, true);
	return vec4(Fin(DgShade(o + d * t, n, -d, row, geo)), dc);
}

// the blend weight of each scheduled probe: 1 - hysteresis, lower hysteresis where the light changed (Majercik 2021)
float PSDgAlpha(ivec2 px)
{
	int i = px.y * Dg.nl.y + px.x;
	if (i >= int(Dg.prm.w)) return 0.0;
	if (texelFetch(tRayD, DgRayTexel(i, 0), 0).a > DG_SKIP * 0.5) return 0.0;
	uint e = sched[i];
	int p = int(e & 0xFFFFu);
	if ((e & DG_RESET) != 0u || texelFetch(tStateD, DgBlock(p), 0).w < 0.5) return 1.0;
	vec3 now = vec3(0.0);
	for (int r = 0; r < DG_RAYS; r++) now += texelFetch(tRayD, DgRayTexel(i, r), 0).rgb;
	now /= float(DG_RAYS);
	vec3 was = vec3(0.0);
	ivec2 b = DgBlock(p) * DG_IRR;
	for (int j = 0; j < DG_IRR; j++)
		for (int k = 0; k < DG_IRR; k++) was += texelFetch(tIrrD, b + ivec2(k, j), 0).rgb;
	was /= float(DG_IRR * DG_IRR);
	float ch = Max3G(abs(now - was)) / max(Max3G(was), 0.02);
	float h = (ch > 0.25 || (e & DG_FAST) != 0u) ? 0.5 : Dg.prm.x;
	return 1.0 - h;
}

// state and offset: open air, shut in, or on and moved out of geometry (Majercik 2021 relocation); a clear entry starts it over
vec4 PSDgState(int i)
{
	uint e = sched[i];
	int p = int(e & 0xFFFFu);
	if ((e & DG_CLEAR) != 0u) return vec4(0.0);
	vec4 prev = texelFetch(tStateP, DgBlock(p), 0);
	if (texelFetch(tRayD, DgRayTexel(i, 0), 0).a > DG_SKIP * 0.5) return prev;
	bool fresh = (e & DG_RESET) != 0u || prev.w < 0.5;
	vec3 off = fresh ? vec3(0.0) : prev.xyz;
	int nb = 0;
	float dB = 1e9, dF = 1e9, dN = 1e9;
	vec3 dirB = vec3(0.0), dirF = vec3(0.0);
	for (int r = 0; r < DG_RAYS; r++) {
		float a = texelFetch(tRayD, DgRayTexel(i, r), 0).a;
		vec3 d = DgRayDir(r);
		dN = min(dN, abs(a));
		if (a < 0.0) { nb++; if (-a < dB) { dB = -a; dirB = d; } }
		else if (a < dF) { dF = a; dirF = d; }
	}
	vec3 cand = off;
	if (float(nb) > 0.25 * float(DG_RAYS)) cand = off + dirB * (dB + 0.25);	// inside: through the nearest back face
	else if (dF < 0.2) cand = off - dirF * (0.2 - dF);						// too close to a front face: away from it
	bool fits = all(lessThanEqual(abs(cand), vec3(0.45)));
	float state;
	if (float(nb) > 0.25 * float(DG_RAYS)) state = fits ? 1.0 : 3.0;
	else state = dN < 2.0 ? 1.0 : 2.0;	// a surface within reach of a receiver it would serve
	return vec4(fits ? cand : off, state);
}

// the scheduled probes' texels: irradiance (cosine weights) or mean distance and its square (sharpened), blended by their weight
vec4 PSDgUpdate(int i, bool dist, ivec2 px)
{
	uint e = sched[i];
	int p = int(e & 0xFFFFu);
	int n = Dg.nl.z;
	float alpha = texelFetch(tAlphaD, DgEntryTexel(i), 0).r;
	if (alpha <= 0.0) return vec4(0.0);
	ivec2 t = px - DgBlock(p) * n;
	vec3 dir = DgOctDec((vec2(t) + 0.5) / float(n) * 2.0 - 1.0);
	vec3 sum = vec3(0.0);
	float ws = 0.0;
	int nb = 0;
	for (int r = 0; r < DG_RAYS; r++) {
		vec4 x = texelFetch(tRayD, DgRayTexel(i, r), 0);
		if (x.a < 0.0) nb++;
		float c = max(dot(dir, DgRayDir(r)), 0.0);
		if (dist) {
			float w = pow(c, 50.0), d = min(abs(x.a), Dg.prm.z);
			sum += vec3(d, d * d, 0.0) * w;
			ws += w;
		}
		else {
			sum += (x.a < 0.0 ? vec3(0.0) : x.rgb) * c;
			ws += c;
		}
	}
	if (float(nb) > 0.25 * float(DG_RAYS)) return vec4(0.0);	// rays from inside geometry: the probe moves first
	return vec4(ws > 1e-6 ? sum / ws : vec3(0.0), alpha);
}

// ---------------------------------------------------------------------------------------------
#if defined(VS_DgFull)
void main()	// a triangle over the viewport
{
	vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
#endif

#if defined(VS_DgQuads)
layout(location = 0) flat out int oEntry;
void main()	// one quad per entry over its probe's block of the target (Dg.nl.z texels a side)
{
	int i = gl_VertexIndex / 6, k = gl_VertexIndex % 6;
	ivec2 cr = ivec2(k == 1 || k == 3 || k == 4 ? 1 : 0, k == 2 || k == 4 || k == 5 ? 1 : 0);
	int p = int(sched[i] & 0xFFFFu), n = Dg.nl.z;
	vec2 px = vec2(DgBlock(p) * n + cr * n);
	vec2 sz = vec2(DG_ROW * n, (DG_NC * DG_NPC / DG_ROW) * n);
	gl_Position = vec4(px.x / sz.x * 2.0 - 1.0, 1.0 - px.y / sz.y * 2.0, 0.0, 1.0);
	oEntry = i;
}
#endif

#if defined(PS_DgTrace) || defined(PS_DgAlpha) || defined(PS_DgState) || defined(PS_DgIrr) || defined(PS_DgDst)
layout(location = 0) out vec4 oColor;
#endif
#if defined(PS_DgState) || defined(PS_DgIrr) || defined(PS_DgDst)
layout(location = 0) flat in int iEntry;
#endif
#ifdef PS_DgTrace
void main() { oColor = PSDgTrace(ivec2(gl_FragCoord.xy)); }
#endif
#ifdef PS_DgAlpha
void main() { oColor = vec4(PSDgAlpha(ivec2(gl_FragCoord.xy))); }
#endif
#ifdef PS_DgState
void main() { oColor = PSDgState(iEntry); }
#endif
#ifdef PS_DgIrr
void main() { oColor = PSDgUpdate(iEntry, false, ivec2(gl_FragCoord.xy)); }
#endif
#ifdef PS_DgDst
void main() { vec4 r = PSDgUpdate(iEntry, true, ivec2(gl_FragCoord.xy)); oColor = vec4(r.xy, 0.0, r.w); }
#endif
