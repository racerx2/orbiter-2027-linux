// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: pure maths of the look passes (design-visual-batch 13)

#include "LookMath.h"
#include <algorithm>
#include <cmath>

namespace LookMath {

double Filmic (double x)
{
	x *= 0.568;
	return std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

double Smoothstep (double e0, double e1, double x)
{
	double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

const double GhostT[GHOSTS] = { -0.6, -0.3, 0.2, 0.45, 0.8, 1.1 };

V2 GhostPos (V2 sun, double t)
{
	return V2{ -t * sun.x, -t * sun.y }; // c + t (c - s) with c = 0
}

double HaloWeight (double sunDist)
{
	return Smoothstep(0.1, 0.3, sunDist) * (1.0 - Smoothstep(0.6, 0.9, sunDist));
}

double EdgeFade (double ndcx, double ndcy)
{
	return 1.0 - Smoothstep(0.85, 1.05, std::max(std::fabs(ndcx), std::fabs(ndcy)));
}

bool ProjectSegment (const double vp[16], const double a[3], const double b[3], double W, double H, double wmin, Seg &out)
{
	auto clip = [&](const double p[3], double h[4]) {
		for (int j = 0; j < 4; j++) h[j] = p[0] * vp[j] + p[1] * vp[4 + j] + p[2] * vp[8 + j] + vp[12 + j];
	};
	double ha[4], hb[4];
	clip(a, ha);
	clip(b, hb);
	if (ha[3] < wmin && hb[3] < wmin) return false;
	out.t0 = 0.0; out.t1 = 1.0;
	double dw = hb[3] - ha[3];
	if (ha[3] < wmin) out.t0 = (wmin - ha[3]) / dw;
	if (hb[3] < wmin) out.t1 = (wmin - ha[3]) / dw;
	double h0[4], h1[4];
	for (int j = 0; j < 4; j++) {
		h0[j] = ha[j] + (hb[j] - ha[j]) * out.t0;
		h1[j] = ha[j] + (hb[j] - ha[j]) * out.t1;
	}
	out.w0 = h0[3]; out.w1 = h1[3];
	out.x0 = (h0[0] / h0[3] + 1.0) * 0.5 * W;
	out.y0 = (1.0 - h0[1] / h0[3]) * 0.5 * H;
	out.x1 = (h1[0] / h1[3] + 1.0) * 0.5 * W;
	out.y1 = (1.0 - h1[1] / h1[3]) * 0.5 * H;
	return true;
}

double SegDist (double px, double py, double x0, double y0, double x1, double y1, double *t)
{
	double ex = x1 - x0, ey = y1 - y0, ll = ex * ex + ey * ey;
	double s = ll > 1e-12 ? std::clamp(((px - x0) * ex + (py - y0) * ey) / ll, 0.0, 1.0) : 0.0;
	if (t) *t = s;
	return std::hypot(px - (x0 + ex * s), py - (y0 + ey * s));
}

double HazeStrength (double level, double d, double w, double t)
{
	if (w <= 0.0 || d >= w) return 0.0;
	double k = 1.0 - d / w;
	return level * k * k * std::sqrt(std::max(0.0, 1.0 - t));
}

double SharpenWeight (double mn, double mx, double a)
{
	double k = std::clamp(std::min(mn, 1.0 - mx) / std::max(mx, 1e-4), 0.0, 1.0);
	return -a * std::sqrt(k);
}

double Sharpen (double c, const double n[4], double a)
{
	double mn = c, mx = c, sum = 0.0;
	for (int i = 0; i < 4; i++) { mn = std::min(mn, n[i]); mx = std::max(mx, n[i]); sum += n[i]; }
	double w = SharpenWeight(mn, mx, a);
	return std::max(0.0, (c + w * sum) / (1.0 + 4.0 * w));
}

double Vignette (double u, double v, double aspect)
{
	double r = std::hypot((u - 0.5) * aspect, v - 0.5) / std::hypot(0.5 * aspect, 0.5);
	return 1.0 - 0.18 * Smoothstep(0.45, 1.05, r);
}

}
