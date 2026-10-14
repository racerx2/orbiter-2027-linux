// not upstream: re-entry hull heating maths (OVP/VulkanClient/HeatMath.cpp, design-reentry-heat 15, Rev 3 real edges)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "HeatMath.h"
#include <array>
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
	CHECK_THAT(LocalRatio(1.0, 0.0, Rn, kfp, 0.0), WithinAbs(1.0 + Tune::lee, 1e-9)); // stagnation point, nose radius R_n
	CHECK_THAT(LocalRatio(1.0 + 1e-7, 0.0, Rn, kfp, 0.0), WithinAbs(1.0 + Tune::lee, 1e-6)); // c over 1 by rounding (review M3c)
	CHECK(std::isfinite(LocalRatio(1.0 + 1e-7, 0.0, Rn, kfp, 0.0)));
	double c = 0.64, x = 10.0;
	double fb = kfp * std::sqrt(std::sqrt(1.0 - c * c)) * c * std::sqrt(Rn / x);
	CHECK_THAT(LocalRatio(c, x, Rn, kfp, 0.0), WithinAbs(fb + Tune::lee, 1e-9)); // belly, Tauber's flat plate
	CHECK_THAT(LocalRatio(c, x, Rn, kfp, 0.9), WithinAbs(0.9 + Tune::lee, 1e-9)); // an edge band above it
	CHECK_THAT(LocalRatio(-0.5, 5.0, Rn, kfp, 0.0), WithinAbs(Tune::lee, 1e-12)); // lee
	CHECK_THAT(LocalRatio(1e-9, 0.5, Rn, kfp, 0.0), WithinAbs(LocalRatio(-1e-9, 0.5, Rn, kfp, 0.0), 1e-6)); // continuous at the shoulder
	CHECK(LocalRatio(1.0, 0.0, Rn, kfp, 5.0) <= 2.0);
	CHECK_THAT(AngleFactor(1.0), WithinAbs(1.0, 1e-12));
	CHECK_THAT(AngleFactor(0.0), WithinAbs(0.1, 1e-12));
	CHECK(AngleFactor(-0.25) == 0.0);
}

TEST_CASE("HeatMath: edge level and band", "[heatmath]")
{
	double Rn = 1.0, d2r = HPI / 180.0;
	CHECK_THAT(EdgeLevel(0.01, Rn, 1.0, 0.0, 0.0), WithinAbs(Tune::edge, 1e-12)); // a sharp edge is capped
	CHECK_THAT(EdgeLevel(2.0, Rn, 1.0, 0.0, 0.0), WithinAbs(0.5, 1e-12)); // Fay-Riddell cylinder: sqrt(R_n/2R)
	CHECK_THAT(EdgeLevel(2.0, Rn, 0.5, 0.0, 0.0), WithinAbs(0.25, 1e-12)); // swept: cos L
	CHECK_THAT(EdgeLevel(2.0, Rn, 1.0, 60.0 * d2r, 0.0), WithinAbs(0.5 * AngleFactor(0.5), 1e-12)); // Lees around the feature
	CHECK(EdgeLevel(0.01, Rn, 1.0, 20.0 * d2r, Tune::edgeBeta * d2r) == 0.0); // the attachment line off the feature
	double prev = EdgeLevel(0.3, Rn, 1.0, 0.0, 0.0);
	for (double b = 0.0; b < 20.0; b += 0.05) { // continuous as the line leaves the feature
		double g = EdgeLevel(0.3, Rn, 1.0, b * d2r, b * d2r);
		CHECK(std::fabs(g - prev) < 0.02);
		CHECK(g <= prev + 1e-12);
		prev = g;
	}
	CHECK_THAT(EdgeBand(0.0, 0.2, 1.1, 1.0), WithinAbs(1.1, 1e-12));
	CHECK_THAT(EdgeBand(0.2 * HPI / 3.0, 0.2, 1.0, 1.0), WithinAbs(AngleFactor(0.5), 1e-9)); // 60 deg around a cylinder of the band's radius
	CHECK(EdgeBand(1.0, 0.2, 1.0, 1.0) == 0.0);
	CHECK(EdgeBand(0.0, 0.2, 1.0, -0.6) == 0.0); // the far side of a thin edge
	CHECK(EdgeBand(0.0, 0.0, 1.0, 1.0) == 0.0);
}

// a closed prism: the polygon (z, y) counter-clockwise, extruded along x in n slices, with end caps (outward normals)
static void Prism (const std::vector<std::array<double, 2>> &poly, double x0, double x1, int n, std::vector<float> &xyz, std::vector<uint32_t> &tri)
{
	size_t m = poly.size();
	uint32_t base = uint32_t(xyz.size() / 3);
	for (int s = 0; s <= n; s++) {
		double x = x0 + (x1 - x0) * s / n;
		for (auto &p : poly) { xyz.push_back(float(x)); xyz.push_back(float(p[1])); xyz.push_back(float(p[0])); }
	}
	auto V = [&](int s, size_t i) { return base + uint32_t(s * m + i % m); };
	for (int s = 0; s < n; s++) for (size_t i = 0; i < m; i++) {
		uint32_t A = V(s, i), B = V(s, i + 1), C = V(s + 1, i + 1), D = V(s + 1, i);
		tri.insert(tri.end(), { A, C, B, A, D, C });
	}
	for (size_t i = 1; i + 1 < m; i++) {
		tri.insert(tri.end(), { V(0, 0), V(0, i), V(0, i + 1) });
		tri.insert(tri.end(), { V(n, 0), V(n, i + 1), V(n, i) });
	}
}

static void Build (EdgeSet &es, const std::vector<float> &xyz, const std::vector<uint32_t> &tri)
{
	es.Begin(xyz, tri);
	for (int i = 0; i < 100000; i++) { size_t r = 3000; if (es.Step(r)) break; }
	REQUIRE(es.Done());
}

static void Flow (double alpha, double slip, double f[3]) // downstream direction for an angle of attack and sideslip, vessel axes (x right, y up, z forward)
{
	double a = alpha * HPI / 180.0, b = slip * HPI / 180.0;
	f[0] = -std::sin(b); f[1] = std::sin(a) * std::cos(b); f[2] = -std::cos(a) * std::cos(b);
}

static const EdgeSeg *Best (const std::vector<EdgeSeg> &s)
{
	const EdgeSeg *b = nullptr;
	for (auto &e : s) if (!b || e.level > b->level) b = &e;
	return b;
}

// a wing slice: sharp wedge leading edge at z = 0.5, flat upper and lower faces 0.08 m apart with a 15 deg ridge on top, a square trailing edge at z = -3
static std::vector<std::array<double, 2>> Wing (bool ridge)
{
	std::vector<std::array<double, 2>> p = { { 0.5, 0.0 }, { 0.0, 0.04 } };
	if (ridge) { p.push_back({ -1.4, 0.04 }); p.push_back({ -1.5, 0.04 + 0.1 * std::tan(15.0 * HPI / 180.0) }); p.push_back({ -1.6, 0.04 }); }
	p.push_back({ -3.0, 0.04 }); p.push_back({ -3.0, -0.04 }); p.push_back({ 0.0, -0.04 });
	return p; // (z, y), counter-clockwise seen with z right and y up
}

TEST_CASE("HeatMath: real edges of a wing", "[heatmath]")
{
	std::vector<float> xyz;
	std::vector<uint32_t> tri;
	Prism(Wing(true), -2.0, 2.0, 4, xyz, tri);
	EdgeSet es;
	Build(es, xyz, tri);
	CHECK(es.tris == tri.size() / 3);
	CHECK(es.real > 0);
	std::vector<EdgeSeg> s;
	double f[3], Rn = 0.5;
	Flow(0.0, 0.0, f); // head on: the leading edge holds the attachment line over the whole span, merged into one segment
	es.Attach(f, Rn, s);
	const EdgeSeg *b = Best(s);
	REQUIRE(b);
	CHECK_THAT(b->level, WithinAbs(Tune::edge, 1e-9));
	CHECK_THAT(b->a[2], WithinAbs(0.5, 1e-6));
	CHECK_THAT(b->b[2], WithinAbs(0.5, 1e-6));
	CHECK_THAT(std::fabs(b->b[0] - b->a[0]), WithinAbs(4.0, 1e-6));
	CHECK_THAT(b->n[2], WithinAbs(1.0, 1e-9));
	s.clear(); // swept 40 deg: cos L
	Flow(0.0, 40.0, f);
	es.Attach(f, Rn, s);
	b = Best(s);
	REQUIRE(b);
	CHECK_THAT(b->level, WithinAbs(Tune::edge * std::cos(40.0 * HPI / 180.0), 1e-6));
	s.clear(); // from behind: nothing on the leading edge
	f[0] = 0.0; f[1] = 0.0; f[2] = 1.0;
	es.Attach(f, Rn, s);
	for (auto &e : s) CHECK(!(e.a[2] > 0.4 && e.b[2] > 0.4));
	for (double al : { -60.0, -65.0, -70.0, 60.0, 65.0 }) { // the ridge faces the flow but is no edge (code review MUST 1), at any attitude
		s.clear();
		Flow(al, 0.0, f);
		es.Attach(f, Rn, s);
		for (auto &e : s) CHECK(!(e.a[2] > -1.8 && e.a[2] < -1.2 && e.b[2] > -1.8 && e.b[2] < -1.2));
		for (auto &e : s) CHECK(std::isfinite(e.level));
		for (auto &e : s) if (e.a[2] > 0.4 && e.b[2] > 0.4) CHECK(e.level <= (std::fabs(al) < 62.0 ? 0.25 * Tune::edge : 0.0)); // the leading edge 25 deg or less from a face's normal: the line lies on the face
	}
	double prev = -1.0; // continuous in the attitude
	for (double al = -80.0; al <= 80.0; al += 0.5) {
		s.clear();
		Flow(al, 0.0, f);
		es.Attach(f, Rn, s);
		double best = 0.0;
		for (auto &e : s) if (e.a[2] > 0.4 && e.b[2] > 0.4) best = std::max(best, e.level);
		if (prev >= 0.0) CHECK(std::fabs(best - prev) < 0.05);
		prev = best;
	}
}

TEST_CASE("HeatMath: rounded leading edge", "[heatmath]")
{
	std::vector<std::array<double, 2>> p; // a 4 cm radius nose in 10 facets from the lower to the upper face
	for (int i = 0; i <= 10; i++) { double a = -0.5 * HPI + HPI * i / 10.0; p.push_back({ 0.04 * std::cos(a), 0.04 * std::sin(a) }); }
	p.push_back({ -3.0, 0.04 }); p.push_back({ -3.0, -0.04 }); // counter-clockwise
	std::vector<float> xyz;
	std::vector<uint32_t> tri;
	Prism(p, -2.0, 2.0, 2, xyz, tri);
	EdgeSet es;
	Build(es, xyz, tri);
	std::vector<EdgeSeg> s;
	double f[3], Rn = 0.5;
	for (double al : { 0.0, 30.0, -30.0 }) { // the band sits on the nose facet facing the flow
		s.clear();
		Flow(al, 0.0, f);
		es.Attach(f, Rn, s);
		const EdgeSeg *b = Best(s);
		REQUIRE(b);
		double ang = std::atan2(b->a[1], b->a[2]) * 180.0 / HPI;
		CHECK(std::fabs(ang + al) < 12.0);
		CHECK(b->level > 0.5);
		CHECK(b->w >= Tune::edgeBand * Rn - 1e-12);
	}
}

TEST_CASE("HeatMath: exposure and shielding", "[heatmath]")
{
	std::vector<float> xyz;
	std::vector<uint32_t> tri;
	std::vector<std::array<double, 2>> boxc = { { 5.0, -1.0 }, { 5.0, 1.0 }, { -5.0, 1.0 }, { -5.0, -1.0 } }; // (z, y) counter-clockwise
	std::vector<std::array<double, 2>> innerc = { { 0.5, -0.3 }, { 0.5, 0.3 }, { -0.5, 0.3 }, { -0.5, -0.3 } };
	Prism(boxc, -5.0, 5.0, 1, xyz, tri);
	size_t nOuter = tri.size();
	Prism(innerc, -0.5, 0.5, 1, xyz, tri); // closed inside the big box: never exposed
	EdgeSet es;
	Build(es, xyz, tri);
	CHECK(es.real > 0);
	std::vector<EdgeSeg> s;
	double f[3] = { 0.0, -std::sqrt(0.5), -std::sqrt(0.5) };
	es.Attach(f, 0.5, s);
	for (auto &e : s) CHECK((std::fabs(e.a[1]) > 0.9 || std::fabs(e.a[2]) > 4.0)); // only the big box's edges
	(void)nOuter;
	for (double h : { 0.02, 0.3 }) { // a step on the top face: a small one sits in the body's layers, a tall one stands out
		std::vector<float> x2;
		std::vector<uint32_t> t2;
		std::vector<std::array<double, 2>> topc = { { -1.0, 1.0 }, { -1.0, 1.0 + h }, { -2.0, 1.0 + h }, { -2.0, 1.0 } };
		Prism(boxc, -5.0, 5.0, 1, x2, t2);
		Prism(topc, -1.0, 1.0, 1, x2, t2);
		EdgeSet e2;
		Build(e2, x2, t2);
		double g[3] = { 0.0, -std::sin(40.0 * HPI / 180.0), -std::cos(40.0 * HPI / 180.0) }; // from ahead and above
		double p[3] = { 0.0, 1.0 + h, -1.0 }, u[3] = { 0.0, std::sin(40.0 * HPI / 180.0), std::cos(40.0 * HPI / 180.0) }, t[3] = { 1.0, 0.0, 0.0 };
		CHECK(e2.Shielded(p, u, t) == (h < 0.1));
		std::vector<EdgeSeg> s2;
		e2.Attach(g, 0.5, s2);
		bool front = false;
		for (auto &e : s2) if (std::fabs(e.a[2] + 1.0) < 1e-3 && std::fabs(e.a[1] - (1.0 + h)) < 1e-3 && std::fabs(e.b[2] + 1.0) < 1e-3) front = true;
		CHECK(front == (h > 0.1));
	}
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
