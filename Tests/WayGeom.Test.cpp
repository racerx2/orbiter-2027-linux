// not upstream (design-ways): streamed ways: the core's decoder and files (TileStreamMvt.cpp) and the client's raster (WayGeom.cpp)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TileStream.h"
#include "WayGeom.h"
#include <cmath>
#include <cstring>
#include <set>

using Catch::Matchers::WithinAbs;

namespace {

// a minimal MVT encoder: lines (2) and polygons (3), tags as strings or "i:<n>" integers
struct Enc {
	struct F { int type; std::vector<std::vector<std::pair<int, int>>> parts; std::vector<std::pair<std::string, std::string>> tags; };
	std::string out;
	static void Var (std::string &s, uint64_t v) { do { uint8_t b = v & 0x7F; v >>= 7; s.push_back ((char)(v ? b | 0x80 : b)); } while (v); }
	static void Key (std::string &s, int f, int wt) { Var (s, (uint64_t)f << 3 | wt); }
	static void Bytes (std::string &s, int f, const std::string &b) { Key (s, f, 2); Var (s, b.size()); s += b; }
	static uint32_t Zz (int32_t v) { return (uint32_t)((v << 1) ^ (v >> 31)); }
	void Layer (const std::string &name, uint32_t extent, const std::vector<F> &fs)
	{
		std::string l;
		Bytes (l, 1, name);
		std::vector<std::string> kn, vn;
		for (const F &f : fs) {
			std::string g, t, ft;
			for (const auto &kv : f.tags) {
				kn.push_back (kv.first);
				vn.push_back (kv.second);
				Var (t, kn.size() - 1);
				Var (t, vn.size() - 1);
			}
			int px = 0, py = 0;
			auto to = [&] (int x, int y) { Var (g, Zz (x - px)); Var (g, Zz (y - py)); px = x; py = y; };
			for (const auto &p : f.parts) {
				Var (g, 1 | 1 << 3); to (p[0].first, p[0].second);
				Var (g, 2 | (uint32_t)(p.size() - 1) << 3);
				for (size_t i = 1; i < p.size(); i++) to (p[i].first, p[i].second);
				if (f.type == 3) Var (g, 7 | 1 << 3);
			}
			Bytes (ft, 2, t);
			Key (ft, 3, 0); Var (ft, f.type);
			Bytes (ft, 4, g);
			Bytes (l, 2, ft);
		}
		for (const std::string &k : kn) Bytes (l, 3, k);
		for (const std::string &v : vn) {
			std::string val;
			if (v.compare (0, 2, "i:") == 0) { Key (val, 4, 0); Var (val, (uint64_t)atoll (v.c_str() + 2)); }
			else Bytes (val, 1, v);
			Bytes (l, 4, val);
		}
		Key (l, 5, 0); Var (l, extent);
		Bytes (out, 3, l);
	}
};

using Pts = std::vector<std::pair<int, int>>;

// TsWay::Tile to the client's tile, as oapiTileStreamWayLoad and WayGeom::FromApi hand it over
std::shared_ptr<const WayGeom::Tile> Client (const TsWay::Tile &t)
{
	std::vector<TSWAY> f;
	std::vector<DWORD> part;
	std::vector<short> pt;
	for (const TsWay::Feature &F : t.f) {
		TSWAY w {};
		w.kind = F.kind; w.flags = F.flags; w.layer = F.layer;
		F.ref.copy (w.ref, sizeof w.ref - 1);
		w.p0 = (DWORD)part.size(); w.np = (DWORD)F.parts.size();
		for (const auto &p : F.parts) { part.push_back ((DWORD)(pt.size() / 2)); pt.insert (pt.end(), p.begin(), p.end()); }
		f.push_back (w);
	}
	DWORD np = (DWORD)part.size();
	part.push_back ((DWORD)(pt.size() / 2));
	TSWAYTILE a {t.z, t.x, t.y, (DWORD)f.size(), np, (DWORD)(pt.size() / 2), f.data(), part.data(), pt.data()};
	return WayGeom::FromApi (a);
}

// a client tile straight from features: kind, flags, ref and lines in tile units
struct Way { uint8_t kind, flags; std::string ref; std::vector<Pts> parts; int8_t layer = 0; };
std::shared_ptr<const WayGeom::Tile> Make (int x, int y, const std::vector<Way> &ws)
{
	TsWay::Tile t;
	t.x = x; t.y = y;
	for (const Way &w : ws) {
		TsWay::Feature F;
		F.kind = w.kind; F.flags = w.flags; F.ref = w.ref; F.layer = w.layer;
		for (const Pts &p : w.parts) {
			std::vector<int16_t> q;
			for (const auto &c : p) { q.push_back ((int16_t)c.first); q.push_back ((int16_t)c.second); }
			F.parts.push_back (q);
		}
		t.f.push_back (F);
	}
	return Client (t);
}

const int TX = 3855, TY = 6778; // z14 over Houston Hobby

// the box of a tile's square of tile units [u0, u1] x [v0, v1]
WayGeom::Input Box (const WayGeom::Tile &T, double u0, double v0, double u1, double v1, int n = 512, int nl = 256)
{
	WayGeom::Input in;
	in.lng0 = T.Lng (u0); in.lng1 = T.Lng (u1);
	in.lat1 = T.Lat (v0); in.lat0 = T.Lat (v1);
	in.n = n; in.nl = nl;
	in.americas = true;
	return in;
}

float Px (const WayGeom::Output &o, int n, int i, int j, int c) { return o.way[0][4 * (j * n + i) + c] / 255.0f; }

}

TEST_CASE ("Ways: the three layers decode, kept and dropped kinds, flags, refs and scaled extents", "[ways]")
{
	Enc e;
	e.Layer ("building", 4096, {{3, {{{0, 0}, {10, 0}, {10, 10}}}, {}}});
	e.Layer ("transportation", 4096, {
		{2, {{{0, 100}, {4096, 100}}}, {{"class", "motorway"}, {"oneway", "i:1"}}},
		{2, {{{0, 200}, {2000, 200}}, {{2000, 300}, {4000, 300}}}, {{"class", "primary"}, {"brunnel", "bridge"}, {"layer", "i:2"}}},
		{2, {{{0, 400}, {100, 400}}}, {{"class", "service"}, {"subclass", "driveway"}}},
		{2, {{{0, 500}, {100, 500}}}, {{"class", "minor"}, {"brunnel", "tunnel"}}},
		{2, {{{0, 600}, {100, 600}}}, {{"class", "ferry"}}},
		{3, {{{100, 700}, {200, 700}, {200, 800}, {100, 800}}}, {{"class", "path"}, {"subclass", "pedestrian"}}},
		{2, {{{0, 900}, {100, 900}}}, {{"class", "rail"}, {"subclass", "tram"}}},
		{3, {{{100, 1000}, {200, 1000}, {200, 1100}}}, {{"class", "rail"}}},
		{2, {{{0, 1200}, {100, 1200}}}, {{"class", "minor"}, {"indoor", "i:1"}}},
		{2, {{{0, 1300}, {100, 1300}}}, {{"class", "track"}, {"surface", "unpaved"}, {"ramp", "i:1"}}},
		{2, {{{0, 1400}, {100, 1400}}}, {{"class", "path"}, {"subclass", "corridor"}}}});
	e.Layer ("aeroway", 512, {
		{3, {{{10, 10}, {60, 10}, {60, 20}, {10, 20}}}, {{"class", "runway"}, {"ref", "13L/31R-and-a-long-tail"}}},
		{2, {{{10, 15}, {60, 15}}}, {{"class", "runway"}, {"ref", "13L/31R"}}},
		{3, {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}}, {{"class", "aerodrome"}}},
		{2, {{{0, 50}, {100, 50}}}, {{"class", "apron"}}}});
	e.Layer ("waterway", 4096, {
		{2, {{{0, 2000}, {4096, 2100}}}, {{"class", "river"}}},
		{2, {{{0, 2200}, {100, 2200}}}, {{"class", "stream"}, {"intermittent", "i:1"}}},
		{2, {{{0, 2300}, {100, 2300}}}, {{"class", "canal"}, {"brunnel", "tunnel"}}}});
	TsWay::Tile t;
	REQUIRE (TsWay::Decode (e.out, t));
	REQUIRE (t.f.size() == 9);
	CHECK (t.f[0].kind == TsWay::MOTORWAY);
	CHECK (t.f[0].flags == TsWay::ONEWAY);
	CHECK (t.f[1].kind == TsWay::PRIMARY);
	CHECK (t.f[1].flags == TsWay::BRIDGE);
	CHECK (t.f[1].layer == 2);
	CHECK (t.f[1].parts.size() == 2); // a MultiLineString
	CHECK (t.f[2].kind == TsWay::DRIVEWAY);
	CHECK (t.f[3].kind == TsWay::PEDESTRIAN);
	CHECK (t.f[3].flags == TsWay::AREA);
	CHECK (t.f[4].kind == TsWay::TRAM);
	CHECK (t.f[5].kind == TsWay::TRACK);
	CHECK (t.f[5].flags == (TsWay::UNPAVED | TsWay::RAMP));
	CHECK (t.f[6].kind == TsWay::RUNWAY);
	CHECK (t.f[6].flags == TsWay::AREA);
	CHECK (t.f[6].ref.size() == 15);
	CHECK (t.f[6].parts[0][0] == 80); // 10 of 512 in 4096 units
	CHECK (t.f[6].parts[0][3] == 80);
	CHECK (t.f[7].kind == TsWay::RUNWAY);
	CHECK (t.f[7].ref == "13L/31R");
	CHECK (t.f[8].kind == TsWay::RIVER);
	CHECK_FALSE (TsWay::Decode (e.out.substr (0, e.out.size() - 3), t));
}

TEST_CASE ("Way files round-trip and refuse hostile counts", "[ways]")
{
	TsWay::Tile t;
	t.x = 10; t.y = 20;
	TsWay::Feature a;
	a.kind = TsWay::RUNWAY; a.flags = TsWay::AREA; a.layer = -1; a.ref = "04/22";
	a.parts = {{0, 0, 100, 0, 100, 100}, {10, 10, 20, 10, 20, 20}};
	TsWay::Feature b;
	b.kind = TsWay::RIVER;
	b.parts = {{-200, 4000, 4300, 4100}};
	t.f = {a, b};
	std::string s = TsWay::Encode (t);
	TsWay::Tile r;
	REQUIRE (TsWay::Read (s, r));
	CHECK (r.x == 10);
	CHECK (r.y == 20);
	REQUIRE (r.f.size() == 2);
	CHECK (r.f[0].ref == "04/22");
	CHECK (r.f[0].layer == -1);
	CHECK (r.f[0].parts == a.parts);
	CHECK (r.f[1].parts == b.parts);
	CHECK (TsWay::Encode (r) == s);
	CHECK_FALSE (TsWay::Read (s.substr (0, s.size() - 1), r));
	CHECK_FALSE (TsWay::Read (s + "x", r));
	std::string k = s;
	k[32] = 0; // the first feature's kind
	CHECK_FALSE (TsWay::Read (k, r));
	std::string f = s;
	f[33] = (char)0x80; // an unknown flag
	CHECK_FALSE (TsWay::Read (f, r));
	TsWay::Tile one = t;
	one.f[1].parts = {{0, 0}}; // a line of one point
	CHECK_FALSE (TsWay::Read (TsWay::Encode (one), r));
	TsWay::Tile far = t;
	far.f[1].parts = {{0, 0, 30000, 0}};
	CHECK_FALSE (TsWay::Read (TsWay::Encode (far), r));
	TsWay::Tile empty;
	REQUIRE (TsWay::Read (TsWay::Encode (empty), r));
	CHECK (r.f.empty());
}

TEST_CASE ("Box filter shares: wide and thin bands, dashes", "[ways]")
{
	CHECK (WayGeom::Cov1 (0.0, 5.0, 1.0) == 1.0);
	CHECK (WayGeom::Cov1 (5.0, 5.0, 1.0) == 0.5);
	CHECK (WayGeom::Cov1 (6.0, 5.0, 1.0) == 0.0);
	CHECK_THAT (WayGeom::Cov1 (0.0, 0.075, 1.0), WithinAbs (0.15, 1e-12)); // a 15 cm line keeps its share of a 1 m texel
	double sum = 0.0;
	for (int k = -20; k <= 20; k++) sum += WayGeom::Cov1 (std::fabs (k * 0.37 + 0.11), 0.075, 0.37);
	CHECK_THAT (sum * 0.37, WithinAbs (0.15, 1e-9)); // the energy across, wherever the texels fall
	double d = 0.0;
	for (int k = 0; k < 1200; k++) d += WayGeom::DashCov (k * 0.01, 0.5, 3.0, 12.0);
	CHECK_THAT (d / 1200.0, WithinAbs (0.25, 0.01));
	CHECK (WayGeom::DashCov (1.5, 0.0, 3.0, 12.0) == 1.0);
	CHECK (WayGeom::DashCov (6.0, 0.0, 3.0, 12.0) == 0.0);
}

TEST_CASE ("Runway designators by the landing bearing", "[ways]")
{
	CHECK (WayGeom::Designator ("13L/31R", 128.0) == "13L");
	CHECK (WayGeom::Designator ("13L/31R", 308.0) == "31R");
	CHECK (WayGeom::Designator ("04/22", 221.0) == "22");
	CHECK (WayGeom::Designator ("09", 92.0) == "09");
	CHECK (WayGeom::Designator ("09", 272.0) == "27");
	CHECK (WayGeom::Designator ("18L", 3.0) == "36R");
	CHECK (WayGeom::Designator ("", 90.0).empty());
	CHECK (WayGeom::Designator ("H1", 90.0).empty());
}

TEST_CASE ("A road paints its width, keeps its row order and fades thin lines by their share", "[ways]")
{
	// an east-west two-way primary road at v = 1024 across the tile's west half
	auto T = Make (TX, TY, {{TSWAY_PRIMARY, 0, "", {{{0, 1024}, {4096, 1024}}}}, {TSWAY_FOOTWAY, 0, "", {{{0, 3072}, {4096, 3072}}}}});
	WayGeom::Input in = Box (*T, 0, 0, 2048, 2048); // the north-west quarter: the road at its middle row
	in.tiles = {T};
	WayGeom::Output o;
	WayGeom::Raster (in, o);
	REQUIRE (o.any);
	REQUIRE (o.way.size() == 10);
	REQUIRE (o.lamp.size() == 9);
	double H = (in.lat1 - in.lat0) * in.R, py = H / 512.0;
	double y = (T->Lat (1024) - 0.5 * (in.lat0 + in.lat1)) * in.R; // the road's y in the box frame
	int jr = (int)std::floor ((0.5 * H - y) / py);
	CHECK (std::abs (jr - 256) <= 1);
	double sum = 0.0;
	for (int j = 0; j < 512; j++) sum += Px (o, 512, 100, j, 0);
	CHECK_THAT (sum * py, WithinAbs (15.0, 0.1)); // 15 m wide, a texel's rounding
	CHECK (Px (o, 512, 100, jr, 0) > 0.99f);
	CHECK (Px (o, 512, 100, 10, 0) == 0.0f); // the north edge: nothing
	float lum = Px (o, 512, 100, jr, 1) / Px (o, 512, 100, jr, 0);
	CHECK (lum > 0.25f); // asphalt
	CHECK (lum < 0.45f);
	// the footway, 2 m wide, at the south-west quarter's middle: a thin share, its energy kept
	WayGeom::Input s = Box (*T, 0, 2048, 2048, 4096);
	s.tiles = {T};
	WayGeom::Output so;
	WayGeom::Raster (s, so);
	REQUIRE (so.any);
	double fs = 0.0, spy = (s.lat1 - s.lat0) * s.R / 512.0;
	for (int j = 0; j < 512; j++) fs += Px (so, 512, 300, j, 0);
	CHECK_THAT (fs * spy, WithinAbs (2.0, 0.05));
	// mip 1 is the mean of its 2 x 2 at level 0, premultiplied
	int i = 50, j = jr / 2;
	double m = 0.0;
	for (int b = 0; b < 2; b++) for (int a = 0; a < 2; a++) m += o.way[0][4 * ((2 * j + b) * 512 + 2 * i + a) + 1];
	CHECK (std::abs (o.way[1][4 * (j * 256 + i) + 1] - m / 4.0) <= 1.0);
}

TEST_CASE ("Nothing painted where there is nothing, and lamps only along lit roads", "[ways]")
{
	auto T = Make (TX, TY, {});
	WayGeom::Input in = Box (*T, 0, 0, 1024, 1024);
	in.tiles = {T};
	WayGeom::Output o;
	WayGeom::Raster (in, o);
	CHECK_FALSE (o.any);
	CHECK (o.way.empty());
	auto L = Make (TX, TY, {{TSWAY_PRIMARY, 0, "", {{{0, 512}, {1024, 512}}}}, {TSWAY_TRACK, 0, "", {{{0, 900}, {1024, 900}}}}});
	in.tiles = {L};
	WayGeom::Raster (in, o);
	REQUIRE (o.any);
	double W = (in.lng1 - in.lng0) * in.R * std::cos (0.5 * (in.lat0 + in.lat1)), pa = W / 256.0, pb = (in.lat1 - in.lat0) * in.R / 256.0;
	auto lv = [&] (int i, int j, int c) { double q = o.lamp[0][4 * (j * 256 + i) + c] / 255.0; return q * q * 4.0; };
	double pool = 0.0, track = 0.0;
	for (int j = 0; j < 256; j++)
		for (int i = 0; i < 256; i++) {
			double v = lv (i, j, 0) * pa * pb;
			if (j < 180) pool += v; else track += v;
		}
	double lamps = W / 35.0; // one every 35 m along the road
	CHECK_THAT (pool / (3.14159265 * 8.96 * 8.96), WithinAbs (lamps, 0.12 * lamps)); // each pool's energy, pi s^2
	CHECK (track < 1e-3);
}

TEST_CASE ("Runway pieces across tiles join; markings at its true ends; the next tile is asked for a cut end", "[ways]")
{
	// a runway east-west from u = 3000 in tile x to u = 1500 in tile x + 1: each piece runs into the other's buffer
	auto A = Make (TX, TY, {{TSWAY_RUNWAY, 0, "09/27", {{{3000, 2048}, {4160, 2048}}}}});
	auto B = Make (TX + 1, TY, {{TSWAY_RUNWAY, 0, "09/27", {{{-64, 2048}, {1500, 2048}}}}});
	auto need = WayGeom::RunwayTiles ({A});
	REQUIRE (need.size() == 1);
	CHECK (need[0] == std::make_pair (TX + 1, TY));
	CHECK (WayGeom::RunwayTiles ({A, B}).empty());
	// rastered over its west threshold, 0.26 m texels
	WayGeom::Input in = Box (*A, 2900, 1920, 3156, 2176);
	in.tiles = {A, B};
	WayGeom::Output o;
	WayGeom::Raster (in, o);
	REQUIRE (o.any);
	double cl = std::cos (0.5 * (in.lat0 + in.lat1)), px = (in.lng1 - in.lng0) * in.R * cl / 512.0, py = (in.lat1 - in.lat0) * in.R / 512.0;
	double len = (A->Lng (4096) - A->Lng (3000) + B->Lng (1500) - B->Lng (0)) * in.R * cl;
	CHECK (len > 900.0);
	CHECK (len < 1500.0);
	int i0 = (int)std::lround ((3000.0 - 2900.0) / 256.0 * 512.0), jc = 256;
	double sum = 0.0;
	for (int j = 0; j < 512; j++) sum += Px (o, 512, i0 + 300, j, 0);
	CHECK_THAT (sum * py, WithinAbs (23.0, 0.3)); // no runway area, 900 to 1500 m long: 23 m wide, one runway's worth
	CHECK (Px (o, 512, i0 - 20, jc, 0) == 0.0f); // nothing before its threshold
	// 25 m in: a threshold stripe 2.625 m off the centre line is paint, the gap at 4.375 m is concrete
	int it = i0 + (int)std::lround (25.0 / px);
	auto lum = [&] (double s) { int j = jc - (int)std::lround (s / py); return Px (o, 512, it, j, 1) / std::max (1e-3f, Px (o, 512, it, j, 0)); };
	CHECK (lum (2.625) > 0.9f);
	CHECK (lum (4.375) < 0.85f);
	CHECK (lum (-2.625) > 0.9f);
	auto shr = [&] (double s) { int j = jc - (int)std::lround (s / py); return o.lamp[0][4 * ((j / 2) * 256 + it / 2) + 3] / 255.0; };
	CHECK (shr (2.625) < 0.05); // paint takes none of the imagery
	CHECK (shr (4.375) > 0.2);  // concrete keeps its share
}

TEST_CASE ("A runway way without a ref joins the ref'd one along the longer axis: one threshold, at the true end", "[ways]")
{
	// 13R/31L as two OSM ways: a long one with the ref and a short end piece without, turned 0.6 deg by the tile grid
	auto T = Make (TX, TY, {{TSWAY_RUNWAY, 0, "09/27", {{{1000, 2048}, {3000, 2048}}}}, {TSWAY_RUNWAY, 0, "", {{{3000, 2048}, {3200, 2050}}}}});
	WayGeom::Input in = Box (*T, 2900, 1848, 3300, 2248);
	in.tiles = {T};
	WayGeom::Output o;
	WayGeom::Raster (in, o);
	REQUIRE (o.any);
	double cl = std::cos (0.5 * (in.lat0 + in.lat1)), px = (in.lng1 - in.lng0) * in.R * cl / 512.0, py = (in.lat1 - in.lat0) * in.R / 512.0;
	double upx = 400.0 / 512.0; // tile units a texel
	auto lum = [&] (double u, double s) {
		int i = (int)std::lround ((u - 2900.0) / upx), j = 256 - (int)std::lround (s / py);
		return Px (o, 512, i, j, 1) / std::max (1e-3f, Px (o, 512, i, j, 0));
	};
	double m25 = 25.0 / px * upx; // 25 m in tile units
	CHECK (lum (3200.0 - m25, 1.6) > 0.9f);  // a threshold stripe 25 m in from the true end (the joined axis lies ~1 m south of the middle row)
	CHECK (lum (3000.0 - m25, 1.6) < 0.85f); // none 25 m from where the two ways meet
}

TEST_CASE ("A base keeps OSM pavement off it, and the airport part it touches out whole", "[ways]")
{
	auto T = Make (TX, TY, {{TSWAY_PRIMARY, 0, "", {{{0, 1024}, {2048, 1024}}}}, {TSWAY_TAXIWAY, 0, "", {{{0, 1500}, {2048, 1500}}}}});
	WayGeom::Input in = Box (*T, 0, 0, 2048, 2048);
	in.tiles = {T};
	double lngm = 0.5 * (in.lng0 + in.lng1);
	in.blocked = [lngm] (double, double lng) { return lng < lngm; }; // the west half is a base
	WayGeom::Output o;
	WayGeom::Raster (in, o);
	REQUIRE (o.any);
	int jr = 256, jt = (int)std::lround ((1500.0 / 2048.0) * 512.0);
	CHECK (Px (o, 512, 50, jr, 0) == 0.0f);   // the road under the base: none
	CHECK (Px (o, 512, 450, jr, 0) > 0.99f);  // and beside it
	CHECK (Px (o, 512, 450, jt, 0) == 0.0f);  // the taxiway touches the base: left out whole
}

TEST_CASE ("Crossing roads: no lines across the junction", "[ways]")
{
	auto T = Make (TX, TY, {{TSWAY_SECONDARY, 0, "", {{{0, 1024}, {2048, 1024}}}}, {TSWAY_SECONDARY, 0, "", {{{1024, 0}, {1024, 2048}}}}});
	WayGeom::Input in = Box (*T, 768, 768, 1280, 1280); // ~265 m, ~0.5 m texels
	in.tiles = {T};
	WayGeom::Output o;
	WayGeom::Raster (in, o);
	REQUIRE (o.any);
	auto yel = [&] (int i, int j) { return Px (o, 512, i, j, 2); };
	double yJ = 0.0, yR = 0.0;
	for (int d = -3; d <= 3; d++) { yJ = std::max (yJ, (double)yel (256 + d, 256)); yR = std::max (yR, (double)yel (60 + d, 256)); }
	CHECK (yR > 0.1); // the double yellow centre line along the road
	CHECK (yJ < 0.02); // none in the junction
}

TEST_CASE ("The tree index covers way surfaces and areas, not the ground beside them", "[ways]")
{
	auto T = Make (TX, TY, {{TSWAY_PRIMARY, 0, "", {{{0, 1024}, {4096, 1024}}}}});
	TsWay::Tile a;
	a.x = TX; a.y = TY;
	TsWay::Feature ap;
	ap.kind = TsWay::APRON; ap.flags = TsWay::AREA;
	ap.parts = {{2000, 2000, 2400, 2000, 2400, 2400, 2000, 2400}};
	a.f = {ap};
	auto A = Client (a);
	WayGeom::Index ix;
	ix.Build ({T, A}, T->Lat (4096), T->Lat (0), T->Lng (0), T->Lng (4096), 6371010.0);
	double lng = T->Lng (1000), lat = T->Lat (1024), dlat = 1.0 / 6371010.0;
	CHECK (ix.Covers (lat, lng, 2.0));
	CHECK (ix.Covers (lat + 9.0 * dlat, lng, 2.0));   // 7.5 m half width and 2 m
	CHECK_FALSE (ix.Covers (lat + 11.0 * dlat, lng, 2.0));
	CHECK (ix.Covers (T->Lat (2200), T->Lng (2200), 0.0)); // inside the apron
	CHECK_FALSE (ix.Covers (T->Lat (3000), T->Lng (3000), 2.0));
}

TEST_CASE ("A feature in two tiles' buffers is painted once", "[ways]")
{
	// the same north-south road in tile A (its buffer past u = 4096) and in tile B (its buffer before u = 0)
	auto A = Make (TX, TY, {{TSWAY_MINOR, 0, "", {{{4096 + 20, 0}, {4096 + 20, 4096}}}}, {TSWAY_SECONDARY, 0, "", {{{3800, 2048}, {4160, 2048}}}}});
	auto B = Make (TX + 1, TY, {{TSWAY_MINOR, 0, "", {{{20, 0}, {20, 4096}}}}, {TSWAY_SECONDARY, 0, "", {{{-64, 2048}, {300, 2048}}}}});
	WayGeom::Input in = Box (*A, 3584, 1536, 4096 + 512, 2560);
	in.tiles = {A, B};
	WayGeom::Output both;
	WayGeom::Raster (in, both);
	in.tiles = {B};
	WayGeom::Output one;
	WayGeom::Raster (in, one);
	REQUIRE (both.any);
	REQUIRE (one.any);
	int diff = 0;
	for (int j = 0; j < 512; j++)
		for (int i = 260; i < 512; i++)
			for (int c = 0; c < 4; c++) diff = std::max (diff, std::abs ((int)both.way[0][4 * (j * 512 + i) + c] - (int)one.way[0][4 * (j * 512 + i) + c]));
	CHECK (diff <= 1); // east of the tile edge B alone paints; A's copy in its buffer adds nothing
}
