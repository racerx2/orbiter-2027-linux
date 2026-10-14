// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: SSR tracing maths (design-ssr 16; McGuire & Mara 2014, Uludag 2014, Heitz 2018, Jimenez 2014, Karis 2014, Lagarde 2013)

#include "SsrMath.h"
#include <algorithm>
#include <cmath>

using namespace PostMath;

namespace SsrMath {

static double Clamp (double x, double a, double b) { return std::min(std::max(x, a), b); }
static Vec3 Cross (const Vec3 &a, const Vec3 &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

double Hdr (double x, double expo) { return 1.0 - std::exp(-expo * x); }
double HdrInv (double y, double expo) { return -std::log(1.0 - Clamp(y, 0.0, 0.985)) / expo; }

double Filmic (double x, double expo)
{
	double t = 0.568 * expo * x;
	return Clamp((t * (2.51 * t + 0.03)) / (t * (2.43 * t + 0.59) + 0.14), 0.0, 1.0);
}

// y (2.43 t^2 + 0.59 t + 0.14) = 2.51 t^2 + 0.03 t: a < 0 and c >= 0 below y = 1.033, so one root is >= 0
double FilmicInv (double y, double expo)
{
	y = Clamp(y, 0.0, 0.985);
	double a = 2.43 * y - 2.51, b = 0.59 * y - 0.03, c = 0.14 * y;
	double t = (-b - std::sqrt(std::max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a);
	return t / (0.568 * expo);
}

double MeshCurve (double x) { x = std::max(x, 0.0); return x * std::pow(1.0 + x * x * x * x, -0.25); }
double MeshCurveInv (double y) { double c = Clamp(y, 0.0, 0.99); return c / std::pow(1.0 - c * c * c * c, 0.25); }

double SrgbToLinear (double c)
{
	c = std::max(c, 0.0);
	return c < 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double LinearToSrgb (double c)
{
	c = std::max(c, 0.0);
	return c < 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

double MeshEncode (double L, double T, double I, double e, bool curve, bool srgb)
{
	double g = (srgb ? LinearToSrgb(L) : L) * e;
	if (curve) g = MeshCurve(g);
	return g * T + I;
}

double MeshDecode (double y, double T, double I, double eSrc, bool curve, bool srgb)
{
	double c = (y - I) / std::max(T, 1e-3);
	if (curve) c = MeshCurveInv(c);
	c = std::max(c, 0.0) / eSrc;
	return srgb ? SrgbToLinear(c) : c;
}

static void Row (const Mat4 &M, const double v[4], double c[4])
{
	for (int j = 0; j < 4; j++) c[j] = v[0] * M.m[0][j] + v[1] * M.m[1][j] + v[2] * M.m[2][j] + v[3] * M.m[3][j];
}

bool Segment (const Mat4 &M, const Vec3 &P, const Vec3 &R, double W, double H, double nearW, double L, Seg &g)
{
	double p[4] = { P.x, P.y, P.z, 1.0 }, r[4] = { R.x, R.y, R.z, 0.0 }, c0[4], d[4];
	Row(M, p, c0);
	Row(M, r, d);
	if (c0[3] < nearW) return false;
	g.clipped = false;
	if (c0[3] + L * d[3] < nearW) { L = (nearW - c0[3]) / d[3]; g.clipped = true; } // toward the camera: ends on the near plane
	double c1[4];
	for (int j = 0; j < 4; j++) c1[j] = c0[j] + d[j] * L;
	g.X0 = (c0[0] / c0[3] * 0.5 + 0.5) * W; g.Y0 = (0.5 - c0[1] / c0[3] * 0.5) * H; g.s0 = 1.0 / c0[3];
	g.X1 = (c1[0] / c1[3] * 0.5 + 0.5) * W; g.Y1 = (0.5 - c1[1] / c1[3] * 0.5) * H; g.s1 = 1.0 / c1[3];
	return true;
}

bool Vanish (const Mat4 &Msky, const Vec3 &R, double W, double H, double *X, double *Y)
{
	double r[4] = { R.x, R.y, R.z, 0.0 }, d[4];
	Row(Msky, r, d);
	if (d[3] <= 1e-6) return false;
	*X = (d[0] / d[3] * 0.5 + 0.5) * W;
	*Y = (0.5 - d[1] / d[3] * 0.5) * H;
	return true;
}

float HiZ::At (int k, int x, int y) const
{
	int w = Wp >> k, h = Hp >> k;
	x = std::min(std::max(x, 0), w - 1);
	y = std::min(std::max(y, 0), h - 1);
	return lv[k][size_t(y) * w + x];
}

int HiZLevels (int W, int H)
{
	int m = std::max(W, H), L = 1;
	while ((m >>= 1) > 0) L++;
	return std::min(L, 9);
}

void BuildHiZ (const std::vector<float> &s, int W, int H, HiZ &z)
{
	z.W = W; z.H = H; z.L = HiZLevels(W, H);
	int a = 1 << (z.L - 1);
	z.Wp = (W + a - 1) / a * a;
	z.Hp = (H + a - 1) / a * a;
	z.lv.assign(z.L, {});
	z.lv[0].assign(size_t(z.Wp) * z.Hp, 0.0f);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) z.lv[0][size_t(y) * z.Wp + x] = s[size_t(y) * W + x];
	for (int k = 1; k < z.L; k++) {
		int w = z.Wp >> k, h = z.Hp >> k, pw = z.Wp >> (k - 1);
		z.lv[k].assign(size_t(w) * h, 0.0f);
		const std::vector<float> &p = z.lv[k - 1];
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				size_t i = size_t(2 * y) * pw + 2 * x;
				z.lv[k][size_t(y) * w + x] = std::max(std::max(p[i], p[i + 1]), std::max(p[i + pw], p[i + pw + 1]));
			}
	}
}

// the part of X0 + t D inside [0, S) on one axis
static bool ClipAxis (double X0, double D, double S, double &tA, double &tB)
{
	if (std::fabs(D) < 1e-9) return X0 >= 0.0 && X0 < S;
	double a = (0.0 - X0) / D, b = (S - X0) / D;
	tA = std::max(tA, std::min(a, b));
	tB = std::min(tB, std::max(a, b));
	return true;
}

// s = 1/z is affine in t along the screen segment; a cell is skipped when the segment through it is in front of its nearest surface
WalkOut Walk (const HiZ &z, const Seg &g, const WalkPrm &p)
{
	WalkOut o;
	double Dx = g.X1 - g.X0, Dy = g.Y1 - g.Y0, ds = g.s1 - g.s0;
	double tA = 0.0, tB = 1.0;
	if (!ClipAxis(g.X0, Dx, z.W, tA, tB) || !ClipAxis(g.Y0, Dy, z.H, tA, tB) || tA >= tB) return o;
	bool full = tB >= 1.0 && !g.clipped;
	double len = std::sqrt(Dx * Dx + Dy * Dy);
	if (len < 1.0) { o.end = full; return o; }
	double t = std::max(tA, p.startPx / len), eps = 0.02 / len;
	int lvl = std::min(1, z.L - 1);
	while (o.it < p.maxIt && t < tB) {
		o.it++;
		double X = g.X0 + t * Dx, Y = g.Y0 + t * Dy;
		double sz = double(1 << lvl);
		double cx = std::floor((X + p.jx) / sz), cy = std::floor((Y + p.jy) / sz);
		double Sc = z.At(lvl, int(cx), int(cy));
		double tx = 1e30;
		if (Dx > 0.0) tx = std::min(tx, ((cx + 1.0) * sz - p.jx - g.X0) / Dx);
		else if (Dx < 0.0) tx = std::min(tx, (cx * sz - p.jx - g.X0) / Dx);
		if (Dy > 0.0) tx = std::min(tx, ((cy + 1.0) * sz - p.jy - g.Y0) / Dy);
		else if (Dy < 0.0) tx = std::min(tx, (cy * sz - p.jy - g.Y0) / Dy);
		tx = std::max(tx, t) + eps;
		double te = std::min(tx, tB);
		double sA = g.s0 + t * ds, sB = g.s0 + te * ds;
		if (std::min(sA, sB) > Sc) { t = tx; lvl = std::min(lvl + 1, z.L - 1); continue; }
		if (lvl > 0) {
			if (ds < 0.0) t = Clamp((Sc - g.s0) / ds, t, te); // away from the camera: on to the cell's nearest plane
			lvl--;
			continue;
		}
		double zs = 1.0 / Sc, zn = 1.0 / std::max(sA, sB);
		if (zn <= zs + p.tau0 + p.tauRel * zs) {
			o.hit = true;
			o.t = ds < 0.0 ? Clamp((Sc - g.s0) / ds, t, te) : t;
			o.X = g.X0 + o.t * Dx;
			o.Y = g.Y0 + o.t * Dy;
			return o;
		}
		t = tx; // behind a thin object: on
	}
	if (t < tB && p.finish > 0) { // the budget ran out (a ray that hugs the surface it leaves): point samples of level 0 finish the segment (r3-ssr2)
		o.finished = true;
		double t0 = t;
		for (int i = 0; i < p.finish; i++) {
			double ti = t0 + (i + 0.5) / p.finish * (tB - t0), si = g.s0 + ti * ds;
			double X = g.X0 + ti * Dx, Y = g.Y0 + ti * Dy;
			double Si = z.At(0, int(std::floor(X + p.jx)), int(std::floor(Y + p.jy)));
			if (Si > 0.0 && si <= Si && 1.0 / si <= 1.0 / Si + p.tau0 + p.tauRel / Si) {
				o.hit = true;
				o.t = ti;
				o.X = X;
				o.Y = Y;
				return o;
			}
		}
		t = tB;
	}
	o.end = full && t >= tB;
	return o;
}

WalkOut March (const HiZ &z, const Seg &g, const WalkPrm &p, double stepPx)
{
	WalkOut o;
	double Dx = g.X1 - g.X0, Dy = g.Y1 - g.Y0, ds = g.s1 - g.s0;
	double tA = 0.0, tB = 1.0;
	if (!ClipAxis(g.X0, Dx, z.W, tA, tB) || !ClipAxis(g.Y0, Dy, z.H, tA, tB) || tA >= tB) return o;
	bool full = tB >= 1.0 && !g.clipped;
	double len = std::sqrt(Dx * Dx + Dy * Dy);
	if (len < 1.0) { o.end = full; return o; }
	double dt = stepPx / len;
	for (double t = std::max(tA, p.startPx / len); t < tB; t += dt) {
		o.it++;
		double X = g.X0 + t * Dx, Y = g.Y0 + t * Dy;
		double Sc = z.At(0, int(std::floor(X + p.jx)), int(std::floor(Y + p.jy)));
		double s = g.s0 + t * ds;
		if (Sc > 0.0 && s <= Sc && 1.0 / s <= 1.0 / Sc + p.tau0 + p.tauRel / Sc) {
			o.hit = true; o.t = t; o.X = X; o.Y = Y;
			return o;
		}
	}
	o.end = full;
	return o;
}

bool SkyAt (const HiZ &z, double X, double Y, const WalkPrm &p)
{
	if (X < 0.0 || Y < 0.0 || X >= z.W || Y >= z.H) return false;
	return z.At(0, int(std::floor(X + p.jx)), int(std::floor(Y + p.jy))) <= 0.0f;
}

Vec3 Reflect (const Vec3 &I, const Vec3 &N) { return Sub(I, Scale(N, 2.0 * Dot(I, N))); }

// the depth at a pixel centre is off the plane by up to ~0.7 pixel angles x distance x tan(view to normal); a lift of k = 1.5 pixel angles clears it
Vec3 Lift (const Vec3 &P, const Vec3 &Ng, double k) { return Add(P, Scale(Ng, k * std::sqrt(Dot(P, P)))); }

Vec3 KeepAbove (const Vec3 &R, const Vec3 &Ng, double c)
{
	double d = Dot(R, Ng);
	return d >= c ? R : Unit(Add(R, Scale(Ng, c - d)));
}

// Heitz 2018: the visible normals of the stretched hemisphere, sampled on a disk warped toward the view
Vec3 Vndf (const Vec3 &V, const Vec3 &N, double a, double u1, double u2)
{
	Vec3 T1 = std::fabs(N.z) < 0.999 ? Unit(Cross(Vec3{ 0.0, 0.0, 1.0 }, N)) : Unit(Cross(Vec3{ 1.0, 0.0, 0.0 }, N));
	Vec3 T2 = Cross(N, T1);
	Vec3 Vh = Unit(Vec3{ a * Dot(V, T1), a * Dot(V, T2), Dot(V, N) });
	double l2 = Vh.x * Vh.x + Vh.y * Vh.y;
	Vec3 A1 = l2 > 0.0 ? Scale(Vec3{ -Vh.y, Vh.x, 0.0 }, 1.0 / std::sqrt(l2)) : Vec3{ 1.0, 0.0, 0.0 };
	Vec3 A2 = Cross(Vh, A1);
	double r = std::sqrt(u1), ph = 6.283185307179586 * u2;
	double t1 = r * std::cos(ph), t2 = r * std::sin(ph);
	double s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * std::sqrt(std::max(0.0, 1.0 - t1 * t1)) + s * t2;
	Vec3 Nh = Add(Add(Scale(A1, t1), Scale(A2, t2)), Scale(Vh, std::sqrt(std::max(0.0, 1.0 - t1 * t1 - t2 * t2))));
	Vec3 Hl = Unit(Vec3{ a * Nh.x, a * Nh.y, std::max(1e-6, Nh.z) });
	return Add(Add(Scale(T1, Hl.x), Scale(T2, Hl.y)), Scale(N, Hl.z));
}

double Ign (double x, double y)
{
	double f = 0.06711056 * x + 0.00583715 * y;
	f -= std::floor(f);
	f *= 52.9829189;
	return f - std::floor(f);
}

double ConeLod (double dPx, double a, double k, double maxLod) { return Clamp(std::log2(std::max(1.0, dPx * a * k)), 0.0, maxLod); }

double Smoothstep (double a, double b, double x)
{
	double t = Clamp((x - a) / (b - a), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

double EdgeFade (double u, double v, double aspect, double width)
{
	return Smoothstep(0.0, width, std::min(u, 1.0 - u) * aspect) * Smoothstep(0.0, width, std::min(v, 1.0 - v));
}

double EnvBRDFApprox (double f0, double perc, double NoV)
{
	const double c0[4] = { -1.0, -0.0275, -0.572, 0.022 }, c1[4] = { 1.0, 0.0425, 1.04, -0.04 };
	double r[4];
	for (int i = 0; i < 4; i++) r[i] = perc * c0[i] + c1[i];
	double a004 = std::min(r[0] * r[0], std::exp2(-9.28 * NoV)) * r[0] + r[1];
	double A = -1.04 * a004 + r[2], B = 1.04 * a004 + r[3];
	return f0 * A + B;
}

Wet WetModel (double w)
{
	w = Clamp(w, 0.0, 1.0);
	return { 0.30 + (0.02 - 0.30) * w, 0.04 + (0.02 - 0.04) * w, 1.0 + (0.55 - 1.0) * w };
}

bool JumpTest (double moved, double prevMoved, double turnRad)
{
	return moved > 1000.0 + 2.0 * prevMoved || turnRad > 0.7853981633974483;
}

Mat4 BodyMatrix (const Mat3 &Rnow, const Vec3 &oNow, const Mat3 &Rprev, const Vec3 &oPrev, const Vec3 &camNow, const Vec3 &camPrev, const Mat4 &VPprev)
{
	return Mul(Rigid(Rnow, oNow, Rprev, oPrev, camNow, camPrev), VPprev);
}

}
