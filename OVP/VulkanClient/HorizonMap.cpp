// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: terrain horizon maps (design-ground-graphics 2.3)

#include "HorizonMap.h"
#include <algorithm>
#include <cmath>

namespace Horizon {

static const double PI_ = 3.14159265358979323846;
static const double GROW = 1.2;   // ray step growth

bool Grid::Covers (double lat, double lng, double &fi, double &fj) const
{
	double dlat = maxlat - minlat, dlng = maxlng - minlng;
	if (dlat <= 0.0 || dlng <= 0.0 || h.size() < size_t(STRIDE) * STRIDE) return false;
	fi = (lat - minlat) / dlat * 256.0;
	if (fi < -1.0 || fi > 257.0) return false;
	for (int w = 0; w < 3; w++) { // the longitude seam
		double l = lng + (w == 1 ? 2.0 * PI_ : w == 2 ? -2.0 * PI_ : 0.0);
		fj = (l - minlng) / dlng * 256.0;
		if (fj >= -1.0 && fj <= 257.0) return true;
	}
	return false;
}

float Grid::At (double fi, double fj) const
{
	fi = std::min(std::max(fi, -1.0), 257.0);
	fj = std::min(std::max(fj, -1.0), 257.0);
	int i0 = std::min(int(std::floor(fi)), 256), j0 = std::min(int(std::floor(fj)), 256);
	float a = float(fi - i0), b = float(fj - j0);
	const float *p = &h[size_t(i0 + 1) * STRIDE + j0 + 1];
	float r0 = p[0] + (p[1] - p[0]) * b;
	float r1 = p[STRIDE] + (p[STRIDE + 1] - p[STRIDE]) * b;
	return r0 + (r1 - r0) * a;
}

double DmaxFor (double span)
{
	return std::min(std::max(10.0 * span, 30e3), 300e3);
}

namespace {

struct Sampler { // the finest grid covering a point
	struct Level { double lat0, lat1, lng0, lng1; size_t a, b; }; // a level's grids g[a..b) and the box round them, pads included
	std::vector<const Grid*> g; // finest first
	std::vector<Level> lv;
	explicit Sampler (const Input &in)
	{
		g.push_back(&in.owner);
		for (const Grid &x : in.grids) g.push_back(&x);
		std::stable_sort(g.begin(), g.end(), [](const Grid *a, const Grid *b) { return a->lvl > b->lvl; });
		for (size_t i = 0; i < g.size(); i++) {
			const Grid *x = g[i];
			double pi = (x->maxlat - x->minlat) / 256.0 * 1.01, pj = (x->maxlng - x->minlng) / 256.0 * 1.01;
			if (lv.empty() || g[lv.back().a]->lvl != x->lvl) lv.push_back({1e9, -1e9, 1e9, -1e9, i, i});
			Level &l = lv.back();
			l.lat0 = std::min(l.lat0, x->minlat - pi); l.lat1 = std::max(l.lat1, x->maxlat + pi);
			l.lng0 = std::min(l.lng0, x->minlng - pj); l.lng1 = std::max(l.lng1, x->maxlng + pj);
			l.b = i + 1;
		}
	}
	float At (double lat, double lng) const
	{
		double fi, fj;
		for (const Level &l : lv) {
			if (lat < l.lat0 || lat > l.lat1) continue;
			bool in = false;
			for (int w = 0; w < 3 && !in; w++) { // the longitude seam, as Covers
				double x = lng + (w == 1 ? 2.0 * PI_ : w == 2 ? -2.0 * PI_ : 0.0);
				in = x >= l.lng0 && x <= l.lng1;
			}
			if (!in) continue;
			for (size_t i = l.a; i < l.b; i++) if (g[i]->Covers(lat, lng, fi, fj)) return g[i]->At(fi, fj);
		}
		return NODATA;
	}
};

} // namespace

void Compute (const Input &in, Map &out)
{
	const Grid &o = in.owner;
	const double R = in.R;
	const double dlat = (o.maxlat - o.minlat) / 256.0, dlng = (o.maxlng - o.minlng) / 256.0;
	const Sampler far(in);

	// near grid: the owner plus a NEAR border from the finest grid that has it
	std::vector<float> ng(size_t(NG) * NG, NODATA);
	for (int a = 0; a < NG; a++) {
		int i = a - NEAR;
		for (int b = 0; b < NG; b++) {
			int j = b - NEAR;
			if (i >= -1 && i <= 257 && j >= -1 && j <= 257) ng[size_t(a) * NG + b] = o.h[size_t(i + 1) * STRIDE + j + 1];
			else ng[size_t(a) * NG + b] = far.At(o.minlat + i * dlat, o.minlng + j * dlng);
		}
	}

	auto nearAt = [&](double fa, double fb) -> float {
		int a0 = int(fa), b0 = int(fb);
		float x = float(fa - a0), y = float(fb - b0);
		const float *p = &ng[size_t(a0) * NG + b0];
		if (p[0] <= NODATA || p[1] <= NODATA || p[NG] <= NODATA || p[NG + 1] <= NODATA) return NODATA;
		float r0 = p[0] + (p[1] - p[0]) * y;
		float r1 = p[NG] + (p[NG + 1] - p[NG]) * y;
		return r0 + (r1 - r0) * x;
	};

	std::vector<float> best(size_t(N) * N * 8, -0.25f);
	const double ce[8] = { 1, 0.70710678118654752, 0, -0.70710678118654752, -1, -0.70710678118654752, 0, 0.70710678118654752 };
	const double cn[8] = { 0, 0.70710678118654752, 1, 0.70710678118654752, 0, -0.70710678118654752, -1, -0.70710678118654752 };

	std::vector<double> ts;
	for (double t = 0.75; t <= double(NEAR); t *= GROW) ts.push_back(t); // node spacings

	for (int i = 0; i < N; i++) {
		double lat = o.minlat + i * dlat;
		double my = R * dlat, mx = R * std::max(std::cos(lat), 1e-3) * dlng;
		for (int j = 0; j < N; j++) {
			float h0 = o.h[size_t(i + 1) * STRIDE + j + 1] + 1.0f;
			for (int k = 0; k < 8; k++) {
				double m = -1e30;
				for (double t : ts) {
					double tm = t * my;
					double fa = i + NEAR + cn[k] * t, fb = j + NEAR + ce[k] * tm / mx;
					float h;
					if (fa >= 0.0 && fa < NG - 1 && fb >= 0.0 && fb < NG - 1) h = nearAt(fa, fb);
					else h = far.At(lat + cn[k] * tm / R, o.minlng + j * dlng + ce[k] * tm / (R * std::max(std::cos(lat), 1e-3)));
					if (h <= NODATA) continue;
					double tn = (h - tm * tm / (2.0 * R) - h0) / tm;
					if (tn > m) m = tn;
				}
				if (m > -1e29) best[(size_t(i) * N + j) * 8 + k] = float(m / std::sqrt(1.0 + m * m));
			}
		}
	}

	// far field on every FAR-th node, 3 rays per azimuth, then up-sampled
	const int NF = (N - 1) / FAR + 1;
	std::vector<float> fv(size_t(NF) * NF * 8, -0.25f);
	const double side = 15.0 * PI_ / 180.0;
	for (int fi = 0; fi < NF; fi++) {
		int i = fi * FAR;
		double lat0 = o.minlat + i * dlat;
		double my = R * dlat, cl = std::max(std::cos(lat0), 1e-3);
		for (int fj = 0; fj < NF; fj++) {
			int j = fj * FAR;
			double lng0 = o.minlng + j * dlng;
			float h0 = o.h[size_t(i + 1) * STRIDE + j + 1] + 1.0f;
			for (int k = 0; k < 8; k++) {
				double m = -1e30;
				for (int r = -1; r <= 1; r++) {
					double th = k * PI_ / 4.0 + r * side, c = std::cos(th), s = std::sin(th);
					for (double tm = NEAR * my; tm <= in.dmax; tm *= GROW) {
						double lat = lat0 + s * tm / R;
						if (lat > PI_ / 2 || lat < -PI_ / 2) break;
						float h = far.At(lat, lng0 + c * tm / (R * cl));
						if (h <= NODATA) continue;
						double tn = (h - tm * tm / (2.0 * R) - h0) / tm;
						if (tn > m) m = tn;
					}
				}
				if (m > -1e29) fv[(size_t(fi) * NF + fj) * 8 + k] = float(m / std::sqrt(1.0 + m * m));
			}
		}
	}

	out.px.assign(size_t(W) * N * 4, 0);
	for (int i = 0; i < N; i++) {
		double gi = double(i) / FAR;
		int i0 = std::min(int(gi), NF - 2);
		float a = float(gi - i0);
		int y = 256 - i; // texture row 0 is north
		for (int j = 0; j < N; j++) {
			double gj = double(j) / FAR;
			int j0 = std::min(int(gj), NF - 2);
			float b = float(gj - j0);
			for (int k = 0; k < 8; k++) {
				const float *f = &fv[(size_t(i0) * NF + j0) * 8 + k];
				float f0 = f[0] + (f[8] - f[0]) * b;
				float f1 = f[NF * 8] + (f[NF * 8 + 8] - f[NF * 8]) * b;
				float s = std::max(best[(size_t(i) * N + j) * 8 + k], f0 + (f1 - f0) * a);
				int x = k < 4 ? j : N + j;
				out.px[(size_t(y) * W + x) * 4 + (k & 3)] = Encode(s);
			}
		}
	}
}

void Sample (const Map &m, double u, double v, uint8_t out[8])
{
	double x = std::min(std::max(u, 0.0), 1.0) * 256.0, y = std::min(std::max(v, 0.0), 1.0) * 256.0;
	int x0 = std::min(int(x), 255), y0 = std::min(int(y), 255);
	double a = x - x0, b = y - y0;
	for (int k = 0; k < 8; k++) {
		int xo = k < 4 ? 0 : N, c = k & 3;
		double t00 = m.Texel(xo + x0, y0)[c], t10 = m.Texel(xo + x0 + 1, y0)[c];
		double t01 = m.Texel(xo + x0, y0 + 1)[c], t11 = m.Texel(xo + x0 + 1, y0 + 1)[c];
		double r = (t00 + (t10 - t00) * a) * (1.0 - b) + (t01 + (t11 - t01) * a) * b;
		out[k] = uint8_t(r + 0.5);
	}
}

} // namespace Horizon
