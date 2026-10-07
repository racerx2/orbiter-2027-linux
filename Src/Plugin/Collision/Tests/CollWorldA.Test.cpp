// not upstream: unit tests of E1's SDK-facing session part on CollFakeSdk (quiet scene, contact frame, zero step, notices, warp)
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "CollWorldA.h"
#include "CollFakeSdk.h"
#include "CollisionAPI.h"

namespace {

CollGroupData BoxMesh (const Vector &h)
{
	CollGroupData g;
	for (int i = 0; i < 8; i++) g.vtx.push_back (CollVtx { (float)((i & 1) ? h.x : -h.x), (float)((i & 2) ? h.y : -h.y), (float)((i & 4) ? h.z : -h.z), 0, 0, 0, 0, 0 });
	const uint16_t f[36] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4, 2,6,3, 3,6,7, 0,4,2, 2,4,6, 1,3,5, 3,7,5 };
	g.idx.assign (f, f + 36);
	return g;
}

struct Geom : CollPhysGeom {
	CollGroupData g = BoxMesh (Vector (1, 1, 1));
	CollGeom geom;
	Geom () { CollSrcGroup s { &g, CollSrc { 0, 0, 0, 0, 0 } }; geom.Build (&s, 1, COLL_WELD_DEFAULT, nullptr); }
	bool Parts (uint32_t id, std::vector<CollPartRef> &fwd, std::vector<CollPartRef> &past, double &rmax) override
	{
		CollPartRef p {};
		p.geom = &geom; p.skin = COLL_SKIN_DEFAULT; p.owner = CollOwnerKey { COLLO_VESSEL, id, -1, -1, -1, -1 };
		fwd = { p }; past = { p }; rmax = std::sqrt (3.0) + COLL_SKIN_DEFAULT;
		return true;
	}
};
struct Host : CollSolveHost {
	CollDetect *det = nullptr;
	CollSMat Material (const CollPairResult &, int, int) override { return CollSMat (); }
	void Feature (const CollPairResult &r, int i, int side, CollImpactSide &s) override { s.owner = CollOwnerRefOf (det->Owner (r, i, side)); }
};

struct World {
	CollFakeSdk sdk;
	Geom geom;
	CollCfgValues cfg;
	std::unique_ptr<CollPhysSession> ps;
	Host host;
	std::vector<CollFakeSdk::Ves *> v;
	World ()
	{
		cfg.model = 1; cfg.check = true;
		ps.reset (new CollPhysSession (sdk, geom, cfg));
		host.det = &ps->fwd;
		ps->Start (nullptr);
	}
	CollFakeSdk::Ves *Add (const std::string &n, const Vector &x, const Vector &vel)
	{
		CollFakeSdk::Ves *p = sdk.AddVessel (n);
		p->rd.x = x; p->rd.v = vel; p->rd.R = IMatrix (); p->rd.m = 1000; p->rd.pmi = Vector (2.0/3, 2.0/3, 2.0/3);
		v.push_back (p);
		return p;
	}
	void Frame (double h)
	{
		std::vector<CollPhysVessel> list;
		for (size_t i = 0; i < v.size (); i++) list.push_back (CollPhysVessel { (uint32_t)i + 1, v[i] });
		ps->PS1Snapshot (sdk.simT, h, list);
		ps->PS3Physics (host);
		ps->PS5Notices ();
		ps->PS6Warp ();
		for (CollFakeSdk::Ves *p : v) p->rd.x += p->rd.v*h;     // the fake does not integrate; free flight is enough here
		sdk.simT += h; sdk.sysT += h;
	}
};

} // namespace

TEST_CASE ("E1 session: a quiet scene makes no state-changing call", "[CollWorldA]")
{
	World W;
	W.Add ("A", Vector (0, 0, 0), Vector (1, 0, 0));
	W.Add ("B", Vector (100, 0, 0), Vector (1, 0, 0));
	for (int f = 0; f < 120; f++) W.Frame (1.0/60);
	REQUIRE (W.sdk.Count ().Writes () == 0);
	REQUIRE (W.sdk.misuse == 0);
	REQUIRE (W.ps->Events ().empty ());
	REQUIRE (W.sdk.LogCount ("Collision prop:") == 1);
}

TEST_CASE ("E1 session: an approaching pair is written, a zero step writes nothing", "[CollWorldA]")
{
	World W;
	W.Add ("A", Vector (-1.15, 0, 0), Vector (2, 0, 0));
	W.Add ("B", Vector (1.15, 0, 0), Vector (-2, 0, 0));
	W.Frame (0.0);
	REQUIRE (W.sdk.Count ().Writes () == 0);
	W.Frame (0.1);
	CAPTURE (W.ps->Stats ().spec, W.ps->Stats ().writes);
	REQUIRE (W.sdk.Count ().Writes () > 0);
	REQUIRE (W.sdk.misuse == 0);
	REQUIRE (W.ps->Stats ().checkFail == 0);
}

TEST_CASE ("A13 CONTACT notice layout", "[CollWorldA]")
{
	REQUIRE (sizeof (COLLA_HDR) == 12);
	REQUIRE (offsetof (COLLA_CONTACTINFO, flags) == 12);
	REQUIRE (offsetof (COLLA_CONTACTINFO, simt) % 8 == 0);
}
