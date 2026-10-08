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
	std::vector<CollPhysAsm> asmb;
	void Assemblies (std::vector<CollPhysAsm> &out) override { out = asmb; }
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

TEST_CASE ("E1 bases: planet-fixed velocity and acceleration as Orbiter's landed branch (review CA-F 1, 13)", "[CollWorldA]")
{
	const double PI = 3.14159265358979323846, T = 86164.1, r = 6371010.0;
	Matrix Rp; Rp.Set (Vector (0.41, 1.3, -0.2));
	const Vector pp (1.4e11, 2.0e9, -3.0e10), pv (2.9e4, 10.0, -2.0e3), w = mul (Rp, Vector (0, 1, 0))*(2.0*PI/T);
	for (double lat : { 28.6, -45.0, 0.0 })
		for (double lng : { -80.6, 120.0 }) {
			double cl = std::cos (lat*PI/180), sl = std::sin (lat*PI/180), cn = std::cos (lng*PI/180), sn = std::sin (lng*PI/180);
			Vector x = pp + mul (Rp, Vector (r*cl*cn, r*sl, r*cl*sn)); // Body.h:91
			double vg = 2.0*PI*r*cl/T;
			Vector vOrb = pv + mul (Rp, Vector (-vg*sn, 0, vg*cn));   // Vessel.cpp:4752-4754
			CAPTURE (lat, lng);
			REQUIRE ((CollSurfaceVel (pp, pv, w, x) - vOrb).length () <= 1e-9);
			Vector axis = pp + mul (Rp, Vector (0, r*sl, 0));
			Vector aRef = (axis - x)*(4.0*PI*PI/(T*T));
			REQUIRE ((CollSurfaceAcc (pp, Vector (1, 2, 3), w, x) - Vector (1, 2, 3) - aRef).length () <= 1e-12);
		}
}

TEST_CASE ("E1 session: an attachment tree turns with the root's PMI, a stack with the composite (review CA-F 3)", "[CollWorldA]")
{
	for (bool stack : { false, true }) {
		World W;
		CollFakeSdk::Ves *a = W.Add ("A", Vector (0, 0, 0), Vector ());
		CollFakeSdk::Ves *b = W.Add ("B", Vector (0, 10, 0), Vector ());
		a->rd.pmi = Vector (2, 3, 4); b->rd.m = 500;
		CollPhysAsm x; x.member = { 1, 2 }; x.root = 1; x.stack = stack; x.memberHash = 77;
		W.geom.asmb = { x };
		W.Frame (0.1);
		REQUIRE (W.ps->Bodies ().size () == 1);
		const CollABody &B = W.ps->Bodies ()[0];
		CAPTURE (stack, B.pmi.x, B.pmi.y, B.pmi.z);
		if (!stack) REQUIRE ((B.pmi - Vector (2, 3, 4)).length () == 0.0);
		else REQUIRE (B.pmi.x > 4.0*3.0);
	}
}

TEST_CASE ("E1 session: a stack write puts its CG where planned and pushes at the CG (review CA-F 2, 6)", "[CollWorldA]")
{
	World W;
	W.sdk.applyWrites = true;
	CollFakeSdk::Ves *a = W.Add ("A", Vector (0, 0, 0), Vector ());
	CollFakeSdk::Ves *b = W.Add ("B", Vector (0, 2.6, 0), Vector ());
	for (CollFakeSdk::Ves *p : { a, b }) p->sv = &W;
	a->rd.svcg = Vector (0, 1.3, 0); b->rd.svcg = Vector (0, -1.3, 0);
	CollPhysAsm x; x.member = { 1, 2 }; x.root = 1; x.stack = true; x.memberHash = 12;
	W.geom.asmb = { x };
	W.Frame (0.1);
	REQUIRE (W.ps->Bodies ().size () == 1);
	const CollABody &B = W.ps->Bodies ()[0];
	REQUIRE ((B.x - Vector (0, 1.3, 0)).length () <= 1e-12);
	CollAWrite w {};
	w.body = 0; w.state = w.attitude = w.force = true;
	w.x = B.x + Vector (0.1, -0.05, 0.2); w.v = Vector (0.3, 0, 0); w.wb = Vector (0, 0, 0.1);
	w.q = B.q; CollRotate (w.q, Vector (0, 0, 0.2));
	w.Fb = Vector (100, 0, 50);
	std::vector<CollAWrite> list { w };
	W.sdk.wr.clear ();
	W.ps->WriteBack (list);
	Vector cg = a->rd.x + mul (a->rd.R, a->rd.svcg);
	CAPTURE (cg.x, cg.y, cg.z);
	REQUIRE ((cg - w.x).length () <= 1e-12);
	int forces = 0;
	for (const CollFakeSdk::Wr &c : W.sdk.wr)
		if (c.op == 'F' && (c.a - w.Fb).length () == 0.0) { forces++; REQUIRE ((c.b - a->rd.svcg).length () == 0.0); }
	REQUIRE (forces == 1);
}

TEST_CASE ("E1 session: LANDED motion carries the landed acceleration, groundNew on the first ground frame (review CA-F 11, 13)", "[CollWorldA]")
{
	World W;
	W.sdk.bodies.push_back (CollFakeSdk::Body ());
	CollFakeSdk::Body *earth = &W.sdk.bodies.back ();
	CollFakeSdk::Ves *a = W.Add ("A", Vector (0, 6371010, 0), Vector ());
	CollFakeSdk::Ves *b = W.Add ("B", Vector (100, 0, 0), Vector ());
	a->rd.status = 1; a->rd.gref = earth; a->rd.aTot = Vector (0, -0.03, 0)*a->rd.m;
	W.Frame (0.1);
	const CollABody *L = nullptr;
	for (const CollABody &x : W.ps->Bodies ()) if (x.kind == COLLB_LANDED) L = &x;
	REQUIRE (L);
	REQUIRE ((L->kin.a0 - Vector (0, -0.03, 0)).length () <= 1e-15);
	REQUIRE ((L->kin.c1 - (L->x + L->v*0.1 + Vector (0, -0.03, 0)*0.005)).length () <= 1e-9);
	for (int f = 0; f < 3; f++) {
		b->rd.ground = f >= 1;
		W.Frame (0.1);
		const CollABody *D = nullptr;
		for (const CollABody &x : W.ps->Bodies ()) if (x.kind == COLLB_DYNAMIC) D = &x;
		REQUIRE (D);
		CAPTURE (f);
		REQUIRE (D->groundNew == (f == 1));
	}
}

TEST_CASE ("A13 CONTACT notice layout", "[CollWorldA]")
{
	REQUIRE (sizeof (COLLA_HDR) == 12);
	REQUIRE (offsetof (COLLA_CONTACTINFO, flags) == 12);
	REQUIRE (offsetof (COLLA_CONTACTINFO, simt) % 8 == 0);
}
