// not upstream: collision addon, unit tests of the base cfg parser, base matching and stock base counts (design E2-U8 ... E2-U10)
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "CollBaseA.h"
#include "CollFakeSdk.h"

TEST_CASE ("E2-U8 base cfg parser", "[CollBaseA]")
{
	CollBaseFile f; std::vector<std::string> w;
	std::string t = "BASE-V2.0\nName = Test\nLOCATION = 10 20\nBEGIN_OBJECTLIST\nBLOCK\n POS 1 0 2\n SCALE 10 5 4\n COLLMAT steel\nEND\n"
		"LPAD2\n POS 0 0 0\nEND\nHANGAR3\n POS 5 0 5\n SCALE 20 10 30\n NOCOLLIDE\nEND\nTANK\n SCALE 5 10 5\nEND\nMESH\n POS 0 0 0\nEND\nBLOCK\nEND\nEND_OBJECTLIST\n";
	REQUIRE (CollParseBaseFile (t, f, w));
	REQUIRE (f.name == "Test");
	REQUIRE (f.haveLocation);
	REQUIRE (f.obj.size () == 4); // MESH without FILE is a read error and ends the list
	REQUIRE (f.obj[0].collMat == "steel");
	REQUIRE (CollBaseObjIncluded (f.obj[0]));
	REQUIRE (!CollBaseObjIncluded (f.obj[1]));
	REQUIRE (!CollBaseObjIncluded (f.obj[2]));
	REQUIRE (CollBaseObjIncluded (f.obj[3]));
	CollFakeSdk s; CollDirs d;
	REQUIRE (CollBaseObjGeometry (f.obj[0], s, d, 6.371e6, false, w));
	REQUIRE (f.obj[0].grp.size () == 3);
	CollBaseFile h; w.clear ();
	REQUIRE (CollParseBaseFile ("BASE-V2.0\nBEGIN_OBJECTLIST\nHANGAR3\n SCALE 20 10 30\nEND\nEND_OBJECTLIST\n", h, w));
	REQUIRE (CollBaseObjGeometry (h.obj[0], s, d, 6.371e6, false, w));
	REQUIRE (h.obj[0].grp[0].idx.size () == 36);
}

TEST_CASE ("E2-U10 matching and pose check", "[CollBaseA]")
{
	CollFakeSdk s;
	s.bodies.push_back ({}); auto &E = s.bodies.back (); E.name = "Earth"; E.type = 4;
	s.bodies.push_back ({}); auto &B = s.bodies.back (); B.name = "Pad"; B.type = 20; B.lng = 0.1; B.lat = 0.2;
	E.base.push_back (&B); s.gbody.push_back (&E);
	s.File (".\\Config\\Earth.cfg", "Name = Earth\n");
	s.dirs[CollFakeSdk::Norm (".\\Config\\Earth\\Base\\")] = { "Pad.cfg", "pad2.CFG" };
	s.File (".\\Config\\Earth\\Base\\Pad.cfg", "BASE-V2.0\nName = Pad\nBEGIN_OBJECTLIST\nBLOCK\n SCALE 10 10 10\nEND\nEND_OBJECTLIST\n");
	CollBaseA b; CollDirs d;
	b.Build (s, d, true);
	REQUIRE (b.rec.size () == 1);
	REQUIRE (b.nIncluded == 1);
	REQUIRE (b.Object (0, 0, 0));
	REQUIRE (b.Object (0, 0, 0)->cls == 0);
	REQUIRE (b.Rec (0, 0)->shape.nObj () == 1);
	// pose: planet at 1 AU, base where the record says
	E.pos = Vector (1.496e11, 0, 0);
	B.pos = E.pos + b.rec[0]->rposP; B.R = b.rec[0]->rrotP;
	b.Poll (s, 0);
	REQUIRE (b.rec[0]->checkedOk);
	B.pos = B.pos + Vector (0.01, 0, 0);
	b.Poll (s, 100);
	REQUIRE (!b.rec[0]->checkedOk);
}

TEST_CASE ("E2-U9 stock bases, flat elevation", "[CollBaseA]")
{
	namespace fs = std::filesystem;
	std::string root = std::string (COLL_TEST_SRC) + "/Src/Celbody";
	uint32_t nb = 0, no = 0, ni = 0;
	std::error_code ec;
	for (auto &e : fs::recursive_directory_iterator (root, ec)) {
		if (e.path ().extension () != ".cfg" || e.path ().parent_path ().filename () != "Base") continue;
		std::ifstream f (e.path ()); std::ostringstream ss; ss << f.rdbuf ();
		std::string t = ss.str ();
		if (t.compare (0, 9, "BASE-V2.0")) continue;
		CollBaseFile bf; std::vector<std::string> w;
		CollParseBaseFile (t, bf, w);
		nb++;
		for (auto &o : bf.obj) { no++; if (CollBaseObjIncluded (o)) ni++; }
	}
	INFO ("bases " << nb << " objects " << no << " included " << ni);
	REQUIRE (nb >= 30);
	REQUIRE (ni > 500);
}
