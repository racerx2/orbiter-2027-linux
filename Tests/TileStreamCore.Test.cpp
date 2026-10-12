
// not upstream: the tile stream core: folder lock, give-up hold, exceptions, ownership, indexes, archives, disks, network caps

#include <catch2/catch_test_macros.hpp>
#include "TileStream.h"
#include "TileStreamNet.h"
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

// QNetworkAccessManager needs one; made first in a test, it goes last
struct App {
	int argc = 1;
	char arg0[24] = "TileStreamCore.Test";
	char *argv[2] = {arg0, nullptr};
	QCoreApplication app {argc, argv};
};

struct Tmp {
	fs::path root;
	explicit Tmp (const fs::path &base = fs::temp_directory_path())
	{
		static int n = 0;
		root = base / ("tilestream-core-" + std::to_string (getpid()) + "-" + std::to_string (n++));
		fs::remove_all (root);
		fs::create_directories (root);
	}
	~Tmp () { std::error_code ec; fs::remove_all (root, ec); }
};

std::string Read (const fs::path &p)
{
	std::ifstream f (p, std::ios::binary);
	return std::string (std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>());
}

void Put (const fs::path &p, size_t n, char c = 'x')
{
	fs::create_directories (p.parent_path());
	std::ofstream (p, std::ios::binary) << std::string (n, c);
}

void Png (const fs::path &p, int size, QColor c)
{
	fs::create_directories (p.parent_path());
	QImage img (size, size, QImage::Format_RGB888);
	img.fill (c);
	REQUIRE (img.save (QString::fromStdString (p.string()), "PNG"));
}

bool WaitFor (const std::function<bool ()> &f, double sec)
{
	auto end = Clock::now() + std::chrono::duration<double> (sec);
	while (Clock::now() < end) {
		if (f()) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (50));
	}
	return f();
}

long long Today ()
{
	return std::chrono::duration_cast<std::chrono::hours> (std::chrono::system_clock::now().time_since_epoch()).count() / 24;
}

std::string Rel (int L, int a, int b, const char *ext = "dds")
{
	char s[48];
	snprintf (s, sizeof s, "%02d/%06d/%06d.%s", L, a, b, ext);
	return s;
}

// L1-L4 on disk already: no required jobs
void Base (const fs::path &tex)
{
	for (int L = 1; L <= 4; L++)
		for (int c = 0; c < (L == 4 ? 2 : 1); c++) Put (tex / "Surf" / Rel (L, 0, c), 10);
}

// file:// sources: EOX tiles, GIBS lights and water
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
	void Tile (int L, int la, int ln) const
	{
		TsImg::Grid g = TsImg::EoxGrid (L, la, ln);
		for (int r = 0; r < g.nrow; r++)
			for (int c = 0; c < g.ncol; c++) Png (Eox (g.z, g.row0 + r, g.col0 + c), 256, QColor (120, 100, 80));
		Masks (L, la, ln);
	}
	void Terr (int L, int la, int ln) const
	{
		TsImg::Grid g = TsImg::TerrGrid (L, la, ln, 15);
		for (int r = 0; r < g.nrow; r++)
			for (int c = 0; c < g.ncol; c++)
				Png (root / "terr" / std::to_string (g.z) / std::to_string (g.col0 + c) / (std::to_string (g.row0 + r) + ".png"), 256, QColor (128, 100, 0));
	}
	std::string Cfg (const fs::path &file, const std::string &eox, const std::string &extra = "") const
	{
		std::ofstream (file) << "BODY Earth\n"
			"SURF eox " << eox << "/{z}/{row}/{col}.png 1 16\n"
			"LIGHTS gibs " << url << "/lights/{n}/{row}/{col}.png 7\n"
			"WATER gibs " << url << "/water/{n}/{row}/{col}.png 8\n" << extra << "END\n";
		return file.string();
	}
};

TileStream::Options Opt (const std::string &cfg, const fs::path &tex)
{
	TileStream::Options o;
	o.cfgfile = cfg;
	o.bodyDir = [tex] (const std::string &) { return tex.string(); };
	o.bodies = {"Earth"};
	o.sky = false;
	return o;
}

struct Log {
	std::mutex m;
	std::vector<std::string> lines;
	std::function<void (const std::string &)> Fn () { return [this] (const std::string &s) { std::lock_guard<std::mutex> l (m); lines.push_back (s); }; }
	int Count (const std::string &part)
	{
		std::lock_guard<std::mutex> l (m);
		int n = 0;
		for (const std::string &s : lines) n += s.find (part) != std::string::npos;
		return n;
	}
};

}

// first in the file: the fork happens before any test has started a thread
TEST_CASE ("A second Orbiter on the folder streams offline and writes nothing; Busy, Clear and the region download refuse", "[TileStreamCore]")
{
	Tmp t;
	Srv s (t.root);
	fs::path tex = t.root / "Earth";
	std::string cfg = s.Cfg (t.root / "ts.cfg", s.url + "/eox");
	Put (tex / "Surf" / Rel (9, 0, 0), 100);
	fs::create_directories (tex / "TileStream");
	std::ofstream (tex / "TileStream" / "written.idx") << "9 0 0\n";
	CHECK_FALSE (TileStream::Busy (tex.string()));
	int go[2], ready[2];
	REQUIRE (pipe (go) == 0);
	REQUIRE (pipe (ready) == 0);
	pid_t pid = fork();
	REQUIRE (pid >= 0);
	if (!pid) { // the other Orbiter: an offline stream holding the folder
		TileStream::Options o = Opt (cfg, tex);
		o.offline = true;
		{
			TileStream ts (o);
			char c = 1;
			if (write (ready[1], &c, 1) != 1 || read (go[0], &c, 1) != 1) _exit (2);
		}
		_exit (0);
	}
	char c = 0;
	REQUIRE (read (ready[0], &c, 1) == 1);
	CHECK (TileStream::Busy (tex.string()));
	CHECK (TileStream::Busy (tex.string() + "/"));
	{
		App app;
		Log log;
		TileStream::Options o = Opt (cfg, tex);
		o.maxActive = 0;
		o.log = log.Fn();
		TileStream ts (o);
		REQUIRE (ts.Enabled ("Earth"));
		CHECK (log.Count ("is in use by another Orbiter") == 1);
		CHECK (ts.Pending() == 0); // offline: no required jobs
		CHECK (ts.Want ("Earth", 12, 5, 5, 0) == -1);
		CHECK (ts.Want ("Earth", 9, 0, 0, 0) == 1);  // on disk
		CHECK (ts.Want ("Earth", 9, 0, 0, 1) == -1); // never deleted
		CHECK (ts.Want ("Earth", 9, 0, 0, 2) == 1);
		ts.Evict();
		CHECK (fs::exists (tex / "Surf" / Rel (9, 0, 0)));
		bool refused = false;
		CHECK (TileStream::Clear (tex.string(), false, &refused) == 0);
		CHECK (refused); // told apart from nothing to clear
		CHECK (fs::exists (tex / "Surf" / Rel (9, 0, 0)));
		TileStream::Progress p;
		std::atomic<bool> cancel {false};
		CHECK (TileStream::Prefetch (o, "Earth", {{5, 0, 0}}, p, cancel, 5.0) == 4);
	}
	c = 1;
	REQUIRE (write (go[1], &c, 1) == 1);
	int st = 0;
	REQUIRE (waitpid (pid, &st, 0) == pid);
	CHECK (WIFEXITED (st));
	CHECK (WEXITSTATUS (st) == 0);
	CHECK_FALSE (TileStream::Busy (tex.string()));
	for (int *f : {go, ready}) { close (f[0]); close (f[1]); }
	{ // one process: its streams share the folder, and Busy says so
		App app;
		TileStream::Options o = Opt (cfg, tex);
		o.maxActive = 0;
		TileStream a (o), b (o);
		CHECK (a.Pending() == 5);
		CHECK (b.Pending() == 5);
		CHECK (TileStream::Busy (tex.string()));
		CHECK (TileStream::Clear (tex.string(), false) == 0);
	}
	CHECK_FALSE (TileStream::Busy (tex.string()));
	bool refused = true;
	CHECK (TileStream::Clear (tex.string(), false, &refused) == 1);
	CHECK_FALSE (refused);
	CHECK_FALSE (fs::exists (tex / "Surf" / Rel (9, 0, 0)));
	refused = true;
	CHECK (TileStream::Clear (tex.string(), false, &refused) == 0); // nothing left: not a refusal
	CHECK_FALSE (refused);
}

TEST_CASE ("A tile that keeps failing is held after its tries, not made again each frame, and counts as failed in a region download", "[TileStreamCore]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	s.Masks (6, 3, 5);
	s.Masks (6, 3, 6);
	fs::path tex = t.root / "Earth";
	Base (tex);
	auto t0 = Clock::now();
	TileStream::Options o = Opt (s.Cfg (t.root / "ts.cfg", "bogus://eox"), tex); // no such scheme: every EOX request fails
	o.now = [t0] { return t0 + (Clock::now() - t0) * 1000; };                     // retries, pauses and the 600 s hold 1000 times faster
	o.staleSec = 1e9;
	{
		TileStream ts (o);
		CHECK (ts.Want ("Earth", 6, 3, 5, 0) == 0);
		CHECK (ts.Pending() == 1);
		REQUIRE (WaitFor ([&] { ts.Want ("Earth", 6, 3, 5, 0); return ts.Pending() == 0; }, 20.0));
		for (int i = 0; i < 10; i++) { // held: still wanted, never made again
			CHECK (ts.Want ("Earth", 6, 3, 5, 0) == 0);
			std::this_thread::sleep_for (std::chrono::milliseconds (10));
		}
		CHECK (ts.Pending() == 0);
	}
	TileStream::Progress p;
	std::atomic<bool> cancel {false};
	CHECK (TileStream::Prefetch (o, "Earth", {{6, 3, 6}}, p, cancel, 10.0) == 0);
	CHECK (p.done == 1);
	CHECK (p.failed == 1);
	CHECK (p.none == 0);
	CHECK (p.have == 0);
}

TEST_CASE ("A build that throws fails its tile, held, logged once, and the stream goes on", "[TileStreamCore]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	for (int c : {5, 6, 7}) s.Tile (6, 3, c);
	fs::path tex = t.root / "Earth";
	Base (tex);
	Log log;
	TileStream::Options o = Opt (s.Cfg (t.root / "ts.cfg", s.url + "/eox"), tex);
	o.log = log.Fn();
	o.staleSec = 1e9;
	o.onBuild = [] (int L, int, int c) { if (L == 6 && c != 6) throw std::runtime_error ("test"); };
	TileStream ts (o);
	for (int c : {5, 6, 7}) CHECK (ts.Want ("Earth", 6, 3, c, 0) == 0);
	REQUIRE (WaitFor ([&] { return fs::exists (tex / "Surf" / Rel (6, 3, 6)) && ts.Pending() == 0; }, 20.0));
	CHECK (ts.Want ("Earth", 6, 3, 5, 0) == 0); // held, not made again
	CHECK (ts.Want ("Earth", 6, 3, 7, 0) == 0);
	CHECK (ts.Pending() == 0);
	CHECK_FALSE (fs::exists (tex / "Surf" / Rel (6, 3, 5)));
	CHECK (log.Count ("a job failed: test") == 1);
}

TEST_CASE ("Elev belongs to the stream only when elev.idx lists it: Evict, Clear and the Elev job leave the rest", "[TileStreamCore]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	fs::path tex = t.root / "Earth", ts = tex / "TileStream";
	auto now = fs::file_time_type::clock::now();
	for (int c : {0, 1})
		for (const char *layer : {"Surf", "Mask", "Elev"}) {
			fs::path p = tex / layer / Rel (9, 0, c, layer[0] == 'E' ? "elv" : "dds");
			Put (p, 1 << 20);
			fs::last_write_time (p, now - std::chrono::hours (5 - c));
		}
	fs::create_directories (ts);
	std::ofstream (ts / "written.idx") << "9 0 0\n9 0 1\n";
	TileStream::Options o = Opt (s.Cfg (t.root / "ts.cfg", s.url + "/eox"), tex);
	o.offline = true;
	o.capMB = 0;
	{ TileStream a (o); } // from before elev.idx: the Elev of every streamed tile is adopted, once
	CHECK (Read (ts / "elev.idx").find ("9 0 0") != std::string::npos);
	CHECK (Read (ts / "elev.idx").find ("9 0 1") != std::string::npos);
	std::ofstream (ts / "elev.idx") << "9 0 0\n"; // 9 0 1's Elev is the user's
	o.capMB = 1;
	{
		TileStream a (o);
		REQUIRE (WaitFor ([&] { return !fs::exists (tex / "Surf" / Rel (9, 0, 1)); }, 10.0));
	}
	CHECK_FALSE (fs::exists (tex / "Elev" / Rel (9, 0, 0, "elv")));
	CHECK (fs::exists (tex / "Elev" / Rel (9, 0, 1, "elv")));
	CHECK (Read (ts / "elev.idx").find ("9 0 0") == std::string::npos);

	// Clear and CacheBytes: our Elev, with its tile or alone; the user's stays
	Put (tex / "Surf" / Rel (12, 1, 2), 100);
	Put (tex / "Surf" / Rel (12, 1, 3), 100);
	Put (tex / "Elev" / Rel (12, 1, 2, "elv"), 50);
	Put (tex / "Elev" / Rel (12, 1, 3, "elv"), 50);
	Put (tex / "Elev" / Rel (13, 5, 5, "elv"), 30);
	std::ofstream (ts / "written.idx") << "12 1 2\n12 1 3\n";
	std::ofstream (ts / "elev.idx") << "12 1 2\n13 5 5\n";
	size_t tiles = 0;
	CHECK (TileStream::CacheBytes (tex.string(), nullptr, &tiles) == 280);
	CHECK (tiles == 3);
	CHECK (TileStream::Clear (tex.string(), false) == 3);
	CHECK_FALSE (fs::exists (tex / "Elev" / Rel (12, 1, 2, "elv")));
	CHECK_FALSE (fs::exists (tex / "Elev" / Rel (13, 5, 5, "elv")));
	CHECK (fs::exists (tex / "Elev" / Rel (12, 1, 3, "elv")));
	CHECK (Read (ts / "elev.idx").empty());

	// the Elev job: made where there is none, never over the user's
	fs::remove_all (tex);
	Base (tex);
	for (int c : {1500, 1501}) {
		s.Tile (14, 1000, c);
		s.Terr (14, 1000, c);
	}
	Put (tex / "Elev" / Rel (14, 1000, 1500, "elv"), 100, 'u');
	o = Opt (s.Cfg (t.root / "ts.cfg", s.url + "/eox", "ELEV terrarium " + s.url + "/terr/{z}/{col}/{row}.png 15 16\n"), tex);
	o.staleSec = 1e9;
	TileStream a (o);
	for (int c : {1500, 1501}) CHECK (a.Want ("Earth", 14, 1000, c, 0) == 0);
	REQUIRE (WaitFor ([&] { std::error_code ec; return fs::file_size (tex / "Elev" / Rel (14, 1000, 1501, "elv"), ec) == 134262 && a.Pending() == 0; }, 30.0));
	CHECK (fs::exists (tex / "Surf" / Rel (14, 1000, 1500)));
	CHECK (Read (tex / "Elev" / Rel (14, 1000, 1500, "elv")) == std::string (100, 'u'));
	CHECK (a.Want ("Earth", 14, 1000, 1500, 2) == 1); // a cache hit asks for no Elev over it either
	std::this_thread::sleep_for (std::chrono::milliseconds (300));
	CHECK (a.Pending() == 0);
	CHECK (Read (ts / "elev.idx").find ("14 1000 1501") != std::string::npos);
	CHECK (Read (ts / "elev.idx").find ("14 1000 1500") == std::string::npos);
}

TEST_CASE ("Index lines are three integers inside the grid: aliasing, torn and foreign lines are dropped", "[TileStreamCore]")
{
	Tmp t;
	fs::path tex = t.root / "Earth", ts = tex / "TileStream";
	Put (tex / "Surf" / Rel (10, 1, 1), 1000); // where "10 0 16777217" would land
	Put (tex / "Surf" / Rel (10, 0, 3), 1000); // where "9 16777216 3" would land
	Put (tex / "Surf" / Rel (9, 0, 0), 1000);  // ours
	Put (tex / "Bldg" / "14" / "16384" / "0.bld", 10);
	Put (tex / "Bldg" / "14" / "5" / "5.bld", 10);
	Put (tex / "Elev_mod" / Rel (12, 3, 4, "elv"), 10);
	Put (tex / "Cloud" / Rel (5, 0, 9), 10);
	fs::create_directories (ts);
	const char *bad = "10 0 16777217\n9 16777216 3\n12 345 67 12 3\n3 0 1\n14 1 1x\n-9 0 0\n9 0 0 junk\n";
	std::ofstream (ts / "written.idx") << bad << "9 0 0\n9 0 0\n";
	std::ofstream (ts / "bldg.idx") << "14 16384 0\n13 1 1\n14 5 5\n";
	std::ofstream (ts / "mod.idx") << "12 3 4 5\n";
	std::ofstream (ts / "cloud.idx") << "5 0 9\n";
	CHECK (TileStream::Clear (tex.string(), false) == 2); // 9 0 0 and the building tile 14/5/5
	CHECK_FALSE (fs::exists (tex / "Surf" / Rel (9, 0, 0)));
	CHECK_FALSE (fs::exists (tex / "Bldg" / "14" / "5" / "5.bld"));
	CHECK (fs::exists (tex / "Surf" / Rel (10, 1, 1)));
	CHECK (fs::exists (tex / "Surf" / Rel (10, 0, 3)));
	CHECK (fs::exists (tex / "Bldg" / "14" / "16384" / "0.bld"));
	CHECK (fs::exists (tex / "Elev_mod" / Rel (12, 3, 4, "elv")));
	CHECK (fs::exists (tex / "Cloud" / Rel (5, 0, 9)));

	// the stream drops the bad lines, and those whose Surf is gone, and writes the index once
	Put (tex / "Surf" / Rel (9, 0, 1), 1000);
	std::ofstream (ts / "written.idx") << bad << "9 0 1\n9 0 1\n9 0 2\n";
	TileStream::Options o = Opt ((t.root / "none.cfg").string(), tex);
	std::ofstream (o.cfgfile) << "BODY Earth\nSURF eox file:///none/{z}/{row}/{col} 1 16\nLIGHTS gibs file:///none/{n}/{row}/{col} 7\nWATER gibs file:///none/{n}/{row}/{col} 8\nEND\n";
	o.offline = true;
	o.capMB = 1;
	{
		TileStream a (o);
		REQUIRE (WaitFor ([&] { return Read (ts / "written.idx") == "9 0 1\n"; }, 10.0));
		a.Evict();
	}
	CHECK (fs::exists (tex / "Surf" / Rel (10, 1, 1)));
	CHECK (fs::exists (tex / "Surf" / Rel (10, 0, 3)));
}

TEST_CASE ("Evict keeps an entry whose file came back during its scan", "[TileStreamCore]")
{
	Tmp t;
	fs::path tex = t.root / "Earth", ts = tex / "TileStream";
	for (int c : {0, 1}) { // L8: counted, never evicted, so every Evict scans
		Put (tex / "Surf" / Rel (8, 0, c), 1 << 20);
		Put (tex / "Mask" / Rel (8, 0, c), 1 << 20);
	}
	Put (tex / "Surf" / Rel (8, 0, 2), 10);
	fs::create_directories (ts);
	std::ofstream (ts / "written.idx") << "8 0 0\n8 0 1\n8 0 2\n";
	TileStream::Options o = Opt ((t.root / "none.cfg").string(), tex);
	std::ofstream (o.cfgfile) << "BODY Earth\nSURF eox file:///none/{z}/{row}/{col} 1 16\nLIGHTS gibs file:///none/{n}/{row}/{col} 7\nWATER gibs file:///none/{n}/{row}/{col} 8\nEND\n";
	o.offline = true;
	o.capMB = 1;
	std::atomic<bool> back {false};
	o.evictScanned = [&] { if (back) Put (tex / "Surf" / Rel (8, 0, 2), 10); }; // written again while Evict scanned
	TileStream a (o);
	std::this_thread::sleep_for (std::chrono::milliseconds (500)); // the startup prune and Evict
	fs::remove (tex / "Surf" / Rel (8, 0, 2));
	back = true;
	a.Evict();
	CHECK (Read (ts / "written.idx").find ("8 0 2") != std::string::npos);
	back = false;
	fs::remove (tex / "Surf" / Rel (8, 0, 2));
	a.Evict();
	CHECK (Read (ts / "written.idx").find ("8 0 2") == std::string::npos);
	CHECK (Read (ts / "written.idx").find ("8 0 1") != std::string::npos);
}

TEST_CASE ("none.idx: out-of-date lines go at startup, and a file untouched for 30 days goes whole", "[TileStreamCore]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	fs::path tex = t.root / "Earth", ts = tex / "TileStream";
	Base (tex);
	fs::create_directories (ts);
	std::ofstream (ts / "none.idx") << "13 50 50 " << Today() << "\n";
	fs::last_write_time (ts / "none.idx", fs::file_time_type::clock::now() - std::chrono::hours (24 * 31));
	TileStream::Options o = Opt (s.Cfg (t.root / "ts.cfg", s.url + "/eox"), tex);
	o.maxActive = 0;
	{
		TileStream a (o);
		CHECK (a.Want ("Earth", 13, 50, 50, 0) == 0);
	}
	CHECK_FALSE (fs::exists (ts / "none.idx"));
	std::ofstream (ts / "none.idx") << "13 50 51 " << Today() - 40 << "\n13 50 52 " << Today() << "\n";
	{
		TileStream a (o);
		CHECK (a.Want ("Earth", 13, 50, 51, 0) == 0);
		CHECK (a.Want ("Earth", 13, 50, 52, 0) == -1);
		CHECK (WaitFor ([&] { return Read (ts / "none.idx") == "13 50 52 " + std::to_string (Today()) + "\n"; }, 5.0));
	}
}

TEST_CASE ("A cloud source that is no image is never kept, and is asked again the next day only", "[TileStreamCore]")
{
	App app;
	if (!QImageReader::supportedImageFormats().contains ("jpg")) SKIP ("no jpeg reader");
	Tmp t;
	Srv s (t.root);
	fs::path img = s.root / "cloud.jpg", tex = t.root / "Earth", src = tex / "TileStream" / "src" / "cloud_small.jpg", day = src.string() + ".day";
	fs::path l1 = tex / "Cloud" / Rel (1, 0, 0);
	fs::create_directories (s.root);
	QImage c (256, 128, QImage::Format_RGB888);
	c.fill (QColor (200, 200, 200));
	auto good = [&] { REQUIRE (c.save (QString::fromStdString (img.string()), "JPG")); };
	auto portal = [&] (const fs::path &p) { Put (p, 0); std::ofstream (p) << "<html><body>sign in to the network</body></html>"; };
	good();
	TileStream::Options o = Opt (s.Cfg (t.root / "ts.cfg", s.url + "/eox", "CLOUD file://" + img.string() + " file://" + (s.root / "none.tif").string() + " 5\n"), tex);
	o.sky = true;
	o.maxActive = 0;
	o.cloudWait = 5;
	Base (tex);
	portal (src); // cached by an older build: not an image
	{ TileStream a (o); }
	CHECK (fs::exists (l1));
	CHECK (QImage (QString::fromStdString (src.string())).width() == 256);

	auto reset = [&] { fs::remove_all (tex / "Cloud"); fs::remove (src); fs::remove (tex / "TileStream" / "cloud.idx"); };
	reset();
	portal (img); // the server answers with a page
	{ TileStream a (o); }
	CHECK_FALSE (fs::exists (src));
	CHECK_FALSE (fs::exists (l1));
	CHECK (fs::exists (day));
	good();
	{ TileStream a (o); } // the same day: not asked again
	CHECK_FALSE (fs::exists (l1));
	std::ofstream (day, std::ios::trunc) << Today() - 1 << "\n";
	{ TileStream a (o); }
	CHECK (fs::exists (l1));
	CHECK_FALSE (fs::exists (day));
}

// an Archive/<Layer>.tree with nodes and no tile data, in ZTreeMgr's layout
struct Node { int64_t pos; uint32_t size; uint32_t child[4]; };
static_assert (sizeof (Node) == 32, "TreeNode");

static void Tree (const fs::path &p, const std::vector<Node> &n, std::array<uint32_t, 5> root)
{
	fs::create_directories (p.parent_path());
	struct { char magic[4]; uint32_t size, flags, dataOfs; int64_t dataLength; uint32_t nodeCount, root[5]; } h;
	static_assert (sizeof h == 48, "TreeFileHeader");
	memcpy (h.magic, "TX\1\0", 4);
	h.size = sizeof h;
	h.flags = 0;
	h.dataOfs = (uint32_t)(sizeof h + n.size() * sizeof (Node));
	h.dataLength = 0;
	h.nodeCount = (uint32_t)n.size();
	memcpy (h.root, root.data(), sizeof h.root);
	std::ofstream f (p, std::ios::binary);
	f.write ((const char *)&h, sizeof h);
	f.write ((const char *)n.data(), (std::streamsize)(n.size() * sizeof (Node)));
}

TEST_CASE ("An installed archive's tiles are never streamed, and our loose copies of them go at startup", "[TileStreamCore]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	fs::path tex = t.root / "Earth", ts = tex / "TileStream";
	const uint32_t no = (uint32_t)-1;
	// L1, L2, L3 without data, both L4, and L5 (0, 0) under the west L4
	std::vector<Node> n = {{0, 100, {no, no, no, no}}, {0, 100, {no, no, no, no}}, {0, 0, {no, no, no, no}},
		{0, 100, {5, no, no, no}}, {0, 100, {no, no, no, no}}, {0, 100, {no, no, no, no}}};
	Tree (tex / "Archive" / "Surf.tree", n, {0, 1, 2, 3, 4});
	Tree (tex / "Archive" / "Elev.tree", n, {0, 1, 2, 3, 4});
	Tree (tex / "Archive" / "Cloud.tree", {{0, 100, {no, no, no, no}}}, {0, no, no, no, no});
	for (const char *layer : {"Surf", "Mask", "Class"}) Put (tex / layer / Rel (5, 0, 0), 10);
	Put (tex / "Surf" / Rel (5, 0, 1), 10);
	Put (tex / "Mask" / Rel (5, 0, 1), 10);
	Put (tex / "Elev" / Rel (5, 0, 0, "elv"), 10);
	Put (tex / "Elev" / Rel (5, 0, 1, "elv"), 10);
	Put (tex / "Cloud" / Rel (1, 0, 0), 10);
	fs::create_directories (ts);
	std::ofstream (ts / "written.idx") << "5 0 0\n5 0 1\n";
	std::ofstream (ts / "elev.idx") << "5 0 0\n5 0 1\n";
	std::ofstream (ts / "cloud.idx") << "1 0 0\n";
	bool jpg = QImageReader::supportedImageFormats().contains ("jpg");
	fs::path img = s.root / "cloud.jpg";
	fs::create_directories (s.root);
	QImage c (256, 128, QImage::Format_RGB888);
	c.fill (QColor (200, 200, 200));
	if (jpg) REQUIRE (c.save (QString::fromStdString (img.string()), "JPG"));
	TileStream::Options o = Opt (s.Cfg (t.root / "ts.cfg", s.url + "/eox", "CLOUD file://" + img.string() + " file://" + (s.root / "none.tif").string() + " 5\n"), tex);
	o.sky = true;
	o.maxActive = 0;
	o.cloudWait = 5;
	TileStream a (o);
	CHECK (a.Pending() == 1); // the required L3 only: the archive has L1, L2 and both L4
	CHECK (a.Want ("Earth", 5, 0, 0, 0) == -1);
	CHECK (a.Prefetch ("Earth", 5, 0, 0) == 1);
	CHECK (a.Pending() == 1);
	CHECK (a.Want ("Earth", 5, 0, 2, 0) == 0);
	for (const char *layer : {"Surf", "Mask", "Class"}) CHECK_FALSE (fs::exists (tex / layer / Rel (5, 0, 0)));
	CHECK (fs::exists (tex / "Surf" / Rel (5, 0, 1)));
	CHECK (fs::exists (tex / "Mask" / Rel (5, 0, 1)));
	CHECK (Read (ts / "written.idx") == "5 0 1\n");
	CHECK_FALSE (fs::exists (tex / "Elev" / Rel (5, 0, 0, "elv")));
	CHECK (fs::exists (tex / "Elev" / Rel (5, 0, 1, "elv")));
	CHECK (Read (ts / "elev.idx") == "5 0 1\n");
	CHECK_FALSE (fs::exists (tex / "Cloud" / Rel (1, 0, 0)));
	if (jpg) CHECK (fs::exists (tex / "Cloud" / Rel (2, 0, 0)));
	CHECK (fs::exists (tex / "Archive" / "Surf.tree"));
}

TEST_CASE ("A layer folder on another disk is written through a .part beside the target; Clear takes stray .part files", "[TileStreamCore]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	s.Tile (6, 3, 5);
	s.Tile (6, 3, 6);
	fs::path tex = t.root / "Earth";
	Base (tex);
	TileStream::Options o = Opt (s.Cfg (t.root / "ts.cfg", s.url + "/eox"), tex);
	o.staleSec = 1e9;
	o.crossDisk = true;
	{
		TileStream a (o);
		REQUIRE (WaitFor ([&] { a.Want ("Earth", 6, 3, 5, 0); return a.Want ("Earth", 6, 3, 5, 0) == 1; }, 20.0));
		CHECK (fs::file_size (tex / "Surf" / Rel (6, 3, 5)) == 174904);
		CHECK (fs::exists (tex / "Mask" / Rel (6, 3, 5)));
		CHECK_FALSE (fs::exists (tex / "Surf" / (Rel (6, 3, 5) + ".part")));
		CHECK (fs::is_empty (tex / "TileStream" / "tmp"));
	}
	struct stat a, b; // a real second file system, when /dev/shm is one
	if (!stat ("/dev/shm", &a) && !stat (t.root.c_str(), &b) && a.st_dev != b.st_dev && access ("/dev/shm", W_OK) == 0) {
		Tmp shm ("/dev/shm");
		fs::copy (tex / "Surf", shm.root / "Surf", fs::copy_options::recursive);
		fs::remove_all (tex / "Surf");
		fs::create_directory_symlink (shm.root / "Surf", tex / "Surf");
		o.crossDisk = false;
		TileStream c (o);
		REQUIRE (WaitFor ([&] { c.Want ("Earth", 6, 3, 6, 0); return fs::exists (shm.root / "Surf" / Rel (6, 3, 6)); }, 20.0));
		CHECK (fs::file_size (shm.root / "Surf" / Rel (6, 3, 6)) == 174904);
		CHECK_FALSE (fs::exists (shm.root / "Surf" / (Rel (6, 3, 6) + ".part")));
	}
	Tmp u;
	fs::path tx = u.root / "Earth";
	Put (tx / "Surf" / Rel (9, 0, 0), 100);
	Put (tx / "Surf" / (Rel (9, 0, 0) + ".part"), 10);
	Put (tx / "Mask" / (Rel (9, 0, 0) + ".part"), 5);
	fs::create_directories (tx / "TileStream");
	std::ofstream (tx / "TileStream" / "written.idx") << "9 0 0\n";
	CHECK (TileStream::CacheBytes (tx.string()) == 115);
	CHECK (TileStream::Clear (tx.string(), false) == 1);
	CHECK_FALSE (fs::exists (tx / "Surf" / (Rel (9, 0, 0) + ".part")));
	CHECK_FALSE (fs::exists (tx / "Mask" / (Rel (9, 0, 0) + ".part")));
}

// HTTP on 127.0.0.1: each connection gets the next canned reply
struct Http {
	int fd = -1, port = 0;
	std::vector<std::string> replies;
	std::mutex m;
	std::vector<std::string> requests; // what each connection sent, under m
	std::atomic<int> dripMs {0};       // > 0: the reply goes out a byte at a time
	std::thread th;
	explicit Http (std::vector<std::string> r): replies (std::move (r))
	{
		fd = socket (AF_INET, SOCK_STREAM, 0);
		sockaddr_in a {};
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
		socklen_t l = sizeof a;
		REQUIRE (bind (fd, (sockaddr *)&a, sizeof a) == 0);
		REQUIRE (listen (fd, 8) == 0);
		REQUIRE (getsockname (fd, (sockaddr *)&a, &l) == 0);
		port = ntohs (a.sin_port);
		th = std::thread ([this] {
			for (const std::string &rep : replies) {
				int c = accept (fd, nullptr, nullptr);
				if (c < 0) return;
				timeval tv {2, 0};
				setsockopt (c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
				std::string req;
				char buf[4096];
				while (req.find ("\r\n\r\n") == std::string::npos) {
					ssize_t n = recv (c, buf, sizeof buf, 0);
					if (n <= 0) break;
					req.append (buf, (size_t)n);
				}
				{
					std::lock_guard<std::mutex> l (m);
					requests.push_back (req);
				}
				if (dripMs > 0) {
					for (char ch : rep) {
						if (send (c, &ch, 1, MSG_NOSIGNAL) != 1) break;
						std::this_thread::sleep_for (std::chrono::milliseconds (dripMs.load()));
					}
				} else send (c, rep.data(), rep.size(), MSG_NOSIGNAL);
				shutdown (c, SHUT_WR);
				while (recv (c, buf, sizeof buf, 0) > 0) {}
				close (c);
			}
		});
	}
	~Http ()
	{
		shutdown (fd, SHUT_RDWR);
		close (fd);
		th.join();
	}
	std::string Url () const { return "http://127.0.0.1:" + std::to_string (port) + "/f"; }
};

static std::string Reply (const char *status, const std::string &headers, const std::string &body)
{
	return std::string ("HTTP/1.1 ") + status + "\r\nContent-Length: " + std::to_string (body.size()) + "\r\n" + headers + "Connection: close\r\n\r\n" + body;
}

TEST_CASE ("Answers past the size caps fail, and a range answer must be the range asked for", "[TileStreamCore]")
{
	App app;
	Tmp t;
	for (const char *v : {"http_proxy", "HTTP_PROXY", "all_proxy", "ALL_PROXY"}) unsetenv (v);
	TsNet net ("test", 1000, 200);
	auto get = [&] (const std::string &url, int64_t off, int64_t len, QByteArray &body) {
		std::atomic<int> st {-1};
		net.Get (url, [&] (int s, QByteArray got, int) { body = got; st = s; }, off, len);
		WaitFor ([&] { return st >= 0; }, 10.0);
		return st.load();
	};
	QByteArray b;
	Put (t.root / "big", 2000);
	Put (t.root / "small", 500);
	std::string big = "file://" + (t.root / "big").string(), small = "file://" + (t.root / "small").string();
	CHECK (get (big, -1, 0, b) == 0);
	CHECK (get (small, -1, 0, b) == 200);
	CHECK (b.size() == 500);
	CHECK (get (big, 100, 300, b) == 0); // more than the range cap
	CHECK (get (big, 100, 50, b) == 200);
	CHECK (b.size() == 50);

	std::string d;
	for (int i = 0; i < 100; i++) d += (char)('a' + i % 26);
	Http h ({
		Reply ("206 Partial Content", "Content-Range: bytes 10-19/100\r\n", d.substr (10, 10)),
		Reply ("206 Partial Content", "Content-Range: bytes 0-9/100\r\n", d.substr (0, 10)),   // the wrong start
		Reply ("206 Partial Content", "Content-Range: bytes 10-14/100\r\n", d.substr (10, 5)),  // short, not the file's end
		Reply ("206 Partial Content", "Content-Range: bytes 95-99/100\r\n", d.substr (95, 5)),  // up to the file's end
		Reply ("206 Partial Content", "", d.substr (10, 10)),                                    // no Content-Range
		Reply ("200 OK", "", d),                          // the whole file, under the range cap: cut here
		Reply ("200 OK", "", std::string (500, 'x')),     // the whole file, past the range cap
		Reply ("200 OK", "", std::string (2000, 'x')),    // past the cap
		Reply ("200 OK", "", std::string (500, 'x')),
	});
	CHECK (get (h.Url(), 10, 10, b) == 200);
	CHECK (b.toStdString() == d.substr (10, 10));
	CHECK (get (h.Url(), 10, 10, b) == 0);
	CHECK (get (h.Url(), 10, 10, b) == 0);
	CHECK (get (h.Url(), 95, 10, b) == 200);
	CHECK (b.toStdString() == d.substr (95, 5));
	CHECK (get (h.Url(), 10, 10, b) == 0);
	CHECK (get (h.Url(), 10, 10, b) == 200);
	CHECK (b.toStdString() == d.substr (10, 10));
	CHECK (get (h.Url(), 10, 10, b) == 0);
	CHECK (get (h.Url(), -1, 0, b) == 0);
	CHECK (get (h.Url(), -1, 0, b) == 200);
	CHECK (b.size() == 500);
}

TEST_CASE ("A stream that throws while it starts leaves the folder free, and a hostile archive TOC is refused before any allocation", "[TileStreamCore]")
{
	App app;
	Tmp t;
	Srv s (t.root);
	fs::path tex = t.root / "Earth";
	Base (tex);
	std::string cfg = s.Cfg (t.root / "ts.cfg", s.url + "/eox");
	TileStream::Options o = Opt (cfg, tex);
	o.offline = true;
	o.log = [] (const std::string &l) { if (l.find ("tiles go to") != std::string::npos) throw std::runtime_error ("test"); }; // after the lock is taken
	CHECK_THROWS_AS (TileStream (o), std::runtime_error);
	CHECK_FALSE (TileStream::Busy (tex.string()));
	TileStream::Progress p;
	std::atomic<bool> cancel {false};
	CHECK (TileStream::Prefetch (o, "Earth", {{1, 0, 0}}, p, cancel, 5.0) == 5); // the region download's stream throws the same way
	CHECK_FALSE (TileStream::Busy (tex.string()));
	Put (tex / "Surf" / Rel (9, 0, 0), 100);
	std::ofstream (tex / "TileStream" / "written.idx") << "9 0 0\n";
	bool refused = true;
	CHECK (TileStream::Clear (tex.string(), false, &refused) == 1);
	CHECK_FALSE (refused);

	// node counts past the TOC's room, past the sanity cap, and a data offset past the end, in sparse files
	struct Bad { uint32_t nodes, dataOfs; uint64_t size; };
	const uint32_t big = (1u << 26) + 1;
	for (const Bad &b : {Bad {0xFFFFFFFFu, 48, (uint64_t)128 << 30}, Bad {big, 48 + big * 32u, 48 + (uint64_t)big * 32}, Bad {10, 48 + 320, 48 + 100}}) {
		fs::path tree = tex / "Archive" / "Surf.tree";
		fs::create_directories (tree.parent_path());
		struct { char magic[4]; uint32_t size, flags, dataOfs; int64_t dataLength; uint32_t nodeCount, root[5]; } h {};
		memcpy (h.magic, "TX\1\0", 4);
		h.size = sizeof h;
		h.dataOfs = b.dataOfs;
		h.nodeCount = b.nodes;
		{
			std::ofstream f (tree, std::ios::binary | std::ios::trunc);
			f.write ((const char *)&h, sizeof h);
		}
		std::error_code ec;
		fs::resize_file (tree, b.size, ec);
		if (ec) fs::resize_file (tree, std::min<uint64_t> (b.size, (uint64_t)1 << 30), ec); // no sparse files that big here
		Log log;
		TileStream::Options q = Opt (cfg, tex);
		q.offline = true;
		q.log = log.Fn();
		auto t0 = Clock::now();
		{
			TileStream a (q);
			CHECK (a.Enabled ("Earth"));
		}
		CHECK (log.Count ("installed archives") == 0);
		CHECK (std::chrono::duration<double> (Clock::now() - t0).count() < 5.0);
		CHECK_FALSE (TileStream::Busy (tex.string()));
		fs::remove (tree);
	}
}

namespace {
std::atomic<int> flockErr {0};
bool nfs = false; // as NFS: an exclusive flock on a file open read-only fails with EBADF
int FakeFlock (int fd, int op)
{
	if (flockErr) { errno = flockErr; return -1; }
	if (nfs && (op & LOCK_EX) && (fcntl (fd, F_GETFL) & O_ACCMODE) == O_RDONLY) { errno = EBADF; return -1; }
	return flock (fd, op);
}
struct FakeLocks {
	FakeLocks () { TsFlock = FakeFlock; }
	~FakeLocks () { TsFlock = flock; flockErr = 0; nfs = false; }
};
}

TEST_CASE ("Only EWOULDBLOCK means another Orbiter: no working locks, the stream writes and Busy is false; on NFS Busy still sees a holder", "[TileStreamCore]")
{
	App app;
	FakeLocks fake;
	Tmp t;
	Srv s (t.root);
	fs::path tex = t.root / "Earth";
	std::string cfg = s.Cfg (t.root / "ts.cfg", s.url + "/eox");
	TileStream::Options o = Opt (cfg, tex);
	o.maxActive = 0;
	for (int e : {ENOLCK, EOPNOTSUPP, EINVAL}) {
		flockErr = e;
		Log log;
		o.log = log.Fn();
		{
			TileStream a (o);
			CHECK (log.Count ("has no working file lock") == 1);
			CHECK (log.Count ("in use by another Orbiter") == 0);
			CHECK (log.Count ("(offline)") == 0);
			CHECK (a.Pending() == 5); // the writer: its required tiles are queued
			CHECK (TileStream::Busy (tex.string())); // this process's stream
		}
		CHECK_FALSE (TileStream::Busy (tex.string())); // nobody can hold a lock here
		bool refused = true;
		TileStream::Clear (tex.string(), false, &refused);
		CHECK_FALSE (refused);
	}
	flockErr = EWOULDBLOCK; // held elsewhere
	CHECK (TileStream::Busy (tex.string()));
	{
		Log log;
		o.log = log.Fn();
		TileStream a (o);
		CHECK (log.Count ("in use by another Orbiter") == 1);
		CHECK (a.Pending() == 0);
	}
	bool refused = false;
	CHECK (TileStream::Clear (tex.string(), false, &refused) == 0);
	CHECK (refused);
	flockErr = 0;
	nfs = true;
	CHECK_FALSE (TileStream::Busy (tex.string())); // the probe opens the file for writing
	int other = open ((tex / "TileStream" / "lock").c_str(), O_RDWR | O_CLOEXEC);
	REQUIRE (other >= 0);
	REQUIRE (flock (other, LOCK_EX | LOCK_NB) == 0); // another Orbiter's lock
	CHECK (TileStream::Busy (tex.string()));
	refused = false;
	CHECK (TileStream::Clear (tex.string(), false, &refused) == 0);
	CHECK (refused);
	close (other);
	CHECK_FALSE (TileStream::Busy (tex.string()));
}

TEST_CASE ("Evict counts and removes our Elev whose Surf is gone, never at L8 and below", "[TileStreamCore]")
{
	Tmp t;
	fs::path tex = t.root / "Earth", ts = tex / "TileStream";
	auto now = fs::file_time_type::clock::now();
	auto put = [&] (const fs::path &p, size_t n, int hoursAgo) { Put (p, n); fs::last_write_time (p, now - std::chrono::hours (hoursAgo)); };
	put (tex / "Elev" / Rel (9, 0, 5, "elv"), 1280 << 10, 10); // ours, its Surf gone
	put (tex / "Elev" / Rel (7, 0, 0, "elv"), 512 << 10, 20);  // ours, alone, a base level
	put (tex / "Surf" / Rel (9, 0, 6), 256 << 10, 1);
	put (tex / "Mask" / Rel (9, 0, 6), 256 << 10, 1);
	fs::create_directories (ts);
	std::ofstream (ts / "written.idx") << "9 0 6\n";
	std::ofstream (ts / "elev.idx") << "9 0 5\n7 0 0\n";
	TileStream::Options o = Opt ((t.root / "none.cfg").string(), tex);
	std::ofstream (o.cfgfile) << "BODY Earth\nSURF eox file:///none/{z}/{row}/{col} 1 16\nLIGHTS gibs file:///none/{n}/{row}/{col} 7\nWATER gibs file:///none/{n}/{row}/{col} 8\nEND\n";
	o.offline = true;
	o.capMB = 2; // 2.25 MB on disk: the alone Elev goes first, then 1 MB is under the cap
	{
		TileStream a (o);
		REQUIRE (WaitFor ([&] { return !fs::exists (tex / "Elev" / Rel (9, 0, 5, "elv")); }, 10.0));
		REQUIRE (WaitFor ([&] { return Read (ts / "elev.idx").find ("9 0 5") == std::string::npos; }, 5.0));
	}
	CHECK (fs::exists (tex / "Elev" / Rel (7, 0, 0, "elv")));
	CHECK (fs::exists (tex / "Surf" / Rel (9, 0, 6)));
	CHECK (Read (ts / "elev.idx").find ("7 0 0") != std::string::npos);
	CHECK (Read (ts / "written.idx") == "9 0 6\n");
}

TEST_CASE ("A request sends only the headers the manual lists, no cookies, and fails past its total time cap however slowly it moves", "[TileStreamCore]")
{
	App app;
	for (const char *v : {"http_proxy", "HTTP_PROXY", "all_proxy", "ALL_PROXY"}) unsetenv (v);
	TsNet net ("OrbiterLinux/test", 1000, 200, 1500);
	auto get = [&] (const std::string &url, QByteArray &body, int ms = 0) {
		std::atomic<int> st {-1};
		net.Get (url, [&] (int s, QByteArray got, int) { body = got; st = s; }, -1, 0, ms);
		WaitFor ([&] { return st >= 0; }, 20.0);
		return st.load();
	};
	Http h ({Reply ("200 OK", "Set-Cookie: id=42; Path=/\r\n", "ok"), Reply ("200 OK", "", "ok"), Reply ("200 OK", "", std::string (300, 'x')), Reply ("200 OK", "", std::string (40, 'y'))});
	QByteArray b;
	CHECK (get (h.Url(), b) == 200);
	CHECK (get (h.Url(), b) == 200);
	std::vector<std::string> req;
	{
		std::lock_guard<std::mutex> l (h.m);
		req = h.requests;
	}
	REQUIRE (req.size() == 2);
	for (const std::string &r : req) {
		CAPTURE (r);
		std::set<std::string> names;
		std::istringstream in (r);
		std::string line;
		std::getline (in, line); // GET /f HTTP/1.1
		while (std::getline (in, line) && line != "\r") {
			std::string n = line.substr (0, line.find (':'));
			std::transform (n.begin(), n.end(), n.begin(), [] (unsigned char c) { return (char)std::tolower (c); });
			names.insert (n);
		}
		CHECK (names == std::set<std::string> {"host", "user-agent", "accept-language", "accept-encoding", "connection"});
		CHECK (r.find ("User-Agent: OrbiterLinux/test\r\n") != std::string::npos);
		CHECK (r.find ("Accept-Language: en\r\n") != std::string::npos);
	}
	h.dripMs = 40; // about 340 bytes at one per 40 ms: 14 s, each byte well inside the 30 s stall timeout
	auto t0 = Clock::now();
	CHECK (get (h.Url(), b) == 0);
	double sec = std::chrono::duration<double> (Clock::now() - t0).count();
	CHECK (sec >= 1.0);
	CHECK (sec < 8.0);
	h.dripMs = 25; // about 2.5 s: past the 1.5 s default, inside the request's own 20 s cap (the cloud TIFF's LongMs)
	CHECK (get (h.Url(), b, 20000) == 200);
	CHECK (b == QByteArray (40, 'y'));
}
