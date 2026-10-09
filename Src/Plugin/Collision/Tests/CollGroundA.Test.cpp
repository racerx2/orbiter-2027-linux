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

CollGroupData Tip (const Vector &t) // one small triangle whose first corner is t, the others 0.5 m above it (+x)
{
	CollGroupData g;
	g.vtx = { CollVtx { (float)t.x, (float)t.y, (float)t.z, 0, 0, 0, 0, 0 }, CollVtx { (float)(t.x + 0.5), (float)(t.y + 0.1), (float)t.z, 0, 0, 0, 0, 0 },
		CollVtx { (float)(t.x + 0.5), (float)t.y, (float)(t.z + 0.1), 0, 0, 0, 0, 0 } };
	g.idx = { 0, 1, 2 };
	return g;
}

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
		v->rd.R = IMatrix (); v->rd.m = 1000; v->rd.pmi = Vector (2, 3, 1); v->rd.gref = v->rd.sref = earth;
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
	CHECK (std::fabs (e.vn - 20) < 1e-4);
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
	CHECK (e.s[1].owner.vesselId == 0);
	CHECK (e.s[1].owner.planet == 0);
	CHECK (e.s[1].owner.base == -1);
	CHECK (e.s[1].owner.obj == -1);
	CHECK (e.s[1].mesh == -1);
	CHECK (Near (e.s[1].n, Vector (1, 0, 0)));
	CHECK (e.flags == COLLEV_FIRST);
	CHECK (e.t == 3.5);
	CHECK (e.vt < 1e-4);
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
	CHECK (std::fabs (e.vn - 20) < 1e-4);
	CHECK (std::fabs (e.vt - 30) < 1e-4);
	Vector t = tmul (R, Vector (0, 0, 1));
	CHECK (Near (e.s[0].tdir, t));
	CHECK (Near (e.s[1].tdir, Vector (0, 0, -1)));
	CHECK (Near (r.fx[0].tdir, t));
	CHECK (std::fabs (r.fx[0].vt - 30) < 1e-4);
}

TEST_CASE ("ground: a resting landed vessel and a 3 m/s touch make none", "[ground]")
{
	Rig r;
	SECTION ("landed, turning with the planet") {
		r.sdk.periodG = 86164;
		r.Place (Tilt (), -0.01);
		for (int k = 0; k < 10; k++) CHECK (r.Frame () == 0);
		CHECK (r.g.tested == 0);
		CHECK (r.sdk.elevCalls == 0);
	}
	SECTION ("a descent on the turning planet: the surface speed is not an approach") {
		r.sdk.periodG = 86164;
		r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (std::fabs (r.ev[0].vn - 20) < 1e-4);
		CHECK (r.ev[0].vt < 1e-4);
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

TEST_CASE ("ground: a second event of one contact region only after 0.25 s, a time jump clears the wait", "[ground]")
{
	Rig r ({ Box (Vector (0.5, 0.5, 4)) });
	const double dt = 0.0625;
	auto hit = [&] (double w) { r.Place (IMatrix (), 0.05, Vector (), Vector (0, w, 0)); return r.Frame (dt); };
	CHECK (hit (2.5) == 1);
	for (int k = 0; k < 3; k++) CHECK (hit (2.5) == 0);
	CHECK (r.sdk.simT == 0.25);
	CHECK (hit (2.5) == 1);
	CHECK (hit (2.5) == 0);
	r.g.TimeJump ();
	CHECK (hit (2.5) == 1);
	CHECK (r.g.events == 3);
	r.g.Drop (1);
	CHECK (hit (2.5) == 1);
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
	SECTION ("no surface reference") { r.v->rd.sref = nullptr; CHECK (r.Frame () == 0); }
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
	SECTION ("event: five terrain samples, the refine and the local normal") {
		r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
		CHECK (r.Frame () == 1);
		CHECK (r.sdk.elevCalls == 8);
	}
}

TEST_CASE ("ground: a spinning vessel uses the point velocity", "[ground]")
{
	Rig r ({ Box (Vector (0.5, 0.5, 4)) });
	SECTION ("the +z end comes down at 10 m/s") {
		r.Place (IMatrix (), 0.05, Vector (), Vector (0, 2.5, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (std::fabs (r.ev[0].vn - 10) < 1e-4);
		CHECK (std::fabs (r.ev[0].vt - 1.25) < 1e-4);
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
		CHECK (r.sdk.elevCalls >= 6);
		CHECK (r.sdk.elevCalls <= 5 + 4 * 3);
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

TEST_CASE ("ground review 1: the lockout is per contact region; a frame with no candidate or a much faster approach re-arms", "[ground]")
{
	Rig r ({ Box (Vector (0.5, 0.5, 4)) });
	auto hit = [&] (double w, double h = 0.05) { r.Place (IMatrix (), h, Vector (), Vector (0, w, 0)); return r.Frame (); };
	REQUIRE (hit (2.5) == 1);
	CHECK (r.ev[0].s[0].c.z == 4);
	SECTION ("the other end right after: its own region") {
		REQUIRE (hit (-2.5) == 1);
		CHECK (r.ev[0].s[0].c.z == -4);
		CHECK (hit (2.5) == 0);
		CHECK (hit (-2.5) == 0);
	}
	SECTION ("a frame with no candidate re-arms") {
		CHECK (hit (2.5) == 0);
		CHECK (hit (2.5, 3.0) == 0);
		CHECK (hit (2.5) == 1);
	}
	SECTION ("1.5 times the last approach fires again") {
		CHECK (hit (3.5) == 0);
		CHECK (hit (3.9) == 1);
		CHECK (std::fabs (r.ev[0].vn - 15.6) < 1e-4);
	}
}

TEST_CASE ("ground review 2: the fastest approach wins over a deeper resting vertex; a rejected refine falls back to the next", "[ground]")
{
	SECTION ("a keel buried at the other end") {
		Rig r ({ Box (Vector (0.5, 0.5, 4)), Box (Vector (0.2, 0.2, 0.2), Vector (-0.7, 0, -3.5)) });
		r.Place (IMatrix (), -0.5, Vector (), Vector (0, 2.5, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (r.ev[0].s[0].c.z == 4);
		CHECK (std::fabs (r.ev[0].vn - 10) < 1e-4);
	}
	SECTION ("a hole under the fastest vertex") {
		Rig r ({ Box (Vector (0.5, 0.5, 4)) });
		double rp = r.earth->size;
		r.sdk.elevG = [rp] (double lng, double lat) { return lng > 3 / rp && lat > 0.3 / rp && lat < 0.7 / rp ? -50.0 : 0.0; };
		r.Place (IMatrix (), 0.05, Vector (), Vector (0, 2.5, -0.1));
		REQUIRE (r.Frame () == 1);
		CHECK (r.ev[0].s[0].c.y == -0.5);
		CHECK (r.ev[0].s[0].c.z == 4);
		CHECK (std::fabs (r.ev[0].vn - 9.95) < 1e-4);
	}
}

TEST_CASE ("ground review 3: the surface reference, not the gravity reference", "[ground]")
{
	Rig r;
	CollFakeSdk::Body mars;
	mars.name = "Mars"; mars.pos = Vector (2e11, 0, 0); mars.size = 3.39e6;
	r.sdk.bodies.push_back (mars);
	r.sdk.gbody.insert (r.sdk.gbody.begin (), &r.sdk.bodies.back ());
	r.v->rd.gref = &r.sdk.bodies.back ();
	r.Place (Tilt (), 0.1, Vector (-20, 0, 0));
	REQUIRE (r.Frame () == 1);
	CHECK (r.ev[0].s[1].owner.planet == 1);
	CHECK (std::fabs (r.ev[0].vn - 20) < 1e-4);
}

TEST_CASE ("ground review 4: the normal is the terrain's at the hit vertex, not the plane over the bound radius", "[ground]")
{
	Rig r ({ Box (Vector (0.5, 0.5, 8)) });
	double rp = r.earth->size;
	r.sdk.elevG = [rp] (double, double lat) { return lat * rp > 7 ? 0.5 : 0.0; };
	r.Place (IMatrix (), 0.05, Vector (0, 200, 0));
	CHECK (r.Frame () == 0);
	r.sdk.elevG = [rp] (double, double lat) { return 0.1 * lat * rp; };
	r.Place (IMatrix (), 0.05, Vector (0, 200, 0));
	REQUIRE (r.Frame () == 1);
	double s = std::sqrt (1.01);
	CHECK (Near (r.ev[0].s[1].n, Vector (1, -0.1, 0) / s, 1e-4));
	CHECK (std::fabs (r.ev[0].vn - 20 / s) < 1e-3);
}

TEST_CASE ("ground review 6: a slope rising within the bound radius is near though the footprint is far below", "[ground]")
{
	Rig r ({ Box (Vector (0.5, 0.5, 8)) });
	double rp = r.earth->size;
	r.sdk.elevG = [rp] (double lng, double) { return 2 * lng * rp; };
	r.v->rd.x = Vector (rp + 16.8, 0, 0);
	r.v->rd.v = Vector (0, 0, 30);
	REQUIRE (r.Frame () == 1);
	CHECK (r.ev[0].s[0].c.z == 8);
	CHECK (std::fabs (r.ev[0].vn - 60 / std::sqrt (5.0)) < 1e-3);
	CHECK (Near (r.ev[0].s[1].n, Vector (1, 0, -2) / std::sqrt (5.0), 1e-4));
}

TEST_CASE ("ground review 7: the prediction looks at most 0.05 s ahead under time warp", "[ground]")
{
	Rig r;
	r.Place (IMatrix (), 5, Vector (-20, 0, 0));
	CHECK (r.Frame (1.0) == 0);
	r.Place (IMatrix (), 0.5, Vector (-20, 0, 0));
	CHECK (r.Frame (1.0) == 1);
}

TEST_CASE ("ground review 8: each part's lowest vertex is always tested, the stride start turns every frame", "[ground]")
{
	auto where = [] (const CollShape &sh, const Vector &t) { // running index of the vertex at t over all parts
		uint32_t base = 0;
		for (uint32_t k = 0; k < sh.nPart (); k++) {
			const CollGeom &G = sh.Part (k).Geom ();
			for (uint32_t i = 0; i < G.vtx.size (); i++) if (Near (G.Pos (i), t, 1e-6)) return base + i;
			base += (uint32_t)G.vtx.size ();
		}
		return ~0u;
	};
	SECTION ("a tip below a plate of 10201 vertices") {
		Vector t (-1, 0, 0);
		Rig r ({ Plate (100, 2, -0.5), Tip (t) });
		uint32_t at = where (r.sh, t);
		REQUIRE (at != ~0u);
		REQUIRE (at % 3 != 0);
		r.Place (IMatrix (), 0.1, Vector (-20, 0, 0));
		REQUIRE (r.Frame () == 1);
		CHECK (Near (r.ev[0].s[0].c, t, 1e-6));
	}
	SECTION ("a fast tip above the plate on a turning boom") {
		Vector t (-0.45, 0, 4);
		Rig r ({ Plate (100, 2, -0.5), Tip (t) });
		uint32_t at = where (r.sh, t);
		REQUIRE (at != ~0u);
		REQUIRE (at % 3 != 0);
		size_t n = 0;
		for (int k = 0; k < 3; k++) { r.Place (IMatrix (), 0.1, Vector (), Vector (0, 2.4, 0)); n += r.Frame (); if (n) break; }
		REQUIRE (n == 1);
		CHECK (Near (r.ev[0].s[0].c, t, 1e-6));
	}
}

TEST_CASE ("ground: the event's impulse goes to the vessel, the hit point leaves at 0.2 of its approach, no second event", "[ground]")
{
	Rig r;
	r.sdk.applyWrites = true;
	Matrix R = Tilt ();
	r.Place (R, 0.1, Vector (-20, 3, 1));
	r.sdk.simT = 1;
	Vector v0 = r.v->rd.v, w0 = r.v->rd.w;
	REQUIRE (r.Frame () == 1);
	const CollImpactEvent &e = r.ev[0];
	CHECK (r.g.kicks == 1);
	Vector p = e.s[0].c, up (1, 0, 0);                              // box: part frame = vessel frame; terrain up is global +x
	CHECK (std::fabs (((r.v->rd.v - v0) & up) * r.v->rd.m - e.Jn) <= 1e-9 * e.Jn); // momentum along the normal: Jn
	CHECK ((r.v->rd.v - v0 - up * ((r.v->rd.v - v0) & up)).length () <= 1e-5);        // along the local normal: up to the curvature over the box
	Vector vp0 = v0 + mul (R, crossp (p, w0)), vp1 = r.v->rd.v + mul (R, crossp (p, r.v->rd.w));
	CHECK (std::fabs ((vp0 & up) + e.vn) < 1e-5);                    // approached at vn
	CHECK (std::fabs ((vp1 & up) - COLL_GROUND_E * e.vn) < 1e-5);    // leaves at e vn
	r.Place (R, 0.1, r.v->rd.v - crossp (r.v->rd.x, r.Wp ()), r.v->rd.w - tmul (R, r.Wp ()));
	CHECK (r.Frame () == 0);                                          // the point now leaves the ground
	r.v->sv = r.v;                                                    // a docked stack takes it as a force over one step
	r.Place (R, 0.1, Vector (-20, 0, 0));
	r.sdk.simT += 1;
	auto nF = [&] () { size_t n = 0; for (auto &w : r.sdk.wr) if (w.op == 'F') n++; return n; };
	size_t nf = nF ();
	REQUIRE (r.Frame () == 1);
	CHECK (nF () == nf + 1);
}
