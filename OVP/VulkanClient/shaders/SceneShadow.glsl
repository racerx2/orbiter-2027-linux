// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: whole-scene sun shadows, the receivers (design-scene-shadows Rev 2); included twice: first CsmPrm, then CsmVis once gCsm and the sampler exist
// cascades (Engel 2006), receiver plane depth bias (Isidoro 2006), normal offset (Holbert 2011), blocker search and penumbra (Fernando 2005), IGN (Jimenez 2014), best-candidate disk (Mitchell 1991)
// nothing here declares a block or a sampler: the including file appends gCsm to its own block and passes its sampler, so VkEffect's parser finds nothing to edit

#if defined(_CSM) && !defined(CSM_PRM)
#define CSM_PRM
struct CsmPrm
{
	mat4 mC[6];	// camera-relative world to atlas u, v (down) and depth 0..1, row vectors: the 4 cascades, then the 2 target tiles
	vec4 vC[6];	// texel m, depth range m, select radius m, band start m; a target tile: texel m, depth range m, on, 0
	vec4 vO[6];	// the tile's first texel in world texels mod 4096 (xy), terrain receiver bias m (z)
	vec4 vL;	// toward the sun (xyz), 2 tan of the sun's angular radius (w)
	vec4 vF;	// fade start m, fade end m, noise phase (frame % 8 with TAA, -1 fixed on the ground), on
};
#elif defined(_CSM) && defined(STAGE_PS) && !defined(CSM_FN)
#define CSM_FN

const vec2 CsmA = vec2(6144.0, 4096.0); // atlas texels: 3 x 2 tiles of 2048 (CsmMath AW, AH)

const vec2 CsmDisk[8] = vec2[8](vec2(0.570221050, 0.691418245), vec2(-0.597767600, -0.715386874), vec2(0.630692208, -0.609182082), vec2(-0.753514249, 0.516043067),
	vec2(-0.017911456, 0.042478922), vec2(0.053921912, -0.976469520), vec2(0.901594246, 0.053632464), vec2(-0.247332633, 0.965349239)); // CsmMath::Disk(8); the first four on the rim

float CsmIGN(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

vec2 CsmOrgT(int i) { return (i < 4 ? vec2(float(i & 1), float(i >> 1)) : vec2(2.0, float(i - 4))) * 2048.0; } // a tile's first texel (CsmMath Col, Row)

struct CsmAt
{
	vec3 s;			// atlas u, v and depth of the offset point
	vec2 g;			// receiver plane: depth change per atlas uv
	float e;		// constant bias, depth units
	vec2 lo, hi;	// gather centres kept inside the tile
	float t, D;		// texel m, depth range m
};

// one tile's frame at a point: normal offset, the plane gradient from the geometric normal (cascades differ between quad pixels, so no dFdx)
CsmAt CsmSetup(int i, vec3 p, vec3 n, float cosT, float kb, float zb, float kt)
{
	CsmAt a;
	a.t = gCsm.vC[i].x;
	a.D = gCsm.vC[i].y;
	mat4 M = gCsm.mC[i];
	float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
	a.s = (vec4(p + n * (a.t * sinT), 1.0) * M).xyz;
	vec2 k = CsmA * a.t; // metres per atlas uv
	a.g = vec2(dot(n, M[0].xyz), dot(n, M[1].xyz)) * (k * k / (a.D * max(cosT, 0.2)));
	float tanX = max(min(sinT / max(cosT, 0.02), 50.0) - 4.8989795, 0.0); // the slope the plane cap (cos 0.2) leaves out, given as bias
	a.e = (0.25 * a.t * kb + zb + kt * gCsm.vO[i].z + 1.5 * a.t * tanX) / a.D;
	vec2 o = CsmOrgT(i);
	a.lo = (o + 1.0) / CsmA;
	a.hi = (o + 2047.0) / CsmA;
	return a;
}

// the four texels round a gather corner, each against the receiver plane at its own centre
vec4 CsmPlane(in CsmAt a, vec2 cc)
{
	vec2 h = 0.5 / CsmA;
	float z = a.s.z + dot(a.g, cc - a.s.xy) - a.e;
	return z + vec4(dot(a.g, vec2(-h.x, h.y)), dot(a.g, h), dot(a.g, vec2(h.x, -h.y)), -dot(a.g, h));
}

// 2x2 compare at atlas uv c, weighted as bilinear: the lit share
float CsmPcf2(sampler2D tC, in CsmAt a, vec2 c)
{
	vec2 f = clamp(c, a.lo, a.hi) * CsmA - 0.5;
	vec2 b = floor(f), w = f - b;
	vec2 cc = (b + 1.0) / CsmA;
	vec4 d = textureGather(tC, cc);
	vec4 lit = max(step(CsmPlane(a, cc), d), step(1.0, d)); // the cleared 1.0 is no caster: the target tile's receivers lie past its depth range
	return mix(mix(lit.w, lit.z, w.x), mix(lit.x, lit.y, w.x), w.y);
}

void CsmBlk(sampler2D tC, in CsmAt a, vec2 c, inout float sum, inout float cnt)
{
	vec2 cc = (floor(clamp(c, a.lo, a.hi) * CsmA - 0.5) + 1.0) / CsmA;
	vec4 d = textureGather(tC, cc);
	vec4 bl = (vec4(1.0) - step(CsmPlane(a, cc), d)) * (vec4(1.0) - step(1.0, d));
	sum += dot(bl, d);
	cnt += dot(bl, vec4(1.0));
}

// PCF on the 8-tap disk of radius R texels, never below one texel: a sub-texel penumbra still gets a smooth edge, not the texel steps
float CsmPcf(sampler2D tC, in CsmAt a, float R, mat2 rot)
{
	vec2 r = vec2(max(R, 1.0)) / CsmA;
	float v = 0.0;
	for (int k = 0; k < 8; k++) v += CsmPcf2(tC, a, a.s.xy + (rot * CsmDisk[k]) * r);
	return v * 0.125;
}

float CsmCascade(sampler2D tC, int i, vec3 p, vec3 n, float cosT, float kb, float zb, float kt)
{
	CsmAt a = CsmSetup(i, p, n, cosT, kb, zb, kt);
	float phi;
	if (gCsm.vF.z >= 0.0) phi = CsmIGN(gl_FragCoord.xy + 5.588238 * gCsm.vF.z); // moves per frame, TAA averages it
	else {
		vec2 ti = floor(clamp(a.s.xy, a.lo, a.hi) * CsmA) - CsmOrgT(i); // texel in the tile
		phi = CsmIGN(vec2(mod(gCsm.vO[i].x + ti.x, 4096.0), mod(gCsm.vO[i].y - ti.y + 4096.0, 4096.0))); // fixed on the ground
	}
	phi *= 6.2831853;
	float cs = cos(phi), sn = sin(phi);
	mat2 rot = mat2(cs, sn, -sn, cs);
	float sum = 0.0, cnt = 0.0;
	CsmBlk(tC, a, a.s.xy, sum, cnt); // blocker search: the centre and 4 rim taps at 4 texels
	for (int k = 0; k < 4; k++) CsmBlk(tC, a, a.s.xy + (rot * CsmDisk[k]) * (4.0 / CsmA), sum, cnt);
	if (cnt < 0.5) return 1.0;
	float w = max(a.s.z - sum / cnt, 0.0) * a.D * gCsm.vL.w; // penumbra width m
	float R = w / (2.0 * a.t);
	if (cnt > 19.5 && R <= 4.0) return 0.0; // umbra, unless a far caster's wide penumbra goes on in the next cascade
	float v = cnt > 19.5 ? 0.0 : CsmPcf(tC, a, min(R, 4.0), rot);
	if (i < 3 && R > 4.0) { // wide penumbra from far casters: the coarser cascades' texels, at most 4 texels each, every step blended over 4-8 texels
		float hw[3], pv[3];
		int m = 0;
		float Rj = R;
		for (int j = i; j < 3 && Rj > 4.0; j++) {
			hw[m] = smoothstep(4.0, 8.0, Rj);
			CsmAt b = CsmSetup(j + 1, p, n, cosT, kb, zb, kt);
			Rj = w / (2.0 * b.t);
			pv[m] = CsmPcf(tC, b, min(Rj, 4.0), rot);
			m++;
		}
		float u = pv[m - 1];
		for (int k = m - 2; k >= 0; k--) u = mix(pv[k], u, hw[k + 1]);
		v = mix(v, u, hw[0]);
	}
	return v;
}

// sun visibility 0..1 at a camera-relative point; n the geometric normal; kb bias scale (3 where the legacy map draws), zb extra bias m, kt 1 for terrain
float CsmVis(sampler2D tC, vec3 posW, vec3 n, float kb, float zb, float kt)
{
	if (gCsm.vF.w < 0.5) return 1.0;
	float d = length(posW);
	if (d >= gCsm.vF.y) return 1.0;
	float cosT = dot(n, gCsm.vL.xyz);
	if (cosT < -0.05) return 1.0; // facing away: unlit already
	cosT = clamp(cosT, 0.0, 1.0);
	int i = 0;
	while (i < 3 && d >= gCsm.vC[i].z) i++;
	float bw = i < 3 ? smoothstep(gCsm.vC[i].w, gCsm.vC[i].z, d) : 0.0;
	float v;
	if (gCsm.vF.z >= 0.0) { // TAA: one cascade a pixel, the band dithered
		if (bw > CsmIGN(gl_FragCoord.yx + 2.7 * gCsm.vF.z)) i++;
		v = CsmCascade(tC, i, posW, n, cosT, kb, zb, kt);
	}
	else {
		v = CsmCascade(tC, i, posW, n, cosT, kb, zb, kt);
		if (bw > 0.0) v = mix(v, CsmCascade(tC, i + 1, posW, n, cosT, kb, zb, kt), bw);
	}
	for (int k = 4; k < 6; k++) if (gCsm.vC[k].z > 0.5) { // the target tiles: the camera's target and the nearest other vessel at their own texel, left out of the cascades, each whole shadow inside its tile
		vec2 u = (vec4(posW, 1.0) * gCsm.mC[k]).xy * CsmA - CsmOrgT(k);
		if (all(greaterThan(u, vec2(0.0))) && all(lessThan(u, vec2(2048.0)))) v = min(v, CsmCascade(tC, k, posW, n, cosT, kb, zb, kt));
	}
	return mix(1.0, v, 1.0 - smoothstep(gCsm.vF.x, gCsm.vF.y, d));
}
#endif
