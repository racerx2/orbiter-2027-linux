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
	vec4 vInsc;		// inscatter (rgb)
};

layout(binding = 0, row_major, scalar) uniform TreeBlock { TreePrmS Prm; };

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
void main()
{
	vec3 ip = iInst.xyz;
	float s = iInst.w;
	float d = length(ip - Prm.vCam.xyz);
	bool imp = Prm.vSun.w > 0.5;
	float kind = floor(iCol.y * 255.0 + 0.5);
	oVS = TreeVS(vec3(0), vec3(0), vec3(0), vec2(0), vec4(0), vec3(0));
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
	else {
		vec3 v = ip - Prm.vCam.xyz;
		vec3 r = normalize(vec3(v.z, 0.0, -v.x) + vec3(1e-4, 0.0, 0.0)); // across the view, horizontal
		float w = kind < 1.5 ? 0.55 : (kind > 3.5 ? 1.1 : 0.85);	// conifers narrow, bushes wide
		p = ip + r * (iPos.x * s * w) + vec3(0.0, iPos.y * s, 0.0);
		n = -normalize(vec3(v.x, 0.0, v.z));
		oVS.uv = vec2(iPos.x + 0.5, iPos.y);
		oVS.obj = vec3(iPos.x * s, iPos.y * s, 0.0);
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
layout(location = 0) out vec4 oColor;
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
		bool trunk = abs(x) < 0.04 && y < 0.4 && kind < 3.5;
		if (!inside && !trunk) discard;
		part = inside ? 1.0 : 0.0;
		float z = sqrt(max(0.0, 1.0 - dot(c, c) * 0.8));
		vec3 v = normalize(vec3(-n.z, 0.0, n.x)); // across
		n = normalize(v * c.x * 0.8 + vec3(0.0, 1.0, 0.0) * c.y * 0.6 + n * z);
	}
	// cross-fade the bands with a screen-door
	float fade = imp ? smoothstep(Prm.vBand.y, Prm.vBand.y + Prm.vBand.z, d) : 1.0 - smoothstep(Prm.vBand.x, Prm.vBand.x + Prm.vBand.z, d);
	float nz = IGNt(gl_FragCoord.xy);
	if (fade < (imp ? 1.0 - nz : nz)) discard; // complementary, one of the two at each pixel
	if (Prm.vAmb.w > 0.5) { oColor = vec4(length(frg.posW)); return; } // distance pass

	vec3 leaf = kind < 1.5 ? vec3(0.09, 0.17, 0.08) : (kind > 3.5 ? vec3(0.17, 0.22, 0.09) : vec3(0.13, 0.22, 0.08));
	leaf *= 0.8 + 0.4 * vec3(frg.tint.x, frg.tint.y, 0.5 * (frg.tint.x + frg.tint.y));
	float grain = 0.85 + 0.3 * fract(sin(dot(floor(frg.obj * 3.0), vec3(12.9898, 78.233, 37.719))) * 43758.5453);
	vec3 alb = part > 0.5 ? leaf * grain : vec3(0.20, 0.15, 0.11);
	vec3 L = Prm.vSun.xyz;
	float ndl = dot(n, L);
	float dif = part > 0.5 ? pow(clamp(ndl * 0.5 + 0.5, 0.0, 1.0), 1.5) : clamp(ndl, 0.0, 1.0); // leaves wrap the light
	float fH = smoothstep(frg.info.w - 0.012, frg.info.w + 0.012, L.y); // terrain horizon at the tree
	float ao = imp ? 0.6 + 0.4 * frg.uv.y : frg.info.y;
	vec3 c = alb * (Prm.vSunCol.rgb * dif * fH + Prm.vAmb.rgb * ao);
	c = 1.0 - exp(-Prm.vSunCol.w * c); // the planet curve, as the ground
	c = c * Prm.vTrans.rgb + Prm.vInsc.rgb;
	oColor = vec4(c, 1.0);
}
#endif
