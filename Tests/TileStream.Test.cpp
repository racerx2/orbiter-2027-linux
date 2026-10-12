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
	int water0 = 0;
	for (int by = 0; by < 128; by++)
		for (int bx = 0; bx < 64; bx++)
			if (AllWater (d, at + ((size_t)by * 128 + bx) * 8, 1)) water0++;
	CHECK (water0 == 128 * 64);
	at += 131072;
	int water1 = 0;
	for (int by = 0; by < 64; by++)
		for (int bx = 0; bx < 32; bx++)
			if (AllWater (d, at + ((size_t)by * 64 + bx) * 8, 1)) water1++;
	CHECK (water1 == 64 * 32);
	CHECK_FALSE (AllWater (d, 128 + 100 * 8, 1)); // land block, 4-colour mode
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
	REQUIRE (b[0].credit.size() == 2);
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
	masks (12, 100, 100);
	masks (13, 50, 50); // no EOX tiles: 404
	std::string cfg = (t.root / "ts.cfg").string();
	std::ofstream (cfg) << "BODY Earth\n"
		"SURF eox " << base << "/eox/{z}/{row}/{col}.png 1 16\n"
		"LIGHTS gibs " << base << "/lights/{n}/{row}/{col}.png 7\n"
		"LIGHTS_BLACK 3 4 14\n"
		"WATER gibs " << base << "/water/{n}/{row}/{col}.png 8\n"
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
	CHECK (ts.Want ("Earth", 14, 1000, 1500, 2) == 1);
	bool seen = false;
	REQUIRE (WaitFor ([&] {
		int k[3 * 16];
		int n = ts.Done ("Earth", k, 16);
		for (int i = 0; i < n; i++) if (k[3 * i] == 14 && k[3 * i + 1] == 1000 && k[3 * i + 2] == 1500) seen = true;
		return seen;
	}, 5.0));
	CHECK (fs::exists (tex / "TileStream" / "written.idx"));

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
