
// not upstream: tile stream decoders on hostile input: TIFF, .elv, MVT, FLOOR boxes, the date line, odd orbits

#include <catch2/catch_test_macros.hpp>
#include "TileStream.h"
#include <QColor>
#include <QCoreApplication>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <zlib.h>

namespace fs = std::filesystem;

namespace {

struct Tmp {
	fs::path root;
	Tmp ()
	{
		static int n = 0;
		root = fs::temp_directory_path() / ("tilestream-decode-" + std::to_string (getpid()) + "-" + std::to_string (n++));
		fs::remove_all (root);
		fs::create_directories (root);
	}
	~Tmp () { std::error_code ec; fs::remove_all (root, ec); }
};

double Secs (std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(); }

void Put (const fs::path &p, const std::string &s)
{
	fs::create_directories (p.parent_path());
	std::ofstream (p, std::ios::binary) << s;
}

// a classic little-endian TIFF with one IFD of inline values (cog.cpp)
struct E { uint16_t tag, type; uint32_t count, val; };
QByteArray Tiff (const std::vector<E> &es)
{
	QByteArray b (8 + 2 + 12 * (int)es.size() + 4, 0);
	auto p16 = [&b] (int at, uint32_t v) { b[at] = (char)(v & 255); b[at + 1] = (char)((v >> 8) & 255); };
	auto p32 = [&b] (int at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = (char)((v >> (8 * i)) & 255); };
	b[0] = 'I'; b[1] = 'I'; p16 (2, 42); p32 (4, 8);
	p16 (8, (uint16_t)es.size());
	for (size_t i = 0; i < es.size(); i++) {
		int at = 10 + 12 * (int)i;
		p16 (at, es[i].tag); p16 (at + 2, es[i].type); p32 (at + 4, es[i].count);
		if (es[i].type == 3 && es[i].count == 1) p16 (at + 8, es[i].val); else p32 (at + 8, es[i].val);
	}
	return b;
}

// a 16 x 16 deflate level with one tile: the fields a test changes
std::vector<E> Level (uint32_t w = 16, uint32_t tw = 16, uint32_t comp = 8, uint32_t pred = 1, uint32_t cnt = 10)
{
	return {{256, 4, 1, w}, {257, 4, 1, w}, {258, 3, 1, 8}, {259, 3, 1, comp}, {277, 3, 1, 1}, {317, 3, 1, pred}, {322, 4, 1, tw}, {323, 4, 1, tw}, {324, 4, 1, 200}, {325, 4, 1, cnt}};
}

QByteArray Deflate (const std::string &raw)
{
	uLongf n = compressBound ((uLong)raw.size());
	std::string c (n, '\0');
	REQUIRE (compress ((Bytef*)&c[0], &n, (const Bytef*)raw.data(), (uLong)raw.size()) == Z_OK);
	return QByteArray (c.data(), (qsizetype)n);
}

// WorldCover classes over one L12 tile (one 3 degree cell) from a fake host serving this header; max: the longest range asked
bool ClassesOf (const QByteArray &tif, uint64_t &max, int &reads)
{
	max = 0;
	reads = 0;
	auto fetch = [&] (std::vector<TsCog::Req> &rq) {
		for (auto &q : rq) {
			reads++;
			max = std::max (max, q.len);
			q.status = 200;
			q.data = std::make_shared<QByteArray> (q.off == 0 ? tif : QByteArray ((qsizetype)std::min<uint64_t> (q.len, 1 << 16), 0));
		}
	};
	std::vector<uint8_t> out;
	return TsCog::Classes ("x/{tile}.tif", 12, 80, 200, 64, fetch, out);
}

}

TEST_CASE ("TIFF levels past the limits are refused before any allocation", "[decode]")
{
	std::vector<TsCog::Ifd> ifd;
	uint64_t max;
	int reads;
	REQUIRE (TsCog::Parse (Tiff (Level ()), ifd) == 0); // the baseline parses
	CHECK (ifd[0].off.size() == 1);
	QByteArray wrap = Tiff ({{256, 4, 1, 0xFFFFFFFFu}, {257, 4, 1, 16}, {258, 3, 1, 8}, {259, 3, 1, 1}, {322, 4, 1, 2}, {323, 4, 1, 16}}); // tiles across wrapped to 0
	CHECK (TsCog::Parse (wrap, ifd) == -1);
	CHECK_FALSE (ClassesOf (wrap, max, reads));
	QByteArray huge = Tiff ({{256, 4, 1, 1u << 20}, {257, 4, 1, 1u << 20}, {258, 3, 1, 8}, {259, 3, 1, 8}, {322, 4, 1, 1u << 20}, {323, 4, 1, 1u << 20}, {324, 4, 1, 0}, {325, 4, 1, 0}});
	CHECK (TsCog::Parse (huge, ifd) == -1); // one sparse 2^20 x 2^20 tile: no bad_alloc
	CHECK_FALSE (ClassesOf (huge, max, reads));
	CHECK (TsCog::Parse (Tiff (Level ((1u << 20) + 16)), ifd) == -1);
	CHECK (TsCog::Parse (Tiff (Level (64, 8)), ifd) == -1);    // not a multiple of 16
	CHECK (TsCog::Parse (Tiff (Level (8192, 4112)), ifd) == -1); // over 4096
	CHECK (TsCog::Parse (Tiff (Level (64, 32)), ifd) == -1);   // 4 tiles, one offset
	for (uint32_t comp : {1u, 5u, 7u, 34887u}) CHECK (TsCog::Parse (Tiff (Level (16, 16, comp)), ifd) == -1);
	CHECK (TsCog::Parse (Tiff (Level (16, 16, 32946)), ifd) == 0);
	CHECK (TsCog::Parse (Tiff (Level (16, 16, 8, 3)), ifd) == -1);
	CHECK (TsCog::Parse (Tiff (Level (16, 16, 8, 2)), ifd) == 0);
	std::vector<E> e = Level ();
	e[4].val = 5; // samples per pixel
	CHECK (TsCog::Parse (Tiff (e), ifd) == -1);
	e = Level ();
	e[2].val = 16; // bits
	CHECK (TsCog::Parse (Tiff (e), ifd) == -1);
}

TEST_CASE ("A TIFF offset table bigger than the bytes in hand asks for more instead of allocating", "[decode]")
{
	std::vector<E> e = Level ();
	e[8] = {324, 4, 1u << 24, 1000};
	auto t0 = std::chrono::steady_clock::now();
	std::vector<TsCog::Ifd> ifd;
	int64_t need = TsCog::Parse (Tiff (e), ifd);
	CHECK (need >= 1000 + 4 * (int64_t)(1 << 24));
	CHECK (Secs (t0) < 0.5);
	uint64_t max;
	int reads;
	CHECK_FALSE (ClassesOf (Tiff (e), max, reads)); // over 8 MB of header: not fetched again
	CHECK (reads == 2); // the tile spans two cells: one header read each
	CHECK (max <= 65536);
}

TEST_CASE ("A TIFF tile body over 8 MB is never asked for", "[decode]")
{
	uint64_t max;
	int reads;
	std::vector<TsCog::Ifd> ifd;
	REQUIRE (TsCog::Parse (Tiff (Level (16, 16, 8, 1, 0x7FFFFFFFu)), ifd) == 0);
	CHECK_FALSE (ClassesOf (Tiff (Level (16, 16, 8, 1, 0x7FFFFFFFu)), max, reads));
	CHECK (reads == 2); // the headers only
	CHECK (max <= 65536);
}

TEST_CASE ("TIFF tiles inflate into exactly their size", "[decode]")
{
	TsCog::Ifd f;
	f.w = f.h = f.tw = f.th = 16;
	f.comp = 8;
	std::string pat (256, '\0');
	for (int i = 0; i < 256; i++) pat[i] = (char)(i * 7);
	std::vector<uint8_t> out;
	REQUIRE (TsCog::Decode (f, Deflate (pat), out));
	CHECK (std::string (out.begin(), out.end()) == pat);
	auto t0 = std::chrono::steady_clock::now();
	QByteArray bomb = Deflate (std::string ((size_t)16 << 20, '\0')); // 16 MB of zeros for a 256 byte tile
	CHECK (TsCog::Decode (f, bomb, out));
	CHECK (out.size() == 256);
	CHECK (Secs (t0) < 3.0);
	CHECK_FALSE (TsCog::Decode (f, Deflate (pat.substr (0, 100)), out)); // short
	CHECK_FALSE (TsCog::Decode (f, Deflate (pat).left (40), out));     // cut off
	CHECK_FALSE (TsCog::Decode (f, QByteArray ("junk junk junk"), out));
	f.comp = 32946;
	CHECK (TsCog::Decode (f, Deflate (pat), out));
	f.comp = 1;
	CHECK_FALSE (TsCog::Decode (f, QByteArray (pat.data(), 256), out)); // deflate only
	f.comp = 8;
	f.pred = 3;
	CHECK_FALSE (TsCog::Decode (f, Deflate (pat), out));
	f.pred = 2;
	std::string d (256, '\0');
	for (int i = 0; i < 256; i++) d[i] = (char)(i % 16 ? 1 : 0);
	REQUIRE (TsCog::Decode (f, Deflate (d), out));
	CHECK (out[15] == 15);
	f.pred = 1;
	f.tw = f.th = 1u << 20;
	CHECK_FALSE (TsCog::Decode (f, QByteArray(), out)); // a sparse tile far too big: refused, not allocated
	f.tw = f.th = 16;
	CHECK_FALSE (TsCog::Decode (f, QByteArray ((qsizetype)(8 << 20) + 1, 'x'), out));
}

namespace {
// an .elv of 259 x 259 nodes all at m metres
std::string Elv (const double b[4], int32_t dt = -16, int32_t nx = 259, double m = 100.0, size_t size = 100 + 259 * 259 * 2)
{
	std::string e (size, '\0');
	int32_t hs = 100;
	double sc = 0.5, of = 0.0;
	e[0] = 'E'; e[1] = 'L'; e[2] = 'E'; e[3] = 1;
	memcpy (&e[4], &hs, 4); memcpy (&e[8], &dt, 4); memcpy (&e[12], &nx, 4); memcpy (&e[16], &nx, 4);
	memcpy (&e[28], &sc, 8); memcpy (&e[36], &of, 8); memcpy (&e[44], b, 32);
	int16_t q = (int16_t)std::lround (m / sc);
	for (size_t at = 100; at + 2 <= size; at += 2) memcpy (&e[at], &q, 2);
	return e;
}
}

TEST_CASE ("ElvAt refuses degenerate, non-finite and short .elv headers", "[decode]")
{
	const double ok[4] = {0.0, 1.0, 0.0, 1.0};
	CHECK (TsSky::ElvAt (Elv (ok), 0.5, 0.5) == 100.0);
	CHECK (TsSky::ElvAt (Elv (ok), 1.0, 1.0) == 100.0); // the far corner: node 257 and its neighbour
	const double flat[4] = {0.5, 0.5, 0.0, 1.0};
	CHECK (std::isnan (TsSky::ElvAt (Elv (flat), 0.5, 0.5))); // latmin == latmax (geo.cpp mode 1)
	for (double bad : {NAN, INFINITY, -INFINITY}) {
		for (int k = 0; k < 4; k++) {
			double b[4] = {0.0, 1.0, 0.0, 1.0};
			b[k] = bad;
			CHECK (std::isnan (TsSky::ElvAt (Elv (b), 0.5, 0.5)));
		}
	}
	const double back[4] = {1.0, 0.0, 0.0, 1.0};
	CHECK (std::isnan (TsSky::ElvAt (Elv (back), 0.5, 0.5)));
	CHECK (std::isnan (TsSky::ElvAt (Elv (ok), NAN, 0.5)));
	CHECK (std::isnan (TsSky::ElvAt (Elv (ok), 0.5, NAN)));
	CHECK (std::isnan (TsSky::ElvAt (Elv (ok, 7), 0.5, 0.5)));  // unknown dtype
	CHECK (std::isnan (TsSky::ElvAt (Elv (ok, -16, 258), 0.5, 0.5)));
	CHECK (std::isnan (TsSky::ElvAt (Elv (ok, -16, 259, 100.0, 1100), 0.5, 0.5))); // short
	CHECK (TsSky::ElvAt (Elv (ok, 0, 259, 0.0, 100), 0.5, 0.5) == 0.0); // header only: its offset
}

namespace {
// square outer ring, tile units, y down (positive area), and k small triangular holes (geo.cpp)
std::vector<std::vector<TsBldg::Pt>> Holed (int k)
{
	std::vector<std::vector<TsBldg::Pt>> rings;
	rings.push_back ({{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}});
	int side = (int)std::ceil (std::sqrt ((double)k));
	for (int i = 0; i < k; i++) {
		double x = 10 + (i % side) * (3900.0 / side), y = 10 + (i / side) * (3900.0 / side);
		rings.push_back ({{x, y}, {x, y + 6}, {x + 6, y}});
	}
	return rings;
}
}

TEST_CASE ("A polygon with 4000 holes triangulates fast with its outer ring kept", "[decode]")
{
	std::vector<std::vector<TsBldg::Pt>> rings = Holed (4000);
	auto t0 = std::chrono::steady_clock::now();
	std::vector<uint32_t> tri;
	CHECK_FALSE (TsBldg::Triangulate (rings, tri)); // holes dropped: false, the roof still made
	CHECK (Secs (t0) < 3.0);
	REQUIRE (tri.size() >= 6);
	uint32_t total = 4 + 4000 * 3;
	bool inRange = true, corners[4] = {false, false, false, false};
	for (uint32_t i : tri) { inRange = inRange && i < total; if (i < 4) corners[i] = true; }
	CHECK (inRange);
	CHECK ((corners[0] && corners[1] && corners[2] && corners[3]));
	t0 = std::chrono::steady_clock::now();
	TsBldg::Feature F;
	F.rings = rings;
	F.height = 10;
	TsBldg::Mesh m = TsBldg::Build (14, 8000, 6000, {F}, 4096);
	CHECK (Secs (t0) < 3.0);
	REQUIRE (m.b.size() == 1);
	CHECK (m.b[0].nv <= 5 * 2000);
	CHECK (m.b[0].nv > 5 * 4); // the outer walls and roof, plus the holes kept
	CHECK (m.b[0].ni > 0);
	std::vector<uint32_t> few;
	CHECK (TsBldg::Triangulate (Holed (10), few)); // a few holes: all bridged
}

TEST_CASE ("A building tile stops at 200k ring vertices", "[decode]")
{
	std::vector<TsBldg::Feature> fs;
	int side = 46; // 2116 polygons of 100 vertices
	for (int i = 0; i < side * side; i++) {
		TsBldg::Feature F;
		double cx = 40 + (i % side) * 88.0, cy = 40 + (i / side) * 88.0;
		std::vector<TsBldg::Pt> r;
		for (int k = 0; k < 100; k++) r.push_back ({cx + 30 * std::cos (k * 2 * M_PI / 100), cy + 30 * std::sin (k * 2 * M_PI / 100)});
		F.rings.push_back (r);
		F.height = 12;
		fs.push_back (F);
	}
	TsBldg::Mesh m = TsBldg::Build (14, 8000, 6000, fs, 4096);
	CHECK (m.b.size() == 2000);
	std::vector<TsBldg::Pt> big;
	for (int k = 0; k < 2001; k++) big.push_back ({2000 + 1500 * std::cos (k * 2 * M_PI / 2001), 2000 + 1500 * std::sin (k * 2 * M_PI / 2001)});
	TsBldg::Feature B;
	B.rings.push_back (big);
	CHECK (TsBldg::Build (14, 8000, 6000, {B}, 4096).b.empty()); // over 2000 outer vertices
}

namespace {
// a minimal MVT encoder: layers of polygon features with numeric or string tags
struct Mvt {
	std::string out;
	static void Var (std::string &s, uint64_t v) { do { uint8_t b = v & 0x7F; v >>= 7; s.push_back ((char)(v ? b | 0x80 : b)); } while (v); }
	static void Key (std::string &s, int f, int wt) { Var (s, (uint64_t)f << 3 | wt); }
	static void Bytes (std::string &s, int f, const std::string &b) { Key (s, f, 2); Var (s, b.size()); s += b; }
	static uint32_t Zz (int32_t v) { return (uint32_t)((v << 1) ^ (v >> 31)); }
	struct Feat { int x0, y0, x1, y1; std::vector<std::pair<std::string, std::string>> tags; }; // value: "d:<double>" or a string
	void Layer (const std::string &name, uint32_t extent, const std::vector<Feat> &fs)
	{
		std::string l, keys, vals;
		Bytes (l, 1, name);
		std::vector<std::string> kn, vn;
		for (const Feat &f : fs) {
			std::string g, t, ft;
			for (const auto &kv : f.tags) {
				kn.push_back (kv.first);
				vn.push_back (kv.second);
				Var (t, kn.size() - 1);
				Var (t, vn.size() - 1);
			}
			int px = 0, py = 0;
			auto to = [&] (int x, int y) { Var (g, Zz (x - px)); Var (g, Zz (y - py)); px = x; py = y; };
			Var (g, 1 | 1 << 3); to (f.x0, f.y0);
			Var (g, 2 | 3 << 3); to (f.x1, f.y0); to (f.x1, f.y1); to (f.x0, f.y1);
			Var (g, 7 | 1 << 3);
			Bytes (ft, 2, t);
			Key (ft, 3, 0); Var (ft, 3);
			Bytes (ft, 4, g);
			Bytes (l, 2, ft);
		}
		for (const std::string &k : kn) Bytes (l, 3, k);
		for (const std::string &v : vn) {
			std::string val;
			if (v.compare (0, 2, "d:") == 0) { double d = strtod (v.c_str() + 2, nullptr); Key (val, 3, 1); val.append ((const char*)&d, 8); }
			else Bytes (val, 1, v);
			Bytes (l, 4, val);
		}
		Key (l, 5, 0); Var (l, extent);
		Bytes (out, 3, l);
	}
};
}

TEST_CASE ("Building heights: non-finite skipped, clamped to 1000 m, min not above top; extent per layer", "[decode]")
{
	Mvt t;
	t.Layer ("building", 4096, {{1000, 1000, 1400, 1400, {{"render_height", "inf"}}},
		{1500, 1000, 1900, 1400, {{"render_height", "d:1e300"}}},
		{2000, 1000, 2400, 1400, {{"render_height", "d:nan"}, {"render_min_height", "nan"}}},
		{2500, 1000, 2900, 1400, {{"render_height", "2000"}, {"render_min_height", "1500"}}}});
	t.Layer ("water", 256, {{0, 0, 10, 10, {}}});
	t.Layer ("building", 512, {{125, 250, 175, 300, {{"render_height", "d:30"}}}}); // a second building layer, coarser units
	std::vector<TsBldg::Feature> f;
	uint32_t ext = 0;
	REQUIRE (TsBldg::Decode (t.out, f, ext));
	REQUIRE (f.size() == 5);
	CHECK (ext == 4096);
	CHECK (f[0].height == 0.0); // "inf": not taken
	CHECK (f[1].height == 1e300);
	CHECK (f[2].height == 0.0);
	CHECK (f[2].minHeight == 0.0);
	CHECK (f[4].rings[0][0].x == 1000.0); // 125 of 512 in 4096 units
	CHECK (f[4].rings[0][0].y == 2000.0);
	CHECK (f[4].rings[0][2].x == 1400.0);
	TsBldg::Mesh m = TsBldg::Build (14, 8000, 6000, f, ext);
	REQUIRE (m.b.size() == 4); // the one with min above its clamped top is dropped
	CHECK (m.b[0].hmax == 5.0f);
	CHECK (m.b[1].hmax == 1000.0f);
	CHECK (m.b[2].hmax == 5.0f);
	CHECK (m.b[3].hmax == 30.0f);
	bool finite = true;
	for (float v : m.vtx) finite = finite && std::isfinite (v);
	CHECK (finite);
	TsBldg::Feature inf;
	inf.rings = f[0].rings;
	inf.height = INFINITY;
	inf.minHeight = NAN;
	TsBldg::Mesh mi = TsBldg::Build (14, 8000, 6000, {inf}, 4096); // geo.cpp mode 5
	REQUIRE (mi.b.size() == 1);
	CHECK (mi.b[0].hmax == 5.0f);
	CHECK (mi.b[0].hmin == 0.0f);
}

TEST_CASE ("An empty building mesh round-trips", "[decode]")
{
	TsBldg::Mesh empty, r;
	empty.z = 14;
	std::string s = TsBldg::Encode (empty);
	CHECK (s.size() == 32);
	REQUIRE (TsBldg::Read (s, r));
	CHECK (r.z == 14);
	CHECK (r.vtx.empty());
	CHECK (r.idx.empty());
	CHECK (r.b.empty());
}

TEST_CASE ("Version 2 building files keep footprints, read back the same walls and refuse hostile counts", "[decode]")
{
	std::vector<TsBldg::Feature> fs (2);
	fs[0].rings = Holed (3);
	fs[0].height = 20;
	fs[1].rings = {{{100, 100}, {300, 100}, {300, 300}, {100, 300}}};
	fs[1].height = 8;
	fs[1].minHeight = 3;
	TsBldg::Mesh m = TsBldg::Build (14, 8000, 6000, fs, 4096);
	REQUIRE (m.b.size() == 2);
	REQUIRE (m.rsz.size() == 5);
	std::string s = TsBldg::Encode (m);
	uint32_t ver = 0;
	memcpy (&ver, s.data() + 4, 4);
	CHECK (ver == 2);
	TsBldg::Mesh r;
	REQUIRE (TsBldg::Read (s, r));
	CHECK (r.vtx == m.vtx); // the same floats: Build extrudes from the points as the file keeps them
	CHECK (r.idx == m.idx);
	CHECK (r.rsz == m.rsz);
	CHECK (r.pts == m.pts);
	for (size_t i = 0; i < 2; i++) {
		CHECK (r.b[i].v0 == m.b[i].v0);
		CHECK (r.b[i].nv == m.b[i].nv);
		CHECK (r.b[i].i0 == m.b[i].i0);
		CHECK (r.b[i].ni == m.b[i].ni);
		CHECK (r.b[i].hmin == m.b[i].hmin);
		CHECK (r.b[i].hmax == m.b[i].hmax);
	}
	CHECK (r.b[1].hmin == 3.0f);
	CHECK (TsBldg::Encode (r) == s);
	TsBldg::Mesh w = m; // no rings, as read from a version 1 file: version 1 again
	w.rsz.clear();
	w.pts.clear();
	for (TsBldg::Building &b : w.b) b.nr = 0;
	std::string s1 = TsBldg::Encode (w);
	memcpy (&ver, s1.data() + 4, 4);
	CHECK (ver == 1);
	REQUIRE (TsBldg::Read (s1, r));
	CHECK (r.vtx == m.vtx);
	CHECK (r.rsz.empty());
	CHECK (TsBldg::Encode (r) == s1);
	CHECK (s.size() * 5 < s1.size());
	const size_t tb = 32, tr = tb + 2 * 26, tp = tr + m.rsz.size() * 2; // building table, ring sizes, points
	auto bad = [] (const std::string &x) { TsBldg::Mesh q; return !TsBldg::Read (x, q); };
	std::string x = s;
	x[tb + 24] = 0; x[tb + 25] = 0;
	CHECK (bad (x)); // a building with no ring
	x = s;
	uint16_t two = 2, five = 5;
	memcpy (&x[tr], &two, 2); memcpy (&x[tr + 2], &five, 2);
	CHECK (bad (x)); // a ring of 2 points, the total kept
	CHECK (bad (s.substr (0, s.size() - 4))); // a point cut
	x = s;
	float v = NAN;
	memcpy (&x[tp], &v, 4);
	CHECK (bad (x));
	x = s;
	v = 5000.0f;
	memcpy (&x[tb + 20], &v, 4);
	CHECK (bad (x)); // taller than 1000 m
	x = s;
	uint32_t big = 0xFFFFFFFFu;
	memcpy (&x[24], &big, 4);
	CHECK (bad (x)); // ring count wraps the size check
	x = s;
	memcpy (&x[28], &big, 4);
	CHECK (bad (x)); // point count too
	x = s;
	x[tb + 24] = 2;
	CHECK (bad (x)); // ring counts no longer add up
	x = s;
	uint16_t many = 2001;
	memcpy (&x[tr], &many, 2);
	CHECK (bad (x)); // a ring past MaxPoly
}

TEST_CASE ("FLOOR boxes parse per body and bad ones are refused", "[decode]")
{
	Tmp t;
	auto cfg = [&] (const std::string &extra) {
		Put (t.root / "ts.cfg", "BODY Earth\nSURF eox u 1 16\nLIGHTS gibs u 7\nWATER gibs u 8\n" + extra + "END\nBODY Moon\nSURF eox u 1 16\nLIGHTS gibs u 7\nWATER gibs u 8\nEND\n");
		std::vector<TsBodyCfg> b;
		std::string err;
		bool ok = TsReadCfg ((t.root / "ts.cfg").string(), b, err);
		return std::make_pair (ok, b);
	};
	auto good = cfg ("FLOOR 35.48 32.69 35.68 32.92 -212 ; Galilee\nFLOOR -117.15 35.85 -116.6 36.65 -86\n");
	REQUIRE (good.first);
	REQUIRE (good.second.size() == 2);
	REQUIRE (good.second[0].floor.size() == 2);
	CHECK (good.second[0].floor[0].lng0 == 35.48);
	CHECK (good.second[0].floor[0].lat1 == 32.92);
	CHECK (good.second[0].floor[0].m == -212.0);
	CHECK (good.second[0].floor[1].m == -86.0);
	CHECK (good.second[1].floor.empty());
	for (const char *bad : {"FLOOR 10 0 5 1 -10\n", "FLOOR 0 1 1 0 -10\n", "FLOOR 0 0 1 1 5\n", "FLOOR 0 0 1 1\n", "FLOOR 0 0 1 1 nan\n", "FLOOR -181 0 1 1 -5\n", "FLOOR 0 0 1 91 -5\n"})
		CHECK_FALSE (cfg (bad).first);
}

namespace {
const int TL = 14, TA = 400, TG = 1000; // a tile near 19.6 N 4.2 W
const double Dh = 180.0 / 1024.0, Tn = 90.0 - TA * Dh, Tw = -180.0 + TG * Dh;

QColor Terr (double m)
{
	double v = m + 32768.0;
	int r = (int)(v / 256.0), g = (int)v % 256, b = (int)std::lround ((v - std::floor (v)) * 256.0);
	return QColor (r, g, b);
}

std::vector<QImage> TerrTiles (const TsImg::Grid &g, const std::function<double (int col)> &m)
{
	std::vector<QImage> v;
	int nt = 1 << g.z;
	for (int r = 0; r < g.nrow; r++)
		for (int c = 0; c < g.ncol; c++) {
			QImage im (256, 256, QImage::Format_RGB888);
			im.fill (Terr (m (((g.col0 + c) % nt + nt) % nt)));
			v.push_back (im);
		}
	return v;
}

int16_t Node (const std::string &e, int i, int j)
{
	int16_t q;
	memcpy (&q, &e[100 + ((size_t)i * 259 + j) * 2], 2);
	return q;
}

int32_t Dtype (const std::string &e) { int32_t d; memcpy (&d, &e[8], 4); return d; }
}

TEST_CASE ("Elev clamps at 0 m except inside FLOOR boxes, first box wins, flat only at 0 everywhere", "[decode]")
{
	TsImg::Grid g = TsImg::TerrGrid (TL, TA, TG, 15);
	std::vector<QImage> low = TerrTiles (g, [] (int) { return -100.0; });
	TsBodyCfg::Floor all {Tw - 1, Tn - Dh - 1, Tw + Dh + 1, Tn + 1, -50.0}, deep = all, west = all;
	deep.m = -200.0;
	west.lng1 = Tw + Dh / 2;
	std::string none = TsImg::Elev (TL, TA, TG, g, low, 0.5);
	CHECK (none.size() == 100); // the sea floor: flat at 0
	CHECK (Dtype (none) == 0);
	std::string in = TsImg::Elev (TL, TA, TG, g, low, 0.5, {all});
	REQUIRE (in.size() == 100 + 259 * 259 * 2);
	CHECK (Dtype (in) == -16);
	CHECK (Node (in, 129, 129) == -100); // -100 m held at the box floor, -50 m
	CHECK (Node (in, 0, 0) == -100);
	double emin;
	memcpy (&emin, &in[76], 8);
	CHECK (emin == -50.0);
	CHECK (Node (TsImg::Elev (TL, TA, TG, g, low, 0.5, {deep}), 129, 129) == -200); // above the floor: as it is
	std::string half = TsImg::Elev (TL, TA, TG, g, low, 0.5, {west});
	CHECK (Node (half, 129, 10) == -100);
	CHECK (Node (half, 129, 250) == 0);
	TsBodyCfg::Floor a = all, b = all;
	a.m = -30.0;
	b.m = -80.0;
	CHECK (Node (TsImg::Elev (TL, TA, TG, g, low, 0.5, {a, b}), 129, 129) == -60);
	CHECK (Node (TsImg::Elev (TL, TA, TG, g, low, 0.5, {b, a}), 129, 129) == -160);
	std::string zero = TsImg::Elev (TL, TA, TG, g, TerrTiles (g, [] (int) { return 0.0; }), 0.5, {all});
	CHECK (zero.size() == 100); // every node exactly 0: flat
	std::string high = TsImg::Elev (TL, TA, TG, g, TerrTiles (g, [] (int) { return 100.0; }), 0.5, {all});
	CHECK (Node (high, 129, 129) == 200);
}

TEST_CASE ("Elev_mod never lifts a node above the Elev tile's floor", "[decode]")
{
	const double R = 6.371e6, d2r = M_PI / 180.0;
	TsBase b;
	b.lat = (Tn - Dh / 2) * d2r; // the tile centre: node (129, 129)
	b.lng = (Tw + Dh / 2) * d2r;
	b.parts.push_back ({0, 0, 0, 0, 100.0});
	b.ext = 100.0;
	const double bounds[4] = {(Tn - Dh) * d2r, Tn * d2r, Tw * d2r, (Tw + Dh) * d2r};
	std::string m = TsSky::ElevMod (TL, TA, TG, {{&b, 0.0}}, R, 0.5, Elv (bounds, -16, 259, -60.0), 150.0);
	REQUIRE (m.size() == 100 + 259 * 259 * 2);
	CHECK (Node (m, 129, 129) == 0);   // the base, flat at 0
	int16_t ring = Node (m, 131, 129); // about 150 m north: in the blend
	CHECK (ring < 0);
	CHECK (ring > -120);
	CHECK (Node (m, 129, 140) == SHRT_MAX);
	std::string up = TsSky::ElevMod (TL, TA, TG, {{&b, 0.0}}, R, 0.5, Elv (bounds, -16, 259, 40.0), 150.0);
	CHECK (Node (up, 131, 129) > 0);
}

TEST_CASE ("Elev padding across the date line comes from the Terrarium tile over the line", "[decode]")
{
	CHECK (TsImg::Url ("t/{z}/{col}/{row}.png", 11, 7, -1) == "t/11/2047/7.png");
	CHECK (TsImg::Url ("t/{z}/{col}/{row}.png", 11, 7, -2048) == "t/11/0/7.png");
	CHECK (TsImg::Url ("t/{z}/{col}/{row}.png", 11, 7, 5) == "t/11/5/7.png");
	TsImg::Grid e = TsImg::TerrGrid (TL, TA, 2047, 15); // east edge: 180 E
	CHECK (e.z == 11);
	CHECK (e.col0 < 0);
	CHECK (e.col0 + e.ncol - 1 == 0); // column 0 over the line
	std::string east = TsImg::Elev (TL, TA, 2047, e, TerrTiles (e, [] (int c) { return c == 0 ? 1000.0 : 10.0; }), 0.5);
	REQUIRE (east.size() == 100 + 259 * 259 * 2);
	CHECK (Node (east, 129, 256) == 20);
	CHECK (Node (east, 129, 257) == 1010); // on the line: half each side
	CHECK (Node (east, 129, 258) == 2000); // the padding node: 1000 m across
	TsImg::Grid w = TsImg::TerrGrid (TL, TA, 0, 15); // west edge: 180 W
	CHECK (w.col0 == -1);
	std::string west = TsImg::Elev (TL, TA, 0, w, TerrTiles (w, [] (int c) { return c == 2047 ? 1000.0 : 10.0; }), 0.5);
	REQUIRE (west.size() == 100 + 259 * 259 * 2);
	CHECK (Node (west, 129, 0) == 2000);
	CHECK (Node (west, 129, 1) == 1010);
	CHECK (Node (west, 129, 2) == 20);
	TsImg::Grid mid = TsImg::TerrGrid (TL, TA, TG, 15);
	CHECK (mid.col0 >= 0);
	CHECK (mid.ncol <= 3);
	TsImg::Grid globe = TsImg::TerrGrid (4, 0, 0, 15); // the whole width already
	CHECK (globe.col0 == 0);
	CHECK (globe.ncol == 2);
}

TEST_CASE ("A base beside the date line touches the tiles across it", "[decode]")
{
	const double R = 6.371e6, d2r = M_PI / 180.0;
	TsBase b;
	b.lat = 0.5 * d2r;
	b.lng = 179.9999 * d2r;
	b.parts.push_back ({0, 0, 0, 0, 100.0});
	b.ext = 100.0;
	int a = (int)((90.0 - 0.5) / Dh);
	CHECK (TsSky::Hits (b, R, TL, a, 2047, 150.0));
	CHECK (TsSky::Hits (b, R, TL, a, 0, 150.0)); // across the line
	CHECK_FALSE (TsSky::Hits (b, R, TL, a, 1, 150.0));
	CHECK_FALSE (TsSky::Hits (b, R, TL, a, 2045, 150.0));
	b.lng = -179.9999 * d2r;
	CHECK (TsSky::Hits (b, R, TL, a, 2047, 150.0));
	CHECK (TsSky::Hits (b, R, 3, 0, 0, 150.0)); // L1-L3: the globe
}

TEST_CASE ("Elev_mod for Elev on disk wraps the date line at stream start", "[decode]")
{
	Tmp t;
	fs::path tex = t.root / "Textures" / "Earth", base = t.root / "Base";
	Put (t.root / "ts.cfg", "BODY Earth\nSURF eox file:///none/{z}/{row}/{col}.jpg 1 16\nLIGHTS gibs file:///none/{n}/{row}/{col}.png 7\n"
		"WATER gibs file:///none/{n}/{row}/{col}.png 8\nELEV terrarium file:///none/{z}/{col}/{row}.png 15 16\nEND\n");
	Put (base / "Edge.cfg", "Location = 179.9999 0.5\nBEGIN_OBJECTLIST\nEND_OBJECTLIST\n");
	Put (tex / "TileStream" / "bases.idx", "Edge 20\n");
	const double span = 180.0 / 128.0, d2r = M_PI / 180.0;
	int a = (int)((90.0 - 0.5) / span);
	char row[16];
	snprintf (row, sizeof row, "%06d", a);
	for (int c : {255, 0}) {
		double w = -180.0 + c * span, n = 90.0 - a * span;
		const double b[4] = {(n - span) * d2r, n * d2r, w * d2r, (w + span) * d2r};
		char col[16];
		snprintf (col, sizeof col, "%06d.elv", c);
		Put (tex / "Elev" / "11" / row / col, Elv (b, -16, 259, 30.0));
	}
	std::vector<std::string> log;
	{
		TileStream::Options o;
		o.cfgfile = (t.root / "ts.cfg").string();
		o.bodyDir = [&] (const std::string &) { return tex.string(); };
		o.baseDir = [&] (const std::string &) { return base.string(); };
		o.radius = [] (const std::string &) { return 6.371e6; };
		o.bodies = {"Earth"};
		o.offline = true;
		o.maxActive = 0;
		o.log = [&] (const std::string &s) { log.push_back (s); };
		TileStream ts (o);
	}
	CHECK (fs::exists (tex / "Elev_mod" / "11" / row / "000255.elv"));
	CHECK (fs::exists (tex / "Elev_mod" / "11" / row / "000000.elv")); // across the line
	bool two = false;
	for (const std::string &s : log) two = two || s.find ("2 Elev_mod tiles made") != std::string::npos;
	CHECK (two);
}

TEST_CASE ("Parabolic and non-finite orbits give finite ground tracks or none", "[decode]")
{
	const double r0[3] = {4194304.0, 0, 0}, mu = 2199023255552.0; // 2^22 m, 2^41: v = 2^10 m/s is exactly escape speed
	for (int k : {0, 2}) {
		double v0[3] = {0, 0, 0};
		v0[k] = 1024.0; // radial, then along the equator
		std::vector<TsTrack::Pt> p = TsTrack::Propagate (r0, v0, mu, 1e-4, 1000.0, 50);
		REQUIRE (p.size() == 51);
		bool finite = true;
		for (const TsTrack::Pt &q : p) finite = finite && std::isfinite (q.lat) && std::isfinite (q.lng);
		CHECK (finite);
		CHECK (!TsTrack::Keys (p, 8, 1).empty());
	}
	const double vn[3] = {NAN, 0, 0};
	std::vector<TsTrack::Pt> bad = TsTrack::Propagate (r0, vn, mu, 0.0, 1000.0, 10);
	for (const auto &k : TsTrack::Keys (bad, 8, 1)) CHECK ((k[1] >= 0 && k[1] < 16 && k[2] >= 0 && k[2] < 32));
	CHECK (TsTrack::Keys ({{NAN, 0, 0}, {0, NAN, 0}, {INFINITY, 0, 0}}, 8, 1).empty());
}

TEST_CASE ("The shipped FLOOR boxes hold the depressions and none of the sea near them", "[decode]")
{
	std::vector<TsBodyCfg> b;
	std::string err;
	REQUIRE (TsReadCfg ((fs::path (__FILE__).parent_path().parent_path() / "Config" / "TileStream.cfg").string(), b, err));
	REQUIRE (!b.empty());
	const std::vector<TsBodyCfg::Floor> &f = b[0].floor;
	auto at = [&] (double lng, double lat) {
		for (const TsBodyCfg::Floor &x : f) if (lng >= x.lng0 && lng <= x.lng1 && lat >= x.lat0 && lat <= x.lat1) return x.m;
		return 0.0;
	};
	CHECK (at (35.59, 32.82) == -212.0); // Sea of Galilee
	CHECK (at (35.57, 32.72) == -212.0); // its south end: in both boxes, the first wins
	CHECK (at (35.50, 32.85) == -212.0); // Ginosar
	CHECK (at (35.50, 31.50) == -430.0); // Dead Sea
	CHECK (at (35.55, 32.30) == -430.0); // lower Jordan valley
	CHECK (at (35.20, 30.70) == -430.0); // Arava
	CHECK (at (51.00, 42.00) == -28.0);  // Caspian Sea
	CHECK (at (53.50, 41.30) == -28.0);  // Kara-Bogaz-Gol
	CHECK (at (40.90, 13.25) == -125.0); // Lake Afrera
	CHECK (at (-115.83, 33.30) == -71.0); // Salton Sea
	CHECK (at (42.42, 11.65) == -155.0); // Lake Assal
	CHECK (at (30.60, 29.46) == -43.0);  // Lake Qarun
	CHECK (at (137.30, -28.40) == -15.0); // Lake Eyre
	CHECK (at (-116.83, 36.25) == -86.0); // Badwater
	CHECK (at (27.10, 29.60) == -133.0); // Qattara
	CHECK (at (89.27, 42.65) == -154.0); // Turpan, Lake Ayding
	CHECK (at (40.30, 14.24) == -125.0); // Danakil, Dallol
	// below 0 m just past the old box edges (Terrain Tiles): inside a box now, no straight cliff
	CHECK (at (41.233, 12.944) == -125.0); // Danakil south, -108 m south-east of its old corner
	CHECK (at (41.15, 12.70) == -125.0);
	CHECK (at (41.43, 13.10) == -125.0);   // its east arm
	CHECK (at (40.06, 14.72) == -125.0);   // Danakil north, -30 m above its old top
	CHECK (at (138.293, -28.128) == -15.0); // Lake Eyre's east edge, -90 m
	CHECK (at (138.33, -28.05) == -15.0);
	CHECK (at (26.738, 28.698) == -133.0); // Qattara's south edge
	CHECK (at (26.45, 28.66) == -133.0);
	CHECK (at (30.461, 28.99) == -43.0);   // Fayum's south edge
	CHECK (at (35.789, 32.751) == -430.0); // Yarmouk, -67 m, between the Dead Sea and Galilee boxes
	CHECK (at (35.76, 32.78) == -430.0);
	CHECK (at (55.53, 46.10) == -28.0);    // the Caspian lowland past its box's east edge
	for (auto k : std::vector<std::pair<double, double>> {{51.60, 43.45}, {51.77, 43.375}, {51.35, 43.72}, {51.65, 43.86}, {51.90, 43.23}, {51.60, 43.35}, {51.66, 43.31}})
		CHECK (at (k.first, k.second) == -132.0); // Karagiye, inside the Caspian box: listed before it
	for (auto k : std::vector<std::pair<double, double>> {{51.20, 43.45}, {51.62, 43.17}, {51.30, 43.45}, {51.50, 43.25}, {51.20, 43.76}})
		CHECK (at (k.first, k.second) == -28.0); // the Caspian off Aktau and Kuryk, the shore, and the depressions beside Karagiye
	for (auto sea : std::vector<std::pair<double, double>> {{34.80, 32.30}, {35.03, 32.84}, {34.95, 29.50}, {42.65, 11.55}, {43.00, 11.70}, {41.45, 14.40}, {40.80, 14.78},
		{39.80, 15.30}, {-114.70, 31.60}, {-117.40, 32.90}, {28.90, 31.00}, {25.20, 31.70}, {137.80, -33.20}, {38.00, 43.00}, {50.50, 27.00}})
		CHECK (at (sea.first, sea.second) == 0.0); // Mediterranean, Haifa Bay, Aqaba, Ghoubbet, Tadjoura, Red Sea, Zula, Gulf of California, Pacific, ...
	for (const TsBodyCfg::Floor &x : f) CHECK ((x.m < 0.0 && x.lng1 - x.lng0 < 12.0 && x.lat1 - x.lat0 < 14.0));
}
