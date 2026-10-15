// not upstream: plume, smoke and plasma maths (OVP/VulkanClient/PlumeMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PlumeMath.h"
#include <algorithm>
#include <cmath>
#include <random>

using namespace PlumeMath;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

static const double DEG = 3.14159265358979323846 / 180.0;
static const double G0 = 9.80665;

TEST_CASE("PlumeMath: Prandtl-Meyer", "[plumemath]")
{
	CHECK(PrandtlMeyer(1.0, 1.4) == 0.0);
	CHECK_THAT(PrandtlMeyer(2.0, 1.4) / DEG, WithinAbs(26.38, 0.01));
	CHECK_THAT(PrandtlMeyerMax(1.2), WithinAbs(3.639, 0.001));
	CHECK_THAT(PrandtlMeyer(1e6, 1.2), WithinAbs(PrandtlMeyerMax(1.2), 1e-3));
}

TEST_CASE("PlumeMath: isentropic relations", "[plumemath]")
{
	CHECK_THAT(AreaRatio(1.0, 1.4), WithinAbs(1.0, 1e-12));
	CHECK_THAT(AreaRatio(2.0, 1.4), WithinAbs(1.6875, 1e-4));
	CHECK_THAT(PressureRatio(2.0, 1.4), WithinAbs(7.824, 1e-3));
	CHECK_THAT(MachFromPressureRatio(PressureRatio(2.0, 1.4), 1.4), WithinAbs(2.0, 1e-9));
	CHECK_THAT(MachFromPressureRatio(PressureRatio(3.7, 1.2), 1.2), WithinAbs(3.7, 1e-9));
}

TEST_CASE("PlumeMath: exit area and pressure", "[plumemath]")
{
	double ssme = ExitArea(2170732.15, 453 * G0, 363 * G0, 101.4e3); // Atlantis.h
	CHECK_THAT(2.0 * std::sqrt(ssme / 3.14159265358979), WithinRel(2.30, 0.02));
	double srb = ExitArea(1.41e7, 2638.89, 2368.79, 101.4e3);
	CHECK_THAT(2.0 * std::sqrt(srb / 3.14159265358979), WithinRel(3.80, 0.15));
	CHECK(ExitArea(2e5, 4e4, 4e4, 101.4e3) == 0.0);
	double p0 = 0.0;
	for (double phi = 0.021; phi < 0.9; phi += 0.01) {
		double p = ExitPressure(phi, 1e6);
		CHECK(p >= 3e3);
		CHECK(p <= 120e3);
		if (p0 > 0.0) CHECK(p <= p0);
		p0 = p;
	}
	CHECK_THAT(ExitPressure(PressureFraction(453 * G0, 363 * G0), 2.17e6), WithinRel(25.5e3, 0.02));
	CHECK(ExitPressure(0.0, 1e6) == 40e3);
	CHECK(ExitPressure(0.0, 1e5) == 8e3);
}

TEST_CASE("PlumeMath: SSME jet at sea level and in vacuum", "[plumemath]")
{
	Jet j = JetState(ExitPressure(PressureFraction(453 * G0, 363 * G0), 2.17e6), PSL);
	CHECK_THAT(j.Me, WithinAbs(3.94, 0.02));
	CHECK_THAT(j.Mj, WithinAbs(3.20, 0.02));
	CHECK_THAT(j.rj, WithinAbs(0.59, 0.02));
	CHECK_THAT(j.Ls / 2.0, WithinAbs(2.3, 0.1)); // L_s / D_e
	CHECK(j.xmd > 0.0);                          // over-expanded: a Mach disk
	CHECK_THAT(j.thInf / DEG, WithinAbs(121.5, 0.5));
	CHECK_THAT(j.watm, WithinAbs(1.0, 1e-12));
	Jet v = JetState(j.pe, 0.0);
	CHECK(std::isfinite(v.Mj));
	CHECK(std::isfinite(v.rj));
	CHECK(std::isfinite(v.Ls));
	CHECK(v.watm == 0.0);
	CHECK_FALSE(v.jet);
}

TEST_CASE("PlumeMath: plume radius", "[plumemath]")
{
	Jet over = JetState(25.5e3, PSL), under = JetState(120e3, 30e3);
	CHECK_THAT(PlumeRadius(over, 0.0), WithinAbs(1.0, 1e-12));
	CHECK_THAT(PlumeRadius(under, 0.0), WithinAbs(1.0, 1e-12));
	CHECK(over.rj < 1.0);
	CHECK(under.rj > 1.0);
	CHECK(PlumeRadius(over, over.Ls) < 1.0);   // necks
	CHECK(PlumeRadius(under, under.Ls) > 1.0); // bulges
	double prev = PlumeRadius(over, 0.0);
	for (double x = 0.01; x < 60.0; x += 0.01) { // continuous
		double r = PlumeRadius(over, x);
		CHECK(std::fabs(r - prev) < 0.01);
		prev = r;
	}
}

TEST_CASE("PlumeMath: Simons vacuum density", "[plumemath]")
{
	double th = JetState(25.5e3, 0.0).thInf;
	CHECK_THAT(Simons(0.0, 0.0, th), WithinAbs(1.0, 1e-12));
	CHECK_THAT(Simons(3.0, th, th), WithinAbs(0.0, 1e-12));
	CHECK_THAT(Simons(0.0, 30 * DEG, th), WithinAbs(0.46, 0.02));
	CHECK_THAT(Simons(0.0, 60 * DEG, th), WithinAbs(0.034, 0.005));
	for (double a = 0.0; a < th; a += 0.01) CHECK(Simons(1.0, a + 0.01, th) <= Simons(1.0, a, th));
	for (double d = 0.0; d < 50.0; d += 0.5) CHECK(Simons(d + 0.5, 0.2, th) < Simons(d, 0.2, th));
}

TEST_CASE("PlumeMath: propellant classes", "[plumemath]")
{
	CHECK(Classify(true, 2.17e6, 453 * G0) == HYDROLOX);  // SSME
	CHECK(Classify(true, 26.7e3, 316 * G0) == HYPERGOLIC); // OMS
	CHECK(Classify(true, 1.41e7, 2638.89) == SOLID);       // SRB
	CHECK(Classify(true, 6.77e6, 2982.0) == KEROLOX);      // F-1
	CHECK(Classify(true, 110e3, 4560.0) == HYDROLOX);      // RL10
	CHECK(Classify(true, 2e5, 4e4) == TEXTURE);            // DG
	CHECK(Classify(true, 2e5, 13943.0) == TEXTURE);        // XR
	CHECK(Classify(false, 2.17e6, 453 * G0) == TEXTURE);   // no thruster
	CHECK(Classify(true, 2.2e6, 3700.0) == METHALOX);
	for (int c = 0; c < NCLASS; c++) CHECK(ClassFromName(ClassName(c)) == c);
	CHECK(ClassFromName("steam") == -1);
}

TEST_CASE("PlumeMath: blackbody colours", "[plumemath]")
{
	double x, y;
	BlackbodyXY(6504.0, x, y);
	CHECK_THAT(x, WithinAbs(0.3135, 0.002));
	CHECK_THAT(y, WithinAbs(0.3236, 0.002));
	double c[3];
	Blackbody(2100.0, c);
	CHECK(c[0] > c[1]);
	CHECK(c[1] > c[2]);
	CHECK_THAT(std::max(c[0], std::max(c[1], c[2])), WithinAbs(1.0, 1e-12));
	Blackbody(12000.0, c);
	CHECK(c[2] > c[0]);
}

TEST_CASE("PlumeMath: plasma colour and intensity", "[plumemath]")
{
	double c[3];
	PlasmaColour(7.62, c);
	CHECK_THAT(c[0], WithinAbs(1.0, 1e-12)); CHECK_THAT(c[1], WithinAbs(0.28, 1e-12)); CHECK_THAT(c[2], WithinAbs(0.0, 1e-12));
	PlasmaColour(10.0, c);
	CHECK_THAT(c[1], WithinAbs(0.47, 1e-12)); CHECK_THAT(c[2], WithinAbs(0.55, 1e-12));
	PlasmaColour(12.0, c);
	CHECK_THAT(c[0], WithinAbs(0.44, 1e-12));
	PlasmaColour(2.0, c);
	CHECK_THAT(c[1], WithinAbs(0.19, 1e-12));
	PlasmaColour(20.0, c);
	CHECK_THAT(c[2], WithinAbs(1.0, 1e-12));
	PlasmaColour(8.31, c);
	CHECK_THAT(c[1], WithinAbs(0.32, 1e-12));
	auto I = [](double rho, double V) { return PlasmaIntensity(0.5 * rho * V * V, V); }; // US76 densities
	CHECK(I(3.416e-6, 7600.0) < 0.02); // 90 km
	double i85 = I(6.958e-6, 7600.0), i84 = I(7.955e-6, 7600.0);
	CHECK(i85 >= 0.05); CHECK(i85 <= 0.25);
	CHECK(i84 >= 0.05); CHECK(i84 <= 0.25);
	CHECK(I(1.846e-5, 7600.0) > 0.5);  // 80 km
	CHECK(I(3.992e-5, 7600.0) > 0.95); // 75 km
	CHECK(I(1e-3, 2900.0) == 0.0);
	CHECK(I(0.0, 7600.0) == 0.0);
}

TEST_CASE("PlumeMath: chord integral of the parabolic sphere", "[plumemath]")
{
	auto num = [](double R, double d, double u0, double u1) {
		const int n = 4096;
		double s = 0.0, du = (u1 - u0) / n;
		for (int i = 0; i < n; i++) {
			double u = u0 + (i + 0.5) * du, r2 = d * d + u * u;
			if (r2 < R * R) s += (1.0 - r2 / (R * R)) * du;
		}
		return s;
	};
	double R = 3.0;
	for (double d : { 0.0, 0.7, 1.9, 2.95 }) {
		double h = std::sqrt(R * R - d * d);
		CHECK_THAT(ChordIntegral(R, d, -h, h), WithinAbs(num(R, d, -h, h), 1e-4));
		CHECK_THAT(ChordIntegral(R, d, -0.3 * h, 0.8 * h), WithinAbs(num(R, d, -0.3 * h, 0.8 * h), 1e-4));
		CHECK_THAT(ChordIntegral(R, d, -10.0, 0.1), WithinAbs(num(R, d, -h, std::min(0.1, h)), 1e-4));
		CHECK_THAT(ChordFull(R, d), WithinAbs(num(R, d, -h, h), 1e-4));
	}
	CHECK(ChordIntegral(R, 3.5, -5.0, 5.0) == 0.0);
}

TEST_CASE("PlumeMath: Henyey-Greenstein normalisation", "[plumemath]")
{
	for (double g : { 0.0, 0.35, 0.5, -0.3, 0.8 }) {
		const int n = 20000;
		double s = 0.0;
		for (int i = 0; i < n; i++) {
			double c = -1.0 + (i + 0.5) * 2.0 / n;
			s += HG(c, g) * 2.0 * 3.14159265358979 * (2.0 / n);
		}
		CHECK_THAT(s, WithinAbs(1.0, 1e-3));
	}
}

TEST_CASE("PlumeMath: band integral", "[plumemath]")
{
	for (double c : { 1.0, 0.5, 0.2, 0.13 }) {
		double lam = 0.3, s = 0.0, ds = 1e-4;
		for (double x = 0.0; x < 40.0; x += ds) s += std::exp(-(x + 0.5 * ds) * c / lam) * ds;
		CHECK_THAT(BandIntegral(lam, c, 8.0), WithinRel(s, 1e-3));
	}
	CHECK_THAT(BandIntegral(0.3, 0.01, 8.0), WithinAbs(2.4, 1e-12));
	CHECK_THAT(BandIntegral(0.3, 0.0, 8.0), WithinAbs(2.4, 1e-12));
}

TEST_CASE("PlumeMath: deep opacity map", "[plumemath]")
{
	double sun[3] = { 0.0, 1.0, 0.0 };
	std::vector<Puff> p = {
		{ { 0, 10, 0 }, 2.0, 0.5 },  // toward the sun
		{ { 0, 0, 0 }, 2.0, 0.5 },   // behind it
		{ { 30, 0, 0 }, 2.0, 0.5 },  // beside
		{ { -30, 0, 0 }, 2.0, 0.5 }, // beside
	};
	std::vector<double> t;
	DeepOpacity(p, sun, 48, t);
	CHECK(t[0] == 0.0);
	CHECK(t[1] > 0.5 * 0.5 * 4.0 / 3.0 * 2.0); // a cell off the exact centre at most
	CHECK(t[1] <= 0.5 * 4.0 / 3.0 * 2.0 + 1e-9);
	CHECK(t[2] == 0.0);
	CHECK(t[3] == 0.0);
	std::vector<Puff> q = { p[3], p[1], p[2], p[0] };
	std::vector<double> u;
	DeepOpacity(q, sun, 48, u);
	CHECK(u[0] == t[3]); CHECK(u[1] == t[1]); CHECK(u[2] == t[2]); CHECK(u[3] == t[0]);
	std::mt19937 rng(7);
	std::uniform_real_distribution<double> U(-50.0, 50.0);
	std::vector<Puff> r;
	for (int i = 0; i < 300; i++) r.push_back({ { U(rng), U(rng), U(rng) }, 3.0 + 0.02 * i, 0.2 });
	std::vector<double> a, b;
	DeepOpacity(r, sun, 48, a);
	std::vector<int> idx(r.size());
	for (size_t i = 0; i < idx.size(); i++) idx[i] = int(i);
	std::shuffle(idx.begin(), idx.end(), rng);
	std::vector<Puff> rs;
	for (int i : idx) rs.push_back(r[i]);
	DeepOpacity(rs, sun, 48, b);
	for (size_t k = 0; k < idx.size(); k++) CHECK(b[k] == a[idx[k]]);
}

TEST_CASE("PlumeMath: flow map projection", "[plumemath]")
{
	double c[3] = { 10, -4, 30 }, f[3] = { 0.3, -0.2, 0.93 }, vp[16], X[3], Y[3];
	double fl = std::sqrt(0.09 + 0.04 + 0.93 * 0.93);
	for (double &v : f) v /= fl;
	double Rb = 20.0;
	FlowViewProj(c, f, 1.15 * Rb, Rb + 5.0, 1.0, 2.0 * Rb + 10.0, vp, X, Y);
	auto proj = [&](const double p[3], double o[4]) {
		for (int j = 0; j < 4; j++) o[j] = p[0] * vp[j] + p[1] * vp[4 + j] + p[2] * vp[8 + j] + vp[12 + j];
	};
	double up[3] = { c[0] - f[0] * 5, c[1] - f[1] * 5, c[2] - f[2] * 5 }, dn[3] = { c[0] + f[0] * 5, c[1] + f[1] * 5, c[2] + f[2] * 5 };
	double a[4], b[4], m[4];
	proj(up, a); proj(dn, b); proj(c, m);
	CHECK(a[2] < b[2]);
	CHECK(a[2] > 0.0); CHECK(b[2] < 1.0);
	CHECK_THAT(m[0], WithinAbs(0.0, 1e-9)); CHECK_THAT(m[1], WithinAbs(0.0, 1e-9));
	CHECK_THAT(m[2], WithinAbs((Rb + 5.0 - 1.0) / (2.0 * Rb + 9.0), 1e-9)); // linear depth
	double cx = X[1] * Y[2] - X[2] * Y[1], cy = X[2] * Y[0] - X[0] * Y[2], cz = X[0] * Y[1] - X[1] * Y[0];
	CHECK_THAT(cx * -f[0] + cy * -f[1] + cz * -f[2], WithinAbs(1.0, 1e-9)); // X x Y = eye - at: right-handed
	double px[3] = { c[0] + X[0] * Rb, c[1] + X[1] * Rb, c[2] + X[2] * Rb }, o[4];
	proj(px, o);
	CHECK_THAT(o[0], WithinAbs(1.0 / 1.15, 1e-9));
}

TEST_CASE("PlumeMath: clustering", "[plumemath]")
{
	std::vector<std::pair<double, std::pair<double, double>>> hex; // 33 nearly touching 1.3 m nozzles on a 1.4 m hexagonal lattice
	for (int i = -6; i <= 6; i++)
		for (int j = -6; j <= 6; j++) {
			double x = 1.4 * (i + 0.5 * j), y = 1.4 * 0.8660254 * j;
			hex.push_back({ x * x + y * y, { x, y } });
		}
	std::sort(hex.begin(), hex.end());
	std::vector<Nozzle> raptors;
	for (int i = 0; i < 33; i++) raptors.push_back({ { hex[i].second.first, hex[i].second.second, 0 }, { 0, 0, 1 }, 0.65, 1.0, 50.0, 1, 0 });
	std::vector<std::vector<int>> g;
	Cluster(raptors, 32, g);
	CHECK(g.size() == 1);
	std::vector<Nozzle> ssme = { // Atlantis.h refs, r_e 1.164
		{ { 0.0, 3.2, -15.5 }, { 0, -0.16, -0.99 }, 1.164, 1.0, 40.0, 0, 0 },
		{ { -1.55, 0.25, -15.5 }, { 0, -0.16, -0.99 }, 1.164, 1.0, 40.0, 0, 0 },
		{ { 1.55, 0.25, -15.5 }, { 0, -0.16, -0.99 }, 1.164, 1.0, 40.0, 0, 0 } };
	for (auto &s : ssme) { double l = std::sqrt(s.axis[1] * s.axis[1] + s.axis[2] * s.axis[2]); s.axis[1] /= l; s.axis[2] /= l; }
	Cluster(ssme, 32, g);
	CHECK(g.size() == 3);
	std::vector<Nozzle> opp = { { { 0, 0, 0 }, { 0, 0, 1 }, 1.0, 1.0, 50.0, 0, 0 }, { { 0, 0, 0.5 }, { 0, 0, -1 }, 1.0, 1.0, 50.0, 0, 0 } };
	Cluster(opp, 32, g);
	CHECK(g.size() == 2);
	std::vector<Nozzle> far;
	for (int i = 0; i < 40; i++) far.push_back({ { 10.0 * i, 0, 0 }, { 0, 0, 1 }, 1.0, 1.0, 50.0, 4, 0 });
	Cluster(far, 32, g);
	CHECK(g.size() <= 32);
	CHECK(g.size() >= 30);
	std::vector<Nozzle> two = { { { 0, 0, 0 }, { 0, 0, 1 }, 1.0, 1.0, 50.0, 4, 0 }, { { 0.5, 0, 0 }, { 0, 0, 1 }, 1.0, 1.0, 50.0, 4, 1 } };
	Cluster(two, 32, g);
	CHECK(g.size() == 2); // other vessel
}

TEST_CASE("PlumeMath: capsule culling", "[plumemath]")
{
	double vp[16] = { 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0 / 1000.0, 1.0, 0, 0, -0.1, 0 }; // z' = z/1000 - 0.1, w = z: 90 degrees down +z, near 100 m, far at infinity
	double pl[6][4];
	FrustumPlanes(vp, pl);
	double a[3] = { 0, 0, 500 }, b[3] = { 0, 0, 520 };
	CHECK(CapsuleVisible(pl, a, b, 5.0));
	double c[3] = { 0, 0, -50 }, d[3] = { 0, 0, -20 }; // behind the camera
	CHECK_FALSE(CapsuleVisible(pl, c, d, 5.0));
	double e[3] = { 900, 0, 500 }, f2[3] = { 950, 0, 500 }; // right of the frustum
	CHECK_FALSE(CapsuleVisible(pl, e, f2, 5.0));
	double g[3] = { 503, 0, 500 }; // just outside, its radius reaches in
	CHECK(CapsuleVisible(pl, g, g, 10.0));
}

namespace {
// a flat rectangle at height y as two triangles, its vertex normals up or down
TEST_CASE("PlumeMath: condensate in the vacuum plume", "[plumemath]")
{
	CHECK(Condensate(101325.0, 8000.0) == 0.0); // sea level
	CHECK(Condensate(150.0, 8000.0) == 0.0);    // SRB separation, ~45 km: no ice yet
	CHECK(Condensate(0.8, 8000.0) == 0.0);      // 1e-4 of the exit pressure: onset
	CHECK(Condensate(0.08, 8000.0) == 1.0);     // 1e-5
	CHECK(Condensate(0.0, 8000.0) == 1.0);      // vacuum
	double m = Condensate(0.8 / std::sqrt(10.0), 8000.0);
	CHECK_THAT(m, WithinAbs(0.5, 1e-9));
	CHECK(Condensate(0.0, 0.0) == 1.0);         // no exit pressure known
}

void Rect (SurfaceGrid &g, double x0, double z0, double x1, double z1, double y, bool up = true)
{
	double a[3] = { x0, y, z0 }, b[3] = { x1, y, z0 }, c[3] = { x1, y, z1 }, d[3] = { x0, y, z1 }, n[3] = { 0, up ? 3.0 : -3.0, 0 };
	g.Add(a, b, c, n);
	g.Add(a, c, d, n);
}
// a launch pad: a mound at 16 m with a trench along x, a platform deck at 30 m with its underside at 22 m and a hole in the middle
SurfaceGrid Pad ()
{
	SurfaceGrid g;
	Rect(g, -60, 5, 60, 60, 16);
	Rect(g, -60, -60, 60, -5, 16);
	Rect(g, -20, 5, 20, 15, 30);
	Rect(g, -20, -15, 20, -5, 30);
	Rect(g, -20, -5, -5, 5, 30);
	Rect(g, 5, -5, 20, 5, 30);
	Rect(g, -20, 5, 20, 15, 22, false);
	Rect(g, -20, -15, 20, -5, 22, false);
	Rect(g, 200, -10, 220, 10, 8); // a roof
	return g;
}
}

TEST_CASE("PlumeMath: base surfaces under a plume, a pad's hole and deck", "[plumemath]")
{
	SurfaceGrid g = Pad();
	CHECK(g.Count() == 2 * 7); // the undersides face down: left out
	double wall[3][3] = { { 0, 0, 0 }, { 10, 0, 0 }, { 0, 10, 0 } }, nz[3] = { 0, 0, 1 };
	g.Add(wall[0], wall[1], wall[2], nz);
	double big[3][3] = { { -900, 0, -900 }, { 900, 0, -900 }, { 0, 0, 900 } }, ny[3] = { 0, 1, 0 };
	g.Add(big[0], big[1], big[2], ny);
	CHECK(g.Count() == 2 * 7); // a wall and a ground sheet: left out
	CHECK(g.Top(0, 0, 100) == SurfaceGrid::NONE); // the hole over the trench: nothing down to the terrain
	CHECK_THAT(g.Top(10, 10, 100), WithinAbs(30.0, 1e-9));
	CHECK_THAT(g.Top(10, 10, 29), WithinAbs(16.0, 1e-9)); // the ceiling: under the deck, the mound
	CHECK(g.Top(500, 500, 100) == SurfaceGrid::NONE);
	int ring = 0;
	CHECK_THAT(g.Deck(0, 0, 10, 33, ring), WithinAbs(30.0, 1e-9)); // the deck round the hole, a little above the nozzle
	CHECK(ring == 8);
	CHECK_THAT(g.Deck(0, 0, 10, 29, ring), WithinAbs(16.0, 1e-9));
	CHECK(ring == 6); // under the deck: the mound, but not over the trench on two sides
	CHECK_THAT(g.Deck(195, 0, 10, 50, ring), WithinAbs(8.0, 1e-9));
	CHECK(ring == 3); // a building's edge, not a hole
	double o[3] = { 10, 50, 10 }, down[3] = { 0, -1, 0 };
	CHECK_THAT(g.Hit(o, down, 100), WithinAbs(20.0, 1e-9));
	double oh[3] = { 0, 28, 0 };
	CHECK(g.Hit(oh, down, 28) == SurfaceGrid::NONE); // down the hole
	double os[3] = { -25, 40, 10 }, slant[3] = { 1 / std::sqrt(2.0), -1 / std::sqrt(2.0), 0 };
	CHECK_THAT(g.Hit(os, slant, 60), WithinAbs(10 * std::sqrt(2.0), 1e-6)); // the deck's top at x -15
	CHECK(g.Hit(o, down, 10) == SurfaceGrid::NONE); // the deck is past tmax
	SurfaceGrid p;
	Rect(p, 298, -3, 304, 3, 30); // a 6 m platform
	double of[3] = { 301 - 970, 1000, 0 };
	CHECK_THAT(p.Hit(of, slant, 1000 * std::sqrt(2.0)), WithinAbs(970 * std::sqrt(2.0), 1e-6)); // from 1 km up the march starts at the top
	CHECK(p.Hit(of, slant, 900 * std::sqrt(2.0)) == SurfaceGrid::NONE); // the top is past tmax
}
