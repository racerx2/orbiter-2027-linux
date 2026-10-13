// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: volumetric cloud coverage map and noise (design-ground-graphics 4.2, 4.3)
// Gradient noise after Perlin 2002 (quintic fade), cellular noise after Worley 1996, both written here.

#include "VolCloudMap.h"
#include <algorithm>
#include <cmath>

namespace VolCloud {

static const double PI = 3.14159265358979323846;

double CloudLng (double lng, double cloudrot)
{
	double l = std::fmod(lng + cloudrot + PI, 2.0 * PI);
	if (l < 0.0) l += 2.0 * PI;
	return l - PI;
}

void Frame::Set (double _lat, double _lng)
{
	lat = _lat; lng = _lng;
	double cl = std::cos(lat), sl = std::sin(lat), cg = std::cos(lng), sg = std::sin(lng);
	U[0] = cl * cg; U[1] = sl; U[2] = cl * sg;
	E[0] = -sg; E[1] = 0.0; E[2] = cg;
	N[0] = -sl * cg; N[1] = cl; N[2] = -sl * sg;
}

void Frame::ToMap (double la, double lo, double R, double *x, double *y) const
{
	double v[3] = { std::cos(la) * std::cos(lo), std::sin(la), std::cos(la) * std::sin(lo) };
	double w = v[0] * U[0] + v[1] * U[1] + v[2] * U[2];
	if (w <= 1e-6) { *x = *y = 1e30; return; }
	*x = R * (v[0] * E[0] + v[1] * E[1] + v[2] * E[2]) / w;
	*y = R * (v[0] * N[0] + v[1] * N[1] + v[2] * N[2]) / w;
}

void Frame::FromMap (double x, double y, double R, double *la, double *lo) const
{
	double p[3];
	for (int i = 0; i < 3; i++) p[i] = U[i] + E[i] * (x / R) + N[i] * (y / R);
	double l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
	*la = std::asin(std::max(-1.0, std::min(1.0, p[1] / l)));
	*lo = std::atan2(p[2], p[0]);
}

bool NeedRebuild (double camx, double camy, bool valid)
{
	return !valid || std::hypot(camx, camy) > 20e3;
}

bool Src::Covers (double lat, double lng, double *u, double *v) const
{
	if (!d || lat < minlat || lat > maxlat) return false;
	for (double L : { lng, lng + 2.0 * PI, lng - 2.0 * PI }) {
		if (L < minlng || L > maxlng) continue;
		*u = (L - minlng) / (maxlng - minlng) * DENSN - 0.5;
		*v = (maxlat - lat) / (maxlat - minlat) * DENSN - 0.5;
		return true;
	}
	return false;
}

float Src::At (double u, double v) const
{
	u = std::max(0.0, std::min(double(DENSN - 1), u));
	v = std::max(0.0, std::min(double(DENSN - 1), v));
	int i0 = std::min(DENSN - 2, int(u)), j0 = std::min(DENSN - 2, int(v));
	double fu = u - i0, fv = v - j0;
	const uint8_t *p = d->data();
	double a = p[j0 * DENSN + i0] * (1 - fu) + p[j0 * DENSN + i0 + 1] * fu;
	double b = p[(j0 + 1) * DENSN + i0] * (1 - fu) + p[(j0 + 1) * DENSN + i0 + 1] * fu;
	return float((a * (1 - fv) + b * fv) / 255.0);
}

void Fill (const Frame &f, double R, std::vector<Src> srcs, std::vector<uint8_t> &map)
{
	std::stable_sort(srcs.begin(), srcs.end(), [](const Src &a, const Src &b) { return a.lvl > b.lvl; }); // finest first
	map.assign(size_t(MAPN) * MAPN, 0);
	double step = 2.0 * MAPHALF / MAPN;
	for (int j = 0; j < MAPN; j++)
		for (int i = 0; i < MAPN; i++) {
			double lat, lng, u, v;
			f.FromMap(-MAPHALF + (i + 0.5) * step, MAPHALF - (j + 0.5) * step, R, &lat, &lng);
			for (const Src &s : srcs)
				if (s.Covers(lat, lng, &u, &v)) {
					map[size_t(j) * MAPN + i] = uint8_t(std::lround(s.At(u, v) * 255.0f));
					break;
				}
		}
}

// noise ---------------------------------------------------------------

static uint32_t Hash (uint32_t x, uint32_t y, uint32_t z, uint32_t s)
{
	uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu ^ s * 0x9e3779b9u;
	h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
	return h;
}

static inline int Wrap (int i, int P) { int m = i % P; return m < 0 ? m + P : m; }
static inline double Fade (double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

static double Grad (int ix, int iy, int iz, int P, uint32_t seed, double fx, double fy, double fz)
{
	static const double g[12][3] = { {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1} };
	const double *v = g[Hash(Wrap(ix, P), Wrap(iy, P), Wrap(iz, P), seed) % 12];
	return v[0] * fx + v[1] * fy + v[2] * fz;
}

static double Perlin (double x, double y, double z, int P, uint32_t seed) // about -1..1, period P
{
	int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
	double fx = x - ix, fy = y - iy, fz = z - iz;
	double u = Fade(fx), v = Fade(fy), w = Fade(fz);
	double r = 0.0;
	for (int c = 0; c < 8; c++) {
		int dx = c & 1, dy = (c >> 1) & 1, dz = c >> 2;
		double k = (dx ? u : 1 - u) * (dy ? v : 1 - v) * (dz ? w : 1 - w);
		r += k * Grad(ix + dx, iy + dy, iz + dz, P, seed, fx - dx, fy - dy, fz - dz);
	}
	return r;
}

static double Worley (double x, double y, double z, int P, uint32_t seed) // 1 at a feature point, 0 a cell away
{
	int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
	double best = 1e9;
	for (int dz = -1; dz <= 1; dz++)
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++) {
				int cx = ix + dx, cy = iy + dy, cz = iz + dz;
				uint32_t h = Hash(Wrap(cx, P), Wrap(cy, P), Wrap(cz, P), seed);
				double px = cx + (h & 1023) / 1024.0, py = cy + ((h >> 10) & 1023) / 1024.0, pz = cz + ((h >> 20) & 1023) / 1024.0;
				double d2 = (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z);
				best = std::min(best, d2);
			}
	return 1.0 - std::min(1.0, std::sqrt(best));
}

static double WorleyFbm (double x, double y, double z, int F, uint32_t seed) // x, y, z in 0..1
{
	return 0.625 * Worley(x * F, y * F, z * F, F, seed) + 0.25 * Worley(x * 2 * F, y * 2 * F, z * 2 * F, 2 * F, seed + 1)
		+ 0.125 * Worley(x * 4 * F, y * 4 * F, z * 4 * F, 4 * F, seed + 2);
}

static inline uint8_t U8 (double v) { return uint8_t(std::lround(std::max(0.0, std::min(1.0, v)) * 255.0)); }

void ShapeNoise (int n, std::vector<uint8_t> &rgba)
{
	rgba.assign(size_t(n) * n * n * 4, 0);
	for (int k = 0; k < n; k++)
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++) {
				double x = (i + 0.5) / n, y = (j + 0.5) / n, z = (k + 0.5) / n;
				double pf = 0.0, a = 1.0, s = 0.0;
				for (int o = 0; o < 3; o++) {
					int F = 4 << o;
					pf += a * Perlin(x * F, y * F, z * F, F, 11 + o);
					s += a;
					a *= 0.5;
				}
				pf = std::max(0.0, std::min(1.0, pf / s * 0.7 + 0.5));
				double w4 = WorleyFbm(x, y, z, 4, 101), w8 = WorleyFbm(x, y, z, 8, 201), w16 = WorleyFbm(x, y, z, 16, 301);
				double pw = (pf - (w4 - 1.0)) / (2.0 - w4); // Perlin remapped by the Worley fbm: billows
				uint8_t *t = &rgba[((size_t(k) * n + j) * n + i) * 4];
				t[0] = U8(pw); t[1] = U8(w4); t[2] = U8(w8); t[3] = U8(w16);
			}
}

void DetailNoise (int n, std::vector<uint8_t> &rgba)
{
	rgba.assign(size_t(n) * n * n * 4, 255);
	for (int k = 0; k < n; k++)
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++) {
				double x = (i + 0.5) / n, y = (j + 0.5) / n, z = (k + 0.5) / n;
				uint8_t *t = &rgba[((size_t(k) * n + j) * n + i) * 4];
				t[0] = U8(WorleyFbm(x, y, z, 4, 401));
				t[1] = U8(WorleyFbm(x, y, z, 8, 501));
				t[2] = U8(WorleyFbm(x, y, z, 16, 601));
			}
}

} // namespace VolCloud
