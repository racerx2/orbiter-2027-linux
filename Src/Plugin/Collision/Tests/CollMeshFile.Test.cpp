// not upstream: collision addon, unit tests of the own .msh parser, present table, Orbiter.cfg reader and paths (design E2-U1, E2-U2)
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <sys/resource.h>
#include "CollMeshFile.h"
#include "CollTestMsh.h"

namespace {
std::string Slurp (const std::string &p) { std::ifstream f (p); std::ostringstream s; s << f.rdbuf (); return s.str (); }
const char *kGrp = "GEOM 3 1\n0 0 0 0 0 1 0 0\n1 0 0 0 0 1 1 0\n0 1 0 0 0 1 0 1\n0 1 2\n";
}

TEST_CASE ("E2-U1 present table", "[CollMeshFile]")
{
	CollRestMesh m;
	REQUIRE (!CollParseMsh ("", "a", m));
	REQUIRE (CollParseMsh ("MSHX2\nGROUPS 1\n", "a", m)); REQUIRE (m.grp.empty ());
	REQUIRE (!CollParseMsh ("MSHX1\nfoo\n", "a", m));
	REQUIRE (!CollParseMsh ("MSHX1\n" + std::string (300, 'x') + "\nGROUPS 1\n", "a", m));
	REQUIRE (CollParseMsh ("MSHX1\nGROUPS\n", "a", m)); REQUIRE (m.grp.empty ());
	REQUIRE (CollParseMsh ("MSHX1\r\nGROUPS 2\n" + std::string (kGrp) + "GEOM 3 1\n0 0 0\n", "a", m));
	REQUIRE (m.grp.size () == 1);
	REQUIRE (CollParseMsh ("MSHX1\nGROUPS 1\nFLAG 2\n" + std::string (kGrp), "a", m));
	REQUIRE (m.grp[0].usrflag == 2);
	REQUIRE (m.nvtx == 3);
}

TEST_CASE ("E2-U1 numbers as glibc sscanf", "[CollMeshFile]")
{
	std::mt19937 rng (7);
	const char *fixed[] = { "1e", "1e+", "2E-", "0x1p3", "0x10", "-0x1.8p1", "1.5e3", "+2.25", "-0", "1e-50", "3.4e39", ".5", "5.", "nan", "inf" };
	auto check = [](const std::string &s) {
		INFO ("input '" << s << "'");
		float a = 0, b = 0;
		int na = sscanf (s.c_str (), "%f", &a);
		const char *p = s.c_str (), *e = p + s.size ();
		bool ok = CollScanFloat (p, e, b);
		REQUIRE ((na == 1) == ok);
		if (ok && a == a) { uint32_t ua, ub; memcpy (&ua, &a, 4); memcpy (&ub, &b, 4); REQUIRE (ua == ub); }
		if (ok) { // the rest of the line as glibc leaves it
			float c1 = 0, c2 = 0; int n = 0;
			sscanf (s.c_str (), "%f%n", &c1, &n);
			REQUIRE ((size_t)(p - s.c_str ()) == (size_t)n);
			(void)c2;
		}
	};
	for (auto *f : fixed) check (f);
	std::uniform_int_distribution<int> d (0, 9), k (0, 7);
	for (int i = 0; i < 200000; i++) {
		std::string s;
		if (k (rng) == 0) s += '-';
		for (int j = 0, n = 1 + d (rng) % 6; j < n; j++) s += char ('0' + d (rng));
		if (k (rng) < 4) { s += '.'; for (int j = 0, n = d (rng) % 8; j < n; j++) s += char ('0' + d (rng)); }
		if (k (rng) < 2) { s += 'e'; if (k (rng) < 3) s += (k (rng) & 1) ? '-' : '+'; for (int j = 0, n = d (rng) % 3; j < n; j++) s += char ('0' + d (rng)); }
		check (s);
	}
}

TEST_CASE ("E2-U1 stock meshes equal the test loader", "[CollMeshFile]")
{
	std::string root = std::string (COLL_TEST_SRC);
	int n = 0;
	std::error_code ec;
	for (auto &e : std::filesystem::recursive_directory_iterator (root + "/Src/Vessel", ec)) {
		if (e.path ().extension () != ".msh") continue;
		std::string text = Slurp (e.path ().string ());
		CollRestMesh a, b;
		bool pa = CollParseMsh (text, e.path ().string ().c_str (), a);
		bool pb = CollTestParseMsh (text, e.path ().string ().c_str (), b);
		if (!pb) continue;
		REQUIRE (pa);
		REQUIRE (a.grp.size () == b.grp.size ());
		for (size_t g = 0; g < a.grp.size (); g++) {
			REQUIRE (a.grp[g].vtx.size () == b.grp[g].vtx.size ());
			REQUIRE (0 == memcmp (a.grp[g].vtx.data (), b.grp[g].vtx.data (), a.grp[g].vtx.size () * sizeof (CollVtx)));
			REQUIRE (a.grp[g].idx == b.grp[g].idx);
			REQUIRE (a.grp[g].usrflag == b.grp[g].usrflag);
		}
		n++;
	}
	REQUIRE (n >= 30);
}

TEST_CASE ("E2-U2 Orbiter.cfg reader", "[CollMeshFile]")
{
	CollOrbCfg c;
	c.SetText ("; c\nMeshDirX = no\nMeshDir = Meshes2 ; comment\r\nMeshDir = second\nConfigDir = \nPropStages = 3 x\nStabiliseSLimit = 0.05\nStabiliseOrbits = true1\nPropSubsampling = abc\nlast = 1");
	std::string v;
	REQUIRE (c.String ("MeshDir", v)); REQUIRE (v == "no");
	CollDirs d;
	c.Dirs (d);
	REQUIRE (d.meshDir == "no\\");
	REQUIRE (d.configDir == ".\\Config\\");
	int i = 0; REQUIRE (c.Int ("PropStages", i)); REQUIRE (i == 3);
	REQUIRE (!c.Int ("PropSubsampling", i));
	double r = 0; REQUIRE (c.Real ("StabiliseSLimit", r)); REQUIRE (r == 0.05);
	bool b = false; REQUIRE (c.Bool ("StabiliseOrbits", b)); REQUIRE (b);
	REQUIRE (!c.String ("last", v)); // the core needs a good stream after the match
	CollOrbCfg l;
	l.SetText (std::string (600, 'x') + "\nMeshDir = a\n");
	REQUIRE (!l.String ("MeshDir", v));
	CollOrbCfg w;
	w.SetText ("MeshDir = " + std::string (256, 'a') + "\n");
	REQUIRE (!w.String ("MeshDir", v));
	CollOrbCfg f;
	f.SetText ("StabiliseOrbits = yes\nX = FALSEY\n");
	b = true; REQUIRE (!f.Bool ("StabiliseOrbits", b)); REQUIRE (b);
	REQUIRE (f.Bool ("X", b)); REQUIRE (!b);
	REQUIRE (CollMeshPath (d, std::string (260, 'm'), ".msh").empty ());
	REQUIRE (CollMeshPath (d, "DG/deltaglider", ".msh") == "no\\DG/deltaglider.msh");
}

TEST_CASE ("fix1: GEOM counts beyond the bytes left allocate nothing", "[CollMeshFile]")
{
	auto peak = [] { struct rusage u; getrusage (RUSAGE_SELF, &u); return (long)u.ru_maxrss; }; // kB
	long p0 = peak ();
	CollRestMesh m;
	REQUIRE (CollParseMsh ("MSHX1\nGROUPS 1\nGEOM 60000000 0\n", "x", m));
	CHECK (m.grp.empty ());
	REQUIRE (CollParseMsh ("MSHX1\nGROUPS 2\n" + std::string (kGrp) + "GEOM 3 60000000\n0 0 0\n1 0 0\n0 1 0\n0 1 2\n", "x", m));
	CHECK (m.grp.size () == 1);
	CHECK (peak () - p0 < 50000);
	REQUIRE (CollParseMsh ("MSHX1\nGROUPS 1\nGEOM 3 1\n\n\n\n0 1 2", "x", m)); // a line per count is enough
	CHECK (m.grp.size () == 1);
}
