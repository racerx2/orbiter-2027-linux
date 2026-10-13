// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: volumetric engine plumes, smoke puffs and re-entry plasma (design-volumetric-plumes, compiled only with VolPlumes = 1)
// Hillaire 2015 (step integral), Umenhoffer et al. 2006 (spherical billboards), Henyey and Greenstein 1941, Jimenez 2014 (IGN), Kopf et al. 2007 (joint bilateral upsampling), Lees 1955 (Newtonian windward weight)

#define NPL 32		// plumes
#define NPZ 4		// plasma vessels
#define PLS 12		// vec4 per plume
#define PZS 10		// vec4 per plasma vessel
#define CL_HYDROLOX 0
#define CL_TEXTURE 5

#if defined(VS_PuffVS) || defined(PS_PuffPS)
layout(binding = 0, row_major, scalar) uniform PuffVSB	// uniform extern: the puff vertex shader
{
mat4	mVP;		// camera-relative world to clip, unjittered
};
layout(binding = 1, row_major, scalar) uniform PuffPSB	// the puff pixel shader
{
vec4	pCamX;		// camera axes, tan of the half apertures in w
vec4	pCamY;
vec4	pCamZ;
vec4	pTex;		// 1/W, 1/H full res; 1/w, 1/h of the puff target
vec4	pSun;		// toward the sun; time s
vec4	pUp;		// local up; flags: 1 fog, 2 cloud distance, 4 cloud shell
vec4	pCloud;		// planet centre (camera-relative), cloud base radius
vec4	pCloud2;	// cloud top radius
};
#else
layout(binding = 1, row_major, scalar) uniform PlumePS	// uniform extern: pixel shader constants (IPI.glsl's are binding 0)
{
vec4	vCamX;		// camera axes, tan of the half apertures in w
vec4	vCamY;
vec4	vCamZ;
vec4	vTex;		// 1/W, 1/H full res; 1/w, 1/h half res
vec4	vFrame;		// time s, plumes, plasma vessels, flags: 1 fog, 2 cloud distance, 4 cloud shell
vec4	vSun;		// toward the sun, camera-relative; near distance
vec4	vCloud;		// planet centre (camera-relative), cloud base radius
vec4	vCloud2;	// cloud top radius
vec4	vBake;		// atlas bake: mode (0 flame strip, 1 flare, 2 mean colour)
vec4	vFlow;		// flow prep: near, far, 2 x eye distance, texel m; reduce and blur: source size, block, direction
vec4	vFlow2;
vec4	vPal[24];	// per class: core rgb, k_L; diamond rgb, disk gain; soot, scatter, albedo, k_d; tail rgb, tail density
vec4	vPl[NPL * PLS];	// per plume, see PostPlume::FillPlume
vec4	vPz[NPZ * PZS];	// per plasma vessel, see PostPlume::FillPlasma
};
#endif

layout(binding = 4) uniform sampler2D tDepthP;	// SceneFx camera distance, 0 none, full res
layout(binding = 5) uniform sampler3D tNoiseP;	// 32^3: R Perlin-Worley, G B A Worley
layout(binding = 6) uniform sampler2D tAtlasP;	// 16 slots of 32x64: flame strip, flare, mean colour
layout(binding = 7) uniform sampler2D tFlowP0;	// zs, zb (m from the front eye), coverage, (n.f)^2
layout(binding = 8) uniform sampler2D tFlowP1;
layout(binding = 9) uniform sampler2D tFlowB0;	// blurred coverage, zs x coverage, base, w x coverage
layout(binding = 10) uniform sampler2D tFlowB1;
layout(binding = 11) uniform sampler2D tFlowR0;	// 1x1: zs min, zb max, covered texels, texel of the minimum
layout(binding = 12) uniform sampler2D tFlowR1;
layout(binding = 13) uniform sampler2D tVolP;	// march: premultiplied, half res
layout(binding = 14) uniform sampler2D tVolPD;	// march: plume distance, scene distance (min of 4)
layout(binding = 15) uniform sampler2D tPuffBk;	// puffs behind the plumes
layout(binding = 16) uniform sampler2D tPuffFr;	// puffs in front
layout(binding = 17) uniform sampler2D tCloudV;	// volume clouds, premultiplied, half res
layout(binding = 18) uniform sampler2D tCloudD;	// volume cloud distance, half res
layout(binding = 19) uniform sampler2D tPuffData;	// 6 texels per puff, 64 puffs per row
layout(binding = 20) uniform sampler2D tSrcP;	// bake and flow inputs
layout(binding = 21) uniform sampler2D tSrc2P;
layout(binding = 22) uniform sampler2D tFogP;	// height fog, half res: in-scatter premultiplied, opacity

const float PI = 3.14159265;

float Ss (float a, float b, float x) { float t = clamp((x - a) / (b - a), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); } // either edge order
float Sq (float x) { return x * x; }
float Lum (vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float IGN (vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); } // static per pixel, no frame term
float HG4 (float c, float g) { float g2 = g * g; return (1.0 - g2) / pow(max(1e-4, 1.0 + g2 - 2.0 * g * c), 1.5); } // 4 pi HG

vec3 SoftCurve (vec3 x)	// the vessels' curve (Metalness.fx)
{
	vec3 h = x * x;
	return x * pow(1.0 + h * h, vec3(-0.25));
}

vec3 LitCurve (vec3 x)	// lit smoke: the planet's curve with Filmic, else the vessels'
{
#if defined(_FILMIC)
	x *= 0.568;
	return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
#else
	return SoftCurve(x);
#endif
}

vec2 SphereT (vec3 d, vec3 c, float r)	// origin 0: entry and exit, x > y none
{
	float b = dot(d, c), h = b * b - dot(c, c) + r * r;
	if (h < 0.0) return vec2(1e30, -1e30);
	h = sqrt(h);
	return vec2(b - h, b + h);
}

vec2 CapsuleT (vec3 d, vec3 pa, vec3 pb, float r)	// origin 0: entry and exit of the capsule pa-pb
{
	vec2 s1 = SphereT(d, pa, r), s2 = SphereT(d, pb, r);
	vec2 t = vec2(min(s1.x, s2.x), max(s1.y, s2.y));
	vec3 ba = pb - pa;
	float L = length(ba);
	if (L < 1e-6) return t;
	vec3 u = ba / L, oc = -pa;
	float du = dot(d, u), ou = dot(oc, u);
	vec3 dp = d - u * du, op = oc - u * ou;
	float A = dot(dp, dp), B = dot(dp, op), C = dot(op, op) - r * r;
	vec2 c = vec2(1e30, -1e30);
	if (A > 1e-9) {
		float h = B * B - A * C;
		if (h >= 0.0) { h = sqrt(h); c = vec2((-B - h) / A, (-B + h) / A); }
	}
	else if (C <= 0.0) c = vec2(-1e30, 1e30);
	if (abs(du) > 1e-9) { // the slab between the end planes
		float s0 = (0.0 - ou) / du, s1b = (L - ou) / du;
		c = vec2(max(c.x, min(s0, s1b)), min(c.y, max(s0, s1b)));
	}
	else if (ou < 0.0 || ou > L) c = vec2(1e30, -1e30);
	if (c.x <= c.y) t = vec2(min(t.x, c.x), max(t.y, c.y));
	return t;
}

#if !defined(VS_PuffVS) && !defined(PS_PuffPS)

vec3 PixelRay (vec2 uv)
{
	return normalize(vCamX.xyz * ((uv.x * 2.0 - 1.0) * vCamX.w) + vCamY.xyz * ((1.0 - uv.y * 2.0) * vCamY.w) + vCamZ.xyz);
}

float SceneDist (vec2 hp)	// the nearest of the 2x2 full-res distances under a half-res pixel; none is far
{
	ivec2 b = ivec2(hp) * 2, m = textureSize(tDepthP, 0) - 1;
	float d = 1e30;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			float z = texelFetch(tDepthP, min(b + ivec2(i, j), m), 0).r;
			d = min(d, z > 0.0 ? z : 1e30);
		}
	return d;
}

float CloudShell (vec3 d)	// distance to the volume cloud slab along the ray, 1e30 none
{
	vec2 a = SphereT(d, vCloud.xyz, vCloud2.x), b = SphereT(d, vCloud.xyz, vCloud.w);
	float r0 = length(vCloud.xyz);
	if (r0 > vCloud2.x) return a.x <= a.y && a.x > 0.0 ? a.x : 1e30;
	if (r0 < vCloud.w) return b.y > 0.0 && b.x <= b.y ? b.y : 1e30;
	return 0.0;
}

// ---------------------------------------------------------------------------------------------
// plume field

vec3 AtlasUV (float slot, vec2 uv, int part)	// part 0 flame strip, 1 flare
{
	vec2 o = vec2(slot * 32.0 + (part == 0 ? 0.5 : 16.5), 0.5);
	vec2 s = part == 0 ? vec2(15.0, 63.0) : vec2(15.0, 15.0);
	return vec3((o + clamp(uv, 0.0, 1.0) * s) / vec2(512.0, 64.0), 0.0);
}

// emission, extinction and lit source of plume i at p (camera-relative); rd: the view ray
void PlumeAt (int i, vec3 p, vec3 rd, float time, inout vec3 E, inout float S, inout vec3 Q)
{
	int k = i * PLS;
	vec4 P0 = vPl[k], P1 = vPl[k + 1], P2 = vPl[k + 2], P3 = vPl[k + 3], P4 = vPl[k + 4], P5 = vPl[k + 5], P6 = vPl[k + 6], P7 = vPl[k + 7];
	vec4 P10 = vPl[k + 10], P11 = vPl[k + 11];
	vec3 q = p - P0.xyz, a = P1.xyz;
	float re = P0.w, L = P1.w, x = dot(q, a);
	vec3 rv = q - a * x;
	float r = length(rv);
	int cls = int(P3.x + 0.5);
	float I = P3.z, watm = P3.w;
	vec4 C0 = vPal[cls * 4], C1 = vPal[cls * 4 + 1], C2 = vPal[cls * 4 + 2], C3 = vPal[cls * 4 + 3];
	float env = P5.z * re, xg = P5.w;
	float h = 1e9;
	if (dot(P4.xyz, P4.xyz) > 0.5) { h = dot(P4.xyz, p) + P4.w; if (h < 0.0) return; }

	// jet in air, radius in metres
	float xs = max(x, 0.0) / re;
	float R;
	if (cls == CL_TEXTURE) R = re * (1.0 + 0.15 * max(x, 0.0) / L);
	else if (P2.z > 0.0) {
		float Ls = P2.z, rj = P2.x;
		R = re * (rj + (1.0 - rj) * exp(-xs / (0.5 * Ls)) + 0.06 * rj * sin(2.0 * PI * xs / Ls) * exp(-xs / (4.0 * Ls)) + 0.094 * max(0.0, xs - 4.0 * Ls));
	}
	else R = re * (1.0 + 0.094 * xs);
	R += env;
	float ax = x < 0.0 ? 0.0 : 1.0 - Ss(L * 0.85, L * 1.15, x);	// the visible length
	if (xg < 1e8) ax *= 1.0 - Ss(xg, xg + 0.3 * P2.x * re + 0.05, x);	// stops at the ground
	float rhoJ = ax * exp(-Sq(Sq(r / R)));

	// vacuum plume (Simons)
	float rhoV = 0.0, th = 0.0, dd = length(q);
	if (watm < 0.999) {
		th = acos(clamp(x / max(dd, 1e-4), -1.0, 1.0));
		float c = cos(min(PI * th / (2.0 * P2.y), 0.5 * PI));
		float c2 = c * c, c4 = c2 * c2;
		rhoV = c4 * c4 * c2 * Sq(1.0 / (1.0 + dd / re)) * (1.0 - Ss(L * 0.6, L, dd));	// cos^10: gamma 1.2
	}

	// turbulence, the wall clock
	float sx = 0.8 * re + 0.25 * max(x, 0.0);
	vec3 b1 = normalize(cross(a, abs(a.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0))), b2 = cross(a, b1);
	vec3 nc = vec3(x - 25.0 * re * time, dot(q, b1), dot(q, b2)) / sx + P7.w;
	vec4 n1 = textureLod(tNoiseP, nc * 0.11, 0.0);
	float nn = n1.r * 0.65 + textureLod(tNoiseP, nc * 0.29 + 0.37, 0.0).g * 0.35;
	float wN = Ss(0.15 * L, 0.5 * L, x) * Ss(0.25, 0.9, r / R) + 0.25;
	float rho = mix(rhoV, rhoJ, watm);
	rho = mix(rho, rho * clamp((nn - (1.0 - rho) * 0.6) / 0.4, 0.0, 1.0), clamp(wN, 0.0, 1.0));

	float fall = exp(-max(x, 0.0) / (C0.w * L));
	vec3 core = C0.rgb;
	float wj = Ss(0.3, 0.7, watm);	// jet against vacuum mapping, smooth through the ascent
	float norm = mix(1.0 / (2.0 * re), 1.0 / (1.8 * R), wj);
	if (cls == CL_TEXTURE && P3.y >= 0.0) {	// the add-on's flame texture, legacy uv
		vec4 t = vec4(0.0);
		if (wj > 0.0) t += wj * textureLod(tAtlasP, AtlasUV(P3.y, vec2(0.5 + 0.5 * r / R, x / L), 0).xy, 0.0);
		if (wj < 1.0) t += (1.0 - wj) * textureLod(tAtlasP, AtlasUV(P3.y, vec2(0.5 + 0.5 * min(th / (0.5 * P2.y), 1.0), min(dd / L, 1.0)), 0).xy, 0.0);
		core = t.rgb * t.a * 2.0;
		fall = 1.0;
		S += 0.6 * t.a * rho * I * norm;
	}
	E += core * (I * rho * fall * norm);

	// shock diamonds and the Mach disk
	if (watm > 0.01 && P2.z > 0.0 && cls != CL_TEXTURE) {
		float Ls = P2.z, rj = P2.x;
		float u = 2.0 * fract(xs / Ls) - 1.0;
		float dia = P5.x * watm * exp(-xs / (4.0 * Ls)) * Ss(0.9, 0.4, abs(u) + r / R) * ax;
		E += C1.rgb * (I * dia / (1.8 * R));
		if (P2.w > 0.0) {
			float md = exp(-Sq((xs - P2.w) / (0.06 * rj))) * Ss(0.6, 0.3, r / (rj * re));
			E += C1.rgb * (I * watm * C1.w * md * 0.5 / (0.12 * rj * re));
		}
	}
	// nozzle glow seen from behind
	float back = Sq(Sq(max(0.0, -dot(rd, a))));
	if (back > 0.0 && cls != CL_TEXTURE) E += (C0.rgb + C1.rgb) * (2.0 * I * back * exp(-Sq(x / (0.15 * re))) * Ss(1.0, 0.7, r / re) / (0.27 * re));

	// soot and scattering inside the jet
	float soot = C2.x * (0.3 + 0.7 * Ss(0.5, 1.0, r / R)) * rho / (1.8 * R);
	float scat = C2.y * rho / (1.8 * R);
	vec3 sunC = P10.rgb, amb = P11.rgb;
	float cs = dot(rd, vSun.xyz);
	S += (soot + scat) * min(I * 4.0, 1.0);
	Q += (sunC * HG4(cs, 0.5) * 0.5 + amb) * (C2.z * scat * min(I * 4.0, 1.0));

	// ground: the wall jet spreads the flame along the plane
	if (xg < 1e8 && h < 1e8) {
		vec3 gp = P0.xyz + a * xg;
		vec3 dg = p - gp;
		float s = length(dg - P4.xyz * dot(dg, P4.xyz));
		float rjm = P2.x * re;
		float hw = rjm * (0.5 + 0.073 * s / rjm), Rw = 0.8 * max(L - xg, 0.0) + 2.0 * rjm;
		float w = exp(-max(h, 0.0) / hw) * exp(-s / Rw) * exp(-xg / (C0.w * L)) * watm;
		E += core * (I * w / (2.0 * hw)) * 0.6;
		S += (C2.x + C2.y) * w / (2.0 * hw) * 0.5;
	}

	// smoke tail beyond the visible flame, in air
	if (P5.y > 0.0 && x > L * 0.7 && watm > 0.01) {
		float Rl = re * 1.2 + 0.094 * max(x - L, 0.0) + env;
		float xt = max(x - L, 0.0);
		float dens = P5.y * watm * exp(-xt / L) * (1.0 - Ss(3.0 * L, 4.0 * L, x)) * Ss(0.7 * L, L, x);
		float rt = exp(-Sq(r / Rl)) * mix(1.0, nn * 1.6, 0.6);
		float st = C3.w * dens * rt / (1.8 * Rl);
		vec3 sp = vSun.xyz - a * dot(vSun.xyz, a);
		float spl = length(sp);
		float tex = Rl;
		if (spl > 1e-3) { vec3 sn = sp / spl; float b = dot(rv, sn), cc = dot(rv, rv) - Rl * Rl, D = b * b - cc; tex = D > 0.0 ? (max(-b + sqrt(D), 0.0) - max(-b - sqrt(D), 0.0)) / spl : 0.0; } // the chord inside the tail toward the sun
		float tsun = exp(-C3.w * dens * tex / (1.8 * Rl));
		S += st;
		Q += C3.rgb * st * (sunC * HG4(cs, 0.5) * tsun + amb);
	}
}

// ---------------------------------------------------------------------------------------------
// re-entry plasma: shock layer on the windward hull from a flow-aligned depth map, halo, base wake

vec4 FlowP (int m, vec2 uv)	// bilinear over the covered texels only; z: the coverage
{
	vec2 f = uv * 128.0 - 0.5;
	ivec2 i0 = ivec2(floor(f));
	vec2 w = f - floor(f);
	vec4 s = vec4(0.0);
	float ws = 0.0, cv = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			ivec2 p = clamp(i0 + ivec2(i, j), ivec2(0), ivec2(127));
			vec4 t = m == 0 ? texelFetch(tFlowP0, p, 0) : texelFetch(tFlowP1, p, 0);
			float b = (i == 0 ? 1.0 - w.x : w.x) * (j == 0 ? 1.0 - w.y : w.y);
			cv += b * t.z;
			if (t.z > 0.5) { s += t * b; ws += b; }
		}
	if (ws <= 0.0) return vec4(0.0);
	s /= ws;
	s.z = cv;
	return s;
}
vec4 FlowB (int m, vec2 uv) { return m == 0 ? textureLod(tFlowB0, uv, 0.0) : textureLod(tFlowB1, uv, 0.0); }
vec4 FlowR (int m) { return m == 0 ? texelFetch(tFlowR0, ivec2(0), 0) : texelFetch(tFlowR1, ivec2(0), 0); }

struct Surf { float zs, zb, cov, w, z; vec2 uv; };

Surf FlowSurf (int k, vec3 p)	// the windward surface on the line through p along the flow
{
	int b = k * PZS;
	vec4 Z0 = vPz[b], Z1 = vPz[b + 1], Z2 = vPz[b + 2], Z3 = vPz[b + 3], Z7 = vPz[b + 7];
	Surf s;
	vec3 q = p - Z3.xyz;
	s.z = dot(q, Z0.xyz) + Z2.w;
	if (Z0.w >= 0.0) {
		s.uv = vec2(0.5 + 0.5 * dot(q, Z1.xyz) / Z1.w, 0.5 - 0.5 * dot(q, Z2.xyz) / Z1.w);
		vec4 f = FlowP(int(Z0.w + 0.5), s.uv);
		s.zs = f.x; s.zb = f.y; s.cov = f.z; s.w = f.w;
		if (any(lessThan(s.uv, vec2(0.0))) || any(greaterThan(s.uv, vec2(1.0)))) s.cov = 0.0;
	}
	else {	// ellipsoid: rows of the scaled vessel frame
		mat3 M = mat3(Z1.xyz, Z2.xyz, Z7.xyz);
		vec3 qe = q * M, g = Z0.xyz * M;
		float gg = dot(g, g), bq = dot(qe, g), c = dot(qe, qe) - 1.0, h = bq * bq - gg * c;
		s.uv = vec2(0.5);
		if (h < 0.0) { s.cov = 0.0; s.zs = s.zb = 0.0; s.w = 0.0; return s; }
		h = sqrt(h);
		float t0 = (-bq - h) / gg, t1 = (-bq + h) / gg;
		s.zs = s.z + t0; s.zb = s.z + t1; s.cov = 1.0;
		vec3 n = normalize(M * (qe + g * t0));
		s.w = Sq(max(0.0, -dot(n, Z0.xyz)));
	}
	return s;
}

vec3 PlasmaColour (int k, float w) { vec4 Z4 = vPz[k * PZS + 4]; return mix(Z4.rgb, vec3(1.0), 0.25 * Sq(Sq(w))); }

struct PzState { float zmin, zbmax, Lb, Rn, lfront, texel; vec2 stag; };	// the band is normalised to unit layer thickness [V], so no lambda

PzState PlasmaState (int k)
{
	int b = k * PZS;
	vec4 Z0 = vPz[b], Z1 = vPz[b + 1], Z3 = vPz[b + 3], Z5 = vPz[b + 5], Z6 = vPz[b + 6], Z7 = vPz[b + 7], Z4 = vPz[b + 4];
	PzState st;
	if (Z0.w >= 0.0) {
		vec4 r = FlowR(int(Z0.w + 0.5));
		st.zmin = r.x; st.zbmax = r.y;
		float af = r.z * Sq(Z7.w);
		st.Rn = clamp(0.25 * sqrt(af / PI), 0.3, 3.0);
		st.stag = (vec2(mod(r.w, 128.0), floor(r.w / 128.0)) + 0.5) / 128.0;
	}
	else {	// ellipsoid: the stagnation point on the axis
		Surf s = FlowSurf(k, Z3.xyz);
		st.zmin = s.zs; st.zbmax = s.zb; st.stag = vec2(0.5);
		st.Rn = clamp(0.25 * Z3.w, 0.3, 3.0);
	}
	st.Lb = max(st.zbmax - st.zmin, 0.1);
	st.lfront = st.Lb * (0.15 + 0.85 * Z4.w) + 0.1 * st.Lb * Z6.z;
	st.texel = Z7.w;
	return st;
}

float RunLength (int k, PzState st, Surf s) { return max(s.zs - st.zmin, 0.0) + length(s.uv - st.stag) * 2.0 * vPz[k * PZS + 1].w; }

vec3 BandAt (int k, PzState st, Surf s, vec3 rd)	// the shock layer crossed once: c I sqrt(w) S(l) front / |cos|
{
	vec4 Z0 = vPz[k * PZS], Z4 = vPz[k * PZS + 4], Z6 = vPz[k * PZS + 6];
	float l = RunLength(k, st, s);
	float lf = 0.1 * st.Rn;
	float Sl = pow(max(l, lf) / lf, -0.5);	// [U] heat.py: laminar s^-0.5
	float fr = Ss(st.lfront + 0.1 * st.Lb, st.lfront - 0.05 * st.Lb, l);
	float c = abs(dot(rd, Z0.xyz));
	return PlasmaColour(k, s.w) * (3.0 * Z4.w * Z6.w * sqrt(s.w) * Sl * fr * min(1.0 / max(c, 1e-3), 8.0));
}

void PlasmaAt (int k, PzState st, vec3 p, inout vec3 E)	// halo and base wake, per metre
{
	int b = k * PZS;
	vec4 Z0 = vPz[b], Z1 = vPz[b + 1], Z2 = vPz[b + 2], Z3 = vPz[b + 3], Z4 = vPz[b + 4], Z5 = vPz[b + 5], Z6 = vPz[b + 6];
	vec3 q = p - Z3.xyz;
	float z = dot(q, Z0.xyz) + Z2.w;
	vec4 fb;
	vec2 uv;
	if (Z0.w >= 0.0) {
		uv = vec2(0.5 + 0.5 * dot(q, Z1.xyz) / Z1.w, 0.5 - 0.5 * dot(q, Z2.xyz) / Z1.w);
		if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return;
		fb = FlowB(int(Z0.w + 0.5), uv);
	}
	else {
		Surf s = FlowSurf(k, p);
		float r = length(q - Z0.xyz * dot(q, Z0.xyz));
		float cv = Ss(1.6 * Z3.w, 0.5 * Z3.w, r);
		fb = vec4(cv, s.cov > 0.0 ? s.zs * cv : (st.zmin + 0.3 * st.Lb) * cv, 0.0, cv * 0.5);
		uv = vec2(0.5);
	}
	float flick = Z6.w;
	if (fb.x > 1e-3) {	// halo
		float zs = fb.y / fb.x, w = fb.w / fb.x;
		float lh = Z5.z;
		E += PlasmaColour(k, w) * (0.3 * Z4.w * flick * fb.x * w * exp(-abs(z - zs) / lh) / (2.0 * lh));
	}
	float dz = z - st.zbmax;
	if (dz > 0.0 && Z5.w > 0.0) {	// base wake, widening with the Mach angle
		float Db = Z6.x > 0.0 ? Z6.x : st.Lb * 0.3;
		vec2 uw = 0.5 + (uv - 0.5) / (1.0 + dz * Z6.y / max(0.5 * Db, 0.1));
		float base = Z0.w >= 0.0 ? FlowB(int(Z0.w + 0.5), uw).z : Ss(0.6 * Db, 0.2 * Db, length(q - Z0.xyz * dot(q, Z0.xyz)) / (1.0 + dz * Z6.y / max(0.5 * Db, 0.1)));
		E += PlasmaColour(k, 0.2) * (Z5.w * Z4.w * flick * base * exp(-dz / Db) / Db);
	}
}

// ---------------------------------------------------------------------------------------------
// the march: half res, every plume and plasma bound on the ray, front to back

vec2 primDist = vec2(0.0);	// MRT 1: transmittance-weighted distance, scene distance

vec4 PSPlumeMarch (float x, float y)
{
	vec2 hp = gl_FragCoord.xy;
	vec2 uv = hp * vTex.zw;
	vec3 rd = PixelRay(uv);
	float dsc = SceneDist(hp);
	primDist = vec2(0.0, dsc);
	float tnear = vSun.w, time = vFrame.x;
	int np = int(vFrame.y + 0.5), nz = int(vFrame.z + 0.5);

	const int MH = 8;
	float h0[MH], h1[MH], hs[MH];
	int hid[MH];
	int nh = 0;
	for (int i = 0; i < NPL + NPZ; i++) {
		bool pl = i < NPL;
		if (pl && i >= np) continue;
		if (!pl && i - NPL >= nz) break;
		vec4 A = pl ? vPl[i * PLS + 8] : vPz[(i - NPL) * PZS + 8];
		vec4 B = pl ? vPl[i * PLS + 9] : vPz[(i - NPL) * PZS + 9];
		vec2 t = CapsuleT(rd, A.xyz, B.xyz, A.w);
		t.x = max(t.x, tnear);
		t.y = min(t.y, dsc);
		if (t.y <= t.x) continue;
		int j;	// keep the nearest MH by entry
		if (nh < MH) j = nh++;
		else { if (t.x >= h0[MH - 1]) continue; j = MH - 1; }
		while (j > 0 && h0[j - 1] > t.x) { h0[j] = h0[j - 1]; h1[j] = h1[j - 1]; hs[j] = hs[j - 1]; hid[j] = hid[j - 1]; j--; }
		h0[j] = t.x; h1[j] = t.y; hs[j] = B.w; hid[j] = i;
	}
	if (nh == 0) return vec4(0.0);

	float fogT = 1.0;
	vec3 fogC = vec3(0.0);
	if ((int(vFrame.w) & 1) != 0) { vec4 f = textureLod(tFogP, uv, 0.0); fogC = f.a > 1e-3 ? f.rgb / f.a : vec3(0.0); }

	float bnd[2 * MH];
	int nb = 0;
	for (int i = 0; i < nh; i++) { bnd[nb++] = h0[i]; bnd[nb++] = h1[i]; }
	for (int i = 1; i < nb; i++) { float v = bnd[i]; int j = i; while (j > 0 && bnd[j - 1] > v) { bnd[j] = bnd[j - 1]; j--; } bnd[j] = v; }

	float want[2 * MH];
	float total = 0.0;
	for (int s = 0; s + 1 < nb; s++) {
		float a = bnd[s], b = bnd[s + 1], m = 0.5 * (a + b), st = 1e30;
		for (int i = 0; i < nh; i++) if (h0[i] <= m && m <= h1[i]) st = min(st, hs[i]);
		want[s] = (b - a < 1e-4 || st > 1e29) ? 0.0 : clamp((b - a) / st, 1.0, 24.0);
		total += want[s];
	}
	float scale = total > 48.0 ? 48.0 / total : 1.0;

	PzState pst[NPZ];
	float dprev[NPZ];
	bool crossed[NPZ];
	for (int k = 0; k < NPZ; k++) { dprev[k] = 1e30; crossed[k] = false; if (k < nz) pst[k] = PlasmaState(k); }

	float jit = IGN(hp);
	vec3 L = vec3(0.0);
	float T = 1.0, tw = 0.0, ts = 0.0;
	for (int s = 0; s + 1 < nb; s++) {
		if (want[s] <= 0.0 || T < 0.01) continue;
		float a = bnd[s], b = bnd[s + 1];
		int n = int(max(1.0, floor(want[s] * scale + 0.5)));
		float dt = (b - a) / float(n);
		float m = 0.5 * (a + b);
		for (int k2 = 0; k2 < 24; k2++) {
			if (k2 >= n || T < 0.01) break;
			float t = a + (float(k2) + jit) * dt;
			vec3 p = rd * t;
			vec3 E = vec3(0.0), Q = vec3(0.0);
			float S = 0.0;
			vec3 Tr = vec3(0.0), In = vec3(0.0);
			float wsum = 0.0;
			for (int i = 0; i < nh; i++) {
				if (!(h0[i] <= m && m <= h1[i])) continue;
				int id = hid[i];
				if (id < NPL) {
					vec3 e0 = E; float s0 = S;
					PlumeAt(id, p, rd, time, E, S, Q);
					float wi = Lum(E - e0) + (S - s0) + 1e-6;
					vec4 P6 = vPl[id * PLS + 6], P7 = vPl[id * PLS + 7];
					Tr += P6.rgb * P6.w * wi; In += (P7.rgb * P6.w + fogC * (1.0 - P6.w)) * wi; wsum += wi;
				}
				else {
					int k = id - NPL;
					PlasmaAt(k, pst[k], p, E);
					Surf sf = FlowSurf(k, p);
					float dk = sf.cov > 0.5 ? sf.zs - sf.z : 1e30;
					if (dprev[k] < 1e29 && dk < 1e29 && (dprev[k] > 0.0) != (dk > 0.0)) {	// crossed the windward surface
						float f = dprev[k] / (dprev[k] - dk);
						vec3 pc = rd * (t - dt * (1.0 - f));
						Surf sc = FlowSurf(k, pc);
						if (sc.cov > 0.5) { L += T * BandAt(k, pst[k], sc, rd) * vPz[k * PZS + 5].x; crossed[k] = true; }	// x: aerial transmission
					}
					dprev[k] = dk;
					float wi = 1e-3;
					Tr += vec3(vPz[k * PZS + 5].x) * wi; wsum += wi;
				}
			}
			if (wsum <= 0.0) continue;
			Tr /= wsum; In /= wsum;
			float ext = S * dt;
			float tr = exp(-ext);
			vec3 src = E * Tr + Q * Tr;
			L += T * (ext > 1e-4 ? src * (1.0 - tr) / S : src * dt) + T * (1.0 - tr) * In;
			float wgt = T * (Lum(E) * dt + (1.0 - tr));
			tw += wgt * t; ts += wgt;
			T *= tr;
		}
	}
	for (int k = 0; k < NPZ; k++) {	// the ray ends on the windward hull (design review S2)
		if (k >= nz || crossed[k] || dsc > 1e29) continue;
		vec3 pe = rd * dsc;
		vec4 A = vPz[k * PZS + 8], B = vPz[k * PZS + 9];
		vec2 tt = CapsuleT(rd, A.xyz, B.xyz, A.w);
		if (dsc < tt.x || dsc > tt.y + 1.0) continue;
		Surf s = FlowSurf(k, pe);
		if (s.cov < 0.5) continue;
		float tol = 1.5 * pst[k].texel / sqrt(max(s.w, 0.01)) + dsc * 2.0 * vCamY.w * vTex.y;	// plus one full-res pixel at that distance
		if (abs(s.zs - s.z) < tol) { L += T * BandAt(k, pst[k], s, rd) * vPz[k * PZS + 5].x; tw += 1e-3 * dsc; ts += 1e-3; }
	}
	float alpha = 1.0 - T;
	if (Lum(L) < 1e-5 && alpha < 1e-4) return vec4(0.0);
	primDist.x = ts > 0.0 ? tw / ts : h0[0];
	if ((int(vFrame.w) & 6) != 0) {	// a volume cloud in front of the plume hides it
		float cd = (int(vFrame.w) & 2) != 0 ? textureLod(tCloudD, uv, 0.0).r : CloudShell(rd);
		vec4 cv = textureLod(tCloudV, uv, 0.0);
		if (cd > 0.0 && cd < primDist.x) { L *= 1.0 - cv.a; alpha *= 1.0 - cv.a; }
	}
#if !defined(_LIGHTGLOW)
	L = SoftCurve(L);
#endif
	return vec4(L, alpha);
}

float DistW (float a, float b) { return exp(-abs(a - b) / (0.03 * min(a, b) + 0.5)); }

vec4 PSPlumeBlur (float x, float y)
{
	vec2 uv = gl_FragCoord.xy * vTex.zw;
	vec2 d0 = textureLod(tVolPD, uv, 0.0).rg;
	vec4 s = vec4(0.0);
	float ws = 0.0;
	for (int j = -1; j <= 1; j++)
		for (int i = -1; i <= 1; i++) {
			vec2 q = uv + vec2(i, j) * vTex.zw;
			vec2 d = textureLod(tVolPD, q, 0.0).rg;
			vec4 c = textureLod(tVolP, q, 0.0);
			float w = ((i == 0 && j == 0) ? 2.0 : 1.0) * DistW(d0.y, d.y) * (d.x > 0.0 && d0.x > 0.0 ? DistW(d0.x, d.x) : 1.0);
			s += c * w;
			ws += w;
		}
	return s / ws;
}

vec4 PSPlumeComposite (float x, float y)
{
	vec2 uv = gl_FragCoord.xy * vTex.xy;
	float d = texelFetch(tDepthP, ivec2(gl_FragCoord.xy), 0).r;
	d = d > 0.0 ? d : 1e30;
	vec2 c = (floor(uv / vTex.zw - 0.5) + 0.5) * vTex.zw;
	vec4 V = vec4(0.0), F = vec4(0.0), B = vec4(0.0);
	float ws = 0.0;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) {
			vec2 q = c + vec2(i, j) * vTex.zw;
			vec2 f = max(1.0 - abs(uv - q) / vTex.zw, 0.0);
			float dt = SceneDist(q / vTex.zw);	// the tap's half-res scene distance from tDepth itself: valid when the march is skipped (puffs only)
			float w = max(f.x * f.y, 1e-3) * (d > 1e29 && dt > 1e29 ? 1.0 : DistW(min(d, 1e7), min(dt, 1e7)));
			V += textureLod(tVolP, q, 0.0) * w;
			F += textureLod(tPuffFr, q, 0.0) * w;
			B += textureLod(tPuffBk, q, 0.0) * w;
			ws += w;
		}
	ws = max(ws, 1e-6);
	V /= ws; F /= ws; B /= ws;
	vec3 col = F.rgb + (1.0 - F.a) * (V.rgb + (1.0 - V.a) * B.rgb);
	float a = 1.0 - (1.0 - F.a) * (1.0 - V.a) * (1.0 - B.a);
	return vec4(col, a);
}

// ---------------------------------------------------------------------------------------------
// atlas bake: an exhaust or particle texture into its slot

vec4 PSAtlasBake (float x, float y)
{
	if (vBake.x < 0.5) return textureLod(tSrcP, vec2(mix(0.01, 0.24, x), y), 0.0);	// flame strip, legacy uv
	if (vBake.x < 1.5) return textureLod(tSrcP, vec2(mix(0.50390625, 0.99609375, x), mix(0.00390625, 0.49609375, y)), 0.0);	// flare
	return textureLod(tSrcP, vec2(0.5), 20.0);	// the mean, the smallest mip
}

// ---------------------------------------------------------------------------------------------
// flow maps: front and back depth (1 - z) to metres, coverage and the Newtonian weight

vec4 PSFlowPrep (float x, float y)
{
	ivec2 p = ivec2(gl_FragCoord.xy), m = textureSize(tSrcP, 0) - 1;
	float zn = vFlow.x, zf = vFlow.y;
	float sf = texelFetch(tSrcP, p, 0).r;
	if (sf <= 0.0) return vec4(zf, zf, 0.0, 0.0);
	float zs = zn + (1.0 - sf) * (zf - zn);
	float sb = texelFetch(tSrc2P, ivec2(m.x - p.x, p.y), 0).r;	// the back map looks the other way: u mirrored
	float zb = sb > 0.0 ? vFlow.z - (zn + (1.0 - sb) * (zf - zn)) : zs;
	vec2 g = vec2(0.0);
	for (int a = 0; a < 2; a++) {
		ivec2 o = a == 0 ? ivec2(1, 0) : ivec2(0, 1);
		float s1 = texelFetch(tSrcP, clamp(p + o, ivec2(0), m), 0).r, s0 = texelFetch(tSrcP, clamp(p - o, ivec2(0), m), 0).r;
		float z1 = s1 > 0.0 ? zn + (1.0 - s1) * (zf - zn) : zs + 1e3, z0 = s0 > 0.0 ? zn + (1.0 - s0) * (zf - zn) : zs + 1e3;
		g[a] = (abs(z1 - zs) < abs(z0 - zs) ? z1 - zs : zs - z0) / vFlow.w;
	}
	return vec4(zs, max(zb, zs), 1.0, 1.0 / (1.0 + dot(g, g)));	// [P] Lees 1955: (n.f)^2
}

vec4 PSFlowReduce (float x, float y)	// vFlow2.x 1: from the prepared map (blocks of vFlow2.y); 2: from the first level
{
	ivec2 o = ivec2(gl_FragCoord.xy) * int(vFlow2.y);
	int n = int(vFlow2.y);
	float zmin = 1e30, zmax = -1e30, cnt = 0.0, idx = 0.0;
	for (int j = 0; j < 16; j++) {
		if (j >= n) break;
		for (int i = 0; i < 16; i++) {
			if (i >= n) break;
			ivec2 p = o + ivec2(i, j);
			vec4 s = texelFetch(tSrcP, p, 0);
			if (vFlow2.x < 1.5) {
				if (s.z < 0.5) continue;
				if (s.x < zmin) { zmin = s.x; idx = float(p.y * 128 + p.x); }
				zmax = max(zmax, s.y);
				cnt += 1.0;
			}
			else {
				if (s.z < 0.5) continue;
				if (s.x < zmin) { zmin = s.x; idx = s.w; }
				zmax = max(zmax, s.y);
				cnt += s.z;
			}
		}
	}
	return vec4(zmin, zmax, cnt, idx);
}

vec4 PSFlowBlur (float x, float y)	// separable 9 taps of coverage, zs x coverage, base outline, w x coverage
{
	vec2 uv = vec2(x, y);
	vec4 r = texelFetch(tSrc2P, ivec2(0), 0);
	float lb = max(r.y - r.x, 0.1);
	const float wt[5] = float[5](0.2270, 0.1945, 0.1216, 0.0540, 0.0162);
	vec4 s = vec4(0.0);
	for (int i = -4; i <= 4; i++) {
		vec4 t = textureLod(tSrcP, uv + vFlow2.zw * float(i), 0.0);
		if (vFlow2.x < 1.5) t = t.z > 0.5 ? vec4(1.0, t.x, t.y > r.y - 0.1 * lb ? 1.0 : 0.0, t.w) : vec4(0.0);
		s += t * wt[abs(i)];
	}
	return s;
}

#endif

// ---------------------------------------------------------------------------------------------
// smoke and steam puffs: spherical billboards at half res (Umenhoffer et al. 2006)

#if defined(VS_PuffVS)
layout(location = 0) in vec4 iPos;	// puff index, corner x, corner y
layout(location = 0) flat out vec4 oA;
layout(location = 1) flat out vec4 oB;
layout(location = 2) flat out vec4 oC;
layout(location = 3) flat out vec4 oD;
layout(location = 4) flat out vec4 oE;
layout(location = 5) flat out vec4 oF;
void main()
{
	int i = int(iPos.x + 0.5);
	ivec2 b = ivec2((i % 64) * 6, i / 64);
	oA = texelFetch(tPuffData, b, 0);
	oB = texelFetch(tPuffData, b + ivec2(1, 0), 0);
	oC = texelFetch(tPuffData, b + ivec2(2, 0), 0);
	oD = texelFetch(tPuffData, b + ivec2(3, 0), 0);
	oE = texelFetch(tPuffData, b + ivec2(4, 0), 0);
	oF = texelFetch(tPuffData, b + ivec2(5, 0), 0);
	if (oF.w >= 0.0) {	// an add-on texture's mean colour tints the light
		vec3 al = texelFetch(tAtlasP, ivec2(int(oF.w + 0.5) * 32 + 24, 56), 0).rgb;
		al *= 1.0 / max(Lum(al), 0.05);
		oB.rgb *= al; oC.rgb *= al; oD.rgb *= al;
	}
	vec3 C = oA.xyz;
	float R = oA.w, D = length(C);
	if (D < 1.05 * R) { gl_Position = vec4(iPos.y, iPos.z, 0.5, 1.0); return; }	// the camera inside: the whole target
	vec3 v = C / D;
	vec3 r1 = normalize(cross(v, abs(v.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0))), u1 = cross(r1, v);
	float h = R / sqrt(1.0 - Sq(R / D));
	gl_Position = vec4(C + (r1 * iPos.y + u1 * iPos.z) * h, 1.0) * mVP;
}
#endif

#if defined(PS_PuffPS)
layout(location = 0) flat in vec4 iA;	// centre, radius
layout(location = 1) flat in vec4 iB;	// sun light x albedo, extinction 1/m
layout(location = 2) flat in vec4 iC;	// ambient x albedo, noise seed
layout(location = 3) flat in vec4 iD;	// plume light x albedo, spin
layout(location = 4) flat in vec4 iE;	// haze transmission, fog transmission
layout(location = 5) flat in vec4 iF;	// haze in-scatter
layout(location = 0) out vec4 oColor;

vec3 PRay (vec2 uv) { return normalize(pCamX.xyz * ((uv.x * 2.0 - 1.0) * pCamX.w) + pCamY.xyz * ((1.0 - uv.y * 2.0) * pCamY.w) + pCamZ.xyz); }

void main()
{
	vec2 hp = gl_FragCoord.xy;
	vec2 uv = hp * pTex.zw;
	vec3 d = PRay(uv);
	vec3 C = iA.xyz;
	float R = iA.w;
	float b = dot(d, C), dd = max(dot(C, C) - b * b, 0.0);
	if (dd >= R * R) discard;
	float hh = sqrt(R * R - dd);
	ivec2 bq = ivec2(hp) * 2, m = textureSize(tDepthP, 0) - 1;
	float dsc = 1e30;
	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 2; i++) { float z = texelFetch(tDepthP, min(bq + ivec2(i, j), m), 0).r; dsc = min(dsc, z > 0.0 ? z : 1e30); }
	float t0 = max(b - hh, 0.05), t1 = min(b + hh, dsc);
	if (t1 <= t0) discard;
	float u0 = t0 - b, u1 = t1 - b;
	float chord = (u1 - u0) * (1.0 - dd / (R * R)) - (u1 * u1 * u1 - u0 * u0 * u0) / (3.0 * R * R);	// parabolic density, exact
	vec3 mid = d * (0.5 * (t0 + t1));
	vec3 lq = (mid - C) / R;
	float ca = cos(iD.w), sa = sin(iD.w);
	vec3 nq = vec3(ca * lq.x - sa * lq.z, lq.y, sa * lq.x + ca * lq.z) * 0.9 + iC.w;
	float n = textureLod(tNoiseP, nq * 0.35, 0.0).r * 0.7 + textureLod(tNoiseP, nq * 0.9 + 0.5, 0.0).g * 0.3;
	float tau = iB.w * chord * (0.6 + 0.8 * n);
	float alpha = 1.0 - exp(-tau);
	if (alpha < 1e-3) discard;
	vec3 s = pSun.xyz;
	float bs = dot(mid - C, s), cs = dot(mid - C, mid - C) - R * R;
	float te = -bs + sqrt(max(bs * bs - cs, 0.0));	// to the sphere's edge toward the sun
	float tself = exp(-0.5 * iB.w * te * max(1.0 - dot(lq, lq), 0.0));
	float mu = dot(normalize(mid - C + 1e-4), pUp.xyz);
	vec3 col = iB.rgb * tself * HG4(dot(d, s), 0.35) + iC.rgb * (0.75 + 0.25 * mu) + iD.rgb;
#if !defined(_LIGHTGLOW)
	col = LitCurve(col);	// LightBlur tone-maps later otherwise
#endif
	vec3 fogC = vec3(0.0);
	if ((int(pUp.w) & 1) != 0) { vec4 f = textureLod(tFogP, uv, 0.0); fogC = f.a > 1e-3 ? f.rgb / f.a : vec3(0.0); }
	col = col * iE.rgb * iE.w + iF.rgb * iE.w + fogC * (1.0 - iE.w);
	if ((int(pUp.w) & 6) != 0) {	// behind a volume cloud
		float cd;
		if ((int(pUp.w) & 2) != 0) cd = textureLod(tCloudD, uv, 0.0).r;
		else {
			vec2 a = SphereT(d, pCloud.xyz, pCloud2.x), bb = SphereT(d, pCloud.xyz, pCloud.w);
			float r0 = length(pCloud.xyz);
			cd = r0 > pCloud2.x ? (a.x <= a.y && a.x > 0.0 ? a.x : 1e30) : (r0 < pCloud.w ? (bb.x <= bb.y && bb.y > 0.0 ? bb.y : 1e30) : 0.0);
		}
		if (cd > 0.0 && cd < 0.5 * (t0 + t1)) alpha *= 1.0 - textureLod(tCloudV, uv, 0.0).a;
	}
	oColor = vec4(col * alpha, alpha);
}
#endif

#if !defined(VS_PuffVS) && !defined(PS_PuffPS)
#if defined(PS_PSPlumeMarch) || defined(PS_PSPlumeBlur) || defined(PS_PSPlumeComposite) || defined(PS_PSAtlasBake) || defined(PS_PSFlowPrep) || defined(PS_PSFlowReduce) || defined(PS_PSFlowBlur)
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
#endif
#ifdef PS_PSPlumeMarch
layout(location = 1) out vec4 oDist;	// plume and scene distance (RG32F)
void main() { oColor = PSPlumeMarch(iX, iY); oDist = vec4(primDist, 0.0, 0.0); }
#endif
#ifdef PS_PSPlumeBlur
void main() { oColor = PSPlumeBlur(iX, iY); }
#endif
#ifdef PS_PSPlumeComposite
void main() { oColor = PSPlumeComposite(iX, iY); }
#endif
#ifdef PS_PSAtlasBake
void main() { oColor = PSAtlasBake(iX, iY); }
#endif
#ifdef PS_PSFlowPrep
void main() { oColor = PSFlowPrep(iX, iY); }
#endif
#ifdef PS_PSFlowReduce
void main() { oColor = PSFlowReduce(iX, iY); }
#endif
#ifdef PS_PSFlowBlur
void main() { oColor = PSFlowBlur(iX, iY); }
#endif
#endif
