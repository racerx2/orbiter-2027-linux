// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: height and valley fog (design-height-fog, Rev 2); FogMath.cpp mirrors the density, the step layout and the drift noise
// Hillaire 2015 (step integral), Wrenninge et al. 2013 (octaves), Henyey-Greenstein 1941, Jimenez 2014 (IGN); written from the papers

#include "Scatter.glsl"

layout(binding = 2, row_major, scalar) uniform FogPS	// Scatter.glsl's Const is binding 1
{
vec4	vCamX;		// camera axes, geocentric (xyz); tan of the half aperture (w)
vec4	vCamY;
vec4	vCamZ;		// forward, the TAA jitter folded in
vec4	vNE;		// near map east, north, up at its centre (geocentric); E.w 1/half, N.w 1 when valid, U.w body radius
vec4	vNN;
vec4	vNU;
vec4	vFE;		// far map
vec4	vFN;
vec4	vFU;
vec4	vPE;		// near map, previous; N.w its weight
vec4	vPN;
vec4	vPU;
vec4	vQE;		// far map, previous; N.w its weight
vec4	vQN;
vec4	vQU;
vec4	vCE;		// cloud coverage map (cloud frame); E.w 1/half
vec4	vCN;
vec4	vCU;
vec4	vCld;		// deck radius, coverage weight (0 none), shadow gain, mean coverage
vec4	vPlA;		// planet-local axes (noise); A.w the drift offset along x (m)
vec4	vPlB;
vec4	vPlC;
vec4	vFog;		// sf, sh, D0, kv (FogMath::Params)
vec4	vFog2;		// Hinv, Hh, ss, Ds
vec4	vFog3;		// ground beyond the far map, wF, volume cloud weight, 1 with the camera above the deck
vec4	vLight;		// 0, sun gain, ambient gain, terrain shadow weight
vec4	vShell;		// r_in, r_out, body radius, camera altitude
vec4	vMarch;		// steps, frame (-1: static offset), max distance, the main pass's near plane for the planet
vec4	vTex;		// 1/w, 1/h of SceneDepth; 1/w, 1/h of the march buffer
};

layout(binding = 13) uniform sampler2D tDepthF;	// camera distance, 0 none
layout(binding = 14) uniform sampler2D tFogN;	// near map: F, V, Hs, m; row 0 north
layout(binding = 15) uniform sampler2D tFogF;	// far map
layout(binding = 16) uniform sampler2D tFogNp;	// near map, previous
layout(binding = 17) uniform sampler2D tFogFp;	// far map, previous
layout(binding = 18) uniform sampler2D tCldF;	// cloud coverage, row 0 north
layout(binding = 19) uniform sampler2D tFH;	// march result

const int SCAN = 16;
const int MAXP = SCAN + 9;	// FogMath::MAXP
const float RECR = 100e3;	// inside SurfTile::Render's 150e3 depthTiles cut (Surfmgr2.cpp) and the far map's >= 130 km: the ground met before this is in tDepth

vec3 PixelRay(vec2 uv)
{
	return normalize(vCamX.xyz * ((uv.x * 2.0 - 1.0) * vCamX.w) + vCamY.xyz * ((1.0 - uv.y * 2.0) * vCamY.w) + vCamZ.xyz);
}

vec2 FullUV(vec2 q) { return (floor(q / vTex.xy - 0.25) + 0.5) * vTex.xy; }	// the SceneDepth texel a half resolution texel stands for

vec2 SphereHit(vec3 o, vec3 d, float r)	// near and far t; x > y: missed
{
	float b = dot(o, d), c = dot(o, o) - r * r, h = b * b - c;
	if (h < 0.0) return vec2(1e30, -1e30);
	h = sqrt(h);
	return vec2(-b - h, -b + h);
}

float HG(float c, float g) { float g2 = g * g; return (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * c, 1.5); }	// 4 pi times Henyey-Greenstein

float DepthW(float a, float b)
{
	if (a <= 0.0 && b <= 0.0) return 1.0;
	if (a <= 0.0 || b <= 0.0) return 0.02;
	return exp(-abs(a - b) / (0.02 * a + 1.0));
}

float IGN(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

// maps -----------------------------------------------------------------

vec2 MapUV(vec3 p, vec4 E, vec4 N, vec4 U, out float r)	// gnomonic; r: Chebyshev radius over the half side
{
	vec2 q = vec2(dot(p, E.xyz), dot(p, N.xyz)) / dot(p, U.xyz) * (U.w * E.w);
	r = max(abs(q.x), abs(q.y));
	return vec2(q.x, -q.y) * 0.5 + 0.5;
}

vec4 Fields(vec3 p, out float ef)	// F, V, Hs, m at p; ef the far map's edge weight (FogState::Field)
{
	float r, rq;
	vec4 f = textureLod(tFogF, MapUV(p, vFE, vFN, vFU, r), 0.0);
	ef = 1.0 - smoothstep(0.85, 1.0, r);
	if (vQN.w > 0.0) { vec2 u = MapUV(p, vQE, vQN, vQU, rq); if (rq < 1.0) f = mix(f, textureLod(tFogFp, u, 0.0), vQN.w); }
	if (vNN.w > 0.0) {
		vec2 un = MapUV(p, vNE, vNN, vNU, r);
		float wn = 1.0 - smoothstep(0.80, 0.95, r);
		if (wn > 0.0) {
			vec4 n = textureLod(tFogN, un, 0.0);
			if (vPN.w > 0.0) { vec2 u = MapUV(p, vPE, vPN, vPU, rq); if (rq < 1.0) n = mix(n, textureLod(tFogNp, u, 0.0), vPN.w); }
			wn *= clamp(1.0 + n.w, 0.0, 1.0);	// uncovered near texels (m = -1) give way to the far map
			f = mix(f, n, wn);
		}
	}
	return f;
}

float CldCov(vec3 p, out float e)
{
	float r;
	vec2 uv = MapUV(p, vCE, vCN, vCU, r);
	e = 1.0 - smoothstep(0.85, 1.0, r);
	return r < 1.0 ? textureLod(tCldF, uv, 0.0).r : 0.0;
}

float CloudShadow(vec3 p, vec3 s)	// the 2D deck's coverage where the sun ray leaves the deck sphere
{
	if (vCld.y <= 0.0) return 1.0;
	vec2 hb = SphereHit(p, s, vCld.x);
	if (hb.x > hb.y || hb.y <= 0.0) return 1.0;
	float e;
	float c = CldCov(p + s * hb.y, e);
	return mix(1.0, exp(-vCld.z * c), vCld.y * e);
}

// density (FogMath::DensitySeg, Bound, SunDepth) ------------------------

float FogHash(ivec3 c)	// Teschner et al. 2003 primes, Murmur3 finaliser
{
	uint h = uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u;
	h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
	return float(h) * (1.0 / 4294967296.0);
}

float Drift(vec3 p)	// value noise, 700 m cells on planet-local metres, periodic over 64 cells along x
{
	vec3 q = vec3(dot(p, vPlA.xyz) + vPlA.w, dot(p, vPlB.xyz), dot(p, vPlC.xyz)) / 700.0;
	vec3 i = floor(q), fr = q - i, u = fr * fr * (3.0 - 2.0 * fr);
	ivec3 k = ivec3(i);
	float r = 0.0;
	for (int c = 0; c < 8; c++) {
		ivec3 o = ivec3(c & 1, (c >> 1) & 1, c >> 2);
		vec3 w = mix(1.0 - u, u, vec3(o));
		r += w.x * w.y * w.z * FogHash(ivec3((k.x + o.x) & 63, k.y + o.y, k.z + o.z));
	}
	return r;
}

float LidDepth(vec4 f) { return (vFog.z * (0.3 + 0.7 * smoothstep(30.0, 300.0, f.y)) + vFog.w * min(f.y, 1200.0)) * sqrt(max(f.w, 0.0)); }	// shallow on flat ground
const float SEAENC = 16.0;	// FogMath::SEAENC: the open-water weight rides in F
float SeaCut(float s) { return 1.0 - smoothstep(1.25, 1.6, s); }	// a floor far below sea level is a dry basin
float SeaW(vec4 f, float ef) { float s = -f.x / SEAENC; return smoothstep(0.15, 0.55, s) * SeaCut(s) * ef; }
float SeaKL(vec4 f) { float s = -f.x / SEAENC; return smoothstep(0.25, 0.9, s) * SeaCut(s); }	// the sea bank's height: full over open water, low at the shore
float SeaLidAt(float kl, float n) { return vFog2.w * kl * (1.0 + 0.3 * (2.0 * n - 1.0)); }
float SeaSoft(float kl) { return 0.2 * vFog2.w * kl + 5.0; }
float SInt(float u) { return u <= 0.0 ? 0.0 : (u >= 1.0 ? u - 0.5 : u * u * u - 0.5 * u * u * u * u); }

float LidMean(float c, float w, float ha, float hb)	// mean of 1 - smoothstep(c - w, c + w, h), h linear from ha to hb
{
	float dh = hb - ha, lo = c - w, w2 = 2.0 * w;
	if (abs(dh) < 0.05 * w) { float u = clamp((0.5 * (ha + hb) - lo) / w2, 0.0, 1.0); return 1.0 - u * u * (3.0 - 2.0 * u); }
	float ga = ha - w2 * SInt((ha - lo) / w2), gb = hb - w2 * SInt((hb - lo) / w2);
	return max(0.0, (gb - ga) / dh);
}

float ExpMean(float g, float H, float ha, float hb)	// mean of exp(-max(h - g, 0) / H), h linear from ha to hb
{
	float a = (ha - g) / H, b = (hb - g) / H;
	if (a > b) { float t = a; a = b; b = t; }
	if (b - a < 1e-3) return exp(-max(0.5 * (a + b), 0.0));
	float I = 0.0;
	if (a < 0.0) I += min(b, 0.0) - a;
	if (b > 0.0) I += exp(-max(a, 0.0)) - exp(-b);
	return I / (b - a);
}

float DensitySeg(vec4 f, float ef, float ha, float hb, float n)
{
	float m = max(f.w, 0.0), hm = 0.5 * (ha + hb);
	float D = LidDepth(f), L = f.x + D + 0.25 * D * (2.0 * n - 1.0), Dl = 0.15 * D + 8.0;
	float rf = vFog.x * m * LidMean(L, Dl, ha, hb) * (0.6 + 0.8 * n) * ef;
	float G = mix(vFog3.x, f.x + f.y, ef);
	float rh = vFog.y * ExpMean(G, vFog2.y, ha, hb) * (1.0 - smoothstep(vFog2.x - 150.0, vFog2.x + 150.0, hm - G));
	float sw = SeaW(f, ef), kl = SeaKL(f), Ls = SeaLidAt(kl, n), Dls = SeaSoft(kl);
	float rs = vFog2.z * sw * LidMean(Ls, Dls, ha, hb) * (0.5 + n);
	return rf + rh + rs;
}

float Bound(vec4 f, float ef, float ha, float hb)	// the layout weight
{
	float hm = 0.5 * (ha + hb), hmin = min(ha, hb);
	float rf = hm < f.x + 1.55 * LidDepth(f) + 16.0 ? vFog.x * max(f.w, 0.0) * 1.4 * ef : 0.0;
	float G = mix(vFog3.x, f.x + f.y, ef);
	float rh = hmin - G < vFog2.x + 150.0 ? vFog.y * ExpMean(G, vFog2.y, ha, hb) : 0.0;
	float rs = hm < 1.5 * vFog2.w + 5.0 ? vFog2.z * SeaW(f, ef) * 1.5 : 0.0;
	return rf + rh + rs;
}

float BoundLid(vec4 f) { return (f.w > 0.0 && vFog.x > 0.0) ? f.x + 1.55 * LidDepth(f) + 16.0 : -1e30; }
float SeaLid(vec4 f, float ef) { return (vFog2.z > 0.0 && SeaW(f, ef) > 0.0) ? 1.5 * vFog2.w + 5.0 : -1e30; }

float ExpBelow(float c, float k, float ha, float hb) { return k > 1e-9 ? ExpMean(-c, 1.0 / k, -ha, -hb) : 1.0; }	// mean of exp(-k max(c - h, 0)), h linear from ha to hb

// the step's light, as means over its altitude range (no jitter in it): sun with Wrenninge octaves through flat layers, terrain and cloud shadow, sky
vec3 StepLight(vec4 f, float ef, float n, float ha, float hb, vec3 pm, vec3 s, vec3 ph, vec3 amb)
{
	float hm = 0.5 * (ha + hb), m = max(f.w, 0.0);
	float D = LidDepth(f), L = f.x + D + 0.25 * D * (2.0 * n - 1.0), Dl = 0.15 * D + 8.0;
	float sw = SeaW(f, ef), kl = SeaKL(f), Ls = SeaLidAt(kl, n), Dls = SeaSoft(kl);
	float A = vFog.x * m * ef, B = vFog2.z * sw, G = mix(vFog3.x, f.x + f.y, ef);
	float mus = dot(normalize(pm), s), im = 1.0 / max(mus, 0.08);
	float hz = vFog.y * vFog2.y * exp(-max(hm - G, 0.0) / vFog2.y) * im;
	float ms = 0.0, k = 1.0;
	for (int i = 0; i < 3; i++) {
		ms += k * ph[i] * exp(-k * hz) * ExpBelow(L + Dl, 0.8 * k * A * im, ha, hb) * ExpBelow(Ls + Dls, 0.8 * k * B * im, ha, hb);
		k *= 0.5;
	}
	float Vt = mix(1.0, 1.0 - LidMean(f.z, 30.0, ha, hb), ef * vLight.w);
	vec3 Cs = GetSunColor(mus, hm - Const.MinAlt) * Const.cSun * Const.Clouds * vLight.y;
	float occ = 0.35 + 0.65 * ExpBelow(L, 0.5 * A, ha, hb) * ExpBelow(Ls, 0.5 * B, ha, hb);
	return Cs * (Vt * CloudShadow(pm, s) * ms) + amb * occ;
}

// the step layout (FogMath::MakeLayout, Steps) ---------------------------

float lt[MAXP];	// layout points
float lw[MAXP];	// interval weights, then cumulative
int ln, extra;	// points, inserted points (at most MAXP - SCAN - 1)
float tauB, total, rayMu;

float RayAlt(float t)	// altitude over the body radius along the view ray, float-stable
{
	float Rc = vShell.z + vShell.w, a = 2.0 * t * Rc * rayMu + t * t;
	return vShell.w + a / (sqrt(max(Rc * Rc + a, 0.0)) + Rc);
}

void PushI(float ta, float ha, vec4 fa, float efa, float tb, float hb, vec4 fb, float efb)
{
	float len = tb - ta;
	float rb = max(Bound(fa, efa, ha, hb), Bound(fb, efb, ha, hb));
	lt[ln] = ta;
	lw[ln] = (1.0 - exp(-rb * len)) * exp(-0.5 * tauB) + 0.02 * len / max(total, 1e-6);
	ln++;
	tauB += rb * len;
}

void SplitI(float ta, float ha, vec4 fa, float efa, float tb, float hb, vec4 fb, float efb)	// at the crossings of the bound lids
{
	float tc0 = 1e30, tc1 = 1e30;
	float ga = ha - BoundLid(fa), gb = hb - BoundLid(fb);
	if ((ga < 0.0) != (gb < 0.0) && abs(ga) < 1e20 && abs(gb) < 1e20 && extra < MAXP - SCAN - 1) { tc0 = ta + (tb - ta) * ga / (ga - gb); extra++; }
	ga = ha - SeaLid(fa, efa); gb = hb - SeaLid(fb, efb);
	if ((ga < 0.0) != (gb < 0.0) && abs(ga) < 1e20 && abs(gb) < 1e20 && extra < MAXP - SCAN - 1) { tc1 = ta + (tb - ta) * ga / (ga - gb); extra++; }
	if (tc1 < tc0) { float t = tc0; tc0 = tc1; tc1 = t; }
	float pt = ta, ph = ha, pe = efa;
	vec4 pf = fa;
	for (int i = 0; i < 2; i++) {
		float tc = i == 0 ? tc0 : tc1;
		if (tc <= pt || tc >= tb) continue;
		float x = (tc - ta) / (tb - ta);
		vec4 cf = mix(fa, fb, x);
		float ce = mix(efa, efb, x), ch = RayAlt(tc);
		PushI(pt, ph, pf, pe, tc, ch, cf, ce);
		pt = tc; ph = ch; pf = cf; pe = ce;
	}
	PushI(pt, ph, pf, pe, tb, hb, fb, efb);
}

void MakeLayout(vec3 o, vec3 d, float t0, float t1)
{
	float SA = clamp(0.1 * (t1 - t0), 30.0, 2000.0);
	float s0 = asinh(t0 / SA), s1 = asinh(t1 / SA);
	float tp = -(vShell.z + vShell.w) * rayMu;	// the ray's lowest point
	ln = 0; extra = 0; tauB = 0.0; total = t1 - t0;
	float ta = t0, ha = RayAlt(t0), efa;
	vec4 fa = Fields(o + d * t0, efa);
	for (int k = 1; k <= SCAN; k++) {
		float tb = k == SCAN ? t1 : SA * sinh(s0 + (s1 - s0) * float(k) / float(SCAN)), efb;
		vec4 fb = Fields(o + d * tb, efb);
		float hb = RayAlt(tb);
		if (tp > ta && tp < tb && extra < MAXP - SCAN - 1) {
			extra++;
			float x = (tp - ta) / (tb - ta);
			vec4 cf = mix(fa, fb, x);
			float ce = mix(efa, efb, x), ch = RayAlt(tp);
			SplitI(ta, ha, fa, efa, tp, ch, cf, ce);
			SplitI(tp, ch, cf, ce, tb, hb, fb, efb);
		}
		else SplitI(ta, ha, fa, efa, tb, hb, fb, efb);
		ta = tb; ha = hb; fa = fb; efa = efb;
	}
	lt[ln] = t1; lw[ln] = 0.0; ln++;
	float acc = 0.0;
	for (int k = 0; k < MAXP; k++) {
		if (k >= ln) break;
		float w = max(lw[k], 0.0);
		lw[k] = acc;
		acc += w;
	}
}

// the first stretch of the ray inside the fog shell, stopped by the surface, the planet or the distance limit
bool Shell(vec3 o, vec3 d, float zd, out float t0, out float t1)
{
	vec2 ho = SphereHit(o, d, vShell.y), hi = SphereHit(o, d, vShell.x);
	t0 = 0.0; t1 = 0.0;
	if (ho.x > ho.y || ho.y <= 0.0) return false;
	t0 = max(ho.x, 0.5);
	t1 = ho.y;
	if (dot(o, o) < vShell.x * vShell.x) t0 = max(hi.y, 0.5);
	else if (hi.x <= hi.y && hi.x > 0.0) t1 = min(t1, hi.x);
	if (zd > 0.0) t1 = min(t1, zd);
	else if (hi.x <= hi.y && hi.x > 0.0 && hi.x < RECR) t1 = min(t1, vMarch.w);	// no surface, yet the ground lies in the recorded range: the main pass's near plane clipped it
	t1 = min(t1, vMarch.z);
	return t1 > t0;
}

float fogDist = 0.0;	// transmittance-weighted fog distance, the plume composite's (FogDist)

vec4 PSFogMarch(float x, float y)
{
	vec2 uv = vec2(x, y);
	float zd = texture(tDepthF, FullUV(uv)).r;
	vec3 d = PixelRay(uv), o = Const.CamPos;
	float t0, t1;
	if (!Shell(o, d, zd, t0, t1)) return vec4(0.0);
	rayMu = dot(d, normalize(o));
	MakeLayout(o, d, t0, t1);

	vec3 s = Const.toSun;
	float c = dot(d, s);
	vec3 ph = vec3(0.75 * HG(c, 0.7) + 0.25 * HG(c, -0.2), 0.75 * HG(c, 0.35) + 0.25 * HG(c, -0.1), 0.75 * HG(c, 0.175) + 0.25 * HG(c, -0.05));
	vec3 amb = GetAmbient(normalize(o + d * (0.5 * (t0 + t1)))) * vLight.z;
	float jit = vMarch.y >= 0.0 ? fract(IGN(gl_FragCoord.xy) + 0.618034 * vMarch.y) : IGN(gl_FragCoord.xy);

	int N = int(vMarch.x);
	float W = lw[ln - 1];
	int k = 0;
	float ea = lt[0], ha = RayAlt(ea);
	vec3 Lr = vec3(0.0);
	float T = 1.0, tw = 0.0, ts = 0.0;
	for (int i = 0; i < 32; i++) {
		if (i >= N || T < 0.01) break;
		float eb;
		if (i == N - 1) eb = lt[ln - 1];
		else {
			float target = W * float(i + 1) / float(N);
			while (k < ln - 2 && lw[k + 1] < target) k++;
			float span = lw[k + 1] - lw[k];
			float f = span > 0.0 ? clamp((target - lw[k]) / span, 0.0, 1.0) : 0.0;
			eb = lt[k] + (lt[k + 1] - lt[k]) * f;
		}
		float dt = eb - ea, hb = RayAlt(eb);
		if (dt > 0.0) {
			vec3 p = o + d * (ea + dt * jit);	// the fields and the noise at the jittered point, the rest per step
			float ef;
			vec4 f = Fields(p, ef);
			float n = Drift(p);
			float rho = DensitySeg(f, ef, ha, hb, n);
			if (rho * dt > 1e-6) {
				float t = ea + 0.5 * dt;
				vec3 S = StepLight(f, ef, n, ha, hb, o + d * t, s, ph, amb);
				float a = 1.0 - exp(-rho * dt);
				Lr += T * a * S;
				tw += T * a * t;
				ts += T * a;
				T *= 1.0 - a;
			}
		}
		ea = eb; ha = hb;
	}
	float alpha = 1.0 - T;
	if (alpha < 1e-3) return vec4(0.0);
	fogDist = tw / max(ts, 1e-6);
	float kDeck = 1.0;	// the 2D cloud deck between a camera above it and the fog
	if (vFog3.w > 0.0) {
		vec2 hd = SphereHit(o, d, vCld.x);
		if (hd.x <= hd.y && hd.x > 0.0 && fogDist > hd.x) {
			float e;
			float cv = mix(vCld.w, CldCov(o + d * hd.x, e), e);
			kDeck = 1.0 - 0.8 * cv * (1.0 - vFog3.z * (1.0 - smoothstep(35e3, 60e3, hd.x)));
		}
	}
	vec3 col = Lr / alpha;
	vec3 pm = o + d * fogDist;	// haze to the fog's mean distance, as TerrainPS
	LandOut sct = GetLandView(length(pm), normalize(pm));
	col = col * sct.atn.rgb + sct.ray.rgb * RayPhase(c) + sct.mie.rgb * MiePhase(c);
	float a = alpha * vFog3.y * kDeck;
	return vec4(HDR(col) * a, a);
}

// 3x3 depth-weighted, as VolBlur
vec4 PSFogBlur(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d0 = texture(tDepthF, FullUV(uv)).r;
	vec4 s = vec4(0.0);
	float ws = 0.0;
	for (int j = -1; j <= 1; j++)
		for (int i = -1; i <= 1; i++) {
			vec2 q = uv + vec2(i, j) * vTex.zw;
			float w = ((i == 0 && j == 0) ? 2.0 : 1.0) * DepthW(d0, texture(tDepthF, FullUV(q)).r);
			s += texture(tFH, q) * w;
			ws += w;
		}
	return s / ws;
}

// depth-aware 2x2 up-sample, as VolComposite; over the scene with SRC_ALPHA, INV_SRC_ALPHA
vec4 PSFogComposite(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d = texture(tDepthF, uv).r;
	vec2 c = (floor(uv / vTex.zw - 0.5) + 0.5) * vTex.zw;
	vec4 s = vec4(0.0);
	float ws = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec2 q = c + vec2(i, j) * vTex.zw;
			vec2 f = 1.0 - abs(uv - q) / vTex.zw;
			float w = max(f.x * f.y, 1e-3) * DepthW(d, texture(tDepthF, FullUV(q)).r);
			s += texture(tFH, q) * w;
			ws += w;
		}
	vec4 v = s / ws;
	if (v.a < 1e-3) return vec4(0.0);
	return vec4(v.rgb / v.a, v.a);
}

#if defined(PS_PSFogMarch) || defined(PS_PSFogBlur) || defined(PS_PSFogComposite)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_PSFogMarch
layout(location = 1) out vec4 oDist;	// the fog distance (R32F), FogDist
void main() { oColor = PSFogMarch(iX, iY); oDist = vec4(fogDist); }
#endif
#ifdef PS_PSFogBlur
void main() { oColor = PSFogBlur(iX, iY); }
#endif
#ifdef PS_PSFogComposite
void main() { oColor = PSFogComposite(iX, iY); }
#endif
