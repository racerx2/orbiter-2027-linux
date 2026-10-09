// not upstream: unit tests of the ground impacts on CollFakeSdk: flat and sloped terrain, box colliders, gates, lockout, formulas (design CA-ground 3)
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "CollGroundA.h"
#include "CollDmgHost.h"
#include "CollFakeSdk.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

CollGroupData Box (const Vector &h, const Vector &c = Vector ())
{
	CollGroupData g;
	for (int i = 0; i < 8; i++) g.vtx.push_back (CollVtx { (float)(c.x + ((i & 1) ? h.x : -h.x)), (float)(c.y + ((i & 2) ? h.y : -h.y)), (float)(c.z + ((i & 4) ? h.z : -h.z)), 0, 0, 0, 0, 0 });
	const uint16_t f[36] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4, 2,6,3, 3,6,7, 0,4,2, 2,4,6, 1,3,5, 3,7,5 };
	g.idx.assign (f, f + 36);
	return g;
}

CollGroupData Plate (int n, double half, double x) // (n+1)^2 vertices in the plane at x, over [-half, half]^2 in y and z
{
	CollGroupData g;
	for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) g.vtx.push_back (CollVtx { (float)x, (float)(-half + 2 * half * i / n), (float)(-half + 2 * half * j / n), 0, 0, 0, 0, 0 });
	for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
		uint16_t a = (uint16_t)(j * (n + 1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + n + 1), d = (uint16_t)(c + 1);
		g.idx.insert (g.idx.end (), { a, b, c, b, d, c });
	}
	return g;
}

bool Near (const Vector &a, const Vector &b, double tol = 1e-6) { return (a - b).length () <= tol; }

struct Rig {
	CollFakeSdk sdk;
	CollCfgValues cfg;
	CollGroundA g { sdk, cfg };
	CollFakeSdk::Body *earth = nullptr;
	std::shared_ptr<CollRestMesh> mesh = std::make_shared<CollRestMesh> ();
	CollShape sh; CollAnim ca; CollTemplateCache cache; CollMeshInfo mi {};
	CollFakeSdk::Ves *v = nullptr;
	std::vector<CollImpactEvent> ev; std::vector<CollFxContact> fx;
	explicit Rig (std::vector<CollGroupData> grp = { Box (Vector (1, 1, 1)) })
	{
		sdk.bodies.push_back (CollFakeSdk::Body ());
		earth = &sdk.bodies.back ();
		earth->name = "Earth";
		sdk.gbody.push_back (earth);
		sdk.periodG = 0;
		mesh->name = "box";
		mesh->grp = std::move (grp);
		for (auto &x : mesh->grp) mesh->nvtx += (uint32_t)x.vtx.size ();
		mi.present = mi.collide = true; mi.serial = 1; mi.key = "box"; mi.rest = mesh;
		sh.Update (&mi, 1, ca, nullptr, 0, cache);
		v = sdk.AddVessel ("PB", "ShuttlePB");
		v->rd.R = IMatrix (); v->rd.m = 1000; v->rd.pmi = Vector (2, 3, 1); v->rd.gref = earth;
	}
	Vector Wp () const { return sdk.periodG ? Vector (0, 2 * kPi / sdk.periodG, 0) : Vector (); }
	Vector Low (const Matrix &R) const // the lowest mesh vertex for rotation R, vessel frame; up is global +x
	{
		Vector lo; double mn = 1e9;
		for (auto &gd : mesh->grp) for (auto &x : gd.vtx) { Vector p (x.x, x.y, x.z); if (mul (R, p).x < mn) mn = mul (R, p).x, lo = p; }
		return lo;
	}
	// lowest vertex hLow above the terrain at lng 0 lat 0 (global +x up), velocity dv relative to the surface, body spin w relative to the planet's
	void Place (const Matrix &R, double hLow, const Vector &dv = Vector (), const Vector &w = Vector ())
	{
		v->rd.R = R;
		v->rd.x = Vector (earth->size + earth->elev + hLow - mul (R, Low (R)).x, 0, 0);
		v->rd.v = crossp (v->rd.x, Wp ()) + dv;
		v->rd.w = w + tmul (R, Wp ());
	}
	size_t Frame (double dt = 0.02)
	{
		ev.clear (); fx.clear ();
		g.Frame (sdk.simT, dt, { CollGroundVessel { 1, v, &sh } }, ev, fx);
		sdk.simT += dt;
		return ev.size ();
	}
};

Matrix Tilt () { Matrix R; R.Set (Vector (0.1, 0.2, 0.3)); return R; }

// CollDmgHostOf over a view whose base lookup keys on nothing
struct LaxView {
	struct G { const CollShape *shape = nullptr; };
	struct S { bool present = false; uint32_t key = 0; uint16_t ngrp = 0; uint32_t nvtx = 0; std::shared_ptr<const CollRestMesh> rest; std::string name; uint32_t serial = 0; };
	struct K { double destroyEnergy = -1; };
	struct O { std::string planet, base, type; int32_t planetIdx = 0, baseIdx = 0; uint32_t obj = 0; int cls = 0; Vector size; double x = 0, z = 0; uint16_t mat = 0; CollH hPlanet = nullptr, hBase = nullptr; };
	O any; std::vector<S> none;
	const G *Geom (uint32_t) const { return nullptr; }
	const std::vector<S> &Slots (uint32_t) const { return none; }
	bool SlotNow (uint32_t, uint32_t, uint32_t &) const { return false; }
	void ClientMeshRebuilt (uint32_t, uint32_t, uint8_t) {}
	void WantSlots (uint32_t, bool) {}
	K Keys (uint32_t) const { return K (); }
	const O *BaseObject (int, int, int) const { return &any; }
	void Bases (std::vector<const O *> &v) const { v = { &any }; }
};
struct LaxIds { void *Vessel (uint32_t) const { return nullptr; } uint32_t IdOf (void *) { return 0; } };

}

TEST_CASE ("ground: a 20 m/s descent makes one event at the lowest vertex, normal down", "[ground]")
{
	Rig r;
	Matrix R = Tilt ();
	r.Place (R, 0.1, Vector (-20, 0, 0));
	r.sdk.simT = 3.5;
	REQUIRE (r.Frame () == 1);
	const CollImpactEvent &e = r.ev[0];
	CHECK (std::fabs (e.vn - 20) < 1e-6);
	CHECK (Near (e.s[0].c, r.Low (R), 1e-12));
	CHECK (Near (e.s[0].n, tmul (R, Vector (-1, 0, 0))));
	CHECK (e.s[0].owner.vesselId == 1);
	CHECK (e.s[0].owner.planet == -1);
	CHECK (e.s[0].mesh == 0);
	CHECK (e.s[0].grp == 0);
	REQUIRE (e.s[0].tri >= 0);
	REQUIRE (e.s[0].tri < 12);
	bool uses = false;
	for (int k = 0; k < 3; k++) { const CollVtx &x = r.mesh->grp[0].vtx[r.mesh->grp[0].idx[3 * e.s[0].tri + k]]; uses = uses || Near (Vector (x.x, x.y, x.z), e.s[0].c, 0); }
	CHECK (uses);
	CHECK (e.s[0].a == COLL_GROUND_PATCH);
	CHECK (e.s[0].tdir.length () == 0);
	CHECK (e.s[1].owner.vesselId == 0);
	CHECK (e.s[1].owner.planet == 0);
	CHECK (e.s[1].owner.base == -1);
	CHECK (e.s[1].owner.obj == -1);
	CHECK (e.s[1].mesh == -1);
	CHECK (Near (e.s[1].n, Vector (1, 0, 0)));
	CHECK (e.flags == COLLEV_FIRST);
	CHECK (e.t == 3.5);
	CHECK (e.vt < 1e-6);
	CHECK (r.g.events == 1);
	REQUIRE (r.fx.size () == 1);
	const CollFxContact &c = r.fx[0];
	CHECK (c.id == 1);
	CHECK (c.h == r.v);
	CHECK (c.building);
	CHECK_FALSE (c.playback);
	CHECK (c.flags == COLLEV_FIRST);
	CHECK (Near (c.c, r.Low (R), 1e-12));
	CHECK (Near (c.n, tmul (R, Vector (-1, 0, 0))));
	CHECK (Near (c.nOther, Vector (0, 1, 0)));
	CHECK (c.matOther == &DentMath::DefaultMaterial (DENTB_BLOCK));
	CHECK (std::fabs (c.vn - e.vn) < 1e-12);
	CHECK (c.Jn == e.Jn);
	CHECK (c.Jt == 0);
	CHECK (c.dt == 0.02);
}

TEST_CASE ("ground: the slip direction follows the tangential speed", "[ground]")
{
	Rig r;
	Matrix R = Tilt ();
	r.Place (R, 0.1, Vector (-20, 0, 30));
	REQUIRE (r.Frame () == 1);
	const CollImpactEvent &e = r.ev[0];
	CHECK (std::fabs (e.vn - 20) < 1e-6);
	CHECK (std::fabs (e.vt - 30) < 1e-6);
	Vector t = tmul (R, Vector (0, 0, 1));
	CHECK (Near (e.s[0].tdir, t));
	CHECK (Near (e.s[1].tdir, Vector (0, 0, -1)));
	CHECK (Near (r.fx[0].tdir, t));
	CHECK (std::fabs (r.fx[0].vt - 30) < 1e-6);
}

TEST_CASE ("ground: a resting landed vessel and a 3 m/s touch make none", "[ground]")
{
	Rig r;
	SECTION ("landed, turning with the planet") {
		r.sdk.periodG = 86164;
		r.Place (Tilt (), -0.01);
		for (int k = 0; k < 10; k++) CHECK (r.Frame () == 0);
		CHECK (r.g.tested == 8);
	}
	SECTION ("a descent on the turning planet: the surface speed is not an approach") {
		r.sdk.periodG = 86164;
		r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (std::fabs (r.ev[0].vn - 20) < 1e-6);
		CHECK (r.ev[0].vt < 1e-6);
	}
	SECTION ("3 m/s: below CollisionGroundMinSpeed") {
		r.Place (Tilt (), 0.03, Vector (-3, 0, 0));
		CHECK (r.Frame () == 0);
		r.Place (Tilt (), 0.03, Vector (-6, 0, 0));
		CHECK (r.Frame () == 1);
	}
	SECTION ("3 m/s with CollisionGroundMinSpeed 2") {
		r.cfg.groundMinSpeed = 2;
		r.Place (Tilt (), 0.03, Vector (-3, 0, 0));
		CHECK (r.Frame () == 1);
	}
	SECTION ("20 m/s still 0.1 m up at the end of the step") {
		r.Place (Tilt (), 0.5, Vector (-20, 0, 0));
		CHECK (r.Frame () == 0);
		CHECK (r.g.tested == 8);
	}
}

TEST_CASE ("ground: a second event only after 0.25 s, a time jump clears the wait", "[ground]")
{
	Rig r;
	r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
	const double dt = 0.0625;
	CHECK (r.Frame (dt) == 1);
	for (int k = 0; k < 3; k++) CHECK (r.Frame (dt) == 0);
	CHECK (r.sdk.simT == 0.25);
	CHECK (r.Frame (dt) == 1);
	CHECK (r.Frame (dt) == 0);
	r.g.TimeJump ();
	CHECK (r.Frame (dt) == 1);
	CHECK (r.g.events == 3);
	r.g.Drop (1);
	CHECK (r.Frame (dt) == 1);
}

TEST_CASE ("ground: debris, playback and the gates make none", "[ground]")
{
	Rig r;
	r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
	SECTION ("CollDebris") { r.v->cls = "colldebris"; CHECK (r.Frame () == 0); }
	SECTION ("playback") { r.v->rd.playback = true; CHECK (r.Frame () == 0); }
	SECTION ("CollisionGround FALSE") { r.cfg.ground = false; CHECK (r.Frame () == 0); CHECK (r.sdk.elevCalls == 0); }
	SECTION ("CollisionModel 0") { r.cfg.model = 0; CHECK (r.Frame () == 0); }
	SECTION ("zero step") { CHECK (r.Frame (0) == 0); }
	SECTION ("no gravity reference") { r.v->rd.gref = nullptr; CHECK (r.Frame () == 0); }
	SECTION ("the reference is not a planet") { r.earth->type = 3; CHECK (r.Frame () == 0); }
	SECTION ("the reference is not a body of the list") { r.sdk.gbody.clear (); CHECK (r.Frame () == 0); }
	SECTION ("no collider") {
		std::vector<CollImpactEvent> ev; std::vector<CollFxContact> fx;
		r.g.Frame (0, 0.02, { CollGroundVessel { 1, r.v, nullptr } }, ev, fx);
		CHECK (ev.empty ());
	}
	SECTION ("all on: one") { CHECK (r.Frame () == 1); }
}

TEST_CASE ("ground: far vessels make no terrain query, the near test one", "[ground]")
{
	Rig r;
	SECTION ("30 km up") {
		r.Place (Tilt (), 30e3, Vector (-20, 0, 0));
		CHECK (r.Frame () == 0);
		CHECK (r.sdk.elevCalls == 0);
	}
	SECTION ("20 m up: above bound radius + |v| dt + 0.5 m") {
		r.Place (IMatrix (), 20, Vector (-20, 0, 0));
		CHECK (r.Frame () == 0);
		CHECK (r.sdk.elevCalls == 1);
		CHECK (r.g.tested == 0);
	}
	SECTION ("event: five terrain samples and the refine") {
		r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
		CHECK (r.Frame () == 1);
		CHECK (r.sdk.elevCalls == 6);
	}
}

TEST_CASE ("ground: a spinning vessel uses the point velocity", "[ground]")
{
	Rig r ({ Box (Vector (0.5, 0.5, 4)) });
	SECTION ("the +z end comes down at 10 m/s") {
		r.Place (IMatrix (), 0.05, Vector (), Vector (0, 2.5, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (std::fabs (r.ev[0].vn - 10) < 1e-6);
		CHECK (std::fabs (r.ev[0].vt - 1.25) < 1e-6);
		CHECK (r.ev[0].s[0].c.z == 4);
		CHECK (r.ev[0].s[0].c.x == -0.5);
	}
	SECTION ("the other way: the -z end") {
		r.Place (IMatrix (), 0.05, Vector (), Vector (0, -2.5, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (r.ev[0].s[0].c.z == -4);
	}
	SECTION ("no spin: none") {
		r.Place (IMatrix (), 0.05);
		CHECK (r.Frame () == 0);
	}
}

TEST_CASE ("ground: meff and the impulse and energy of the event", "[ground]")
{
	CHECK (std::fabs (CollGroundMeff (1000, Vector (2, 3, 1), Vector (1, -2, 3), Vector (0, -1, 0)) - 1000 / 6.5) < 1e-9);
	CHECK (std::fabs (CollGroundMeff (1000, Vector (2, 3, 1), Vector (0, -2, 0), Vector (0, -1, 0)) - 1000) < 1e-9);
	CHECK (std::fabs (CollGroundMeff (1000, Vector (0, 3, 1), Vector (1, -2, 3), Vector (0, -1, 0)) - 500) < 1e-9); // pmi x locked
	CHECK (CollGroundMeff (0, Vector (2, 3, 1), Vector (1, -2, 3), Vector (0, -1, 0)) == 0);
	Rig r;
	Matrix R = Tilt ();
	r.Place (R, 0.1, Vector (-20, 0, 0));
	REQUIRE (r.Frame () == 1);
	const CollImpactEvent &e = r.ev[0];
	double m = CollGroundMeff (1000, Vector (2, 3, 1), e.s[0].c, e.s[0].n);
	CHECK (m < 1000);
	CHECK (std::fabs (e.meff - m) < 1e-9);
	CHECK (std::fabs (e.Jn - m * e.vn * (1 + COLL_GROUND_E)) < 1e-6);
	CHECK (std::fabs (e.dKE - 0.5 * m * e.vn * e.vn * (1 - COLL_GROUND_E * COLL_GROUND_E)) < 1e-6);
	CHECK (e.vn_post == COLL_GROUND_E * e.vn);
	CHECK (e.Jt == 0);
	CHECK (e.Wf == 0);
}

TEST_CASE ("ground: the terrain plane from the samples, the hit vertex refined at its own position", "[ground]")
{
	Rig r;
	SECTION ("slope 0.1 rising east") {
		const double k = 0.1;
		r.sdk.elevG = [&] (double lng, double) { return k * lng * r.earth->size; };
		r.Place (IMatrix (), 0.1, Vector (-20, 0, 0));
		REQUIRE (r.Frame () == 1);
		double s = std::sqrt (1 + k * k);
		CHECK (Near (r.ev[0].s[1].n, Vector (1, 0, -k) / s, 1e-4));
		CHECK (Near (r.fx[0].nOther, Vector (-k, 1, 0) / s, 1e-4));
		CHECK (std::fabs (r.ev[0].vn - 20 / s) < 1e-3);
		CHECK (r.ev[0].s[0].c.z == 1);
	}
	SECTION ("a hole under the hit vertex: none") {
		r.sdk.elevG = [] (double lng, double lat) { return lng != 0 && lat != 0 ? -50.0 : 0.0; };
		r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
		CHECK (r.Frame () == 0);
		CHECK (r.sdk.elevCalls == 6);
	}
}

TEST_CASE ("ground: hidden groups are not tested, at most 4096 vertices per vessel", "[ground]")
{
	SECTION ("keel group hidden") {
		Rig r ({ Box (Vector (1, 1, 1)), Box (Vector (0.2, 0.2, 0.2), Vector (-1.3, 0, 0)) });
		r.Place (IMatrix (), 0.1, Vector (-20, 0, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (r.ev[0].s[0].grp == 1);
		CHECK (r.ev[0].s[0].c.x == -1.5f);
		r.sh.SetGroupHidden (0, 1, true);
		r.g.TimeJump ();
		CHECK (r.Frame () == 0);
		r.Place (IMatrix (), -0.4, Vector (-20, 0, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (r.ev[0].s[0].grp == 0);
		CHECK (r.ev[0].s[0].c.x == -1);
	}
	SECTION ("10201 vertices: every third") {
		Rig r ({ Plate (100, 2, -0.5) });
		r.Place (IMatrix (), 0.1, Vector (-20, 0, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (r.g.tested == 3401);
	}
}

TEST_CASE ("ground: the ground side of an event, and no building lookup for base -1", "[ground]")
{
	CHECK (CollGroundSide (CollOwnerRef { 0, 2, -1, -1, -1 }));
	CHECK_FALSE (CollGroundSide (CollOwnerRef { 4, -1, -1, -1, -1 }));
	CHECK_FALSE (CollGroundSide (CollOwnerRef { 0, 2, 0, 3, 0 }));
	CHECK_FALSE (CollGroundSide (CollOwnerRef { 0, -1, -1, -1, -1 }));
	Rig r;
	CollFakeSdk::Body sun;
	sun.name = "Sun"; sun.type = 3;
	r.sdk.bodies.push_back (sun);
	r.sdk.gbody.insert (r.sdk.gbody.begin (), &r.sdk.bodies.back ());
	r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
	REQUIRE (r.Frame () == 1);
	CHECK (r.ev[0].s[1].owner.planet == 1);
	CHECK (CollGroundSide (r.ev[0].s[1].owner));
	LaxView lv; LaxIds li;
	CollDmgHostOf<LaxView, LaxIds> host (lv, li);
	CollDmgBaseObj o;
	CHECK (host.BaseObject (0, 0, 5, o));
	CHECK_FALSE (host.BaseObject (1, -1, -1, o));
}
