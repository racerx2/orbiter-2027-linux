// not upstream: tree placement and meshes (OVP/VulkanClient/TreeGeom.cpp)

#include <catch2/catch_test_macros.hpp>
#include "TreeGeom.h"
#include <cmath>
#include <map>
#include <set>
#include <tuple>

using namespace TreeGeom;

namespace {
const double R = 6371e3;
const double PI = 3.14159265358979323846;

ClassFn AllTrees () { return [](double, double, Class *c) { c->r = 1.0f; c->g = 0.0f; c->b = 0.0f; c->water = false; return true; }; }

size_t Count (double lat, double side, const ClassFn &cls, const BlockFn &blocked = nullptr, std::vector<Tree> *out = nullptr)
{
	Params p;
	p.R = R;
	double dlat = side / R, dlng = side / (R * std::cos(lat));
	std::vector<Tree> t;
	Place(lat, lat + dlat, 0.3, 0.3 + dlng, p, cls, nullptr, blocked, t);
	if (out) *out = t;
	return t.size();
}
} // namespace

TEST_CASE("Trees: deterministic", "[trees]")
{
	std::vector<Tree> a, b;
	Count(0.8, 300.0, AllTrees(), nullptr, &a);
	Count(0.8, 300.0, AllTrees(), nullptr, &b);
	REQUIRE(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		CHECK(a[i].lat == b[i].lat);
		CHECK(a[i].lng == b[i].lng);
		CHECK(a[i].h == b[i].h);
		CHECK(a[i].kind == b[i].kind);
	}
}

TEST_CASE("Trees: density per hectare where the class is all trees", "[trees]")
{
	for (double latd : { 0.0, 48.0, 65.0 }) {
		double lat = latd * PI / 180.0;
		size_t n = Count(lat, 700.0, AllTrees());
		double perHa = n / 49.0;
		INFO("lat " << latd << " trees/ha " << perHa);
		CHECK(perHa > 100.0);
		CHECK(perHa < 160.0);
	}
}

TEST_CASE("Trees: none on water, bare or built-up ground, black class or blocked spots", "[trees]")
{
	CHECK(Count(0.8, 300.0, [](double, double, Class *c) { c->r = 1; c->water = true; return true; }) == 0);
	CHECK(Count(0.8, 300.0, [](double, double, Class *c) { c->r = 1; c->b = 0.4f; return true; }) == 0);
	CHECK(Count(0.8, 300.0, [](double, double, Class *c) { c->r = 0.02f; c->g = 0.02f; c->b = 0.02f; return true; }) == 0);
	CHECK(Count(0.8, 300.0, [](double, double, Class *) { return false; }) == 0);
	size_t all = Count(0.8, 300.0, AllTrees());
	size_t half = Count(0.8, 300.0, AllTrees(), [](double, double lng) { return lng < 0.3 + 150.0 / (R * std::cos(0.8)); });
	CHECK(all > 100);
	CHECK(half > all / 3);
	CHECK(half < all * 2 / 3);
}

TEST_CASE("Trees: shrub land gets bushes, fewer", "[trees]")
{
	std::vector<Tree> t;
	size_t n = Count(0.8, 500.0, [](double, double, Class *c) { c->r = 110 / 255.0f; c->g = 145 / 255.0f; return true; }, nullptr, &t);
	size_t trees = Count(0.8, 500.0, AllTrees());
	CHECK(n > 0);
	CHECK(n < trees / 2);
	for (const Tree &x : t) { CHECK(x.kind == BUSH); CHECK(x.h <= 3.0f); }
}

TEST_CASE("Trees: kinds by latitude", "[trees]")
{
	auto Conifers = [](double latd) {
		std::vector<Tree> t;
		Count(latd * PI / 180.0, 500.0, AllTrees(), nullptr, &t);
		size_t c = 0;
		for (const Tree &x : t) if (x.kind == SPRUCE || x.kind == PINE) c++;
		return double(c) / std::max<size_t>(1, t.size());
	};
	CHECK(Conifers(62.0) > 0.75);
	CHECK(Conifers(10.0) < 0.2);
	double mid = Conifers(45.0);
	CHECK(mid > 0.35);
	CHECK(mid < 0.65);
}

TEST_CASE("Trees: the lattice is the terrain shader's", "[trees]")
{
	double c = LatticeC(R);
	uint32_t s = 12345;
	auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / double(1 << 24); };
	for (int k = 0; k < 200; k++) {
		int L = 13 + int(rnd() * 7.0), lvl = L - 4;
		double height = PI / double(1 << lvl);
		double rep = std::exp2(std::round(std::log2(std::max(1.0, height * R / 8.0))));
		double lat = (rnd() * 2.0 - 1.0) * 1.2, lng = (rnd() * 2.0 - 1.0) * PI * 0.999;
		int ilat = int((0.5 * PI - lat) / height), ilng = int((lng + PI) / height);
		double maxlat = 0.5 * PI - ilat * height, minlng = -PI + ilng * height;
		double u = (lng - minlng) / height, v = (maxlat - lat) / height;
		double sx = std::fmod(u * rep + std::fmod(ilng * rep, P), P), sy = std::fmod(v * rep + std::fmod(ilat * rep, P), P);
		double px, py;
		Lattice(lat, lng, c, &px, &py);
		double dx = std::fabs(px - sx), dy = std::fabs(py - sy);
		dx = std::min(dx, P - dx); dy = std::min(dy, P - dy);
		INFO("L" << L << " lat " << lat << " lng " << lng);
		CHECK(dx < 1e-3);
		CHECK(dy < 1e-3);
	}
}

TEST_CASE("Trees: meshes closed, normals out, triangle budget", "[trees]")
{
	for (int kind = 0; kind < NKIND; kind++) {
		std::vector<Vtx> v;
		std::vector<uint16_t> ix;
		Mesh(kind, v, ix);
		INFO("kind " << kind);
		size_t tris = ix.size() / 3;
		CHECK(tris >= 100);
		CHECK(tris <= 300);
		std::map<std::tuple<long, long, long>, int> weld;
		auto W = [&](uint16_t i) {
			auto key = std::make_tuple(std::lround(v[i].x * 1e5), std::lround(v[i].y * 1e5), std::lround(v[i].z * 1e5));
			auto it = weld.find(key);
			if (it != weld.end()) return it->second;
			int id = int(weld.size());
			weld[key] = id;
			return id;
		};
		std::map<std::pair<int, int>, int> edges;
		bool out = true;
		for (size_t t = 0; t < tris; t++) {
			const Vtx &a = v[ix[3 * t]], &b = v[ix[3 * t + 1]], &c = v[ix[3 * t + 2]];
			double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, wx = c.x - a.x, wy = c.y - a.y, wz = c.z - a.z;
			double cx = uy * wz - uz * wy, cy = uz * wx - ux * wz, cz = ux * wy - uy * wx;
			double nx = a.nx + b.nx + c.nx, ny = a.ny + b.ny + c.ny, nz = a.nz + b.nz + c.nz;
			if (cx * nx + cy * ny + cz * nz < 0.0) out = false;
			int w[3] = { W(ix[3 * t]), W(ix[3 * t + 1]), W(ix[3 * t + 2]) };
			for (int e = 0; e < 3; e++) {
				int p = w[e], q = w[(e + 1) % 3];
				if (p == q) continue;
				edges[{ std::min(p, q), std::max(p, q) }]++;
			}
		}
		CHECK(out);
		bool closed = true;
		for (auto &e : edges) if (e.second != 2) closed = false;
		CHECK(closed);
	}
	std::vector<Vtx> v;
	std::vector<uint16_t> ix;
	Impostor(v, ix);
	CHECK(v.size() == 4);
	CHECK(ix.size() == 6);
}

TEST_CASE("Trees: the cell ground is bilinear, its slope the plane's", "[trees]")
{
	const double lat0 = 0.8, span = 300.0 / R;
	Ground g;
	g.n = 17; g.R = R;
	g.lat0 = lat0; g.lat1 = lat0 + span; g.lng0 = 0.3; g.lng1 = 0.3 + span / std::cos(lat0 + span / 2);
	auto plane = [&](double lat, double lng) { return 100.0 + 0.5 * (lng - g.lng0) * R * std::cos(lat) + 0.8 * (lat - lat0) * R; };
	g.h.resize(17 * 17);
	for (int y = 0; y < 17; y++)
		for (int x = 0; x < 17; x++)
			g.h[y * 17 + x] = plane(g.lat0 + (g.lat1 - g.lat0) * y / 16, g.lng0 + (g.lng1 - g.lng0) * x / 16);
	for (int k = 0; k < 50; k++) {
		double fy = (k * 0.37 - std::floor(k * 0.37)), fx = (k * 0.61 - std::floor(k * 0.61));
		double lat = g.lat0 + fy * (g.lat1 - g.lat0), lng = g.lng0 + fx * (g.lng1 - g.lng0), s;
		double h = g.At(lat, lng, &s);
		CHECK(std::fabs(h - plane(lat, lng)) < 0.05);
		CHECK(std::fabs(s - std::hypot(0.5, 0.8)) < 0.01);
	}
	Ground steep = g;
	for (int y = 0; y < 17; y++)
		for (int x = 0; x < 17; x++) steep.h[y * 17 + x] = (g.lat0 + (g.lat1 - g.lat0) * y / 16 - lat0) * R * std::tan(45.0 * PI / 180.0);
	double s;
	steep.At(lat0 + span / 2, g.lng0 + 0.001 * span, &s);
	CHECK(s > std::tan(40.0 * PI / 180.0)); // the worker's slope limit
	CHECK(Ground().At(lat0, 0.3, &s) == 0.0);
}

TEST_CASE("Trees: footprints block inside and within the margin, not past it", "[trees]")
{
	std::vector<std::array<double, 2>> box = { { -10, -5 }, { 10, -5 }, { 10, 5 }, { -10, 5 } };
	CHECK(InRing(box, 0, 0, 2.0));
	CHECK(InRing(box, 9.9, 4.9, 0.0));
	CHECK(InRing(box, 11.5, 0, 2.0));
	CHECK_FALSE(InRing(box, 12.5, 0, 2.0));
	CHECK_FALSE(InRing(box, 0, 7.5, 2.0));
	CHECK(InRing(box, 11.0, 6.0, 1.5)); // the corner, 1.41 m off
	std::vector<std::array<double, 2>> ell = { { 0, 0 }, { 20, 0 }, { 20, 10 }, { 10, 10 }, { 10, 20 }, { 0, 20 } };
	CHECK(InRing(ell, 5, 15, 0.0));
	CHECK_FALSE(InRing(ell, 16, 16, 2.0)); // the notch
	CHECK(InRing(ell, 11, 11, 2.0));
	CHECK_FALSE(InRing({ { 0, 0 }, { 1, 1 } }, 0.5, 0.5, 1.0));
}

// not upstream (design-far-forest) ---------------------------------------------

TEST_CASE("Far forest: a quarter of the candidates at level 1 or more, a sixteenth at 2", "[trees]")
{
	long long n[MaxLevel + 2] = {};
	for (int64_t Y = 0; Y < 1024; Y++)
		for (int64_t X = 0; X < 1024; X++) n[Level(X, Y)]++;
	long long above = 0;
	for (int l = MaxLevel; l >= 1; l--) {
		above += n[l];
		INFO("level " << l);
		CHECK(above == (1024LL * 1024) >> (2 * l));
	}
	CHECK(Level(0, 0) == MaxLevel);
	CHECK(Level(1 << 12, 3 << 12) == MaxLevel);
	CHECK(Level(12, 8) == 2);
}

TEST_CASE("Far forest: the drawn density is 4^-lambda through the bands", "[trees]")
{
	for (double lam = 0.0; lam <= 6.0; lam += 0.05) {
		double drawn = 0.0;
		for (int l = 0; l <= MaxLevel; l++) {
			double frac = l < MaxLevel ? std::pow(4.0, -l) - std::pow(4.0, -l - 1) : std::pow(4.0, -l);
			drawn += frac * DrawShare(l, lam);
		}
		INFO("lambda " << lam);
		CHECK(std::fabs(drawn / std::pow(4.0, -lam) - 1.0) < 1e-9);
	}
}

TEST_CASE("Far forest: a level's trees are level 0's, same places, kinds and heights", "[trees]")
{
	Params p;
	p.R = R;
	double lat = 0.5, dlat = 2000.0 / R, dlng = 2000.0 / (R * std::cos(lat));
	std::vector<Tree> all, l2;
	Place(lat, lat + dlat, 0.3, 0.3 + dlng, p, AllTrees(), nullptr, nullptr, all, 0);
	Place(lat, lat + dlat, 0.3, 0.3 + dlng, p, AllTrees(), nullptr, nullptr, l2, 2);
	std::map<std::pair<double, double>, Tree> by;
	size_t want = 0, ge1 = 0;
	for (const Tree &t : all) {
		if (t.lvl >= 2) { by[{ t.lat, t.lng }] = t; want++; }
		if (t.lvl >= 1) ge1++;
	}
	REQUIRE(all.size() > 30000);
	CHECK(std::fabs(double(ge1) / all.size() - 0.25) < 0.01);
	CHECK(std::fabs(double(want) / all.size() - 0.0625) < 0.005);
	CHECK(l2.size() == want);
	for (const Tree &t : l2) {
		auto it = by.find({ t.lat, t.lng });
		REQUIRE(it != by.end());
		CHECK(t.lvl >= 2);
		CHECK(it->second.h == t.h);
		CHECK(it->second.kind == t.kind);
		CHECK(it->second.yaw == t.yaw);
	}
}

TEST_CASE("Far forest: the shader's random number keeps the share", "[trees]")
{
	Params p;
	p.R = R;
	double lat = 0.5, dlat = 3000.0 / R, dlng = 3000.0 / (R * std::cos(lat));
	std::vector<Tree> all;
	Place(lat, lat + dlat, 0.3, 0.3 + dlng, p, AllTrees(), nullptr, nullptr, all, 0);
	for (double lam : { 0.0, 0.5, 1.0, 1.5, 2.25 }) {
		double kept = 0.0, expect = 0.0;
		for (const Tree &t : all) {
			double share = DrawShare(t.lvl, lam), rnd = uint8_t(t.yaw / (2.0 * PI) * 255.0) / 255.0; // TreeMgr's yaw byte, Tree.glsl's iCol.x
			expect += share;
			if (rnd < share) kept++;
		}
		INFO("lambda " << lam);
		CHECK(std::fabs(kept / std::max(1.0, expect) - 1.0) < 0.03);
		CHECK(std::fabs(expect / (all.size() * std::pow(4.0, -lam)) - 1.0) < 0.03);
	}
}

TEST_CASE("Far forest: the cells to the far limit cover it once, coarser farther", "[trees]")
{
	const double lat = 28.6 * PI / 180.0, lng = -80.69 * PI / 180.0, D0 = 2500.0, far = 120e3;
	std::vector<Quad::Want> w;
	Quad::Select(lat, lng, 0.0, D0, far, R, w);
	INFO("cells " << w.size());
	CHECK(w.size() > 3000);
	CHECK(w.size() < 6000);
	int lv[MaxLevel + 1] = {};
	for (size_t i = 0; i < w.size(); i++) {
		int m = Quad::Lvl(w[i].key);
		lv[m]++;
		if (i) CHECK(w[i].d >= w[i - 1].d);
		CHECK(w[i].d <= far);
		if (m > 0) CHECK(w[i].d >= D0 * std::exp2(m));
		CHECK(Quad::Near(w[i].key, lat, lng, R) == w[i].d);
	}
	CHECK(lv[0] > 800);
	CHECK(lv[0] < 1500);
	std::set<long long> keys;
	for (auto &q : w) keys.insert(q.key);
	CHECK(keys.size() == w.size());
	for (auto &q : w) for (long long a = Quad::Parent(q.key); a >= 0; a = Quad::Parent(a)) CHECK(!keys.count(a));
	uint32_t s = 777;
	auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / double(1 << 24); };
	for (int k = 0; k < 2000; k++) {
		double r = far * 0.98 * std::sqrt(rnd()), az = 2.0 * PI * rnd();
		double la = lat + r * std::cos(az) / R, lo = lng + r * std::sin(az) / (R * std::cos(lat));
		int in = 0;
		for (auto &q : w) {
			double a0, a1, o0, o1;
			Quad::Bounds(q.key, a0, a1, o0, o1);
			if (la >= a0 && la < a1 && lo >= o0 && lo < o1) in++;
		}
		CHECK(in == 1);
	}
	std::vector<Quad::Want> hi;
	Quad::Select(lat, lng, 5100.0, D0, far, R, hi); // half a 10.2 km height: every tree beyond 2 D0, no level 0 cells
	for (auto &q : hi) CHECK(Quad::Lvl(q.key) >= 1);
}

TEST_CASE("Far forest: a parent's children tile it", "[trees]")
{
	long long k = Quad::Key(3, 1234, 5678), c[4];
	Quad::Children(k, c);
	double a0, a1, o0, o1;
	Quad::Bounds(k, a0, a1, o0, o1);
	double area = 0.0;
	for (long long q : c) {
		CHECK(Quad::Parent(q) == k);
		double b0, b1, p0, p1;
		Quad::Bounds(q, b0, b1, p0, p1);
		CHECK(b0 >= a0 - 1e-12); CHECK(b1 <= a1 + 1e-12); CHECK(p0 >= o0 - 1e-12); CHECK(p1 <= o1 + 1e-12);
		area += (b1 - b0) * (p1 - p0);
	}
	CHECK(std::fabs(area - (a1 - a0) * (o1 - o0)) < 1e-15);
	CHECK(Quad::Parent(Quad::Key(MaxLevel, 1, 1)) == -1);
	double l0, l1, g0, g1;
	Quad::Bounds(Quad::Key(0, 5 * 16 + 3, 7 * 16 + 9), l0, l1, g0, g1); // level 0: an L16 tile's cell as before
	CHECK(std::fabs(l1 - (0.5 * PI - (5.0 * 16 + 3) * PI / 65536.0)) < 1e-12);
	CHECK(std::fabs(g0 - (-PI + (7.0 * 16 + 9) * PI / 65536.0)) < 1e-12);
}

TEST_CASE("Far forest: a parent drawn until its children are placed, children until their parent is", "[trees]")
{
	long long P = Quad::Key(2, 100, 200), c[4];
	Quad::Children(P, c);
	std::map<long long, int> st;
	auto S = [&](long long k) { auto it = st.find(k); return it == st.end() ? 0 : it->second; };
	std::vector<long long> want(c, c + 4), out;
	st = { { P, 2 }, { c[0], 2 }, { c[1], 2 }, { c[2], 1 }, { c[3], 1 } };
	Quad::DrawSet(want, S, out);
	CHECK(out == std::vector<long long>{ P });
	st = { { P, 2 }, { c[0], 2 }, { c[1], 2 }, { c[2], 2 }, { c[3], 2 } };
	Quad::DrawSet(want, S, out);
	CHECK(out.size() == 4);
	for (long long k : out) CHECK(k != P);
	st = { { P, 1 }, { c[0], 2 }, { c[1], 2 }, { c[2], 0 }, { c[3], 2 } };
	Quad::DrawSet({ P }, S, out);
	CHECK(out.size() == 3);
	st = { { P, 2 } };
	Quad::DrawSet({ P }, S, out);
	CHECK(out == std::vector<long long>{ P });
	long long G = Quad::Parent(P);
	st = { { G, 2 }, { c[0], 2 } };
	Quad::DrawSet(want, S, out);
	CHECK(out == std::vector<long long>{ G });
}
