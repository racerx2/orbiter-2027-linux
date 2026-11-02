// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ray-traced GI's probe volume, read by the trace and the screen passes (design-ddgi 4, 6; Rev 2)
// DdgiMath.cpp holds the same maths with tests; probe weights after Majercik et al. 2019 and 2021, octahedral maps after Cigolle et al. 2014
// included by SSGI.glsl under _DDGI, after SunAt, TerrAmb and MeshAmb

#define DG_NC 6
#define DG_GX 24
#define DG_GY 12
#define DG_GZ 24
#define DG_NPC 6912
#define DG_ROW 576
#define DG_IRR 6
#define DG_DST 14
#define DG_RAYS 96
#define DG_NW 41472			// the world's probes, DG_NC x DG_NPC
#define DG_VG 16			// the cockpit volume (design-ddgi-cockpit 2): DG_VG^3 probes after the world's
#define DG_NVC 4096
#define DG_VCASC 6
#define DG_AROWS 80			// atlas rows of DG_ROW probes
#define DG_MWORLD 0x03u		// ray masks: terrain, bases and other vessels (0x01), the focus vessel's exterior (0x02), its cockpit view (0x04)
#define DG_MCOCKPIT 0x05u

struct DdgiPrm
{
	ivec4 org[6];	// cascade origin, cells from the anchor (xyz)
	ivec4 off[6];	// the anchor's slot offset per cascade (xyz)
	vec4 ax;		// anchor axes, global frame (xyz)
	vec4 ay;
	vec4 az;
	vec4 cam;		// camera, anchor frame (xyz)
	vec4 ctr;		// body centre, anchor frame (xyz), radius (w)
	vec4 geo;		// anchor from the body centre, global axes (xyz)
	vec4 sun;		// to the sun, anchor frame (xyz)
	vec4 rot[3];	// this frame's ray rotation (rows)
	vec4 prm;		// hysteresis, ray reach [m], distance cap [cells], probes scheduled
	vec4 alb;		// terrain albedo pow and scale
	vec4 lpos[8];	// local lights, anchor frame
	vec4 ldir[8];	// direction (xyz); w: 1 spot, 2 lights the exterior, 4 the cockpit
	vec4 ldif[8];
	vec4 latt[8];
	vec4 lprm[8];	// range, falloff, theta, phi
	vec4 vo;		// cockpit volume: lowest cell corner, vessel frame (xyz), spacing (w, 0 none)
	vec4 vr[3];		// vessel frame to anchor frame: rotation rows (xyz), translation (w)
	ivec4 nl;		// lights, probes per ray row, block side of the pass, entries for the state pass
};

layout(binding = 3, row_major, scalar) uniform DdgiBlock { DdgiPrm Dg; };

layout(binding = 23) uniform sampler2D tIrrD;	// irradiance, light units, DG_IRR^2 per probe
layout(binding = 24) uniform sampler2D tDstD;	// mean distance and its square [cells], DG_DST^2 per probe
layout(binding = 25) uniform sampler2D tStateD;	// offset [cells] (xyz), state (w): 0 new, 1 on, 2 open air, 3 shut in
layout(binding = 26) uniform sampler2D tAlbTab;	// mesh group albedo; texel 0 the far ground

float DgSpacing(int c) { return exp2(2.0 * float(c)); }
int DgMod(int a, int m) { return a - m * int(floor(float(a) / float(m))); }	// GLSL leaves % of a negative undefined
ivec3 DgDims() { return ivec3(DG_GX, DG_GY, DG_GZ); }
ivec2 DgBlock(int p) { return ivec2(p % DG_ROW, p / DG_ROW); }
int DgCasc(int p) { return p < DG_NW ? p / DG_NPC : DG_VCASC; }

// the cockpit volume (DdgiMath::VProbePos): its frame is the focus vessel's
vec3 DgV2A(vec3 v) { return vec3(dot(Dg.vr[0].xyz, v) + Dg.vr[0].w, dot(Dg.vr[1].xyz, v) + Dg.vr[1].w, dot(Dg.vr[2].xyz, v) + Dg.vr[2].w); }
vec3 DgV2Ad(vec3 v) { return vec3(dot(Dg.vr[0].xyz, v), dot(Dg.vr[1].xyz, v), dot(Dg.vr[2].xyz, v)); }
vec3 DgA2Vd(vec3 a) { return a.x * Dg.vr[0].xyz + a.y * Dg.vr[1].xyz + a.z * Dg.vr[2].xyz; }
vec3 DgA2V(vec3 a) { return DgA2Vd(a - vec3(Dg.vr[0].w, Dg.vr[1].w, Dg.vr[2].w)); }
ivec3 DgVCell(int slot) { return ivec3(slot % DG_VG, slot / (DG_VG * DG_VG), (slot / DG_VG) % DG_VG); }
vec3 DgVProbe(int slot, vec3 off) { return Dg.vo.xyz + (vec3(DgVCell(slot)) + 0.5 + off) * Dg.vo.w; }

int DgSlot(ivec3 cellA, int c)	// a cell from the anchor to its slot (DdgiMath::Slot of its absolute cell)
{
	ivec3 q = cellA + Dg.off[c].xyz;
	return (DgMod(q.y, DG_GY) * DG_GZ + DgMod(q.z, DG_GZ)) * DG_GX + DgMod(q.x, DG_GX);
}

ivec3 DgCell(int slot, int c)	// the cell from the anchor in this frame's grid that holds the slot (DdgiMath::CellOf)
{
	ivec3 s = ivec3(slot % DG_GX, slot / (DG_GX * DG_GZ), (slot / DG_GX) % DG_GZ);
	ivec3 o = Dg.org[c].xyz;
	ivec3 q = s - Dg.off[c].xyz - o;
	return o + ivec3(DgMod(q.x, DG_GX), DgMod(q.y, DG_GY), DgMod(q.z, DG_GZ));
}

vec2 DgOctEnc(vec3 d)	// unit vector to [-1, 1]^2
{
	d /= abs(d.x) + abs(d.y) + abs(d.z);
	vec2 p = d.xy;
	if (d.z < 0.0) p = (1.0 - abs(d.yx)) * vec2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
	return p;
}

vec3 DgOctDec(vec2 e)
{
	vec3 d = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	if (d.z < 0.0) d.xy = (1.0 - abs(d.yx)) * vec2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
	return normalize(d);
}

ivec2 DgWrap(ivec2 t, int n)	// a texel off the map to the one it continues on
{
	if (t.x < 0) { t.x = 0; t.y = n - 1 - t.y; }
	else if (t.x >= n) { t.x = n - 1; t.y = n - 1 - t.y; }
	if (t.y < 0) { t.y = 0; t.x = n - 1 - t.x; }
	else if (t.y >= n) { t.y = n - 1; t.x = n - 1 - t.x; }
	return t;
}

vec4 DgOctRead(sampler2D s, int p, vec3 d, int n)	// bilinear over the probe's n x n map, across its folds
{
	vec2 px = (DgOctEnc(d) * 0.5 + 0.5) * float(n) - 0.5;
	ivec2 i0 = ivec2(floor(px));
	vec2 f = px - vec2(i0);
	ivec2 b = DgBlock(p) * n;
	vec4 r = vec4(0.0);
	for (int k = 0; k < 4; k++) {
		ivec2 o = ivec2(k & 1, k >> 1);
		vec2 w = mix(1.0 - f, f, vec2(o));
		r += texelFetch(s, b + DgWrap(i0 + o, n), 0) * (w.x * w.y);
	}
	return r;
}

// irradiance in light units at P (anchor frame) for the normal n; v toward the viewer; cover: the share the probes hold, the rest is the caller's
vec3 DgIrradiance(vec3 P, vec3 n, vec3 v, out float cover)
{
	vec3 E = vec3(0.0);
	float rest = 1.0;
	vec3 dims = vec3(DgDims());
	for (int c = 0; c < DG_NC && rest > 0.01; c++) {
		float s = DgSpacing(c);
		vec3 Pb = P + (n * 0.2 + v * 0.1) * s;	// the surface bias, in this cascade's cells
		vec3 q = Pb / s - vec3(Dg.org[c].xyz) - 0.5;
		vec3 fl = min(q, dims - 1.0 - q);
		float b = clamp(min(min(fl.x, fl.y), fl.z) * 0.5, 0.0, 1.0);
		if (b <= 0.0) continue;
		ivec3 i0 = ivec3(floor(q));
		vec3 f = q - vec3(i0);
		vec3 sum = vec3(0.0);
		float ws = 0.0;
		for (int k = 0; k < 8; k++) {
			ivec3 o = ivec3(k & 1, (k >> 1) & 1, k >> 2);
			ivec3 cl = clamp(i0 + o, ivec3(0), DgDims() - 1);
			ivec3 cellA = Dg.org[c].xyz + cl;
			int p = c * DG_NPC + DgSlot(cellA, c);
			vec4 st = texelFetch(tStateD, DgBlock(p), 0);
			if (st.w < 0.5 || st.w > 1.5) continue;	// new, open air or shut in
			vec3 pp = (vec3(cellA) + 0.5 + st.xyz) * s;
			vec3 t3 = mix(1.0 - f, f, vec3(o));
			float wt = t3.x * t3.y * t3.z;
			vec3 toProbe = normalize(pp - P);
			float wb = max(1e-4, (dot(toProbe, n) + 1.0) * 0.5);
			float w = wb * wb + 0.2;
			vec3 d = Pb - pp;
			float r = length(d) / s;
			vec2 m = DgOctRead(tDstD, p, d / max(r * s, 1e-6), DG_DST).xy;
			float ch = 1.0;
			if (r > m.x) {
				float var = abs(m.y - m.x * m.x) + 1e-4, dd = r - m.x;
				ch = var / (var + dd * dd);
				ch = ch * ch * ch;
			}
			w *= max(0.05, ch);
			w = max(1e-6, w);
			if (w < 0.2) w *= w * w / 0.04;	// crush the faint ones
			w *= wt;
			sum += DgOctRead(tIrrD, p, n, DG_IRR).rgb * w;
			ws += w;
		}
		if (ws < 1e-5) continue;	// no probe on here: the next cascade
		E += rest * b * sum / ws;
		rest *= 1.0 - b;
	}
	cover = 1.0 - rest;
	return cover > 1e-4 ? E / cover : vec3(0.0);
}

// the cockpit volume's irradiance at Pv for the normal n, v toward the viewer (vessel frame); cover 1 within its box, 0 a cell beyond it
vec3 DgIrradianceV(vec3 Pv, vec3 n, vec3 v, out float cover)
{
	cover = 0.0;
	float s = Dg.vo.w;
	if (s <= 0.0) return vec3(0.0);
	vec3 Pb = Pv + (n * 0.2 + v * 0.1) * s;
	vec3 q = (Pb - Dg.vo.xyz) / s - 0.5;
	vec3 fl = min(q, vec3(float(DG_VG - 1)) - q);
	float b = clamp(min(min(fl.x, fl.y), fl.z) + 1.5, 0.0, 1.0);	// the box's faces are half a cell past the outer probes
	if (b <= 0.0) return vec3(0.0);
	ivec3 i0 = ivec3(floor(q));
	vec3 f = q - vec3(i0);
	vec3 sum = vec3(0.0);
	float ws = 0.0;
	for (int k = 0; k < 8; k++) {
		ivec3 o = ivec3(k & 1, (k >> 1) & 1, k >> 2);
		ivec3 cl = clamp(i0 + o, ivec3(0), ivec3(DG_VG - 1));
		int p = DG_NW + (cl.y * DG_VG + cl.z) * DG_VG + cl.x;
		vec4 st = texelFetch(tStateD, DgBlock(p), 0);
		if (st.w < 0.5 || st.w > 1.5) continue;
		vec3 pp = Dg.vo.xyz + (vec3(cl) + 0.5 + st.xyz) * s;
		vec3 t3 = mix(1.0 - f, f, vec3(o));
		float wt = t3.x * t3.y * t3.z;
		vec3 toProbe = normalize(pp - Pv);
		float wb = max(1e-4, (dot(toProbe, n) + 1.0) * 0.5);
		float w = wb * wb + 0.2;
		vec3 d = Pb - pp;
		float r = length(d) / s;
		vec2 m = DgOctRead(tDstD, p, d / max(r * s, 1e-6), DG_DST).xy;
		float ch = 1.0;
		if (r > m.x) {
			float var = abs(m.y - m.x * m.x) + 1e-4, dd = r - m.x;
			ch = var / (var + dd * dd);
			ch = ch * ch * ch;
		}
		w *= max(0.05, ch);
		w = max(1e-6, w);
		if (w < 0.2) w *= w * w / 0.04;
		w *= wt;
		sum += DgOctRead(tIrrD, p, n, DG_IRR).rgb * w;
		ws += w;
	}
	if (ws < 1e-5) return vec3(0.0);
	cover = b;
	return sum / ws;
}

vec3 DgGeo(vec3 P) { return Dg.geo.xyz + Dg.ax.xyz * P.x + Dg.ay.xyz * P.y + Dg.az.xyz * P.z; }	// anchor frame to geocentric

// the sky's part of the shaders' ambient; the configured fill light is left to the shaders, the probes neither add nor take it
vec3 TerrSky(vec3 Pg) { return vFade.w > 0.5 ? max(TerrAmb(Pg) - Const.cAmbient * Const.Ambient, vec3(0.0)) : vec3(0.0); }
vec3 MeshSky(vec3 Pg) { return max(MeshAmb(Pg) - vAmbC.rgb, vec3(0.0)); }

// the ground beyond the traced region: the far tile's albedo in the sun and the sky (light units)
vec3 DgFarGround()
{
	vec3 Pg = Dg.geo.xyz;
	vec3 up = normalize(-Dg.ctr.xyz);
	vec3 rho = pow(clamp(texelFetch(tAlbTab, ivec2(0), 0).rgb, 0.0, 1.0), vec3(Dg.alb.x)) * Dg.alb.y;
	return rho * (2.0 * SunAt(Pg) * max(dot(up, Dg.sun.xyz), 0.0) + TerrSky(Pg));
}
