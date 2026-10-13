// not upstream: terrain horizon maps (OVP/VulkanClient/HorizonMap.cpp)

#include <catch2/catch_test_macros.hpp>
#include "HorizonMap.h"
#include <chrono>
#include <cmath>
#include <functional>

using namespace Horizon;

namespace {

const double R = 6371e3;
const double LSB = 1.25 / 255.0;

Grid MakeGrid (int lvl, double minlat, double maxlat, double minlng, double maxlng, const std::function<double(double, double)> &f)
{
	Grid g;
	g.lvl = lvl;
	g.minlat = minlat; g.maxlat = maxlat; g.minlng = minlng; g.maxlng = maxlng;
	g.h.assign(size_t(STRIDE) * STRIDE, 0.0f);
	double dlat = (maxlat - minlat) / 256.0, dlng = (maxlng - minlng) / 256.0;
	for (int i = -1; i <= 257; i++)
		for (int j = -1; j <= 257; j++)
			g.h[size_t(i + 1) * STRIDE + j + 1] = float(f(minlat + i * dlat, minlng + j * dlng));
	return g;
}

double S (const Map &m, int i, int j, int k) // sin of the horizon at node (i from south, j), azimuth k
{
	int x = k < 4 ? j : N + j;
	return Decode(m.Texel(x, 256 - i)[k & 3]);
}

const double SPAN = 0.001; // rad, about 6.4 km: 24.9 m nodes
const double PI_T = 3.14159265358979323846;

// the finest grid at a point, as Compute samples
float FinestAt (const Input &in, double lat, double lng)
{
	std::vector<const Grid*> g{ &in.owner };
	for (const Grid &x : in.grids) g.push_back(&x);
	std::stable_sort(g.begin(), g.end(), [](const Grid *a, const Grid *b) { return a->lvl > b->lvl; });
	double fi, fj;
	for (const Grid *x : g) if (x->Covers(lat, lng, fi, fj)) return x->At(fi, fj);
	return NODATA;
}

// a plain march in small steps, the reference for the near and far split: the azimuth itself to NEAR nodes, its 30 degree sector past them, as Compute's far rays
double March (const Input &in, int i, int j, int k)
{
	const Grid &o = in.owner;
	double dlat = (o.maxlat - o.minlat) / 256.0, dlng = (o.maxlng - o.minlng) / 256.0;
	double lat0 = o.minlat + i * dlat, lng0 = o.minlng + j * dlng, cl = std::cos(lat0);
	double node = in.R * dlat, m = -1e30;
	double h0 = o.h[size_t(i + 1) * STRIDE + j + 1] + 1.0;
	for (int r = -1; r <= 1; r++) {
		double th = k * PI_T / 4.0 + r * 15.0 * PI_T / 180.0;
		for (double t = 0.5 * node; t <= in.dmax; t += std::max(0.5 * node, 0.01 * t)) {
			if (r && t < NEAR * node) continue;
			float h = FinestAt(in, lat0 + std::sin(th) * t / in.R, lng0 + std::cos(th) * t / (in.R * cl));
			if (h <= NODATA) continue;
			m = std::max(m, (h - t * t / (2.0 * in.R) - h0) / t);
		}
	}
	return m > -1e29 ? std::max(-0.25, m / std::sqrt(1.0 + m * m)) : -0.25;
}

} // namespace

TEST_CASE("Horizon: encode round trip", "[horizon]")
{
	for (double s = -0.25; s <= 1.0; s += 0.0137)
		REQUIRE(std::fabs(Decode(Encode(s)) - s) <= LSB / 2 + 1e-9);
	REQUIRE(Encode(-1.0) == 0);
	REQUIRE(Encode(2.0) == 255);
	REQUIRE(DmaxFor(4900.0) == 49000.0);
	REQUIRE(DmaxFor(1000.0) == 30e3);
	REQUIRE(DmaxFor(1e6) == 300e3);
}

TEST_CASE("Horizon: flat ground has a level horizon", "[horizon]")
{
	Input in;
	in.R = R;
	in.owner = MakeGrid(12, 0.0, SPAN, 0.0, SPAN, [](double, double) { return 0.0; });
	Map m;
	Compute(in, m);
	REQUIRE(m.px.size() == size_t(W) * N * 4);
	for (int i = 0; i < N; i += 16)
		for (int j = 0; j < N; j += 16)
			for (int k = 0; k < 8; k++) {
				INFO("node " << i << "," << j << " azimuth " << k);
				bool edge = i < 32 || i > 224 || j < 32 || j > 224; // no data past the owner: only the 1 m eye height is seen
				if (edge) REQUIRE(S(m, i, j, k) < LSB);
				else REQUIRE(std::fabs(S(m, i, j, k)) < LSB);
			}
}

TEST_CASE("Horizon: a plateau east casts the right angle, nothing west", "[horizon]")
{
	Input in;
	in.R = R;
	in.dmax = 30e3;
	const double edge = SPAN * 200.0 / 256.0;
	in.owner = MakeGrid(12, 0.0, SPAN, 0.0, SPAN, [edge](double, double lng) { return lng >= edge ? 100.0 : 0.0; });
	Map m;
	Compute(in, m);
	const double node = R * SPAN / 256.0;
	for (int j : {96, 128, 160}) { // far field nodes
		double d = (200 - j) * node;
		double hi = std::sin(std::atan(99.0 / (d - node))), lo = std::sin(std::atan((99.0 - 1.44 * d * d / (2.0 * R)) / (1.2 * d))); // the ray grows 1.2 a step
		double s = S(m, 128, j, 0);
		REQUIRE(s <= hi + LSB);
		REQUIRE(s >= lo - LSB);
		REQUIRE(std::fabs(S(m, 128, j, 4)) < LSB); // west: flat
		double c30 = std::cos(30.0 * PI_T / 180.0), r2 = std::sqrt(2.0); // north-east and south-east: the 30 degree side ray meets the wall first
		double dhi = std::sin(std::atan(99.0 / (d / c30 - node))), dlo = std::sin(std::atan(99.0 / (1.2 * r2 * d + node)));
		for (int k : {1, 7}) {
			INFO("diagonal " << k << " node " << j);
			REQUIRE(S(m, 128, j, k) <= dhi + LSB);
			REQUIRE(S(m, 128, j, k) >= dlo - LSB);
		}
		REQUIRE(std::fabs(S(m, 128, j, 3)) < LSB);
		REQUIRE(std::fabs(S(m, 128, j, 5)) < LSB);
	}
	REQUIRE(S(m, 128, 199, 0) > 0.9); // right at the step
	REQUIRE(S(m, 128, 220, 0) < LSB); // on top of it, flat further east
}

TEST_CASE("Horizon: a hill is seen the same from both sides", "[horizon]")
{
	Input in;
	in.R = R;
	in.owner = MakeGrid(12, 0.0, SPAN, 0.0, SPAN, [](double lat, double lng) {
		double dy = (lat - SPAN / 2) * R, dx = (lng - SPAN / 2) * R; // near the equator: cos lat ~ 1
		return 300.0 * std::exp(-(dx * dx + dy * dy) / (2.0 * 600.0 * 600.0));
	});
	Map m;
	Compute(in, m);
	for (int off : {16, 40, 80}) {
		REQUIRE(std::fabs(S(m, 128, 128 - off, 0) - S(m, 128, 128 + off, 4)) <= 2 * LSB);
		REQUIRE(std::fabs(S(m, 128 - off, 128, 2) - S(m, 128 + off, 128, 6)) <= 2 * LSB);
		REQUIRE(S(m, 128, 128 - off, 0) > 0.05); // looking at the hill
		REQUIRE(S(m, 128, 128 - off, 4) < S(m, 128, 128 - off, 0));
	}
}

TEST_CASE("Horizon: the finest grid wins", "[horizon]")
{
	Input in;
	in.R = R;
	in.dmax = 30e3;
	in.owner = MakeGrid(12, 0.0, SPAN, 0.0, SPAN, [](double, double) { return 0.0; });
	in.grids.push_back(MakeGrid(12, SPAN, 2 * SPAN, 0.0, SPAN, [](double, double) { return 0.0; })); // flat neighbour north
	in.grids.push_back(MakeGrid(9, -4 * SPAN, 4 * SPAN, -4 * SPAN, 4 * SPAN, [](double lat, double) { return lat > SPAN ? 300.0 : 0.0; }));
	Map m;
	Compute(in, m);
	const double span = R * SPAN;
	double s = S(m, 256, 128, 2); // north edge, looking north: the plateau begins one tile away
	REQUIRE(s < std::sin(std::atan(299.0 / (0.8 * span))) + LSB);
	REQUIRE(s > std::sin(std::atan(299.0 / (1.3 * span))) - LSB);
}

TEST_CASE("Horizon: across the longitude seam", "[horizon]")
{
	const double PI = 3.14159265358979323846;
	Input in;
	in.R = R;
	in.dmax = 30e3;
	in.owner = MakeGrid(12, 0.0, SPAN, PI - SPAN, PI, [](double, double) { return 0.0; });
	in.grids.push_back(MakeGrid(12, 0.0, SPAN, -PI, -PI + SPAN, [](double, double) { return 200.0; })); // east of the seam
	Map m;
	Compute(in, m);
	REQUIRE(S(m, 128, 256, 0) > 0.9);  // the cliff right at the seam
	REQUIRE(S(m, 128, 128, 0) > 0.04); // seen from the middle, 3.2 km off
	REQUIRE(std::fabs(S(m, 128, 128, 4)) < LSB);
}

TEST_CASE("Horizon: Sample reads both halves", "[horizon]")
{
	Map m;
	m.px.assign(size_t(W) * N * 4, 0);
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++)
			for (int c = 0; c < 4; c++) {
				m.px[(size_t(y) * W + x) * 4 + c] = uint8_t(10 + c);
				m.px[(size_t(y) * W + N + x) * 4 + c] = uint8_t(20 + c);
			}
	m.px[(size_t(0) * W + 0) * 4 + 0] = 200; // north-west corner, azimuth 0
	uint8_t v[8];
	Sample(m, 0.5, 0.5, v);
	for (int k = 0; k < 8; k++) REQUIRE(v[k] == (k < 4 ? 10 + k : 20 + k - 4));
	Sample(m, 0.0, 0.0, v);
	REQUIRE(v[0] == 200);
	Sample(m, 1.0, 1.0, v);
	REQUIRE(v[0] == 10);
}

TEST_CASE("Horizon: the planet's curve lowers a far ridge", "[horizon]")
{
	Input in;
	in.R = R;
	in.dmax = 200e3;
	const double D = 150e3, edge = SPAN / 2 + D / R;
	in.owner = MakeGrid(12, 0.0, SPAN, 0.0, SPAN, [](double, double) { return 0.0; });
	in.grids.push_back(MakeGrid(6, -64 * SPAN, 64 * SPAN, -64 * SPAN, 64 * SPAN, [edge](double, double lng) { return lng >= edge ? 3000.0 : 0.0; }));
	Map m;
	Compute(in, m);
	const double cnode = R * 128.0 * SPAN / 256.0; // the coarse grid's node, its ramp
	double s = S(m, 128, 128, 0);
	double hi = std::sin(std::atan((2999.0 - (D - cnode) * (D - cnode) / (2.0 * R)) / (D - cnode)));
	double flat = std::sin(std::atan(2999.0 / (1.2 * D))); // what a flat planet would give
	INFO("s " << s << " hi " << hi << " flat " << flat);
	REQUIRE(s <= hi + LSB);
	REQUIRE(s > -LSB);
	REQUIRE(s < flat - LSB);
	REQUIRE(std::fabs(S(m, 128, 128, 4)) < LSB);
}

TEST_CASE("Horizon: the near and far split follows a plain march", "[horizon]")
{
	const double span = 4.9e3 / R;
	auto f = [](double lat, double lng) { return 600.0 + 300.0 * std::sin(lat * 6e3) * std::cos(lng * 5e3) + 80.0 * std::sin(lat * 3e4 - lng * 2e4); };
	Input in;
	in.R = R;
	in.dmax = DmaxFor(span * R);
	in.owner = MakeGrid(12, 0.0, span, 0.0, span, f);
	for (int a = -1; a <= 1; a++)
		for (int b = -1; b <= 1; b++)
			if (a || b) in.grids.push_back(MakeGrid(12, a * span, (a + 1) * span, b * span, (b + 1) * span, f));
	in.grids.push_back(MakeGrid(9, -8 * span, 8 * span, -8 * span, 8 * span, f));
	Map m;
	Compute(in, m);
	double sum = 0.0, worst = 0.0, bias = 0.0, sumF = 0.0, worstF = 0.0;
	int n = 0, nF = 0;
	for (int i = 4; i < N; i += 20) // far field nodes and the up-sampled ones between
		for (int j = 4; j < N; j += 20)
			for (int k = 0; k < 8; k++) {
				double d = S(m, i, j, k) - March(in, i, j, k);
				sum += std::fabs(d);
				bias += d;
				worst = std::max(worst, std::fabs(d));
				n++;
				if (i % FAR == 0 && j % FAR == 0) { sumF += std::fabs(d); worstF = std::max(worstF, std::fabs(d)); nF++; }
			}
	WARN("split vs march: mean " << sum / n << " bias " << bias / n << " worst " << worst << " over " << n << "; far nodes mean " << (nF ? sumF / nF : 0.0) << " worst " << worstF << " over " << nF);
	REQUIRE(sum / n < 1.5 * LSB);   // measured 0.7 LSB
	REQUIRE(std::fabs(bias / n) < LSB);
	REQUIRE(worst < 0.04);          // measured 0.023: the 1.2 step growth misses the odd peak
}

TEST_CASE("Horizon: time for an L16 owner with neighbours", "[horizon]")
{
	const double span = 4.9e3 / R; // file level 16
	auto f = [](double lat, double lng) { return 800.0 + 400.0 * std::sin(lat * 9e3) * std::cos(lng * 7e3) + 60.0 * std::sin(lat * 7e4 + lng * 5e4); };
	Input in;
	in.R = R;
	in.dmax = DmaxFor(span * R);
	in.owner = MakeGrid(12, 0.0, span, 0.0, span, f);
	for (int a = -1; a <= 1; a++)
		for (int b = -1; b <= 1; b++)
			if (a || b) in.grids.push_back(MakeGrid(12, a * span, (a + 1) * span, b * span, (b + 1) * span, f));
	for (int a = -1; a <= 1; a++)
		for (int b = -1; b <= 1; b++)
			in.grids.push_back(MakeGrid(9, a * 8 * span, (a + 1) * 8 * span, b * 8 * span, (b + 1) * 8 * span, f));
	in.grids.push_back(MakeGrid(6, -32 * span, 32 * span, -32 * span, 32 * span, f));
	Map m;
	auto t0 = std::chrono::steady_clock::now();
	Compute(in, m);
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	WARN("Horizon::Compute L16 owner: " << ms << " ms");
	REQUIRE(m.px.size() == size_t(W) * N * 4);
}
