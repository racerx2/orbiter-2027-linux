// not upstream: volumetric cloud coverage map and noise (OVP/VulkanClient/VolCloudMap.cpp)

#include <catch2/catch_test_macros.hpp>
#include "VolCloudMap.h"
#include <cmath>
#include <cstdlib>

using namespace VolCloud;

namespace {
const double R = 6371e3;
const double PI = 3.14159265358979323846;

Src Tile (int lvl, double minlat, double maxlat, double minlng, double maxlng)
{
	Src s;
	s.lvl = lvl;
	s.minlat = minlat; s.maxlat = maxlat; s.minlng = minlng; s.maxlng = maxlng;
	auto d = std::make_shared<std::vector<uint8_t>>(size_t(DENSN) * DENSN);
	for (int j = 0; j < DENSN; j++)
		for (int i = 0; i < DENSN; i++) (*d)[size_t(j) * DENSN + i] = uint8_t((i * 3 + j * 5 + lvl * 40) & 255);
	s.d = d;
	return s;
}
} // namespace

TEST_CASE("VolCloud: tangent plane round trip", "[volcloud]")
{
	for (double lat : { -1.2, -0.3, 0.0, 0.5, 1.3 })
		for (double lng : { -3.1, -1.0, 0.0, 2.0, 3.13 }) {
			Frame f;
			f.Set(lat, lng);
			for (double x : { -90e3, 0.0, 40e3 })
				for (double y : { -70e3, 0.0, 95e3 }) {
					double la, lo, x2, y2;
					f.FromMap(x, y, R, &la, &lo);
					f.ToMap(la, lo, R, &x2, &y2);
					CHECK(std::fabs(x2 - x) < 1e-3);
					CHECK(std::fabs(y2 - y) < 1e-3);
				}
		}
}

TEST_CASE("VolCloud: east and north on the map", "[volcloud]")
{
	Frame f;
	f.Set(0.4, 1.0);
	double x, y;
	f.ToMap(0.4, 1.0 + 1e-3, R, &x, &y);
	CHECK(x > 0.0);
	CHECK(std::fabs(y) < 0.05 * x);
	f.ToMap(0.4 + 1e-3, 1.0, R, &x, &y);
	CHECK(y > 0.0);
	CHECK(std::fabs(y - R * 1e-3) < 1.0);
}

TEST_CASE("VolCloud: cloud longitude wraps", "[volcloud]")
{
	CHECK(std::fabs(CloudLng(0.5, 0.25) - 0.75) < 1e-12);
	CHECK(std::fabs(CloudLng(3.0, 0.5) - (3.5 - 2.0 * PI)) < 1e-12);
	CHECK(std::fabs(CloudLng(-3.0, -0.5) - (-3.5 + 2.0 * PI)) < 1e-12);
	CHECK(std::fabs(CloudLng(1.0, 4.0 * PI)  - 1.0) < 1e-9);
}

TEST_CASE("VolCloud: rebuild rule", "[volcloud]")
{
	CHECK(NeedRebuild(0, 0, false));
	CHECK_FALSE(NeedRebuild(10e3, 15e3, true));
	CHECK(NeedRebuild(15e3, 15e3, true));
}

TEST_CASE("VolCloud: fill equals direct sampling, finest tile wins", "[volcloud]")
{
	Frame f;
	f.Set(0.3, 0.2);
	double span = PI / 64.0;
	std::vector<Src> srcs;
	srcs.push_back(Tile(6, 0.3 - span, 0.3 + span, 0.2 - span, 0.2 + span));				// coarse, covers all
	srcs.push_back(Tile(7, 0.3, 0.3 + span / 2, 0.2, 0.2 + span / 2));						// finer, north-east
	std::vector<uint8_t> map;
	Fill(f, R, srcs, map);
	REQUIRE(map.size() == size_t(MAPN) * MAPN);
	int checked = 0;
	for (int j = 3; j < MAPN; j += 37)
		for (int i = 5; i < MAPN; i += 41) {
			double step = 2.0 * MAPHALF / MAPN, lat, lng, u, v;
			f.FromMap(-MAPHALF + (i + 0.5) * step, MAPHALF - (j + 0.5) * step, R, &lat, &lng);
			const Src &s = srcs[1].Covers(lat, lng, &u, &v) ? srcs[1] : srcs[0];
			REQUIRE(s.Covers(lat, lng, &u, &v));
			CHECK(std::abs(int(map[size_t(j) * MAPN + i]) - int(std::lround(s.At(u, v) * 255.0f))) <= 0);
			checked++;
		}
	CHECK(checked > 20);
}

TEST_CASE("VolCloud: fill across the date line", "[volcloud]")
{
	Frame f;
	f.Set(0.0, PI - 1e-3);
	double span = PI / 64.0;
	std::vector<Src> srcs;
	srcs.push_back(Tile(6, -span, span, PI - span, PI));
	srcs.push_back(Tile(6, -span, span, -PI, -PI + span));
	std::vector<uint8_t> map;
	Fill(f, R, srcs, map);
	bool anyEast = false;
	for (int i = MAPN / 2 + 10; i < MAPN; i++) if (map[size_t(MAPN / 2) * MAPN + i]) anyEast = true;
	CHECK(anyEast);
}

TEST_CASE("VolCloud: noise tiles and is deterministic", "[volcloud]")
{
	const int n = 16;
	std::vector<uint8_t> a, b;
	ShapeNoise(n, a);
	ShapeNoise(n, b);
	CHECK(a == b);
	// across the wrap the step is like a step inside
	double inner = 0.0, wrap = 0.0;
	for (int k = 0; k < n; k++)
		for (int j = 0; j < n; j++) {
			auto at = [&](int i, int c) { return int(a[((size_t(k) * n + j) * n + i) * 4 + c]); };
			for (int c = 0; c < 4; c++) {
				inner += std::abs(at(1, c) - at(0, c));
				wrap += std::abs(at(0, c) - at(n - 1, c));
			}
		}
	CHECK(wrap < 2.0 * inner);
	int lo = 255, hi = 0;
	for (size_t i = 0; i < a.size(); i += 4) { lo = std::min(lo, int(a[i])); hi = std::max(hi, int(a[i])); }
	CHECK(hi - lo > 100);
	std::vector<uint8_t> d;
	DetailNoise(8, d);
	CHECK(d.size() == size_t(8 * 8 * 8 * 4));
	CHECK(d[3] == 255);
}
