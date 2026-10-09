// not upstream: unit tests of E1's SDK-facing session part on CollFakeSdk (quiet scene, contact frame, zero step, notices, warp)
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
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
	W.sdk.applyWrites = true;
	const double g0 = W.v[1]->rd.x.x - W.v[0]->rd.x.x - 2.0;
	W.Frame (0.1);
	const double u1 = W.v[1]->rd.v.x - W.v[0]->rd.v.x, g1 = W.v[1]->rd.x.x - W.v[0]->rd.x.x - 2.0; // speculative: approach cut to the gap
	W.Frame (0.1);
	const double u2 = W.v[1]->rd.v.x - W.v[0]->rd.v.x, g2 = W.v[1]->rd.x.x - W.v[0]->rd.x.x - 2.0; // touch: the write separates the pair
	CAPTURE (W.ps->Stats ().spec, W.ps->Stats ().writes, g0, u1, g1, u2, g2);
	REQUIRE (W.sdk.Count ().Writes () > 0);
	REQUIRE (u1 > -4.0 + 1e-3);
	REQUIRE (g1 >= -COLLA_DEV_TOL);
	REQUIRE (u2 >= -1e-9);
	REQUIRE (g2 > g1);
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

namespace {
Vector PointG (const Vector &x, double GM) { double r = x.length (); return x*(-GM/(r*r*r)); }
Vector LeoRef (Vector x, Vector v, double GM, double h)   // RK4 in 1000 steps
{
	const int n = 1000; double k = h/n;
	for (int i = 0; i < n; i++) {
		Vector a1 = PointG (x, GM), v1 = v;
		Vector a2 = PointG (x + v1*(0.5*k), GM), v2 = v + a1*(0.5*k);
		Vector a3 = PointG (x + v2*(0.5*k), GM), v3 = v + a2*(0.5*k);
		Vector a4 = PointG (x + v3*k, GM), v4 = v + a3*k;
		x += (v1 + v2*2.0 + v3*2.0 + v4)*(k/6.0); v += (a1 + a2*2.0 + a3*2.0 + a4)*(k/6.0);
	}
	return x;
}
}

TEST_CASE ("fix1: playback and frozen bodies in LEO are predicted with gravity (h 0.1, error below 1 mm)", "[CollWorldA]")
{
	const double GM = 6.67259e-11*5.97e24, r = 6.771e6, h = 0.1;
	for (int kind : { COLLB_PLAYBACK, COLLB_FROZEN }) {
		World W;
		W.sdk.bodies.push_back (CollFakeSdk::Body ());
		CollFakeSdk::Body *earth = &W.sdk.bodies.back ();
		const Vector x (r*0.6, r*0.8, 0), v (-0.8*7672.0, 0.6*7672.0, 0);
		CollFakeSdk::Ves *a = W.Add ("A", x, v);
		CollFakeSdk::Ves *b = W.Add ("B", x + Vector (0, 0, 500), v);
		a->rd.gref = b->rd.gref = earth;
		b->rd.aTot = PointG (b->rd.x, GM)*b->rd.m;
		if (kind == COLLB_PLAYBACK) { a->rd.playback = true; a->rd.aTot = PointG (Vector (r, 0, 0), GM)*a->rd.m; } // the cache from before the playback started
		else { a->rd.aTot = PointG (x, GM)*a->rd.m; CollPhysAsm m; m.member = { 1 }; m.root = 1; m.mixed = true; m.memberHash = 1; W.geom.asmb = { m }; }
		W.Frame (h);
		const CollABody *P = nullptr;
		for (const CollABody &B : W.ps->Bodies ()) if (B.kind == kind) P = &B;
		REQUIRE (P);
		double err = (P->kin.c1 - LeoRef (x, v, GM, h)).length ();
		CAPTURE (kind, err);
		REQUIRE (err < 1e-3);
		REQUIRE (W.sdk.misuse == 0);
	}
}

TEST_CASE ("fix1 R2: the weight cached before a state write is turned into the written attitude (stale GetWeightVector)", "[CollWorldA]")
{
	const double GM = 6.67259e-11*5.97e24, r = 6.771e6;
	World W;
	W.sdk.applyWrites = true;
	W.sdk.bodies.push_back (CollFakeSdk::Body ());
	CollFakeSdk::Body *earth = &W.sdk.bodies.back ();
	CollFakeSdk::Ves *a = W.Add ("A", Vector (r, 0, 0), Vector (0, 7672.0, 0));
	W.Add ("B", Vector (r, 0, 1000), Vector (0, 7672.0, 0));
	Matrix R0; R0.Set (Vector (0.3, -0.2, 1.1));
	a->rd.R = R0; a->rd.gref = earth;
	const Vector g = PointG (a->rd.x, GM)*(1.0 + 1e-3);     // the core's field: not the point mass alone
	a->rd.W = tmul (R0, g)*a->rd.m;                        // cached by an earlier read this frame; the fake, like the core, does not refresh it on a write
	W.Frame (0.1);
	REQUIRE (!W.ps->Bodies ().empty ());
	int ia = -1;
	for (size_t i = 0; i < W.ps->Bodies ().size (); i++) if (W.ps->Bodies ()[i].id == 1) ia = (int)i;
	REQUIRE (ia >= 0);
	const CollABody &B = W.ps->Bodies ()[ia];
	CollAWrite w {};
	w.body = ia; w.state = w.attitude = w.weight = true;
	w.x = B.x; w.v = B.v; w.q = B.q; CollRotate (w.q, Vector (0.1, 0.25, -0.3));
	std::vector<CollAWrite> list { w };
	W.ps->WriteBack (list);
	REQUIRE ((int)W.ps->GExact ().size () > ia);
	Vector ge = W.ps->GExact ()[ia];
	CAPTURE (ge.x, ge.y, ge.z, g.x, g.y, g.z);
	REQUIRE ((ge - g).length () <= 1e-12*g.length ());
	REQUIRE (W.sdk.misuse == 0);
}

TEST_CASE ("fix2: a state write below the terrain is kept and logged once per vessel", "[CollWorldA]")
{
	for (double elev : { 0.0, 300.0 }) {
		World W;
		W.sdk.bodies.push_back (CollFakeSdk::Body ());
		CollFakeSdk::Body *earth = &W.sdk.bodies.back ();
		earth->elev = elev;
		const double R0 = earth->size + 100.0;
		CollFakeSdk::Ves *a = W.Add ("A", Vector (R0, -1.15, 0), Vector (0, 2, 0));
		CollFakeSdk::Ves *b = W.Add ("B", Vector (R0, 1.15, 0), Vector (0, -2, 0));
		a->rd.gref = b->rd.gref = earth;
		for (int f = 0; f < 4; f++) W.Frame (0.1);
		std::vector<CollH> st;
		for (const CollFakeSdk::Wr &x : W.sdk.wr) if (x.op == 'S' && std::find (st.begin (), st.end (), x.h) == st.end ()) st.push_back (x.h);
		int states = 0;
		for (const CollFakeSdk::Wr &x : W.sdk.wr) if (x.op == 'S') states++;
		CAPTURE (elev, st.size (), states, W.sdk.LogCount ("below the terrain"));
		REQUIRE (!st.empty ());
		REQUIRE (W.sdk.LogCount ("below the terrain") == (elev > 0 ? (int)st.size () : 0));
		REQUIRE (W.sdk.misuse == 0);
	}
}

TEST_CASE ("fix2: a landed playback vessel moves with its cache, not with predicted gravity", "[CollWorldA]")
{
	const double r = 6.371e6;
	World W;
	W.sdk.bodies.push_back (CollFakeSdk::Body ());
	CollFakeSdk::Body *earth = &W.sdk.bodies.back ();
	CollFakeSdk::Ves *a = W.Add ("A", Vector (r, 0, 0), Vector ());
	CollFakeSdk::Ves *b = W.Add ("B", Vector (r, 0, 500), Vector ());
	a->rd.gref = b->rd.gref = earth;
	a->rd.playback = true; a->rd.status = 1; a->rd.aTot = Vector (0, 0, 0);
	W.Frame (0.1);
	const CollABody *P = nullptr;
	for (const CollABody &B : W.ps->Bodies ()) if (B.kind == COLLB_PLAYBACK) P = &B;
	REQUIRE (P);
	CAPTURE (P->kin.a0.x, P->kin.a0.y, P->kin.a0.z, P->gEst.length ());
	REQUIRE (P->gEst.length () > 9.0);
	REQUIRE (P->kin.a0.length () < 1e-9);
	REQUIRE ((P->kin.c1 - P->x).length () < 1e-9);
	REQUIRE (W.sdk.misuse == 0);
}
