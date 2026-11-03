// GPSNav tests: the OpenStreetMap map's water tiles
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Map.h"
#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace gps;
using namespace gps::map;
using Catch::Matchers::WithinAbs;

namespace {

using Ring = std::vector<std::pair<int, int>>;

// a protobuf writer for hand-made tiles
struct Pbw {
	std::string b;
	void Var (uint64_t v) { while (v >= 0x80) b += (char)((v & 0x7F) | 0x80), v >>= 7; b += (char)v; }
	void Key (int f, int wt) { Var ((uint64_t)f << 3 | wt); }
	void Bytes (int f, const std::string &s) { Key (f, 2), Var (s.size ()), b += s; }
	void UInt (int f, uint64_t v) { Key (f, 0), Var (v); }
};

uint32_t Zig (int v) { return (uint32_t)((v << 1) ^ (v >> 31)); }

std::string Geom (const std::vector<Ring> &rings)
{
	Pbw g;
	int cx = 0, cy = 0;
	auto To = [&] (std::pair<int, int> p) { g.Var (Zig (p.first - cx)), g.Var (Zig (p.second - cy)), cx = p.first, cy = p.second; };
	for (const Ring &r : rings) {
		g.Var (1 | 1 << 3), To (r[0]);
		g.Var (2 | (uint64_t)(r.size () - 1) << 3);
		for (size_t i = 1; i < r.size (); i++) To (r[i]);
		g.Var (7 | 1 << 3);
	}
	return g.b;
}

std::string Feat (int type, const std::string &geom)
{
	Pbw f;
	f.UInt (1, 7);
	f.Bytes (2, std::string ("\x00\x00", 2)); // tags: key 0, value 0
	f.UInt (3, type);
	f.Bytes (4, geom);
	return f.b;
}

std::string Layer (const char *name, const std::vector<std::string> &feats, int extent = 4096)
{
	Pbw l;
	l.UInt (15, 2);
	l.Bytes (1, name);
	for (auto &f : feats) l.Bytes (2, f);
	l.Bytes (3, "class");
	Pbw v;
	v.Bytes (1, "ocean");
	l.Bytes (4, v.b);
	l.UInt (5, extent);
	return l.b;
}

std::string Tile (const std::vector<std::string> &layers)
{
	Pbw t;
	for (auto &l : layers) t.Bytes (3, l);
	return t.b;
}

Ring Box (int x0, int y0, int x1, int y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }

WaterTile Built (const std::vector<std::vector<Ring>> &features, int z = 10, int x = 300, int y = 400)
{
	std::vector<Feature> f;
	for (auto &rings : features) {
		Feature p;
		for (auto &r : rings) {
			auto &o = p.rings.emplace_back ();
			for (auto &q : r) o.push_back (q.first), o.push_back (q.second);
		}
		f.push_back (p);
	}
	WaterTile t;
	t.Build (z, x, y, f, 4096);
	return t;
}

} // namespace

TEST_CASE("a vector tile's water layer is found and decoded", "[map]")
{
	// a lake with an island, a second lake past the tile's left edge, and a line (skipped)
	std::string lake = Feat (3, Geom ({Box (100, 200, 3000, 3100), Box (1000, 1100, 1500, 1600)}));
	std::string west = Feat (3, Geom ({Box (-80, 50, 40, 90)}));
	Pbw line;
	line.Var (1 | 1 << 3), line.Var (Zig (5)), line.Var (Zig (5)), line.Var (2 | 1 << 3), line.Var (Zig (10)), line.Var (Zig (0));
	std::string tile = Tile ({Layer ("landcover", {Feat (3, Geom ({Box (0, 0, 10, 10)}))}), Layer ("water", {lake, Feat (2, line.b), west}, 8192)});
	std::string layer;
	REQUIRE(WaterLayer ((const uint8_t *)tile.data (), tile.size (), layer));
	REQUIRE(layer == Layer ("water", {lake, Feat (2, line.b), west}, 8192));
	std::vector<Feature> w;
	uint32_t extent = 0;
	REQUIRE(DecodeLayer ((const uint8_t *)layer.data (), layer.size (), w, extent));
	REQUIRE(extent == 8192);
	REQUIRE(w.size () == 2);
	REQUIRE(w[0].rings.size () == 2);
	REQUIRE(w[0].rings[0] == std::vector<int32_t> {100, 200, 3000, 200, 3000, 3100, 100, 3100});
	REQUIRE(w[0].rings[1] == std::vector<int32_t> {1000, 1100, 1500, 1100, 1500, 1600, 1000, 1600});
	REQUIRE(w[1].rings[0] == std::vector<int32_t> {-80, 50, 40, 50, 40, 90, -80, 90});

	// no water layer: land; an empty tile too
	std::string dry = Tile ({Layer ("transportation", {})});
	REQUIRE(WaterLayer ((const uint8_t *)dry.data (), dry.size (), layer));
	REQUIRE(layer.empty ());
	REQUIRE(WaterLayer ((const uint8_t *)"", 0, layer));
	REQUIRE(layer.empty ());
	REQUIRE(DecodeLayer ((const uint8_t *)"", 0, w, extent));
	REQUIRE(w.empty ());
	REQUIRE(extent == 4096);

	// malformed: a cut tile, a cut layer, a LineTo before a MoveTo, an open ring, no extent
	REQUIRE_FALSE(WaterLayer ((const uint8_t *)tile.data (), tile.size () - 7, layer));
	std::string whole = Layer ("water", {lake});
	REQUIRE_FALSE(DecodeLayer ((const uint8_t *)whole.data (), whole.size () / 2, w, extent));
	Pbw bad;
	bad.Var (2 | 1 << 3), bad.Var (Zig (1)), bad.Var (Zig (1));
	std::string l1 = Layer ("water", {Feat (3, bad.b)});
	REQUIRE_FALSE(DecodeLayer ((const uint8_t *)l1.data (), l1.size (), w, extent));
	std::string open = Geom ({Box (0, 0, 100, 100)});
	open.pop_back (); // its ClosePath
	std::string l2 = Layer ("water", {Feat (3, open)});
	REQUIRE_FALSE(DecodeLayer ((const uint8_t *)l2.data (), l2.size (), w, extent));
	std::string l3 = Layer ("water", {lake}, 0);
	REQUIRE_FALSE(DecodeLayer ((const uint8_t *)l3.data (), l3.size (), w, extent));

	// a ring of two points encloses nothing: left out, the rest kept
	std::string l4 = Layer ("water", {Feat (3, Geom ({{{0, 0}, {10, 10}}, Box (0, 0, 64, 64)}))});
	REQUIRE(DecodeLayer ((const uint8_t *)l4.data (), l4.size (), w, extent));
	REQUIRE(w.size () == 1);
	REQUIRE(w[0].rings.size () == 1);
}

TEST_CASE("each feature fills even-odd on its own, the features joined", "[map]")
{
	// a lake with an island; a pond over part of the island; a lagoon over the lake's edge
	WaterTile t = Built ({{Box (1024, 1024, 3072, 3072), Box (1536, 1536, 2560, 2560)}, {Box (2048, 2048, 2304, 2304)}, {Box (2900, 1200, 3300, 1400)}});
	auto W = [&t] (double x, double y) { return t.Water (x / 4096.0, y / 4096.0); };
	REQUIRE(W (1200, 1200));        // the lake
	REQUIRE_FALSE(W (1700, 1700));  // the island
	REQUIRE(W (2100, 2100));        // the pond on it
	REQUIRE(W (2950, 1300));        // lake and lagoon
	REQUIRE(W (3200, 1300));        // the lagoon past the lake
	REQUIRE_FALSE(W (500, 500));    // land
	REQUIRE_FALSE(W (3500, 3500));

	// texels by their centres: x 1024 to 3072 is texels 128 to 383
	REQUIRE_FALSE(t.Bit (127, 200));
	REQUIRE(t.Bit (128, 200));
	REQUIRE(t.Bit (383, 200));
	REQUIRE_FALSE(t.Bit (384, 200));

	// a tile all water, its buffer past the edges
	WaterTile sea = Built ({{Box (-128, -128, 4224, 4224)}});
	for (int y : {0, 255, 511})
		for (int x : {0, 255, 511}) REQUIRE(sea.Bit (x, y));
	REQUIRE(sea.shore.empty ());
	REQUIRE(sea.Water (0.0, 0.0));
	REQUIRE(sea.Water (1.0, 1.0)); // clamped to the last texel
}

TEST_CASE("shores: not the tile's cut, not between two water features", "[map]")
{
	// the sea west of x 2048, cut at the tile's buffer: one shore, down the middle
	WaterTile t = Built ({{Box (-64, -64, 2048, 4160)}});
	REQUIRE(t.mid.size () == 2);
	REQUIRE(t.shore.size () == 6);
	REQUIRE_THAT(t.mid[0], WithinAbs (0.5, 1e-6));
	REQUIRE_THAT(t.mid[1], WithinAbs (0.5, 1e-6));
	double a[3], b[3]; // its ends, either way round
	Local::Unit (TileLatLon (10, 300.5, 400.0), a);
	Local::Unit (TileLatLon (10, 300.5, 401.0), b);
	bool ab = std::fabs (t.shore[0] - a[0]) + std::fabs (t.shore[1] - a[1]) + std::fabs (t.shore[2] - a[2]) < 1e-6;
	const double *s0 = ab ? a : b, *s1 = ab ? b : a;
	for (int i = 0; i < 3; i++) {
		REQUIRE_THAT(t.shore[i], WithinAbs (s0[i], 1e-6));
		REQUIRE_THAT(t.shore[3 + i], WithinAbs (s1[i], 1e-6));
	}

	// cut on the tile's own edges: only the shore at x 2048
	t = Built ({{Box (0, 0, 2048, 4096)}});
	REQUIRE(t.mid.size () == 2);
	REQUIRE_THAT(t.mid[0], WithinAbs (0.5, 1e-6));

	// the sea, and a river against it at x 2048: the shared edge is water both sides, only the river's far bank is a shore
	t = Built ({{Box (-64, -64, 2048, 4160)}, {Box (2048, -64, 3072, 4160)}});
	REQUIRE(t.mid.size () == 2);
	REQUIRE_THAT(t.mid[0], WithinAbs (0.75, 1e-6));

	// a canal two texels wide keeps both banks; water narrower than a texel has no shore
	t = Built ({{Box (2048, -64, 2064, 4160)}});
	REQUIRE(t.mid.size () == 4);
	t = Built ({{Box (2048, -64, 2054, 4160)}, {Box (1000, 1000, 1006, 1006)}});
	REQUIRE(t.mid.empty ());
	t = Built ({{Box (1000, 1000, 1012, 1012)}});
	REQUIRE(t.mid.size () == 8);

	// a lake in the tile: its four sides; one over the corner: its two sides in the tile
	t = Built ({{Box (1000, 1000, 2000, 2000)}});
	REQUIRE(t.mid.size () == 8);
	t = Built ({{Box (-500, -500, 1000, 1000)}});
	REQUIRE(t.mid.size () == 4);
	for (float m : t.mid) REQUIRE((m > 0.0f && m < 0.25f));
}

TEST_CASE("hostile water tiles: small extents, far points, too many points, too much work", "[map]")
{
	std::vector<Feature> w;
	uint32_t extent = 0;
	std::string small = Layer ("water", {Feat (3, Geom ({Box (0, 0, 100, 100)}))}, 255);
	REQUIRE_FALSE(DecodeLayer ((const uint8_t *)small.data (), small.size (), w, extent));

	// points far out: clamped to the tile and a tile each way, the fill as before
	std::string far = Layer ("water", {Feat (3, Geom ({Box (-500000000, -100, 500000000, 5000)}))});
	REQUIRE(DecodeLayer ((const uint8_t *)far.data (), far.size (), w, extent));
	REQUIRE(w.size () == 1);
	REQUIRE(w[0].rings[0] == std::vector<int32_t> {-4096, -100, 8192, -100, 8192, 5000, -4096, 5000});
	WaterTile t;
	REQUIRE(t.Build (10, 300, 400, w, extent));
	REQUIRE(t.Water (0.5, 0.5));
	REQUIRE(t.Water (0.0, 0.99));

	// past 200k points: refused
	Ring big;
	for (int i = 0; i < 200001; i++) big.push_back ({i % 4000, i / 4000});
	std::string many = Layer ("water", {Feat (3, Geom ({big}))});
	REQUIRE_FALSE(DecodeLayer ((const uint8_t *)many.data (), many.size (), w, extent));

	// features that fill the tile over and over: the build gives up, no mask
	std::vector<Feature> fill (200);
	for (Feature &f : fill) f.rings.push_back ({0, 0, 4096, 2048, 0, 4096});
	WaterTile m;
	REQUIRE_FALSE(m.Build (10, 300, 400, fill, 4096));
	REQUIRE(m.bits.empty ());
	REQUIRE_FALSE(m.Water (0.1, 0.5));
	fill.resize (20);
	REQUIRE(m.Build (10, 300, 400, fill, 4096)); // a few: built
	REQUIRE(m.Water (0.1, 0.5));
}

TEST_CASE("Web Mercator tiles", "[map]")
{
	double tx, ty;
	REQUIRE(TileXY ({52.37 * RAD, 4.90 * RAD}, 7, tx, ty)); // Amsterdam
	REQUIRE(((int)tx == 65 && (int)ty == 42));
	REQUIRE(TileXY ({29.6454 * RAD, -95.2789 * RAD}, 12, tx, ty)); // Hobby
	REQUIRE_THAT(tx, WithinAbs (963.9378, 1e-3));
	REQUIRE_THAT(ty, WithinAbs (1694.5585, 1e-3));
	REQUIRE(TileXY ({-33.9399 * RAD, 151.1753 * RAD}, 14, tx, ty)); // Sydney
	REQUIRE(((int)tx == 15072 && (int)ty == 9835));
	REQUIRE(TileXY ({0.0, PI}, 3, tx, ty)); // the date line: the west edge
	REQUIRE_THAT(tx, WithinAbs (0.0, 1e-9));
	REQUIRE_FALSE(TileXY ({86.0 * RAD, 0.0}, 5, tx, ty));
	REQUIRE_FALSE(TileXY ({0.0, 0.0}, 15, tx, ty));

	std::mt19937 rng (7);
	std::uniform_real_distribution<double> la (-MaxLat, MaxLat), lo (-PI, PI);
	for (int i = 0; i < 1000; i++) {
		LatLon p {la (rng), lo (rng)};
		int z = i % 15;
		REQUIRE(TileXY (p, z, tx, ty));
		REQUIRE((tx >= 0.0 && ty >= 0.0 && tx < (1 << z) && ty < (1 << z)));
		LatLon q = TileLatLon (z, tx, ty);
		REQUIRE(Angle (p, q) < 1e-9);
	}
	uint64_t k = Pack (14, 16383, 9835);
	REQUIRE((KeyZ (k) == 14 && KeyX (k) == 16383 && KeyY (k) == 9835));
}
