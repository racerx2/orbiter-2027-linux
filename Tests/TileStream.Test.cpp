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
	REQUIRE (b[0].credit.size() == 5);
	CHECK (b[0].credit[3] == "Clouds: NASA Visible Earth");
	CHECK (b[0].credit[4] == "Water/land cover: ESA WorldCover 2021 (CC-BY 4.0)");
	CHECK (b[0].credit[2] == "Elevation: Terrain Tiles on AWS (Mapzen, with SRTM, GMTED2010, ETOPO1 and others)");
	CHECK (b[0].credit[3] == "Clouds: NASA Visible Earth");
	CHECK (b[0].cloudSmall.find ("cloud_combined_2048.jpg") != std::string::npos);
	CHECK (b[0].cloudBig.find ("cloud_combined_8192.tif") != std::string::npos);
	CHECK (b[0].cloudMax == 7);
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
	std::ofstream (cfg) << "BODY Earth\nSURF eox http://127.0.0.1:9/{z}/{row}/{col} 1 16\nLIGHTS gibs http://127.0.0.1:9/{n}/{row}/{col} 7\nWATER gibs http://127.0.0.1:9/{n}/{row}/{col} 8\nEND\n";
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
