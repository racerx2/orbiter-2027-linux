
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space GI maths (design-ssgi 5, 12; Rev 2), the same steps as SSGI.glsl

#include "SsgiMath.h"
#include <algorithm>
#include <cmath>

namespace SsgiMath {

V3 Add (const V3 &a, const V3 &b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 Sub (const V3 &a, const V3 &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 Scale (const V3 &a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double Dot (const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross (const V3 &a, const V3 &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
V3 Unit (const V3 &a) { double l = std::sqrt(Dot(a, a)); return l > 0.0 ? Scale(a, 1.0 / l) : a; }

static double Clamp (double x, double a, double b) { return std::min(b, std::max(a, x)); }
static double Smooth (double a, double b, double x) { double t = Clamp((x - a) / (b - a), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }

double TwF (double u, bool filmic)
{
	if (!filmic) return 1.0 - std::exp(-u);
	double x = u * 0.568;
	return Clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

double TwI (double y, bool filmic)
{
	y = Clamp(y, 0.0, 0.98);
	if (!filmic) return -std::log(1.0 - y);
	double a = 2.51 - 2.43 * y, b = 0.03 - 0.59 * y, c = -0.14 * y; // (2.51 - 2.43y) x^2 + (0.03 - 0.59y) x - 0.14y = 0
	double x = (-b + std::sqrt(std::max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a);
	return x / 0.568;
}

double TmF (double u, bool glow)
{
	if (glow) return u;
	double h = u * u;
	return u * std::pow(1.0 + h * h, -0.25);
}

double TmI (double y, bool glow)
{
	if (glow) return y;
	double h = y * y;
	return y * std::pow(std::max(1.0 - h * h, 1e-4), -0.25);
}

double TaF (double u, double g) { return std::pow(Clamp(u, 0.0, 1.0), g); }
double TaI (double y, double g) { return std::pow(Clamp(y, 0.0, 1.0), 1.0 / g); }
double S2L (double c) { c = std::max(c, 0.0); return c < 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double L2S (double c) { c = std::max(c, 0.0); return c < 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }
double LightFX (double x) { return 2.0 * x / (1.0 + x); }
double LightFXSq (double S) { S = std::max(S, 0.0); return 1.8 * std::sqrt(S) / std::sqrt(2.0 + S); }

static double QMax (int cls, int fam, const Curve &c)
{
	if (cls == TREE) return 0.98;
	if (fam == METAL || fam == PBR) return c.glow ? c.uMax : 0.98;
	return c.uMax;
}

double SenderV (double y, double A, double Iraw, double Iobj, int cls, int fam, const Curve &c)
{
	double Ac = std::max(A, 0.05), v;
	if (cls == TERRAIN) {
		if (c.air) v = std::max(TwI(std::min(y, 0.98), c.filmic) - c.expo * Iraw, 0.0) / Ac / c.s;
		else v = TaI(y, c.g) / c.s;
	}
	else {
		double q = Clamp((y - Iobj) / Ac, 0.0, QMax(cls, fam, c));
		if (cls == TREE) v = TwI(q, c.filmic) / c.sTree;
		else if (fam == METAL || fam == PBR) v = TmI(q, c.glow);
		else v = q;
	}
	return Clamp(v, 0.0, c.uMax);
}

double CompositeDy (double y, double A, double Iraw, double Iobj, double rho, double E, int cls, int fam, const Curve &c)
{
	if (!(rho > 1e-5) || !(E > 0.0)) return 0.0;
	double e = std::max(c.e, 1e-6), Ac = std::max(A, 0.05), dy = 0.0;
	if (cls == TERRAIN) {
		if (c.air) {
			double u = TwI(std::min(y, 0.98), c.filmic);
			dy = TwF(u + A * c.s * rho * E, c.filmic) - TwF(u, c.filmic);
		}
		else { // rho LightFX(L): the light behind x = rho LightFX(L0) gains E / e
			double v = TaI(y, c.g) / c.s, x = v / e;
			double r = std::min(x / rho, 1.9), L0 = r / (2.0 - r);
			double dx = rho * (LightFX(L0 + E / e) - LightFX(L0));
			dy = TaF(c.s * (v + e * dx), c.g) - TaF(c.s * v, c.g);
		}
	}
	else {
		double q = Clamp((y - Iobj) / Ac, 0.0, QMax(cls, fam, c));
		if (cls == TREE) {
			double u = TwI(q, c.filmic);
			dy = (TwF(u + c.sTree * rho * E, c.filmic) - TwF(u, c.filmic)) * A;
		}
		else if (fam == METAL) { // lights add in quadrature (LightFXSq)
			double v = TmI(q, c.glow), L = std::min(v / e / rho, 1.75);
			double S = 2.0 * L * L / (3.24 - L * L), S1 = S + (E / e) * (E / e);
			double v1 = v + e * rho * std::max(LightFXSq(S1) - L, 0.0);
			dy = (TmF(v1, c.glow) - TmF(v, c.glow)) * A;
		}
		else if (fam == PBR) { // linear light, shown as Tm(sRGB(c) e)
			double v = TmI(q, c.glow), cl = S2L(v / e) + rho * S2L(E / e);
			dy = (TmF(e * L2S(cl), c.glow) - TmF(v, c.glow)) * A;
		}
		else { // legacy and base tiles: the light is clamped at 1 before the texture
			double v = q;
			dy = std::min(rho * E, std::max(e * rho - v, 0.0)) * A;
		}
	}
	return std::isfinite(dy) ? std::max(dy, 0.0) : 0.0;
}

double Quant10 (double x) { return std::round(Clamp(x, 0.0, 1.0) * 1023.0) / 1023.0; }

void OctEncode (const V3 &n, double *u, double *v)
{
	double s = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
	double x = n.x / s, y = n.y / s;
	if (n.z < 0.0) {
		double ox = (1.0 - std::fabs(y)) * (x >= 0.0 ? 1.0 : -1.0), oy = (1.0 - std::fabs(x)) * (y >= 0.0 ? 1.0 : -1.0);
		x = ox; y = oy;
	}
	*u = Quant10(x * 0.5 + 0.5);
	*v = Quant10(y * 0.5 + 0.5);
}

V3 OctDecode (double u, double v)
{
	double x = u * 2.0 - 1.0, y = v * 2.0 - 1.0, z = 1.0 - std::fabs(x) - std::fabs(y);
	double t = std::max(-z, 0.0);
	x += x >= 0.0 ? -t : t;
	y += y >= 0.0 ? -t : t;
	return Unit({ x, y, z });
}

double SliceMass (double g) { return std::cos(g) + g * std::sin(g); }

double SliceCdf (double t, double g)
{
	t = Clamp(t, g - 0.5 * PI, g + 0.5 * PI);
	double s = std::sin(g), c = std::cos(g), neg = 0.5 * c + 0.5 * (g - 0.5 * PI) * s;
	if (t <= 0.0) return 0.25 * std::cos(2.0 * t - g) - 0.5 * t * s + 0.25 * c + 0.5 * (g - 0.5 * PI) * s;
	return neg - 0.25 * std::cos(2.0 * t - g) + 0.5 * t * s + 0.25 * c;
}

unsigned BitMask (double ta, double tb, double g, double jit)
{
	double M = SliceMass(g);
	int i0 = int(std::floor(32.0 * SliceCdf(std::min(ta, tb), g) / M + jit)); // the slice's offset dithers the sector edges: unbiased coverage
	int i1 = int(std::floor(32.0 * SliceCdf(std::max(ta, tb), g) / M + jit));
	i0 = std::min(32, std::max(0, i0));
	i1 = std::min(32, std::max(0, i1));
	int n = i1 - i0;
	if (n <= 0) return 0u;
	unsigned m = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
	return m << i0;
}

int PopCount (unsigned m) { int n = 0; for (; m; m &= m - 1) n++; return n; }

static double Angle (const V3 &D, const V3 &V, const V3 &T) { return std::atan2(Dot(D, T), Dot(D, V)); }

unsigned SampleBits (const V3 &P, const V3 &S, double thick, const V3 &V, const V3 &T, double g, double jit)
{
	V3 B = Add(S, Scale(Unit(S), thick));
	return BitMask(Angle(Sub(S, P), V, T), Angle(Sub(B, P), V, T), g, jit);
}

double SliceGamma (const V3 &N, const V3 &V, const V3 &T, double *np)
{
	V3 Np = Unit(Cross(V, T));
	V3 n = Sub(N, Scale(Np, Dot(N, Np)));
	*np = std::sqrt(Dot(n, n));
	return Clamp(std::atan2(Dot(n, T), Dot(n, V)), -0.5 * PI, 0.5 * PI);
}

double Radius (double d, double R0, double k) { return std::max(R0, k * d); }
double PixelRadius (double R, double d, double tanY, double H2, double pmin, double pmax) { return Clamp(R * H2 / (2.0 * d * tanY), pmin, pmax); }
double RadiusFromPixels (double px, double d, double tanY, double H2) { return px * 2.0 * d * tanY / H2; }

void ProbeWeights (int envMapMode, bool irradiance, bool probesExist, double w[4])
{
	bool on = envMapMode != 0 && irradiance && probesExist;
	w[TERRAIN] = on ? 0.0 : 1.0;                       // the probe draws the planet
	w[TREE] = 1.0;
	w[BASE] = (on && envMapMode == 2) ? 0.0 : 1.0;     // and the base structures in mode 2 (flag 0x20)
	w[VESSEL] = 1.0;                                   // not the vessel itself
}

double RayLength (double cosDir, double r0, double r1)
{
	double y = r0 * cosDir, z2 = r0 * r0 - y * y;
	return std::sqrt(std::max(r1 * r1 - z2, 0.0)) + y;
}

void Gauss7 (double cosDir, double r0, double dist, const double iH[2], const Atmo &a, double out[2])
{
	static const double n[7] = { 0.0714, 0.21428, 0.35714, 0.5, 0.64285, 0.78571, 0.92857 };
	static const double w[7] = { 0.1295, 0.27971, 0.38183, 0.41796, 0.38183, 0.27971, 0.1295 };
	double x = 2.0 * r0 * cosDir, r2 = r0 * r0;
	out[0] = out[1] = 0.0;
	for (int i = 0; i < 7; i++) {
		double d = dist * n[i], alt = std::sqrt(std::max(r2 + d * (d - x), 0.0)) - a.PlanetRad;
		for (int k = 0; k < 2; k++) out[k] += std::exp(Clamp(-alt * iH[k], -20.0, 20.0)) * w[i];
	}
	out[0] *= dist * 0.5;
	out[1] *= dist * 0.5;
}

void MeshAmbient (const Atmo &a, const V3 &p, const V3 &toSun, double out[3])
{
	for (int i = 0; i < 3; i++) out[i] = a.cfg[i];
	double r = std::sqrt(Dot(p, p)), alt = r - a.PlanetRad;
	if (alt > a.AtmoAlt * 0.5) return; // in space: the config level only
	V3 up = Scale(p, 1.0 / r);
	double dNS = Dot(up, toSun);
	double fA = (1.0 - Smooth(0.0, a.TW_Dst, -dNS)) * a.twBld;
	double ang = -dNS, rm[2];
	Gauss7(ang, r, RayLength(ang, r, a.AtmoRad), a.iH, a, rm);
	double cs[3], mx = 0.0;
	for (int i = 0; i < 3; i++) { cs[i] = std::exp(-(a.RayWave[i] * rm[0] * a.rmO[0] + a.MieWave[i] * rm[1] * a.rmO[1])) * a.cSun[i]; mx = std::max(mx, cs[i]); }
	if (mx > 1.0) for (double &c : cs) c /= mx;
	V3 u = Unit({ a.cAmbient[0] + 2.0 * cs[0], a.cAmbient[1] + 2.0 * cs[1], a.cAmbient[2] + 2.0 * cs[2] });
	double k = fA * a.cAmbient[1] * std::exp(-alt * a.iH[0]) * a.rmI[0] * 6e5;
	out[0] += u.x * k;
	out[1] += u.y * k;
	out[2] += u.z * k;
}

}
