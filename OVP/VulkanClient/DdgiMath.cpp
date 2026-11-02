
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: ray-traced probe GI maths (design-ddgi 4, 6; Rev 2)

#include "DdgiMath.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DdgiMath {

V3 Add (const V3 &a, const V3 &b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 Sub (const V3 &a, const V3 &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 Scale (const V3 &a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double Dot (const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Unit (const V3 &a) { double l = std::sqrt (Dot (a, a)); return l > 0.0 ? Scale (a, 1.0 / l) : a; }

static int Mod (int a, int m) { int r = a % m; return r < 0 ? r + m : r; }

double Spacing (int c) { return std::ldexp (1.0, 2 * c); }

int Weight (int c) { static const int w[NCASC] = { 8, 4, 2, 1, 1, 1 }; return c >= 0 && c < NCASC ? w[c] : 0; }
int CockpitTake (int left) { return left <= 0 ? 0 : std::min (NVC, std::max (1, left / 2)); }

I3 Dims () { return { GX, GY, GZ }; }

int Cascade (int probe) { return probe < NWORLD ? probe / NPC : VCASC; }

VGrid CockpitGrid (const V3 &lo, const V3 &hi, const V3 &eye)
{
	VGrid g;
	V3 ext = Sub (hi, lo);
	double e = std::max ({ ext.x, ext.y, ext.z, 0.0 });
	V3 c = Scale (Add (lo, hi), 0.5);
	if (e > VG * VSMAX) { g.s = VSMAX; c = eye; g.eye = true; }
	else g.s = std::clamp (e / VG, VSMIN, VSMAX);
	g.o = Sub (c, Scale ({ 1.0, 1.0, 1.0 }, 0.5 * VG * g.s));
	return g;
}

bool SameGrid (const VGrid &a, const VGrid &b)
{
	double q = 0.25 * std::min (a.s, b.s);
	V3 d = Sub (a.o, b.o);
	return a.eye == b.eye && std::fabs (a.s - b.s) * VG <= q && std::fabs (d.x) <= q && std::fabs (d.y) <= q && std::fabs (d.z) <= q;
}

bool EyeKept (const VGrid &g, const V3 &eye)
{
	if (!g.eye) return true;
	V3 q = Scale (Sub (eye, g.o), 1.0 / g.s);
	double a = 0.25 * VG, b = 0.75 * VG;
	return q.x >= a && q.x <= b && q.y >= a && q.y <= b && q.z >= a && q.z <= b;
}

V3 VProbePos (const VGrid &g, int slot)
{
	int x = slot % VG, z = (slot / VG) % VG, y = slot / (VG * VG);
	return Add (g.o, Scale ({ x + 0.5, y + 0.5, z + 0.5 }, g.s));
}

I3 Origin (int c, const V3 &cam, double ground)
{
	double s = Spacing (c);
	I3 o;
	o.x = (int)std::floor (cam.x / s) - GX / 2;
	o.z = (int)std::floor (cam.z / s) - GZ / 2;
	bool low = cam.y - ground < 0.5 * GY * s; // the camera well within the grid that stands on the ground
	o.y = low ? (int)std::floor (ground / s) - GY / 4 : (int)std::floor (cam.y / s) - GY / 2;
	return o;
}

int Slot (const I3 &cell)
{
	return (Mod (cell.y, GY) * GZ + Mod (cell.z, GZ)) * GX + Mod (cell.x, GX);
}

I3 CellOf (int slot, const I3 &o)
{
	int sx = slot % GX, sz = (slot / GX) % GZ, sy = slot / (GX * GZ);
	return { o.x + Mod (sx - o.x, GX), o.y + Mod (sy - o.y, GY), o.z + Mod (sz - o.z, GZ) };
}

V3 ProbePos (int c, const I3 &cell)
{
	double s = Spacing (c);
	return { (cell.x + 0.5) * s, (cell.y + 0.5) * s, (cell.z + 0.5) * s };
}

static double Face (int c, const I3 &o, const V3 &p)
{
	double s = Spacing (c);
	double f[3] = { p.x / s - o.x - 0.5, p.y / s - o.y - 0.5, p.z / s - o.z - 0.5 };
	int d[3] = { GX, GY, GZ };
	double m = 1e30;
	for (int i = 0; i < 3; i++) m = std::min (m, std::min (f[i], d[i] - 1 - f[i]));
	return m;
}

bool Inside (int c, const I3 &o, const V3 &p, double margin) { return Face (c, o, p) >= margin; }

double Blend (int c, const I3 &o, const V3 &p) { return std::clamp (Face (c, o, p) * 0.5, 0.0, 1.0); }

static double SignNZ (double v) { return v >= 0.0 ? 1.0 : -1.0; }

void OctEncode (const V3 &d, double *u, double *v)
{
	double l1 = std::fabs (d.x) + std::fabs (d.y) + std::fabs (d.z);
	double px = d.x / l1, py = d.y / l1;
	if (d.z < 0.0) {
		double qx = (1.0 - std::fabs (py)) * SignNZ (px), qy = (1.0 - std::fabs (px)) * SignNZ (py);
		px = qx; py = qy;
	}
	*u = px; *v = py;
}

V3 OctDecode (double u, double v)
{
	V3 d = { u, v, 1.0 - std::fabs (u) - std::fabs (v) };
	if (d.z < 0.0) {
		double x = (1.0 - std::fabs (d.y)) * SignNZ (d.x), y = (1.0 - std::fabs (d.x)) * SignNZ (d.y);
		d.x = x; d.y = y;
	}
	return Unit (d);
}

V3 TexelDir (int i, int j, int n) { return OctDecode ((i + 0.5) / n * 2.0 - 1.0, (j + 0.5) / n * 2.0 - 1.0); }

void WrapTexel (int *i, int *j, int n)
{
	if (*i < 0) { *i = 0; *j = n - 1 - *j; }
	else if (*i >= n) { *i = n - 1; *j = n - 1 - *j; }
	if (*j < 0) { *j = 0; *i = n - 1 - *i; }
	else if (*j >= n) { *j = n - 1; *i = n - 1 - *i; }
}

Tap OctBilinear (const V3 &d, int n)
{
	double u, v;
	OctEncode (d, &u, &v);
	double px = (u * 0.5 + 0.5) * n - 0.5, py = (v * 0.5 + 0.5) * n - 0.5;
	int i0 = (int)std::floor (px), j0 = (int)std::floor (py);
	double fx = px - i0, fy = py - j0;
	Tap t;
	for (int k = 0; k < 4; k++) {
		int i = i0 + (k & 1), j = j0 + (k >> 1);
		WrapTexel (&i, &j, n);
		t.i[k] = i; t.j[k] = j;
		t.w[k] = ((k & 1) ? fx : 1.0 - fx) * ((k >> 1) ? fy : 1.0 - fy);
	}
	return t;
}

V3 Fibonacci (int i, int n)
{
	const double g = 0.6180339887498949;
	double phi = 2.0 * CPI * (i * g - std::floor (i * g));
	double cz = 1.0 - (2.0 * i + 1.0) / n, sz = std::sqrt (std::max (0.0, 1.0 - cz * cz));
	return { std::cos (phi) * sz, std::sin (phi) * sz, cz };
}

static double Hash (uint32_t x)
{
	x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
	return (x >> 8) * (1.0 / 16777216.0);
}

void Rotation (unsigned seed, double R[9])
{
	double u1 = Hash (seed * 3u + 1u), u2 = Hash (seed * 3u + 2u), u3 = Hash (seed * 3u + 3u); // Shoemake's uniform quaternion
	double a = std::sqrt (1.0 - u1), b = std::sqrt (u1);
	double w = a * std::sin (2.0 * CPI * u2), x = a * std::cos (2.0 * CPI * u2), y = b * std::sin (2.0 * CPI * u3), z = b * std::cos (2.0 * CPI * u3);
	R[0] = 1 - 2 * (y * y + z * z); R[1] = 2 * (x * y - w * z);     R[2] = 2 * (x * z + w * y);
	R[3] = 2 * (x * y + w * z);     R[4] = 1 - 2 * (x * x + z * z); R[5] = 2 * (y * z - w * x);
	R[6] = 2 * (x * z - w * y);     R[7] = 2 * (y * z + w * x);     R[8] = 1 - 2 * (x * x + y * y);
}

double Chebyshev (double mean, double mean2, double r)
{
	if (r <= mean) return 1.0;
	double var = std::fabs (mean2 - mean * mean) + 1e-4, d = r - mean;
	double w = var / (var + d * d);
	return w * w * w;
}

int Budget (int n, double ms, double target, int nmin, int nmax)
{
	if (!(ms > 0.0)) return std::clamp (n, nmin, nmax);
	double r = std::clamp (target / ms, 0.25, 4.0);
	return std::clamp ((int)std::lround (n * (0.5 + 0.5 * r)), nmin, nmax);
}

void AtlasTexel (int probe, int n, int *x, int *y) { *x = (probe % ROW) * n; *y = (probe / ROW) * n; }

void AtlasSize (int n, int *w, int *h) { *w = ROW * n; *h = ((NPROBE + ROW - 1) / ROW) * n; }

V3 MissRadiance (const V3 &dir, const V3 &up, const V3 &sky, const V3 &ground) { return Dot (dir, up) >= 0.0 ? sky : ground; }

V3 Irradiance (const V3 &n, const V3 &up, const V3 &sky, const V3 &ground)
{
	double c = std::clamp (Dot (n, up), -1.0, 1.0);
	return Add (Scale (sky, 0.5 * (1.0 + c)), Scale (ground, 0.5 * (1.0 - c)));
}

}
