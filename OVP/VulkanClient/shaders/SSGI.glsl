
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space global illumination, one diffuse bounce from the lit opaque scene (design-ssgi, Rev 2)
// slices and their measure after Jimenez et al. 2016 (GTAO), visibility bitmask after Therrien et al. 2023, on-screen senders after Ritschel et al. 2009
// octahedral normals after Cigolle et al. 2014, pixel noise after Jimenez 2014 (IGN); SsgiMath.cpp holds the same maths with tests

#include "Scatter.glsl"

layout(binding = 2, row_major, scalar) uniform GiPS	// Scatter.glsl's Const is binding 1
{
vec4	vCamX;		// camera axes, global (xyz); tan of the half apertures (vCamX.w, vCamY.w); the TAA jitter folded into vCamZ
vec4	vCamY;
vec4	vCamZ;
vec4	vTex;		// 1/W, 1/H of SceneDepth; 1/w, 1/h of the half-res buffers
vec4	vRad;		// R0 m, R per m of distance, min and max radius (half-res px)
vec4	vFade;		// fade start, fade end (m), frame % 64, atmosphere (1) or not (0)
vec4	vCurve;		// TrGamma, terrain s (Expo / e, TrExpo / e without air), e, sender cap uMax
vec4	vAmbC;		// config ambient (rgb, light units), building twilight factor tw_bld (w)
vec4	vMisc;		// strength, tree s (their Expo / e), spare, half-res height px
vec4	vProbe;		// probe-lit hulls: gain from a sender's display value to what the probe holds, Metalness (x), PbrMesh (y); the probe draws terrain and trees (z), bases (w)
vec4	vTemp;		// alpha at rest, alpha moving, reset (1), e now / e last frame
vec4	vDir;		// blur step (uv), TAA flag valid (z)
mat4	mRigP;		// camera-relative point now to camera-relative a frame back, static in the proxy body's frame
mat4	mRigV;		// the same, rigid with the camera target vessel
mat4	mPrevVP;	// last frame's view projection, unjittered
};

layout(binding = 13) uniform sampler2D tDepthG;	// camera distance, 0 none
layout(binding = 14) uniform sampler2D tNrmG;	// oct normal (rg), class (a)
layout(binding = 15) uniform sampler2D tAlbG;	// albedo / 4 (rgb), family (a)
layout(binding = 16) uniform sampler2D tScnG;	// the lit opaque scene
layout(binding = 17) uniform sampler2D tSrc0;	// sender radiance times coverage (rgb), coverage (a): half, quarter, eighth res
layout(binding = 18) uniform sampler2D tSrc1;
layout(binding = 19) uniform sampler2D tSrc2;
layout(binding = 20) uniform sampler2D tGiG;	// irradiance, half res
layout(binding = 21) uniform sampler2D tHistG;	// irradiance (rgb), distance (a), half res
layout(binding = 22) uniform sampler2D tFlagG;	// TAA: the camera target vessel

#define GPI 3.14159265
#define NSLICE 2
#define NSTEP 8

float Max3G(vec3 v) { return max(max(v.r, v.g), v.b); }
float IGN(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }
vec2 SnapF(vec2 uv) { return (floor(uv / vTex.xy) + 0.5) * vTex.xy; }	// the full-res texel, as SSAO's Snap
float DistF(vec2 uv) { return textureLod(tDepthG, uv, 0.0).r; }
vec3 PixelRay(vec2 uv) { return normalize(vCamX.xyz * ((uv.x * 2.0 - 1.0) * vCamX.w) + vCamY.xyz * ((1.0 - uv.y * 2.0) * vCamY.w) + vCamZ.xyz); }
vec3 OctDec(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-n.z, 0.0);
	n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
	return normalize(n);
}
int ClassOf(float a) { return int(a * 3.0 + 0.5); }
vec3 Fin(vec3 v) { return vec3(isnan(v.x) || isinf(v.x) ? 0.0 : v.x, isnan(v.y) || isinf(v.y) ? 0.0 : v.y, isnan(v.z) || isinf(v.z) ? 0.0 : v.z); }

// display curves (SsgiMath.cpp)
vec3 TwF(vec3 u)
{
#if defined(_FILMIC)
	vec3 x = u * 0.568;
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
#else
	return 1.0 - exp(-u);
#endif
}
vec3 TwI(vec3 y)
{
	y = clamp(y, 0.0, 0.98);
#if defined(_FILMIC)
	vec3 a = 2.51 - 2.43 * y, b = 0.03 - 0.59 * y, c = -0.14 * y;
	return (-b + sqrt(max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a) / 0.568;
#else
	return -log(1.0 - y);
#endif
}
vec3 TmF(vec3 u)
{
#if defined(_LIGHTGLOW)
	return u;
#else
	vec3 h = u * u;
	return u * pow(1.0 + h * h, vec3(-0.25));
#endif
}
vec3 TmI(vec3 y)
{
#if defined(_LIGHTGLOW)
	return y;
#else
	vec3 h = y * y;
	return y * pow(max(1.0 - h * h, vec3(1e-4)), vec3(-0.25));
#endif
}
vec3 TaF(vec3 u) { return pow(clamp(u, 0.0, 1.0), vec3(vCurve.x)); }
vec3 TaI(vec3 y) { return pow(clamp(y, 0.0, 1.0), vec3(1.0 / vCurve.x)); }
vec3 S2L(vec3 c) { c = max(c, vec3(0.0)); return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }
vec3 L2S(vec3 c) { c = max(c, vec3(0.0)); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }
vec3 LFX(vec3 x) { return 2.0 * x / (1.0 + x); }
vec3 LFXSq(vec3 S) { S = max(S, vec3(0.0)); return 1.8 * sqrt(S) / sqrt(2.0 + S); }

// the surface at Pg (geocentric) seen along dir from distance d: land transmittance A and raw inscatter Iraw (NewPlanet), objects' A and display inscatter Io (GetObjectAtmoParams, 1 and 0 within 1 km)
void AtmoAt(vec3 Pg, vec3 dir, float d, out vec3 A, out vec3 Iraw, out vec3 Ao, out vec3 Io)
{
	A = Ao = vec3(1.0);
	Iraw = Io = vec3(0.0);
	if (vFade.w < 0.5) return;
	float r = length(Pg);
	LandOut s = GetLandView(r, Pg / r);
	float c = dot(dir, Const.toSun);
	A = s.atn.rgb;
	Iraw = s.ray.rgb * RayPhase(c) + s.mie.rgb * MiePhase(c);
	float k = smoothstep(800.0, 1200.0, d);
	Ao = mix(vec3(1.0), A, k);
	Io = k * (1.0 - exp(-Const.Expo * Iraw));
}

// sunlight at Pg, the brightest channel at most 1 (NewPlanet's cSF)
vec3 SunAt(vec3 Pg)
{
	if (vFade.w < 0.5) return vec3(1.0);
	float r = length(Pg);
	vec3 c = GetSunColor(dot(Pg / r, Const.toSun), r - Const.PlanetRad) * Const.cSun;
	float m = Max3G(c);
	return m > 1.0 ? c / m : c;
}

// NewPlanet's terrain ambient (fM 0.5), light units
vec3 TerrAmb(vec3 Pg)
{
	if (vFade.w < 0.5) return vec3(0.0);
	float r = length(Pg), alt = r - Const.PlanetRad;
	vec3 up = Pg / r;
	vec4 cAmb = AmbientApprox(up, false);
	vec3 cA = normalize(cAmb.rgb + SunAt(Pg) * 4.0) * cAmb.a * cAmb.g * 0.5 * exp(-alt * Const.iH.r) * Const.rmI.r * 6e5 * Const.TW_Terrain;
	return max(cA, vec3(0.0)) + Const.cAmbient * Const.Ambient;
}

// GetObjectAtmoParams' sky ambient of meshes and trees (VPlanetAtmo.cpp), light units, the config level in it
vec3 MeshAmb(vec3 Pg)
{
	if (vFade.w < 0.5) return vAmbC.rgb;
	float r = length(Pg), alt = r - Const.PlanetRad;
	if (alt > Const.AtmoAlt * 0.5) return vAmbC.rgb;
	float dNS = dot(Pg / r, Const.toSun);
	float fA = (1.0 - smoothstep(0.0, Const.TW_Dst, -dNS)) * vAmbC.w;
	vec2 rm = Gauss7(-dNS, r, RayLength(-dNS, r), Const.iH) * Const.rmO;
	vec3 cs = exp(-(Const.RayWave * rm.x + Const.MieWave * rm.y)) * Const.cSun;
	float m = Max3G(cs);
	if (m > 1.0) cs /= m;
	return normalize(Const.cAmbient + cs * 2.0) * fA * Const.cAmbient.g * exp(-alt * Const.iH.x) * Const.rmI.x * 6e5 + vAmbC.rgb;
}

float QMax(int cls, int fam)
{
	if (cls == 1) return 0.98;
#if defined(_LIGHTGLOW)
	return vCurve.w;
#else
	return (fam == 1 || fam == 2) ? 0.98 : vCurve.w;
#endif
}

// sender radiance in exposed linear units (e x), per class and family (SsgiMath::SenderV)
vec3 SenderV(vec3 y, vec3 A, vec3 Iraw, vec3 Io, int cls, int fam)
{
	vec3 v;
	if (cls == 0) {
		if (vFade.w > 0.5) v = max(TwI(min(y, vec3(0.98))) - Const.Expo * Iraw, 0.0) / max(A, vec3(0.05)) / vCurve.y;
		else v = TaI(y) / vCurve.y;
	}
	else {
		vec3 q = clamp((y - Io) / max(A, vec3(0.05)), 0.0, QMax(cls, fam));
		if (cls == 1) v = TwI(q) / vMisc.y;
		else if (fam == 1 || fam == 2) v = TmI(q);
		else v = q;
	}
	return clamp(Fin(v), 0.0, vCurve.w);
}

// display increment of a receiver for the added irradiance E (exposed units), >= 0 (SsgiMath::CompositeDy)
vec3 CompositeDy(vec3 y, vec3 A, vec3 Iraw, vec3 Io, vec3 rho, vec3 E, int cls, int fam)
{
	float e = max(vCurve.z, 1e-6);
	vec3 rh = max(rho, vec3(1e-5)), dy;
	if (cls == 0) {
		if (vFade.w > 0.5) {
			vec3 u = TwI(min(y, vec3(0.98)));
			dy = TwF(u + A * vCurve.y * rho * E) - TwF(u);
		}
		else {
			vec3 v = TaI(y) / vCurve.y, x = v / e;
			vec3 r = min(x / rh, vec3(1.9)), L0 = r / (2.0 - r);
			vec3 dx = rho * (LFX(L0 + E / e) - LFX(L0));
			dy = TaF(vCurve.y * (v + e * dx)) - TaF(vCurve.y * v);
		}
	}
	else {
		vec3 Ac = max(A, vec3(0.05));
		vec3 q = clamp((y - Io) / Ac, 0.0, QMax(cls, fam));
		if (cls == 1) {
			vec3 u = TwI(q);
			dy = (TwF(u + vMisc.y * rho * E) - TwF(u)) * A;
		}
		else if (fam == 1) {
			vec3 v = TmI(q), L = min(v / e / rh, vec3(1.75));
			vec3 S = 2.0 * L * L / (3.24 - L * L), S1 = S + (E / e) * (E / e);
			vec3 v1 = v + e * rho * max(LFXSq(S1) - L, 0.0);
			dy = (TmF(v1) - TmF(v)) * A;
		}
		else if (fam == 2) {
			vec3 v = TmI(q), cl = S2L(v / e) + rho * S2L(E / e);
			dy = (TmF(e * L2S(cl)) - TmF(v)) * A;
		}
		else dy = min(rho * E, max(e * rho - q, 0.0)) * A;
	}
	return max(Fin(dy), 0.0);
}

// slice measure cos(t - g) |sin t| on [g - pi/2, g + pi/2], t from V (SsgiMath::SliceCdf); q: g, sin g, cos g, the mass below t = 0
float SliceCdf(float t, vec4 q)
{
	t = clamp(t, q.x - 0.5 * GPI, q.x + 0.5 * GPI);
	float k = 0.25 * cos(2.0 * t - q.x);
	return t <= 0.0 ? k - 0.5 * t * q.y + q.w - 0.25 * q.z : q.w - k + 0.5 * t * q.y + 0.25 * q.z;
}

uint Bits(float ta, float tb, vec4 q, float iM, float jit)	// the 32 sectors uniform in the CDF, the edges dithered per slice (unbiased)
{
	int i0 = clamp(int(floor(32.0 * SliceCdf(min(ta, tb), q) * iM + jit)), 0, 32);
	int i1 = clamp(int(floor(32.0 * SliceCdf(max(ta, tb), q) * iM + jit)), 0, 32);
	int n = i1 - i0;
	if (n <= 0) return 0u;
	uint m = n >= 32 ? 0xFFFFFFFFu : ((1u << uint(n)) - 1u);
	return m << uint(i0);
}

// ---------------------------------------------------------------------------------------------
// half res: the sender radiance of the 2x2 texels near the reference one's distance, capped by what their albedo and light allow (design-ssgi S5)
vec4 PSGiPrep(float x, float y)
{
	vec2 uv0 = SnapF(vec2(x, y));
	float d0 = DistF(uv0);
	if (d0 <= 0.0) return vec4(0.0);
	vec3 dir = PixelRay(uv0);
	vec3 Pg = Const.CamPos + dir * d0;
	vec3 A, Iraw, Ao, Io;
	AtmoAt(Pg, dir, d0, A, Iraw, Ao, Io);
	float e = vCurve.z, night = vFade.w > 0.5 ? clamp(-4.0 * dot(normalize(Pg), Const.toSun) + 0.05, 0.0, 1.0) : 0.0;
	vec3 sun = SunAt(Pg), aT = TerrAmb(Pg), aM = MeshAmb(Pg);
	vec3 sum = vec3(0.0);
	float n = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec2 q = uv0 - vec2(i, j) * vTex.xy;
			float dq = DistF(q);
			if (dq <= 0.0 || abs(dq - d0) > 0.02 * d0) continue;
			int cls = ClassOf(textureLod(tNrmG, q, 0.0).a);
			vec4 al = textureLod(tAlbG, q, 0.0);
			int fam = ClassOf(al.a);
			vec3 yq = textureLod(tScnG, q, 0.0).rgb;
			vec3 v = cls == 0 ? SenderV(yq, A, Iraw, Io, 0, 0) : SenderV(yq, Ao, Iraw, Io, cls, fam);
			vec3 rho = al.rgb * 4.0;
			if (fam == 2 && cls >= 2) rho = L2S(rho);	// PbrMesh albedo is linear, the senders sRGB-encoded
			vec3 cap = 1.5 * e * rho * (2.0 * sun + (cls == 0 ? aT : aM) + 0.5 * night);
			sum += min(v, cap);
			n += 1.0;
		}
	return n > 0.0 ? vec4(Fin(sum / n), 1.0) : vec4(0.0);
}

// half res: GTAO slices with a visibility bitmask, each newly covered sector adds its sender's excess over the ambient the shaders already give
vec4 PSGiMarch(float x, float y)
{
	vec2 uv0 = SnapF(vec2(x, y));
	float d = DistF(uv0);
	if (d <= 0.0 || d >= vFade.y) return vec4(0.0);
	vec3 ray = PixelRay(uv0), P = ray * d, V = -ray;
	vec4 nc = textureLod(tNrmG, uv0, 0.0);
	vec3 N = OctDec(nc.xy);
	float nv = dot(N, V);
	if (nv < 0.05) N = normalize(N + V * (0.05 - nv));	// silhouette normals bent toward the view (design-ssgi S7)
	int rc = ClassOf(nc.a), rf = ClassOf(textureLod(tAlbG, uv0, 0.0).a);
	vec3 Pg = Const.CamPos + P;
	vec3 amb = vCurve.z * (rc == 0 ? (vFade.w > 0.5 ? TerrAmb(Pg) : vec3(0.0)) : MeshAmb(Pg));
	float pk = rc == 3 ? (rf == 1 ? vProbe.x : (rf == 2 ? vProbe.y : 0.0)) * vCurve.z : 0.0;	// a probe-lit hull holds what its probe drew at pk times the display value (r3-gi2)
	float tanY = vCamY.w, H2 = vMisc.w;
	float R = max(vRad.x, vRad.y * d);	// the world radius; the falloff keeps it when the screen cap shortens the march (r3-gi2)
	float rpx = clamp(R * H2 / (2.0 * d * tanY), vRad.z, vRad.w);
	float bias = 0.003 * d + 0.02;
	float fr = vFade.z * 5.588238;
	float xr = IGN(gl_FragCoord.xy + fr), xs = IGN(gl_FragCoord.yx + 17.0 + fr), xb = IGN(gl_FragCoord.xy + 37.0 + fr);
	vec3 E = vec3(0.0);
	for (int k = 0; k < NSLICE; k++) {
		float phi = (float(k) + xr) * GPI / float(NSLICE);
		vec2 w = vec2(cos(phi), sin(phi));
		vec3 r1 = PixelRay(uv0 + w * vTex.zw);
		vec3 T = r1 - ray * dot(r1, ray);
		if (dot(T, T) < 1e-20) continue;
		T = normalize(T);
		vec3 Np = normalize(cross(V, T));
		vec3 np = N - Np * dot(N, Np);
		float lp = length(np);
		if (lp < 1e-4) continue;
		float g = clamp(atan(dot(np, T), dot(np, V)), -0.5 * GPI, 0.5 * GPI);
		float sg = sin(g), cg = cos(g);
		vec4 sq = vec4(g, sg, cg, 0.5 * cg + 0.5 * (g - 0.5 * GPI) * sg);
		float M = cg + g * sg, iM = 1.0 / M;
		float jit = fract(xb + float(k) * 0.6180339887);
		vec3 S = vec3(0.0);
		uint occ = 0u;	// one set of 32 sectors per slice, both sides
		for (int side = 0; side < 2; side++) {
			vec2 sd = side == 0 ? w : -w;
			for (int j = 0; j < NSTEP; j++) {
				float t = (float(j) + xs) / float(NSTEP);
				float o = rpx * t * t + 1.0;
				vec2 q = uv0 + sd * o * vTex.zw;
				if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0) break;
				vec2 qs = SnapF(q);
				float ds = DistF(qs);
				if (ds <= 0.0) continue;
				vec3 rs = PixelRay(qs), Sp = rs * ds, D = Sp - P;
				if (abs(dot(D, N)) < bias) continue;	// on the receiver's own plane
				vec4 sn = textureLod(tNrmG, qs, 0.0);
				int sc = ClassOf(sn.a);
				float th = sc == 0 ? 1e6 : (sc == 1 ? 4.0 : (sc == 2 ? max(1.0, 0.01 * d) : max(0.5, 0.005 * d)));
				vec3 B = Sp + rs * th - P;
				uint m = Bits(atan(dot(D, T), dot(D, V)), atan(dot(B, T), dot(B, V)), sq, iM, jit);
				uint nw = m & ~occ;
				occ |= m;
				if (nw == 0u) continue;
				float dl = length(D);
				float face = smoothstep(-0.05, 0.25, dot(OctDec(sn.xy), -D / dl));
				float fall = 1.0 - smoothstep(0.6 * R, R, dl);
				if (face * fall <= 0.0) continue;
				float sp = rpx * (2.0 * t + 1.0 / float(NSTEP)) / float(NSTEP);	// tap spacing, half-res px
				vec4 src = sp < 2.0 ? textureLod(tSrc0, qs, 0.0) : (sp < 4.0 ? textureLod(tSrc1, qs, 0.0) : textureLod(tSrc2, qs, 0.0));
				vec3 Ls = src.a > 0.05 ? src.rgb / src.a : vec3(0.0);
				vec3 aS = amb;
				if (pk > 0.0 && (sc <= 1 ? vProbe.z : (sc == 2 ? vProbe.w : 0.0)) > 0.5) aS = max(amb, pk * textureLod(tScnG, qs, 0.0).rgb);	// only the excess over what the probe holds
				S += float(bitCount(nw)) * (face * fall) * max(Ls - aS, 0.0);
			}
		}
		E += lp * M * S / 32.0;
	}
	return vec4(Fin(E / float(NSLICE)), 1.0);
}

// half res, 9 taps along vDir, depth and normal aware
vec4 PSGiBlur(float x, float y)
{
	vec2 uv = vec2(x, y);
	vec2 s0 = SnapF(uv);
	float d0 = DistF(s0);
	if (d0 <= 0.0) return vec4(0.0);
	vec3 n0 = OctDec(textureLod(tNrmG, s0, 0.0).xy);
	vec3 s = vec3(0.0);
	float ws = 0.0;
	for (int i = -4; i <= 4; i++) {
		vec2 q = uv + vDir.xy * float(i);
		vec2 qs = SnapF(q);
		float dq = DistF(qs);
		if (dq <= 0.0) continue;
		float nn = max(dot(n0, OctDec(textureLod(tNrmG, qs, 0.0).xy)), 0.0);
		nn *= nn; nn *= nn; nn *= nn;
		float w = exp(-float(i * i) / 8.0) * exp(-abs(dq - d0) / (0.02 * d0 + 0.05)) * nn;
		s += textureLod(tGiG, q, 0.0).rgb * w;
		ws += w;
	}
	return vec4(ws > 1e-6 ? s / ws : textureLod(tGiG, uv, 0.0).rgb, 1.0);
}

// half res: last frame's irradiance at this point's previous pixel when its distance agrees, rescaled by the exposure change
vec4 PSGiTemporal(float x, float y)
{
	vec2 uv = vec2(x, y);
	vec2 s0 = SnapF(uv);
	float d = DistF(s0);
	if (d <= 0.0) return vec4(0.0);
	vec3 cur = textureLod(tGiG, uv, 0.0).rgb;
	vec3 P = PixelRay(s0) * d;
	bool rig = vDir.z > 0.5 ? textureLod(tFlagG, s0, 0.0).r > 0.5 : ClassOf(textureLod(tNrmG, s0, 0.0).a) == 3;
	vec3 Q = (vec4(P, 1.0) * (rig ? mRigV : mRigP)).xyz;
	vec4 h = vec4(Q, 1.0) * mPrevVP;
	if (vTemp.z < 0.5 && h.w > 1e-3) {
		vec2 pu = vec2(h.x / h.w * 0.5 + 0.5, 0.5 - h.y / h.w * 0.5);
		if (pu.x >= 0.0 && pu.y >= 0.0 && pu.x <= 1.0 && pu.y <= 1.0) {
			vec4 hs = textureLod(tHistG, pu, 0.0);
			float dq = length(Q);
			if (hs.a > 0.0 && abs(hs.a - dq) < 0.03 * dq + 0.2) {
				float a = length((pu - uv) / vTex.zw) > 1.0 ? vTemp.y : vTemp.x;
				cur = mix(Fin(hs.rgb) * vTemp.w, cur, a);
			}
		}
	}
	return vec4(clamp(Fin(cur), 0.0, 4.0 * vCurve.w), d);
}

// full res, additive over the target: the 2x2 up-sample of the irradiance, through each receiver's own curve
vec4 PSGiComposite(float x, float y)
{
	vec2 uv = vec2(x, y);
	float d = DistF(uv);
	if (d <= 0.0 || d >= vFade.y) return vec4(0.0);
	vec4 al = textureLod(tAlbG, uv, 0.0);
	vec3 rho = al.rgb * 4.0;
	if (Max3G(rho) <= 1e-4) return vec4(0.0);
	vec4 nc = textureLod(tNrmG, uv, 0.0);
	vec3 n = OctDec(nc.xy);
	vec2 c = (floor(uv / vTex.zw - 0.5) + 0.5) * vTex.zw;
	vec3 s = vec3(0.0);
	float ws = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec2 q = c + vec2(i, j) * vTex.zw;
			vec2 qs = SnapF(q);
			float dq = DistF(qs);
			vec2 f = 1.0 - abs(uv - q) / vTex.zw;
			float nn = max(dot(n, OctDec(textureLod(tNrmG, qs, 0.0).xy)), 0.0);
			nn *= nn; nn *= nn; nn *= nn;
			float w = max(f.x * f.y, 1e-3) * (dq > 0.0 ? exp(-abs(dq - d) / (0.02 * d + 0.05)) : 1e-3) * max(nn, 1e-3);
			s += textureLod(tHistG, q, 0.0).rgb * w;
			ws += w;
		}
	vec3 E = clamp(Fin(s / ws), 0.0, 4.0 * vCurve.w) * vMisc.x * (1.0 - smoothstep(vFade.x, vFade.y, d));
	if (Max3G(E) <= 0.0) return vec4(0.0);
	vec3 dir = PixelRay(uv);
	vec3 A, Iraw, Ao, Io;
	AtmoAt(Const.CamPos + dir * d, dir, d, A, Iraw, Ao, Io);
	int cls = ClassOf(nc.a), fam = ClassOf(al.a);
	vec3 yv = textureLod(tScnG, uv, 0.0).rgb;
	vec3 dy = cls == 0 ? CompositeDy(yv, A, Iraw, Io, rho, E, 0, 0) : CompositeDy(yv, Ao, Iraw, Io, rho, E, cls, fam);
	return vec4(dy, 0.0);
}

#if defined(PS_PSGiPrep) || defined(PS_PSGiMarch) || defined(PS_PSGiBlur) || defined(PS_PSGiTemporal) || defined(PS_PSGiComposite)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_PSGiPrep
void main() { oColor = PSGiPrep(iX, iY); }
#endif
#ifdef PS_PSGiMarch
void main() { oColor = PSGiMarch(iX, iY); }
#endif
#ifdef PS_PSGiBlur
void main() { oColor = PSGiBlur(iX, iY); }
#endif
#ifdef PS_PSGiTemporal
void main() { oColor = PSGiTemporal(iX, iY); }
#endif
#ifdef PS_PSGiComposite
void main() { oColor = PSGiComposite(iX, iY); }
#endif
