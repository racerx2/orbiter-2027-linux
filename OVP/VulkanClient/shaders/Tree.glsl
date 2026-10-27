// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: instanced trees, near meshes and far impostors, and their camera distance (design-ground-graphics 5.5, 5.6)

struct TreePrmS
{
	mat4 mW;		// cell frame to camera-relative world
	mat4 mVP;		// view projection
	vec4 vCam;		// camera in the cell frame (xyz), time s (w)
	vec4 vBand;		// meshes end, impostors start, cross-fade width, range (m)
	vec4 vSun;		// toward the sun, cell frame (xyz); 0 meshes, 1 impostors (w)
	vec4 vHorW0;	// sun azimuth weights of the horizon bytes 0-3
	vec4 vHorW1;	// 4-7
	vec4 vSunCol;	// sun colour (rgb), planet exposure (w)
	vec4 vAmb;		// ambient (rgb), 1 for the distance pass (w)
	vec4 vTrans;	// atmospheric transmission (rgb)
	vec4 vInsc;		// inscatter (rgb), TAA frame % 8 (w)
	vec4 vLod;		// not upstream (design-far-forest): D0 = TreeRange, far limit, its fade, px per unit tangent
	vec4 vLod2;		// share drawn at this height, the distance the cell's haze is for
};

#include "SceneShadow.glsl" // not upstream: CsmPrm (_CSM)
#if defined(_CSM)
layout(binding = 0, row_major, scalar) uniform TreeBlock { TreePrmS Prm; CsmPrm gCsm; }; // not upstream: gCsm, the scene shadow cascades (design-scene-shadows Rev 2)
layout(binding = 4) uniform sampler2D tCsm; // not upstream: their atlas
#include "SceneShadow.glsl" // not upstream: CsmVis (pixel stage)
#else
layout(binding = 0, row_major, scalar) uniform TreeBlock { TreePrmS Prm; };
#endif

struct TreeVS
{
	vec3 nrm;		// cell frame
	vec3 obj;		// object space, m
	vec3 posW;		// camera-relative world
	vec2 uv;		// impostor
	vec4 info;		// part, ao, kind, horizon sine
	vec3 tint;		// tint r, g; distance
};

float IGNt(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

#ifdef VS_TreeVS
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec3 iNrm;
layout(location = 5) in vec2 iTex;		// part, ao
layout(location = 6) in vec4 iInst;		// position in the cell frame, height
layout(location = 3) in vec4 iCol;		// yaw, kind, tint r, tint g
layout(location = 7) in vec4 iHor0;
layout(location = 8) in vec4 iHor1;
layout(location = 0) out TreeVS oVS;
#if defined(_CSM)
layout(location = 6) out float oHgt; // not upstream: the tree's height, its own crown's shadow bias
#endif
layout(location = 7) out vec2 oLod; // not upstream (design-far-forest): widening, crown detail 0..1
void main()
{
	vec3 ip = iInst.xyz;
	float s = iInst.w;
#if defined(_CSM)
	oHgt = s; // not upstream
#endif
	float d = length(ip - Prm.vCam.xyz);
	bool imp = Prm.vSun.w > 0.5;
	float kb = floor(iCol.y * 255.0 + 0.5);
	float kind = mod(kb, 16.0), lvl = floor(kb / 16.0); // not upstream (design-far-forest): the level in the high bits
	oVS = TreeVS(vec3(0), vec3(0), vec3(0), vec2(0), vec4(0), vec3(0));
	oLod = vec2(1.0);
	if ((!imp && d > Prm.vBand.x + Prm.vBand.z) || (imp && (d < Prm.vBand.y || d > Prm.vBand.w))) {
		gl_Position = vec4(0.0, 0.0, -2.0, 1.0); // outside this draw's band
		return;
	}
	vec3 p, n;
	if (!imp) {
		float a = iCol.x * 6.2831853, c = cos(a), sn = sin(a);
		vec3 q = iPos * s;
		float sway = 0.015 * s * iPos.y * iPos.y * sin(Prm.vCam.w * 1.3 + iCol.z * 40.0); // wind
		p = ip + vec3(q.x * c - q.z * sn + sway, q.y, q.x * sn + q.z * c);
		n = vec3(iNrm.x * c - iNrm.z * sn, iNrm.y, iNrm.x * sn + iNrm.z * c);
		oVS.obj = q;
	}
	else { // not upstream (design-far-forest): beyond D0 a share of the levels, each widened for the trees not drawn, at least 1 px tall, facing the camera
		float lam = max(0.0, log2(d / Prm.vLod.x)), lf = floor(lam), f = lam - lf;
		float keep = lvl < lf - 0.5 ? 0.0 : ((lvl > lf + 0.5 || lvl > 7.5) ? 1.0 : (exp2(2.0 - 2.0 * f) - 1.0) / 3.0); // TreeGeom::DrawShare
		keep *= (1.0 - smoothstep(Prm.vLod.y - Prm.vLod.z, Prm.vLod.y, d)) * Prm.vLod2.x;
		vec3 c0 = ip + vec3(0.0, 0.6 * s, 0.0), v = c0 - Prm.vCam.xyz, vh = normalize(v);
		float wid = exp2(lam), hs = s * mix(1.0, wid, abs(vh.y));
		float px = hs * Prm.vLod.w / max(d, 1.0), grow = max(1.0, 1.0 / px);
		keep /= grow;
		if (iCol.x >= keep) {
			gl_Position = vec4(0.0, 0.0, -2.0, 1.0); // not drawn at this distance
			return;
		}
		vec3 r = normalize(vec3(v.z, 0.0, -v.x) + vec3(1e-4, 0.0, 0.0)); // across the view, horizontal
		float t = clamp(lam, 0.0, 1.0);
		vec3 u = normalize(mix(vec3(0.0, 1.0, 0.0), cross(vh, r), t));
		float w = kind < 1.5 ? 0.55 : (kind > 3.5 ? 1.1 : 0.85);	// conifers narrow, bushes wide
		p = c0 + r * (iPos.x * s * w * wid) + u * ((iPos.y - 0.6) * hs + iPos.y * hs * (grow - 1.0));
		n = -normalize(mix(normalize(vec3(v.x, 0.0, v.z) + vec3(1e-4, 0.0, 0.0)), vh, t));
		oVS.uv = vec2(iPos.x + 0.5, iPos.y);
		oVS.obj = vec3(iPos.x * s, iPos.y * s, 0.0);
		oLod = vec2(wid, clamp((px * grow - 1.0) / 3.0, 0.0, 1.0));
	}
	oVS.posW = (vec4(p, 1.0) * Prm.mW).xyz;
	oVS.nrm = n;
	float hs = dot(iHor0, Prm.vHorW0) + dot(iHor1, Prm.vHorW1);
	oVS.info = vec4(iTex.x, iTex.y, kind, hs * 1.25 - 0.25);
	oVS.tint = vec3(iCol.z, iCol.w, d);
	gl_Position = vec4(oVS.posW, 1.0) * Prm.mVP;
}
#endif

#ifdef PS_TreePS
layout(location = 0) in TreeVS frg;
#if defined(_CSM)
layout(location = 6) in float iHgt; // not upstream
#endif
layout(location = 7) in vec2 iLod; // not upstream (design-far-forest)
layout(location = 0) out vec4 oColor;
#if defined(_SSGI)
layout(location = 1) out vec4 oAlbG;	// not upstream: SSGI albedo / 4 (distance pass only)
layout(location = 2) out vec4 oNrmG;	// not upstream: SSGI octahedral world normal, class
#endif
void main()
{
	bool imp = Prm.vSun.w > 0.5;
	float kind = frg.info.z, d = frg.tint.z;
	vec3 n = normalize(frg.nrm);
	float part = frg.info.x;
	if (imp) { // the crown's outline, a sphere-like normal
		vec2 uv = frg.uv;
		float x = uv.x - 0.5, y = uv.y;
		bool inside;
		vec2 c;
		float e = sin(frg.obj.x * 1.7 + frg.obj.y * 2.3) * 0.04;
		if (kind < 1.5) { inside = abs(x) < 0.5 * (1.0 - y) * (0.9 + e) && y > 0.12; c = vec2(x * 2.0 / max(1.0 - y, 0.1), (y - 0.5) * 2.0); }
		else if (kind > 3.5) { vec2 q = vec2(x / 0.5, (y - 0.45) / 0.45); inside = dot(q, q) < 1.0 + e; c = q; }
		else { vec2 q = vec2(x / 0.45, (y - 0.62) / 0.38); inside = dot(q, q) < 1.0 + e; c = q; }
		bool trunk = abs(x) * iLod.x < 0.04 && y < 0.4 && kind < 3.5; // not upstream (design-far-forest): its own width
		if (!inside && !trunk) discard;
		part = inside ? 1.0 : 0.0;
		c *= iLod.y; // not upstream (design-far-forest): the crown's mean below 4 px
		float z = sqrt(max(0.0, 1.0 - dot(c, c) * 0.8));
		vec3 v = normalize(vec3(-n.z, 0.0, n.x)); // across
		n = normalize(v * c.x * 0.8 + vec3(0.0, 1.0, 0.0) * c.y * 0.6 + n * z);
	}
	// cross-fade the bands with a screen-door
	float fade = imp ? smoothstep(Prm.vBand.y, Prm.vBand.y + Prm.vBand.z, d) : 1.0 - smoothstep(Prm.vBand.x, Prm.vBand.x + Prm.vBand.z, d);
#ifdef _TAA
	float nz = IGNt(gl_FragCoord.xy + Prm.vInsc.w * 5.588238); // frame % 8 moves the pattern, TAA averages it (Jimenez 2014)
#else
	float nz = IGNt(gl_FragCoord.xy);
#endif
	if (fade < (imp ? 1.0 - nz : nz)) discard; // complementary, one of the two at each pixel
#if defined(_SSGI)
	if (Prm.vAmb.w > 0.5) { // not upstream: the distance pass with the SSGI albedo and normal (design-ssgi 4.1)
		vec3 lf = kind < 1.5 ? vec3(0.09, 0.17, 0.08) : (kind > 3.5 ? vec3(0.17, 0.22, 0.09) : vec3(0.13, 0.22, 0.08));
		vec2 tn = mix(vec2(0.5), frg.tint.xy, iLod.y); // not upstream (design-far-forest): the mean tint and grain below 4 px
		lf *= 0.8 + 0.4 * vec3(tn.x, tn.y, 0.5 * (tn.x + tn.y));
		float gr = mix(1.0, 0.85 + 0.3 * fract(sin(dot(floor(frg.obj * 3.0), vec3(12.9898, 78.233, 37.719))) * 43758.5453), iLod.y);
		vec3 ab = part > 0.5 ? lf * gr : vec3(0.20, 0.15, 0.11);
		vec3 nw = normalize((vec4(n, 0.0) * Prm.mW).xyz);
		nw /= abs(nw.x) + abs(nw.y) + abs(nw.z);
		vec2 oc = nw.z >= 0.0 ? nw.xy : (1.0 - abs(nw.yx)) * vec2(nw.x >= 0.0 ? 1.0 : -1.0, nw.y >= 0.0 ? 1.0 : -1.0);
		oColor = vec4(length(frg.posW));
		oAlbG = vec4(ab * 0.25, 0.0);
		oNrmG = vec4(oc * 0.5 + 0.5, 0.0, 1.0 / 3.0); // class 1 trees
		return;
	}
#else
	if (Prm.vAmb.w > 0.5) { oColor = vec4(length(frg.posW)); return; } // distance pass
#endif

	vec3 leaf = kind < 1.5 ? vec3(0.09, 0.17, 0.08) : (kind > 3.5 ? vec3(0.17, 0.22, 0.09) : vec3(0.13, 0.22, 0.08));
	vec2 tn = mix(vec2(0.5), frg.tint.xy, iLod.y); // not upstream (design-far-forest): the mean tint and grain below 4 px
	leaf *= 0.8 + 0.4 * vec3(tn.x, tn.y, 0.5 * (tn.x + tn.y));
	float grain = mix(1.0, 0.85 + 0.3 * fract(sin(dot(floor(frg.obj * 3.0), vec3(12.9898, 78.233, 37.719))) * 43758.5453), iLod.y);
	vec3 alb = part > 0.5 ? leaf * grain : vec3(0.20, 0.15, 0.11);
	vec3 L = Prm.vSun.xyz;
	float ndl = dot(n, L);
	float dif = part > 0.5 ? pow(clamp(ndl * 0.5 + 0.5, 0.0, 1.0), 1.5) : clamp(ndl, 0.0, 1.0); // leaves wrap the light
	float fH = smoothstep(frg.info.w - 0.012, frg.info.w + 0.012, L.y); // terrain horizon at the tree
#if defined(_CSM)
	fH = min(fH, CsmVis(tCsm, frg.posW, gCsm.vL.xyz, 1.0, 0.5 * iHgt, 0.0)); // not upstream: neighbours, buildings, relief and vessels; the own crown within half a height ignored (design-scene-shadows 7.2)
#endif
	float ao = imp ? 0.6 + 0.4 * frg.uv.y : frg.info.y;
	vec3 c = alb * (Prm.vSunCol.rgb * dif * fH + Prm.vAmb.rgb * ao);
#if defined(_FILMIC)
	c *= Prm.vSunCol.w * 0.568; // the planet's filmic curve, as the ground
	c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
#else
	c = 1.0 - exp(-Prm.vSunCol.w * c); // the planet curve, as the ground
#endif
	vec3 T = Prm.vTrans.rgb, I = Prm.vInsc.rgb;
	if (Prm.vLod2.y > 1.0) { // not upstream (design-far-forest): the cell's haze carried to this tree's distance
		float q = d / Prm.vLod2.y;
		vec3 one = vec3(1.0) - T, Tq = pow(max(T, vec3(1e-4)), vec3(q));
		I *= mix(vec3(q), (vec3(1.0) - Tq) / max(one, vec3(1e-4)), step(vec3(1e-4), one));
		T = Tq;
	}
	c = c * T + I;
	oColor = vec4(c, 1.0);
}
#endif
