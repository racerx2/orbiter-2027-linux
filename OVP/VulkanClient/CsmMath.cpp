// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: cascade fitting and texel snapping for the scene shadows (Engel 2006 cascades, Valient 2008 snapping, Holbert 2011 normal offset, Fernando 2005 penumbra)

#include "CsmMath.h"
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace CsmMath {

const double Radius[NC] = { 80.0, 600.0, 4500.0, 45000.0 };
const double TerrainCut[NC] = { 0.0, 0.0, 8.8, 88.0 };
const double TerrainBias[NC] = { 0.2, 0.5, 2.0, 20.0 };
const double Disk8[8][2] = {
	{ 0.570221050, 0.691418245 }, { -0.597767600, -0.715386874 }, { 0.630692208, -0.609182082 }, { -0.753514249, 0.516043067 },
	{ -0.017911456, 0.042478922 }, { 0.053921912, -0.976469520 }, { 0.901594246, 0.053632464 }, { -0.247332633, 0.965349239 } };

V3 Add (const V3 &a, const V3 &b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 Sub (const V3 &a, const V3 &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 Mul (const V3 &a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double Dot (const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross (const V3 &a, const V3 &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double Len (const V3 &a) { return std::sqrt (Dot (a, a)); }
V3 Unit (const V3 &a) { double l = Len (a); return l > 0.0 ? Mul (a, 1.0 / l) : V3{ 0.0, 0.0, 0.0 }; }

V3 MulM (const M3 &A, const V3 &v)
{
	return { A.m[0][0] * v.x + A.m[0][1] * v.y + A.m[0][2] * v.z,
	         A.m[1][0] * v.x + A.m[1][1] * v.y + A.m[1][2] * v.z,
	         A.m[2][0] * v.x + A.m[2][1] * v.y + A.m[2][2] * v.z };
}

V3 MulMT (const M3 &A, const V3 &v)
{
	return { A.m[0][0] * v.x + A.m[1][0] * v.y + A.m[2][0] * v.z,
	         A.m[0][1] * v.x + A.m[1][1] * v.y + A.m[2][1] * v.z,
	         A.m[0][2] * v.x + A.m[1][2] * v.y + A.m[2][2] * v.z };
}

double Angle (const V3 &a, const V3 &b)
{
	return std::atan2 (Len (Cross (a, b)), Dot (a, b)); // accurate for small angles
}

double Texel (int i) { return 2.0 * Radius[i] / N; }
double Depth (int i) { return 2.0 * Radius[i] + Zext; }

double Smoothstep (double e0, double e1, double x)
{
	double t = std::min (1.0, std::max (0.0, (x - e0) / (e1 - e0)));
	return t * t * (3.0 - 2.0 * t);
}

M3 Basis (const V3 &L, bool &alt)
{
	double s = Len (Cross (V3{ 0.0, 1.0, 0.0 }, L));
	if (alt) { if (s > 0.1) alt = false; }
	else if (s < 0.05) alt = true;
	V3 a = alt ? V3{ 1.0, 0.0, 0.0 } : V3{ 0.0, 1.0, 0.0 };
	V3 X = Unit (Cross (a, L)), Y = Cross (X, L), Z = Mul (L, -1.0);
	M3 B;
	B.m[0][0] = X.x; B.m[0][1] = X.y; B.m[0][2] = X.z;
	B.m[1][0] = Y.x; B.m[1][1] = Y.y; B.m[1][2] = Y.z;
	B.m[2][0] = Z.x; B.m[2][1] = Z.y; B.m[2][2] = Z.z;
	return B;
}

Box Fit (int i, const V3 &q)
{
	double t = Texel (i);
	return { std::round (q.x / t) * t, std::round (q.y / t) * t, q.z - Radius[i] - Zext };
}

Xf Transform (const M3 &B, const M3 &Rp, const V3 &cp, const Box &b)
{
	Xf x;
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			x.A.m[r][c] = B.m[r][0] * Rp.m[c][0] + B.m[r][1] * Rp.m[c][1] + B.m[r][2] * Rp.m[c][2]; // B Rp^T
	V3 q = MulM (B, cp);
	x.T = { q.x - b.mx, q.y - b.my, q.z - b.z0 };
	return x;
}

V3 Apply (const Xf &x, const V3 &v) { return Add (MulM (x.A, v), x.T); }

static void Fill (const Xf &x, const double sc[3], const double of[3], double out[4][4])
{
	const double T[3] = { x.T.x, x.T.y, x.T.z };
	for (int c = 0; c < 3; c++) {
		for (int r = 0; r < 3; r++) out[r][c] = x.A.m[c][r] * sc[c];
		out[3][c] = T[c] * sc[c] + of[c];
	}
	for (int r = 0; r < 3; r++) out[r][3] = 0.0;
	out[3][3] = 1.0;
}

int Col (int i) { return i < NC ? (i & 1) : 2; }
int Row (int i) { return i < NC ? (i >> 1) : i - NC; }

void CasterMatrix (int i, double r, double D, const Xf &x, double out[4][4])
{
	const double kx = double(N) / AW, ky = double(N) / AH;
	const double sc[3] = { kx / r, ky / r, 1.0 / D };
	const double of[3] = { (2 * Col (i) + 1) * kx - 1.0, 1.0 - (2 * Row (i) + 1) * ky, 0.0 };
	Fill (x, sc, of, out);
}

void ReceiverMatrix (int i, double r, double D, const Xf &x, double out[4][4])
{
	const double kx = double(N) / AW, ky = double(N) / AH;
	const double sc[3] = { 0.5 * kx / r, -0.5 * ky / r, 1.0 / D };
	const double of[3] = { (Col (i) + 0.5) * kx, (Row (i) + 0.5) * ky, 0.0 };
	Fill (x, sc, of, out);
}

void CasterMatrix (int i, const Xf &x, double out[4][4]) { CasterMatrix (i, Radius[i], Depth (i), x, out); }
void ReceiverMatrix (int i, const Xf &x, double out[4][4]) { ReceiverMatrix (i, Radius[i], Depth (i), x, out); }

bool FitTarget (const V3 *p, int n, double wMax, double prevT, Target &f)
{
	if (n <= 0) return false;
	double x0 = p[0].x, x1 = x0, y0 = p[0].y, y1 = y0, z0 = p[0].z, z1 = z0;
	for (int k = 1; k < n; k++) {
		x0 = std::min (x0, p[k].x); x1 = std::max (x1, p[k].x);
		y0 = std::min (y0, p[k].y); y1 = std::max (y1, p[k].y);
		z0 = std::min (z0, p[k].z); z1 = std::max (z1, p[k].z);
	}
	double e = 0.5 * std::max (x1 - x0, y1 - y0);
	double raw = std::max (std::max (e / (0.5 * N - TgtMargin), std::max (wMax, 0.0) / (2.0 * TgtKernel)), TgtMinT);
	double t = std::exp2 (std::ceil (4.0 * std::log2 (raw) - 1e-9) / 4.0); // quarter-octave steps: a still vessel keeps its grid
	if (prevT > t && prevT <= t * std::exp2 (0.25) * 1.0001 && raw > 0.97 * t) t = prevT; // one step finer only below 0.97 of it
	if (t > Texel (0)) return false;
	if (prevT <= 0.0 && t * std::exp2 (0.25) > Texel (0) && raw > 0.97 * t) return false; // back on at the coarsest step only below 0.97 of it
	f.t = t;
	f.r = 0.5 * N * t;
	f.box = { std::round (0.5 * (x0 + x1) / t) * t, std::round (0.5 * (y0 + y1) / t) * t, z0 - 1.0 };
	f.D = z1 - z0 + 4.0;
	return true;
}

bool CullSphere (int i, const Xf &x, const V3 &c, double rho)
{
	V3 s = Apply (x, c);
	double r = Radius[i];
	return std::fabs (s.x) < r + rho && std::fabs (s.y) < r + rho && s.z >= -rho && s.z <= Depth (i) + rho;
}

bool CastsIn (int i, double rho) { return rho >= 0.5 * Texel (i); }

bool Due (int i, unsigned frame)
{
	if (i < 2) return true;
	if (i == 2) return (frame & 1u) == 0u;
	return (frame & 3u) == 1u;
}

bool MovedOut (int i, const V3 &qDrawn, const V3 &qNow)
{
	double k = MoveK * Radius[i];
	return std::fabs (qNow.x - qDrawn.x) > k || std::fabs (qNow.y - qDrawn.y) > k || std::fabs (qNow.z - qDrawn.z) > k;
}

int Select (double d, double *band)
{
	for (int i = 0; i < NC; i++) {
		if (d < SelectK * Radius[i]) {
			if (band) *band = i < NC - 1 ? Smoothstep (BandK * Radius[i], SelectK * Radius[i], d) : 0.0;
			return i;
		}
	}
	if (band) *band = 0.0;
	return -1;
}

double FadeWeight (double d) { return 1.0 - Smoothstep (FadeStart, FadeEnd, d); }

double Penumbra (double dz, double tan2w) { return std::max (dz, 0.0) * tan2w; }

double FilterRadius (double w, double t) { return std::max (std::min (w / (2.0 * t), 4.0), 1.0); }

double HandOver (double w, double t) { return Smoothstep (4.0, 8.0, w / (2.0 * t)); }

// a cascade reached by a hand-over: 1 to 4 texels, on to the next one above 4 (SceneShadow.glsl CsmCascade)
static double ChainRadius (int i, double w)
{
	double t = Texel (i), r = t * FilterRadius (w, t);
	if (i >= NC - 1) return r;
	return r + (ChainRadius (i + 1, w) - r) * HandOver (w, t);
}

double EffectiveRadius (int i, double w)
{
	double t = Texel (i);
	double r0 = t * FilterRadius (w, t);
	if (i >= NC - 1) return r0;
	return r0 + (ChainRadius (i + 1, w) - r0) * HandOver (w, t);
}

double NormalOffset (double t, double cosT)
{
	double c = std::min (1.0, std::max (-1.0, cosT));
	return t * std::sqrt (1.0 - c * c);
}

double PlaneSlope (double cosT)
{
	double c = std::min (1.0, std::max (0.0, cosT));
	return std::sqrt (1.0 - c * c) / std::max (c, 0.2);
}

double ConstBias (double t, double kb) { return 0.25 * t * kb; }

double CapBias (double t, double cosT)
{
	double c = std::min (1.0, std::max (0.0, cosT));
	return 1.5 * t * std::max (std::min (std::sqrt (1.0 - c * c) / std::max (c, 0.02), 50.0) - PlaneSlope (0.2), 0.0);
}

void WorldTexel (double t, const Box &b, double &ox, double &oy)
{
	long long kx = std::llround (b.mx / t) - N / 2, ky = std::llround (b.my / t) + N / 2;
	ox = double(((kx % HashP) + HashP) % HashP);
	oy = double(((ky % HashP) + HashP) % HashP);
}

void WorldTexel (int i, const Box &b, double &ox, double &oy) { WorldTexel (Texel (i), b, ox, oy); }

void Disk (int n, double out[][2])
{
	uint64_t s = 5;
	auto rnd = [&s]() { s = s * 6364136223846793005ull + 1442695040888963407ull; return double(s >> 11) / double(1ull << 53); };
	for (int k = 0; k < n; k++) {
		int nc = k ? 32 * (k + 1) : 1;
		double bd = -1.0, bx = 0.0, by = 0.0;
		for (int c = 0; c < nc; c++) {
			double u1 = rnd (), u2 = rnd ();
			double r = std::sqrt (u1), a = 2.0 * Pi * u2;
			double px = r * std::cos (a), py = r * std::sin (a);
			double d = k ? 1e30 : 0.0;
			for (int j = 0; j < k; j++) d = std::min (d, std::hypot (px - out[j][0], py - out[j][1]));
			if (d > bd) { bd = d; bx = px; by = py; }
		}
		out[k][0] = bx; out[k][1] = by;
	}
}

bool Covers (double dShadow, double rho)
{
	if (dShadow >= CoverEnd) return false;
	for (int i = 0; i < NC; i++)
		if (dShadow + rho < SelectK * Radius[i]) return CastsIn (i, rho);
	return false;
}

}
