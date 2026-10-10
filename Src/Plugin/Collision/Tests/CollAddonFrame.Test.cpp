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
	struct TB { CollOrbState o; std::shared_ptr<Geo> geo; uint32_t id; double rmax; Vector push; bool landed = false, woke = false; Vector split; std::vector<Vector> td; double tdK = 0, tdD = 0, tdMu = 0; Vector ft0, mt0; };
	std::vector<TB> tb;
	std::shared_ptr<Geo> baseGeo; Vector basePos;
	int events = 0, writes = 0, lastWrites = 0;
	Vector gxAdd;                                        // added to the exact gravity Finish gets (tests)
	std::vector<CollAWrite> lastOut;                     // the writes of the last frame after Finish
	std::vector<CollImpactEvent> evs;
	double t = 0;
	bool jumpNext = false;                               // the next frame follows a time jump: JUMP entry, Orbiter's cache reset to gravity
	bool touchHold = false;                              // the step keeps the touchdown force of the frame start (the unseen input the addon assumes)
	std::vector<CollABody> lastB;                        // the bodies of the last Run (dev)
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
	bool Touch (const TB &b, Vector &F, Vector &Mb) const        // touchdown points on the floor y = 0 from the step start, as a constant force and body torque
	{
		F = Mb = Vector ();
		bool on = false;
		Vector wg = mul (b.o.s.R, b.o.s.omega);
		for (const Vector &p : b.td) {
			Vector r = mul (b.o.s.R, p), x = b.o.s.pos + r, vp = b.o.s.vel + Xc (wg, r);
			double d = -x.y;
			if (!(d > 0)) continue;
			on = true;
			double fn = std::max (0.0, b.tdK*d - b.tdD*vp.y);
			Vector f (0, fn, 0), vt (vp.x, 0, vp.z);
			if (vt.length () > 1e-9) f -= vt*(b.tdMu*fn/vt.length ());
			F += f; Mb += tmul (b.o.s.R, Xc (r, f));
		}
		return on;
	}
	void Frame (double h)
	{
		for (TB &b : tb) if (!b.td.empty ()) b.o.ground = Touch (b, b.ft0, b.mt0);
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
			k.aTot = b.o.acc; k.arot = b.o.arot; k.gEst = g; k.rmax = b.rmax; k.ground = b.o.ground;
			if (jumpNext) k.entry |= COLLE_JUMP;
			if (b.woke) k.entry |= COLLE_ACTIVATED;
			if (b.landed) {                                  // kinematic at rest, wakeable (6.6)
				k.kin = CollMotion {};
				k.kin.c0 = k.kin.c1 = k.x; k.kin.q0 = k.kin.q1 = k.q; k.kin.h = h; k.kin.tb = 1; k.kin.a0ok = true;
				k.wakeable = true; k.wakeV = k.v; k.wakeWb = k.wb;
			}
			CollPartRef p {};
			p.geom = &b.geo->geom; p.skin = COLL_SKIN_DEFAULT; p.owner = CollOwnerKey { COLLO_VESSEL, b.id, -1, -1, -1, -1 }; p.partKey = 0; p.version = 0;
			if (b.split.length () > 0) {                     // two components at -split and +split, the second owned by id + 100
				p.P0.t = p.P1.t = -b.split; k.parts.push_back (p);
				p.P0.t = p.P1.t = b.split; p.owner.id = b.id + 100; p.partKey = 1;
			}
			k.parts.push_back (p);
			bodies.push_back (k);
		}
		std::vector<CollAWrite> out;
		std::vector<CollImpactEvent> ev;
		fr.Run (fwd, ver, mir, bodies, {}, h, t, host, out, ev);
		lastB = bodies;
		int off = baseGeo ? 1 : 0;
		std::vector<Vector> gx (bodies.size (), g + gxAdd);
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
		lastOut = out;
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
		for (TB &b : tb) if (!b.landed) {
			Vector ft, mt;
			if (!b.td.empty ()) { if (touchHold) { ft = b.ft0; mt = b.mt0; } else Touch (b, ft, mt); }
			b.o.aC = g + b.push + ft/b.o.m; b.o.tauU = mt/b.o.m; mir.Step (b.o, h); b.o.aC = g; b.o.tauU = Vector ();   // the step ends with Orbiter's spin clamp
		}
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
		const Vector dp = S.tb[0].o.s.pos - S.tb[1].o.s.pos, dv = S.tb[0].o.s.vel - S.tb[1].o.s.vel;
		REQUIRE ((dp & dv) > 0.0);                                            // separating along the line of centres (push 120 is a glancing hit, review 2 M1)
		if (push > 0.0) REQUIRE (dp.y > 0.0);                                 // the pushed sphere stays ahead in y: B is not dragged through A
		if (push == 0.0) REQUIRE (S.tb[0].o.s.vel.x < S.tb[1].o.s.vel.x);
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

TEST_CASE ("fix2 M2: a body turning 162 deg in the step: the island builder passes the raw n2, the solver falls back to n", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	g_log.clear ();
	Sim S;
	std::vector<CollSContact> con;
	S.fr.conProbe = &con;
	auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
	const double h = 0.1;
	S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-2.3, 0, 0), Vector (5, 0, 0), 1.05);
	S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (), Vector (), 1.05);
	S.tb[0].o.s.omega = Vector (0, 0, 0.9*3.14159265358979323846/h);
	const Vector P0 = S.P ();
	for (int f = 0; f < 6; f++) S.Frame (h);
	double n2max = 0;
	for (const CollSContact &c : con) n2max = std::max (n2max, c.n2.length ());
	CAPTURE (con.size (), n2max, S.events, S.tb[0].o.s.vel.x, S.tb[1].o.s.vel.x);
	REQUIRE (!con.empty ());
	REQUIRE (n2max < 0.5);                                   // normalised it was 1 and the fallback never ran
	REQUIRE (S.tb[0].o.s.vel.x <= S.tb[1].o.s.vel.x + 1e-9);
	REQUIRE ((S.P () - P0).length () <= 1e-12*S.Pscale ());
	REQUIRE (S.fr.Stats ().checkFail == 0);
	g_collLog = nullptr;
}

TEST_CASE ("fix2 M3: five and six separate 3-sphere chains in one frame: every island merges, momentum exact, none held fixed", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (int nT : { 5, 6 }) {
		g_log.clear ();
		Sim S;
		auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
		for (int t = 0; t < nT; t++) {
			double y = 50.0*t;
			S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-3.5, y, 0), Vector (30, 0, 0), 1.05);
			S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (0, y, 0), Vector (), 1.05);
			S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (2.6, y, 0), Vector (), 1.05);
		}
		const Vector P0 = S.P ();
		double worstP = 0;
		for (int f = 0; f < 6; f++) { S.Frame (0.1); worstP = std::max (worstP, (S.P () - P0).length ()/S.Pscale ()); }
		CAPTURE (nT, worstP, LogCount ("held fixed"), S.fr.Stats ().rounds);
		REQUIRE (worstP <= 1e-12);
		REQUIRE (LogCount ("held fixed") == 0);
		for (int t = 0; t < nT; t++) {
			REQUIRE (S.tb[3*t].o.s.vel.x <= S.tb[3*t + 1].o.s.vel.x + 1e-9);
			REQUIRE (S.tb[3*t + 1].o.s.vel.x <= S.tb[3*t + 2].o.s.vel.x + 1e-9);
		}
		REQUIRE (S.fr.Stats ().checkFail == 0);
	}
	g_collLog = nullptr;
}

TEST_CASE ("fix2 M7: 25-sphere chain at h 0.1: delivery converges with the integrator level held, no check failed", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double gap : { 0.3, 0.05 }) {
		g_log.clear ();
		Sim S;
		auto sph = std::make_shared<Geo> (SphereMesh (1.0, 8, 12));
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-3.5, 0, 0), Vector (30, 0, 0), 1.05);
		for (int k = 0; k < 24; k++) S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (k*(2.0 + gap), 0, 0), Vector (), 1.05);
		const Vector P0 = S.P ();
		double worstP = 0;
		for (int f = 0; f < 15; f++) { S.Frame (0.1); worstP = std::max (worstP, (S.P () - P0).length ()/S.Pscale ()); }
		CAPTURE (gap, worstP, S.fr.Stats ().checkFail, LogCount ("check failed"));
		REQUIRE (LogCount ("check failed") == 0);
		REQUIRE (S.fr.Stats ().checkFail == 0);
		REQUIRE (worstP <= 1e-12);
	}
	g_collLog = nullptr;
}

TEST_CASE ("fix2 Resolve0: past-check pairs spanning two components: every owner pair's event is resolved, none left approaching", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	int multi = 0, missed = 0;
	for (double h : { 0.1, 0.2 })
		for (Vector p : { Vector (4.4, 2.5, 0), Vector (6, 4, 0), Vector (8, 2, 0) }) {
			Sim S;
			auto half = std::make_shared<Geo> (BoxMesh (Vector (4, 0.3, 0.3)));   // upper and lower slab of each rod: a tip hit spans both
			Vector pmi ((0.09 + 0.36)/3, (16 + 0.36)/3, (16 + 0.09)/3);
			S.Add (half, 500, pmi, Vector (), Vector (p.y*0.5, 0, 0), 5);
			S.Add (half, 500, pmi, Vector (9, 0, 0), Vector (-p.y*0.5, 0, 0), 5);
			for (auto &b : S.tb) b.split = Vector (0, 0, 0.3);
			S.tb[0].o.s.omega = Vector (0, 0, p.x); S.tb[1].o.s.omega = Vector (0, 0, -p.x);
			for (int f = 0; f*h < 3.0; f++) S.Frame (h);
			missed += S.fr.Stats ().missed;
			for (const CollImpactEvent &e : S.evs) {
				CAPTURE (h, p.x, e.t, e.vn, e.vn_post, e.flags, e.s[0].owner.vesselId, e.s[1].owner.vesselId);
				if (e.s[0].owner.vesselId > 100 || e.s[1].owner.vesselId > 100) multi++;
				REQUIRE (e.vn_post >= 0.0);
			}
			REQUIRE (S.fr.Stats ().checkFail == 0);
		}
	CAPTURE (multi, missed);
	REQUIRE (multi > 0);
	REQUIRE (missed > 0);
	g_collLog = nullptr;
}

TEST_CASE ("dmg3 L4/L1: a box sliding on a roof gives one contact per frame, vt the sliding speed, Jn m g h, tdir along the slide", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double h : { 1.0/60.0, 0.05 }) {
		Sim S;
		S.g = Vector (0, -9.81, 0);
		S.baseGeo = std::make_shared<Geo> (BoxMesh (Vector (200, 1, 200)));
		S.basePos = Vector (0, -1, 0);
		auto box = std::make_shared<Geo> (BoxMesh (Vector (1, 0.5, 1)));
		const double m = 1000;
		S.Add (box, m, Vector ((0.25 + 1)/3, 2.0/3, (1 + 0.25)/3), Vector (0, 0.5 + 0.04 + 0.02, 0), Vector (15, 0, 0), 1.6);
		int frames = 0;
		for (int f = 0; f*h < 2.0; f++) {
			double vx = S.tb[0].o.s.vel.x;
			S.Frame (h);
			if (f*h < 0.5) continue;
			frames++;
			CAPTURE (h, f, vx, S.fr.contacts.size ());
			REQUIRE (S.fr.contacts.size () == 1);
			const CollContactRec &c = S.fr.contacts[0];
			int sv = c.s[0].owner.vesselId == 1 ? 0 : 1;
			CAPTURE (c.vt, c.Jn, c.Jt, c.dt, c.s[sv].tdir.x, c.s[sv].tdir.y, c.s[sv].tdir.z, c.s[sv].n.y);
			REQUIRE (c.s[sv].owner.vesselId == 1);
			REQUIRE (c.s[1 - sv].owner.vesselId == 0);
			REQUIRE (c.dt == h);
			REQUIRE (std::fabs (c.vt - vx) <= 0.05*vx + 0.05);
			REQUIRE (std::fabs (c.Jn - m*9.81*h) <= 0.1*m*9.81*h);
			REQUIRE (std::fabs (c.Jt - COLL_MU*c.Jn) <= 0.1*c.Jn);
			REQUIRE (c.s[sv].tdir.x > 0.99);                   // the box's surface moves +x relative to the roof
			REQUIRE (c.s[1 - sv].tdir.x < -0.99);
			REQUIRE (std::fabs (dotp (c.s[sv].tdir, c.s[sv].n)) <= 1e-9);
		}
		CAPTURE (h, S.events);
		REQUIRE (frames > 0);
		REQUIRE (S.events <= 1);                               // the slide is a contact, not an event
		REQUIRE (S.fr.Stats ().checkFail == 0);
	}
	g_collLog = nullptr;
}

TEST_CASE ("dmg3 L1: event slip direction of a glancing hit, zero for a head-on one", "[CollAddonFrame]")
{
	for (double vy : { 0.0, 3.0 }) {
		Sim S;
		auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-2.5, 0, 0), Vector (5, vy, 0), 1.05);
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (2.5, 0, 0), Vector (-5, 0, 0), 1.05);
		for (int f = 0; f < 60 && S.evs.empty (); f++) S.Frame (1.0/60.0);
		REQUIRE (S.evs.size () == 1);
		const CollImpactEvent &e = S.evs[0];
		int s1 = e.s[0].owner.vesselId == 1 ? 0 : 1;
		CAPTURE (vy, e.vt, e.s[s1].tdir.x, e.s[s1].tdir.y, e.s[1 - s1].tdir.y);
		if (vy == 0.0) { REQUIRE (e.s[0].tdir.length () == 0.0); REQUIRE (e.s[1].tdir.length () == 0.0); continue; }
		REQUIRE (e.s[s1].tdir.y > 0.9);                        // body 1 slides +y over body 2
		REQUIRE (e.s[1 - s1].tdir.y < -0.9);
		REQUIRE (std::fabs (e.s[s1].tdir.length () - 1.0) <= 1e-9);
	}
}

TEST_CASE ("dmg3 L5: a filtered body pair passes through without contacts or events, another pair still collides", "[CollAddonFrame]")
{
	for (int filt : { 1, 0 }) {
		Sim S;
		auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-2.5, 0, 0), Vector (5, 0, 0), 1.05);
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (2.5, 0, 0), Vector (-5, 0, 0), 1.05);
		S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (0, 8, 0), Vector (), 1.05);
		std::vector<std::pair<uint32_t, uint32_t>> np { filt ? std::make_pair (2u, 1u) : std::make_pair (1u, 3u) };
		S.fwd.SetNoPair (np); S.ver.SetNoPair (np);
		int con = 0;
		for (int f = 0; f < 60; f++) { S.Frame (1.0/60.0); con += (int)S.fr.contacts.size (); }
		CAPTURE (filt, con, S.events, S.tb[0].o.s.pos.x);
		if (filt) { REQUIRE (con == 0); REQUIRE (S.events == 0); REQUIRE (S.tb[0].o.s.pos.x > 2.0); }
		else { REQUIRE (con > 0); REQUIRE (S.events == 1); REQUIRE (S.tb[0].o.s.pos.x < 0.0); }
	}
}

namespace {
bool Fin (const Vector &v) { return std::isfinite (v.x) && std::isfinite (v.y) && std::isfinite (v.z); }
bool FinState (const CollOrbState &o) { return Fin (o.s.pos) && Fin (o.s.vel) && Fin (o.s.omega) && std::isfinite (o.s.Q.qs) && std::isfinite (o.s.Q.qvx) && std::isfinite (o.s.Q.qvy) && std::isfinite (o.s.Q.qvz) && Fin (o.acc) && Fin (o.arot); }
void HBD2 (Sim &S, const Vector &w)                    // Blast cell debris HB_D2 of Coll.Base.Hangar: 11.379 kg, the measured PMI, the CollBreakA touchdown set, on the floor
{
	S.g = Vector (0, -9.81, 0);
	const double m = 11.379, K = 4*m*9.81/0.02;
	const Vector e (0.295, 0.02, 0.211);
	auto plate = std::make_shared<Geo> (BoxMesh (e));
	S.Add (plate, m, Vector (0.0149, 0.0383, 0.029), Vector (0, e.y - m*9.81/(4*K), 0), Vector (), e.length ());
	Sim::TB &b = S.tb.back ();
	b.td = { Vector (0, -e.y, e.z), Vector (-e.x, -e.y, -e.z), Vector (e.x, -e.y, -e.z), Vector (-e.x, e.y, -e.z), Vector (e.x, e.y, -e.z), Vector (-e.x, e.y, e.z), Vector (e.x, e.y, e.z), Vector (0, e.y, e.z) };
	b.tdK = K; b.tdD = 2*0.7*std::sqrt (K*m); b.tdMu = 0.5;
	b.o.s.omega = w;
}
void Settle (Sim &S)                                     // Orbiter's cache after a step under the same touchdown force and no own force
{
	Sim::TB &b = S.tb.back ();
	Vector f, mb;
	if (!b.td.empty ()) S.Touch (b, f, mb);
	b.o.acc = S.g + f/b.o.m; b.o.arot = b.o.EulerInv (mb/b.o.m, b.o.s.omega);
}
Vector Mulc3 (const Vector &a, const Vector &b) { return Vector (a.x*b.x, a.y*b.y, a.z*b.z); }
Vector ClampL (const CollOrbState &o, const Vector &L)   // L as Orbiter keeps it at o's attitude: the body spin clamped to 100 pi (Rigidbody.cpp:279-294)
{
	Vector w = tmul (o.s.R, L);
	w = Vector (w.x/(o.m*o.pmi.x), w.y/(o.m*o.pmi.y), w.z/(o.m*o.pmi.z));
	if (w.length () > COLL_OMEGA_MAX) w *= COLL_OMEGA_MAX/w.length ();
	return mul (o.s.R, Vector (w.x*o.pmi.x, w.y*o.pmi.y, w.z*o.pmi.z)*o.m);
}
double RelL (const CollOrbState &a, const Vector &L) { return (a.SpinL () - L).length ()/L.length (); }
}

TEST_CASE ("P1 HB_D2: ground debris with a small PMI and a large plan: the Sim gets the planned P and the planned L as Orbiter clamps it, the next frame adds nothing", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	const double h = 0.02;
	struct Case { Vector p, l, w; double P, L; };
	const Case cs[] = {                                  // the measured frame (tgtP 207.4 N s, tgtL 159.9 N m s, |w| 29.6) in several directions, then the later 682 N s and a larger spin plan
		{ Vector (1, 0.3, 0.2), Vector (0.3, 1, 0.5), Vector (12, 25, 10), 207.4, 159.9 },
		{ Vector (0.2, 1, -0.4), Vector (1, 0.2, -0.3), Vector (-20, 5, 21), 207.4, 159.9 },
		{ Vector (-0.6, 0.5, 1), Vector (0.1, -0.4, 1), Vector (3, -29, 4), 207.4, 159.9 },
		{ Vector (0.7, -0.2, 0.6), Vector (-0.5, 0.8, 0.3), Vector (16, -9, 23), 682.0, 159.9 },
		{ Vector (-0.3, 0.4, -0.9), Vector (0.6, 0.6, -0.5), Vector (-7, 27, -9), 207.4, 400.0 },
	};
	for (const Case &c : cs) {
		g_log.clear ();
		Sim S, R;                                            // R: the same body without the plan
		for (Sim *x : { &S, &R }) { HBD2 (*x, c.w); x->touchHold = true; Settle (*x); }
		Vector ft, mt;
		REQUIRE (S.Touch (S.tb[0], ft, mt));                 // on the floor: ground contact, force path (6.6)
		REQUIRE (S.tb[0].o.s.omega.length () > 29.0);
		std::vector<CollAPlanEdit> pe { CollAPlanEdit { 1, Vector (), Vector (), c.p.unit ()*(c.P/h), c.l.unit ()*(c.L/h) } };
		S.fr.planEdit = &pe;
		S.Frame (h); R.Frame (h);
		S.fr.planEdit = nullptr;
		const CollOrbState o1 = S.tb[0].o;
		const int fail1 = S.fr.Stats ().checkFail;
		const double eL = RelL (o1, ClampL (o1, R.tb[0].o.SpinL () + c.l.unit ()*c.L)), eP = ((o1.s.vel - R.tb[0].o.s.vel)*o1.m - c.p.unit ()*c.P).length ()/c.P;
		Sim T;                                               // T: S's state after the step, no addon history
		HBD2 (T, c.w); T.touchHold = true; T.tb[0].o = o1;
		S.Frame (h); T.Frame (h);
		const double eT = RelL (S.tb[0].o, T.tb[0].o.SpinL ());
		CAPTURE (c.P, c.L, c.p.x, c.p.y, c.p.z, fail1, S.fr.Stats ().checkFail, S.fr.Stats ().groundWrites, S.fr.Stats ().deliveryIt, o1.s.omega.length (), o1.s.vel.length (), eL, eP, eT);
		REQUIRE (FinState (o1));
		REQUIRE (FinState (S.tb[0].o));
		REQUIRE (fail1 == 0);
		REQUIRE (S.fr.Stats ().checkFail == 0);
		REQUIRE (LogCount ("delivery-nan") == 0);
		REQUIRE (std::fabs (o1.s.omega.length () - COLL_OMEGA_MAX) <= 1e-12*COLL_OMEGA_MAX);   // every case plans more than 100 pi: Orbiter keeps the clamp
		REQUIRE (eL <= 1e-9);
		REQUIRE (eP <= 1e-9);
		REQUIRE (eT <= 1e-9);
	}
	g_collLog = nullptr;
}

TEST_CASE ("custom-fix3 HB_D2: with the touchdown force recomputed after the writes the delivered P and L stay within the contact damping of one step", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	g_log.clear ();
	const double h = 0.02, P = 207.4, L = 159.9;
	const Vector p = Vector (1, 0.3, 0.2).unit (), l = Vector (0.3, 1, 0.5).unit (), w (12, 25, 10);
	Sim S, R;
	for (Sim *x : { &S, &R }) { HBD2 (*x, w); x->touchHold = false; Settle (*x); }
	std::vector<CollAPlanEdit> pe { CollAPlanEdit { 1, Vector (), Vector (), p*(P/h), l*(L/h) } };
	S.fr.planEdit = &pe;
	S.Frame (h); R.Frame (h);
	S.fr.planEdit = nullptr;
	const CollOrbState o1 = S.tb[0].o;
	const double eL = RelL (o1, ClampL (o1, R.tb[0].o.SpinL () + l*L)), eP = ((o1.s.vel - R.tb[0].o.s.vel)*o1.m - p*P).length ()/P;
	CAPTURE (eL, eP, S.fr.Stats ().checkFail);
	REQUIRE (FinState (o1));
	REQUIRE (S.fr.Stats ().checkFail == 0);
	REQUIRE (LogCount ("delivery-nan") == 0);
	REQUIRE (eP <= 0.6);                                     // measured 0.42: the dampers answer the 18 m/s written into an 11 kg piece inside the step, the unseen force the addon holds
	REQUIRE (eL <= 0.6);                                     // measured 0.34, same cause through the touchdown levers
	REQUIRE (eP >= 0.05);                                    // the held-force assumption is visible here, unlike the touchHold cases (1e-9)
	g_collLog = nullptr;
}

TEST_CASE ("P1 spin clamp: a plan above 100 pi is clamped and logged, the Sim keeps the clamp, the next frame sees no own torque in the zeroed arot", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double h : { 1.0/60.0, 0.02 }) {
		g_log.clear ();
		const double m = 40;
		const Vector pmi (0.02, 0.03, 0.04), I = pmi*m, w0 (30, -20, 240), dw (100, -80, 300);
		auto box = std::make_shared<Geo> (BoxMesh (Vector (0.4, 0.3, 0.2)));
		Sim S, R;                                            // R: the same body without the plan
		for (Sim *x : { &S, &R }) { x->Add (box, m, pmi, Vector (), Vector (), 0.6); x->tb[0].o.s.omega = w0; Settle (*x); }
		const CollOrbState o0 = S.tb[0].o;
		Vector wn = w0 + dw;
		wn *= COLL_OMEGA_MAX/wn.length ();
		const Vector L0 = o0.SpinL (), Ln = mul (o0.s.R, Vector (wn.x*I.x, wn.y*I.y, wn.z*I.z));
		const Vector M = Ln.unit ()*(2.0*COLL_OMEGA_MAX*I.z/h);   // the end spin above the clamp at any attitude
		std::vector<CollAPlanEdit> pe { CollAPlanEdit { 1, Vector (), dw, Vector (), M } };
		S.fr.planEdit = &pe;
		S.Frame (h); R.Frame (h);
		S.fr.planEdit = nullptr;
		const CollOrbState o1 = S.tb[0].o;
		const double eL = RelL (o1, ClampL (o1, R.tb[0].o.SpinL () + (Ln - L0) + M*h));
		CAPTURE (h, eL, o1.s.omega.length (), R.tb[0].o.s.omega.length (), S.fr.Stats ().checkFail, S.fr.Stats ().deliveryIt);
		REQUIRE (LogCount ("rad/s clamped") == 1);
		REQUIRE (FinState (o1));
		REQUIRE (S.fr.Stats ().checkFail == 0);
		REQUIRE (LogCount ("delivery-nan") == 0);
		REQUIRE (R.tb[0].o.s.omega.length () < COLL_OMEGA_MAX);
		REQUIRE (std::fabs (o1.s.omega.length () - COLL_OMEGA_MAX) <= 1e-12*COLL_OMEGA_MAX);
		REQUIRE ((o1.arot.x == 0.0 && o1.arot.y == 0.0 && o1.arot.z == 0.0));
		REQUIRE (eL <= 1e-9);
		Sim T;                                               // T: S's state after the step, no addon history
		T.Add (box, m, pmi, Vector (), Vector (), 0.6); T.tb[0].o = o1;
		S.Frame (h); T.Frame (h);
		const double eT = RelL (S.tb[0].o, T.tb[0].o.SpinL ());
		S.Frame (h);
		CAPTURE (eT, S.lastB[0].dev, S.fr.Stats ().jumps);
		REQUIRE (FinState (S.tb[0].o));
		REQUIRE (S.fr.Stats ().checkFail == 0);
		REQUIRE (eT <= 1e-9);                                // the reference step keeps Orbiter's zeroed arot
		REQUIRE (S.lastB[0].dev <= 1e-6);                    // the prediction of frame 2 has no spurious torque
	}
	g_collLog = nullptr;
}

TEST_CASE ("custom-fix3: the plan without the speculative impulse comes from the unclamped plan, then is clamped", "[CollAddonFrame]")
{
	const double ca = std::cos (0.7), sa = std::sin (0.7), cb = std::cos (-0.4), sb = std::sin (-0.4);
	const Matrix R = Matrix (ca, -sa, 0, sa, ca, 0, 0, 0, 1)*Matrix (1, 0, 0, 0, cb, -sb, 0, sb, cb);
	const Vector I (2, 3, 4), u = Vector (0.2, -0.6, 0.77).unit ();
	const Vector dL1 = mul (R, Mulc3 (I, u*200.0));            // the speculative part: +200 rad/s along u
	const Vector w300 = CollNoSpecSpin (R, I, u*500.0, dL1);  // plan 500 (Orbiter keeps 314); without the impulse: 300, not 314 - 200
	CHECK ((w300 - u*300.0).length () <= 1e-9*300.0);
	const Vector w2 = CollNoSpecSpin (R, I, u*900.0, dL1);    // 700 without the impulse: clamped
	CHECK (std::fabs (w2.length () - COLL_OMEGA_MAX) <= 1e-12*COLL_OMEGA_MAX);
	CHECK ((w2.unit () - u).length () <= 1e-12);
}

TEST_CASE ("custom-fix3: a body Orbiter holds one rounding step above 100 pi logs no clamp line", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (int edit = 0; edit < 2; edit++) {
		g_log.clear ();
		const double m = 40;
		const Vector pmi (0.02, 0.03, 0.04);
		auto box = std::make_shared<Geo> (BoxMesh (Vector (0.4, 0.3, 0.2)));
		Sim S;
		S.Add (box, m, pmi, Vector (), Vector (), 0.6);
		S.tb[0].o.s.omega = Vector (0, 0, std::nextafter (COLL_OMEGA_MAX, 1e9)); // what omega *= vmag_max/vmag can leave
		Settle (S);
		std::vector<CollAPlanEdit> pe { CollAPlanEdit { 1, Vector (), Vector (), Vector (), Vector () } };
		if (edit) S.fr.planEdit = &pe;
		S.Frame (0.02);
		S.fr.planEdit = nullptr;
		CAPTURE (edit);
		CHECK (LogCount ("rad/s clamped") == 0);
		CHECK (FinState (S.tb[0].o));
	}
	g_collLog = nullptr;
}

TEST_CASE ("P1 write fallback: a ground body turning once per step cannot carry a horizontal plan by a body-frame force; the state is written", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	const double h = 0.02;
	for (double turns : { 1.0, 2.0 }) {
		g_log.clear ();
		Sim S;
		S.mir.subMax = 1000;                                 // PropSubsampling 1000: the force path's mean rotation is singular to rounding
		HBD2 (S, Vector (0, turns*2.0*3.14159265358979323846/h, 0));
		S.tb[0].o.pmi = Vector (0.03, 0.03, 0.03);
		std::vector<CollAPlanEdit> pe { CollAPlanEdit { 1, Vector (), Vector (), Vector (207.4/h, 0, 0), Vector () } };
		S.fr.planEdit = &pe;
		S.Frame (h);
		CAPTURE (turns, S.fr.Stats ().checkFail, S.fr.Stats ().groundWrites, S.writes, S.fr.Stats ().forceWrites);
		REQUIRE (FinState (S.tb[0].o));
		REQUIRE (S.fr.Stats ().checkFail == 0);
		REQUIRE (S.fr.Stats ().groundWrites == 1);
		REQUIRE (S.writes == 1);
		REQUIRE (LogCount ("state written") == 1);
	}
	g_collLog = nullptr;
}

TEST_CASE ("P1 non-finite plan: nothing is written, Orbiter's own step stands, the next frame is finite", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	const double h = 0.02, nan = std::nan ("");
	for (int k = 0; k < 3; k++) {
		g_log.clear ();
		Sim S, R;                                            // R: the same body without the plan
		HBD2 (S, Vector (12, 25, 10));
		HBD2 (R, Vector (12, 25, 10));
		CollAPlanEdit e { 1, Vector (), Vector (), Vector (100, 0, 0), Vector (0, 50, 0) };
		if (k == 0) e.F.x = nan;
		else if (k == 1) e.M.z = nan;
		else e.dwb.y = nan;
		std::vector<CollAPlanEdit> pe { e };
		S.fr.planEdit = &pe;
		S.Frame (h); R.Frame (h);
		S.fr.planEdit = nullptr;
		CAPTURE (k, S.fr.Stats ().checkFail, S.writes);
		REQUIRE (S.fr.Stats ().checkFail == 1);
		REQUIRE (LogCount ("delivery-nan") == 1);
		REQUIRE (S.writes == 0);
		REQUIRE (FinState (S.tb[0].o));
		REQUIRE (S.tb[0].o.s.pos.x == R.tb[0].o.s.pos.x); REQUIRE (S.tb[0].o.s.pos.y == R.tb[0].o.s.pos.y); REQUIRE (S.tb[0].o.s.pos.z == R.tb[0].o.s.pos.z);
		REQUIRE (S.tb[0].o.s.vel.x == R.tb[0].o.s.vel.x); REQUIRE (S.tb[0].o.s.omega.y == R.tb[0].o.s.omega.y);
		for (int f = 0; f < 3; f++) { S.Frame (h); R.Frame (h); }
		REQUIRE (FinState (S.tb[0].o));
		REQUIRE (S.fr.Stats ().checkFail == 1);
		REQUIRE ((S.tb[0].o.s.pos - R.tb[0].o.s.pos).length () == 0.0);
	}
	g_collLog = nullptr;
}

TEST_CASE ("P1 Finish: a non-finite exact gravity keeps Apply's force; momentum exact, no NaN", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	g_log.clear ();
	Sim S;
	S.mir.mode[0] = COLLM_RK4;                           // the cached acceleration enters the velocity stages
	S.gxAdd = Vector (std::nan (""), 0, 0);
	auto sph = std::make_shared<Geo> (SphereMesh (1.0, 12, 24));
	S.Add (sph, 1000, Vector (0.4, 0.4, 0.4), Vector (-1.05 - 2.5, 0, 0), Vector (5, 0, 0), 1.05);
	S.Add (sph, 3000, Vector (0.4, 0.4, 0.4), Vector (1.05 + 2.5, 0, 0), Vector (-5, 0, 0), 1.05);
	const Vector P0 = S.P ();
	int weighed = 0;
	for (int f = 0; f < 60; f++) {
		S.Frame (1.0/60.0);
		CAPTURE (f);
		for (const CollAWrite &w : S.lastOut) { weighed += w.weight ? 1 : 0; REQUIRE (Fin (w.Fb)); REQUIRE (Fin (w.Mb)); }
		REQUIRE (FinState (S.tb[0].o));
		REQUIRE (FinState (S.tb[1].o));
		REQUIRE ((S.P () - P0).length () <= 1e-12*S.Pscale ());
	}
	CAPTURE (S.events, S.writes, weighed);
	REQUIRE (weighed > 0);
	REQUIRE (S.events == 1);
	REQUIRE (S.fr.Stats ().checkFail == 0);
	g_collLog = nullptr;
}

TEST_CASE ("P2 LANDED body on a pad hit from above keeps its pad contact: no sinking, no pad event the next frames", "[CollAddonFrame]")
{
	g_collLog = LogSink;
	for (double h : { 1.0/60.0, 0.05 })
		for (double u : { 2.0, 6.0 }) {
			g_log.clear ();
			Sim S;
			S.g = Vector (0, -9.81, 0);
			S.baseGeo = std::make_shared<Geo> (BoxMesh (Vector (20, 1, 20)));
			S.basePos = Vector (0, -1, 0);
			auto box = std::make_shared<Geo> (BoxMesh (Vector (1, 0.5, 1)));
			const Vector pmi ((0.25 + 1)/3, 2.0/3, (1 + 0.25)/3);
			const double y0 = 0.5 + 2*COLL_SKIN_DEFAULT;
			S.Add (box, 2000, pmi, Vector (0, y0, 0), Vector (), 1.6);
			S.Add (box, 1000, pmi, Vector (0.3, y0 + 1.0 + 2*COLL_SKIN_DEFAULT + 1.5*u*h, 0), Vector (0, -u, 0), 1.6);
			S.tb[0].landed = true;
			int wokeAt = -1, padEv = 0, woke = 0;
			double ymin = 1e9;
			for (int f = 0; f*h < 1.0; f++) {
				size_t e0 = S.evs.size ();
				S.Frame (h);
				if (wokeAt < 0 && !S.tb[0].landed) wokeAt = f;
				for (size_t k = e0; k < S.evs.size (); k++) {
					if (S.evs[k].s[0].owner.vesselId == 0 || S.evs[k].s[1].owner.vesselId == 0) padEv++;
					if (S.evs[k].flags & COLLEV_WOKE_LANDED) woke++;
				}
				if (wokeAt >= 0) ymin = std::min (ymin, S.tb[0].o.s.pos.y);
			}
			CAPTURE (h, u, wokeAt, padEv, woke, S.events, ymin, y0, LogCount ("GRACE"));
			REQUIRE (wokeAt >= 0);
			REQUIRE (woke == 1);
			REQUIRE (padEv == 0);
			REQUIRE (ymin >= y0 - COLL_SLOP);
			REQUIRE (LogCount ("GRACE") == 0);
			REQUIRE (S.fr.Stats ().checkFail == 0);
		}
	g_collLog = nullptr;
}
