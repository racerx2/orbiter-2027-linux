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
	const CollAddonFrame *fr = nullptr;
	const CollDetect *ver = nullptr;
	int calls = 0, none = 0, verCalls = 0;
	CollSMat Material (const CollPairResult &, int, int) override { return CollSMat (); }
	void Feature (const CollPairResult &r, int i, int side, CollImpactSide &s) override // only from the detector the driver names (review CA-F 4)
	{
		calls++;
		const CollDetect *d = fr->FeatDet ();
		if (!d) { none++; return; }
		if (d == ver) verCalls++;
		s.owner = CollOwnerRefOf (d->Owner (r, i, side));
	}
};

// "Orbiter": mirror bodies stepped by CollOrbMirror; the addon's writes applied as the SDK would
struct Sim {
	CollOrbMirror mir;
	CollAddonFrame fr;
	CollDetect fwd, ver;
	Host host;
	Vector g;
	struct TB { CollOrbState o; std::shared_ptr<Geo> geo; uint32_t id; double rmax; Vector push; bool landed = false, woke = false; };
	std::vector<TB> tb;
	std::shared_ptr<Geo> baseGeo; Vector basePos;
	int events = 0, writes = 0, lastWrites = 0;
	std::vector<CollImpactEvent> evs;
	double t = 0;
	bool jumpNext = false;                               // the next frame follows a time jump: JUMP entry, Orbiter's cache reset to gravity
	Sim () { host.fr = &fr; host.ver = &ver; fr.hRest = mir.HRest (); }
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
			k.id = b.id; k.kind = b.landed ? COLLB_LANDED : COLLB_DYNAMIC; k.member = { b.id }; k.memberHash = b.id;
			k.m = b.o.m; k.pmi = b.o.pmi;
			k.x = b.o.s.pos; k.v = b.o.s.vel; k.wb = b.o.s.omega; k.q = b.o.s.Q;
			k.aTot = b.o.acc; k.arot = b.o.arot; k.gEst = g; k.rmax = b.rmax;
			if (jumpNext) k.entry |= COLLE_JUMP;
			if (b.woke) k.entry |= COLLE_ACTIVATED;
			if (b.landed) {                                  // kinematic at rest, wakeable (6.6)
				k.kin = CollMotion {};
				k.kin.c0 = k.kin.c1 = k.x; k.kin.q0 = k.kin.q1 = k.q; k.kin.h = h; k.kin.tb = 1; k.kin.a0ok = true;
				k.wakeable = true; k.wakeV = k.v; k.wakeWb = k.wb;
			}
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
		jumpNext = false;
		for (TB &b : tb) b.woke = false;
		for (const CollAWrite &w : out) {
			CollOrbState &o = tb[w.body - off].o;
			if (w.state && tb[w.body - off].landed) { tb[w.body - off].landed = false; tb[w.body - off].woke = true; } // DefSetStateEx frees it (Vessel.cpp:864)
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
		for (TB &b : tb) if (!b.landed) { b.o.aC = g + b.push; mir.Step (b.o, h); b.o.aC = g; }
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
			REQUIRE (std::fabs (S.evs[0].vn - u) <= 0.02*u);   // approach at the touch, not the held one
			REQUIRE (S.fr.Stats ().checkFail == 0);
			REQUIRE (S.host.calls > 0);
			REQUIRE (S.host.none == 0);
			for (const CollImpactEvent &e : S.evs) REQUIRE (((e.s[0].owner.vesselId == 1 && e.s[1].owner.vesselId == 2) || (e.s[0].owner.vesselId == 2 && e.s[1].owner.vesselId == 1)));
			if (u*h > 0.5) REQUIRE (S.fr.Stats ().spec > 0);
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

TEST_CASE ("A17 unseen push in the speculative step: FREE path or touch, never a pass-through, one event", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double push : { 0.0, 40.0, 120.0 }) {
		Sim S;
		auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-2.0, 0, 0), Vector (5, 0, 0), 1.05);
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (2.0, 0, 0), Vector (-5, 0, 0), 1.05);
		double gmin = 1e9;
		for (int f = 0; f < 30; f++) {
			S.tb[0].push = (f == 1) ? Vector (0, push, 0) : Vector ();   // a module force after the plugin in the speculative step
			S.Frame (1.0/6.0);
			gmin = std::min (gmin, MinGap (S));
		}
		CAPTURE (push, gmin, S.events, S.fr.Stats ().freePath, S.fr.Stats ().touchPath, S.fr.Stats ().past, S.host.verCalls);
		REQUIRE (S.events == 1);
		REQUIRE (S.host.none == 0);
		for (const CollImpactEvent &e : S.evs) REQUIRE (e.s[0].owner.vesselId + e.s[1].owner.vesselId == 3);
		REQUIRE (gmin >= -0.25);
		REQUIRE (S.tb[0].o.s.vel.x < S.tb[1].o.s.vel.x);
		REQUIRE (S.fr.Stats ().checkFail == 0);
	}
	g_collLog = nullptr;
}

TEST_CASE ("A21b time jump on a resting box: the support of the last frame is not solved twice (review CA-F 8)", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double h : { 1.0/60.0, 0.1 }) {
		Sim S;
		S.g = Vector (0, -9.81, 0);
		S.baseGeo = std::make_shared<Geo> (BoxMesh (Vector (20, 1, 20)));
		S.basePos = Vector (0, -1, 0);
		auto box = std::make_shared<Geo> (BoxMesh (Vector (1, 0.5, 1)));
		S.Add (box, 1000, Vector ((0.25 + 1)/3, 2.0/3, (1 + 0.25)/3), Vector (0, 0.5 + 0.04 + 0.1, 0), Vector (), 1.6);
		for (int f = 0; f*h < 3.0; f++) S.Frame (h);
		double y0 = S.tb[0].o.s.pos.y, vmax = 0, ymax = -1e9;
		S.fr.OnTimeJump ();
		S.jumpNext = true;
		S.tb[0].o.acc = S.g;                             // Vessel::Timejump -> RPlace: acc = Gacc (Vessel.cpp:862-871)
		for (int f = 0; f*h < 1.0; f++) {
			S.Frame (h);
			vmax = std::max (vmax, std::fabs (S.tb[0].o.s.vel.y));
			ymax = std::max (ymax, S.tb[0].o.s.pos.y);
		}
		CAPTURE (h, vmax, ymax - y0, S.events);
		REQUIRE (vmax <= 0.02);
		REQUIRE (ymax - y0 <= 0.005);
		REQUIRE (S.fr.Stats ().checkFail == 0);
	}
	g_collLog = nullptr;
}

TEST_CASE ("LANDED partner: a hard hit wakes it with P exact, a soft one leaves it LANDED (6.6, review CA-F 5)", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double h : { 1.0/60.0, 0.1 })
		for (double u : { 0.02, 2.0 }) {
			Sim S;
			auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
			S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-2.1 - 2.0*u*h, 0, 0), Vector (u, 0, 0), 1.05);
			S.Add (sph, 2000, Vector (0.4, 0.4, 0.4), Vector (0, 0, 0), Vector (), 1.05);
			S.tb[1].landed = true;
			const Vector P0 = S.P ();
			bool woke = false;
			for (int f = 0; f*h < 3.0; f++) {
				S.Frame (h);
				if (!S.tb[1].landed && !woke) { woke = true; CAPTURE (f); REQUIRE ((S.P () - P0).length () <= 1e-9*S.Pscale ()); }
			}
			int flagged = 0;
			for (const CollImpactEvent &e : S.evs) if (e.flags & COLLEV_WOKE_LANDED) flagged++;
			CAPTURE (h, u, S.events, flagged, S.tb[0].o.s.vel.x, S.tb[1].o.s.vel.x);
			REQUIRE (S.events >= 1);
			REQUIRE (S.fr.Stats ().checkFail == 0);
			if (u < 0.05) {
				REQUIRE (S.tb[1].landed);
				REQUIRE (flagged == 0);
			} else {
				REQUIRE (!S.tb[1].landed);
				REQUIRE (flagged == 1);
				REQUIRE (S.tb[1].o.s.vel.x > 0.5);
				REQUIRE (S.tb[0].o.s.vel.x < S.tb[1].o.s.vel.x);
				REQUIRE ((S.P () - P0).length () <= 1e-9*S.Pscale ());
			}
		}
	g_collLog = nullptr;
}

TEST_CASE ("Quaternion from a matrix near a half turn with rounding noise: unit and the same rotation", "[CollAddonFrame]")
{
	for (double qs : { 0.0, 2.14e-6, 1e-4 }) {
		double n = std::sqrt (qs*qs + 1.0 + 0.0035*0.0035);
		Quaternion q (0.0, -1.0/n, 0.0035/n, qs/n);
		Matrix R; R.Set (q);
		R.m13 += 1e-11; R.m31 -= 1e-11;                 // a near half turn as Orbiter's integrated R holds it (smoke run, PB-B)
		Quaternion p; p.Set (R);
		Matrix P; P.Set (p);
		double d = 0;
		for (int k = 0; k < 9; k++) d = std::max (d, std::fabs (P.data[k] - R.data[k]));
		CAPTURE (qs, p.qs, p.qvx, p.qvy, p.qvz, d);
		REQUIRE (std::fabs (p.norm2 () - 1.0) <= 1e-14);
		REQUIRE (d <= 1e-9);
	}
}

namespace {
int LogCount (const char *s) { int n = 0; for (const std::string &l : g_log) if (l.find (s) != std::string::npos) n++; return n; }
}

TEST_CASE ("fix1 M2: two spheres 0.13 m apart closing at 30 m/s: no false overshoot, no rollback, no missed", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double x0 : { -2.13, -2.3 }) {
		g_log.clear ();
		Sim S;
		auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (x0, 0, 0), Vector (30, 0, 0), 1.05);
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (), Vector (), 1.05);
		const Vector P0 = S.P ();
		S.Frame (0.02);
		double g1 = S.tb[1].o.s.pos.x - S.tb[0].o.s.pos.x - 2.0;   // pole to pole: the spheres' x poles face each other
		for (int f = 0; f < 10; f++) S.Frame (0.02);
		CAPTURE (x0, g1, S.events, S.fr.Stats ().freePath, S.fr.Stats ().past, S.fr.Stats ().missed, LogCount ("rollback"));
		REQUIRE (g1 <= 2*COLL_SKIN_DEFAULT + 0.01);              // the whole speculative gap closed in the first step
		REQUIRE (g1 >= 2*COLL_SKIN_DEFAULT - 0.01);
		REQUIRE (S.fr.Stats ().freePath == 0);
		REQUIRE (S.fr.Stats ().past == 0);
		REQUIRE (S.fr.Stats ().missed == 0);
		REQUIRE (LogCount ("rollback") + LogCount ("missed") == 0);
		REQUIRE (S.events == 1);
		REQUIRE (std::fabs (S.evs[0].vn - 30.0) <= 0.6);
		REQUIRE (S.tb[0].o.s.vel.x < S.tb[1].o.s.vel.x);
		REQUIRE ((S.P () - P0).length () <= 1e-12*S.Pscale ());
		REQUIRE (S.fr.Stats ().checkFail == 0);
	}
	g_collLog = nullptr;
}

TEST_CASE ("fix1 M3: three spheres in a row at 30 m/s, h 0.1: the outside body joins the island, no penetration", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double h : { 0.1, 1.0/30.0 }) {
		g_log.clear ();
		Sim S;
		auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-3.5, 0, 0), Vector (30, 0, 0), 1.05);
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (), Vector (), 1.05);
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (2.6, 0, 0), Vector (), 1.05);
		const Vector P0 = S.P ();
		double pen = 0;
		for (int f = 0; f*h < 0.6; f++) {
			S.Frame (h);
			for (int k = 0; k < 2; k++) pen = std::min (pen, S.tb[k + 1].o.s.pos.x - S.tb[k].o.s.pos.x - 2.0);
			REQUIRE ((S.P () - P0).length () <= 1e-12*S.Pscale ());
		}
		CAPTURE (h, pen, S.events, S.fr.Stats ().past, S.fr.Stats ().missed, S.tb[0].o.s.vel.x, S.tb[1].o.s.vel.x, S.tb[2].o.s.vel.x);
		REQUIRE (pen >= -COLLA_DEV_TOL);
		REQUIRE (S.fr.Stats ().missed == 0);
		REQUIRE (S.tb[0].o.s.vel.x <= S.tb[1].o.s.vel.x + 1e-9);
		REQUIRE (S.tb[1].o.s.vel.x <= S.tb[2].o.s.vel.x + 1e-9);
		REQUIRE (S.fr.Stats ().checkFail == 0);
	}
	g_collLog = nullptr;
}

TEST_CASE ("fix1 M3: past the round limit the outside body is a kinematic partner: the plan stops at contact", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	Sim S;
	S.fr.rounds = 0;                                     // no merge allowed: every outside hit is capped
	auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
	S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-3.5, 0, 0), Vector (30, 0, 0), 1.05);
	S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (), Vector (), 1.05);
	S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (2.6, 0, 0), Vector (), 1.05);
	double pen = 0;
	for (int f = 0; f < 6; f++) {
		S.Frame (0.1);
		for (int k = 0; k < 2; k++) pen = std::min (pen, S.tb[k + 1].o.s.pos.x - S.tb[k].o.s.pos.x - 2.0);
	}
	CAPTURE (pen, S.events, S.fr.Stats ().missed, S.tb[0].o.s.vel.x, S.tb[1].o.s.vel.x, S.tb[2].o.s.vel.x);
	REQUIRE (pen >= -COLLA_DEV_TOL);
	REQUIRE (S.tb[0].o.s.vel.x <= S.tb[1].o.s.vel.x + 1e-9);
	REQUIRE (S.tb[1].o.s.vel.x <= S.tb[2].o.s.vel.x + 1e-9);
	REQUIRE (S.fr.Stats ().checkFail == 0);
	g_collLog = nullptr;
}

TEST_CASE ("fix1: spinning rods hit in the past check: one missed line and count per pair per frame, INACCURATE event not left approaching", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	int inacc = 0;
	for (double h : { 0.1, 0.2 })
		for (Vector p : { Vector (4.4, 2.5, 0), Vector (6, 4, 0), Vector (8, 2, 0) }) {
			Sim S;
			auto rod = std::make_shared<Geo> (BoxMesh (Vector (4, 0.3, 0.6)));
			Vector pmi ((0.09 + 0.36)/3, (16 + 0.36)/3, (16 + 0.09)/3);
			S.Add (rod, 500, pmi, Vector (), Vector (p.y*0.5, 0, 0), 5);
			S.Add (rod, 500, pmi, Vector (9, 0, 0), Vector (-p.y*0.5, 0, 0), 5);
			S.tb[0].o.s.omega = Vector (0, 0, p.x); S.tb[1].o.s.omega = Vector (0, 0, -p.x);   // tumbling at 0.4-1.6 rad per step: INACCURATE
			int lines = 0, worst = 0;
			for (int f = 0; f*h < 3.0; f++) {
				g_log.clear ();
				S.Frame (h);
				lines += LogCount ("Collision missed: t=");
				worst = std::max (worst, LogCount ("Collision missed: t="));
			}
			CAPTURE (h, p.x, p.y, lines, worst, S.fr.Stats ().missed, S.events);
			REQUIRE (worst <= 1);                            // one body pair
			REQUIRE (lines == S.fr.Stats ().missed);
			REQUIRE (S.fr.Stats ().past == S.fr.Stats ().missed);
			for (const CollImpactEvent &e : S.evs) {
				CAPTURE (e.t, e.vn, e.vn_post, e.flags);
				if (e.flags & COLLEV_INACCURATE) inacc++;
				REQUIRE (e.vn_post >= 0.0);
			}
			REQUIRE (S.fr.Stats ().checkFail == 0);
		}
	REQUIRE (inacc > 0);
	g_collLog = nullptr;
}
