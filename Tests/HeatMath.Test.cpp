// not upstream: re-entry hull heating maths (OVP/VulkanClient/HeatMath.cpp, design-reentry-heat 15)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "HeatMath.h"
#include <cmath>
#include <random>

using namespace HeatMath;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

static const double CP = 1004.5;

TEST_CASE("HeatMath: Sutton-Graves stagnation flux", "[heatmath]")
{
	double k = SuttonGravesK(287.0);
	CHECK(k == 1.7415e-4);
	CHECK(SuttonGravesK(188.9) == 1.9027e-4);
	CHECK(SuttonGravesK(3600.0) == 0.6556e-4);
	double q = SuttonGraves(1e-4, 1.0, 7000.0, k);
	CHECK_THAT(q, WithinRel(1.7415e-4 * 0.01 * 343e9, 1e-9));
	CHECK_THAT(SuttonGraves(4e-4, 1.0, 7000.0, k) / q, WithinRel(2.0, 1e-9));
	CHECK_THAT(SuttonGraves(1e-4, 4.0, 7000.0, k) / q, WithinRel(0.5, 1e-9));
	CHECK_THAT(SuttonGraves(1e-4, 1.0, 14000.0, k) / q, WithinRel(8.0, 1e-9));
	double qfm = SuttonGraves(1e-9, 1.0, 7000.0, k);
	CHECK_THAT(qfm, WithinRel(0.5 * 1e-9 * 343e9, 1e-9)); // free-molecular cap
	CHECK(SuttonGraves(0.0, 1.0, 7000.0, k) == 0.0);
	CHECK(SuttonGraves(1e-4, 1.0, 0.0, k) == 0.0);
}

TEST_CASE("HeatMath: hot wall and flat plate", "[heatmath]")
{
	double h0 = CP * 220.0 + 0.5 * 7500.0 * 7500.0;
	CHECK(HotWallFactor(0.0, CP, h0) == 1.0);
	CHECK_THAT(HotWallFactor(h0 / CP, CP, h0), WithinAbs(0.0, 1e-12));
	CHECK_THAT(HotWallFactor(2.0 * h0 / CP, CP, h0), WithinAbs(-1.0, 1e-12)); // above the recovery temperature: convective cooling (review N10)
	CHECK(HotWallFactor(-5.0, CP, h0) == 1.0);
	CHECK_THAT(KFlatPlate(7500.0), WithinRel(0.1453 * std::pow(7500.0, 0.2), 2e-3));
	CHECK_THAT(KFlatPlate(7500.0), WithinAbs(0.87, 0.01));
}

TEST_CASE("HeatMath: equilibrium wall", "[heatmath]")
{
	Wall w = { 0.85, HeatCapacity(25.0, 0.85), 220.0 };
	double q = 4.43e5;
	double Tcw = EquilibriumWall(q, 1.0, CP, 1e30, w);
	CHECK_THAT(Tcw, WithinRel(std::pow(q / (0.85 * SIGMA) + std::pow(220.0, 4.0), 0.25), 1e-6));
	double h0 = CP * 220.0 + 0.5 * 7500.0 * 7500.0;
	double Thw = EquilibriumWall(q, 1.0, CP, h0, w);
	CHECK(Thw < Tcw);
	CHECK_THAT(Thw, WithinAbs(1714.0, 5.0)); // design 2.1, Atlantis at 75 km
	CHECK_THAT(EquilibriumWall(0.0, 1.0, CP, h0, w), WithinAbs(220.0, 1e-3));
	CHECK(EquilibriumWall(q, 0.1, CP, h0, w) < Thw);
}

TEST_CASE("HeatMath: wall lag", "[heatmath]")
{
	Wall w = { 0.85, HeatCapacity(25.0, 0.85), 3.0 };
	double q = 0.85 * SIGMA * std::pow(1500.0, 4.0); // equilibrium at 1500 K, cold wall
	double T = 1510.0;
	for (int i = 0; i < 100; i++) T = StepWall(T, 0.1, q, 1.0, CP, 1e30, w);
	CHECK_THAT(T - 1500.0, WithinRel(10.0 * std::exp(-10.0 / 25.0), 0.02)); // tau = lag at Tref
	CHECK(StepWall(T, 0.0, q, 1.0, CP, 1e30, w) == T); // pause
	double big = StepWall(800.0, 1e4, q, 1.0, CP, 1e30, w); // time warp: no overshoot
	CHECK(big > 800.0);
	CHECK(big <= 1500.0 + 1e-6);
	CHECK(std::isfinite(big));
	CHECK_THAT(StepWall(800.0, 1e12, q, 1.0, CP, 1e30, w), WithinAbs(1500.0, 0.01));
	double t = 0.0, Tc = 1500.0; // cooling from 1500 K with no heating
	while (Tc > 1000.0 && t < 1000.0) { Tc = StepWall(Tc, 0.05, 0.0, 1.0, CP, 1e30, w); t += 0.05; }
	CHECK_THAT(t, WithinAbs(80.0, 8.0));
}

TEST_CASE("HeatMath: afterglow after peak heating", "[heatmath]")
{
	Wall w = { 0.85, HeatCapacity(25.0, 0.85), 220.0 };
	double h0 = CP * 220.0 + 0.5 * 7500.0 * 7500.0;
	double q1 = 6e5;
	double T = EquilibriumWall(q1, 1.0, CP, h0, w);
	double last = T;
	bool above = true, mono = true;
	for (int i = 1; i <= 400; i++) {
		double s = i * 0.05;
		double q = q1 * std::max(0.0, 1.0 - s / 20.0);
		T = StepWall(T, 0.05, q, 1.0, CP, h0, w);
		double Te = EquilibriumWall(q, 1.0, CP, h0, w);
		if (!(T > Te)) above = false;
		if (T > last + 1e-9) mono = false;
		last = T;
	}
	CHECK(above);
	CHECK(mono);
	CHECK(T > 1000.0); // still glowing at 20 s, when the equilibrium is the sink
}

TEST_CASE("HeatMath: local heating ratio", "[heatmath]")
{
	double Rn = 1.0, kfp = KFlatPlate(7500.0);
	CHECK_THAT(LocalRatio(1.0, 0.0, 1.0, 1.0, 0.3, Rn, kfp), WithinAbs(1.0 + Tune::lee, 1e-9)); // stagnation point, nose radius R_n
	CHECK_THAT(LocalRatio(1.0 + 1e-7, 0.0, 1.0, 1.0, 0.0, Rn, kfp), WithinAbs(1.0 + Tune::lee, 1e-6)); // c over 1 by rounding (review M3c)
	CHECK(std::isfinite(LocalRatio(1.0 + 1e-7, 0.0, 1.0, 1.0, 0.0, Rn, kfp)));
	double edge = LocalRatio(1.0, 50.0, 10.0, 0.0, 0.0, Rn, kfp); // unswept edge, R = R_n/10, far from the nose
	CHECK_THAT(edge, WithinAbs(Tune::edge + Tune::lee, 1e-9));
	double s45 = std::sqrt(0.5), c45 = s45; // 45 deg swept edge: on its attachment line c = cos L, f . d2 = sin L
	double fp = kfp * std::sqrt(std::sqrt(1.0 - c45 * c45)) * c45 * std::sqrt(Rn / 50.0);
	CHECK_THAT(LocalRatio(c45, 50.0, 10.0, 0.0, s45, Rn, kfp), WithinAbs(std::sqrt(std::pow(Tune::edge * c45, 2.0) + fp * fp * 0.5) + Tune::lee, 1e-9));
	CHECK(LocalRatio(c45, 50.0, 10.0, 0.0, s45, Rn, kfp) > 0.7 * (edge - Tune::lee)); // review M1: 0.71 of the unswept edge, not 0
	CHECK_THAT(LocalRatio(c45, 50.0, 10.0, 10.0, s45, Rn, kfp), WithinAbs(LocalRatio(c45, 50.0, 10.0, 10.0, 0.0, Rn, kfp), 1e-12)); // a sphere has no sweep
	double c = 0.64, x = 10.0;
	double fb = kfp * std::sqrt(std::sqrt(1.0 - c * c)) * c * std::sqrt(Rn / x);
	CHECK_THAT(LocalRatio(c, x, 0.3, 0.0, 0.0, Rn, kfp), WithinAbs(fb + Tune::lee, 1e-9)); // belly: a fuselage curvature is no edge (review M2)
	CHECK_THAT(LocalRatio(c, x, 1.9, 0.0, 0.0, Rn, kfp), WithinAbs(fb + Tune::lee, 1e-9)); // below the gate
	double cc = 0.55, fc = kfp * std::sqrt(std::sqrt(1.0 - cc * cc)) * cc * std::sqrt(Rn / 20.0);
	CHECK_THAT(LocalRatio(cc, 20.0, 10.0, 0.0, 0.0, Rn, kfp), WithinAbs(fc + Tune::lee, 1e-9)); // a sharp crease on a windward panel holds no attachment line
	CHECK(LocalRatio(0.97, 20.0, 10.0, 0.0, 0.0, Rn, kfp) > 1.1); // a leading edge facing the flow does
	double fs = kfp * std::sqrt(std::sqrt(1.0 - cc * cc)) * cc * std::sqrt(Rn / 20.0);
	CHECK_THAT(LocalRatio(cc, 20.0, 10.0, 0.0, 0.7, Rn, kfp), WithinAbs(fs + Tune::lee, 1e-9)); // the same crease swept so that |f.d2| = 0.7: c_perp 0.77, still none (code review MUST 1)
	CHECK_THAT(LocalRatio(-0.5, 5.0, 10.0, 10.0, 0.0, Rn, kfp), WithinAbs(Tune::lee, 1e-12)); // lee
	CHECK_THAT(LocalRatio(1e-9, 0.5, 1.0, 1.0, 0.0, Rn, kfp), WithinAbs(LocalRatio(-1e-9, 0.5, 1.0, 1.0, 0.0, Rn, kfp), 1e-6)); // continuous at the shoulder
	double prev = LocalRatio(0.99, 20.0, 0.0, 0.0, 0.0, Rn, kfp); // continuous in the curvature gate band
	for (double k = 1.5; k < 4.5; k += 0.01) {
		double g = LocalRatio(0.99, 20.0, k, 0.0, 0.0, Rn, kfp);
		CHECK(std::fabs(g - prev) < 0.015);
		prev = g;
	}
	prev = LocalRatio(0.80, 20.0, 10.0, 0.0, 0.0, Rn, kfp); // and in the attachment gate band
	for (double c2 = 0.80; c2 < 1.0; c2 += 0.001) {
		double g = LocalRatio(c2, 20.0, 10.0, 0.0, 0.0, Rn, kfp);
		CHECK(std::fabs(g - prev) < 0.02);
		prev = g;
	}
	CHECK(LocalRatio(1.0, 0.0, 1e6, 1e6, 0.0, Rn, kfp) <= 2.0);
	CHECK_THAT(AngleFactor(1.0), WithinAbs(1.0, 1e-12));
	CHECK_THAT(AngleFactor(0.0), WithinAbs(0.1, 1e-12));
	CHECK(AngleFactor(-0.25) == 0.0);
}

static void Cross (const double a[3], const double b[3], double o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static double Dot3 (const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

TEST_CASE("HeatMath: Weingarten map from screen derivatives", "[heatmath]")
{
	std::mt19937 rng(7);
	std::uniform_real_distribution<double> U(-1.0, 1.0);
	int n = 0;
	for (int it = 0; it < 400 && n < 200; it++) {
		double N[3] = { U(rng), U(rng), U(rng) }, l = std::sqrt(Dot3(N, N));
		if (l < 0.2) continue;
		for (double &v : N) v /= l;
		double a[3] = { U(rng), U(rng), U(rng) }, t1[3], t2[3];
		Cross(N, a, t1);
		double lt = std::sqrt(Dot3(t1, t1));
		if (lt < 0.2) continue;
		for (double &v : t1) v /= lt;
		Cross(N, t1, t2);
		n++;
		double s1 = 0.01 + 0.05 * std::fabs(U(rng)), s2 = 0.01 + 0.05 * std::fabs(U(rng)), sh = 0.5 * U(rng);
		double dPx[3], dPy[3], dNx[3], dNy[3], k1, k2, d2[3];
		for (int i = 0; i < 3; i++) { dPx[i] = s1 * t1[i] + 0.3 * s1 * N[i]; dPy[i] = s2 * (t2[i] + sh * t1[i]); } // rotated, sheared, not in the tangent plane
		double R = 0.2 + 3.0 * std::fabs(U(rng));
		auto tang = [&](const double d[3], double o[3]) { double q = Dot3(N, d); for (int i = 0; i < 3; i++) o[i] = d[i] - q * N[i]; };
		double tx[3], ty[3];
		tang(dPx, tx); tang(dPy, ty);
		for (int i = 0; i < 3; i++) { dNx[i] = tx[i] / R; dNy[i] = ty[i] / R; } // sphere
		REQUIRE(Weingarten(N, dNx, dNy, dPx, dPy, k1, k2, d2));
		CHECK_THAT(k1, WithinRel(1.0 / R, 1e-6));
		CHECK_THAT(k2, WithinRel(1.0 / R, 1e-6));
		double ax[3] = { t1[0] * 0.6 + t2[0] * 0.8, t1[1] * 0.6 + t2[1] * 0.8, t1[2] * 0.6 + t2[2] * 0.8 }; // cylinder axis in the tangent plane
		for (int i = 0; i < 3; i++) { dNx[i] = (tx[i] - Dot3(tx, ax) * ax[i]) / R; dNy[i] = (ty[i] - Dot3(ty, ax) * ax[i]) / R; }
		REQUIRE(Weingarten(N, dNx, dNy, dPx, dPy, k1, k2, d2));
		CHECK_THAT(k1, WithinRel(1.0 / R, 1e-6));
		CHECK_THAT(k2, WithinAbs(0.0, 1e-6 / R));
		CHECK_THAT(std::fabs(Dot3(d2, ax)), WithinAbs(1.0, 1e-6)); // the low-curvature direction is the axis
		double rk1 = k1, rk2 = k2;
		double rx[3], ry[3], mx[3], my[3]; // the screen rolled 90 deg: same curvatures (review M1)
		for (int i = 0; i < 3; i++) { rx[i] = dPy[i]; ry[i] = -dPx[i]; mx[i] = dNy[i]; my[i] = -dNx[i]; }
		REQUIRE(Weingarten(N, mx, my, rx, ry, k1, k2, d2));
		CHECK_THAT(k1, WithinRel(rk1, 1e-9));
		CHECK_THAT(k2, WithinAbs(rk2, 1e-9));
		for (int i = 0; i < 3; i++) { dNx[i] = -tx[i] / R; dNy[i] = -ty[i] / R; } // concave: no edge
		REQUIRE(Weingarten(N, dNx, dNy, dPx, dPy, k1, k2, d2));
		CHECK(k1 == 0.0);
	}
	CHECK(n >= 100);
	double N[3] = { 0, 0, 1 }, d[3] = { 0.01, 0, 0 }, k1 = 1, k2 = 1, d2[3];
	CHECK(!Weingarten(N, d, d, d, d, k1, k2, d2)); // singular quad
	CHECK(k1 == 0.0);
	CHECK(k2 == 0.0);
	double far[3] = { 1e3, 0, 0 }, far2[3] = { 0, 1e3, 0 }, fn[3] = { 1e3 / 4.0, 0, 0 }, fn2[3] = { 0, 1e3 / 4.0, 0 };
	REQUIRE(Weingarten(N, fn, fn2, far, far2, k1, k2, d2)); // scale-free threshold (review M3d)
	CHECK_THAT(k1, WithinRel(0.25, 1e-9));
}

TEST_CASE("HeatMath: blackbody colour and luminance", "[heatmath]")
{
	double xyz[3];
	BlackbodyXYZ(6504.0, xyz);
	double s = xyz[0] + xyz[1] + xyz[2];
	CHECK_THAT(xyz[0] / s, WithinAbs(0.3135, 0.002));
	CHECK_THAT(xyz[1] / s, WithinAbs(0.3236, 0.002));
	BlackbodyXYZ(2042.0, xyz);
	CHECK_THAT(xyz[1], WithinRel(6.0e5, 0.03)); // CIPM 1946: 60 cd/cm2 at the freezing point of platinum
	double last = 0.0;
	for (double T = 700.0; T <= 3000.0; T += 50.0) {
		double rgb[3];
		Blackbody(T, rgb);
		CHECK(rgb[0] >= rgb[1]);
		CHECK(rgb[1] >= rgb[2]);
		BlackbodyXYZ(T, xyz);
		CHECK(xyz[1] > last);
		last = xyz[1];
	}
	double r[3];
	Blackbody(400.0, r);
	CHECK(r[0] < 1e-12);
}

TEST_CASE("HeatMath: colour table", "[heatmath]")
{
	ColourTable t;
	t.Build(64, 500.0, 4000.0);
	for (double T = 700.0; T <= 4000.0; T += 37.0) {
		double a[3], b[3];
		t.Lookup(T, a);
		Blackbody(T, b);
		double La = 0.2126 * a[0] + 0.7152 * a[1] + 0.0722 * a[2], Lb = 0.2126 * b[0] + 0.7152 * b[1] + 0.0722 * b[2];
		CHECK_THAT(La, WithinRel(Lb, 0.01));
	}
	double z[3];
	t.Lookup(450.0, z);
	CHECK(z[0] == 0.0);
	CHECK(z[1] == 0.0);
	for (float v : t.ln) CHECK(std::isfinite(v)); // review M3a: no -inf
	for (float v : t.ln) CHECK(v >= -60.0f);
	double lnB[NBIN][3]; // bins as HullHeat makes them, the hottest at 2400 K, the coldest cold
	for (int k = 0; k < NBIN; k++) {
		double rgb[3];
		t.Lookup(2400.0 * std::pow(BinRatio(k) / 2.0, 0.25), rgb);
		for (int j = 0; j < 3; j++) lnB[k][j] = rgb[j] > 0.0 ? std::max(-60.0, std::log(rgb[j] * Tune::camera / LREF)) : -60.0;
	}
	for (double G : { 2.0, 1.99999, 0.8, 0.0033, 0.00131, 1e-6, 0.0 }) {
		double e[3];
		ShaderBins(lnB, G, e);
		for (double v : e) { CHECK(std::isfinite(v)); CHECK(v >= 0.0); }
	}
	double e0[3], e1[3];
	ShaderBins(lnB, 2.0, e0);
	ShaderBins(lnB, 2.0 * 0.4, e1);
	CHECK(e0[0] > e1[0]);
}

TEST_CASE("HeatMath: bins", "[heatmath]")
{
	CHECK(LowPass(0.0, 5.0) == 0.0);
	CHECK(LowPass(-1.0, 5.0) == 0.0);
	CHECK_THAT(LowPass(1e9, 5.0), WithinAbs(1.0, 1e-12));
	CHECK_THAT(LowPass(5.0, 5.0), WithinAbs(1.0 - std::exp(-1.0), 1e-12));
	CHECK_THAT(FlowTau(25.0, 1500.0), WithinAbs(25.0 / 3.0, 1e-9));
	CHECK(FlowTau(25.0, 3000.0) >= 0.2);
	CHECK(FlowTau(25.0, 500.0) <= 25.0);
	CHECK_THAT(BinRatio(0), WithinAbs(2.0, 1e-12));
	CHECK_THAT(BinRatio(7), WithinRel(2.0 * std::pow(0.4, 7.0), 1e-12));
	for (int k = 0; k < NBIN; k++) CHECK_THAT(BinCoord(BinRatio(k)), WithinAbs(double(k), 1e-9));
	CHECK(BinCoord(5.0) == 0.0);
	CHECK(BinCoord(0.0) == double(NBIN + 1));
	double prev = -1.0;
	for (double G = 2.0; G > 1e-4; G *= 0.97) { double c = BinCoord(G); CHECK(c >= prev); prev = c; }
}

TEST_CASE("HeatMath: support points", "[heatmath]")
{
	std::mt19937 rng(3);
	std::normal_distribution<double> Nd(0.0, 1.0);
	std::vector<std::array<double, 3>> sph, set;
	const double R = 2.0;
	for (int i = 0; i < 2000; i++) {
		double v[3] = { Nd(rng), Nd(rng), Nd(rng) }, l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		sph.push_back({ R * v[0] / l + 5.0, R * v[1] / l, R * v[2] / l - 1.0 });
	}
	SupportSet(sph, set, 128);
	CHECK(set.size() <= 128);
	CHECK(set.size() >= 60);
	for (int i = 0; i < 300; i++) {
		double d[3] = { Nd(rng), Nd(rng), Nd(rng) }, l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		for (double &x : d) x /= l;
		double exact = -1e300;
		std::array<double, 3> ep = {};
		for (auto &p : sph) { double v = p[0] * d[0] + p[1] * d[1] + p[2] * d[2]; if (v > exact) { exact = v; ep = p; } }
		auto q = SupportQuery(set, d);
		CHECK_THAT(q[0] * d[0] + q[1] * d[1] + q[2] * d[2], WithinAbs(exact, 0.03 * R)); // 128 directions: about 12 deg apart at most
		CHECK(std::sqrt((q[0] - ep[0]) * (q[0] - ep[0]) + (q[1] - ep[1]) * (q[1] - ep[1]) + (q[2] - ep[2]) * (q[2] - ep[2])) < 0.3 * R);
	}
	std::vector<std::array<double, 3>> box;
	for (int i = 0; i < 1000; i++) {
		std::uniform_real_distribution<double> U(-1.0, 1.0);
		box.push_back({ 3.0 * U(rng), 1.0 * U(rng), 0.5 * U(rng) });
	}
	box.push_back({ 3.0, 1.0, 0.5 });
	SupportSet(box, set, 128);
	double d[3] = { 0.8, 0.5, 0.33 };
	auto q = SupportQuery(set, d);
	CHECK(q[0] == 3.0);
	CHECK(q[1] == 1.0);
	CHECK(q[2] == 0.5);
}

TEST_CASE("HeatMath: class cfg line", "[heatmath]")
{
	HeatCfg c;
	CHECK(ParseHeatLine("HEAT nose=0.4 emis=0.8 lag=30 glow=2 ; comment", c));
	CHECK(c.nose == 0.4);
	CHECK(c.emis == 0.8);
	CHECK(c.lag == 30.0);
	CHECK(c.glow == 2.0);
	CHECK(c.bad == 0);
	CHECK(!c.off);
	CHECK(ParseHeatLine("  heat off", c));
	CHECK(c.off);
	HeatCfg d;
	CHECK(ParseHeatLine("HEAT nose=0 emis=7 lag=-3 glow=99 junk nose=abc", d));
	CHECK(d.nose == 0.05);
	CHECK(d.emis == 1.0);
	CHECK(d.lag == 1.0);
	CHECK(d.glow == 10.0);
	CHECK(d.bad == 2);
	HeatCfg e;
	CHECK(!ParseHeatLine("EXHAUST main hydrolox", e));
	CHECK(!ParseHeatLine("; HEAT off", e));
	CHECK(!e.off);
}
