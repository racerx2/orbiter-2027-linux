// not upstream: the planet tile stream: grids, pixels, DDS, config, size cap, and the service on file:// sources

#include <catch2/catch_test_macros.hpp>
#include "TileStream.h"
#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

// QNetworkAccessManager needs one; made first in a test, it goes last
struct App {
	int argc = 1;
	char arg0[16] = "TileStream.Test";
	char *argv[2] = {arg0, nullptr};
	QCoreApplication app {argc, argv};
};

struct Tmp {
	fs::path root;
	Tmp ()
	{
		static int n = 0;
		root = fs::temp_directory_path() / ("tilestream-test-" + std::to_string (getpid()) + "-" + std::to_string (n++));
		fs::remove_all (root);
		fs::create_directories (root);
	}
	~Tmp () { std::error_code ec; fs::remove_all (root, ec); }
};

static uint32_t U32 (const std::string &d, size_t at)
{
	const unsigned char *p = (const unsigned char*)d.data() + at;
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static std::string Read (const fs::path &p)
{
	std::ifstream f (p, std::ios::binary);
	return std::string (std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>());
}

static void Png (const fs::path &p, int size, QColor c)
{
	fs::create_directories (p.parent_path());
	QImage img (size, size, QImage::Format_RGB888);
	img.fill (c);
	REQUIRE (img.save (QString::fromStdString (p.string()), "PNG"));
}

static void FillPng (const fs::path &p)
{
	fs::create_directories (p.parent_path());
	QImage img (256, 256, QImage::Format_Indexed8);
	img.setColorTable ({qRgb (24, 44, 78)});
	img.fill (0);
	REQUIRE (img.save (QString::fromStdString (p.string()), "PNG"));
}

static bool WaitFor (const std::function<bool ()> &f, double sec)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::duration<double> (sec);
	while (std::chrono::steady_clock::now() < end) {
		if (f()) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (50));
	}
	return f();
}

// every DXT1 block of a level is 3-colour mode with all four-by-four pixels transparent
static bool AllWater (const std::string &d, size_t at, int blocks)
{
	for (int i = 0; i < blocks; i++, at += 8) {
		uint16_t c0 = (uint16_t)U32 (d, at), c1 = (uint16_t)(U32 (d, at) >> 16);
		if (c0 > c1 || U32 (d, at + 4) != 0xFFFFFFFF) return false;
	}
	return true;
}

TEST_CASE ("An Orbiter tile is four EOX tiles one zoom level up from L-4", "[TileStream]")
{
	TsImg::Grid g = TsImg::EoxGrid (16, 1397, 2260); // KSC
	CHECK (g.z == 13);
	CHECK (g.row0 == 2794);
	CHECK (g.col0 == 4520);
	CHECK (g.nrow == 2);
	CHECK (g.ncol == 2);
	g = TsImg::EoxGrid (2, 0, 0);
	CHECK (g.z == 1);
	CHECK (g.nrow == 2);
	CHECK (g.ncol == 4);
	CHECK (TsImg::Size (1) == 128);
	CHECK (TsImg::Size (2) == 256);
	CHECK (TsImg::Size (3) == 512);
	CHECK (TsImg::Size (12) == 512);
	CHECK (TsImg::Url ("x/{z}/{row}/{col}.jpg", 13, 2795, 4521) == "x/13/2795/4521.jpg");
	CHECK (TsImg::Url ("g/{n}/{row}/{col}/{n}", 7, 1, 2) == "g/7/1/2/7");
}

TEST_CASE ("GIBS tiles cover an Orbiter tile two per axis at most, inside the globe", "[TileStream]")
{
	for (int L = 4; L <= 14; L++) {
		int nlat = 1 << (L - 4), nlng = 1 << (L - 3);
		for (int la : {0, nlat / 2, nlat - 1})
			for (int ln : {0, nlng / 3, nlng - 1}) {
				TsImg::Grid g = TsImg::GibsGrid (L, la, ln, 8);
				CHECK (g.z == std::min (L - 3, 8));
				CHECK (g.nrow >= 1);
				CHECK (g.nrow <= 2);
				CHECK (g.ncol >= 1);
				CHECK (g.ncol <= 2);
				CHECK (g.col0 + g.ncol <= (int)std::ceil (1.25 * (1 << g.z)));
				CHECK (g.row0 + g.nrow <= (int)std::ceil (0.625 * (1 << g.z)));
			}
	}
	TsImg::Grid g = TsImg::GibsGrid (2, 0, 0, 7);
	CHECK (g.z == 0);
	CHECK (g.row0 == 0);
	CHECK (g.col0 == 0);
	CHECK (g.nrow == 1);
	CHECK (g.ncol == 2);
}

TEST_CASE ("The EOX ocean fill tile is told apart from a picture", "[TileStream]")
{
	int fill[3] = {24, 44, 78};
	QImage f (256, 256, QImage::Format_Indexed8);
	f.setColorTable ({qRgb (24, 44, 78)});
	f.fill (0);
	QByteArray png;
	QBuffer b (&png);
	b.open (QIODevice::WriteOnly);
	REQUIRE (f.save (&b, "PNG"));
	QImage g;
	REQUIRE (g.loadFromData (png));
	CHECK (TsImg::IsFill (g, fill));
	QImage h = g.convertToFormat (QImage::Format_RGB888);
	h.setPixelColor (3, 3, QColor (200, 200, 200));
	CHECK_FALSE (TsImg::IsFill (h, fill));
	CHECK_FALSE (TsImg::IsFill (QImage(), fill));
}

TEST_CASE ("A tile is a DXT1 DDS with ten mip levels, 174904 bytes, that decodes close to the picture", "[TileStream]")
{
	QImage img (512, 512, QImage::Format_RGB888);
	for (int y = 0; y < 512; y++)
		for (int x = 0; x < 512; x++) img.setPixelColor (x, y, QColor (x / 2, y / 2, (x + y) / 4));
	std::string d = TsImg::Dds (img, false);
	CHECK (d.size() == 174904);
	CHECK (d.compare (0, 4, "DDS ") == 0);
	CHECK (d.compare (84, 4, "DXT1") == 0);
	CHECK (U32 (d, 12) == 512);
	CHECK (U32 (d, 16) == 512);
	CHECK (U32 (d, 20) == 131072);
	CHECK (U32 (d, 28) == 10);
	QImage back = TsImg::DdsImage (QByteArray (d.data(), (qsizetype)d.size()));
	REQUIRE (back.width() == 512);
	REQUIRE (back.height() == 512);
	int worst = 0;
	double sum = 0.0;
	for (int y = 0; y < 512; y++)
		for (int x = 0; x < 512; x++) {
			QColor a = img.pixelColor (x, y), c = back.pixelColor (x, y);
			int e = std::max ({std::abs (a.red() - c.red()), std::abs (a.green() - c.green()), std::abs (a.blue() - c.blue())});
			worst = std::max (worst, e);
			sum += e;
		}
	CHECK (worst <= 16);
	CHECK (sum / (512.0 * 512.0) < 4.0);
	CHECK (TsImg::Dds (QImage (128, 128, QImage::Format_RGB888), false).size() == 11064); // L1: 8 levels
}

TEST_CASE ("The mask takes off the night light background and keeps water transparent down the mips", "[TileStream]")
{
	QImage lights (512, 512, QImage::Format_RGB888), water (512, 512, QImage::Format_RGB888);
	lights.fill (QColor (60, 50, 40));
	for (int y = 0; y < 512; y++)
		for (int x = 0; x < 512; x++) water.setPixelColor (x, y, x < 256 ? QColor (128, 128, 128) : QColor (75, 75, 75));
	int black[3] = {3, 4, 14};
	QImage m = TsImg::Mask (lights, water, black, 101.5);
	CHECK (qAlpha (m.pixel (10, 10)) == 0);
	CHECK (qAlpha (m.pixel (400, 10)) == 255);
	CHECK (qRed (m.pixel (400, 10)) == 57);
	CHECK (qGreen (m.pixel (400, 10)) == 46);
	CHECK (qBlue (m.pixel (400, 10)) == 26);
	std::string d = TsImg::Dds (m, true);
	REQUIRE (d.size() == 174904);
	size_t at = 128;
	for (int lv = 0; lv <= 6; lv++) { // 512 px down to 8 px: the west half stays water
		int bw = 128 >> lv, water = 0;
		for (int by = 0; by < bw; by++)
			for (int bx = 0; bx < bw / 2; bx++)
				if (AllWater (d, at + ((size_t)by * bw + bx) * 8, 1)) water++;
		CHECK (water == bw * bw / 2);
		CHECK_FALSE (AllWater (d, at + (size_t)(bw - 1) * 8, 1)); // east edge block: land, 4-colour mode
		at += (size_t)bw * bw * 8;
	}
}

TEST_CASE ("Terrarium heights become a 259x259 .elv, row 0 south, nothing for sea level", "[TileStream]")
{
	auto tiles = [] (const TsImg::Grid &g, std::function<QColor (int row)> col) {
		std::vector<QImage> v;
		for (int r = 0; r < g.nrow; r++)
			for (int c = 0; c < g.ncol; c++) {
				QImage im (256, 256, QImage::Format_RGB888);
				im.fill (col (g.row0 + r));
				v.push_back (im);
			}
		return v;
	};
	TsImg::Grid g = TsImg::TerrGrid (14, 1000, 1500, 15);
	CHECK (g.z == 11);
	CHECK (g.nrow >= 1);
	CHECK (g.ncol >= 1);
	CHECK (g.nrow <= 3);
	CHECK (g.ncol <= 3); // aligned edges: the padding reaches both neighbours
	std::string e = TsImg::Elev (14, 1000, 1500, g, tiles (g, [] (int) { return QColor (128, 100, 0); }), 0.5);
	REQUIRE (e.size() == 134262);
	CHECK (e.compare (0, 3, "ELE") == 0);
	CHECK (e[3] == 1);
	CHECK (U32 (e, 4) == 100);
	CHECK ((int32_t)U32 (e, 8) == -16);
	CHECK (U32 (e, 12) == 259);
	CHECK (U32 (e, 20) == 1);
	double scale, emin, emax, latmin, latmax;
	memcpy (&scale, &e[28], 8);
	memcpy (&latmin, &e[44], 8);
	memcpy (&latmax, &e[52], 8);
	memcpy (&emin, &e[76], 8);
	memcpy (&emax, &e[84], 8);
	CHECK (scale == 0.5);
	CHECK (emin == 100.0);
	CHECK (emax == 100.0);
	CHECK (std::abs ((latmax - latmin) * 180.0 / M_PI - 180.0 / 1024.0) < 1e-9);
	int16_t v;
	memcpy (&v, &e[100 + 2 * (130 * 259 + 130)], 2);
	CHECK (v == 200);
	CHECK (TsImg::Elev (14, 1000, 1500, g, tiles (g, [] (int) { return QColor (127, 206, 0); }), 0.5).empty()); // -50 m: sea
	TsImg::Grid h = TsImg::TerrGrid (4, 0, 0, 15); // z1: north row 1000 m, south row 10 m
	REQUIRE (h.nrow == 2);
	std::string w = TsImg::Elev (4, 0, 0, h, tiles (h, [] (int row) { return row ? QColor (128, 10, 0) : QColor (131, 232, 0); }), 0.5);
	REQUIRE (w.size() == 134262);
	int16_t south, north;
	memcpy (&south, &w[100 + 2 * (0 * 259 + 100)], 2);
	memcpy (&north, &w[100 + 2 * (258 * 259 + 100)], 2);
	CHECK (south == 20);
	CHECK (north == 2000);
}

TEST_CASE ("An ancestor's sub-area is cut and scaled to a tile", "[TileStream]")
{
	QImage anc (512, 512, QImage::Format_RGB888);
	for (int y = 0; y < 512; y++)
		for (int x = 0; x < 512; x++)
			anc.setPixelColor (x, y, y < 256 ? (x < 256 ? Qt::red : Qt::green) : (x < 256 ? Qt::blue : Qt::white));
	QImage r = TsImg::Region (anc, 1, 1, 0); // south-west child
	REQUIRE (r.width() == 512);
	CHECK (r.pixelColor (256, 256) == QColor (Qt::blue));
	CHECK (TsImg::Region (anc, 1, 0, 1).pixelColor (256, 256) == QColor (Qt::green));
	CHECK (TsImg::Region (anc, 2, 3, 3).pixelColor (256, 256) == QColor (Qt::white));
}

TEST_CASE ("The tile stream config reads its body and rejects bad lines", "[TileStream]")
{
	std::vector<TsBodyCfg> b;
	std::string err;
	REQUIRE (TsReadCfg ("Config/TileStream.cfg", b, err));
	REQUIRE (b.size() == 1);
	CHECK (b[0].name == "Earth");
	CHECK (b[0].surf.kind == "eox");
	CHECK (b[0].surf.lmin == 1);
	CHECK (b[0].surf.lmax == 16);
	CHECK (b[0].fallback.kind == "gibs");
	CHECK (b[0].fallback.nmax == 7);
	CHECK (b[0].fallback.lmax == 10);
	CHECK (b[0].lights.nmax == 7);
	CHECK (b[0].water.nmax == 8);
	CHECK (b[0].black[2] == 14);
	CHECK (b[0].waterGrey == 101.5);
	CHECK (b[0].fill[0] == 24);
	CHECK (b[0].elev.kind == "terrarium");
	CHECK (b[0].elev.nmax == 15);
	CHECK (b[0].elev.lmax == 16);
	CHECK (b[0].elevScale == 0.5);
	REQUIRE (b[0].credit.size() == 3);
	CHECK (b[0].credit[2].find ("Terrain Tiles on AWS") != std::string::npos);
	CHECK (b[0].credit[0].find ("EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016 & 2017)") != std::string::npos);
	CHECK (b[0].credit[1].find ("OpenStreetMap contributors") != std::string::npos);
	Tmp t;
	auto bad = [&] (const std::string &text, const std::string &what) {
		std::string f = (t.root / "bad.cfg").string();
		std::ofstream (f) << text;
		std::string e;
		CHECK_FALSE (TsReadCfg (f, b, e));
		CHECK (e.find (what) != std::string::npos);
	};
	bad ("BODY X\nSURF eox u 1\nEND\n", "bad SURF");
	bad ("SURF eox u 1 16\n", "outside BODY");
	bad ("BODY X\nSURF eox u 1 16\nEND\n", "needs SURF, LIGHTS and WATER");
	bad ("BODY X\nLIGHTS eox u 7\nEND\n", "bad LIGHTS");
	bad ("BODY X\nFOO 1\nEND\n", "unknown FOO");
	bad ("BODY X\nSURF eox u 1 16\n", "has no END");
	CHECK_FALSE (TsReadCfg ((t.root / "none.cfg").string(), b, err));
}

TEST_CASE ("The size cap takes the oldest streamed tiles, never L8 and below or a tile it didn't write", "[TileStream]")
{
	App app;
	Tmp t;
	fs::path tex = t.root / "Earth";
	std::string cfg = (t.root / "ts.cfg").string();
	std::ofstream (cfg) << "BODY Earth\nSURF eox file:///none/{z}/{row}/{col} 1 16\nLIGHTS gibs file:///none/{n}/{row}/{col} 7\nWATER gibs file:///none/{n}/{row}/{col} 8\nEND\n";
	std::string mb (1 << 20, 'x');
	auto now = fs::file_time_type::clock::now();
	auto put = [&] (const char *rel, int hours) {
		for (const char *layer : {"Surf", "Mask"}) {
			fs::path p = tex / layer / rel;
			fs::create_directories (p.parent_path());
			std::ofstream (p, std::ios::binary) << mb;
			fs::last_write_time (p, now - std::chrono::hours (hours));
		}
	};
	put ("09/000000/000000.dds", 3);
	put ("09/000000/000001.dds", 2);
	put ("08/000000/000000.dds", 9);
	put ("09/000000/000002.dds", 10); // not ours
	fs::create_directories (tex / "TileStream");
	std::ofstream (tex / "TileStream" / "written.idx") << "9 0 0\n9 0 1\n8 0 0\n";
	TileStream::Options o;
	o.cfgfile = cfg;
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth"};
	o.capMB = 3;
	o.offline = true;
	{
		TileStream ts (o);
		REQUIRE (ts.Enabled ("Earth"));
		CHECK (ts.Want ("Earth", 12, 0, 0, 0) == -1); // offline
		ts.Evict();
		std::this_thread::sleep_for (std::chrono::milliseconds (300));
	}
	CHECK_FALSE (fs::exists (tex / "Surf" / "09" / "000000" / "000000.dds"));
	CHECK_FALSE (fs::exists (tex / "Mask" / "09" / "000000" / "000001.dds"));
	CHECK (fs::exists (tex / "Surf" / "08" / "000000" / "000000.dds"));
	CHECK (fs::exists (tex / "Surf" / "09" / "000000" / "000002.dds"));
	std::string idx = Read (tex / "TileStream" / "written.idx");
	CHECK (idx.find ("9 0 0") == std::string::npos);
	CHECK (idx.find ("8 0 0") != std::string::npos);
}

TEST_CASE ("Wants go stale, required tiles stay, and the queue is capped", "[TileStream]")
{
	App app;
	Tmp t;
	std::string cfg = (t.root / "ts.cfg").string();
	std::ofstream (cfg) << "BODY Earth\nSURF eox file:///none/{z}/{row}/{col} 1 16\nLIGHTS gibs file:///none/{n}/{row}/{col} 7\nWATER gibs file:///none/{n}/{row}/{col} 8\nEND\n";
	std::atomic<int64_t> ms {0};
	auto t0 = std::chrono::steady_clock::now();
	TileStream::Options o;
	o.cfgfile = cfg;
	o.bodyDir = [&] (const std::string &) { return (t.root / "Earth").string(); };
	o.bodies = {"Earth"};
	o.maxActive = 0;
	o.now = [&] { return t0 + std::chrono::milliseconds (ms.load()); };
	TileStream ts (o);
	CHECK (ts.Pending() == 5); // L1-L4
	CHECK (ts.Want ("Earth", 14, 1, 1, 0) == 0);
	CHECK (ts.Want ("Earth", 14, 1, 1, 0) == 0);
	CHECK (ts.Pending() == 6);
	ms = 3000;
	CHECK (WaitFor ([&] { return ts.Pending() == 5; }, 2.0));
	for (int i = 0; i < 300; i++) ts.Want ("Earth", 14, 0, i, 0);
	CHECK (ts.Pending() == 256);
	CHECK (ts.Want ("Earth", 17, 0, 0, 0) == -1);
	CHECK (ts.Want ("Earth", 4, 0, 2, 0) == -1);
	CHECK (ts.Want ("Earth", 2, 0, 1, 0) == -1);
	CHECK (ts.Want ("Mars", 4, 0, 0, 0) == -1);
	CHECK (ts.Want ("Earth", 14, 1, 1, 1) == -1); // a bad file the stream didn't write is never replaced
}

TEST_CASE ("The service writes stock tiles from file:// sources", "[TileStream]")
{
	App app;
	Tmp t;
	fs::path srv = t.root / "srv", tex = t.root / "Textures" / "Earth";
	std::string base = "file://" + srv.string();
	auto eox = [&] (int z, int r, int c) { return srv / "eox" / std::to_string (z) / std::to_string (r) / (std::to_string (c) + ".png"); };
	auto gibs = [&] (const char *layer, int n, int r, int c) { return srv / layer / std::to_string (n) / std::to_string (r) / (std::to_string (c) + ".png"); };
	auto masks = [&] (int L, int la, int ln) {
		TsImg::Grid g = TsImg::GibsGrid (L, la, ln, 7);
		for (int r = 0; r < g.nrow; r++)
			for (int c = 0; c < g.ncol; c++) Png (gibs ("lights", g.z, g.row0 + r, g.col0 + c), 512, QColor (100, 90, 80));
		g = TsImg::GibsGrid (L, la, ln, 8);
		for (int r = 0; r < g.nrow; r++)
			for (int c = 0; c < g.ncol; c++) Png (gibs ("water", g.z, g.row0 + r, g.col0 + c), 512, QColor (128, 128, 128));
	};
	for (int r = 0; r < 2; r++)
		for (int c = 0; c < 4; c++) Png (eox (1, r, c), 256, QColor (40 + 60 * r, 40 + 40 * c, 10));
	masks (4, 0, 0);
	masks (4, 0, 1);
	masks (1, 0, 0);
	for (int r = 0; r < 2; r++)
		for (int c = 0; c < 2; c++) {
			Png (eox (11, 2000 + r, 3000 + c), 256, QColor (200, 150, 100));
			FillPng (eox (9, 200 + r, 200 + c));
		}
	masks (14, 1000, 1500);
	{
		TsImg::Grid g = TsImg::TerrGrid (14, 1000, 1500, 15);
		for (int r = 0; r < g.nrow; r++)
			for (int c = 0; c < g.ncol; c++)
				Png (srv / "terr" / std::to_string (g.z) / std::to_string (g.col0 + c) / (std::to_string (g.row0 + r) + ".png"), 256, QColor (128, 100, 0)); // 100 m
	}
	masks (12, 100, 100);
	masks (13, 50, 50); // no EOX tiles: 404
	std::string cfg = (t.root / "ts.cfg").string();
	std::ofstream (cfg) << "BODY Earth\n"
		"SURF eox " << base << "/eox/{z}/{row}/{col}.png 1 16\n"
		"LIGHTS gibs " << base << "/lights/{n}/{row}/{col}.png 7\n"
		"LIGHTS_BLACK 3 4 14\n"
		"WATER gibs " << base << "/water/{n}/{row}/{col}.png 8\n"
		"ELEV terrarium " << base << "/terr/{z}/{col}/{row}.png 15 16\n"
		"CREDIT test credit\nEND\n";
	TileStream::Options o;
	o.cfgfile = cfg;
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth", "Mars"};
	o.capMB = 0;
	TileStream ts (o);
	REQUIRE (ts.Enabled ("Earth"));
	CHECK_FALSE (ts.Enabled ("Mars"));
	CHECK (ts.Credit ("Earth") == "test credit");

	// the required tiles come first, unasked
	fs::path l4w = tex / "Surf" / "04" / "000000" / "000000.dds";
	REQUIRE (WaitFor ([&] { return fs::exists (l4w) && fs::exists (tex / "Surf" / "04" / "000000" / "000001.dds") && fs::exists (tex / "Surf" / "01" / "000000" / "000000.dds"); }, 20.0));
	CHECK (fs::file_size (l4w) == 174904);
	CHECK (fs::file_size (tex / "Surf" / "01" / "000000" / "000000.dds") == 11064);
	CHECK (fs::exists (tex / "Mask" / "04" / "000000" / "000000.dds"));
	std::string d = Read (l4w);
	QImage w = TsImg::DdsImage (QByteArray (d.data(), (qsizetype)d.size()));
	REQUIRE (w.width() == 512);
	auto near = [] (QColor a, QColor b) { return std::abs (a.red() - b.red()) <= 8 && std::abs (a.green() - b.green()) <= 8 && std::abs (a.blue() - b.blue()) <= 8; };
	CHECK (near (w.pixelColor (100, 100), QColor (40, 40, 10)));   // z1 row 0 col 0, north-west
	CHECK (near (w.pixelColor (400, 400), QColor (100, 80, 10)));  // z1 row 1 col 1
	d = Read (tex / "Surf" / "04" / "000000" / "000001.dds");
	w = TsImg::DdsImage (QByteArray (d.data(), (qsizetype)d.size()));
	CHECK (near (w.pixelColor (100, 100), QColor (40, 120, 10)));  // z1 row 0 col 2

	// a tile the client wants, touched while it waits
	fs::path l14 = tex / "Surf" / "14" / "001000" / "001500.dds";
	CHECK (ts.Want ("Earth", 14, 1000, 1500, 0) == 0);
	REQUIRE (WaitFor ([&] { ts.Want ("Earth", 14, 1000, 1500, 0); return fs::exists (l14); }, 20.0));
	CHECK (fs::file_size (l14) == 174904);
	std::string m = Read (tex / "Mask" / "14" / "001000" / "001500.dds");
	REQUIRE (m.size() == 174904);
	CHECK (AllWater (m, 128, 128 * 128));
	CHECK (ts.Want ("Earth", 14, 1000, 1500, 0) == 1);
	bool seen = false;
	REQUIRE (WaitFor ([&] {
		int k[3 * 16];
		int n = ts.Done ("Earth", k, 16);
		for (int i = 0; i < n; i++) if (k[3 * i] == 14 && k[3 * i + 1] == 1000 && k[3 * i + 2] == 1500) seen = true;
		return seen;
	}, 5.0));
	CHECK (fs::exists (tex / "TileStream" / "written.idx"));

	// its elevation came with it; a cache hit on it without one has it made
	fs::path elv = tex / "Elev" / "14" / "001000" / "001500.elv";
	REQUIRE (fs::exists (elv));
	CHECK (fs::file_size (elv) == 134262);
	CHECK_FALSE (fs::exists (tex / "Elev" / "04" / "000000" / "000000.elv")); // no Terrarium tile served: no Elev, the Surf still came
	fs::remove (elv);
	CHECK (ts.Want ("Earth", 14, 1000, 1500, 2) == 1);
	REQUIRE (WaitFor ([&] { return fs::exists (elv); }, 20.0));

	// a file of ours that failed to load is made again
	fs::resize_file (l14, 1000);
	CHECK (ts.Want ("Earth", 14, 1000, 1500, 1) == 0);
	REQUIRE (WaitFor ([&] { ts.Want ("Earth", 14, 1000, 1500, 0); std::error_code ec; return fs::file_size (l14, ec) == 174904; }, 20.0));

	// open ocean: no file, and nothing below it is asked for
	REQUIRE (WaitFor ([&] { return ts.Want ("Earth", 12, 100, 100, 0) == -1; }, 20.0));
	CHECK_FALSE (fs::exists (tex / "Surf" / "12" / "000100" / "000100.dds"));
	CHECK (ts.Want ("Earth", 13, 201, 200, 0) == -1);
	CHECK (ts.Want ("Earth", 16, 1601, 1603, 0) == -1);

	// no imagery: not asked for again
	REQUIRE (WaitFor ([&] { return ts.Want ("Earth", 13, 50, 50, 0) == -1; }, 20.0));
	CHECK_FALSE (fs::exists (tex / "Surf" / "13" / "000050" / "000050.dds"));
	CHECK (Read (tex / "TileStream" / "none.idx").find ("13 50 50") == 0);
}

// file:// sources in a temp folder: EOX tiles, GIBS lights, water and Blue Marble
struct Srv {
	fs::path root;
	std::string url;
	explicit Srv (const fs::path &r): root (r / "srv"), url ("file://" + (r / "srv").string()) {}
	fs::path Eox (int z, int r, int c) const { return root / "eox" / std::to_string (z) / std::to_string (r) / (std::to_string (c) + ".png"); }
	fs::path Gibs (const char *l, int n, int r, int c) const { return root / l / std::to_string (n) / std::to_string (r) / (std::to_string (c) + ".png"); }
	void Layer (const char *l, int L, int la, int ln, int nmax, QColor col) const
	{
		TsImg::Grid g = TsImg::GibsGrid (L, la, ln, nmax);
		for (int r = 0; r < g.nrow; r++)
			for (int c = 0; c < g.ncol; c++) Png (Gibs (l, g.z, g.row0 + r, g.col0 + c), 512, col);
	}
	void Masks (int L, int la, int ln) const
	{
		Layer ("lights", L, la, ln, 7, QColor (100, 90, 80));
		Layer ("water", L, la, ln, 8, QColor (75, 75, 75));
	}
	void EoxTile (int L, int la, int ln, QColor col) const
	{
		TsImg::Grid g = TsImg::EoxGrid (L, la, ln);
		for (int r = 0; r < g.nrow; r++)
			for (int c = 0; c < g.ncol; c++) Png (Eox (g.z, g.row0 + r, g.col0 + c), 256, col);
	}
	void Required (bool eox) const
	{
		for (int L = 1; L <= 4; L++)
			for (int ln = 0; ln < (L == 4 ? 2 : 1); ln++) {
				if (eox) EoxTile (L, 0, ln, QColor (50, 60, 70));
				Masks (L, 0, ln);
				Layer ("bmng", L, 0, ln, 7, QColor (30, 90, 30));
			}
	}
	std::string Cfg (const fs::path &file, const std::string &eoxurl) const
	{
		std::ofstream (file) << "BODY Earth\n"
			"SURF eox " << eoxurl << "/{z}/{row}/{col}.png 1 16\n"
			"SURF_FALLBACK gibs " << url << "/bmng/{n}/{row}/{col}.png 7 10\n"
			"LIGHTS gibs " << url << "/lights/{n}/{row}/{col}.png 7\n"
			"WATER gibs " << url << "/water/{n}/{row}/{col}.png 8\nEND\n";
		return file.string();
	}
};

static QColor Pixel (const fs::path &dds, int x, int y)
{
	std::string d = Read (dds);
	QImage im = TsImg::DdsImage (QByteArray (d.data(), (qsizetype)d.size()));
	return im.isNull() ? QColor() : im.pixelColor (x, y);
}

static bool Near (QColor a, QColor b)
{
	return std::abs (a.red() - b.red()) <= 8 && std::abs (a.green() - b.green()) <= 8 && std::abs (a.blue() - b.blue()) <= 8;
}

TEST_CASE ("EOX not answering: the host pauses and Blue Marble stands in, marked for a refetch", "[TileStream]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	s.Required (false);
	s.Masks (6, 3, 5);
	s.Layer ("bmng", 6, 3, 5, 7, QColor (30, 90, 30));
	fs::path tex = t.root / "Earth";
	TileStream::Options o;
	o.cfgfile = s.Cfg (t.root / "ts.cfg", "bogus://eox"); // no such scheme: nothing answers
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth"};
	TileStream ts (o);
	fs::path f = tex / "Surf" / "06" / "000003" / "000005.dds";
	REQUIRE (WaitFor ([&] { ts.Want ("Earth", 6, 3, 5, 0); return fs::exists (f); }, 25.0));
	CHECK (Near (Pixel (f, 256, 256), QColor (30, 90, 30)));
	CHECK (fs::exists (tex / "Surf" / "04" / "000000" / "000001.dds"));
	REQUIRE (WaitFor ([&] { return Read (tex / "TileStream" / "refetch.idx").find ("6 3 5") != std::string::npos; }, 5.0));
	CHECK (ts.Want ("Earth", 12, 100, 100, 0) == 0); // above the fallback's levels: waits for EOX
}

TEST_CASE ("Coarse levels go first; ocean quarters are filled from the ancestor", "[TileStream]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	s.Required (true);
	s.EoxTile (8, 10, 20, QColor (200, 100, 50));
	s.Masks (8, 10, 20);
	s.EoxTile (10, 42, 85, QColor (100, 200, 50));
	s.Masks (10, 42, 85);
	// L12 (150, 300): north-west and south-east quarters are EOX ocean fill
	TsImg::Grid g = TsImg::EoxGrid (12, 150, 300);
	FillPng (s.Eox (g.z, g.row0, g.col0));
	Png (s.Eox (g.z, g.row0, g.col0 + 1), 256, QColor (220, 220, 220));
	Png (s.Eox (g.z, g.row0 + 1, g.col0), 256, QColor (220, 220, 220));
	FillPng (s.Eox (g.z, g.row0 + 1, g.col0 + 1));
	s.Masks (12, 150, 300);
	fs::path tex = t.root / "Earth";
	QImage anc (512, 512, QImage::Format_RGB888);
	anc.fill (QColor (90, 160, 30));
	fs::path ap = tex / "Surf" / "11" / "000075" / "000150.dds"; // an installed ancestor
	fs::create_directories (ap.parent_path());
	std::string ad = TsImg::Dds (anc, false);
	std::ofstream (ap, std::ios::binary).write (ad.data(), (std::streamsize)ad.size());
	TileStream::Options o;
	o.cfgfile = s.Cfg (t.root / "ts.cfg", s.url + "/eox");
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth"};
	o.maxActive = 1;
	TileStream ts (o);
	CHECK (ts.Want ("Earth", 10, 42, 85, 0) == 0);
	CHECK (ts.Want ("Earth", 8, 10, 20, 0) == 0);
	CHECK (ts.Want ("Earth", 12, 150, 300, 0) == 0);
	std::vector<int> order;
	REQUIRE (WaitFor ([&] {
		for (int L : {8, 10, 12}) ts.Want ("Earth", L, L == 8 ? 10 : L == 10 ? 42 : 150, L == 8 ? 20 : L == 10 ? 85 : 300, 0);
		int k[3 * 16];
		int n = ts.Done ("Earth", k, 16);
		for (int i = 0; i < n; i++) if (k[3 * i] >= 8) order.push_back (k[3 * i]);
		return order.size() == 3;
	}, 20.0));
	CHECK (order == std::vector<int> {8, 10, 12});
	fs::path f = tex / "Surf" / "12" / "000150" / "000300.dds";
	CHECK (Near (Pixel (f, 100, 100), QColor (90, 160, 30)));   // fill: the ancestor
	CHECK (Near (Pixel (f, 400, 100), QColor (220, 220, 220))); // picture
	CHECK (Near (Pixel (f, 400, 400), QColor (90, 160, 30)));
	CHECK (Read (tex / "TileStream" / "written.idx").find ("11 75 150") == std::string::npos); // the installed one isn't ours
}

TEST_CASE ("A tile that can't be written is held and still wanted, with no Mask left behind", "[TileStream]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	s.Required (true);
	s.EoxTile (13, 61, 61, QColor (10, 200, 10));
	s.Masks (13, 61, 61);
	fs::path tex = t.root / "Earth", lock = tex / "Surf" / "13" / "000061";
	fs::create_directories (lock);
	fs::permissions (lock, fs::perms::owner_read | fs::perms::owner_exec);
	TileStream::Options o;
	o.cfgfile = s.Cfg (t.root / "ts.cfg", s.url + "/eox");
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth"};
	{
		TileStream ts (o);
		CHECK (ts.Want ("Earth", 13, 61, 61, 0) == 0);
		REQUIRE (WaitFor ([&] { ts.Want ("Earth", 13, 61, 61, 0); return ts.Pending() == 0; }, 20.0));
		CHECK (ts.Want ("Earth", 13, 61, 61, 0) == 0);
		CHECK_FALSE (fs::exists (tex / "Mask" / "13" / "000061" / "000061.dds"));
		CHECK_FALSE (fs::exists (lock / "000061.dds"));
	}
	fs::permissions (lock, fs::perms::owner_all);
}

TEST_CASE ("Refetches wait their turn and never block what the client asks for", "[TileStream]")
{
	App app;
	Tmp t;
	fs::path tex = t.root / "Earth";
	fs::create_directories (tex / "TileStream");
	{
		std::ofstream w (tex / "TileStream" / "written.idx"), r (tex / "TileStream" / "refetch.idx");
		for (int i = 0; i < 300; i++) {
			w << "10 0 " << i << "\n";
			r << "10 0 " << i << "\n";
		}
	}
	Srv s (t.root);
	TileStream::Options o;
	o.cfgfile = s.Cfg (t.root / "ts.cfg", s.url + "/eox");
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth"};
	o.maxActive = 0;
	TileStream ts (o);
	std::this_thread::sleep_for (std::chrono::milliseconds (200));
	CHECK (ts.Pending() == 5); // the required set only
	CHECK (ts.Want ("Earth", 14, 5, 5, 0) == 0);
	CHECK (ts.Pending() == 6);
}
