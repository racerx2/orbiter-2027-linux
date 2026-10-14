// not upstream: screen-space reflection tracing maths (OVP/VulkanClient/SsrMath.cpp, design-ssr 16)

#include <catch2/catch_test_macros.hpp>
#include "SsrMath.h"
#include <algorithm>
#include <cmath>
#include <random>

using namespace PostMath;
using namespace SsrMath;

namespace {

Vec3 Cross (const Vec3 &a, const Vec3 &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Mat3 Columns (const Vec3 &x, const Vec3 &y, const Vec3 &z) { return { { { x.x, y.x, z.x }, { x.y, y.y, z.y }, { x.z, y.z, z.z } } }; }
Vec3 Col (const Mat3 &m, int j) { return { m.m[0][j], m.m[1][j], m.m[2][j] }; }

Mat3 LookAt (const Vec3 &from, const Vec3 &to, const Vec3 &up) // camera axes X right, Y up, Z forward (D3D LH)
{
	Vec3 z = Unit(Sub(to, from)), x = Unit(Cross(up, z)), y = Cross(z, x);
	return Columns(x, y, z);
}

// a pinhole camera 2 m over a ground plane, a box, a thin pole; camera-relative world coordinates
struct Scene {
	int W = 256, H = 144;
	double tanAp = std::tan(0.5), aspect = 144.0 / 256.0;
	Mat3 rot;
	Mat4 VP;
	struct Box { Vec3 a, b; } box[3] = { { { -1.0, -2.0, 8.0 }, { 1.0, 0.0, 10.0 } }, { { 2.0, -2.0, 6.0 }, { 2.1, 3.0, 6.1 } }, { { -6.0, -2.0, 30.0 }, { 12.0, 30.0, 31.0 } } };
	double pix = 2.0 * std::tan(0.5) / 144.0; // pixel angle
	std::vector<float> s;
	HiZ z;

	Scene ()
	{
		rot = LookAt({ 0, 0, 0 }, { 0.0, -std::sin(0.08), std::cos(0.08) }, { 0, 1, 0 });
		VP = Mul(View(rot), Proj(tanAp, aspect, 0.1, 1e5, 0, 0));
		s.assign(size_t(W) * H, 0.0f);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				Vec3 d = Ray(x + 0.5, y + 0.5);
				double t;
				Vec3 n;
				if (Cast(d, &t, &n)) s[size_t(y) * W + x] = float(1.0 / Dot(Scale(d, t), Col(rot, 2)));
			}
		BuildHiZ(s, W, H, z);
	}
	Vec3 Ray (double px, double py) const
	{
		double nx = px / W * 2.0 - 1.0, ny = 1.0 - py / H * 2.0;
		return Unit(Add(Add(Scale(Col(rot, 0), nx * tanAp / aspect), Scale(Col(rot, 1), ny * tanAp)), Col(rot, 2)));
	}
	bool Cast (const Vec3 &d, double *tOut, Vec3 *nOut) const // nearest of the ground (y = -2) and the boxes, from the camera
	{
		double best = 1e30;
		Vec3 bn = { 0, 0, 0 };
		if (d.y < -1e-9) { best = -2.0 / d.y; bn = { 0, 1, 0 }; }
		for (const Box &b : box) {
			double t0 = 0.0, t1 = 1e30;
			int ax = -1;
			const double o[3] = { 0, 0, 0 }, dd[3] = { d.x, d.y, d.z }, lo[3] = { b.a.x, b.a.y, b.a.z }, hi[3] = { b.b.x, b.b.y, b.b.z };
			bool ok = true;
			for (int i = 0; i < 3 && ok; i++) {
				if (std::fabs(dd[i]) < 1e-12) { ok = o[i] >= lo[i] && o[i] <= hi[i]; continue; }
				double ta = (lo[i] - o[i]) / dd[i], tb = (hi[i] - o[i]) / dd[i];
				if (ta > tb) std::swap(ta, tb);
				if (ta > t0) { t0 = ta; ax = i; }
				t1 = std::min(t1, tb);
				ok = t0 <= t1;
			}
			if (ok && ax >= 0 && t0 < best) {
				best = t0;
				bn = { 0, 0, 0 };
				double sg = dd[ax] > 0.0 ? -1.0 : 1.0;
				if (ax == 0) bn.x = sg; else if (ax == 1) bn.y = sg; else bn.z = sg;
			}
		}
		if (best > 1e29) return false;
		*tOut = best;
		*nOut = bn;
		return true;
	}
};

const Scene &Sc () { static Scene s; return s; }

}

TEST_CASE("SsrMath: curve pairs round trip and are monotonic", "[ssrmath]")
{
	for (double e : { 0.5, 1.0, 2.0, 4.0 }) {
		double last = -1.0;
		for (double y = 0.0; y <= 0.985 + 1e-9; y += 0.005) {
			CHECK(std::fabs(Hdr(HdrInv(y, e), e) - y) < 1e-4);
			CHECK(std::fabs(Filmic(FilmicInv(y, e), e) - y) < 1e-4);
			double x = FilmicInv(y, e);
			CHECK(x >= last);
			last = x;
		}
	}
	for (double y = 0.0; y <= 0.99; y += 0.01) CHECK(std::fabs(MeshCurve(MeshCurveInv(y)) - y) < 1e-4);
	for (double c = 0.0; c <= 1.5; c += 0.01) CHECK(std::fabs(LinearToSrgb(SrgbToLinear(c)) - c) < 1e-4);
	for (double y : { 0.985, 1.0, 2.0 }) { // capture values over 1 (PostProcess 1, mip averages) stay finite
		CHECK(std::isfinite(HdrInv(y, 1.0)));
		CHECK(std::isfinite(FilmicInv(y, 1.0)));
		CHECK(FilmicInv(y, 1.0) == FilmicInv(0.985, 1.0));
	}
}

TEST_CASE("SsrMath: a mirror shows the displayed hit colour", "[ssrmath]")
{
	for (bool curve : { false, true })
		for (bool srgb : { false, true })
			for (double T : { 0.3, 0.6, 1.0 })
				for (double I : { 0.0, 0.1, 0.2 })
					for (double e : { 0.25, 1.0, 2.0 })
						for (double y = I; y <= I + 0.98 * T; y += 0.02) {
							double L = MeshDecode(y, T, I, e, curve, srgb);
							CHECK(std::fabs(MeshEncode(L, T, I, e, curve, srgb) - y) < 1e-3);
						}
	for (double y = 0.0; y <= 0.985; y += 0.01) CHECK(std::fabs(Hdr(HdrInv(y, 1.7), 1.7) - y) < 1e-6);
}

TEST_CASE("SsrMath: the Hi-Z levels are the max over each footprint", "[ssrmath]")
{
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(0.0f, 1.0f);
	for (auto wh : { std::pair<int, int>{ 37, 23 }, { 1, 5 }, { 300, 17 } }) {
		int W = wh.first, H = wh.second;
		std::vector<float> s(size_t(W) * H);
		for (float &v : s) v = u(rng) < 0.2f ? 0.0f : u(rng);
		HiZ z;
		BuildHiZ(s, W, H, z);
		REQUIRE(z.L == HiZLevels(W, H));
		CHECK(z.Wp % (1 << (z.L - 1)) == 0);
		CHECK(z.Hp % (1 << (z.L - 1)) == 0);
		for (int k = 0; k < z.L; k++) {
			int w = z.Wp >> k, h = z.Hp >> k, a = 1 << k;
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					float m = 0.0f;
					for (int j = y * a; j < (y + 1) * a; j++)
						for (int i = x * a; i < (x + 1) * a; i++) if (i < W && j < H) m = std::max(m, s[size_t(j) * W + i]);
					CHECK(z.At(k, x, y) == m);
				}
		}
		CHECK(z.At(z.L - 1, -5, 1000) == z.At(z.L - 1, 0, (z.Hp >> (z.L - 1)) - 1)); // clamped cells
	}
	CHECK(HiZLevels(1920, 1080) == 9);
}

TEST_CASE("SsrMath: the Hi-Z walk finds what a dense march finds", "[ssrmath]")
{
	const Scene &sc = Sc();
	std::mt19937 rng(11);
	std::uniform_real_distribution<double> u(0.0, 1.0);
	WalkPrm p;
	p.maxIt = 400;
	int n = 0, agree = 0, toward = 0, towardAgree = 0, hits = 0, within40 = 0, self = 0;
	for (int i = 0; i < 4000 && n < 1500; i++) {
		double px = 1.0 + u(rng) * (sc.W - 2), py = 1.0 + u(rng) * (sc.H - 2);
		Vec3 d = sc.Ray(px, py), N;
		double t;
		if (!sc.Cast(d, &t, &N)) continue;
		Vec3 P = Scale(d, t), V = Scale(d, -1.0);
		double sel = u(rng);
		Vec3 R = Reflect(d, N);
		if (sel > 0.4 && sel < 0.7) R = Reflect(d, Vndf(V, N, 0.4, u(rng), u(rng)));
		else if (sel >= 0.7) R = Unit(Add(V, Scale(Unit(Vec3{ u(rng) - 0.5, u(rng) - 0.5, u(rng) - 0.5 }), 0.8))); // mostly toward the camera
		if (Dot(R, N) <= 0.0) continue;
		R = KeepAbove(R, N, 0.01);
		Seg g;
		if (!Segment(sc.VP, Lift(P, N, 1.5 * sc.pix), R, sc.W, sc.H, 0.1, 1e5, g)) continue;
		n++;
		WalkPrm q = p;
		WalkOut w = Walk(sc.z, g, q), m = March(sc.z, g, q, 0.05);
		REQUIRE(w.it <= q.maxIt);
		q.maxIt = 40;
		WalkOut w40 = Walk(sc.z, g, q);
		bool ok = (w.hit == m.hit) && (!w.hit || (std::fabs(w.X - m.X) <= 1.0 && std::fabs(w.Y - m.Y) <= 1.0)) && (w.hit || w.end == m.end);
		agree += ok;
		hits += w.hit;
		self += w.hit && std::fabs(w.X - g.X0) < 3.0 && std::fabs(w.Y - g.Y0) < 3.0 && std::fabs(1.0 / (g.s0 + w.t * (g.s1 - g.s0)) - 1.0 / g.s0) < 0.05 / g.s0; // back on its own surface
		within40 += (w40.hit || w40.end || (!w40.hit && w40.it < 40)) && w40.hit == w.hit;
		if (g.s1 > g.s0) { toward++; towardAgree += ok; }
	}
	INFO("rays " << n << " agree " << agree << " hits " << hits << " self " << self << " toward " << toward << "/" << towardAgree << " same within 40 " << within40);
	REQUIRE(n >= 1000);
	CHECK(self <= n / 100);
	CHECK(agree >= int(0.995 * n));
	CHECK(toward >= 100);
	CHECK(towardAgree >= int(0.995 * toward));
	CHECK(hits >= n / 8);
	CHECK(within40 >= int(0.9 * n));
}

TEST_CASE("SsrMath: rays behind a thin pole pass it; sky rays end at their vanishing pixel", "[ssrmath]")
{
	const Scene &sc = Sc();
	WalkPrm p;
	p.maxIt = 400;
	// straight up from 1.5 m behind the pole, on its line of sight: the ray stays behind it on screen, deeper than the thickness
	Seg g;
	REQUIRE(Segment(sc.VP, Scale(Vec3{ 2.05, -1.5, 6.05 }, 1.25), { 0.0, 1.0, 0.0 }, sc.W, sc.H, 0.1, 1e5, g));
	WalkOut w = Walk(sc.z, g, p), m = March(sc.z, g, p, 0.05);
	CHECK(!w.hit);
	CHECK(!m.hit);
	// the same 0.1 m behind the pole's back face: inside the thickness, a hit
	Seg g2;
	REQUIRE(Segment(sc.VP, Scale(Vec3{ 2.03, -1.5, 6.05 }, 6.2 / 6.05), { 0.0, 1.0, 0.0 }, sc.W, sc.H, 0.1, 1e5, g2));
	WalkOut w2 = Walk(sc.z, g2, p);
	CHECK(w2.hit);
	// up into the sky from the ground left of the box
	Vec3 R = Unit({ -0.5, 0.15, 1.0 });
	Seg g3;
	REQUIRE(Segment(sc.VP, Lift(Vec3{ -2.0, -2.0, 5.0 }, Vec3{ 0.0, 1.0, 0.0 }, 1.5 * sc.pix), R, sc.W, sc.H, 0.1, 1e5, g3));
	WalkOut w3 = Walk(sc.z, g3, p);
	CHECK(!w3.hit);
	CHECK(w3.end);
	double vx, vy;
	REQUIRE(Vanish(sc.VP, R, sc.W, sc.H, &vx, &vy));
	CHECK(std::fabs(vx - g3.X1) < 0.05);
	CHECK(std::fabs(vy - g3.Y1) < 0.05);
	CHECK(SkyAt(sc.z, vx, vy, p));
}

TEST_CASE("SsrMath: near clip", "[ssrmath]")
{
	const Scene &sc = Sc();
	Seg g;
	Vec3 P = Scale(sc.Ray(128.0, 60.0), 5.0), R = Scale(Col(sc.rot, 2), -1.0);
	REQUIRE(Segment(sc.VP, P, R, sc.W, sc.H, 0.1, 1e5, g));
	CHECK(g.clipped);
	CHECK(std::fabs(1.0 / g.s1 - 0.1) < 1e-6);
	CHECK(g.s1 > g.s0);
	CHECK(!Segment(sc.VP, Scale(Col(sc.rot, 2), -3.0), R, sc.W, sc.H, 0.1, 1e5, g)); // the start behind the camera
	REQUIRE(Segment(sc.VP, P, Col(sc.rot, 2), sc.W, sc.H, 0.1, 1e5, g));
	CHECK(!g.clipped);
}

TEST_CASE("SsrMath: body matrices map a static ground point and a point on a rolling vessel to their last pixel", "[ssrmath]")
{
	const double W = 1920.0, H = 1080.0, TANAP = std::tan(0.35), ASPECT = H / W, DT = 1.0 / 60.0;
	const double Rp = 6.371e6, om = 7.292e-5;
	Vec3 oNow = { 1.4e11, 1.2e7, 5.1e10 }, vel = { 2.98e4, 0.0, -1.1e4 }, oPrev = Sub(oNow, Scale(vel, DT));
	Mat3 Rnow = Mul(RotX(0.41), RotY(1.234)), Rprev = Mul(RotX(0.41), RotY(1.234 - om * DT));
	Vec3 up = Unit({ 0.6, 0.7, 0.2 }), east = Unit(Cross(Vec3{ 0, 1, 0 }, up)), north = Cross(up, east);
	Vec3 l = Scale(up, Rp);
	Vec3 lcNow = Add(l, Add(Scale(up, 30.0), Scale(east, -120.0))), lcPrev = Sub(lcNow, Scale(east, 230.0 * DT));
	Vec3 camNow = Add(Mul(Rnow, lcNow), oNow), camPrev = Add(Mul(Rprev, lcPrev), oPrev);
	Mat3 cNow = LookAt(camNow, Add(Mul(Rnow, l), oNow), Mul(Rnow, up)), cPrev = Mul(LookAt(camPrev, Add(Mul(Rprev, l), oPrev), Mul(Rprev, up)), RotY(0.003));
	Mat4 VPprev = Mul(View(cPrev), Proj(TANAP, ASPECT, 0.1, 1e8, 0, 0));
	// a ground point
	for (int i = -2; i <= 2; i++) {
		Vec3 lp = Add(l, Add(Scale(east, 20.0 * i), Scale(north, 15.0 * i)));
		Vec3 pNow = Sub(Add(Mul(Rnow, lp), oNow), camNow), pPrev = Sub(Add(Mul(Rprev, lp), oPrev), camPrev);
		double rx, ry, qx, qy;
		REQUIRE(ToPixel(VPprev, pPrev, 1.0, W, H, &rx, &ry));
		REQUIRE(ToPixel(ToFloat(BodyMatrix(Rnow, oNow, Rprev, oPrev, camNow, camPrev, VPprev)), pNow, 1.0, W, H, &qx, &qy));
		CHECK(std::fabs(qx - rx) < 0.01);
		CHECK(std::fabs(qy - ry) < 0.01);
	}
	// a vessel at 7.7 km/s, rolling, near the camera
	Vec3 vNow = Add(camNow, Mul(cNow, Vec3{ 3.0, -2.0, 25.0 })), vPrev = Sub(vNow, Scale(Unit({ 0.3, 0.1, 1.0 }), 7700.0 * DT));
	Mat3 VRnow = Mul(RotX(0.2), RotY(0.5)), VRprev = Mul(RotX(0.2 - 0.3 * DT), RotY(0.5));
	for (int i = -2; i <= 2; i++) {
		Vec3 lp = { 2.0 * i, 0.5 * i, -3.0 * i };
		Vec3 pNow = Sub(Add(Mul(VRnow, lp), vNow), camNow), pPrev = Sub(Add(Mul(VRprev, lp), vPrev), camPrev);
		double rx, ry, qx, qy;
		if (!ToPixel(VPprev, pPrev, 1.0, W, H, &rx, &ry)) continue;
		REQUIRE(ToPixel(ToFloat(BodyMatrix(VRnow, vNow, VRprev, vPrev, camNow, camPrev, VPprev)), pNow, 1.0, W, H, &qx, &qy));
		CHECK(std::fabs(qx - rx) < 0.01);
		CHECK(std::fabs(qy - ry) < 0.01);
	}
}

TEST_CASE("SsrMath: VNDF samples", "[ssrmath]")
{
	std::mt19937 rng(3);
	std::uniform_real_distribution<double> u(0.0, 1.0);
	Vec3 N = Unit({ 0.2, 0.9, -0.3 });
	for (double a : { 0.1, 0.3, 0.6 }) {
		const int n = 100000;
		std::vector<double> t2(n);
		for (int i = 0; i < n; i++) {
			Vec3 H = Vndf(N, N, a, u(rng), u(rng));
			double c = Dot(H, N);
			REQUIRE(c > 0.0);
			t2[i] = (1.0 - c * c) / (c * c);
		}
		std::sort(t2.begin(), t2.end());
		double dmax = 0.0;
		for (int i = 0; i < n; i += 97) { // V = N: the CDF of tan^2 is tan^2 / (a^2 + tan^2)
			double F = t2[i] / (a * a + t2[i]);
			dmax = std::max(dmax, std::fabs(F - (i + 0.5) / n));
		}
		CHECK(dmax < 0.01);
	}
	Vec3 V = Unit({ 0.5, 0.4, 0.1 });
	for (int i = 0; i < 1000; i++) {
		Vec3 H = Vndf(V, N, 0.35, u(rng), u(rng));
		CHECK(Dot(H, N) > 0.0);
	}
	Vec3 H0 = Vndf(V, N, 1e-6, 0.37, 0.81);
	Vec3 R0 = Reflect(Scale(V, -1.0), H0), Rm = Reflect(Scale(V, -1.0), N);
	CHECK(std::fabs(R0.x - Rm.x) < 1e-3);
	CHECK(std::fabs(R0.y - Rm.y) < 1e-3);
	CHECK(std::fabs(R0.z - Rm.z) < 1e-3);
	for (int i = 0; i < 64; i++) { double g = Ign(i * 7.0, i * 3.0); CHECK(g >= 0.0); CHECK(g < 1.0); }
}

TEST_CASE("SsrMath: cone level, edge fade, base surface weight, wet model, jumps", "[ssrmath]")
{
	CHECK(ConeLod(300.0, 0.0, 1.0, 10.0) == 0.0);
	double last = 0.0;
	for (double a = 0.0; a <= 1.0; a += 0.05) { double l = ConeLod(300.0, a, 1.0, 10.0); CHECK(l >= last); last = l; }
	CHECK(ConeLod(1e5, 1.0, 1.0, 10.0) == 10.0);
	CHECK(EdgeFade(0.0, 0.5, 1.78, 0.06) == 0.0);
	CHECK(EdgeFade(0.5, 1.0, 1.78, 0.06) == 0.0);
	CHECK(EdgeFade(0.5, 0.5, 1.78, 0.06) == 1.0);
	CHECK(EdgeFade(0.04, 0.07, 1.78, 0.06) == 1.0);
	// dry concrete (alpha 0.3): the review's numbers; below the 0.03 cut at normal incidence
	double perc = std::sqrt(0.3);
	CHECK(std::fabs(EnvBRDFApprox(0.04, perc, 0.05) - 0.12) < 0.01);
	CHECK(EnvBRDFApprox(0.04, perc, 1.0) < 0.03);
	double fade = 1.0 - Smoothstep(0.20, 0.36, 0.30); // SsrBase's early-out holds the roughness fade: dry ground traces at grazing views only
	CHECK(EnvBRDFApprox(0.04, perc, 0.05) * fade >= 0.03);
	CHECK(EnvBRDFApprox(0.04, perc, 0.5) * fade < 0.03);
	last = 1.0;
	for (double c = 0.05; c <= 1.0; c += 0.05) { double f = EnvBRDFApprox(0.04, perc, c); CHECK(f <= last + 1e-9); last = f; }
	CHECK(EnvBRDFApprox(0.04, 0.0, 0.0) > 0.9); // a mirror at grazing reflects nearly all
	Wet d = WetModel(0.0), w = WetModel(1.0);
	CHECK(d.alpha == 0.30); CHECK(d.f0 == 0.04); CHECK(d.albedo == 1.0);
	CHECK(std::fabs(w.alpha - 0.02) < 1e-12); CHECK(std::fabs(w.f0 - 0.02) < 1e-12); CHECK(std::fabs(w.albedo - 0.55) < 1e-12);
	for (double x = 0.0; x < 0.85; x += 0.1) { CHECK(WetModel(x + 0.1).alpha < WetModel(x).alpha); CHECK(WetModel(x + 0.1).albedo < WetModel(x).albedo); }
	CHECK(!JumpTest(250.0 / 60.0, 250.0 / 60.0, 0.01));
	CHECK(!JumpTest(7700.0 / 60.0, 7700.0 / 60.0, 0.002));
	CHECK(JumpTest(5000.0, 130.0, 0.0));
	CHECK(JumpTest(1.0, 1.0, 1.0));
}

TEST_CASE("SsrMath: grazing water rays are finished within the budget", "[ssrmath]") // r3-ssr2: a sea seen from 1.4 m, a vessel at 25 m, a pole at 50 m
{
	const int W = 960, H = 540;
	const double tanAp = std::tan(0.5236), aspect = double(H) / W, h = 1.4, pix = 2.0 * tanAp / H;
	Mat3 rot = LookAt({ 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 });
	Mat4 VP = Mul(View(rot), Proj(tanAp, aspect, 0.1, 1e6, 0, 0));
	struct B { Vec3 a, b; } box[2] = { { { -9.0, -0.2, 24.0 }, { 9.0, 2.5, 26.0 } }, { { -12.0, -h, 50.0 }, { -11.7, 6.6, 50.3 } } };
	auto ray = [&](double px, double py) {
		double nx = px / W * 2.0 - 1.0, ny = 1.0 - py / H * 2.0;
		return Unit(Add(Add(Scale(Col(rot, 0), nx * tanAp / aspect), Scale(Col(rot, 1), ny * tanAp)), Col(rot, 2)));
	};
	auto cast = [&](const Vec3 &d, double *tOut, bool *water) {
		double best = 1e30;
		bool wt = false;
		if (d.y < -1e-9) { best = -h / d.y; wt = true; }
		for (const B &b : box) {
			double t0 = 0.0, t1 = 1e30;
			bool ok = true;
			const double dd[3] = { d.x, d.y, d.z }, lo[3] = { b.a.x, b.a.y, b.a.z }, hi[3] = { b.b.x, b.b.y, b.b.z };
			for (int i = 0; i < 3 && ok; i++) {
				if (std::fabs(dd[i]) < 1e-12) { ok = lo[i] <= 0.0 && hi[i] >= 0.0; continue; }
				double ta = lo[i] / dd[i], tb = hi[i] / dd[i];
				if (ta > tb) std::swap(ta, tb);
				t0 = std::max(t0, ta);
				t1 = std::min(t1, tb);
				ok = t0 <= t1;
			}
			if (ok && t0 < best) { best = t0; wt = false; }
		}
		if (best > 1e29) return false;
		*tOut = best;
		*water = wt;
		return true;
	};
	std::vector<float> s(size_t(W) * H, 0.0f);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			Vec3 d = ray(x + 0.5, y + 0.5);
			double t;
			bool wt;
			if (cast(d, &t, &wt)) s[size_t(y) * W + x] = float(1.0 / (t * Dot(d, Col(rot, 2))));
		}
	HiZ z;
	BuildHiZ(s, W, H, z);
	std::mt19937 rng(5);
	std::uniform_real_distribution<double> u(-1.0, 1.0);
	WalkPrm p, q;
	p.maxIt = q.maxIt = 48;
	q.finish = 0;
	int n = 0, cut = 0, finished = 0, agreeP = 0, agreeQ = 0, hits = 0;
	for (int y = 0; y < H; y += 2)
		for (int x = 0; x < W; x += 2) {
			Vec3 d = ray(x + 0.5, y + 0.5);
			double t;
			bool wt;
			if (!cast(d, &t, &wt) || !wt) continue;
			Vec3 N = Unit({ u(rng) * 0.08, 1.0, u(rng) * 0.08 }); // wave facets up to 4.6 degrees
			Vec3 R = KeepAbove(Reflect(d, N), { 0, 1, 0 }, 0.01);
			Seg g;
			if (!Segment(VP, Lift(Scale(d, t), { 0, 1, 0 }, 1.5 * pix), R, W, H, 0.1, 1e6, g)) continue;
			n++;
			WalkOut a = Walk(z, g, p), b = Walk(z, g, q), m = March(z, g, p, 0.25);
			REQUIRE(a.it <= p.maxIt);
			cut += !b.hit && !b.end && b.it >= q.maxIt;
			finished += a.finished;
			hits += a.hit;
			auto same = [&](const WalkOut &w) { return w.hit == m.hit && (!w.hit || (std::fabs(w.X - m.X) <= 2.0 && std::fabs(w.Y - m.Y) <= 2.0)) && (w.hit || w.end == m.end); };
			agreeP += same(a);
			agreeQ += same(b);
		}
	INFO("rays " << n << " cut by the budget " << cut << " finished " << finished << " hits " << hits << " agree " << agreeP << " (no finish " << agreeQ << ")");
	REQUIRE(n >= 20000);
	CHECK(cut >= n / 50);             // the budget does cut grazing rays here
	CHECK(finished <= cut);           // the rest of the cut rays had reached the screen edge
	CHECK(finished >= cut - cut / 50);
	CHECK(hits >= n / 50);            // the vessel and the pole are seen in the water
	CHECK(agreeP >= int(0.995 * n));  // with the finish the walk ends as the dense march does
	CHECK(agreeP > agreeQ);
}
