// not upstream: collision addon, unit tests of the streamed building colliders (design bldg-collision 7)
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>
#include "CollAddonFrame.h"
#include "CollBaseA.h"
#include "CollBldgA.h"
#include "CollFakeSdk.h"

namespace {
const double R0 = 6.371e6;
const double LAT0 = 0.5, LNG0 = -1.4; // the reference point [rad]
const double PI_T = 3.14159265358979323846;

Vector Up (double lat, double lng) { return Vector (cos (lat) * cos (lng), sin (lat), cos (lat) * sin (lng)); }
Vector East (double lng) { return Vector (-sin (lng), 0.0, cos (lng)); }
Vector North (double lat, double lng) { return Vector (-sin (lat) * cos (lng), cos (lat), -sin (lat) * sin (lng)); }
Vector Cross (const Vector &a, const Vector &b) { return Vector (a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); } // cross (b-a, c-a) of a front face points out (BldgGeom Tri)

void LatLng (double de, double dn, double &lat, double &lng) { lat = LAT0 + dn / R0; lng = LNG0 + de / (R0 * cos (LAT0)); } // de east, dn north of the reference [m]

void TileOf (double lat, double lng, int &x, int &y)
{
	int n = 1 << 14;
	x = (int)std::floor ((lng / (2.0 * PI_T) + 0.5) * n);
	y = (int)std::floor ((1.0 - std::asinh (std::tan (lat)) / PI_T) * 0.5 * n);
}

void TileCentre (int x, int y, double &lat, double &lng)
{
	double n = (double)(1 << 14);
	lng = (x + 0.5) / n * 2.0 * PI_T - PI_T;
	lat = std::atan (std::sinh (PI_T * (1.0 - 2.0 * (y + 0.5) / n)));
}

typedef std::vector<std::array<double, 2>> Ring; // east, north; clockwise seen from above

// one .bld building as TsBldg::Build writes it: wall quads (p0, p1 bottom, p1, p0 top), then the roof as a fan
void Poly (CollFakeSdk::BldgT &t, double lat, double lng, const Ring &ring, float hmin, float hmax)
{
	CollBldgRec b { lat, lng, hmin, hmax, (uint32_t)(t.vtx.size () / 6), 0, (uint32_t)t.idx.size (), 0 };
	float bot = hmin > 0 ? hmin : -4.0f, top = hmax;
	uint32_t nv = 0;
	auto vtx = [&] (double e, double h, double n, double nx, double ny, double nz) {
		t.vtx.insert (t.vtx.end (), { (float)e, (float)h, (float)n, (float)nx, (float)ny, (float)nz });
		return nv++;
	};
	for (size_t k = 0; k < ring.size (); k++) {
		const auto &p0 = ring[k], &p1 = ring[(k + 1) % ring.size ()];
		double de = p1[0] - p0[0], dn = p1[1] - p0[1], l = std::hypot (de, dn);
		uint32_t i0 = vtx (p0[0], bot, p0[1], -dn / l, 0, de / l);
		vtx (p1[0], bot, p1[1], -dn / l, 0, de / l);
		vtx (p1[0], top, p1[1], -dn / l, 0, de / l);
		vtx (p0[0], top, p0[1], -dn / l, 0, de / l);
		t.idx.insert (t.idx.end (), { i0, i0 + 1, i0 + 2, i0, i0 + 2, i0 + 3 });
	}
	uint32_t r0 = nv;
	for (const auto &p : ring) vtx (p[0], top, p[1], 0, 1, 0);
	for (uint32_t k = 1; k + 1 < ring.size (); k++) t.idx.insert (t.idx.end (), { r0, r0 + k, r0 + k + 1 });
	b.nv = nv;
	b.ni = (uint32_t)t.idx.size () - b.i0;
	t.b.push_back (b);
}

Ring Box (double w, double d) { return { { -w / 2, -d / 2 }, { -w / 2, d / 2 }, { w / 2, d / 2 }, { w / 2, -d / 2 } }; } // w east, d north

Ring NGon (double r, int n)
{
	Ring q;
	for (int k = 0; k < n; k++) q.push_back ({ r * cos (-2.0 * PI_T * k / n), r * sin (-2.0 * PI_T * k / n) });
	return q;
}

struct Rig {
	CollFakeSdk s;
	CollFakeSdk::Body *E = nullptr;
	CollFakeSdk::Ves *v = nullptr;
	CollCfgValues cfg;
	CollBaseA bases;
	std::unique_ptr<CollBldgA> b;
	double r = 5;                                 // the vessel's bound radius
	Rig ()
	{
		s.bodies.push_back ({});
		E = &s.bodies.back ();
		E->name = "Earth"; E->type = 4; E->size = R0;
		s.gbody.push_back (E);
		s.bldgBodyB.push_back (E);
		s.periodG = 0;                            // not turning: the ground speed is the global speed
		v = s.AddVessel ("V");
		b = std::make_unique<CollBldgA> (s, cfg);
	}
	double Ground (double lat, double lng) { return s.Elevation (E, lng, lat); }
	CollFakeSdk::BldgT &Add (double de, double dn, const Ring &ring, float hmin, float hmax) // a building de east, dn north of the reference, in its tile
	{
		double lat, lng;
		LatLng (de, dn, lat, lng);
		int x, y;
		TileOf (lat, lng, x, y);
		CollFakeSdk::BldgT &t = s.bldgB[{ (CollH)E, x, y }];
		Poly (t, lat, lng, ring, hmin, hmax);
		return t;
	}
	void At (double de, double dn, double h, const Vector &vel = Vector ())
	{
		double lat, lng;
		LatLng (de, dn, lat, lng);
		v->rd.x = Up (lat, lng) * (R0 + Ground (lat, lng) + h);
		s.velE[v] = vel;
	}
	void Start () { b->Start (&bases); }
	void Frame (double dt = 0.02) { b->Frame (s.simT, { CollBldgA::Live { (CollH)v, r } }); s.simT += dt; s.sysT += dt; }
	Vector Planet (const CollBldgA::Body &B, const Vector &q) const { return B.rposP + mul (B.rrotP, q); } // tile frame -> planet frame
	std::vector<uint32_t> Parts () const { std::vector<uint32_t> p; for (auto &B : b->Bodies ()) for (auto &q : B.part) p.push_back (q.index); std::sort (p.begin (), p.end ()); return p; }
};
}

TEST_CASE ("bldg: a box building's collider at Up (R + g) + East x + Up y + North z within 1 mm", "[CollBldgA]")
{
	Rig r;
	r.s.elevG = [] (double, double lat) { return 12.0 + 2000.0 * (lat - LAT0); }; // a slope: 12 m at the reference, rising north
	r.Add (60, 40, Box (20, 30), 0.0f, 15.0f);
	r.At (0, 0, 10);
	r.Start ();
	REQUIRE (r.b->On ());
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	const CollBldgA::Body &B = r.b->Bodies ()[0];
	REQUIRE (B.part.size () == 1);
	CHECK (B.part[0].index == 0);
	CHECK (B.slot == 0);
	CHECK (B.planet == 0);
	CHECK (B.rmax > 0);
	double lat, lng;
	LatLng (60, 40, lat, lng);
	double g = r.Ground (lat, lng);
	Vector ub = Up (lat, lng), eb = East (lng), nb = North (lat, lng);
	std::vector<Vector> want;
	for (double x : { -10.0, 10.0 }) for (double z : { -15.0, 15.0 }) for (double y : { -4.0, 15.0 }) want.push_back (ub * (R0 + g) + eb * x + ub * y + nb * z);
	const CollGeom &G = *B.part[0].geom;
	std::vector<int> hit (want.size (), 0);
	for (const Vector &q : G.vtx) {
		Vector P = r.Planet (B, q);
		double best = 1e9;
		size_t k = 0;
		for (size_t j = 0; j < want.size (); j++) if ((P - want[j]).length () < best) { best = (P - want[j]).length (); k = j; }
		CHECK (best < 1e-3);
		hit[k]++;
	}
	for (int h : hit) CHECK (h >= 1);
	CHECK (G.tri.size () == 10); // 4 walls of 2 triangles, the roof's 2
	std::string name;
	REQUIRE (r.b->Name (0, CollBldgA::BaseOf (B.slot), 0, name));
	int tx, ty;
	TileOf (lat, lng, tx, ty);
	CHECK (name == "building Earth z14 " + std::to_string (tx) + "/" + std::to_string (ty) + " #0");
	CHECK_FALSE (r.b->Name (0, -1, 0, name));
	CHECK (r.s.misuse == 0);
}

TEST_CASE ("bldg: an untagged 10 x 8 m house gets the client's gable roof, eave 5.0 m and ridge 7.80 m; 30 x 20 m keeps its flat roof", "[CollBldgA]")
{
	Rig r;
	r.Add (50, 0, Box (10, 8), 0.0f, 5.0f);     // TsBldg's untagged height
	r.Add (-60, 0, Box (30, 20), 0.0f, 5.0f);
	r.At (0, 0, 10);
	r.Start ();
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	const CollBldgA::Body &B = r.b->Bodies ()[0];
	REQUIRE (B.part.size () == 2);
	double ridge = 0.5 * 8.0 * std::tan (35.0 * PI_T / 180.0) + 5.0; // BldgGeom: rise min (0.5 short side tan 35, 4.5) over the eave
	for (const CollBldgA::Part &p : B.part) {
		double lat, lng;
		LatLng (p.index == 0 ? 50 : -60, 0, lat, lng);
		Vector base = Up (lat, lng) * (R0 + r.Ground (lat, lng)), ub = Up (lat, lng);
		double top = -1e9;
		int eave = 0;
		for (const Vector &q : p.geom->vtx) {
			double h = dotp (r.Planet (B, q) - base, ub);
			top = std::max (top, h);
			if (std::fabs (h - 5.0) < 1e-3) eave++;
		}
		if (p.index == 0) {
			CHECK (std::fabs (ridge - 7.80) < 0.01);
			CHECK (std::fabs (top - ridge) < 1e-3);
			CHECK (eave >= 4);
		} else CHECK (std::fabs (top - 5.0) < 1e-3);
	}
}

TEST_CASE ("bldg: the base-frame axes, a wall facing east in the .bld faces east in the planet frame", "[CollBldgA]")
{
	Rig r;
	r.Add (60, 40, Box (20, 30), 0.0f, 15.0f);
	r.At (0, 0, 10);
	r.Start ();
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	const CollBldgA::Body &B = r.b->Bodies ()[0];
	const CollGeom &G = *B.part[0].geom;
	double lat, lng;
	LatLng (60, 40, lat, lng);
	Vector base = Up (lat, lng) * R0, eb = East (lng), nb = North (lat, lng), ub = Up (lat, lng);
	int east = 0, north = 0, up = 0;
	for (const CollTri &t : G.tri) {
		Vector a = r.Planet (B, G.vtx[t.v[0]]), b = r.Planet (B, G.vtx[t.v[1]]), c = r.Planet (B, G.vtx[t.v[2]]);
		Vector n = Cross (b - a, c - a);
		n = n / n.length ();
		Vector m = (a + b + c) / 3.0 - base;
		if (dotp (n, eb) > 0.99) { east++; CHECK (std::fabs (dotp (m, eb) - 10.0) < 1e-3); }
		if (dotp (n, nb) > 0.99) { north++; CHECK (std::fabs (dotp (m, nb) - 15.0) < 1e-3); }
		if (dotp (n, ub) > 0.99) { up++; CHECK (std::fabs (dotp (m, ub) - 15.0) < 1e-3); }
	}
	CHECK (east == 2);
	CHECK (north == 2);
	CHECK (up == 2);
	int tx, ty;                                // the tile frame itself: x south, y up, z east at the tile centre, no mirror
	TileOf (lat, lng, tx, ty);
	double tlat, tlng;
	TileCentre (tx, ty, tlat, tlng);
	Vector c0 = mul (B.rrotP, Vector (1, 0, 0)), c1 = mul (B.rrotP, Vector (0, 1, 0)), c2 = mul (B.rrotP, Vector (0, 0, 1));
	CHECK ((c1 - Up (tlat, tlng)).length () < 1e-9);
	CHECK ((c0 + North (tlat, tlng)).length () < 1e-9);
	CHECK ((c2 - East (tlng)).length () < 1e-9);
	CHECK (dotp (Cross (c0, c1), c2) > 1.0 - 1e-9);
	CHECK ((B.rposP - Up (tlat, tlng) * (R0 + r.Ground (tlat, tlng))).length () < 1e-6);
}

TEST_CASE ("bldg: a building gained over a parked vessel sets the entry bit once; slots are never reused; a ground move rebuilds only that building", "[CollBldgA]")
{
	Rig r;
	double bump = 0;
	r.s.elevG = [&bump] (double, double lat) { return lat < LAT0 - 1e-7 ? 3.0 + bump : 3.0; }; // the south building's ground moves
	r.Add (0, 40, Box (20, 20), 0.0f, 15.0f);
	r.Add (0, -40, Box (20, 20), 0.0f, 15.0f);
	r.r = 12;
	r.At (0, 40, 3);                           // parked where the north building appears
	r.Start ();
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	CHECK (r.b->Bodies ()[0].entry);
	CHECK (r.b->Bodies ()[0].slot == 0);
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	CHECK_FALSE (r.b->Bodies ()[0].entry);
	uint32_t v[2] = { 0, 0 };
	for (auto &p : r.b->Bodies ()[0].part) v[p.index] = p.version;
	REQUIRE (v[0] != 0);
	REQUIRE (v[1] != 0);
	CHECK (v[0] != v[1]);
	bump = 1.0;
	r.s.simT += COLL_BLDG_GROUND + 0.1;
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	for (auto &p : r.b->Bodies ()[0].part) {
		if (p.index == 0) CHECK (p.version == v[0]);
		else CHECK (p.version != v[1]);
	}
	CHECK (r.b->st.rebuilt == 1);
	r.At (20000, 0, 10);                       // 20 km away: the tile goes
	r.Frame ();
	CHECK (r.b->Tiles () == 0);
	CHECK (r.b->Bodies ().empty ());
	CHECK (r.s.bldgFreeB == 1);
	for (auto &kv : r.s.bldgB) CHECK (kv.second.held == 0);
	r.At (0, 0, 10);
	r.s.sysT += 1.0;
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	CHECK (r.b->Bodies ()[0].slot == 1);       // a new slot: no pair-store state of the old one
	r.b->Clear ();
	CHECK (r.s.bldgFreeB == 2);
	CHECK (r.s.misuse == 0);
}

TEST_CASE ("bldg: activation reach and the vertex budget, nearest first; ids and owner keys apart from bases and the ground", "[CollBldgA]")
{
	SECTION ("reach 300 m + 2 s x ground speed") {
		Rig r;
		r.Add (250, 0, Box (10, 10), 0.0f, 15.0f);
		r.Add (0, 400, Box (10, 10), 0.0f, 15.0f);
		r.At (0, 0, 10);
		r.Start ();
		r.Frame ();
		CHECK (r.Parts () == std::vector<uint32_t> { 0 });
		double lat, lng;
		LatLng (0, 0, lat, lng);
		r.At (0, 0, 10, East (lng) * 100.0);   // reach 500 m
		r.Frame ();
		CHECK (r.Parts () == std::vector<uint32_t> { 0, 1 });
	}
	SECTION ("20k vertices a frame past 100 m, nearest first; all within 100 m at once") {
		Rig r;
		const int n = 30;
		for (int k = 0; k < n; k++) {
			double d = 150.0 + 4.0 * k, a = 0.7 * k;
			r.Add (d * cos (a), d * sin (a), NGon (8, 300), 0.0f, 15.0f); // 1500 .bld vertices each
		}
		r.Add (0, 60, NGon (8, 300), 0.0f, 15.0f);
		r.Add (60, 0, NGon (8, 300), 0.0f, 15.0f);
		r.At (0, 0, 10);
		r.Start ();
		r.Frame ();
		std::vector<uint32_t> p = r.Parts ();
		CHECK (r.b->st.buildVtxMax == 19500);
		REQUIRE (p.size () == 13);              // the two near ones, then 11 x 1500 within 20000 with them
		for (uint32_t k = 0; k < 11; k++) CHECK (p[k] == k); // nearest first
		CHECK (p[11] == 30);
		CHECK (p[12] == 31);
		r.Frame ();
		CHECK (r.Parts ().size () == 26);       // 13 more
		r.Frame ();
		CHECK (r.Parts ().size () == 32);
		CHECK (r.b->BuiltCount () == 32);
	}
	SECTION ("ids") {
		CHECK (CollBldgA::BodyId (3, 0) == ((3u << 16) | 0x8000u));
		CHECK ((CollBldgA::BodyId (3, COLL_BLDG_SLOTS - 1) & 0xFFFFu) == 0xFFFEu);
		CHECK ((CollBldgA::BodyId (3, 0) & 0xFFFFu) >= 0x8000u); // a base id is planet << 16 | base index, below 0x8000
		CHECK (CollBldgA::BaseOf (0) == -2);    // -1 is the ground and the support wildcard
		CHECK (CollBldgA::BaseOf (COLL_BLDG_SLOTS - 1) == -2 - 0x7FFE);
	}
}

TEST_CASE ("bldg: base exclusion, a building 4 m from a base wall is never built, one 10 m away is", "[CollBldgA]")
{
	Rig r;
	r.s.bodies.push_back ({});
	CollFakeSdk::Body &P = r.s.bodies.back ();
	P.name = "Pad"; P.type = 20; P.lng = LNG0; P.lat = LAT0;
	r.E->base.push_back (&P);
	r.s.File (".\\Config\\Earth.cfg", "Name = Earth\n");
	r.s.dirs[CollFakeSdk::Norm (".\\Config\\Earth\\Base\\")] = { "Pad.cfg" };
	r.s.File (".\\Config\\Earth\\Base\\Pad.cfg", "BASE-V2.0\nName = Pad\nBEGIN_OBJECTLIST\nBLOCK\n POS 0 0 0\n SCALE 20 10 20\nEND\nEND_OBJECTLIST\n"); // walls at 10 m south, north, east and west
	CollDirs d;
	r.bases.Build (r.s, d, true, true);
	REQUIRE (r.bases.rec.size () == 1);
	REQUIRE (r.bases.rec[0]->bidx);
	r.Add (17, 0, Box (6, 6), 0.0f, 15.0f);   // west wall 4 m east of the block's east wall
	r.Add (-23, 0, Box (6, 6), 0.0f, 15.0f);  // east wall 10 m west of its west wall
	r.At (0, 60, 10);
	r.Start ();
	for (int k = 0; k < 3; k++) {
		r.Frame ();
		CHECK (r.Parts () == std::vector<uint32_t> { 1 });
	}
	CHECK (r.b->st.excluded == 1);
	CHECK (r.s.misuse == 0);
}

TEST_CASE ("bldg: a fixed structure keeps buildings out as a base does; gone, they are built; back, the built one is freed", "[CollBldgA]")
{
	Rig r;
	auto mesh = std::make_shared<CollRestMesh> (); // a 20 x 20 m block 10 m tall, its walls only, vessel frame x east, y up, z north
	CollGroupData g;
	const double c[4][2] = { { -10, -10 }, { 10, -10 }, { 10, 10 }, { -10, 10 } };
	for (int k = 0; k < 4; k++) {
		const double *a = c[k], *b = c[(k + 1) % 4];
		uint16_t i0 = (uint16_t)g.vtx.size ();
		for (double h : { 0.0, 10.0 }) for (const double *p : { a, b }) g.vtx.push_back (CollVtx { (float)p[0], (float)h, (float)p[1], 0, 0, 0, 0, 0 });
		g.idx.insert (g.idx.end (), { i0, (uint16_t)(i0 + 1), (uint16_t)(i0 + 3), i0, (uint16_t)(i0 + 3), (uint16_t)(i0 + 2) });
	}
	mesh->grp.push_back (g);
	Vector u = Up (LAT0, LNG0), e = East (LNG0), n = North (LAT0, LNG0);
	CollBldgA::FixedIn f { (CollH)0x1234, "Pad", u * (R0 + r.Ground (LAT0, LNG0)), Matrix (e.x, u.x, n.x, e.y, u.y, n.y, e.z, u.z, n.z), 7, { { mesh, Vector () } } };
	r.Add (17, 0, Box (6, 6), 0.0f, 15.0f);   // west wall 4 m east of the block's east wall
	r.Add (-23, 0, Box (6, 6), 0.0f, 15.0f);  // east wall 10 m west of its west wall
	r.At (0, 60, 10);
	r.Start ();
	auto run = [&] (bool on, int frames) { for (int k = 0; k < frames; k++) { r.b->Fixed (on ? std::vector<CollBldgA::FixedIn> { f } : std::vector<CollBldgA::FixedIn> {}); r.Frame (); } };
	run (true, 3);
	CHECK (r.Parts () == std::vector<uint32_t> { 1 });
	CHECK (r.b->st.excluded == 1);
	run (false, 30);                          // a want round later: no structure, both are built
	CHECK (r.Parts () == std::vector<uint32_t> { 0, 1 });
	run (true, 30);                           // back: the built one is freed and excluded
	CHECK (r.Parts () == std::vector<uint32_t> { 1 });
	CHECK (r.b->st.excluded == 2);
	f.x = f.x + e * 200.0;                    // moved 200 m east: the old place's building is built again
	run (true, 30);
	CHECK (r.Parts () == std::vector<uint32_t> { 0, 1 });
	CHECK (r.s.misuse == 0);
}

TEST_CASE ("bldg: a fixed structure's clear zone keeps out a building off its faces; without one it stays (design fixed-clear)", "[CollBldgA]")
{
	for (double clear : { 50.0, 0.0 }) {
		Rig r;
		auto mesh = std::make_shared<CollRestMesh> (); // the 20 x 20 m block 10 m tall, its walls only
		CollGroupData g;
		const double c[4][2] = { { -10, -10 }, { 10, -10 }, { 10, 10 }, { -10, 10 } };
		for (int k = 0; k < 4; k++) {
			const double *a = c[k], *b = c[(k + 1) % 4];
			uint16_t i0 = (uint16_t)g.vtx.size ();
			for (double h : { 0.0, 10.0 }) for (const double *p : { a, b }) g.vtx.push_back (CollVtx { (float)p[0], (float)h, (float)p[1], 0, 0, 0, 0, 0 });
			g.idx.insert (g.idx.end (), { i0, (uint16_t)(i0 + 1), (uint16_t)(i0 + 3), i0, (uint16_t)(i0 + 3), (uint16_t)(i0 + 2) });
		}
		mesh->grp.push_back (g);
		Vector u = Up (LAT0, LNG0), e = East (LNG0), n = North (LAT0, LNG0);
		CollBldgA::FixedIn f { (CollH)0x1234, "Pad", u * (R0 + r.Ground (LAT0, LNG0)), Matrix (e.x, u.x, n.x, e.y, u.y, n.y, e.z, u.z, n.z), 7, { { mesh, Vector () } }, clear };
		r.Add (37, 0, Box (6, 6), 0.0f, 15.0f);   // west wall 24 m from the block, 34 m from its origin
		r.Add (75, 0, Box (6, 6), 0.0f, 15.0f);   // west wall 72 m from its origin
		r.At (55, 60, 10);
		r.Start ();
		for (int k = 0; k < 3; k++) { r.b->Fixed ({ f }); r.Frame (); }
		if (clear > 0) {
			CHECK (r.Parts () == std::vector<uint32_t> { 1 });
			CHECK (r.b->st.excluded == 1);
		} else {
			CHECK (r.Parts () == std::vector<uint32_t> { 0, 1 });
			CHECK (r.b->st.excluded == 0);
		}
		CHECK (r.s.misuse == 0);
	}
}

TEST_CASE ("bldg: a base file's clear zone keeps out a building reaching into it, wherever the base is; without one it stays (design base-clear)", "[CollBldgA]")
{
	for (double rad : { 50.0, 0.0 }) {
		Rig r;
		r.s.bodies.push_back ({});
		CollFakeSdk::Body &P = r.s.bodies.back ();
		P.name = "Site"; P.type = 20; P.lng = LNG0 + 0.2; P.lat = LAT0; // the base's own point far away
		r.E->base.push_back (&P);
		if (rad > 0) r.s.zonesB[(CollH)&P] = { { LNG0, LAT0, rad } };
		r.Add (37, 0, Box (6, 6), 0.0f, 15.0f);   // west wall 34 m from the zone's centre
		r.Add (75, 0, Box (6, 6), 0.0f, 15.0f);   // west wall 72 m from it
		r.At (55, 60, 10);
		r.Start ();
		for (int k = 0; k < 3; k++) r.Frame ();
		if (rad > 0) {
			CHECK (r.Parts () == std::vector<uint32_t> { 1 });
			CHECK (r.b->st.excluded == 1);
		} else {
			CHECK (r.Parts () == std::vector<uint32_t> { 0, 1 });
			CHECK (r.b->st.excluded == 0);
		}
		CHECK (r.s.misuse == 0);
	}
}

TEST_CASE ("bldg: the switch off, or collisions off, make no SDK call for buildings", "[CollBldgA]")
{
	for (int mode = 0; mode < 2; mode++) {
		Rig r;
		if (mode == 0) r.cfg.streamBuildings = false;
		else r.cfg.model = 0;
		r.b = std::make_unique<CollBldgA> (r.s, r.cfg);
		r.Add (60, 40, Box (20, 30), 0.0f, 15.0f);
		r.At (0, 0, 10);
		r.bases.Build (r.s, CollDirs (), r.cfg.model != 0, r.cfg.model != 0 && r.cfg.streamBuildings);
		r.Start ();
		CHECK_FALSE (r.b->On ());
		for (int k = 0; k < 40; k++) r.Frame ();
		CHECK (r.b->Bodies ().empty ());
		CHECK (r.s.bldgWantB == 0);
		CHECK (r.s.bldgLoadB == 0);
		CHECK (r.s.bldgStreamB == 0);
		CHECK (r.s.bldgFreeB == 0);
		r.b->Clear ();
		CHECK (r.s.bldgFreeB == 0);
	}
	Rig r;                                     // and a planet without a building stream: no want or load either
	r.s.bldgBodyB.clear ();
	r.Add (60, 40, Box (20, 30), 0.0f, 15.0f);
	r.At (0, 0, 10);
	r.Start ();
	CHECK_FALSE (r.b->On ());
	r.Frame ();
	CHECK (r.s.bldgWantB == 0);
	CHECK (r.s.bldgLoadB == 0);
}

TEST_CASE ("bldg: a ground move under a vessel on the roof waits; the building moves once the vessel is off it", "[CollBldgA]")
{
	Rig r;
	double bump = 0;
	r.s.elevG = [&bump] (double, double) { return 3.0 + bump; };
	r.Add (0, 0, Box (20, 20), 0.0f, 15.0f);
	r.r = 6;
	r.At (0, 0, 18);                           // resting on the roof
	r.Start ();
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	REQUIRE (r.b->Bodies ()[0].part.size () == 1);
	uint32_t v0 = r.b->Bodies ()[0].part[0].version;
	bump = 1.0;
	r.s.simT += COLL_BLDG_GROUND + 0.1;
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	CHECK (r.b->Bodies ()[0].part[0].version == v0);
	CHECK (r.b->st.deferred == 1);
	CHECK (r.b->st.rebuilt == 0);
	r.At (0, 100, 10);                         // off it, still within reach
	r.s.simT += COLL_BLDG_GROUND + 0.1;
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	REQUIRE (r.b->Bodies ()[0].part.size () == 1);
	CHECK (r.b->Bodies ()[0].part[0].version != v0);
	CHECK_FALSE (r.b->Bodies ()[0].entry);
	CHECK (r.b->st.rebuilt == 1);
	CHECK (r.b->st.deferred == 1);
}

TEST_CASE ("bldg: the tile body handed to the detector, a building gained on a body that exists sets the entry bit; only buildings near a vessel are parts", "[CollBldgA]")
{
	Rig r;
	r.Add (0, 0, Box (20, 20), 0.0f, 15.0f);
	r.Add (0, 250, Box (20, 20), 0.0f, 15.0f);
	r.r = 12;
	r.At (0, -200, 3);
	r.Start ();
	r.Frame ();
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	CHECK (r.Parts () == std::vector<uint32_t> { 0 });
	CHECK_FALSE (r.b->Bodies ()[0].entry);
	uint32_t slot = r.b->Bodies ()[0].slot;
	r.At (0, 250, 3);                          // parked inside the north building, the body already there
	r.Frame ();
	REQUIRE (r.b->Bodies ().size () == 1);
	CHECK (r.b->Bodies ()[0].slot == slot);
	CHECK (r.Parts () == std::vector<uint32_t> { 0, 1 });
	CHECK (r.b->Bodies ()[0].entry);
	CollABody E;
	CollBldgA::Emit (r.b->Bodies ()[0], E);
	CHECK ((E.entry & COLLE_MESH) != 0);
	r.Frame ();
	CHECK_FALSE (r.b->Bodies ()[0].entry);
	r.At (0, 450, 3);                          // the south building beyond reach, not yet freed: built, no part
	r.Frame ();
	CHECK (r.b->BuiltCount () == 2);
	REQUIRE (r.b->Bodies ().size () == 1);
	const CollBldgA::Body &T = r.b->Bodies ()[0];
	REQUIRE (T.part.size () == 1);
	CHECK (T.part[0].index == 1);
	const CollGeom &G = *T.part[0].geom;
	CHECK (std::fabs (T.rmax - (G.bsCentre.length () + G.bsRadius)) < 1e-9);
	CollABody B;
	CollBldgA::Emit (T, B);
	CHECK (B.kind == COLLB_BASE);
	CHECK (B.id == CollBldgA::BodyId (0, slot));
	CHECK (B.memberHash == B.id);
	CHECK (B.m == 0);
	CHECK (B.planet == 0);
	CHECK (B.rmax == T.rmax + COLL_SKIN_MAX);
	CHECK ((B.entry & COLLE_MESH) == 0);
	REQUIRE (B.parts.size () == 1);
	const CollPartRef &p = B.parts[0];
	CHECK (p.geom == &G);
	CHECK (p.skin == G.skin);
	CHECK (p.version == T.part[0].version);
	CHECK (p.mesh == 1);
	CHECK (p.owner.kind == COLLO_BUILDING);
	CHECK (p.owner.id == 0);
	CHECK (p.owner.planet == 0);
	CHECK (p.owner.base == -2 - (int32_t)slot);
	CHECK (p.owner.obj == 1);
	CHECK (p.owner.part == 0);
}

TEST_CASE ("bldg: a building far from every vessel is freed after 10 s, also after a time jump back", "[CollBldgA]")
{
	Rig r;
	r.Add (0, 0, Box (20, 20), 0.0f, 15.0f);
	r.At (0, 0, 10);
	r.Start ();
	r.Frame ();
	CHECK (r.b->BuiltCount () == 1);
	r.At (0, -700, 10);                        // beyond twice the reach, the tile kept
	r.s.simT = 100;
	for (int k = 0; k < 10; k++) r.Frame (0.5);
	CHECK (r.b->BuiltCount () == 1);
	r.s.simT = 0;                              // back in time: without the reset the timer would wait for t = 110
	r.b->TimeJump ();
	for (int k = 0; k < 22; k++) r.Frame (0.5);
	CHECK (r.b->BuiltCount () == 0);
	CHECK (r.b->Tiles () == 1);
	CHECK (r.b->st.freed == 1);
}

TEST_CASE ("bldg: a tile the stream does not have (-1) is asked again", "[CollBldgA]")
{
	Rig r;
	r.Add (0, 0, Box (20, 20), 0.0f, 15.0f);
	r.At (0, 0, 10);
	r.Start ();
	REQUIRE (r.b->On ());
	r.s.bldgBodyB.clear ();                    // the fake answers -1: offline and not on disk
	r.Frame ();
	CHECK (r.b->Tiles () == 0);
	int asked = r.s.bldgWantB;
	CHECK (asked > 0);
	r.s.bldgBodyB.push_back (r.E);             // on disk now
	r.s.sysT += 1.0;
	r.Frame ();
	CHECK (r.s.bldgWantB > asked);
	CHECK (r.b->Tiles () == 1);
	CHECK (r.Parts () == std::vector<uint32_t> { 0 });
}

TEST_CASE ("bldg: the collider groups split past 65535 vertices and keep every triangle", "[CollBldgA]")
{
	const uint32_t n = 70000;                  // a strip: triangle k is k, k + 1, k + 2
	std::vector<CollVtx> v (n);
	for (uint32_t k = 0; k < n; k++) v[k] = CollVtx { (float)k, 0, 0, 0, 1, 0, 0, 0 };
	std::vector<std::array<uint32_t, 3>> tri;
	for (uint32_t k = 0; k + 2 < n; k++) tri.push_back ({ k, k + 1, k + 2 });
	std::vector<CollGroupData> grp;
	CollBldgA::Split (v, tri, grp);
	REQUIRE (grp.size () == 2);
	size_t t = 0;
	bool same = true;
	for (const CollGroupData &G : grp) {
		CHECK (G.vtx.size () <= 65535);
		CHECK (G.idx.size () % 3 == 0);
		for (size_t k = 0; k < G.idx.size (); k += 3, t++)
			for (int j = 0; j < 3; j++) same = same && G.idx[k + j] < G.vtx.size () && G.vtx[G.idx[k + j]].x == (float)tri[t][j];
	}
	CHECK (same);
	CHECK (t == tri.size ());
	CHECK (grp[0].vtx.size () == 65533);       // each triangle adds one vertex; the next one would pass 65535
	CHECK (grp[1].vtx.size () == n - 65531);   // the shared vertices again in the second group
}

TEST_CASE ("bldg: at most COLL_BLDG_ELEVS elevation calls a frame past 100 m; a vessel's ground is asked again after 25 m or 1 s", "[CollBldgA]")
{
	Rig r;
	const size_t n = 80;
	for (size_t k = 0; k < n; k++) {
		double d = 150.0 + k, a = 0.37 * k;
		r.Add (d * cos (a), d * sin (a), Box (4, 4), 0.0f, 10.0f);
	}
	r.At (0, 0, 10);
	r.Start ();
	int c0 = r.s.elevCalls;
	r.Frame ();
	CHECK (r.s.elevCalls - c0 == (int)COLL_BLDG_ELEVS); // the vessel, the tile centre, then buildings up to the budget
	CHECK (r.b->BuiltCount () == COLL_BLDG_ELEVS - 2);
	c0 = r.s.elevCalls;
	r.Frame ();
	CHECK (r.s.elevCalls - c0 == (int)COLL_BLDG_ELEVS); // the vessel's ground from the cache
	CHECK (r.b->BuiltCount () == 2 * COLL_BLDG_ELEVS - 2);
	r.Frame ();
	CHECK (r.b->BuiltCount () == n);
	CHECK (r.b->st.elevCallsMax == COLL_BLDG_ELEVS);
	r.At (0, 30, 10);
	c0 = r.s.elevCalls;
	r.Frame ();
	CHECK (r.s.elevCalls - c0 == 1);           // 30 m on
	c0 = r.s.elevCalls;
	r.Frame (1.0);
	CHECK (r.s.elevCalls == c0);
	r.Frame ();
	CHECK (r.s.elevCalls - c0 == 1);           // a second later
}
