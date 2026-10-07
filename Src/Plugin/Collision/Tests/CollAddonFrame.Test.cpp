// not upstream: unit tests of the E1 frame driver on the propagator mirror as "Orbiter" (A1, A6, A21 and the momentum check of 5.5)
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "CollAddonFrame.h"

namespace {

std::vector<std::string> g_log;
void LogSink (int level, const char *msg) { if (level >= COLLLOG_INFO) g_log.push_back (msg); }

CollGroupData BoxMesh (const Vector &h)
{
	CollGroupData g;
	for (int i = 0; i < 8; i++) g.vtx.push_back (CollVtx { (float)((i & 1) ? h.x : -h.x), (float)((i & 2) ? h.y : -h.y), (float)((i & 4) ? h.z : -h.z), 0, 0, 0, 0, 0 });
	const uint16_t f[36] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4, 2,6,3, 3,6,7, 0,4,2, 2,4,6, 1,3,5, 3,7,5 };
	g.idx.assign (f, f + 36);
	return g;
}
CollGroupData SphereMesh (double r, int nlat, int nlon)
{
	CollGroupData g;
	g.vtx.push_back (CollVtx { (float)r, 0, 0, 0, 0, 0, 0, 0 });
	for (int i = 1; i < nlat; i++) {
		double th = 3.14159265358979323846*i/nlat;
		for (int j = 0; j < nlon; j++) {
			double ph = 2.0*3.14159265358979323846*j/nlon;
			g.vtx.push_back (CollVtx { (float)(r*std::cos (th)), (float)(r*std::sin (th)*std::cos (ph)), (float)(r*std::sin (th)*std::sin (ph)), 0, 0, 0, 0, 0 });
		}
	}
	g.vtx.push_back (CollVtx { (float)-r, 0, 0, 0, 0, 0, 0, 0 });
	uint16_t last = (uint16_t)(g.vtx.size () - 1);
	auto ring = [&] (int i, int j) { return (uint16_t)(1 + (i - 1)*nlon + (j % nlon)); };
	for (int j = 0; j < nlon; j++) { g.idx.push_back (0); g.idx.push_back (ring (1, j)); g.idx.push_back (ring (1, j + 1)); }
	for (int i = 1; i < nlat - 1; i++)
		for (int j = 0; j < nlon; j++) {
			uint16_t a = ring (i, j), b = ring (i, j + 1), c = ring (i + 1, j), d = ring (i + 1, j + 1);
			g.idx.push_back (a); g.idx.push_back (c); g.idx.push_back (b);
			g.idx.push_back (b); g.idx.push_back (c); g.idx.push_back (d);
		}
	for (int j = 0; j < nlon; j++) { g.idx.push_back (last); g.idx.push_back (ring (nlat - 1, j + 1)); g.idx.push_back (ring (nlat - 1, j)); }
	return g;
}
struct Geo {
	CollGroupData g; CollGeom geom;
	explicit Geo (const CollGroupData &d) : g (d) { CollSrcGroup s { &g, CollSrc { 0, 0, 0, 0, 0 } }; REQUIRE (geom.Build (&s, 1, COLL_WELD_DEFAULT, nullptr)); }
};
struct Host : CollSolveHost {
	const CollDetect *det = nullptr;
	CollSMat Material (const CollPairResult &, int, int) override { return CollSMat (); }
	void Feature (const CollPairResult &r, int i, int side, CollImpactSide &s) override { s.owner = CollOwnerRefOf (det->Owner (r, i, side)); }
};

// "Orbiter": mirror bodies stepped by CollOrbMirror; the addon's writes applied as the SDK would
struct Sim {
	CollOrbMirror mir;
	CollAddonFrame fr;
	CollDetect fwd, ver;
	Host host;
	Vector g;
	struct TB { CollOrbState o; std::shared_ptr<Geo> geo; uint32_t id; double rmax; };
	std::vector<TB> tb;
	std::shared_ptr<Geo> baseGeo; Vector basePos;
	int events = 0, writes = 0, lastWrites = 0;
	std::vector<CollImpactEvent> evs;
	double t = 0;
	Sim () { host.det = &fwd; fr.hRest = mir.HRest (); }
	void Add (std::shared_ptr<Geo> geo, double m, const Vector &pmi, const Vector &x, const Vector &v, double rmax)
	{
		TB b;
		b.o.s.R = IMatrix (); b.o.s.Q.Set (b.o.s.R);
		b.o.s.pos = x; b.o.s.vel = v; b.o.m = m; b.o.pmi = pmi;
		b.o.aC = g; b.o.acc = g; b.o.gReset = g;
		b.geo = geo; b.id = (uint32_t)tb.size () + 1; b.rmax = rmax;
		tb.push_back (b);
	}
	Vector P () const { Vector p; for (const TB &b : tb) p += b.o.s.vel*b.o.m; return p; }
	double Pscale () const { double s = 0; for (const TB &b : tb) s += b.o.s.vel.length ()*b.o.m; return s; }
	void Frame (double h)
	{
		std::vector<CollABody> bodies;
		if (baseGeo) {
			CollABody k;
			k.id = 1; k.kind = COLLB_BASE; k.m = 0; k.rmax = 0;
			k.kin = CollMotion {};
			k.kin.c0 = k.kin.c1 = basePos; k.kin.h = h; k.kin.tb = 1; k.kin.a0ok = true;
			CollPartRef p {};
			p.geom = &baseGeo->geom; p.skin = COLL_SKIN_DEFAULT; p.owner = CollOwnerKey { COLLO_BUILDING, 0, 0, 0, 0, 0 }; p.partKey = 0; p.version = 0;
			k.parts.push_back (p);
			bodies.push_back (k);
		}
		for (const TB &b : tb) {
			CollABody k;
			k.id = b.id; k.kind = COLLB_DYNAMIC; k.member = { b.id }; k.memberHash = b.id;
			k.m = b.o.m; k.pmi = b.o.pmi;
			k.x = b.o.s.pos; k.v = b.o.s.vel; k.wb = b.o.s.omega; k.q = b.o.s.Q;
			k.aTot = b.o.acc; k.arot = b.o.arot; k.gEst = g; k.rmax = b.rmax;
			CollPartRef p {};
			p.geom = &b.geo->geom; p.skin = COLL_SKIN_DEFAULT; p.owner = CollOwnerKey { COLLO_VESSEL, b.id, -1, -1, -1, -1 }; p.partKey = 0; p.version = 0;
			k.parts.push_back (p);
			bodies.push_back (k);
		}
		std::vector<CollAWrite> out;
		std::vector<CollImpactEvent> ev;
		fr.Run (fwd, ver, mir, bodies, {}, h, t, host, out, ev);
		int off = baseGeo ? 1 : 0;
		std::vector<Vector> gx (bodies.size (), g);
		lastWrites = 0;
		for (const CollAWrite &w : out) {
			CollOrbState &o = tb[w.body - off].o;
			if (w.state) { o.DefSetStateEx (w.x, w.v, w.wb); lastWrites++; }
			if (w.attitude) { Matrix R; R.Set (w.q); o.SetRotationMatrix (R); }
		}
		fr.Finish (mir, bodies, out, gx);
		for (const CollAWrite &w : out) {
			CollOrbState &o = tb[w.body - off].o;
			if (w.spin) o.SetAngularVel (w.wb);
			if (w.force) o.AddForce (w.Fb, Vector ());
			if (w.Mb.length () > 0) {
				double ml = w.Mb.length ();
				Vector a = std::fabs (w.Mb.x) < 0.6*ml ? Vector (1, 0, 0) : Vector (0, 1, 0);
				Vector r1 = Xc (w.Mb, a); r1 /= r1.length ();
				Vector u = Xc (w.Mb, r1)*0.5;
				o.AddForce (u, r1); o.AddForce (-u, -r1);
			}
		}
		writes += lastWrites;
		events += (int)ev.size ();
		for (const CollImpactEvent &e : ev) evs.push_back (e);
		for (TB &b : tb) mir.Step (b.o, h);
		t += h;
	}
};

double MinGap (Sim &S)
{
	double m = 1e9;
	CollScratch s;
	for (size_t i = 0; i < S.tb.size (); i++) {
		CollAffine A { S.tb[i].o.s.R, Vector () };
		for (size_t j = i + 1; j < S.tb.size (); j++) {
			CollAffine B { S.tb[j].o.s.R, S.tb[j].o.s.pos - S.tb[i].o.s.pos };
			m = std::min (m, CollDistance (S.tb[i].geo->geom, A, S.tb[j].geo->geom, B, 10.0, nullptr, s) - 2*COLL_SKIN_DEFAULT);
		}
		if (S.baseGeo) {
			CollAffine B { IMatrix (), S.basePos - S.tb[i].o.s.pos };
			m = std::min (m, CollDistance (S.tb[i].geo->geom, A, S.baseGeo->geom, B, 10.0, nullptr, s) - 2*COLL_SKIN_DEFAULT);
		}
	}
	return m;
}

} // namespace

TEST_CASE ("A1 head-on spheres: one event, separation, momentum exact every frame, no tunnelling", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double h : { 1.0/60.0, 1.0/6.0 })
		for (double u : { 1.0, 10.0, 100.0 }) {
			g_log.clear ();
			Sim S;
			auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
			S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-1.05 - u*0.25, 0, 0), Vector (u*0.5, 0, 0), 1.05);
			S.Add (sph, 3000, Vector (0.4, 0.4, 0.4), Vector (1.05 + u*0.25, 0, 0), Vector (-u*0.5, 0, 0), 1.05);
			Vector P0 = S.P ();
			double sc = S.Pscale (), gmin = 1e9;
			for (int f = 0; f < 120; f++) {
				S.Frame (h);
				CAPTURE (h, u, f);
				REQUIRE ((S.P () - P0).length () <= 1e-12*sc);
				gmin = std::min (gmin, MinGap (S));
			}
			CAPTURE (h, u, gmin, S.events);
			REQUIRE (S.events == 1);
			REQUIRE (S.tb[0].o.s.vel.x < S.tb[1].o.s.vel.x);   // separating
			REQUIRE (gmin >= -0.05);
			REQUIRE (S.evs[0].dKE >= 0.0);
			REQUIRE (S.fr.Stats ().checkFail == 0);
		}
	g_collLog = nullptr;
}

TEST_CASE ("A6 box resting on a roof under gravity: settles, then no state writes, support = weight", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double h : { 1.0/60.0, 0.1 }) {
		Sim S;
		S.g = Vector (0, -9.81, 0);
		S.baseGeo = std::make_shared<Geo> (BoxMesh (Vector (20, 1, 20)));
		S.basePos = Vector (0, -1, 0);
		auto box = std::make_shared<Geo> (BoxMesh (Vector (1, 0.5, 1)));
		S.Add (box, 1000, Vector ((0.25 + 1)/3, 2.0/3, (1 + 0.25)/3), Vector (0, 0.5 + 0.04 + 0.1, 0), Vector (), 1.6);
		int late = 0;
		double ymin = 1e9, ymax = -1e9;
		for (int f = 0; f*h < 6.0; f++) {
			S.Frame (h);
			if (f*h > 3.0) { late += S.lastWrites; ymin = std::min (ymin, S.tb[0].o.s.pos.y); ymax = std::max (ymax, S.tb[0].o.s.pos.y); }
		}
		CAPTURE (h, S.events, S.writes, late, ymin, ymax, S.tb[0].o.s.vel.y);
		REQUIRE (S.events == 1);
		REQUIRE (late == 0);
		REQUIRE (ymax - ymin <= 1e-6);
		REQUIRE (std::fabs (S.tb[0].o.s.vel.y) <= 1e-6);
		REQUIRE (MinGap (S) >= -2*COLL_SLOP);
		REQUIRE (S.fr.Stats ().checkFail == 0);
	}
	g_collLog = nullptr;
}

TEST_CASE ("A21 simdt == 0 frame: no write, records kept; time jump drops records", "[CollAddonFrame]")
{
	Sim S;
	auto sph = std::make_shared<Geo> (SphereMesh (1.0, 8, 16));
	S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-1.6, 0, 0), Vector (5, 0, 0), 1.05);
	S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (1.6, 0, 0), Vector (-5, 0, 0), 1.05);
	S.Frame (0.1);
	std::vector<CollABody> none;
	std::vector<CollAWrite> out;
	std::vector<CollImpactEvent> ev;
	S.fr.Run (S.fwd, S.ver, S.mir, none, {}, 0.0, S.t, S.host, out, ev);
	REQUIRE (out.empty ());
	REQUIRE (ev.empty ());
	S.fr.OnTimeJump ();
	S.Frame (0.1);
	REQUIRE (S.fr.Stats ().checkFail == 0);
}
