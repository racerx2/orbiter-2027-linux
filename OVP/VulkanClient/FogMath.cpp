// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: height fog maths (design-height-fog, Rev 2); the density, the step placement and the noise are mirrored in shaders/HeightFog.glsl

#include "FogMath.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace FogMath {

static const double PI = 3.14159265358979323846;

static inline double Smooth (double a, double b, double x)
{
	double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
	return t * t * (3.0 - 2.0 * t);
}

static inline float Smoothf (float a, float b, float x)
{
	float t = std::min(1.0f, std::max(0.0f, (x - a) / (b - a)));
	return t * t * (3.0f - 2.0f * t);
}

static inline double Dot (const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static inline double SInt (double u) { return u <= 0.0 ? 0.0 : (u >= 1.0 ? u - 0.5 : u * u * u - 0.5 * u * u * u * u); } // integral of smoothstep from 0

// mean of 1 - smoothstep(c - w, c + w, h) while h runs linearly from ha to hb
static double LidMean (double c, double w, double ha, double hb)
{
	double dh = hb - ha, lo = c - w, w2 = 2.0 * w;
	if (std::fabs(dh) < 0.05 * w) { double u = std::min(1.0, std::max(0.0, (0.5 * (ha + hb) - lo) / w2)); return 1.0 - u * u * (3.0 - 2.0 * u); }
	auto G = [&](double h) { return h - w2 * SInt((h - lo) / w2); };
	return std::max(0.0, (G(hb) - G(ha)) / dh);
}

// mean of exp(-max(h - g, 0) / H) while h runs linearly from ha to hb
static double ExpMean (double g, double H, double ha, double hb)
{
	double a = (ha - g) / H, b = (hb - g) / H;
	if (a > b) std::swap(a, b);
	if (b - a < 1e-3) return std::exp(-std::max(0.5 * (a + b), 0.0)); // as the GPU's float
	double I = 0.0;
	if (a < 0.0) I += std::min(b, 0.0) - a;
	if (b > 0.0) I += std::exp(-std::max(a, 0.0)) - std::exp(-b);
	return I / (b - a);
}

// planes ---------------------------------------------------------------

void Plane::Set (double la, double lo)
{
	lat = la; lng = lo;
	double cl = std::cos(la), sl = std::sin(la), cg = std::cos(lo), sg = std::sin(lo);
	U[0] = cl * cg; U[1] = sl; U[2] = cl * sg;
	E[0] = -sg; E[1] = 0.0; E[2] = cg;
	N[0] = -sl * cg; N[1] = cl; N[2] = -sl * sg;
}

bool Plane::ToMap (const double p[3], double R, double *x, double *y) const
{
	double w = Dot(p, U);
	if (w <= 1e-9 * std::sqrt(Dot(p, p))) return false;
	*x = R * Dot(p, E) / w;
	*y = R * Dot(p, N) / w;
	return true;
}

void Plane::FromMap (double x, double y, double R, double *la, double *lo) const
{
	double p[3];
	for (int i = 0; i < 3; i++) p[i] = U[i] + E[i] * (x / R) + N[i] * (y / R);
	double l = std::sqrt(Dot(p, p));
	*la = std::asin(std::max(-1.0, std::min(1.0, p[1] / l)));
	*lo = std::atan2(p[2], p[0]);
}

bool Map::Sample (const double p[3], float out[4], double *r) const
{
	double x, y;
	if (px.size() < size_t(n) * n * 4 || !f.ToMap(p, R, &x, &y)) return false;
	double qx = x / half, qy = y / half;
	if (r) *r = std::max(std::fabs(qx), std::fabs(qy));
	double u = (qx * 0.5 + 0.5) * n - 0.5, v = (-qy * 0.5 + 0.5) * n - 0.5; // texel centres, clamped to the edge as the sampler
	u = std::min(double(n - 1), std::max(0.0, u));
	v = std::min(double(n - 1), std::max(0.0, v));
	int i0 = std::min(int(u), n - 2), j0 = std::min(int(v), n - 2);
	double a = u - i0, b = v - j0;
	const float *t00 = &px[(size_t(j0) * n + i0) * 4], *t01 = t00 + 4, *t10 = t00 + size_t(n) * 4, *t11 = t10 + 4;
	for (int c = 0; c < 4; c++)
		out[c] = float((t00[c] * (1 - a) + t01[c] * a) * (1 - b) + (t10[c] * (1 - a) + t11[c] * a) * b);
	return true;
}

// density --------------------------------------------------------------

float LidDepth (const Params &P, const float f[4])
{
	float kf = 0.3f + 0.7f * Smoothf(30.0f, 300.0f, f[1]); // flat ground holds a shallow layer; the cold pool deepens with the valley
	return (P.D0 * kf + P.kv * std::min(f[1], 1200.0f)) * std::sqrt(std::max(f[3], 0.0f));
}

static inline float SeaCut (float s) { return 1.0f - Smoothf(1.25f, 1.6f, s); } // a floor far below sea level is a dry basin, not the encoded sea (review r2 NIT 3)

float SeaW (const float f[4], float ef)
{
	float s = -f[0] / SEAENC;
	return Smoothf(0.15f, 0.55f, s) * SeaCut(s) * ef; // full over the water and at the shore, none a few km inland (nor over raised land)
}

float SeaKL (const float f[4])
{
	float s = -f[0] / SEAENC;
	return Smoothf(0.25f, 0.9f, s) * SeaCut(s); // open water: the full bank; it slopes down to the shore and over sheltered water
}

static inline float SeaLidAt (const Params &P, float kl, float n) // the sea bank's top
{
	return P.Ds * kl * (1.0f + 0.3f * (2.0f * n - 1.0f));
}

static inline float SeaSoft (const Params &P, float kl) { return 0.2f * P.Ds * kl + 5.0f; }

float Density (const Params &P, const float f[4], float ef, double h, float n)
{
	float hf = float(h), m = std::max(f[3], 0.0f);
	float D = LidDepth(P, f), L = f[0] + D + 0.25f * D * (2.0f * n - 1.0f), Dl = 0.15f * D + 8.0f;
	float rf = P.sf * m * (1.0f - Smoothf(L - Dl, L + Dl, hf)) * (0.6f + 0.8f * n) * ef;
	float G = P.Gmean + (f[0] + f[1] - P.Gmean) * ef, hg = hf - G;
	float rh = P.sh * std::exp(-std::max(hg, 0.0f) / P.Hh) * (1.0f - Smoothf(P.Hinv - 150.0f, P.Hinv + 150.0f, hg));
	float sea = SeaW(f, ef), kl = SeaKL(f);
	float Ls = SeaLidAt(P, kl, n), Dls = SeaSoft(P, kl);
	float rs = P.ss * sea * (1.0f - Smoothf(Ls - Dls, Ls + Dls, hf)) * (0.5f + n);
	return rf + rh + rs;
}

float DensitySeg (const Params &P, const float f[4], float ef, double ha, double hb, float n)
{
	float m = std::max(f[3], 0.0f), hm = float(0.5 * (ha + hb));
	float D = LidDepth(P, f), L = f[0] + D + 0.25f * D * (2.0f * n - 1.0f), Dl = 0.15f * D + 8.0f;
	float rf = P.sf * m * float(LidMean(L, Dl, ha, hb)) * (0.6f + 0.8f * n) * ef;
	float G = P.Gmean + (f[0] + f[1] - P.Gmean) * ef;
	float rh = P.sh * float(ExpMean(G, P.Hh, ha, hb)) * (1.0f - Smoothf(P.Hinv - 150.0f, P.Hinv + 150.0f, hm - G));
	float sea = SeaW(f, ef), kl = SeaKL(f);
	float Ls = SeaLidAt(P, kl, n), Dls = SeaSoft(P, kl);
	float rs = P.ss * sea * float(LidMean(Ls, Dls, ha, hb)) * (0.5f + n);
	return rf + rh + rs;
}

float Bound (const Params &P, const float f[4], float ef, double ha, double hb)
{
	float m = std::max(f[3], 0.0f), hm = float(0.5 * (ha + hb)), hmin = float(std::min(ha, hb));
	float D = LidDepth(P, f);
	float rf = hm < f[0] + 1.55f * D + 16.0f ? P.sf * m * 1.4f * ef : 0.0f;
	float G = P.Gmean + (f[0] + f[1] - P.Gmean) * ef;
	float rh = hmin - G < P.Hinv + 150.0f ? P.sh * float(ExpMean(G, P.Hh, ha, hb)) : 0.0f;
	float sea = SeaW(f, ef);
	float rs = hm < 1.5f * P.Ds + 5.0f ? P.ss * sea * 1.5f : 0.0f;
	return rf + rh + rs;
}

float LidTop (const Params &P, const float f[4])
{
	float top = f[0] + 1.55f * LidDepth(P, f) + 16.0f;
	if (SeaW(f, 1.0f) > 0.0f) top = std::max(top, 1.5f * P.Ds + 5.0f);
	return top;
}

// noise ----------------------------------------------------------------

uint32_t Hash3 (int x, int y, int z)
{
	uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
	h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
	return h;
}

float Drift (double x, double y, double z, double off)
{
	double q[3] = { (x + off) / 700.0, y / 700.0, z / 700.0 };
	int k[3];
	double u[3];
	for (int i = 0; i < 3; i++) {
		double fl = std::floor(q[i]), fr = q[i] - fl;
		k[i] = int(fl);
		u[i] = fr * fr * (3.0 - 2.0 * fr);
	}
	double r = 0.0;
	for (int c = 0; c < 8; c++) {
		int dx = c & 1, dy = (c >> 1) & 1, dz = c >> 2;
		double w = (dx ? u[0] : 1 - u[0]) * (dy ? u[1] : 1 - u[1]) * (dz ? u[2] : 1 - u[2]);
		r += w * (Hash3((k[0] + dx) & 63, k[1] + dy, k[2] + dz) * (1.0 / 4294967296.0));
	}
	return float(r);
}

// rays -----------------------------------------------------------------

double RayAlt (double hc, double R, double mu, double t)
{
	double Rc = R + hc, a = 2.0 * t * Rc * mu + t * t;
	return hc + a / (std::sqrt(std::max(Rc * Rc + a, 0.0)) + Rc);
}

void ScanPoints (double t0, double t1, double *tk)
{
	double SA = std::min(2000.0, std::max(30.0, 0.1 * (t1 - t0)));
	double s0 = std::asinh(t0 / SA), s1 = std::asinh(t1 / SA);
	for (int k = 0; k <= SCAN; k++) tk[k] = SA * std::sinh(s0 + (s1 - s0) * k / SCAN);
	tk[0] = t0;
	tk[SCAN] = t1;
}

double Weight (double tau, double tauBefore, double len, double total)
{
	return (1.0 - std::exp(-tau)) * std::exp(-0.5 * tauBefore) + 0.02 * len / std::max(total, 1e-6);
}

void Steps (const double *t, const double *w, int np, int n, double *edges)
{
	double c[MAXP];
	c[0] = 0.0;
	for (int k = 0; k + 1 < np; k++) c[k + 1] = c[k] + std::max(w[k], 0.0);
	double W = c[np - 1];
	edges[0] = t[0];
	int k = 0;
	for (int i = 1; i < n; i++) {
		double target = W * i / n;
		while (k < np - 2 && c[k + 1] < target) k++;
		double span = c[k + 1] - c[k];
		double x = span > 0.0 ? std::min(1.0, std::max(0.0, (target - c[k]) / span)) : 0.0;
		edges[i] = t[k] + (t[k + 1] - t[k]) * x;
	}
	edges[n] = t[np - 1];
}

float BoundLid (const Params &P, const float f[4])
{
	return f[3] > 0.0f && P.sf > 0.0f ? f[0] + 1.55f * LidDepth(P, f) + 16.0f : -1e30f;
}

float SeaLid (const Params &P, const float f[4], float ef)
{
	return P.ss > 0.0f && SeaW(f, ef) > 0.0f ? 1.5f * P.Ds + 5.0f : -1e30f;
}

void MakeLayout (const Params &P, const FieldFn &field, const double o[3], const double d[3], double R, double t0, double t1, Layout &L)
{
	struct Pt { double t, h; float f[4], ef; };
	double ro = std::sqrt(Dot(o, o)), hc = ro - R, mu = Dot(o, d) / ro, tp = -(R + hc) * mu; // tp: the ray's lowest point
	auto Make = [&](double t) {
		Pt q;
		q.t = t;
		q.h = RayAlt(hc, R, mu, t);
		double p[3] = { o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t };
		if (!field(p, q.f, &q.ef)) { q.f[0] = P.Gmean; q.f[1] = 0.0f; q.f[2] = LIT; q.f[3] = 0.0f; q.ef = 0.0f; }
		return q;
	};
	auto Lerp = [&](const Pt &a, const Pt &b, double t) {
		Pt q;
		double x = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 0.0;
		q.t = t;
		q.h = RayAlt(hc, R, mu, t);
		for (int c = 0; c < 4; c++) q.f[c] = float(a.f[c] + (b.f[c] - a.f[c]) * x);
		q.ef = float(a.ef + (b.ef - a.ef) * x);
		return q;
	};
	double tauB = 0.0, total = t1 - t0;
	int extra = 0; // inserted points: at most MAXP - SCAN - 1, so the scan always fits
	L.n = 0;
	auto Push = [&](const Pt &a, const Pt &b) { // one interval with a monotone altitude
		double len = b.t - a.t;
		double rb = std::max(Bound(P, a.f, a.ef, a.h, b.h), Bound(P, b.f, b.ef, a.h, b.h));
		L.t[L.n] = a.t;
		L.w[L.n] = Weight(rb * len, tauB, len, total);
		L.n++;
		tauB += rb * len;
	};
	auto Split = [&](const Pt &a, const Pt &b) { // at the crossings of the bound lids
		double tc[2];
		int nc = 0;
		for (int s = 0; s < 2; s++) {
			double ga = a.h - (s ? SeaLid(P, a.f, a.ef) : BoundLid(P, a.f)), gb = b.h - (s ? SeaLid(P, b.f, b.ef) : BoundLid(P, b.f));
			if ((ga < 0.0) != (gb < 0.0) && std::fabs(ga) < 1e20 && std::fabs(gb) < 1e20 && extra < MAXP - SCAN - 1) { tc[nc++] = a.t + (b.t - a.t) * ga / (ga - gb); extra++; }
		}
		if (nc == 2 && tc[1] < tc[0]) std::swap(tc[0], tc[1]);
		Pt prev = a;
		for (int i = 0; i < nc; i++) {
			if (tc[i] <= prev.t || tc[i] >= b.t) continue;
			Pt c = Lerp(a, b, tc[i]);
			Push(prev, c);
			prev = c;
		}
		Push(prev, b);
	};
	double tk[SCAN + 1];
	ScanPoints(t0, t1, tk);
	Pt a = Make(tk[0]);
	for (int k = 0; k < SCAN; k++) {
		Pt b = Make(tk[k + 1]);
		if (tp > a.t && tp < b.t && extra < MAXP - SCAN - 1) {
			extra++;
			Pt c = Lerp(a, b, tp);
			Split(a, c);
			Split(c, b);
		}
		else Split(a, b);
		a = b;
	}
	L.t[L.n] = t1;
	L.w[L.n] = 0.0;
	L.n++;
}

double Transmittance (const Params &P, const FieldFn &field, const double o[3], const double d[3], double R, double t0, double t1, int steps, double jit, double drift)
{
	if (!(t1 > t0)) return 1.0;
	steps = std::min(64, std::max(1, steps));
	double ro = std::sqrt(Dot(o, o)), hc = ro - R, mu = Dot(o, d) / ro;
	Layout L;
	MakeLayout(P, field, o, d, R, t0, t1, L);
	double e[65];
	Steps(L.t, L.w, L.n, steps, e);
	double T = 1.0, ha = RayAlt(hc, R, mu, e[0]);
	for (int i = 0; i < steps && T > 1e-4; i++) {
		double dt = e[i + 1] - e[i], hb = RayAlt(hc, R, mu, e[i + 1]);
		if (dt <= 0.0) { ha = hb; continue; }
		double t = e[i] + dt * jit;
		double p[3] = { o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t };
		float f[4], ef;
		if (!field(p, f, &ef)) { f[0] = P.Gmean; f[1] = 0.0f; f[2] = LIT; f[3] = 0.0f; ef = 0.0f; }
		float nz = drift < 0.0 ? 0.5f : Drift(p[0], p[1], p[2], drift);
		T *= std::exp(-double(DensitySeg(P, f, ef, ha, hb, nz)) * dt);
		ha = hb;
	}
	return T;
}

// sun and time ---------------------------------------------------------

SunTime Sun (double lat, double lng, const double s[3], int sense)
{
	SunTime st;
	double dec = std::asin(std::max(-1.0, std::min(1.0, s[1]))), lngS = std::atan2(s[2], s[0]);
	double dl = std::remainder(lng - lngS, 2.0 * PI);
	st.tau = std::fmod(12.0 + (sense < 0 ? -1.0 : 1.0) * dl * 12.0 / PI + 48.0, 24.0);
	double la = std::max(-1.5706, std::min(1.5706, lat));
	double c = -std::tan(la) * std::tan(dec);
	if (c <= -1.0) { st.polarDay = true; st.dayLen = 24.0; }
	else if (c >= 1.0) { st.polarNight = true; st.dayLen = 0.0; }
	else st.dayLen = 2.0 * std::acos(c) * 12.0 / PI;
	double rise = 12.0 - 0.5 * st.dayLen, set = 12.0 + 0.5 * st.dayLen;
	st.a = std::fmod(st.tau - rise + 48.0, 24.0);
	st.b = std::fmod(st.tau - set + 48.0, 24.0);
	st.up = st.polarDay || (!st.polarNight && st.a < st.dayLen);
	return st;
}

double Diurnal (const SunTime &st, double moist, double *Hinv)
{
	double day = st.dayLen, night = 24.0 - day;
	double peak = 0.1 + 0.9 * Smooth(0.0, 6.0, night);         // short nights cool little
	double trough = 0.1 + 0.9 * (1.0 - Smooth(0.0, 8.0, day)); // short days do not burn it off
	double f;
	if (st.up) {
		double e = std::min(3.5, 0.5 * day), s0 = std::min(0.5, 0.15 * day);
		f = peak - (peak - trough) * Smooth(s0, e, st.a);
	}
	else {
		double x = Smooth(0.0, night, st.b);
		f = trough + (peak - trough) * x * x;
	}
	double kd = 1.0 - Smooth(0.0, 1.0, night), kn = 1.0 - Smooth(0.0, 1.0, day); // the polar constants over the last hour
	f += (0.15 - f) * kd;
	f += (0.4 * moist - f) * kn;
	if (Hinv) {
		double hd = 400.0 + 1100.0 * Smooth(1.0, 8.0, st.up ? st.a : day);
		double k = st.up ? 1.0 : 1.0 - Smooth(0.0, std::min(2.0, 0.8 * night), st.b); // back to 400 m before sunrise
		double h = 400.0 + (hd - 400.0) * k;
		*Hinv = h + (1500.0 - h) * kd;
	}
	return f;
}

double FogDayProb (double moist, double nightLen)
{
	return std::min(0.5, (0.12 + 0.28 * moist) * (0.75 + 0.4 * Smooth(9.0, 15.0, nightLen)));
}

static uint32_t Hash4 (int x, int y, int z, int t, uint32_t seed)
{
	uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u ^ uint32_t(t) * 2654435761u ^ seed * 0x9e3779b9u;
	h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
	return h;
}

double DayNoise (double lat, double lng, double mjd, uint32_t seed, double cell, double days)
{
	double q[4] = { std::cos(lat) * std::cos(lng) / cell, std::sin(lat) / cell, std::cos(lat) * std::sin(lng) / cell, mjd / days };
	int k[4];
	double u[4];
	for (int i = 0; i < 4; i++) {
		double fl = std::floor(q[i]), fr = q[i] - fl;
		k[i] = int(fl);
		u[i] = fr * fr * (3.0 - 2.0 * fr);
	}
	double v = 0.0, var = 0.0;
	for (int c = 0; c < 16; c++) {
		double w = 1.0;
		for (int i = 0; i < 4; i++) w *= ((c >> i) & 1) ? u[i] : 1.0 - u[i];
		v += w * (Hash4(k[0] + (c & 1), k[1] + ((c >> 1) & 1), k[2] + ((c >> 2) & 1), k[3] + (c >> 3), seed) * (1.0 / 4294967296.0));
		var += w * w;
	}
	double z = (v - 0.5) / std::sqrt(var / 12.0); // the weighted sum's spread at this point: near uniform everywhere through the normal CDF
	return 0.5 * (1.0 + std::erf(z / std::sqrt(2.0)));
}

double FogDay (double lat, double lng, double mjd, double p)
{
	return 1.35 * Smooth(1.0 - p, 1.15 - p, DayNoise(lat, lng, mjd, 1u, 0.05, 1.0));
}

double SeaDay (double lat, double lng, double mjd, double moist)
{
	double p = 0.12 * moist;
	if (p <= 0.0) return 0.0;
	return Smooth(1.0 - p, 1.1 - p, DayNoise(lat, lng, mjd, 2u, 0.05, 2.0));
}

double Patch (double lat, double lng, double mjd, double R)
{
	double u = 0.65 * DayNoise(lat, lng, mjd, 4u, PATCH / R, 1.0) + 0.35 * DayNoise(lat, lng, mjd, 5u, PATCH / (2.3 * R), 1.0); // two octaves: no lattice look
	return Smooth(0.28, 0.72, u); // fog patches and clear gaps a few km across, a new pattern each day
}

Params Frame (const FrameIn &in, SunTime *out)
{
	SunTime st = Sun(in.lat, in.lng, in.sun, in.sense);
	if (out) *out = st;
	double moist = in.wet ? Smooth(0.0, 0.15, in.wFar) : 0.0;
	double Hinv = 400.0;
	double ftod = Diurnal(st, moist, &Hinv);
	double night = 24.0 - st.dayLen;
	double fseason = 0.55 + 0.6 * Smooth(9.0, 15.0, night);
	double hum = 0.3 + 0.7 * moist;
	double fclear = in.covMean < 0.0 ? 1.0 : 1.0 - 0.6 * Smooth(0.5, 0.9, in.covMean);
	double body = in.wet ? 1.0 : 0.25;
	double eve = st.up ? 0.0 : 1.0 - Smooth(0.0, 3.0, st.b);
	Params P;
	P.sf = float(0.008 * ftod * fseason * fclear * hum * body);
	P.D0 = float(20.0 + 160.0 * ftod * fseason * hum);
	P.kv = float(0.25 * (0.4 + 0.6 * ftod));
	P.sh = float(2.5e-5 * hum * body * (0.4 + 0.6 * std::max(ftod, eve)));
	P.Hinv = float(Hinv);
	P.Hh = 350.0f;
	double sea = in.wet ? SeaDay(in.lat, in.lng, in.mjd, moist) : 0.0;
	P.ss = float(0.006 * sea * hum * (0.55 + 0.45 * ftod));
	P.Ds = float(60.0 + 200.0 * sea);
	P.Gmean = float(in.Gmean);
	return P;
}

// bake -----------------------------------------------------------------

static bool GridAt (const Horizon::Grid &G, double lat, double lng, float *h)
{
	double dlat = G.maxlat - G.minlat, dlng = G.maxlng - G.minlng;
	if (dlat <= 0.0 || dlng <= 0.0 || G.h.size() < size_t(Horizon::STRIDE) * Horizon::STRIDE) return false;
	double fi = (lat - G.minlat) / dlat * 256.0;
	if (fi < 0.0 || fi > 256.0) return false;
	for (double w : { 0.0, 2.0 * PI, -2.0 * PI }) {
		double fj = (lng + w - G.minlng) / dlng * 256.0;
		if (fj < 0.0 || fj > 256.0) continue;
		int i0 = std::min(int(fi), 255), j0 = std::min(int(fj), 255);
		double a = fi - i0, b = fj - j0;
		const float *p = &G.h[size_t(i0 + 1) * Horizon::STRIDE + j0 + 1];
		double r0 = p[0] + (p[1] - p[0]) * b, r1 = p[Horizon::STRIDE] + (p[Horizon::STRIDE + 1] - p[Horizon::STRIDE]) * b;
		*h = float(r0 + (r1 - r0) * a);
		return true;
	}
	return false;
}

bool GridHeight (const std::vector<const Horizon::Grid*> &g, double lat, double lng, float *h)
{
	for (const Horizon::Grid *G : g)
		if (G && GridAt(*G, lat, lng, h)) return true;
	return false;
}

static void MinFilterInt (const std::vector<float> &src, int n, int r, std::vector<float> &dst)
{
	std::vector<float> tmp(src.size());
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			float m = 1e30f;
			for (int k = std::max(0, i - r); k <= std::min(n - 1, i + r); k++) { float v = src[size_t(j) * n + k]; if (v > Horizon::NODATA * 0.5f) m = std::min(m, v); }
			tmp[size_t(j) * n + i] = m;
		}
	dst.assign(src.size(), Horizon::NODATA);
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			float m = 1e30f;
			for (int k = std::max(0, j - r); k <= std::min(n - 1, j + r); k++) m = std::min(m, tmp[size_t(k) * n + i]);
			dst[size_t(j) * n + i] = m < 1e29f ? m : Horizon::NODATA;
		}
}

// a radius in texels with a fraction: the min over the two nearest whole radii, mixed
static void MinFilter (const std::vector<float> &src, int n, double r, std::vector<float> &dst)
{
	int r0 = std::max(0, int(std::floor(r)));
	double fr = r - r0;
	MinFilterInt(src, n, r0, dst);
	if (fr < 1e-3) return;
	std::vector<float> b;
	MinFilterInt(src, n, r0 + 1, b);
	for (size_t k = 0; k < dst.size(); k++)
		if (dst[k] > Horizon::NODATA * 0.5f && b[k] > Horizon::NODATA * 0.5f) dst[k] = float(dst[k] + (b[k] - dst[k]) * fr);
		else if (b[k] > Horizon::NODATA * 0.5f) dst[k] = b[k];
}

// box mean over a radius in texels with a fraction (the outer ring weighted by it), NODATA skipped
static void BoxFilter (const std::vector<float> &src, int n, double r, std::vector<float> &dst)
{
	int ri = int(std::ceil(r - 1e-9));
	auto W = [&](int k) { int a = std::abs(k); return a <= int(std::floor(r + 1e-9)) ? 1.0 : r - std::floor(r); };
	std::vector<double> s(src.size(), 0.0), c(src.size(), 0.0);
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			double a = 0.0, b = 0.0;
			for (int k = std::max(0, i - ri); k <= std::min(n - 1, i + ri); k++) { float v = src[size_t(j) * n + k]; if (v > Horizon::NODATA * 0.5f) { double w = W(k - i); a += w * v; b += w; } }
			s[size_t(j) * n + i] = a;
			c[size_t(j) * n + i] = b;
		}
	dst.assign(src.size(), Horizon::NODATA);
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			double a = 0.0, b = 0.0;
			for (int k = std::max(0, j - ri); k <= std::min(n - 1, j + ri); k++) { double w = W(k - j); a += w * s[size_t(k) * n + i]; b += w * c[size_t(k) * n + i]; }
			if (b > 0.0) dst[size_t(j) * n + i] = float(a / b);
		}
}

void Floor (const std::vector<float> &mean, int n, double step, std::vector<float> &F, std::vector<float> &G, std::vector<float> *lo)
{
	double rm = std::max(1.0, 2000.0 / step), rb = std::min(rm, std::max(1.0, 1500.0 / step)), rg = std::max(1.0, 3000.0 / step); // metric radii, the same in both cascades
	std::vector<float> a;
	MinFilter(mean, n, rm, a);
	BoxFilter(a, n, rb, F);
	for (size_t k = 0; k < F.size(); k++) if (mean[k] > Horizon::NODATA * 0.5f && F[k] > mean[k]) F[k] = mean[k]; // never above the ground
	BoxFilter(mean, n, rg, G);
	if (lo) lo->swap(a);
}

void Water (const std::vector<float> &mean, const std::vector<float> &lo, int n, double step, bool wet, std::vector<float> &sea, std::vector<float> &lake)
{
	sea.assign(size_t(n) * n, 0.0f);
	lake.assign(size_t(n) * n, 0.0f);
	if (!wet) return;
	const float ND = Horizon::NODATA * 0.5f;
	std::vector<uint8_t> flag(size_t(n) * n, 0);
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			size_t k = size_t(j) * n + i;
			float h = mean[k];
			if (h <= ND) continue;
			if (h <= 0.5f && h >= -2.0f) { sea[k] = 1.0f; continue; } // sea clamped at 0 (map-water 3); dry land below sea level is no sea (review r2 NIT 3)
			float mn = h, mx = h;
			bool all = i >= 2 && j >= 2 && i < n - 2 && j < n - 2; // a whole 5 x 5 window
			for (int b = j - 2; b <= j + 2 && all; b++)
				for (int a = i - 2; a <= i + 2; a++) {
					if (a < 0 || b < 0 || a >= n || b >= n) continue;
					float v = mean[size_t(b) * n + a];
					if (v <= ND) { all = false; break; }
					mn = std::min(mn, v); mx = std::max(mx, v);
				}
			if (all && mx - mn <= 0.5f && lo[k] > ND && h - lo[k] < 0.75f) flag[k] = 1; // a flat local minimum: DEM lakes are flattened
		}
	std::vector<float> raw(size_t(n) * n, 0.0f);
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			if (!flag[size_t(j) * n + i]) continue;
			int c = 0;
			for (int b = std::max(0, j - 1); b <= std::min(n - 1, j + 1); b++)
				for (int a = std::max(0, i - 1); a <= std::min(n - 1, i + 1); a++) c += flag[size_t(b) * n + a];
			if (c >= 4) raw[size_t(j) * n + i] = 1.0f;
		}
	BoxFilter(raw, n, std::max(1.0, 1000.0 / step), lake);
	for (float &v : lake) v = std::max(0.0f, v);
}

void ShadowSweep (const std::vector<float> &hp, int n, double step, double sE, double sN, double sU, const std::function<float (double x, double y)> &up, std::vector<float> &Hp)
{
	const float LOW = -1e30f, ND = Horizon::NODATA * 0.5f;
	Hp.resize(hp.size());
	for (size_t k = 0; k < hp.size(); k++) Hp[k] = hp[k] > ND ? hp[k] : LOW;
	double ch = std::sqrt(sE * sE + sN * sN);
	if (ch < 1e-4) return; // the sun at the zenith
	double uE = sE / ch, uN = sN / ch, tanE = sU / ch, half = 0.5 * n * step;
	bool alongX = std::fabs(uE) >= std::fabs(uN);
	double dU = step / (alongX ? std::fabs(uE) : std::fabs(uN)), drop = dU * tanE;
	auto X = [&](double i) { return -half + (i + 0.5) * step; };
	auto Y = [&](double j) { return half - (j + 0.5) * step; };
	auto Upstream = [&](double x, double y) { float v = up ? up(x, y) : Horizon::NODATA; return v > ND ? double(v) : double(LOW); };
	if (alongX) {
		int sx = uE > 0.0 ? 1 : -1;
		double dj = -uN / std::fabs(uE); // row shift toward the sun (rows run south)
		for (int c = 0; c < n; c++) {
			int i = sx > 0 ? n - 1 - c : c, iu = i + sx;
			for (int j = 0; j < n; j++) {
				double ju = j + dj, hu;
				if (iu < 0 || iu >= n || ju < 0.0 || ju > n - 1) hu = Upstream(X(i) + sx * step, Y(ju));
				else {
					int j0 = std::min(int(ju), n - 2);
					double a = ju - j0, h0 = Hp[size_t(j0) * n + iu], h1 = Hp[size_t(j0 + 1) * n + iu];
					hu = (h0 <= LOW * 0.5 || h1 <= LOW * 0.5) ? std::max(h0, h1) : h0 + (h1 - h0) * a;
				}
				float &o = Hp[size_t(j) * n + i];
				if (hu > LOW * 0.5) o = std::max(o, float(hu - drop));
			}
		}
	}
	else {
		int sy = uN > 0.0 ? 1 : -1; // +1: the sun to the north, rows from 0
		double di = uE / std::fabs(uN);
		for (int c = 0; c < n; c++) {
			int j = sy > 0 ? c : n - 1 - c, ju = j - sy;
			for (int i = 0; i < n; i++) {
				double iu = i + di, hu;
				if (ju < 0 || ju >= n || iu < 0.0 || iu > n - 1) hu = Upstream(X(iu), Y(j) + sy * step);
				else {
					int i0 = std::min(int(iu), n - 2);
					double a = iu - i0, h0 = Hp[size_t(ju) * n + i0], h1 = Hp[size_t(ju) * n + i0 + 1];
					hu = (h0 <= LOW * 0.5 || h1 <= LOW * 0.5) ? std::max(h0, h1) : h0 + (h1 - h0) * a;
				}
				float &o = Hp[size_t(j) * n + i];
				if (hu > LOW * 0.5) o = std::max(o, float(hu - drop));
			}
		}
	}
}

void TileRange (double lat0, double lat1, double lng0, double lng1, int lvl, int *ia, int *ib, int *ja, int *jb)
{
	int nlat = 1 << lvl, nlng = 2 << lvl;
	*ia = std::min(nlat - 1, std::max(0, int(std::floor((0.5 - lat1 / PI) * nlat))));
	*ib = std::min(nlat - 1, std::max(0, int(std::floor((0.5 - lat0 / PI) * nlat))));
	*ja = int(std::floor(lng0 / (2.0 * PI) * nlng + nlng / 2));
	*jb = int(std::floor(lng1 / (2.0 * PI) * nlng + nlng / 2));
	if (*jb - *ja + 1 > nlng) *jb = *ja + nlng - 1;
}

void GridBox (double lat, double lng, double ext, double R, double *la0, double *la1, double *lo0, double *lo1)
{
	double dl = std::min(ext / R, 0.5 * PI), s = std::sin(dl), c = std::cos(lat);
	double dg = s >= c ? PI : std::asin(s / c); // a spherical cap's longitude half width; it holds the pole when dl >= 90 deg - |lat| (review SHOULD 1)
	*la0 = std::max(-0.5 * PI, lat - dl);
	*la1 = std::min(0.5 * PI, lat + dl);
	*lo0 = lng - dg;
	*lo1 = lng + dg;
}

void Bake (const BakeIn &in, Map &out)
{
	const int n = MAPN;
	const float ND = Horizon::NODATA * 0.5f;
	double step = 2.0 * in.half / n, R = in.R;
	out.f.Set(in.lat, in.lng);
	out.half = in.half;
	out.R = R;
	out.n = n;
	std::vector<const Horizon::Grid*> g;
	for (const auto &p : in.grids) if (p && p->h.size() >= size_t(Horizon::STRIDE) * Horizon::STRIDE) g.push_back(p.get());
	std::stable_sort(g.begin(), g.end(), [](const Horizon::Grid *a, const Horizon::Grid *b) { return a->lvl > b->lvl; }); // finest first

	// texel corners, then each texel's mean and highest corner
	const int nc = n + 1;
	std::vector<float> cz(size_t(nc) * nc);
	for (int j = 0; j < nc; j++)
		for (int i = 0; i < nc; i++) {
			double la, lo;
			out.f.FromMap(-in.half + i * step, in.half - j * step, R, &la, &lo);
			float h;
			cz[size_t(j) * nc + i] = GridHeight(g, la, lo, &h) ? h : Horizon::NODATA;
		}
	std::vector<float> mean(size_t(n) * n), mx(size_t(n) * n);
	size_t covered = 0;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			float c[4] = { cz[size_t(j) * nc + i], cz[size_t(j) * nc + i + 1], cz[size_t(j + 1) * nc + i], cz[size_t(j + 1) * nc + i + 1] };
			bool ok = c[0] > ND && c[1] > ND && c[2] > ND && c[3] > ND;
			size_t k = size_t(j) * n + i;
			mean[k] = ok ? 0.25f * (c[0] + c[1] + c[2] + c[3]) : Horizon::NODATA;
			mx[k] = ok ? std::max(std::max(c[0], c[1]), std::max(c[2], c[3])) : Horizon::NODATA;
			covered += ok;
		}
	out.cover = float(double(covered) / (double(n) * n));

	std::vector<float> F, G, lo, sea, lake, ws;
	Floor(mean, n, step, F, G, &lo);
	Water(mean, lo, n, step, in.wet, sea, lake);
	std::vector<float> seaSrc(sea); // the sea fog's weight: the open water, reaching SEAR over the shore (uncovered texels skipped)
	for (size_t k = 0; k < seaSrc.size(); k++) if (mean[k] <= ND) seaSrc[k] = Horizon::NODATA;
	BoxFilter(seaSrc, n, std::max(1.0, SEAR / step), ws);
	double wsum = 0.0, gsum = 0.0;
	for (size_t k = 0; k < mean.size(); k++)
		if (mean[k] > ND) { wsum += std::min(1.0f, sea[k] + lake[k]); gsum += G[k] > ND ? G[k] : mean[k]; }
	out.water = covered ? float(wsum / covered) : 0.0f;
	float gmean = covered ? float(gsum / covered) : 0.0f;
	double p = FogDayProb(Smooth(0.0, 0.15, in.wFar >= 0.0 ? in.wFar : out.water), in.nightLen);

	// sun-shadow heights on the centre's tangent plane: the sun ray is a straight line there
	double sE = Dot(in.sun, out.f.E), sN = Dot(in.sun, out.f.N), sU = Dot(in.sun, out.f.U);
	auto R2 = [&](double x, double y) { return (x * x + y * y) / (2.0 * R); };
	std::vector<float> hp(mean.size()), Hp;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			size_t k = size_t(j) * n + i;
			double x = -in.half + (i + 0.5) * step, y = in.half - (j + 0.5) * step;
			hp[k] = mx[k] > ND ? float(mx[k] - R2(x, y)) : Horizon::NODATA;
		}
	bool dark = sU < std::sin(-2.0 * PI / 180.0);
	if (!dark) {
		std::shared_ptr<const Map> outer = in.outer;
		const Plane &pl = out.f;
		auto up = [&](double x, double y) -> float {
			if (!outer) return Horizon::NODATA;
			double la, lo, q[3];
			pl.FromMap(x, y, R, &la, &lo);
			q[0] = std::cos(la) * std::cos(lo); q[1] = std::sin(la); q[2] = std::cos(la) * std::sin(lo);
			float s[4];
			double r;
			if (!outer->Sample(q, s, &r) || r > 1.0 || s[2] <= LIT * 0.5f || s[2] >= DARK * 0.5f) return Horizon::NODATA;
			return float(s[2] - R2(x, y));
		};
		ShadowSweep(hp, n, step, sE, sN, sU, up, Hp);
	}

	// strength: fog days (smooth over 320 km, so a 9 x 9 lattice), lakes, no radiation fog over the sea
	const int nd = 9;
	double dn[nd][nd];
	for (int b = 0; b < nd; b++)
		for (int a = 0; a < nd; a++) {
			double la, lo;
			out.f.FromMap(-in.half + 2.0 * in.half * a / (nd - 1), in.half - 2.0 * in.half * b / (nd - 1), R, &la, &lo);
			dn[b][a] = FogDay(la, lo, in.mjd, p);
		}

	out.px.assign(size_t(n) * n * 4, 0.0f);
	float Fmin = 1e30f, Gmax = -1e30f, Ltop = -1e30f;
	Params Pmax;
	Pmax.D0 = 204.0f; Pmax.kv = 0.25f; Pmax.Ds = 260.0f;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			size_t k = size_t(j) * n + i;
			float *o = &out.px[k * 4];
			if (mean[k] <= ND) { o[0] = gmean; o[1] = 0.0f; o[2] = LIT; o[3] = -1.0f; continue; } // uncovered: no fog, and the near map gives way to the far one there (review MUST 1)
			double x = -in.half + (i + 0.5) * step, y = in.half - (j + 0.5) * step;
			float Fk = F[k] > ND ? F[k] : mean[k], Gk = G[k] > ND ? G[k] : mean[k];
			Fk = std::min(Fk, mean[k]);
			float w = ws[k] > ND ? std::min(1.0f, std::max(0.0f, ws[k])) : 0.0f;
			o[0] = Fk - SEAENC * w;
			o[1] = std::max(Gk - Fk, 0.0f) + SEAENC * w;
			if (dark) o[2] = DARK;
			else o[2] = Hp[k] > -1e29f ? float(Hp[k] + R2(x, y)) : LIT;
			double fa = (x / in.half * 0.5 + 0.5) * (nd - 1), fb = (0.5 - y / in.half * 0.5) * (nd - 1);
			int a0 = std::min(nd - 2, std::max(0, int(fa))), b0 = std::min(nd - 2, std::max(0, int(fb)));
			double wa = fa - a0, wb = fb - b0;
			double md = (dn[b0][a0] * (1 - wa) + dn[b0][a0 + 1] * wa) * (1 - wb) + (dn[b0 + 1][a0] * (1 - wa) + dn[b0 + 1][a0 + 1] * wa) * wb;
			double keep = std::max(double(lake[k]), Smooth(30.0, 300.0, std::max(Gk - Fk, 0.0f))); // valleys and lakes keep their fog; flat ground breaks into patches
			double patch = 1.0;
			if (keep < 1.0 && sea[k] < 1.0f && md > 0.0) { // the same function in both maps (review r2 SHOULD 2); the far map's texels average 2 x 2 points
				int ns = step > 0.25 * PATCH ? 2 : 1;
				double ps = 0.0, la, lo;
				for (int b = 0; b < ns; b++)
					for (int a = 0; a < ns; a++) {
						out.f.FromMap(x + (ns > 1 ? (a - 0.5) * 0.5 * step : 0.0), y + (ns > 1 ? (b - 0.5) * 0.5 * step : 0.0), R, &la, &lo);
						ps += Patch(la, lo, in.mjd, R);
					}
				patch = keep + (1.0 - keep) * ps / (ns * ns);
			}
			o[3] = float(std::min(3.0, std::max(0.0, md * (1.0 + 1.5 * lake[k]) * (1.0 - sea[k]) * patch)));
			Fmin = std::min(Fmin, o[0]);
			Gmax = std::max(Gmax, o[0] + o[1]);
			Ltop = std::max(Ltop, LidTop(Pmax, o));
		}
	out.Fmin = covered ? Fmin : 0.0f;
	out.Gmax = covered ? Gmax : 0.0f;
	out.Gmean = gmean;
	out.Ltop = covered ? Ltop : 0.0f;
}

uint16_t ToHalf (float v)
{
	uint32_t x;
	std::memcpy(&x, &v, 4);
	uint32_t sign = (x >> 16) & 0x8000u, ax = x & 0x7fffffffu;
	if (ax > 0x7f800000u) return uint16_t(sign | 0x7e00u);  // NaN stays NaN
	if (ax >= 0x477fe000u) return uint16_t(sign | 0x7bffu); // 65504 and up, inf included: the largest finite
	if (ax < 0x38800000u) {                                 // below 2^-14: subnormal
		if (ax < 0x33000000u) return uint16_t(sign);
		uint32_t m = (ax & 0x7fffffu) | 0x800000u;
		int shift = 126 - int(ax >> 23);
		uint32_t q = m >> shift, rem = m & ((1u << shift) - 1u), hw = 1u << (shift - 1);
		if (rem > hw || (rem == hw && (q & 1u))) q++;
		return uint16_t(sign | q);
	}
	uint32_t h = ((((ax >> 23) - 127u + 15u) << 10) | ((ax & 0x7fffffu) >> 13));
	uint32_t rem = ax & 0x1fffu;
	if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) h++; // to nearest even; a carry moves into the exponent
	return uint16_t(sign | h);
}

bool NeedRebuild (const Rebuild &r, bool nearMap)
{
	if (!r.valid || !r.sameObj) return true;
	double dmax = nearMap ? 3e3 : 20e3, sbase = nearMap ? 0.5 : 1.0, sigMin = nearMap ? 2.0 : 5.0;
	if (r.dist > dmax) return true;
	double sth = sbase;
	if (r.sunElevDeg > -3.0 && r.sunElevDeg < 10.0) sth = std::min(sbase, std::max(0.1, 0.1 * r.sunElevDeg)); // review SHOULD 10
	if (r.sunDeg > sth) return true;
	if (r.age > 120.0) return true;
	return r.sigChanged && r.age >= sigMin;
}

}
