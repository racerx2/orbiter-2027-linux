// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: atlas uv ranges of cloud and surface tiles (design-cloud-seams 2.2)

#include "CloudAtlas.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace CloudAtlas {

static const double PI = 3.14159265358979323846;

static double U (double lng) // lng in rad to atlas u in [0, 1); the atlas repeats in u
{
	double u = std::fmod((lng + PI) / (2.0 * PI), 1.0);
	return u < 0.0 ? u + 1.0 : u;
}

Range Quad (double minlat, double maxlat, double minlng, double maxlng, double rot)
{
	Range r;
	r.x = float(U(minlng + rot));
	r.y = float((0.5 * PI - maxlat) / PI); // tv 0 at maxlat
	r.z = float((maxlng - minlng) / (2.0 * PI));
	r.w = float((maxlat - minlat) / PI);
	return r;
}

Range Hemi (int ilng, double rot)
{
	const double du = 0.5 / 512.0;             // CreateMesh_hemisphere's half-texel inset
	double s = 0.5 / (1.0 - 2.0 * du);
	Range r;
	r.z = float(s);
	r.x = float(U(-PI + PI * ilng + rot) - du * s); // tu = du at the west edge of the hemisphere
	r.w = 1.0f;                                // tv is colatitude / pi
	r.y = 0.0f;
	return r;
}

// half steps d (n x ch) box-filtered along a joint over +-R px, so texture detail does not become streaks
static void Smooth (std::vector<float> &d, int n, int ch, bool wrap)
{
	const int R = 4;
	std::vector<float> s(d.size(), 0.0f);
	for (int i = 0; i < n; i++) {
		int k = 0;
		for (int j = i - R; j <= i + R; j++) {
			int jj = wrap ? (j % n + n) % n : j;
			if (jj < 0 || jj >= n) continue;
			for (int c = 0; c < ch; c++) s[size_t(i) * ch + c] += d[size_t(jj) * ch + c];
			k++;
		}
		for (int c = 0; c < ch; c++) s[size_t(i) * ch + c] /= float(k);
	}
	d.swap(s);
}

static void Add (unsigned char &p, float v) { p = (unsigned char)std::lround(std::min(255.0f, std::max(0.0f, p + v))); }

// the source tiles do not match at their edges; a linear ramp over band px takes each side to the joint's smoothed mean
void Unseam (unsigned char *px, int w, int h, int ch, int tile, int band)
{
	band = std::min(band, tile / 2); // the ramps of two joints must not overlap
	std::vector<float> d;
	auto at = [&](int x, int y, int c) -> unsigned char & { return px[(size_t(y) * w + x) * ch + c]; };
	for (int b = 0; b < w; b += tile) { // vertical joints, x = 0 is the +-180 deg joint
		int xl = (b - 1 + w) % w, xr = b;
		d.assign(size_t(h) * ch, 0.0f);
		for (int y = 0; y < h; y++)
			for (int c = 0; c < ch; c++) d[size_t(y) * ch + c] = 0.5f * (float(at(xr, y, c)) - float(at(xl, y, c)));
		Smooth(d, h, ch, false);
		for (int y = 0; y < h; y++)
			for (int c = 0; c < ch; c++)
				for (int i = 0; i < band; i++) {
					float v = d[size_t(y) * ch + c] * float(band - i) / float(band);
					Add(at((xl - i + w) % w, y, c), v);
					Add(at((xr + i) % w, y, c), -v);
				}
	}
	for (int b = tile; b < h; b += tile) { // horizontal joints, no wrap at the poles
		d.assign(size_t(w) * ch, 0.0f);
		for (int x = 0; x < w; x++)
			for (int c = 0; c < ch; c++) d[size_t(x) * ch + c] = 0.5f * (float(at(x, b, c)) - float(at(x, b - 1, c)));
		Smooth(d, w, ch, true);
		for (int x = 0; x < w; x++)
			for (int c = 0; c < ch; c++)
				for (int i = 0; i < band; i++) {
					float v = d[size_t(x) * ch + c] * float(band - i) / float(band);
					Add(at(x, b - 1 - i, c), v);
					Add(at(x, b + i, c), -v);
				}
	}
}

void Half (const unsigned char *src, int w, int h, unsigned char *dst)
{
	int w2 = std::max(1, w / 2), h2 = std::max(1, h / 2);
	for (int y = 0; y < h2; y++)
		for (int x = 0; x < w2; x++) {
			int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
			int s = src[size_t(y0) * w + x0] + src[size_t(y0) * w + x1] + src[size_t(y1) * w + x0] + src[size_t(y1) * w + x1];
			dst[size_t(y) * w2 + x] = (unsigned char)((s + 2) / 4);
		}
}

}
