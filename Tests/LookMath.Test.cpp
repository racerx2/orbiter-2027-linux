// not upstream: look pass maths (OVP/VulkanClient/LookMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "LookMath.h"
#include <cmath>

using namespace LookMath;

TEST_CASE("LookMath: the filmic curve matches 1 - exp(-x) in the mid tones", "[lookmath]")
{
	for (double x = 0.3; x <= 2.0001; x += 0.05) CHECK(std::fabs(Filmic(x) - (1.0 - std::exp(-x))) < 0.04);
	CHECK(Filmic(0.0) == 0.0);
	CHECK(Filmic(100.0) <= 1.0);
}

TEST_CASE("LookMath: flare ghosts lie on the line through the sun and the centre", "[lookmath]")
{
	const V2 suns[] = { { 0.3, -0.2 }, { -0.7, 0.4 }, { 0.05, 0.45 }, { -0.2, -0.1 } };
	for (const V2 &s : suns) {
		for (int k = 0; k < GHOSTS; k++) {
			V2 g = GhostPos(s, GhostT[k]);
			CHECK(std::fabs(g.x * s.y - g.y * s.x) < 1e-12); // collinear with the centre and the sun
			double along = (g.x * s.x + g.y * s.y) / (s.x * s.x + s.y * s.y);
			CHECK(std::fabs(along + GhostT[k]) < 1e-12);
		}
		V2 sun = GhostPos(s, -1.0), mir = GhostPos(s, 1.0);
		CHECK(std::fabs(sun.x - s.x) < 1e-12);
		CHECK(std::fabs(sun.y - s.y) < 1e-12);
		CHECK(std::fabs(mir.x + s.x) < 1e-12);
		CHECK(std::fabs(mir.y + s.y) < 1e-12);
	}
	for (int k = 1; k < GHOSTS; k++) CHECK(GhostT[k] > GhostT[k - 1]);
	CHECK(GhostT[0] > -1.0); // none on the sun itself
}

TEST_CASE("LookMath: the halo is strongest with the sun 0.3 to 0.6 from the centre", "[lookmath]")
{
	for (double d = 0.3; d <= 0.6; d += 0.05) CHECK(HaloWeight(d) == 1.0);
	CHECK(HaloWeight(0.0) == 0.0);
	CHECK(HaloWeight(0.95) == 0.0);
	CHECK(HaloWeight(0.2) > 0.0);
	CHECK(HaloWeight(0.2) < 1.0);
	CHECK(EdgeFade(0.0, 0.0) == 1.0);
	CHECK(EdgeFade(0.8, -0.5) == 1.0);
	CHECK(EdgeFade(0.2, 1.1) == 0.0);
	CHECK(EdgeFade(-0.95, 0.0) > 0.0);
	CHECK(EdgeFade(-0.95, 0.0) < 1.0);
}

// D3D left-handed, row vectors: view identity, 90 degree aperture, square pixels
static void Proj (double W, double H, double vp[16])
{
	double n = 0.5, f = 1e5, q = f / (f - n);
	for (int i = 0; i < 16; i++) vp[i] = 0.0;
	vp[0] = H / W; vp[5] = 1.0; vp[10] = q; vp[11] = 1.0; vp[14] = -n * q;
}

TEST_CASE("LookMath: haze segments project to pixels and are clipped at the camera", "[lookmath]")
{
	const double W = 1600, H = 900;
	double vp[16];
	Proj(W, H, vp);
	Seg s;
	const double a[3] = { 0.0, 0.0, 10.0 }, b[3] = { 2.0, 1.0, 10.0 };
	REQUIRE(ProjectSegment(vp, a, b, W, H, 0.1, s));
	CHECK(std::fabs(s.x0 - W / 2) < 1e-9);
	CHECK(std::fabs(s.y0 - H / 2) < 1e-9);
	CHECK(std::fabs(s.x1 - (2.0 * H / W / 10.0 + 1.0) * 0.5 * W) < 1e-9);
	CHECK(std::fabs(s.y1 - (1.0 - 0.1) * 0.5 * H) < 1e-9); // up is up the screen, y down
	CHECK(s.t0 == 0.0);
	CHECK(s.t1 == 1.0);
	CHECK(std::fabs(s.w0 - 10.0) < 1e-9);

	const double c[3] = { 1.0, 0.0, -5.0 }, d[3] = { 1.0, 0.0, 15.0 }; // starts behind the camera
	REQUIRE(ProjectSegment(vp, c, d, W, H, 0.1, s));
	CHECK(std::fabs(s.t0 - 5.1 / 20.0) < 1e-12);
	CHECK(s.t1 == 1.0);
	CHECK(std::fabs(s.w0 - 0.1) < 1e-12);
	CHECK(std::fabs(s.w1 - 15.0) < 1e-12);
	CHECK(std::isfinite(s.x0));
	REQUIRE(ProjectSegment(vp, d, c, W, H, 0.1, s)); // the other way round
	CHECK(s.t0 == 0.0);
	CHECK(std::fabs(s.t1 - 14.9 / 20.0) < 1e-12);
	CHECK(std::fabs(s.w1 - 0.1) < 1e-12);

	const double e[3] = { 0.0, 1.0, -1.0 }, f[3] = { 0.0, 2.0, -3.0 }; // wholly behind
	CHECK_FALSE(ProjectSegment(vp, e, f, W, H, 0.1, s));
}

TEST_CASE("LookMath: pixel to segment distance and the haze strength", "[lookmath]")
{
	double t;
	CHECK(std::fabs(SegDist(5, 3, 0, 0, 10, 0, &t) - 3.0) < 1e-12);
	CHECK(std::fabs(t - 0.5) < 1e-12);
	CHECK(std::fabs(SegDist(-3, 4, 0, 0, 10, 0, &t) - 5.0) < 1e-12);
	CHECK(t == 0.0);
	CHECK(std::fabs(SegDist(13, 4, 0, 0, 10, 0, &t) - 5.0) < 1e-12);
	CHECK(t == 1.0);
	CHECK(std::fabs(SegDist(2, 2, 1, 1, 1, 1, &t) - std::sqrt(2.0)) < 1e-12); // a point

	CHECK(HazeStrength(0.8, 0.0, 10.0, 0.0) == 0.8);
	CHECK(HazeStrength(0.8, 10.0, 10.0, 0.0) == 0.0);
	CHECK(HazeStrength(0.8, 12.0, 10.0, 0.0) == 0.0);
	CHECK(HazeStrength(0.8, 0.0, 10.0, 1.0) == 0.0);
	double prev = 1.0;
	for (double d = 0.0; d < 10.0; d += 1.0) {
		double s = HazeStrength(1.0, d, 10.0, 0.3);
		CHECK(s <= prev);
		CHECK(s >= 0.0);
		prev = s;
	}
}

TEST_CASE("LookMath: sharpening weights stay in bounds", "[lookmath]")
{
	for (double a : { 0.12, 0.18 }) {
		for (double mn = 0.0; mn <= 1.0; mn += 0.05)
			for (double mx = mn; mx <= 1.0; mx += 0.05) {
				double w = SharpenWeight(mn, mx, a);
				CHECK(w <= 0.0);
				CHECK(w >= -a);
				CHECK(1.0 + 4.0 * w > 0.25);
			}
		CHECK(SharpenWeight(0.0, 0.5, a) == 0.0); // a black neighbour: no ringing below it
		CHECK(SharpenWeight(0.3, 1.0, a) == 0.0); // a white one: none above it
		const double flat[4] = { 0.4, 0.4, 0.4, 0.4 };
		CHECK(std::fabs(Sharpen(0.4, flat, a) - 0.4) < 1e-12);
		const double dim[4] = { 0.3, 0.3, 0.35, 0.3 };
		CHECK(Sharpen(0.5, dim, a) > 0.5); // a peak gets brighter
		const double lit[4] = { 0.6, 0.6, 0.55, 0.6 };
		CHECK(Sharpen(0.4, lit, a) < 0.4); // a pit darker
		const double hi[4] = { 0.3, 0.3, 0.3, 0.3 };
		CHECK(Sharpen(0.1, hi, a) >= 0.0);
	}
}

TEST_CASE("LookMath: the vignette darkens the corners only", "[lookmath]")
{
	const double asp = 16.0 / 9.0;
	CHECK(Vignette(0.5, 0.5, asp) == 1.0);
	CHECK(std::fabs(Vignette(0.0, 0.0, asp) - 0.82) < 0.01);
	CHECK(std::fabs(Vignette(1.0, 1.0, asp) - Vignette(0.0, 0.0, asp)) < 1e-12);
	CHECK(std::fabs(Vignette(0.2, 0.7, asp) - Vignette(0.8, 0.3, asp)) < 1e-12);
	CHECK(Vignette(0.65, 0.5, asp) == 1.0);
	CHECK(Vignette(0.5, 0.0, asp) > Vignette(0.0, 0.5, asp)); // the near edge less than the far one
}
