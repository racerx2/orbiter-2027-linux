// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ground materials' maths (design-ground-detail 1); hex-tiling after Mikkelsen, "Practical Real-Time Hex-Tiling", JCGT 2022 (MIT, CREDITS)

#include "GroundGeom.h"
#include "TreeGeom.h"
#include <algorithm>
#include <cmath>

namespace GroundGeom {

float Smooth (float e0, float e1, float x)
{
	float t = std::clamp ((x - e0) / (e1 - e0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

float Veg (V3 s)
{
	return Smooth (0.02f, 0.10f, (2.0f * s.y - s.x - s.z) / (s.x + s.y + s.z + 0.01f));
}

float Sandy (V3 s)
{
	float mx = std::max ({s.x, s.y, s.z}), mn = std::min ({s.x, s.y, s.z}), d = mx - mn;
	if (d < 1e-4f) return 0.0f;
	float h = mx == s.x ? 60.0f * (s.y - s.z) / d : (mx == s.y ? 60.0f * ((s.z - s.x) / d + 2.0f) : 60.0f * ((s.x - s.y) / d + 4.0f));
	if (h < 0.0f) h += 360.0f;
	float L = (s.x + s.y + s.z) / 3.0f;
	return Smooth (0.30f, 0.40f, L) * Smooth (20.0f, 30.0f, h) * (1.0f - Smooth (55.0f, 65.0f, h)) * Smooth (0.08f, 0.15f, d / mx);
}

void Weights (V3 cls, V3 s, float cosSlope, float W[NMAT])
{
	float tot = cls.x + cls.y + cls.z;
	if (tot > 1.0f) { cls.x /= tot; cls.y /= tot; cls.z /= tot; tot = 1.0f; }
	float r = 1.0f - Smooth (0.64278761f, 0.81915204f, cosSlope); // rock from 35 to 50 deg
	float k = 1.0f - r, v = Veg (s), y = Sandy (s);
	W[GRASS] = (cls.y + cls.z) * v * k;
	W[FOREST] = cls.x * k;
	W[SOIL] = cls.y * (1.0f - v) * k;
	W[SAND] = cls.z * (1.0f - v) * y * k;
	W[GRAVEL] = cls.z * (1.0f - v) * (1.0f - y) * k;
	W[ROCK] = r; // steep ground is rock whatever the class says, snow and glacier too
}

void WeightsFree (float cosSlope, float patch, float e, float W[NMAT])
{
	float r = 1.0f - Smooth (0.64278761f, 0.81915204f, cosSlope);
	for (int i = 0; i < NMAT; i++) W[i] = 0.0f;
	W[FINE] = (1.0f - r) * (1.0f - e) * patch;
	W[COARSE] = (1.0f - r) * (1.0f - e) * (1.0f - patch);
	W[BRIGHT] = (1.0f - r) * e;
	W[FROCK] = r;
}

float Patches (float n32, float n128)
{
	return Smooth (0.25f, 0.75f, 0.6f * n32 + 0.4f * n128);
}

float Third (float n64)
{
	return Smooth (0.55f, 0.85f, n64);
}

void Top3 (const float W[NMAT], int idx[3], float w[3])
{
	bool used[NMAT] = {};
	float all = 0.0f, top = 0.0f;
	for (int i = 0; i < NMAT; i++) all += W[i];
	for (int j = 0; j < 3; j++) {
		int b = -1;
		for (int i = 0; i < NMAT; i++) if (!used[i] && (b < 0 || W[i] > W[b])) b = i;
		used[b] = true;
		idx[j] = b; w[j] = W[b]; top += W[b];
	}
	float sc = top > 0.0f ? all / top : 0.0f;
	for (int j = 0; j < 3; j++) w[j] *= sc;
}

void HeightBlend (const float w[3], const float h[3], float out[3])
{
	float m = -1e9f, sw = 0.0f, sb = 0.0f, a[3];
	for (int j = 0; j < 3; j++) { a[j] = w[j] + h[j]; sw += w[j]; if (w[j] > 0.0f) m = std::max (m, a[j]); }
	for (int j = 0; j < 3; j++) { out[j] = w[j] > 0.0f ? std::max (a[j] - m + 0.2f, 0.0f) : 0.0f; sb += out[j]; }
	for (int j = 0; j < 3; j++) out[j] = sb > 0.0f ? out[j] * sw / sb : 0.0f;
}

static double FloorMod (double x, double p) { return x - p * std::floor (x / p); }

void SkewSplit (double ou, double ov, float *cx, float *cy, float *fx, float *fy)
{
	double sx = HexK * (ou - ov * 0.57735026918962576), sy = HexK * ov * 1.1547005383792515;
	double bx = std::floor (sx), by = std::floor (sy);
	*fx = float(sx - bx); *fy = float(sy - by);
	*cx = float(FloorMod (bx, HexPeriod)); *cy = float(FloorMod (by, HexPeriod));
}

void Grid (float u, float v, float fx, float fy, float cx, float cy, Tri &t)
{
	const float k = float(HexK);
	float sx = k * (u - v * 0.57735027f) + fx, sy = k * v * 1.15470054f + fy;
	float bx = std::floor (sx), by = std::floor (sy), tx = sx - bx, ty = sy - by, tz = 1.0f - tx - ty;
	float s = tz <= 0.0f ? 1.0f : 0.0f, s2 = 2.0f * s - 1.0f;
	t.w[0] = -tz * s2; t.w[1] = s - ty * s2; t.w[2] = s - tx * s2;
	float vx[3] = { bx + s, bx + s, bx + 1.0f - s }, vy[3] = { by + s, by + 1.0f - s, by + s };
	for (int j = 0; j < 3; j++) {
		t.id[j][0] = vx[j] + cx - HexPeriod * std::floor ((vx[j] + cx) / HexPeriod);
		t.id[j][1] = vy[j] + cy - HexPeriod * std::floor ((vy[j] + cy) / HexPeriod);
		float a = vx[j] - fx, b = vy[j] - fy;
		t.cu[j] = (a + 0.5f * b) / k; t.cv[j] = 0.8660254f * b / k;
	}
}

void VertexXf (const float id[2], float *rot, float *ou, float *ov)
{
	*rot = 6.2831853f * TreeGeom::ClsHash (id[0] + 0.25f, id[1] + 0.75f);
	*ou = TreeGeom::ClsHash (id[0] + 0.75f, id[1] + 0.25f);
	*ov = TreeGeom::ClsHash (id[0] + 0.5f, id[1] + 0.5f);
}

void TapUV (float u, float v, const Tri &t, int j, float *tu, float *tv)
{
	float rot, ou, ov;
	VertexXf (t.id[j], &rot, &ou, &ov);
	float c = std::cos (rot), s = std::sin (rot), du = u - t.cu[j], dv = v - t.cv[j];
	*tu = c * du - s * dv + ou; *tv = s * du + c * dv + ov;
}

void HexWeights (const float w[3], const float h[3], float out[3])
{
	float s = 0.0f;
	for (int j = 0; j < 3; j++) {
		float w2 = w[j] * w[j], w4 = w2 * w2;
		out[j] = (1.0f + (h[j] - 1.0f) * 0.6f) * w4 * w2 * w[j];
		s += out[j];
	}
	for (int j = 0; j < 3; j++) out[j] = s > 0.0f ? out[j] / s : 1.0f / 3.0f;
}

void TurnNormal (float nx, float ny, float rot, float *wx, float *wy)
{
	float c = std::cos (rot), s = std::sin (rot);
	*wx = c * nx - s * ny; *wy = s * nx + c * ny; // the map's y is image-up, against v: R(+rot) in east, north
}

float Handover (float texels)
{
	return 1.0f - Smooth (2.0f, 5.0f, std::log2 (std::max (texels, 1e-6f)));
}

float LambertRatio (V3 n, V3 m, V3 L)
{
	float dn = n.x * L.x + n.y * L.y + n.z * L.z, dm = m.x * L.x + m.y * L.y + m.z * L.z;
	return std::clamp (1.0f + (dn - dm) / std::max (dm + 0.1f, 0.1f), 0.0f, 2.5f);
}

void BSpline (float f, float *g0, float *g1, float *h0, float *h1)
{
	float f2 = f * f, f3 = f2 * f, o = 1.0f - f;
	float w0 = o * o * o / 6.0f, w1 = (3.0f * f3 - 6.0f * f2 + 4.0f) / 6.0f, w2 = (-3.0f * f3 + 3.0f * f2 + 3.0f * f + 1.0f) / 6.0f, w3 = f3 / 6.0f;
	*g0 = w0 + w1; *g1 = w2 + w3;
	*h0 = w1 / *g0 - 0.5f; *h1 = w3 / *g1 + 1.5f;
}

} // namespace GroundGeom
