// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: temporal anti-aliasing resolve of the opaque 3D scene (design-visual-batch 3)
// after Karis 2014 (reprojection, neighbourhood clip in YCoCg, luma weights), Salvi 2016 (variance box) and the Catmull-Rom spline (1974); written from the talks and the spline's definition

layout(binding = 1, row_major, scalar) uniform TaaPS	// IPI.glsl's VS block is binding 0
{
mat4	mPt;		// camera-relative point now to last frame's clip space, static in the proxy body's frame
mat4	mVs;		// the same, rigid with the camera target vessel
mat4	mSky;		// direction now to last frame's clip space (the view's turn only)
vec4	vCamX;		// camera axes, global (xyz); tan of the half apertures (vCamX.w, vCamY.w)
vec4	vCamY;
vec4	vCamZ;		// forward, the jitter folded in
vec4	vPl;		// camera minus the proxy's centre (xyz); |xyz|^2 - r^2 of the sphere for depth-0 pixels, from doubles (w)
vec4	vSph;		// 1: depth-0 pixels take the sphere (x)
vec4	vPrm;		// blend weight, fast blend weight, reset (1), clip box half width in sigma
vec4	vTex;		// 1/w, 1/h, w, h
vec4	vJit;		// this frame's jitter in uv (xy); flag target valid (z), volume distance valid (w)
};

layout(binding = 4) uniform sampler2D tCur;		// this frame, single sample
layout(binding = 5) uniform sampler2D tHist;	// last frame's output
layout(binding = 6) uniform sampler2D tDepth;	// camera distance, 0 none
layout(binding = 7) uniform sampler2D tFlag;	// > 0 on the camera target vessel
layout(binding = 8) uniform sampler2D tVolD;	// volume cloud distance, half res, 0 none
layout(binding = 9) uniform sampler2D tVol;		// volume clouds, premultiplied, half res

vec3 Ray(vec2 uv)	// as VolCloud.glsl PixelRay
{
	return normalize(vCamX.xyz * ((uv.x * 2.0 - 1.0) * vCamX.w) + vCamY.xyz * ((1.0 - uv.y * 2.0) * vCamY.w) + vCamZ.xyz);
}

vec3 YCoCg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 RGB(vec3 y) { return vec3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z); }

vec3 Fix(vec3 c) { return (any(isnan(c)) || any(isinf(c))) ? vec3(0.0) : max(c, vec3(0.0)); }

// 1/(1+luma) weights: bright pixels do not dominate; Y of the result stays below 1, so it inverts
vec3 Tm(vec3 c) { return c / (1.0 + YCoCg(c).x); }
vec3 ITm(vec3 c) { return c / max(1.0 - YCoCg(c).x, 1e-3); }

// the Catmull-Rom spline's 4 weights per axis at fraction f, from its basis matrix
void CRWeights(vec2 f, out vec2 w0, out vec2 w1, out vec2 w2, out vec2 w3)
{
	vec2 f2 = f * f, f3 = f2 * f;
	w0 = 0.5 * (2.0 * f2 - f3 - f);
	w1 = 0.5 * (3.0 * f3 - 5.0 * f2) + 1.0;
	w2 = 0.5 * (4.0 * f2 - 3.0 * f3 + f);
	w3 = 0.5 * (f3 - f2);
}

// the 4x4 spline as a cross of 5 bilinear taps: the two middle texels of each axis share one tap, the 4 corners are left out and the rest renormalised
vec3 History(vec2 uv)
{
	vec2 p = uv * vTex.zw - 0.5, t = floor(p), f = p - t;
	vec2 w0, w1, w2, w3;
	CRWeights(f, w0, w1, w2, w3);
	vec2 wm = w1 + w2, um = (t + 0.5 + w2 / wm) * vTex.xy;
	vec2 ua = (t - 0.5) * vTex.xy, ub = (t + 2.5) * vTex.xy;
	vec2 tap[5] = vec2[5](um, vec2(ua.x, um.y), vec2(ub.x, um.y), vec2(um.x, ua.y), vec2(um.x, ub.y));
	float wt[5] = float[5](wm.x * wm.y, w0.x * wm.y, w3.x * wm.y, wm.x * w0.y, wm.x * w3.y);
	vec3 s = vec3(0.0);
	float ws = 0.0;
	for (int k = 0; k < 5; k++) { s += textureLod(tHist, tap[k], 0.0).rgb * wt[k]; ws += wt[k]; }
	return s / ws;
}

// h moved toward the box centre c along h - c until it is on the box (half extent e)
vec3 Clip(vec3 h, vec3 c, vec3 e)
{
	vec3 r = h - c, a = max(abs(r), vec3(1e-7));
	float t = min(min(e.x / a.x, e.y / a.y), e.z / a.z);
	return t < 1.0 ? c + r * t : h;
}

vec2 Prev(vec4 cp) { return cp.w > 0.0 ? vec2(cp.x / cp.w * 0.5 + 0.5, 0.5 - cp.y / cp.w * 0.5) + vJit.xy : vec2(-1.0); } // the history is unjittered
bool OnScreen(vec2 p) { return all(greaterThanEqual(p, vec2(0.0))) && all(lessThanEqual(p, vec2(1.0))); }
bool Bad(vec3 c) { return any(isnan(c)) || any(isinf(c)); }

vec4 PSTaa(float x, float y)
{
	vec2 uv = vec2(x, y);
	vec4 c0 = texture(tCur, uv);
	c0.rgb = Fix(c0.rgb);
	if (vPrm.z > 0.5) return c0;

	// 3x3: mean and sigma in YCoCg, the nearest depth (edges move with the front), this frame filtered at the unjittered centre (Blackman-Harris fit, Karis 2014)
	vec3 m1 = vec3(0.0), m2 = vec3(0.0), cs = vec3(0.0);
	float dn = 1e30, ws = 0.0;
	vec2 un = uv, jp = vJit.xy * vTex.zw;
	for (int j = -1; j <= 1; j++)
		for (int i = -1; i <= 1; i++) {
			vec2 q = uv + vec2(i, j) * vTex.xy;
			vec3 s = YCoCg(Tm(Fix(texture(tCur, q).rgb)));
			m1 += s;
			m2 += s * s;
			vec2 e = vec2(i, j) - jp; // that sample shows the scene at its pixel minus the jitter
			float w = exp(-2.29 * dot(e, e));
			cs += s * w;
			ws += w;
			float d = texture(tDepth, q).r;
			if (d > 0.0 && d < dn) { dn = d; un = q; }
		}
	vec3 mu = m1 / 9.0, sg = sqrt(max(m2 / 9.0 - mu * mu, vec3(0.0)));
	vec3 cy = cs / ws;
	vec4 cr = vec4(Fix(ITm(RGB(cy))), c0.a);

	// where this pixel was a frame ago
	vec3 dir = Ray(uv);
	float d = dn < 1e30 ? dn : 0.0;
	bool vs = d > 0.0 && vJit.z > 0.5 && texture(tFlag, un).r > 0.0;
	if (vJit.w > 0.5) { // in front of the ground: the volume cloud's own distance
		float vd = texture(tVolD, uv).r;
		if (texture(tVol, uv).a > 0.3 && vd > 0.0 && (d <= 0.0 || vd < d)) { d = vd; vs = false; }
	}
	bool two = false;
	if (d <= 0.0 && vSph.x > 0.5) { // the cloud shell from below, the planet from above; roots in the forms that do not cancel
		float b = dot(vPl.xyz, dir), c = vPl.w, h = b * b - c;
		if (c < 0.0) { h = sqrt(max(h, 0.0)); d = b >= 0.0 ? -c / (b + h) : h - b; two = true; }
		else if (b < 0.0 && h >= 0.0) d = c / (sqrt(h) - b);
	}
	vec2 pv = Prev(d > 0.0 ? vec4(dir * d, 1.0) * (vs ? mVs : mPt) : vec4(dir, 0.0) * mSky);
	vec2 ps = two ? Prev(vec4(dir, 0.0) * mSky) : vec2(-1.0); // under the shell an empty pixel is the 2D layer or the stars: both are tried
	bool on1 = OnScreen(pv), on2 = OnScreen(ps);
	if (!on1 && !on2) return cr;

	vec3 c0y = YCoCg(Tm(c0.rgb));
	vec3 h = on1 ? History(pv) : vec3(0.0);
	bool ok = on1 && !Bad(h);
	if (on2) { // the history nearer this pixel wins
		vec3 h2 = History(ps);
		if (!Bad(h2) && (!ok || distance(YCoCg(Tm(max(h2, vec3(0.0)))), c0y) < distance(YCoCg(Tm(max(h, vec3(0.0)))), c0y))) { h = h2; pv = ps; ok = true; }
	}
	if (!ok) return cr;
	vec3 bl = min(mu - vPrm.w * sg, c0y), bh = max(mu + vPrm.w * sg, c0y); // the box holds this pixel: lone bright pixels keep their history
	vec3 hy = Clip(YCoCg(Tm(max(h, vec3(0.0)))), 0.5 * (bl + bh), 0.5 * (bh - bl));
	float a = mix(vPrm.x, vPrm.y, smoothstep(1.5, 2.5, length((pv - uv) * vTex.zw))); // faster where the image moves
	vec3 o = Fix(ITm(RGB(mix(hy, cy, a))));
	return vec4(o, c0.a);
}

#ifdef PS_PSTaa
layout(location = 0) in float iX;	// TEXCOORD0 of IPI.glsl
layout(location = 1) in float iY;	// TEXCOORD1
layout(location = 0) out vec4 oColor;
void main() { oColor = PSTaa(iX, iY); }
#endif
