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
	// rotated base at the recorded position must fail too
	B.pos = E.pos + b.rec[0]->rposP;
	b.rec[0]->checkedOk = true;
	b.Poll (s, 200);
	REQUIRE (b.rec[0]->checkedOk);
	double c = cos (1e-6), sn = sin (1e-6);
	B.R = b.rec[0]->rrotP * Matrix (c, 0, -sn, 0, 1, 0, sn, 0, c);
	b.Poll (s, 300);
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

namespace {
std::vector<CollVtx> FixVtx (const CollBaseObjDef &o) { std::vector<CollVtx> r; for (auto &g : o.grp) r.insert (r.end (), g.vtx.begin (), g.vtx.end ()); return r; }
}

TEST_CASE ("fix1 M4: object collider at its own terrain height (Baseobj.cpp Setup)", "[CollBaseA]")
{
	CollBaseFile f; std::vector<std::string> w;
	REQUIRE (CollParseBaseFile ("BASE-V2.0\nBEGIN_OBJECTLIST\nBLOCK\n POS 100 0 50\n SCALE 10 10 10\nEND\nEND_OBJECTLIST\n", f, w));
	CollFakeSdk s; CollDirs d;
	const double R = 6.371e6, l0 = 0.1, b0 = 0.2;
	CollBaseElev el;
	el.lng = l0; el.lat = b0; el.elev = 5;
	el.at = [&] (double l, double b) { return 5 + (l - l0) * R * cos (b0) * 0.01 - (b - b0) * R * 0.02; }; // 0.01 z + 0.02 x above the base
	REQUIRE (CollBaseObjGeometry (f.obj[0], s, d, R, false, w, &el));
	float ymin = 1e9f;
	for (auto &v : FixVtx (f.obj[0])) ymin = std::min (ymin, v.y);
	CHECK (std::fabs (ymin - 2.5f) < 1e-3f);
	REQUIRE (CollBaseObjGeometry (f.obj[0], s, d, R, false, w));
	ymin = 1e9f;
	for (auto &v : FixVtx (f.obj[0])) ymin = std::min (ymin, v.y);
	CHECK (ymin == 0.0f);
}

TEST_CASE ("fix1 M5: TANK NSTEP and HANGAR2 ROOFH shape the collider", "[CollBaseA]")
{
	CollFakeSdk s; CollDirs d;
	auto geom = [&] (const std::string &obj) {
		CollBaseFile f; std::vector<std::string> w;
		CollParseBaseFile ("BASE-V2.0\nBEGIN_OBJECTLIST\n" + obj + "END_OBJECTLIST\n", f, w);
		REQUIRE (f.obj.size () == 1);
		REQUIRE (CollBaseObjGeometry (f.obj[0], s, d, 6.371e6, false, w));
		return FixVtx (f.obj[0]);
	};
	CHECK (geom ("TANK\n NSTEP 4\n SCALE 5 10 5\nEND\n").size () == 14);
	CHECK (geom ("TANK\n SCALE 5 10 5\nEND\n").size () == 38);
	CHECK (geom ("TANK\n NSTEP -1\n SCALE 5 10 5\nEND\n").size () == 11);
	CHECK (geom ("TANK\n NSTEP 70000\n SCALE 5 10 5\nEND\n").size () == 49151);
	auto hasY = [] (const std::vector<CollVtx> &v, float y) { for (auto &x : v) if (std::fabs (x.y - y) < 1e-4f) return true; return false; };
	auto h1 = geom ("HANGAR2\n SCALE 10 10 10\n ROOFH 1\nEND\n"), h0 = geom ("HANGAR2\n SCALE 10 10 10\nEND\n");
	CHECK (hasY (h1, 9)); CHECK (!hasY (h1, 5));
	CHECK (hasY (h0, 5)); CHECK (!hasY (h0, 9));
}

TEST_CASE ("fix1: per-type Read ends the object list where the core's ends", "[CollBaseA]")
{
	auto count = [] (const std::string &objs) {
		CollBaseFile f; std::vector<std::string> w;
		CollParseBaseFile ("BASE-V2.0\nBEGIN_OBJECTLIST\n" + objs + "END_OBJECTLIST\n", f, w);
		return f.obj.size ();
	};
	const std::string blk = "BLOCK\n SCALE 10 10 10\nEND\n";
	CHECK (count ("LPAD2\n POS 0 0 0\n NAV abc\nEND\n" + blk) == 0);     // Lpad02::ParseLine: parse error 2
	CHECK (count ("LPAD1\n NAV 112.5\nEND\n" + blk) == 2);
	CHECK (count ("RUNWAY\n END1 0 0 0\n END2 100 0 0\n POS 1 2\nEND\n" + blk) == 2); // Runway::Read has no POS
	CHECK (count ("RUNWAYLIGHTS\n END1 0 0 0\n SCALE a\nEND\n" + blk) == 2);
	CHECK (count ("BEACONARRAY\n ROT x\nEND\n" + blk) == 2);
	CHECK (count ("TRAIN2\n END1 0 0 0\n POS 1\nEND\n" + blk) == 2);
	CHECK (count ("SOLARPLANT\n POS 1 2\n SCALE 3\nEND\n" + blk) == 2);
	CHECK (count ("RUNWAY\n RWTEX end\nEND\n" + blk) == 1);           // RWTEX overwrites the label: the stray END ends the list
	CHECK (count ("TRAIN1\n TEX end 2\n" + blk) == 2);
}

TEST_CASE ("fix2 M12: BEGIN_OBJECTLIST found as the core's FindLine", "[CollBaseA]")
{
	auto count = [] (const std::string &t) { CollBaseFile f; std::vector<std::string> w; CollParseBaseFile (t, f, w); return f.obj.size (); };
	const std::string blk = "BLOCK\n SCALE 10 10 10\nEND\nEND_OBJECTLIST\n";
	CHECK (count ("BASE-V2.0\n; " + std::string (300, 'x') + "\nBEGIN_OBJECTLIST\n" + blk) == 1); // long line before the list
	CHECK (count ("BASE-V2.0\n; " + std::string (3000, 'x') + "\nBEGIN_OBJECTLIST\n" + blk) == 1);
	CHECK (count ("BASE-V2.0\n  BEGIN_OBJECTLIST\n" + blk) == 0);                                      // indented: not at column 0
	CHECK (count ("BASE-V2.0\nBEGIN_OBJECTLIST ;c\n" + blk) == 1);                                    // prefix match
	CHECK (count ("BASE-V2.0\nbegin_objectlist\n" + blk) == 1);
	CHECK (count ("BASE-V2.0\n" + std::string (1023, 'x') + "BEGIN_OBJECTLIST\n" + blk) == 1);       // the rest of a long line is read as a line
}

TEST_CASE ("fix2: MESH with a bare FILE line keeps the object (strdup of the empty value)", "[CollBaseA]")
{
	CollBaseFile f; std::vector<std::string> w;
	REQUIRE (CollParseBaseFile ("BASE-V2.0\nBEGIN_OBJECTLIST\nMESH\n FILE\nEND\nBLOCK\n SCALE 10\nEND\nEND_OBJECTLIST\n", f, w));
	REQUIRE (f.obj.size () == 2);
	CHECK (f.obj[0].type == "MESH");
	CHECK (f.obj[0].meshFile.empty ());
	CHECK (f.obj[1].index == 1);
	CollFakeSdk s; CollDirs d;
	CHECK (!CollBaseObjGeometry (f.obj[0], s, d, 6.371e6, false, w));
}

TEST_CASE ("fix2: COLLIDE on a WRAPTOSURFACE mesh follows the terrain per vertex (MapToAltitude)", "[CollBaseA]")
{
	CollFakeSdk s; CollDirs d;
	std::string m = "MSHX1\nGROUPS 1\nGEOM 4 2\n-10 0 -10 0 1 0 0 0\n10 0 -10 0 1 0 0 0\n10 0 10 0 1 0 0 0\n-10 0 10 0 1 0 0 0\n0 2 1\n0 3 2\n";
	s.File (".\\Meshes\\pad.msh", m);
	const double R = 6.371e6, l0 = 0.1, b0 = 0.2;
	CollBaseElev el;
	el.lng = l0; el.lat = b0; el.elev = 5;
	el.at = [&] (double l, double b) { return 5 + (l - l0) * R * cos (b0) * 0.01 - (b - b0) * R * 0.02; }; // 0.01 z + 0.02 x above the base
	auto ys = [&] (const char *extra) {
		CollBaseFile f; std::vector<std::string> w;
		REQUIRE (CollParseBaseFile (std::string ("BASE-V2.0\nBEGIN_OBJECTLIST\nMESH\n FILE pad\n POS 100 0 50\n COLLIDE\n") + extra + "END\nEND_OBJECTLIST\n", f, w));
		REQUIRE (CollBaseObjIncluded (f.obj[0]));
		REQUIRE (CollBaseObjGeometry (f.obj[0], s, d, R, false, w, &el));
		std::vector<CollVtx> v;
		for (auto &g : f.obj[0].grp) v.insert (v.end (), g.vtx.begin (), g.vtx.end ());
		return v;
	};
	for (auto &v : ys (" WRAPTOSURFACE\n")) CHECK (std::fabs (v.y - (0.02f * v.x + 0.01f * v.z)) < 1e-3f);
	for (auto &v : ys ("")) CHECK (std::fabs (v.y - 2.5f) < 1e-3f);
}
