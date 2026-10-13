// not upstream: tree placement and meshes (OVP/VulkanClient/TreeGeom.cpp)

#include <catch2/catch_test_macros.hpp>
#include "TreeGeom.h"
#include <cmath>
#include <map>
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
