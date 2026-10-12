// not upstream: the Launchpad Earth streaming page: the base list on the shipped base cfgs, and when Clear and Download are refused

#include <catch2/catch_test_macros.hpp>
#include "StreamBases.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>

namespace fs = std::filesystem;

struct Dir {
	fs::path root;
	Dir ()
	{
		root = fs::temp_directory_path() / ("tilestream-lp-test-" + std::to_string (getpid()));
		fs::remove_all (root);
		fs::create_directories (root);
	}
	~Dir () { std::error_code ec; fs::remove_all (root, ec); }
};

TEST_CASE ("Every shipped Earth base cfg gives a named base at its location, sorted by name", "[TileStreamLaunchpad]")
{
	const fs::path dir = "Config/Earth/Base";
	REQUIRE (fs::is_directory (dir));
	size_t cfgs = 0;
	for (const auto &e : fs::directory_iterator (dir)) cfgs += e.path().extension() == ".cfg";
	REQUIRE (cfgs >= 30);
	std::vector<StreamBase> b = StreamBaseList (dir.string());
	CHECK (b.size() == cfgs);
	std::set<std::string> names;
	for (size_t i = 0; i < b.size(); i++) {
		names.insert (b[i].name);
		CHECK_FALSE (b[i].name.empty());
		CHECK (std::fabs (b[i].lng) <= 180.0);
		CHECK (std::fabs (b[i].lat) <= 90.0);
		if (i) CHECK (b[i - 1].name <= b[i].name);
	}
	CHECK (names.size() == b.size());
	auto find = [&] (const std::string &n) { for (const StreamBase &x : b) if (x.name == n) return x; return StreamBase(); };
	StreamBase ksc = find ("Cape Canaveral");
	CHECK (std::fabs (ksc.lng + 80.65165) < 1e-9);
	CHECK (std::fabs (ksc.lat - 28.58438) < 1e-9);
	StreamBase p = find ("Peenem\xc3\xbcnde"); // UTF-8 as in the file
	CHECK (std::fabs (p.lng - 13.9) < 1e-9);
	CHECK (std::fabs (p.lat - 54.1) < 1e-9);
	CHECK (StreamBaseList ("Config/Earth/NoSuchFolder").empty());
}

TEST_CASE ("A base needs Name and Location before BEGIN_; spaces, case and CR are taken", "[TileStreamLaunchpad]")
{
	Dir d;
	auto put = [&] (const char *f, const std::string &s) { std::ofstream (d.root / f, std::ios::binary) << s; };
	put ("a.cfg", "BASE-V2.0\r\n  name =  Alpha Base \r\nLOCATION\t= -10.5 +20.25\r\nBEGIN_OBJECTLIST\r\nName = Wrong\r\nEND_OBJECTLIST\r\n");
	put ("b.cfg", "Name = Bravo\nBEGIN_SURFTILELIST\nLocation = 1 2\n"); // Location after BEGIN_: not a base
	put ("c.cfg", "Location = 3 4\n");                                       // no name
	put ("d.cfg", "Name = Delta\nLocation = east\n");                        // no numbers
	put ("e.txt", "Name = Echo\nLocation = 5 6\n");                          // not a cfg
	put ("f.cfg", "Name = Charlie\nLocation = 7 -8 ; comment\n");
	std::vector<StreamBase> b = StreamBaseList (d.root.string());
	REQUIRE (b.size() == 2);
	CHECK (b[0].name == "Alpha Base");
	CHECK (b[0].lng == -10.5);
	CHECK (b[0].lat == 20.25);
	CHECK (b[1].name == "Charlie");
	CHECK (b[1].lng == 7.0);
	CHECK (b[1].lat == -8.0);
	StreamBase x;
	CHECK_FALSE (StreamReadBase ((d.root / "b.cfg").string(), x));
	CHECK_FALSE (StreamReadBase ((d.root / "none.cfg").string(), x));
}

TEST_CASE ("Clear and Download are refused while the other runs, in a session, offline or when the cache is busy", "[TileStreamLaunchpad]")
{
	StreamState idle;
	CHECK (StreamRefusal (StreamAct::Clear, idle).empty());
	CHECK (StreamRefusal (StreamAct::Download, idle).empty());
	auto with = [] (bool StreamState::*f) { StreamState s; s.*f = true; return s; };
	for (auto a : {StreamAct::Clear, StreamAct::Download}) {
		CHECK (StreamRefusal (a, with (&StreamState::session)).find ("session") != std::string::npos);
		CHECK (StreamRefusal (a, with (&StreamState::busy)).find ("Another Orbiter") != std::string::npos);
		CHECK (StreamRefusal (a, with (&StreamState::clearing)).find ("cleared") != std::string::npos);
		CHECK (StreamRefusal (a, with (&StreamState::pendingClear)).find ("cleared") != std::string::npos);
		CHECK (StreamRefusal (a, with (&StreamState::fetching)).find ("download") != std::string::npos);
	}
	CHECK (StreamRefusal (StreamAct::Download, with (&StreamState::offline)).find ("Offline") != std::string::npos);
	CHECK (StreamRefusal (StreamAct::Clear, with (&StreamState::offline)).empty()); // clearing makes no requests
	StreamState s = with (&StreamState::session);
	s.clearing = s.busy = true;
	CHECK (StreamRefusal (StreamAct::Download, s).find ("session") != std::string::npos); // the session goes first
	s.session = false;
	CHECK (StreamRefusal (StreamAct::Download, s).find ("cleared") != std::string::npos); // then what the page itself runs
}
