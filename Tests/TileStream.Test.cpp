// not upstream: the planet tile stream: grids, pixels, DDS, config, size cap, and the service on file:// sources

#include <catch2/catch_test_macros.hpp>
#include "TileStream.h"
#include "TileStreamNet.h"
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
#include <set>
#include <thread>
#include <unistd.h>
#include <zlib.h>

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
	std::string sea = TsImg::Elev (14, 1000, 1500, g, tiles (g, [] (int) { return QColor (127, 206, 0); }), 0.5); // -50 m
	REQUIRE (sea.size() == 100); // flat, header only
	CHECK (U32 (sea, 8) == 0);
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
	CHECK (b[0].cover.kind == "worldcover");
	CHECK (b[0].cover.lmin == 12);
	CHECK (b[0].cover.url.find ("{tile}") != std::string::npos);
	CHECK (b[0].bldgJson == "https://tiles.openfreemap.org/planet");
	CHECK (b[0].bldgUrl.find ("https://tiles.openfreemap.org/planet/") == 0);
	CHECK (b[0].cloudSmall.find ("cloud_combined_2048.jpg") != std::string::npos);
	CHECK (b[0].cloudBig.find ("cloud_combined_8192.tif") != std::string::npos);
	CHECK (b[0].cloudMax == 7);
	// every source's required credit: EOX wording, OSM's ODbL link, WorldCover's sentence, the Terrain Tiles list (tilezen/joerd attribution.md)
	const std::vector<std::string> &cr = b[0].credit;
	auto has = [&] (const std::string &s) { for (const std::string &c : cr) if (c.find (s) != std::string::npos) return true; return false; };
	auto count = [&] (const std::string &s) { int n = 0; for (const std::string &c : cr) n += c.find (s) != std::string::npos; return n; };
	REQUIRE (cr.size() == 19);
	CHECK (cr[0].find ("EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016 & 2017), CC BY 4.0") != std::string::npos);
	CHECK (has ("Blue Marble: Next Generation"));
	CHECK (has ("NASA Black Marble 2016"));
	CHECK (has ("NASA Visible Earth"));
	CHECK (count ("\xc2\xa9 OpenStreetMap contributors, available under the Open Database License https://www.openstreetmap.org/copyright") == 2); // water mask, buildings
	CHECK (has ("\xc2\xa9 ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium"));
	CHECK (has ("OpenFreeMap https://openfreemap.org"));
	CHECK (has ("Terrain Tiles on AWS https://registry.opendata.aws/terrain-tiles: Mapzen"));
	for (const char *t : {"ArcticDEM terrain data", "Australia terrain data", "Austria terrain data", "Canada terrain data", "Europe terrain data",
		"Global ETOPO1 terrain data", "Mexico terrain data", "New Zealand terrain data", "Norway terrain data", "United Kingdom terrain data",
		"United States 3DEP (formerly NED) and global GMTED2010 and SRTM terrain data courtesy of the U.S. Geological Survey."})
		CHECK (count (t) == 1);
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
	bad ("BODY X\nCOVER gibs u 12\nEND\n", "bad COVER");
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

	// its elevation follows as a job of its own
	fs::path elv = tex / "Elev" / "14" / "001000" / "001500.elv";
	REQUIRE (WaitFor ([&] { std::error_code ec; return fs::file_size (elv, ec) == 134262; }, 20.0));
	CHECK_FALSE (fs::exists (tex / "Elev" / "04" / "000000" / "000000.elv")); // no Terrarium tile served: no Elev, the Surf still came

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

	// next session: a cache hit on a tile of ours without Elev has it made
	fs::remove (elv);
	TileStream ts2 (o);
	CHECK (ts2.Want ("Earth", 14, 1000, 1500, 2) == 1);
	REQUIRE (WaitFor ([&] { return fs::exists (elv); }, 20.0));
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
	if (geteuid() == 0) SKIP ("root writes into a read-only folder");
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

// earth-sky ---------------------------------------------------------------

TEST_CASE ("The ground track follows a circular orbit and the planet turning under it", "[TileStream]")
{
	const double mu = 3.986e14, R = 6.371e6, r = R + 400e3, vc = std::sqrt (mu / r), T = 2 * M_PI * std::sqrt (r * r * r / mu);
	double r0[3] = {r, 0, 0}, v0[3] = {0, 0, vc}; // equatorial, eastward (lng = atan2 (z, x))
	auto still = TsTrack::Propagate (r0, v0, mu, 0.0, T / 4, 100);
	REQUIRE (still.size() == 101);
	CHECK (std::fabs (still[0].lng) < 1e-9);
	CHECK (std::fabs (still.back().lng - M_PI / 2) < 1e-6);
	CHECK (std::fabs (still.back().lat) < 1e-9);
	double w = 2 * M_PI / 86164.1;
	auto turning = TsTrack::Propagate (r0, v0, mu, w, T / 4, 100);
	CHECK (std::fabs (turning.back().lng - (M_PI / 2 - w * T / 4)) < 1e-6);
	double v1[3] = {0, vc * std::sin (0.9), vc * std::cos (0.9)}; // 51.6 deg inclined: the top of the track at that latitude
	auto incl = TsTrack::Propagate (r0, v1, mu, 0.0, T / 4, 100);
	CHECK (std::fabs (incl.back().lat - 0.9) < 1e-6);
	double ve[3] = {0, 0, vc * 1.5}; // escape: stepped, still moving east
	auto esc = TsTrack::Propagate (r0, ve, mu, 0.0, 600, 60);
	CHECK (esc.back().lng > 0.1);
	CHECK (TsTrack::Level (400e3, 6, 14) == 11);
	CHECK (TsTrack::Level (100e6, 6, 14) == 6);
	CHECK (TsTrack::Level (100.0, 6, 14) == 14);
}

TEST_CASE ("Ground-track keys run nearest first, one tile to each side, wrapping in longitude", "[TileStream]")
{
	std::vector<TsTrack::Pt> pts;
	for (int i = 0; i <= 40; i++) pts.push_back ({remainder (M_PI - 0.05 + i * 0.0025, 2 * M_PI), 0.001, (double)i});
	auto k = TsTrack::Keys (pts, 8, 1); // L8: 16 x 32 tiles, 11.25 deg
	REQUIRE (!k.empty());
	CHECK (k[0][0] == 8);
	CHECK (k[0][1] == 6);
	CHECK (k[0][2] == 30); // 177 deg east is tile 31; one to the west first
	bool wrapped = false;
	std::set<std::array<int, 3>> s (k.begin(), k.end());
	CHECK (s.size() == k.size());
	for (auto &x : k) { CHECK (x[1] >= 6); CHECK (x[1] <= 8); wrapped |= x[2] == 0; }
	CHECK (wrapped); // across 180 deg to tile 0
	CHECK (TsTrack::Keys (pts, 3, 1).empty());
}

TEST_CASE ("Prefetch waits below the camera's wants, never goes stale, and is capped", "[TileStream]")
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
	size_t base = ts.Pending();
	for (int i = 0; i < 64; i++) CHECK (ts.Prefetch ("Earth", 12, 10, i) == 1);
	CHECK (ts.Prefetch ("Earth", 12, 10, 64) == 0); // full
	CHECK (ts.Prefetch ("Earth", 12, 10, 3) == 1);  // already queued
	CHECK (ts.Prefetch ("Earth", 3, 0, 0) == -1);
	CHECK (ts.Prefetch ("Mars", 12, 0, 0) == -1);
	CHECK (ts.Prefetching() == 64);
	CHECK (ts.Want ("Earth", 12, 10, 5, 0) == 0); // a want takes the prefetch over
	CHECK (ts.Prefetching() == 63);
	ms = 3000;
	CHECK (WaitFor ([&] { return ts.Pending() == base + 63; }, 2.0)); // the want went stale, the prefetches stay
	ms = 14000;
	CHECK (WaitFor ([&] { return ts.Pending() == base; }, 2.0)); // left behind by the track: gone after 10 s
	for (int i = 0; i < 10; i++) CHECK (ts.Prefetch ("Earth", 12, 11, i) == 1);
	for (int i = 0; i < 300; i++) ts.Want ("Earth", 14, 0, i, 0);
	CHECK (ts.Pending() == 256);
	CHECK (ts.Prefetching() == 0); // the cap takes prefetches before wants
}

TEST_CASE ("Prefetch never takes every slot from the camera's wants", "[TileStream]")
{
	App app;
	Tmp t;
	std::string cfg = (t.root / "ts.cfg").string();
	std::ofstream (cfg) << "BODY Earth\nSURF eox bogus://eox/{z}/{row}/{col} 1 16\nLIGHTS gibs bogus://gibs/{n}/{row}/{col} 7\nWATER gibs bogus://gibs/{n}/{row}/{col} 8\nEND\n"; // no such scheme: nothing answers
	fs::create_directories (t.root / "Earth" / "Surf");
	for (int L = 1; L <= 4; L++)
		for (int c = 0; c < (L == 4 ? 2 : 1); c++) {
			fs::path p = t.root / "Earth" / "Surf" / (L < 10 ? "0" + std::to_string (L) : std::to_string (L)) / "000000" / ("00000" + std::to_string (c) + ".dds");
			fs::create_directories (p.parent_path());
			std::ofstream (p) << "x";
		}
	TileStream::Options o;
	o.cfgfile = cfg;
	o.bodyDir = [&] (const std::string &) { return (t.root / "Earth").string(); };
	o.bodies = {"Earth"};
	o.maxActive = 6;
	o.staleSec = 1e9;
	TileStream ts (o);
	for (int i = 0; i < 20; i++) ts.Prefetch ("Earth", 12, 10, i);
	CHECK (ts.Prefetching() >= 18); // at most 6 / 3 = 2 started, the rest wait (and none time out yet)
	CHECK (ts.Want ("Earth", 12, 11, 0, 0) == 0);
	CHECK (ts.Pending() >= 19);
}

TEST_CASE ("A base cfg becomes discs and runway capsules round its point", "[TileStream]")
{
	Tmp t;
	std::string f = (t.root / "B.cfg").string();
	std::ofstream (f) << "BASE-V2.0\nName = B\nLocation = -80.0 +28.0\nSize = 1000\nBEGIN_OBJECTLIST\nBLOCK\n\tPOS 100 0 200\n\tSCALE 30 20 40\nEND\n"
		"LPAD2\n\tPOS -500 0 0\n\tSCALE 2\nEND\nRUNWAY\n\tEND1 0 0 -1000\n\tEND2 0 0 1000\n\tWIDTH 60\nEND\nEND_OBJECTLIST\n";
	TsBase b;
	REQUIRE (TsReadBase (f, b));
	CHECK (std::fabs (b.lng * 180 / M_PI + 80.0) < 1e-9);
	CHECK (std::fabs (b.lat * 180 / M_PI - 28.0) < 1e-9);
	REQUIRE (b.parts.size() == 4);
	CHECK (b.parts[0].n0 == -100); // x is south
	CHECK (b.parts[0].e0 == 200);  // z is east
	CHECK (std::fabs (b.parts[0].r - (25.0 + 15.0)) < 1e-9);
	CHECK (b.parts[1].n0 == 500);
	CHECK (b.parts[1].r == 95.0); // a pad, scaled
	CHECK (b.parts[2].e0 == -1000);
	CHECK (b.parts[2].e1 == 1000);
	CHECK (b.parts[2].r == 50.0);
	CHECK (b.ext >= 1050.0);
	CHECK (!TsReadBase ((t.root / "none.cfg").string(), b));
}

static std::string FlatElv (int L, int ilat, int ilng, double m)
{
	double dh = 180.0 / (1 << (L - 4)), n = 90.0 - ilat * dh, w = -180.0 + ilng * dh;
	std::string out (100, '\0');
	auto i32 = [&out] (size_t at, int32_t v) { memcpy (&out[at], &v, 4); };
	auto f64 = [&out] (size_t at, double v) { memcpy (&out[at], &v, 8); };
	out[0] = 'E'; out[1] = 'L'; out[2] = 'E'; out[3] = 1;
	i32 (4, 100); i32 (8, -16); i32 (12, 259); i32 (16, 259); i32 (20, 1); i32 (24, 1);
	f64 (28, 0.5); f64 (36, 0.0);
	f64 (44, (n - dh) * M_PI / 180); f64 (52, n * M_PI / 180); f64 (60, w * M_PI / 180); f64 (68, (w + dh) * M_PI / 180);
	std::vector<int16_t> z (259 * 259, (int16_t)std::lround (m / 0.5));
	out.append ((const char *)z.data(), z.size() * 2);
	return out;
}

TEST_CASE ("Elev_mod lays a base flat at its height, blends to the terrain and keeps the rest", "[TileStream]")
{
	const double R = 6.37101e6;
	int L = 14, ilat = 400, ilng = 1000;
	double dh = 180.0 / (1 << (L - 4));
	TsBase b;
	b.lat = (90.0 - ilat * dh - 120.5 * dh / 256.0) * M_PI / 180.0; // inside the tile, about 20 deg north
	b.lng = (-180.0 + ilng * dh + 99.5 * dh / 256.0) * M_PI / 180.0;
	b.parts.push_back ({0, 0, 0, 0, 300.0});
	b.ext = 300.0;
	std::string elv = FlatElv (L, ilat, ilng, 100.0);
	CHECK (std::fabs (TsSky::ElvAt (elv, b.lng, b.lat) - 100.0) < 1e-9);
	CHECK (TsSky::Hits (b, R, L, ilat, ilng, 150.0));
	CHECK (!TsSky::Hits (b, R, L, ilat, ilng + 2, 150.0));
	std::string m = TsSky::ElevMod (L, ilat, ilng, {{&b, 20.0}}, R, 0.5, elv, 150.0);
	REQUIRE (m.size() == 100 + 259 * 259 * 2);
	int32_t dt;
	memcpy (&dt, &m[8], 4);
	CHECK (dt == -16);
	auto node = [&] (int i, int j) { int16_t q; memcpy (&q, &m[100 + ((size_t)i * 259 + j) * 2], 2); return q; };
	// the node nearest the base: row from the south, column from the west
	double spacing = dh / 256.0 * M_PI / 180.0 * R;
	int ci = (int)std::lround ((b.lat * 180 / M_PI - (90.0 - (ilat + 1) * dh)) / (dh / 256.0)) + 1, cj = (int)std::lround ((b.lng * 180 / M_PI + 180.0 - ilng * dh) / (dh / 256.0)) + 1;
	CHECK (node (ci, cj) == 40); // 20 m
	CHECK (node (ci, cj + (int)(250.0 / (spacing * std::cos (b.lat)))) == 40);
	int ring = (int)std::lround (375.0 / spacing);
	int16_t mid = node (ci + ring, cj);
	CHECK (mid > 40);
	CHECK (mid < 200);
	CHECK (node (ci + (int)std::lround (600.0 / spacing), cj) == SHRT_MAX);
	CHECK (node (0, 258) == SHRT_MAX);
	CHECK (TsSky::ElevMod (L, ilat, ilng + 5, {{&b, 20.0}}, R, 0.5, elv, 150.0).empty());
	CHECK (std::isnan (TsSky::ElvAt (elv, b.lng + 1.0, b.lat)));
}

TEST_CASE ("Cloud tiles are white DXT5 with the image's density in alpha", "[TileStream]")
{
	QImage src (2048, 1024, QImage::Format_Grayscale8);
	for (int y = 0; y < 1024; y++) memset (src.scanLine (y), y < 512 ? 230 : 0, 2048);
	QImage l1 = TsSky::Cloud (src, 1, 0, 0);
	CHECK (l1.width() == 128);
	QImage l5 = TsSky::Cloud (src, 5, 0, 1), l5s = TsSky::Cloud (src, 5, 1, 1), l9 = TsSky::Cloud (src, 9, 3, 7);
	CHECK (l5.width() == 512);
	CHECK (qAlpha (l5.pixel (100, 100)) > 200);
	CHECK (qRed (l5.pixel (100, 100)) == 255);
	CHECK (qAlpha (l5s.pixel (100, 100)) == 0);
	CHECK (l9.width() == 512); // upsampled
	std::string d = TsSky::Dds5 (l1);
	CHECK (d.compare (84, 4, "DXT5") == 0);
	CHECK (U32 (d, 28) == 8);
	size_t sz = 128;
	for (int s = 128; s >= 1; s /= 2) sz += (size_t)std::max (1, s / 4) * std::max (1, s / 4) * 16;
	CHECK (d.size() == sz);
	CHECK ((uchar)d[128] > 200); // the north half's first block: alpha endpoints high
}

// a tiled 8-bit TIFF, one IFD per level (the first full size, the rest overviews), deflate and predictor 2
struct CogLevel { int size; std::vector<uint8_t> px; };

static QByteArray Cog (const std::vector<CogLevel> &lv, int tile, bool big)
{
	QByteArray out;
	auto put = [&] (uint64_t v, int n) { for (int i = 0; i < n; i++) out.append ((char)(v >> (8 * i))); };
	auto at = [&] (size_t pos, uint64_t v, int n) { for (int i = 0; i < n; i++) out[(qsizetype)(pos + i)] = (char)(v >> (8 * i)); };
	int fw = big ? 8 : 4;
	out.append ("II");
	put (big ? 43 : 42, 2);
	if (big) { put (8, 2); put (0, 2); }
	size_t next = (size_t)out.size();
	put (0, fw);
	for (size_t li = 0; li < lv.size(); li++) {
		const CogLevel &l = lv[li];
		int nt = (l.size + tile - 1) / tile;
		std::vector<uint64_t> offs, cnts;
		for (int ty = 0; ty < nt; ty++)
			for (int tx = 0; tx < nt; tx++) {
				std::vector<uint8_t> raw ((size_t)tile * tile, 0);
				for (int y = 0; y < tile; y++)
					for (int x = 0; x < tile; x++) {
						int gx = tx * tile + x, gy = ty * tile + y;
						if (gx < l.size && gy < l.size) raw[(size_t)y * tile + x] = l.px[(size_t)gy * l.size + gx];
					}
				for (int y = 0; y < tile; y++)
					for (int x = tile - 1; x > 0; x--) raw[(size_t)y * tile + x] -= raw[(size_t)y * tile + x - 1];
				QByteArray z = qCompress (raw.data(), (qsizetype)raw.size()).mid (4);
				offs.push_back ((uint64_t)out.size());
				cnts.push_back ((uint64_t)z.size());
				out.append (z);
			}
		int ot = big ? 16 : 4, os = big ? 8 : 4;
		size_t offArr = (size_t)out.size();
		for (uint64_t o : offs) put (o, os);
		size_t cntArr = (size_t)out.size();
		for (uint64_t c : cnts) put (c, os);
		if (out.size() & 1) out.append ('\0');
		at (next, (uint64_t)out.size(), fw);
		struct E { int tag, type; uint64_t count, val; };
		uint64_t n = offs.size();
		std::vector<E> es = {{254, 4, 1, li ? 1u : 0u}, {256, 3, 1, (uint64_t)l.size}, {257, 3, 1, (uint64_t)l.size}, {258, 3, 1, 8}, {259, 3, 1, 8},
			{262, 3, 1, 1}, {277, 3, 1, 1}, {317, 3, 1, 2}, {322, 3, 1, (uint64_t)tile}, {323, 3, 1, (uint64_t)tile},
			{324, ot, n, n == 1 ? offs[0] : offArr}, {325, ot, n, n == 1 ? cnts[0] : cntArr}};
		put (es.size(), big ? 8 : 2);
		for (const E &e : es) {
			put ((uint64_t)e.tag, 2);
			put ((uint64_t)e.type, 2);
			put (e.count, fw);
			int ts = e.type == 3 ? 2 : e.type == 4 ? 4 : 8;
			if (e.count * ts <= (uint64_t)fw) { put (e.val, ts); put (0, fw - ts); }
			else put (e.val, fw);
		}
		next = (size_t)out.size();
		put (0, fw);
	}
	return out;
}

static std::vector<uint8_t> Pattern (int size)
{
	std::vector<uint8_t> v ((size_t)size * size);
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++) v[(size_t)y * size + x] = (uint8_t)(x * 7 + y * 3);
	return v;
}

TEST_CASE ("A COG's IFDs parse, overviews included, and its tiles inflate with predictor 2 undone", "[TileStream]")
{
	for (bool big : {false, true}) {
		QByteArray f = Cog ({{64, Pattern (64)}, {32, Pattern (32)}}, 32, big);
		std::vector<TsCog::Ifd> ifd;
		CHECK (TsCog::Parse (f.left (12), ifd) > 12); // the header asks for more
		REQUIRE (TsCog::Parse (f, ifd) == 0);
		REQUIRE (ifd.size() == 2);
		CHECK (ifd[0].w == 64);
		CHECK (ifd[0].tw == 32);
		CHECK (ifd[0].comp == 8);
		CHECK (ifd[0].pred == 2);
		CHECK (ifd[0].off.size() == 4);
		CHECK (ifd[1].w == 32);
		CHECK (ifd[1].off.size() == 1);
		std::vector<uint8_t> px;
		const TsCog::Ifd &a = ifd[0];
		REQUIRE (TsCog::Decode (a, f.mid ((qsizetype)a.off[3], (qsizetype)a.cnt[3]), px)); // tile (1, 1)
		REQUIRE (px.size() == 32 * 32);
		std::vector<uint8_t> p = Pattern (64);
		bool same = true;
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++) same = same && px[(size_t)y * 32 + x] == p[(size_t)(32 + y) * 64 + 32 + x];
		CHECK (same);
		CHECK_FALSE (TsCog::Decode (a, QByteArray ("junk"), px));
	}
	std::vector<TsCog::Ifd> ifd;
	CHECK (TsCog::Parse (QByteArray (64, 'x'), ifd) == -1);
	CHECK (TsCog::CellName (27, -81) == "N27W081");
	CHECK (TsCog::CellName (-3, 6) == "S03E006");
}

TEST_CASE ("Byte ranges of a file:// URL", "[TileStream]")
{
	App app;
	Tmp t;
	QByteArray data;
	for (int i = 0; i < 100000; i++) data.append ((char)(i * 31));
	fs::path p = t.root / "f.bin";
	{
		std::ofstream o (p, std::ios::binary);
		o.write (data.constData(), data.size());
	}
	TsNet net ("test");
	auto get = [&] (const std::string &url, int64_t off, int64_t len, QByteArray &body) {
		std::atomic<int> st {-1};
		net.Get (url, [&] (int s, QByteArray b, int) { body = b; st = s; }, off, len);
		WaitFor ([&] { return st >= 0; }, 5.0);
		return st.load();
	};
	QByteArray b;
	CHECK (get ("file://" + p.string(), 1000, 50, b) == 200);
	CHECK (b == data.mid (1000, 50));
	CHECK (get ("file://" + p.string(), 99990, 50, b) == 200);
	CHECK (b == data.mid (99990));
	CHECK (get ("file://" + p.string(), 200000, 50, b) == 416);
	CHECK (get ("file://" + (t.root / "none.bin").string(), 0, 50, b) == 404);
	CHECK (get ("file://" + p.string(), -1, 0, b) == 200); // the whole file as before
	CHECK (b == data);
}

// a WorldCover-like cell: west half water (80), east half trees (10)
static void WcCell (const fs::path &p)
{
	auto half = [] (int s) {
		std::vector<uint8_t> v ((size_t)s * s);
		for (int y = 0; y < s; y++)
			for (int x = 0; x < s; x++) v[(size_t)y * s + x] = x < s / 2 ? 80 : 10;
		return v;
	};
	QByteArray f = Cog ({{64, half (64)}, {32, half (32)}}, 32, false);
	fs::create_directories (p.parent_path());
	std::ofstream o (p, std::ios::binary);
	o.write (f.constData(), f.size());
}

TEST_CASE ("WorldCover classes over an Orbiter tile: water bit, open sea, class tile", "[TileStream]")
{
	App app;
	Tmp t;
	WcCell (t.root / "wc" / "c_N27W081.tif");
	std::string tmpl = "file://" + (t.root / "wc").string() + "/c_{tile}.tif";
	TsNet net ("test");
	int reads = 0;
	TsCog::Fetch fetch = [&] (std::vector<TsCog::Req> &rq) {
		std::atomic<int> left {(int)rq.size()};
		for (TsCog::Req &r : rq) {
			reads++;
			net.Get (r.url, [&r, &left] (int s, QByteArray b, int) { r.status = s; r.data = std::make_shared<const QByteArray> (b); left--; }, (int64_t)r.off, (int64_t)r.len);
		}
		WaitFor ([&] { return left == 0; }, 5.0);
	};
	// L12 row 86 col 142: lat 28.83..29.53, lng -80.16..-79.45; the cell's half line at -79.5
	std::vector<uint8_t> c;
	REQUIRE (TsCog::Classes (tmpl, 12, 86, 142, 512, fetch, c));
	REQUIRE (c.size() == 512 * 512);
	CHECK (c[100 * 512 + 10] == 80);
	CHECK (c[100 * 512 + 470] == 80);
	CHECK (c[100 * 512 + 490] == 10);
	CHECK (c[100 * 512 + 511] == 10);
	CHECK (reads == 3); // a header and the two full-size tiles of its row
	int again = reads;
	REQUIRE (TsCog::Classes (tmpl, 12, 86, 142, 512, fetch, c));
	CHECK (reads == again); // headers and decoded tiles kept in memory
	// col 145: lng -78.05..-77.34, east of -78 the N27W078 cell is missing: open sea
	REQUIRE (TsCog::Classes (tmpl, 12, 86, 145, 512, fetch, c));
	CHECK (c[200 * 512 + 10] == 10);
	CHECK (c[200 * 512 + 300] == 0);
	CHECK (TsCog::IsWater (c[200 * 512 + 300]));
	// outside WorldCover (Antarctica): 255, the GIBS water stays
	std::vector<uint8_t> s;
	REQUIRE (TsCog::Classes (tmpl, 12, 240, 10, 512, fetch, s));
	CHECK (s[0] == 255);
	QImage gibs (512, 512, QImage::Format_RGB888);
	gibs.fill (QColor (30, 30, 30));
	QImage w = TsCog::Water (s, 512, gibs);
	CHECK (w.pixelColor (5, 5).red() == 30);
	// the mask: transparent over WorldCover water, opaque over trees
	w = TsCog::Water (c, 512, gibs);
	QImage lights (512, 512, QImage::Format_RGB888);
	lights.fill (QColor (50, 50, 50));
	int black[3] = {0, 0, 0};
	QImage m = TsImg::Mask (lights, w, black, 101.5);
	CHECK (m.pixelColor (10, 200).alpha() == 255);
	CHECK (m.pixelColor (300, 200).alpha() == 0);
	// the class tile: trees red, sea black
	QImage k = TsCog::ClassImage (c, 512, 256);
	REQUIRE (k.width() == 256);
	CHECK (k.pixelColor (2, 100).red() == 255);
	CHECK (k.pixelColor (200, 100) == QColor (0, 0, 0));
	std::string d = TsImg::Dds (k, false);
	CHECK (d.size() == 43832); // 256 px DXT1 with nine mips
	// a read that fails: no classes
	TsCog::Fetch bad = [] (std::vector<TsCog::Req> &rq) { for (TsCog::Req &r : rq) r.status = 0; };
	CHECK_FALSE (TsCog::Classes ("file:///nowhere/{tile}.tif", 12, 86, 142, 512, bad, c));
}

// ---- buildings (TileStreamMvt.cpp) ----

namespace {
void PVar (std::string &s, uint64_t v) { do { uint8_t b = v & 0x7F; v >>= 7; s += (char)(b | (v ? 0x80 : 0)); } while (v); }
void PKey (std::string &s, int f, int wt) { PVar (s, (uint64_t)f << 3 | wt); }
void PBytes (std::string &s, int f, const std::string &b) { PKey (s, f, 2); PVar (s, b.size()); s += b; }
uint32_t Zig (int32_t n) { return (uint32_t)((n << 1) ^ (n >> 31)); }

// MoveTo, LineTo, ClosePath for each ring, absolute tile points to deltas
std::string Geom (const std::vector<std::vector<std::pair<int, int>>> &rings)
{
	std::string g;
	int x = 0, y = 0;
	for (const auto &r : rings) {
		PVar (g, 1 | 1 << 3);
		PVar (g, Zig (r[0].first - x)); PVar (g, Zig (r[0].second - y));
		x = r[0].first; y = r[0].second;
		PVar (g, 2 | (uint32_t)(r.size() - 1) << 3);
		for (size_t i = 1; i < r.size(); i++) {
			PVar (g, Zig (r[i].first - x)); PVar (g, Zig (r[i].second - y));
			x = r[i].first; y = r[i].second;
		}
		PVar (g, 7 | 1 << 3);
	}
	return g;
}

std::string Feat (const std::vector<uint32_t> &tags, const std::string &geom)
{
	std::string f, t;
	for (uint32_t v : tags) PVar (t, v);
	PBytes (f, 2, t);
	PKey (f, 3, 0); PVar (f, 3);
	PBytes (f, 4, geom);
	return f;
}

// a water layer to skip, then buildings: a 100 x 100 block with a 40 x 40 court (30 m, from 10 m), and a hidden one
std::string TestPbf ()
{
	std::string water, l, v, tile;
	PBytes (water, 1, "water");
	PBytes (water, 2, Feat ({}, Geom ({{{0, 0}, {10, 0}, {10, 10}}})));
	PKey (water, 15, 0); PVar (water, 2);
	PBytes (tile, 3, water);
	PKey (l, 15, 0); PVar (l, 2);
	PBytes (l, 1, "building");
	auto ring = std::vector<std::pair<int, int>> {{1000, 1000}, {1100, 1000}, {1100, 1100}, {1000, 1100}};
	auto hole = std::vector<std::pair<int, int>> {{1030, 1030}, {1030, 1070}, {1070, 1070}, {1070, 1030}};
	PBytes (l, 2, Feat ({0, 0, 1, 1}, Geom ({ring, hole})));
	PBytes (l, 2, Feat ({0, 0, 2, 2}, Geom ({{{2000, 2000}, {2100, 2000}, {2100, 2100}, {2000, 2100}}})));
	PBytes (l, 3, "render_height");
	PBytes (l, 3, "render_min_height");
	PBytes (l, 3, "hide_3d");
	std::string v0, v1, v2;
	double h = 30.0;
	PKey (v0, 3, 1); v0.append ((const char*)&h, 8);
	PKey (v1, 5, 0); PVar (v1, 10);
	PKey (v2, 7, 0); PVar (v2, 1);
	PBytes (l, 4, v0); PBytes (l, 4, v1); PBytes (l, 4, v2);
	PKey (l, 5, 0); PVar (l, 4096);
	PBytes (tile, 3, l);
	return tile;
}

double TriArea (const std::vector<std::vector<TsBldg::Pt>> &rings, const std::vector<uint32_t> &tri, bool &cw)
{
	std::vector<TsBldg::Pt> all;
	for (const auto &r : rings) all.insert (all.end(), r.begin(), r.end());
	double a = 0.0;
	cw = true;
	for (size_t i = 0; i + 2 < tri.size(); i += 3) {
		const TsBldg::Pt &p = all[tri[i]], &q = all[tri[i + 1]], &r = all[tri[i + 2]];
		double c = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
		if (c > 0) cw = false;
		a += -0.5 * c;
	}
	return a;
}
}

TEST_CASE ("The MVT decoder reads the building layer", "[TileStream]")
{
	std::vector<TsBldg::Feature> f;
	uint32_t ext = 0;
	REQUIRE (TsBldg::Decode (TestPbf(), f, ext));
	CHECK (ext == 4096);
	REQUIRE (f.size() == 2);
	CHECK (f[0].height == 30.0);
	CHECK (f[0].minHeight == 10.0);
	CHECK_FALSE (f[0].hide);
	REQUIRE (f[0].rings.size() == 2);
	CHECK (f[0].rings[0].size() == 4);
	CHECK (f[0].rings[1][1].x == 1030);
	CHECK (f[0].rings[1][1].y == 1070);
	CHECK (f[1].hide);
	std::string bad = TestPbf();
	bad.resize (bad.size() - 7);
	CHECK_FALSE (TsBldg::Decode (bad, f, ext));
}

TEST_CASE ("Ear clipping covers the polygon, holes included, clockwise", "[TileStream]")
{
	std::vector<uint32_t> tri;
	bool cw;
	std::vector<std::vector<TsBldg::Pt>> sq {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, {{30, 30}, {70, 30}, {70, 70}, {30, 70}}};
	REQUIRE (TsBldg::Triangulate (sq, tri));
	CHECK (tri.size() == 8 * 3);
	CHECK (std::fabs (TriArea (sq, tri, cw) - 8400.0) < 1e-6);
	CHECK (cw);
	std::vector<std::vector<TsBldg::Pt>> ell {{{0, 0}, {0, 20}, {10, 20}, {10, 10}, {20, 10}, {20, 0}}}; // clockwise L
	REQUIRE (TsBldg::Triangulate (ell, tri));
	CHECK (tri.size() == 4 * 3);
	CHECK (std::fabs (TriArea (ell, tri, cw) - 300.0) < 1e-6);
	CHECK (cw);
}

TEST_CASE ("Building meshes extrude, round-trip through the file and inflate from gzip", "[TileStream]")
{
	std::vector<TsBldg::Feature> f;
	uint32_t ext;
	REQUIRE (TsBldg::Decode (TestPbf(), f, ext));
	TsBldg::Mesh m = TsBldg::Build (14, 4824, 6157, f, ext);
	REQUIRE (m.b.size() == 1); // the hidden one is left out
	const TsBldg::Building &b = m.b[0];
	CHECK (b.hmin == 10.0f);
	CHECK (b.hmax == 30.0f);
	CHECK (b.nv == 8 * 4 + 8);
	CHECK (b.ni == 8 * 6 + 8 * 3);
	double lat, lng;
	TsBldg::LatLng (14, 4824 + 1050.0 / 4096, 6157 + 1050.0 / 4096, lat, lng);
	CHECK (std::fabs (b.lat - lat) < 1e-9);
	CHECK (std::fabs (b.lng - lng) < 1e-9);
	int x, y;
	TsBldg::TileOf (lat, lng, 14, x, y);
	CHECK (x == 4824);
	CHECK (y == 6157);
	float span = 0, hmax = 0;
	for (uint32_t i = 0; i < b.nv; i++) {
		span = std::max (span, std::fabs (m.vtx[(b.v0 + i) * 6]));
		hmax = std::max (hmax, m.vtx[(b.v0 + i) * 6 + 1]);
	}
	CHECK (span > 21.0f); // 100 units of 4096 at z14, 40.75 N: 45 m across
	CHECK (span < 24.0f);
	CHECK (hmax == 30.0f);
	std::string bytes = TsBldg::Encode (m);
	TsBldg::Mesh r;
	REQUIRE (TsBldg::Read (bytes, r));
	CHECK (r.x == 4824);
	CHECK (r.b.size() == 1);
	CHECK (r.vtx == m.vtx);
	CHECK (r.idx == m.idx);
	CHECK_FALSE (TsBldg::Read (bytes.substr (0, bytes.size() - 1), r));
	TsBldg::Mesh empty;
	REQUIRE (TsBldg::Read (TsBldg::Encode (empty), r));
	CHECK (r.b.empty());
	std::string pbf = TestPbf(), gz (pbf.size() + 128, '\0'), out;
	z_stream s {};
	REQUIRE (deflateInit2 (&s, 6, Z_DEFLATED, 16 + 15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
	s.next_in = (Bytef*)pbf.data(); s.avail_in = (uInt)pbf.size();
	s.next_out = (Bytef*)gz.data(); s.avail_out = (uInt)gz.size();
	REQUIRE (deflate (&s, Z_FINISH) == Z_STREAM_END);
	gz.resize (s.total_out);
	deflateEnd (&s);
	REQUIRE (TsBldg::Inflate (gz, out));
	CHECK (out == pbf);
}

TEST_CASE ("The service writes building tiles from file:// sources", "[TileStream]")
{
	App app;
	Tmp t;
	fs::path src = t.root / "bldg", tex = t.root / "Earth";
	fs::create_directories (src / "14" / "4824");
	std::ofstream (src / "14" / "4824" / "6157.pbf", std::ios::binary) << TestPbf();
	std::ofstream (t.root / "tiles.json") << "{\"tiles\":[\"file://" << src.string() << "/{z}/{x}/{y}.pbf\"],\"maxzoom\":14}";
	std::string cfg = (t.root / "ts.cfg").string();
	std::ofstream (cfg) << "BODY Earth\nSURF eox file:///none/{z}/{row}/{col} 1 16\nLIGHTS gibs file:///none/{n}/{row}/{col} 7\nWATER gibs file:///none/{n}/{row}/{col} 8\n"
		"BLDG file://" << (t.root / "tiles.json").string() << " file:///none/{z}/{x}/{y}.pbf\nEND\n";
	TileStream::Options o;
	o.cfgfile = cfg;
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth"};
	o.staleSec = 30.0;
	{
		TileStream ts (o);
		CHECK (ts.WantBldg ("Earth", 4824, 6157) == 0);
		CHECK (ts.WantBldg ("Earth", 4824, 6158) == 0); // no file: written empty
		CHECK (ts.WantBldg ("Earth", -1, 0) == -1);
		CHECK (ts.WantBldg ("Mars", 0, 0) == -1);
		CHECK (WaitFor ([&] { return ts.WantBldg ("Earth", 4824, 6157) == 1 && ts.WantBldg ("Earth", 4824, 6158) == 1; }, 20.0));
		TsBldg::Mesh m;
		REQUIRE (TsBldg::Read (Read (ts.BldgPath ("Earth", 4824, 6157)), m));
		CHECK (m.b.size() == 1);
		REQUIRE (TsBldg::Read (Read (ts.BldgPath ("Earth", 4824, 6158)), m));
		CHECK (m.b.empty());
	}
	CHECK (Read (tex / "TileStream" / "bldg.idx").find ("14 4824 6157") != std::string::npos);
	o.buildings = false;
	TileStream off (o);
	CHECK (off.WantBldg ("Earth", 4824, 6157) == -1);
}

TEST_CASE ("The size cap takes building tiles too", "[TileStream]")
{
	App app;
	Tmp t;
	fs::path tex = t.root / "Earth";
	std::string cfg = (t.root / "ts.cfg").string();
	std::ofstream (cfg) << "BODY Earth\nSURF eox file:///none/{z}/{row}/{col} 1 16\nLIGHTS gibs file:///none/{n}/{row}/{col} 7\nWATER gibs file:///none/{n}/{row}/{col} 8\nBLDG file:///none.json file:///none/{z}/{x}/{y}.pbf\nEND\n";
	std::string mb (1 << 20, 'x');
	auto now = fs::file_time_type::clock::now();
	for (int i = 0; i < 4; i++) {
		fs::path p = tex / "Bldg" / "14" / "10" / (std::to_string (i) + ".bld");
		fs::create_directories (p.parent_path());
		std::ofstream (p, std::ios::binary) << mb;
		fs::last_write_time (p, now - std::chrono::hours (10 - i));
	}
	fs::create_directories (tex / "TileStream");
	std::ofstream (tex / "TileStream" / "bldg.idx") << "14 10 0\n14 10 1\n14 10 2\n";
	TileStream::Options o;
	o.cfgfile = cfg;
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth"};
	o.capMB = 2;
	o.offline = true;
	{
		TileStream ts (o);
		ts.Evict();
		std::this_thread::sleep_for (std::chrono::milliseconds (300));
	}
	CHECK_FALSE (fs::exists (tex / "Bldg" / "14" / "10" / "0.bld"));
	CHECK_FALSE (fs::exists (tex / "Bldg" / "14" / "10" / "1.bld"));
	CHECK (fs::exists (tex / "Bldg" / "14" / "10" / "2.bld"));
	CHECK (fs::exists (tex / "Bldg" / "14" / "10" / "3.bld")); // not ours
	CHECK (Read (tex / "TileStream" / "bldg.idx") == "14 10 2\n");
}

// not upstream: Launchpad cache controls and region prefetch

static bool HasKey (const std::vector<TileStream::TileKey> &v, int L, int la, int ln)
{
	for (const TileStream::TileKey &k : v) if (k.L == L && k.ilat == la && k.ilng == ln) return true;
	return false;
}

TEST_CASE ("A region lists its tiles coarse to fine, inside the grid and around the centre", "[TileStream]")
{
	double lat = 28.5244, lng = -80.6508; // KSC
	std::vector<TileStream::TileKey> v = TileStream::RegionKeys (lat, lng, 5.0, 15);
	REQUIRE (!v.empty());
	CHECK (v.front().L == 1);
	CHECK (v.back().L == 15);
	int n15 = 0;
	for (size_t i = 0; i < v.size(); i++) {
		const TileStream::TileKey &k = v[i];
		if (i) CHECK (k.L >= v[i - 1].L);
		if (k.L < 4) { CHECK ((k.ilat == 0 && k.ilng == 0)); continue; }
		CHECK (k.ilat >= 0);
		CHECK (k.ilat < 1 << (k.L - 4));
		CHECK (k.ilng >= 0);
		CHECK (k.ilng < 1 << (k.L - 3));
		double sz = 180.0 / (1 << (k.L - 4)), n = 90.0 - k.ilat * sz, w = -180.0 + k.ilng * sz;
		CHECK (n - sz <= lat + 0.05); // 5 km is 0.045 degrees
		CHECK (n >= lat - 0.05);
		CHECK (w <= lng + 0.06);
		CHECK (w + sz >= lng - 0.06);
		if (k.L == 15) n15++;
	}
	CHECK (n15 >= 1);
	CHECK (n15 <= 9);
	for (int L = 4; L <= 15; L++) {
		double sz = 180.0 / (1 << (L - 4));
		CHECK (HasKey (v, L, (int)std::floor ((90.0 - lat) / sz), (int)std::floor ((lng + 180.0) / sz)));
	}
	size_t n16 = 0;
	for (const TileStream::TileKey &k : TileStream::RegionKeys (lat, lng, 20.0, 16)) if (k.L == 16) n16++;
	CHECK (n16 >= 50); // 1257 km^2 over 4.9 x 4.3 km tiles, plus the rim
	CHECK (n16 <= 120);
	std::vector<TileStream::TileKey> one = TileStream::RegionKeys (10.1, 20.1, 0.0, 12);
	CHECK (one.size() == 12);
	std::vector<TileStream::TileKey> dl = TileStream::RegionKeys (0.5, 179.99, 50.0, 10); // across the date line
	CHECK (HasKey (dl, 10, 31, 0));
	CHECK (HasKey (dl, 10, 31, 127));
	int row0 = 0;
	for (const TileStream::TileKey &k : TileStream::RegionKeys (89.9, 10.0, 50.0, 8)) if (k.L == 8 && k.ilat == 0) row0++;
	CHECK (row0 == 32); // over the pole: every longitude
	CHECK (TileStream::RegionKeys (95.0, 0.0, 5.0, 10).empty());
	CHECK (TileStream::RegionKeys (60.0, 0.0, 400.0, 18).size() == 100000); // cut off
}

// a written.idx with L8, L9 and L12 tiles of ours, one of someone else's, and an Archive
static fs::path CacheDir (const fs::path &root)
{
	fs::path tex = root / "Earth";
	auto put = [&] (const char *layer, const char *rel, size_t n) {
		fs::path p = tex / layer / rel;
		fs::create_directories (p.parent_path());
		std::ofstream (p, std::ios::binary) << std::string (n, 'x');
	};
	put ("Surf", "09/000000/000000.dds", 1000);
	put ("Mask", "09/000000/000000.dds", 1000);
	put ("Surf", "12/000001/000002.dds", 1000);
	put ("Mask", "12/000001/000002.dds", 1000);
	put ("Elev", "12/000001/000002.elv", 500);
	put ("Surf", "08/000000/000000.dds", 200);
	put ("Surf", "09/000000/000001.dds", 300); // not ours
	put ("Archive", "Surf.tree", 700);
	fs::create_directories (tex / "TileStream" / "tmp");
	std::ofstream (tex / "TileStream" / "written.idx") << "9 0 0\n12 1 2\n8 0 0\n13 5 5\n";
	std::ofstream (tex / "TileStream" / "refetch.idx") << "9 0 0\n8 0 0\n";
	std::ofstream (tex / "TileStream" / "none.idx") << "14 1 1 20000\n";
	std::ofstream (tex / "TileStream" / "ocean.idx") << "10 3 3\n";
	std::ofstream (tex / "TileStream" / "tmp" / "123.4.tmp") << "x";
	return tex;
}

TEST_CASE ("The cache size counts the streamed tiles and every loose tile", "[TileStream]")
{
	Tmp t;
	fs::path tex = CacheDir (t.root);
	uint64_t total = 0;
	size_t tiles = 0;
	CHECK (TileStream::CacheBytes (tex.string() + "/", &total, &tiles) == 4700);
	CHECK (tiles == 3);
	CHECK (total == 5000);
	CHECK (TileStream::CacheBytes ((t.root / "none").string()) == 0);
}

TEST_CASE ("Clearing removes only the streamed tiles, base levels on request, and rewrites the indexes", "[TileStream]")
{
	Tmp t;
	fs::path tex = CacheDir (t.root), ts = tex / "TileStream";
	CHECK (TileStream::Clear (tex.string(), true) == 2);
	CHECK_FALSE (fs::exists (tex / "Surf" / "09" / "000000" / "000000.dds"));
	CHECK_FALSE (fs::exists (tex / "Mask" / "12" / "000001" / "000002.dds"));
	CHECK_FALSE (fs::exists (tex / "Elev" / "12" / "000001" / "000002.elv"));
	CHECK_FALSE (fs::exists (tex / "Surf" / "12" / "000001")); // its empty row folder
	CHECK (fs::exists (tex / "Surf" / "08" / "000000" / "000000.dds"));
	CHECK (fs::exists (tex / "Surf" / "09" / "000000" / "000001.dds"));
	CHECK (fs::exists (tex / "Archive" / "Surf.tree"));
	CHECK (Read (ts / "written.idx") == "8 0 0\n");
	CHECK (Read (ts / "refetch.idx") == "8 0 0\n");
	CHECK_FALSE (fs::exists (ts / "none.idx"));
	CHECK (fs::exists (ts / "ocean.idx"));
	CHECK (fs::is_empty (ts / "tmp"));
	CHECK (TileStream::Clear (tex.string(), false) == 1);
	CHECK_FALSE (fs::exists (tex / "Surf" / "08" / "000000" / "000000.dds"));
	CHECK (fs::exists (tex / "Surf" / "09" / "000000" / "000001.dds"));
	CHECK (fs::exists (tex / "Archive" / "Surf.tree"));
	CHECK (Read (ts / "written.idx").empty());
	CHECK_FALSE (fs::exists (ts / "ocean.idx"));
	CHECK (TileStream::Clear ((t.root / "none").string(), false) == 0);
	CHECK_FALSE (fs::exists (t.root / "none" / "TileStream" / "written.idx"));
}

TEST_CASE ("A region prefetch from file:// sources fetches every tile coarse to fine and can be cancelled", "[TileStream]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	s.Required (true);
	std::vector<TileStream::TileKey> keys = TileStream::RegionKeys (0.0, 0.0, 50.0, 9); // on a corner: 4 tiles a level from L5
	REQUIRE (keys.size() == 25);
	for (const TileStream::TileKey &k : keys)
		if (k.L >= 5) {
			s.EoxTile (k.L, k.ilat, k.ilng, QColor (120, 100, 80));
			s.Masks (k.L, k.ilat, k.ilng);
		}
	fs::path tex = t.root / "Earth";
	TileStream::Options o;
	o.cfgfile = s.Cfg (t.root / "ts.cfg", s.url + "/eox");
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	o.offline = true; // a prefetch is always online
	TileStream::Progress p;
	std::atomic<bool> cancel {false};
	CHECK (TileStream::Prefetch (o, "Earth", keys, p, cancel, 20.0) == 0);
	CHECK (p.total == 25);
	CHECK (p.done == 25);
	CHECK (p.have == 25);
	CHECK (p.none == 0);
	CHECK (p.bytes >= 20u * 2 * 174904);
	for (const TileStream::TileKey &k : keys) {
		char rel[40];
		snprintf (rel, sizeof rel, "%02d/%06d/%06d.dds", k.L, k.ilat, k.ilng);
		CHECK (fs::exists (tex / "Surf" / rel));
		CHECK (fs::exists (tex / "Mask" / rel));
	}
	size_t tiles = 0;
	CHECK (TileStream::CacheBytes (tex.string(), nullptr, &tiles) > 0);
	CHECK (tiles == 25);
	TileStream::Progress q;
	CHECK (TileStream::Prefetch (o, "Earth", keys, q, cancel, 20.0) == 0); // all on disk
	CHECK (q.have == 25);
	cancel = true;
	TileStream::Progress c;
	CHECK (TileStream::Prefetch (o, "Earth", TileStream::RegionKeys (45.0, 45.0, 50.0, 9), c, cancel, 20.0) == 1);
	o.cfgfile = (t.root / "nothing.cfg").string();
	cancel = false;
	CHECK (TileStream::Prefetch (o, "Earth", keys, c, cancel, 20.0) == 3);
}

TEST_CASE ("A region prefetch cancelled from another thread after its first tiles stops within a second and keeps them", "[TileStream]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	s.Required (true);
	std::vector<TileStream::TileKey> keys = TileStream::RegionKeys (0.0, 0.0, 50.0, 12);
	REQUIRE (keys.size() > 25);
	for (const TileStream::TileKey &k : keys)
		if (k.L >= 5) {
			s.EoxTile (k.L, k.ilat, k.ilng, QColor (120, 100, 80));
			s.Masks (k.L, k.ilat, k.ilng);
		}
	fs::path tex = t.root / "Earth";
	TileStream::Options o;
	o.cfgfile = s.Cfg (t.root / "ts.cfg", s.url + "/eox");
	o.bodyDir = [&] (const std::string &) { return tex.string(); };
	TileStream::Progress p;
	std::atomic<bool> cancel {false};
	std::atomic<int> result {-1};
	std::chrono::steady_clock::time_point stopped;
	std::thread run ([&] { result = TileStream::Prefetch (o, "Earth", keys, p, cancel, 20.0); stopped = std::chrono::steady_clock::now(); });
	bool landed = WaitFor ([&] { return p.have >= 1; }, 20.0);
	auto asked = std::chrono::steady_clock::now();
	cancel = true;
	run.join();
	REQUIRE (landed);
	CHECK (result == 1);
	CHECK (p.done < p.total);
	CHECK (std::chrono::duration<double> (stopped - asked).count() < 1.0);
	int kept = 0;
	for (const TileStream::TileKey &k : keys) {
		char rel[40];
		snprintf (rel, sizeof rel, "%02d/%06d/%06d.dds", k.L, k.ilat, k.ilng);
		kept += fs::exists (tex / "Surf" / rel);
	}
	CHECK (kept >= p.have);
}

TEST_CASE ("Clear and CacheBytes cover Class, buildings, Elev_mod and clouds", "[TileStream]")
{
	Tmp t;
	fs::path tex = t.root / "Earth";
	auto put = [&] (const fs::path &f, size_t n) { fs::create_directories (f.parent_path()); std::ofstream (f, std::ios::binary) << std::string (n, 'x'); };
	put (tex / "Surf" / "10" / "000001" / "000002.dds", 100);
	put (tex / "Class" / "10" / "000001" / "000002.dds", 10);
	put (tex / "Bldg" / "14" / "4824" / "6157.bld", 20);
	put (tex / "Elev_mod" / "12" / "000003" / "000004.elv", 30);
	put (tex / "Cloud" / "05" / "000000" / "000001.dds", 40);
	fs::create_directories (tex / "TileStream");
	std::ofstream (tex / "TileStream" / "written.idx") << "10 1 2\n";
	std::ofstream (tex / "TileStream" / "bldg.idx") << "14 4824 6157\n";
	std::ofstream (tex / "TileStream" / "mod.idx") << "12 3 4\n";
	std::ofstream (tex / "TileStream" / "cloud.idx") << "5 0 1\n";
	uint64_t total = 0;
	size_t tiles = 0;
	CHECK (TileStream::CacheBytes (tex.string(), &total, &tiles) == 200);
	CHECK (total == 200);
	CHECK (tiles == 4);
	CHECK (TileStream::Clear (tex.string(), true) == 3);
	CHECK_FALSE (fs::exists (tex / "Class" / "10" / "000001" / "000002.dds"));
	CHECK_FALSE (fs::exists (tex / "Bldg" / "14" / "4824" / "6157.bld"));
	CHECK_FALSE (fs::exists (tex / "Elev_mod" / "12" / "000003" / "000004.elv"));
	CHECK (fs::exists (tex / "Cloud" / "05" / "000000" / "000001.dds")); // a base level
	CHECK (Read (tex / "TileStream" / "cloud.idx") == "5 0 1\n");
	CHECK (Read (tex / "TileStream" / "bldg.idx").empty());
	CHECK (TileStream::Clear (tex.string(), false) == 1);
	CHECK_FALSE (fs::exists (tex / "Cloud" / "05" / "000000" / "000001.dds"));
}
