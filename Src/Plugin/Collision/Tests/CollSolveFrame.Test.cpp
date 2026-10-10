// not upstream: unit tests for Src/Orbiter/CollSolveFrame (D3 U13, U14 adapter, U18, U19, U20)
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "CollSolve.h"

namespace {

Matrix QMatrix (const Quaternion &q) { Matrix R; R.Set (q); return R; }

std::vector<std::string> g_log;
void LogSink (int level, const char *msg) { g_log.push_back (std::to_string (level) + " " + msg); }
struct LogCapture {
	LogCapture () { g_log.clear (); g_collLog = LogSink; }
	~LogCapture () { g_collLog = nullptr; }
	int Count (const char *s) const { int n = 0; for (const std::string &l : g_log) if (l.find (s) != std::string::npos) n++; return n; }
};

void Quad (CollGroupData &g, const Vector &a, const Vector &b, const Vector &c, const Vector &d)
{
	uint16_t o = (uint16_t)g.vtx.size ();
	for (const Vector *v : { &a, &b, &c, &d }) g.vtx.push_back (CollVtx { (float)v->x, (float)v->y, (float)v->z, 0, 0, 0, 0, 0 });
	const uint16_t f[6] = { 0, 1, 2, 0, 2, 3 };
	for (uint16_t i : f) g.idx.push_back ((uint16_t)(o + i));
}

CollGroupData BoxMesh (const Vector &h)
{
	CollGroupData g;
	for (int i = 0; i < 8; i++) g.vtx.push_back (CollVtx { (float)((i & 1) ? h.x : -h.x), (float)((i & 2) ? h.y : -h.y), (float)((i & 4) ? h.z : -h.z), 0, 0, 0, 0, 0 });
	const uint16_t f[36] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4, 2,6,3, 3,6,7, 0,4,2, 2,4,6, 1,3,5, 3,7,5 };
	g.idx.assign (f, f + 36);
	return g;
}

// UV sphere with its poles on the x axis
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

// octahedron of radius r: single-point contacts on flat faces
CollGroupData OctaMesh (double r)
{
	CollGroupData g;
	const double v[6][3] = { { r, 0, 0 }, { -r, 0, 0 }, { 0, r, 0 }, { 0, -r, 0 }, { 0, 0, r }, { 0, 0, -r } };
	for (const auto &p : v) g.vtx.push_back (CollVtx { (float)p[0], (float)p[1], (float)p[2], 0, 0, 0, 0, 0 });
	const uint16_t f[24] = { 0,2,4, 2,1,4, 1,3,4, 3,0,4, 2,0,5, 1,2,5, 3,1,5, 0,3,5 };
	g.idx.assign (f, f + 24);
	return g;
}

struct Geo {
	CollGroupData g;
	CollGeom geom;
	explicit Geo (const CollGroupData &d) : g (d)
	{
		CollSrcGroup src { &g, CollSrc { 0, 0, 0, 0, 0 } };
		REQUIRE (geom.Build (&src, 1, COLL_WELD_DEFAULT, nullptr));
	}
};

CollPartRef MakePart (const CollGeom *g, uint32_t owner, uint32_t key, uint16_t mesh = 0)
{
	CollPartRef p {};
	p.geom = g; p.skin = COLL_SKIN_DEFAULT;
	p.owner = CollOwnerKey { COLLO_VESSEL, owner, -1, -1, -1, -1 };
	p.partKey = key; p.version = g ? g->version : 0; p.mesh = mesh; p.mask = nullptr;
	return p;
}

CollBody MakeBody (const Vector &c0, const Vector &v, double h, uint32_t id, uint8_t kind, const std::vector<CollPartRef> &parts, const Vector &wg = Vector ())
{
	CollBody b {};
	b.m.c0 = c0; b.m.v0 = v; b.m.c1 = c0 + v*h; b.m.v1 = v;
	Quaternion q1;
	CollRotate (q1, wg*h);                                              // identity start: body and world axes agree
	b.m.q1.Set (q1);
	b.m.w0g = wg; b.m.w1g = wg; b.m.h = h; b.m.ta = 0; b.m.tb = 1; b.m.a0ok = true;
	b.parts = parts; b.rmax = 0; b.id = id; b.kind = kind; b.entry = 0; b.jump1 = false; b.planet = -1;
	return b;
}

CollFrameBody MakeFB (const CollBody &b, bool dyn, double m, const Vector &pmi)
{
	CollFrameBody f {};
	f.dyn = dyn; f.wakeable = false; f.id = b.id; f.m = m; f.pmi = pmi;
	f.x1 = b.m.c1; f.v1 = b.m.v1; f.q1.Set (b.m.q1); f.wb1 = tmul (QMatrix (b.m.q1), b.m.w1g);
	return f;
}

struct Host : CollSolveHost {
	const CollDetect *det = nullptr;
	CollSMat ma, mb;                                                    // material of side A and B
	CollSMat Material (const CollPairResult &r, int i, int side) override { return side ? mb : ma; }
	void Feature (const CollPairResult &r, int i, int side, CollImpactSide &s) override
	{
		s.owner = CollOwnerRefOf (det->Owner (r, i, side));
		const CollBody &B = det->Body (side ? r.bodyB : r.bodyA);
		s.mesh = B.parts[side ? r.pt[i].partB : r.pt[i].partA].mesh;
		s.grp = 0; s.tri = (int)(side ? r.pt[i].triB : r.pt[i].triA);
	}
};

// frame states after the write-back deltas in application order (what CollWorld commits)
struct Fin {
	std::vector<Vector> x, v, wb; std::vector<Quaternion> q;
	Fin (const std::vector<CollFrameBody> &fb, const std::vector<CollBodyDelta> &d)
	{
		for (const CollFrameBody &b : fb) {
			x.push_back (b.x1); v.push_back (b.woke ? b.wakeV1 : b.v1); wb.push_back (b.woke ? b.wakeWb1 : b.wb1); q.push_back (b.q1);
		}
		for (const CollBodyDelta &e : d) REQUIRE (CollApplyDeltaState (x[e.body], v[e.body], q[e.body], wb[e.body], fb[e.body].pmi*fb[e.body].m, e.d));
	}
};

// test-side D3 3.2 mapping of one island (from the design table) to compare with the adapter
CollIsland Mirror (const CollDetect &det, const std::vector<CollPairResult> &rs, const std::vector<CollFrameBody> &fb, CollSolveHost &host, double h, const CollSolveParams &p)
{
	CollIsland isl;
	isl.tau = rs[0].tau; isl.h = h;
	std::vector<int> member, map (fb.size (), -1), at (fb.size (), -1);
	for (size_t k = 0; k < rs.size (); k++) {
		if (at[rs[k].bodyA] < 0) at[rs[k].bodyA] = (int)k;
		if (at[rs[k].bodyB] < 0) at[rs[k].bodyB] = (int)k;
	}
	for (int pass = 0; pass < 2; pass++)
		for (size_t i = 0; i < fb.size (); i++) if (at[i] >= 0 && fb[i].dyn == (pass == 0)) member.push_back ((int)i);
	int o = -1;
	for (int i : member) if (fb[i].dyn && (o < 0 || fb[i].id < fb[o].id)) o = i;
	Vector O = fb[o].x1;
	for (size_t j = 0; j < member.size (); j++) {
		int f = member[j];
		map[f] = (int)j;
		const CollPairResult &r = rs[at[f]];
		const CollBodyAt &s = f == r.bodyA ? r.a : r.b;
		CollSBody b {};
		b.dyn = fb[f].dyn; b.m = fb[f].m; b.pmi = fb[f].pmi;
		b.Rt = QMatrix (s.q); b.xt = (r.origin - O) + s.c; b.vt = s.v; b.wt = s.w;
		b.R1 = fb[f].dyn ? QMatrix (fb[f].q1) : QMatrix (det.Body (f).m.Rot (1.0));
		b.x1 = fb[f].dyn ? fb[f].x1 - O : b.xt; b.v1 = fb[f].dyn ? fb[f].v1 : Vector (); b.wb1 = fb[f].dyn ? fb[f].wb1 : Vector ();
		isl.body.push_back (b);
	}
	for (const CollPairResult &r : rs) {
		Matrix RtA = QMatrix (r.a.q), RtB = QMatrix (r.b.q);
		Matrix TA = (fb[r.bodyA].dyn ? QMatrix (fb[r.bodyA].q1) : QMatrix (det.Body (r.bodyA).m.Rot (1.0)))*transp (RtA);
		Matrix TB = (fb[r.bodyB].dyn ? QMatrix (fb[r.bodyB].q1) : QMatrix (det.Body (r.bodyB).m.Rot (1.0)))*transp (RtB);
		for (int i = 0; i < r.npt; i++) {
			const CollContact &pt = r.pt[i];
			CollSContact c {};
			c.a = map[r.bodyA]; c.b = map[r.bodyB];
			Vector mid = (pt.pA + pt.pB)*0.5;
			c.p = (r.origin - O) + mid;                                    // p_isl (3.2)
			c.n = pt.n;
			c.n2 = (mul (TA, pt.n) + mul (TB, pt.n)).unit ();
			c.kind = r.kind; c.gap = pt.gap;
			if (r.kind == COLL_SPECULATIVE) c.gap = r.specGap;
			else if ((r.flags & COLLF_INACCURATE) && pt.gap > 0) c.kind = COLL_SPECULATIVE;
			c.flags = pt.flags;
			CollSMat a = host.Material (r, i, 0), b = host.Material (r, i, 1);
			c.e0 = std::max (a.e0, b.e0); c.vy = std::min (a.vy, b.vy); c.mu = std::sqrt (a.mu*b.mu);
			Vector s1A = det.SurfaceVel (r, i, 0, 1.0), s1B = det.SurfaceVel (r, i, 1, 1.0);
			const CollMotion &mA = det.Body (r.bodyA).m, &mB = det.Body (r.bodyB).m;
			c.vka_t = fb[r.bodyA].dyn ? pt.vsA : r.a.v + Xc (r.a.w, mid - r.a.c) + pt.vsA;
			c.vkb_t = fb[r.bodyB].dyn ? pt.vsB : r.b.v + Xc (r.b.w, mid - r.b.c) + pt.vsB;
			c.vka_1 = fb[r.bodyA].dyn ? s1A : mA.Vel (1.0) + Xc (mA.Omega (1.0), mul (TA, mid - r.a.c)) + s1A;
			c.vkb_1 = fb[r.bodyB].dyn ? s1B : mB.Vel (1.0) + Xc (mB.Omega (1.0), mul (TB, mid - r.b.c)) + s1B;
			isl.con.push_back (c);
		}
	}
	REQUIRE (isl.Solve (p));
	return isl;
}

bool Close (const Vector &a, const Vector &b, double rel)
{
	return (a - b).length () <= rel*std::max (std::max (a.length (), b.length ()), 1e-300);
}

const CollBodyDelta *DeltaOf (const std::vector<CollBodyDelta> &d, int body, bool poscorr = false)
{
	for (const CollBodyDelta &e : d) if (e.body == body && e.poscorr == poscorr) return &e;
	return nullptr;
}

double ERest (double v, double e0 = COLL_E0, double vy = COLL_VY) { return CollRestitution (v, e0, vy, CollSolveParams ()); }

bool SameBits4 (const CollOwnerKey &a, const CollOwnerKey &b) { return a == b; }

// hand-made result: kinematic carrier A (0), dynamic box B (1); far apart, so Resweep finds nothing
struct Scene {
	Geo plate { BoxMesh (Vector (5, 0.1, 5)) }, box { BoxMesh (Vector (1, 1, 1)) }, door { BoxMesh (Vector (1, 0.05, 1)) };
	CollDetect det;
	std::vector<CollFrameBody> fb;
	Host host;
	double h;
	Scene (double h_, const Vector &vbox, const Vector &boxAt1, bool twoParts = false) : h (h_)
	{
		det.Begin (CollParams (), h);
		std::vector<CollPartRef> pa { MakePart (&plate.geom, 1, 1, 0) };
		if (twoParts) pa.push_back (MakePart (&door.geom, 1, 2, 1));
		CollBody A = MakeBody (Vector (-1000, 0, 0), Vector (), h, 1, COLLB_FROZEN, pa);
		CollBody B = MakeBody (Vector (1000, 0, 0), vbox, h, 2, COLLB_DYNAMIC, { MakePart (&box.geom, 2, 1, 0) });
		REQUIRE (det.AddBody (A) == 0);
		REQUIRE (det.AddBody (B) == 1);
		fb.push_back (MakeFB (A, false, 1.0, Vector (1, 1, 1)));
		fb.push_back (MakeFB (B, true, 1000.0, Vector (0.667, 0.667, 0.667)));
		fb[1].x1 = boxAt1;                                              // the frame state the result describes (origin 0)
		host.det = &det;
	}
};

// result of carrier A (at origin) vs box B with points on the box bottom, n from B to A (down)
CollPairResult BoxResult (uint8_t kind, double tau, const Vector &boxAtTau, const Vector &vbox, const std::vector<Vector> &pts, const std::vector<double> &gap, const std::vector<uint8_t> &flags)
{
	CollPairResult r {};
	r.kind = (CollKind)kind; r.bodyA = 0; r.bodyB = 1; r.tau = tau; r.origin = Vector ();
	r.b.c = boxAtTau; r.b.v = vbox;
	r.npt = (int)pts.size ();
	for (int i = 0; i < r.npt; i++) {
		CollContact &c = r.pt[i];
		c.n = Vector (0, -1, 0);
		c.pB = pts[i]; c.pA = pts[i] + c.n*(gap[i] + 2*COLL_SKIN_DEFAULT);
		c.gap = gap[i]; c.flags = flags[i]; c.partA = 0; c.partB = 0;
	}
	return r;
}

} // namespace

TEST_CASE ("U13 adapter sign and frame: two spheres with the pair origin at 0, 1.5e11 m and 4.5e12 m", "[CollSolveFrame]")
{
	Geo sph (SphereMesh (1.0, 12, 16));
	const double h = 1.0/64.0, d0 = 1.0390625;                         // centres and steps exactly representable at 4.5e12 m
	struct Out { Vector vA, vB, cA, cB, p; double tau; } ref {};
	for (double X0 : { 0.0, 1.5e11, 4.5e12 }) {
		CollDetect det;
		det.Begin (CollParams (), h);
		CollBody A = MakeBody (Vector (X0 - d0, 0, 0), Vector (2, 0, 0), h, 1, COLLB_DYNAMIC, { MakePart (&sph.geom, 1, 1) });
		CollBody B = MakeBody (Vector (X0 + d0, 0, 0), Vector (-2, 0, 0), h, 2, COLLB_DYNAMIC, { MakePart (&sph.geom, 2, 1) });
		REQUIRE (det.AddBody (A) == 0);
		REQUIRE (det.AddBody (B) == 1);
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		det.Detect (res, st);
		REQUIRE (res.size () == 1);
		REQUIRE (res[0].kind == COLL_TOI);
		for (int i = 0; i < res[0].npt; i++) REQUIRE (res[0].pt[i].n.x < -0.99);  // normal from B to A
		std::vector<CollFrameBody> fb { MakeFB (A, true, 500, Vector (2.28, 2.31, 0.79)), MakeFB (B, true, 500, Vector (2.28, 2.31, 0.79)) };
		Host host; host.det = &det;
		CollFrameSolver fs;
		fs.check = true;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		LogCapture lc;
		fs.Run (det, res, fb, h, 100.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		REQUIRE (lc.Count ("Collision check") == 0);
		Fin f (fb, delta);
		Out o { f.v[0], f.v[1], ev.empty () ? Vector () : ev[0].s[0].c, ev.empty () ? Vector () : ev[0].s[1].c, (res[0].pt[0].pA + res[0].pt[0].pB)*0.5, res[0].tau };
		std::printf ("U13: X0 %.1e: tau %.6f, n (%.3f %.3f %.3f), vA %.6f vB %.6f m/s, event c_A (%.5f %.5f %.5f), rounds %d, islands %d\n", X0, o.tau,
			res[0].pt[0].n.x, res[0].pt[0].n.y, res[0].pt[0].n.z, o.vA.x, o.vB.x, o.cA.x, o.cA.y, o.cA.z, fs.stats.rounds, fs.stats.islands);
		REQUIRE (ev.size () == 1);
		REQUIRE ((ev[0].flags & COLLEV_FIRST));
		REQUIRE (o.vA.x < 0.0);                                         // separating: A moves to -x, B to +x
		REQUIRE (o.vB.x > 0.0);
		double e = ERest (4.0);
		REQUIRE (std::fabs ((o.vB.x - o.vA.x) - 4.0*e) < 1e-6);
		REQUIRE (std::fabs (ev[0].vn - 4.0) < 1e-6);
		REQUIRE (std::fabs (o.cA.x - (1.0 + 0.5*(res[0].pt[0].pB - res[0].pt[0].pA).length ())) < 1e-4); // point on A's pole side in A's rest frame
		if (X0 == 0.0) ref = o;
		else {
			REQUIRE ((o.p - ref.p).length () < 1e-4);                   // points relative to the pair origin
			REQUIRE ((o.cA - ref.cA).length () < 1e-4);
			REQUIRE ((o.cB - ref.cB).length () < 1e-4);
			REQUIRE (std::fabs (o.tau - ref.tau) < 1e-9);
			REQUIRE ((o.vA - ref.vA).length () < 1e-9);
			REQUIRE ((o.vB - ref.vB).length () < 1e-9);
		}
	}
}

TEST_CASE ("U14 adapter: SPECULATIVE specGap in both phases; INACCURATE per point", "[CollSolveFrame]")
{
	const double h = 0.1, tau = 0.5, tr = (1 - tau)*h, gap = 0.05;
	std::vector<Vector> corners;
	for (double x : { -1.0, 1.0 }) for (double z : { -1.0, 1.0 }) corners.push_back (Vector (x, 0.1 + gap + 2*COLL_SKIN_DEFAULT, z));
	for (double u : { 0.5, 1.0, 3.0 }) {
		Vector at (0, 1.1 + gap + 2*COLL_SKIN_DEFAULT, 0), vb (0, -u, 0);
		Scene s (h, vb, at + vb*tr);
		std::vector<CollPairResult> res { BoxResult (COLL_SPECULATIVE, tau, at, vb, corners, { 0.07, 0.07, 0.07, 0.07 }, { COLLP_FIRST, COLLP_FIRST, COLLP_FIRST, COLLP_FIRST }) };
		res[0].specGap = gap;
		CollFrameSolver fs;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		fs.Run (s.det, res, s.fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, s.host, delta, ev);
		Fin f (s.fb, delta);
		double allowed = gap/tr, uf = -f.v[1].y;
		const CollImpactEvent *e = ev.empty () ? nullptr : &ev[0];
		double ua = e ? -e->vn_post : u;
		double gapEnd = gap - tr*0.5*(ua + uf);
		std::printf ("U14: SPECULATIVE u %.1f: approach after phase 1 %.6f, at t1 %.6f (allowed %.3f), final gap %.2e, deltas %zu\n", u, ua, uf, allowed, gapEnd, delta.size ());
		REQUIRE (fs.stats.resweeps <= 1);
		if (u <= allowed) REQUIRE (delta.empty ());
		else {
			REQUIRE (std::fabs (ua - allowed) < 1e-6);
			REQUIRE (std::fabs (uf - allowed) < 1e-6);
			REQUIRE (e);
			REQUIRE ((e->flags & COLLEV_SPECULATIVE));
		}
		REQUIRE (gapEnd >= -1e-9);
	}
	// INACCURATE TOI: the +0.03 point solves like SPECULATIVE with its gap, the -0.002 point as TOI
	Vector at (0, 1.1 + 2*COLL_SKIN_DEFAULT, 0), vb (0.2, -3.0, 0.1);
	Scene s (h, vb, at + vb*tr);
	std::vector<Vector> pts { Vector (-1, 0.1 + 2*COLL_SKIN_DEFAULT, -0.5), Vector (1, 0.1 + 2*COLL_SKIN_DEFAULT, 0.5) };
	std::vector<CollPairResult> res { BoxResult (COLL_TOI, tau, at, vb, pts, { 0.03, -0.002 }, { 0, 0 }) };
	res[0].flags = COLLF_INACCURATE;
	std::vector<CollPairResult> one (res);
	CollIsland m = Mirror (s.det, one, s.fb, s.host, h, CollSolveParams ());
	REQUIRE (m.con[0].kind == COLL_SPECULATIVE);
	REQUIRE (m.con[1].kind == COLL_TOI);
	CollFrameSolver fs;
	std::vector<CollBodyDelta> delta;
	std::vector<CollImpactEvent> ev;
	fs.Run (s.det, res, s.fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, s.host, delta, ev);
	const CollBodyDelta *d = DeltaOf (delta, 1);
	REQUIRE (d);
	CollDelta md;
	m.Delta (0, md);
	REQUIRE (Close (d->d.dv, md.dv, 1e-12)); REQUIRE (Close (d->d.dx, md.dx, 1e-12));
	REQUIRE (Close (d->d.dLw, md.dLw, 1e-12)); REQUIRE (Close (d->d.dth, md.dth, 1e-12));
	CollPairResult both = res[0];                                       // negative control: both points as TOI differ
	both.flags = 0;
	std::vector<CollPairResult> two { both };
	CollIsland m2 = Mirror (s.det, two, s.fb, s.host, h, CollSolveParams ());
	CollDelta md2;
	m2.Delta (0, md2);
	REQUIRE ((md2.dv - md.dv).length () > 1e-3);
	REQUIRE (ev.size () == 1);
	REQUIRE ((ev[0].flags & COLLEV_INACCURATE));
	std::printf ("U14: INACCURATE: dv adapter (%.6f %.6f %.6f), mirror (%.6f %.6f %.6f), both-TOI control dv.y %.6f\n", d->d.dv.x, d->d.dv.y, d->d.dv.z, md.dv.x, md.dv.y, md.dv.z, md2.dv.y);
}

TEST_CASE ("U18 re-sweep start state and rule: impact at tau = 0; RESTING island with a pending CORE result", "[CollSolveFrame]")
{
	// impact at tau = 0: box touching the floor at t0, approaching at u
	{
		Geo floor (BoxMesh (Vector (10, 0.5, 10))), box (BoxMesh (Vector (1, 1, 1)));
		const double h = 1.0/60.0, u = 1.4;
		CollDetect det;
		det.Begin (CollParams (), h);
		CollBody F = MakeBody (Vector (0, -0.5, 0), Vector (), h, 1, COLLB_FROZEN, { MakePart (&floor.geom, 1, 1) });
		CollBody B = MakeBody (Vector (0, 1.0 + 2*COLL_SKIN_DEFAULT + 0.01, 0), Vector (0, -u, 0), h, 2, COLLB_DYNAMIC, { MakePart (&box.geom, 2, 1) });
		REQUIRE (det.AddBody (F) == 0);
		REQUIRE (det.AddBody (B) == 1);
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		det.Detect (res, st);
		REQUIRE (!res.empty ());
		REQUIRE (res[0].kind == COLL_RESTING);
		CollMotion m0 = det.Body (1).m;
		std::vector<CollFrameBody> fb { MakeFB (F, false, 1, Vector (1, 1, 1)), MakeFB (B, true, 1000, Vector (0.667, 0.667, 0.667)) };
		Host host; host.det = &det;
		CollFrameSolver fs;
		fs.check = true;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		LogCapture lc;
		fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		Fin f (fb, delta);
		double e = ERest (u);
		std::printf ("U18: impact at tau 0: kinds of the first pass %d results, rounds %d, islands %d, resweeps %d, deltas %zu, v after %.6f (e u %.6f), dKE %.4f J (closed form %.4f)\n",
			(int)res.size (), fs.stats.rounds, fs.stats.islands, fs.stats.resweeps, delta.size (), f.v[1].y, e*u, ev.empty () ? 0.0 : ev[0].dKE, 0.5*1000*u*u*(1 - e*e));
		REQUIRE (lc.Count ("Collision check") == 0);
		REQUIRE (fs.stats.resweeps >= 1);
		REQUIRE (delta.size () == 1);                                    // round 2 adds no impulse
		REQUIRE (std::fabs (f.v[1].y - e*u) < 1e-6);
		REQUIRE (ev.size () == 1);
		REQUIRE (std::fabs (ev[0].dKE - 0.5*1000*u*u*(1 - e*e)) < 1e-6*ev[0].dKE); // dKE once
		// CollRestart: start state at tau = 0 with dv = dP1/m, end state = the corrected t1 state
		const CollMotion &m1 = det.Body (1).m;
		REQUIRE (m1.ta == 0.0);
		REQUIRE (std::fabs (m1.v0.y - (m0.Vel (0.0).y + ev[0].Jn/1000.0)) < 1e-9);
		REQUIRE (std::memcmp (m1.c1.data, f.x[1].data, sizeof (f.x[1].data)) == 0);
		REQUIRE (std::memcmp (m1.v1.data, f.v[1].data, sizeof (f.v[1].data)) == 0);
		REQUIRE (std::memcmp (m1.q1.data, f.q[1].data, sizeof (f.q[1].data)) == 0);
		REQUIRE ((m1.w1g - mul (QMatrix (f.q[1]), f.wb[1])).length () == 0.0);
	}
	// RESTING island, zero impulse, pending CORE result: box sliding on a floor into a wall of its part
	{
		CollGroupData lg;
		Quad (lg, Vector (-6, 0, -5), Vector (-6, 0, 5), Vector (1.5, 0, 5), Vector (1.5, 0, -5));
		Quad (lg, Vector (1.5, 0, -5), Vector (1.5, 0, 5), Vector (1.5, 5, 5), Vector (1.5, 5, -5));
		Geo L (lg), box (BoxMesh (Vector (1, 1, 1)));
		const double h = 0.1, u = 10.0;
		CollDetect det;
		det.Begin (CollParams (), h);
		CollBody F = MakeBody (Vector (0, 0, 0), Vector (), h, 1, COLLB_FROZEN, { MakePart (&L.geom, 1, 1) });
		CollBody B = MakeBody (Vector (-0.2, 1.0 + 2*COLL_SKIN_DEFAULT + 0.005, 0), Vector (u, 0, 0), h, 2, COLLB_DYNAMIC, { MakePart (&box.geom, 2, 1) });
		REQUIRE (det.AddBody (F) == 0);
		REQUIRE (det.AddBody (B) == 1);
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		det.Detect (res, st);
		bool rest = false, core = false;
		for (const CollPairResult &r : res) { rest = rest || r.kind == COLL_RESTING; core = core || (r.flags & COLLF_CORE); }
		std::printf ("U18: wall: first pass %zu results (RESTING %d, CORE %d)\n", res.size (), (int)rest, (int)core);
		REQUIRE (rest);
		REQUIRE (core);
		CollMotion m0 = det.Body (1).m;
		std::vector<CollFrameBody> fb { MakeFB (F, false, 1, Vector (1, 1, 1)), MakeFB (B, true, 1000, Vector (0.667, 0.667, 0.667)) };
		Host host; host.det = &det;
		CollFrameSolver fs;
		fs.check = true;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		LogCapture lc;
		fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		Fin f (fb, delta);
		bool staleCore = false, resweptToi = false;
		double tauWall = -1;
		for (const CollPairResult &r : res) {
			if ((r.flags & COLLF_CORE) && !(r.flags & COLLF_RESWEEP)) staleCore = true;
			if ((r.flags & COLLF_RESWEEP) && r.kind == COLL_TOI) { resweptToi = true; tauWall = r.tau; }
		}
		double e = ERest (u);
		const CollFrameBody &b1 = fb[1];
		Vector Ib = b1.pmi*b1.m;
		double ke0 = 0.5*b1.m*b1.v1.length2 (), ke1 = 0.5*b1.m*f.v[1].length2 () + 0.5*(Ib.x*f.wb[1].x*f.wb[1].x + Ib.y*f.wb[1].y*f.wb[1].y + Ib.z*f.wb[1].z*f.wb[1].z);
		std::printf ("U18: wall: solved %zu results, rounds %d, islands %d, resweeps %d, deltas %zu, wall tau %.4f, v after (%.6f %.6f %.6f) (-e u %.6f), dKE %.3f J, KE loss %.3f J\n",
			res.size (), fs.stats.rounds, fs.stats.islands, fs.stats.resweeps, delta.size (), tauWall, f.v[1].x, f.v[1].y, f.v[1].z, -e*u, ev.empty () ? 0.0 : ev[0].dKE, ke0 - ke1);
		REQUIRE (lc.Count ("Collision check") == 0);
		REQUIRE (!staleCore);                                           // the CORE result was replaced by the re-sweep
		REQUIRE (resweptToi);
		REQUIRE (fs.stats.resweeps >= 2);
		REQUIRE (delta.size () == 1);                                    // the RESTING island had no impulse to write back
		REQUIRE (f.v[1].x < -0.9*e*u);                                   // bounced off the wall (7-point manifold, not the 1-D closed form)
		REQUIRE (f.v[1].x > -1.1*e*u);
		REQUIRE (ev.size () == 1);                                       // dKE once: the kinetic energy lost at the wall
		REQUIRE (std::fabs (ev[0].dKE - (ke0 - ke1)) < 1e-6*ke0);
		const CollMotion &m1 = det.Body (1).m;
		REQUIRE (m1.ta == tauWall);
		REQUIRE ((m1.v0 - (m0.Vel (tauWall) + delta[0].d.dv)).length () < 1e-6); // start velocity = Vel (tau) + dP1/m (no phase-2 impulse here)
		REQUIRE (std::memcmp (m1.c1.data, f.x[1].data, sizeof (f.x[1].data)) == 0);
		REQUIRE (std::memcmp (m1.v1.data, f.v[1].data, sizeof (f.v[1].data)) == 0);
		REQUIRE (std::memcmp (m1.q1.data, f.q[1].data, sizeof (f.q[1].data)) == 0);
	}
}

TEST_CASE ("U19 Y3' energy: FIRST door points and a payload resting on the floor in one event", "[CollSolveFrame]")
{
	const double h = 1.0/60.0;
	struct Case { uint8_t kind; double vdoor; bool first; bool emitted; uint32_t flags; };
	const Case cases[] = {
		{ COLL_RESTING, 0.2, true, true, COLLEV_FIRST | COLLEV_RESTING },
		{ COLL_RESTING, 0.05, true, true, COLLEV_FIRST | COLLEV_RESTING | COLLEV_SLOW },
		{ COLL_RESTING, 0.05, false, false, 0 },
		{ COLL_RESTING, 0.2, false, true, COLLEV_RESTING },
		{ COLL_TOI, 0.2, true, true, COLLEV_FIRST },
	};
	for (const Case &cs : cases) {
		double tau = cs.kind == COLL_RESTING ? 0.0 : 0.4, tr = (1 - tau)*h;
		const double fg = 0.0, dg = 0.0;                                  // floor and door gaps: touching (a positive gap may close, fix1 R4)
		Vector at (0, 1.1 + 2*COLL_SKIN_DEFAULT + fg, 0), vb;
		Scene s (h, vb, at + vb*tr, true);
		// carrier A: floor part 0 below payload, door part 1 closing on its side at vdoor (surface v)
		CollPairResult r {};
		r.kind = (CollKind)cs.kind; r.bodyA = 0; r.bodyB = 1; r.tau = tau; r.origin = Vector ();
		r.b.c = at;
		for (double x : { -1.0, 1.0 })
			for (double z : { -1.0, 1.0 }) {
				CollContact &c = r.pt[r.npt++];
				c.n = Vector (0, 1, 0)*-1.0; c.pB = Vector (x, 0.1 + 2*COLL_SKIN_DEFAULT + fg, z); c.pA = c.pB + c.n*(fg + 2*COLL_SKIN_DEFAULT);
				c.gap = fg; c.partA = 0; c.partB = 0; c.flags = 0;
			}
		for (double y : { -0.5, 0.5 }) {
			CollContact &c = r.pt[r.npt++];
			c.n = Vector (1, 0, 0); c.pB = Vector (1, at.y + y, 0); c.pA = c.pB + c.n*(dg + 2*COLL_SKIN_DEFAULT);
			c.gap = dg; c.partA = 1; c.partB = 0; c.flags = cs.first ? COLLP_FIRST : 0;
			c.vsA = Vector (-cs.vdoor, 0, 0);                              // door surface moving onto the payload
		}
		std::vector<CollPairResult> res { r }, one { r };
		CollIsland m = Mirror (s.det, one, s.fb, s.host, h, CollSolveParams ());
		double first = 0, firstWf = 0, all = 0;
		for (const CollSContact &c : m.con) {
			all -= c.Wn + c.Wt;
			if (c.flags & COLLP_FIRST) { first -= c.Wn + c.Wt; firstWf -= c.Wt; }
		}
		CollFrameSolver fs;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		fs.Run (s.det, res, s.fb, h, 5.0, CollSolveParams (), COLL_TOI_ROUNDS, s.host, delta, ev);
		std::printf ("U19: kind %d door %.2f m/s FIRST %d: events %zu, flags 0x%x, dKE %.6f J (FIRST points %.6f, all points %.6f), Wf %.6f\n",
			cs.kind, cs.vdoor, (int)cs.first, ev.size (), ev.empty () ? 0u : ev[0].flags, ev.empty () ? 0.0 : ev[0].dKE, first, all, ev.empty () ? 0.0 : ev[0].Wf);
		REQUIRE (ev.size () == (cs.emitted ? 1u : 0u));
		if (!cs.emitted) continue;
		const CollImpactEvent &e = ev[0];
		REQUIRE ((e.flags & (COLLEV_FIRST | COLLEV_RESTING | COLLEV_SLOW)) == cs.flags);
		REQUIRE ((e.flags & COLLEV_SURFVEL));
		if (cs.first) {
			REQUIRE (std::fabs (e.dKE - first) <= 1e-9*std::fabs (first) + 1e-12);
			REQUIRE (std::fabs (e.Wf - firstWf) <= 1e-9*std::fabs (first) + 1e-12);
			REQUIRE (e.dKE > 0.0);
		} else {
			REQUIRE (e.dKE == 0.0);
			REQUIRE (e.Wf == 0.0);
		}
		REQUIRE (e.s[0].owner.vesselId == 1); REQUIRE (e.s[1].owner.vesselId == 2);
		REQUIRE (e.t == 5.0 + tau*h);
		REQUIRE (e.s[0].mesh == 1);                                     // largest phase-1 impulse: a door point
	}
}

TEST_CASE ("U20 adapter names: D2 structs in, CollSContact out (3.2)", "[CollSolveFrame]")
{
	// kinematic A moving and turning (planet-fixed), dynamic box B with an animated part (surface v)
	Geo plate (BoxMesh (Vector (5, 0.1, 5))), box (BoxMesh (Vector (1, 1, 1)));
	const double h = 0.05, tau = 0.3, tr = (1 - tau)*h, X0 = 1.5e11;
	CollDetect det;
	det.Begin (CollParams (), h);
	const Vector W (0, 7.2921159e-5, 0), VA (465.0, 3.0, -2.0);
	CollBody A = MakeBody (Vector (X0 - 5000, 0, 0), VA, h, 7, COLLB_FROZEN, { MakePart (&plate.geom, 7, 1) }, W);
	CollPartRef bp = MakePart (&box.geom, 3, 5, 2);
	bp.P1.A = IMatrix (); bp.P1.t = Vector (0, 0, 0.01);                // the box part slides 1 cm over the step
	CollBody B = MakeBody (Vector (X0 + 5000, 0, 0), VA + Vector (0.3, -2.0, 0.1), h, 3, COLLB_DYNAMIC, { bp }, Vector (0.02, -0.03, 0.05));
	REQUIRE (det.AddBody (A) == 0);
	REQUIRE (det.AddBody (B) == 1);
	std::vector<CollFrameBody> fb { MakeFB (A, false, 1, Vector (1, 1, 1)), MakeFB (B, true, 1500, Vector (0.8, 1.1, 0.6)) };
	Host host; host.det = &det;
	host.ma = CollSMat { 0.5, 2.0, 0.8 }; host.mb = CollSMat { 0.2, 0.5, 0.3 };
	// the result claims B above A at tau; frame state of B consistent with it
	CollPairResult r {};
	r.kind = COLL_TOI; r.bodyA = 0; r.bodyB = 1; r.tau = tau; r.origin = Vector (X0, 0, 0);
	r.a.c = Vector (); r.a.v = VA; r.a.w = W;
	Quaternion qa; CollRotate (qa, W*(tau*h)); r.a.q.Set (qa);
	r.b.c = Vector (0.2, 1.15, -0.1); r.b.v = VA + Vector (0.3, -2.0, 0.1); r.b.w = Vector (0.02, -0.03, 0.05);
	Quaternion qb; CollRotate (qb, r.b.w*(tau*h)); r.b.q.Set (qb);
	fb[1].x1 = r.origin + r.b.c + r.b.v*tr; fb[1].v1 = r.b.v + Vector (0, -9.81*tr, 0);
	const uint8_t fl[4] = { COLLP_FIRST, COLLP_FIRST | COLLP_DEGENERATE, 0, COLLP_FIRST };
	int k = 0;
	for (double x : { -0.9, 0.9 })
		for (double z : { -0.8, 0.8 }) {
			CollContact &c = r.pt[r.npt++];
			c.n = Vector (0.05, -1, 0.02).unit ();
			c.pB = r.b.c + Vector (x, -1.0, z); c.pA = c.pB + c.n*(0.004 + 2*COLL_SKIN_DEFAULT);
			c.gap = 0.004 - 0.002*k; c.flags = fl[k]; c.partA = 0; c.partB = 0; c.triA = 3; c.triB = 7;
			c.vsB = Vector (0, 0, 0.2);                                // part motion at tau
			k++;
		}
	std::vector<CollPairResult> one { r };
	CollIsland m = Mirror (det, one, fb, host, h, CollSolveParams ());
	// the mapped fields themselves
	const CollSBody &sa = m.body[1], &sb = m.body[0];
	Vector O = fb[1].x1;
	REQUIRE (m.body[0].dyn); REQUIRE (!m.body[1].dyn);
	REQUIRE (Close (sb.xt, (r.origin - O) + r.b.c, 1e-15)); REQUIRE (Close (sa.xt, (r.origin - O) + r.a.c, 1e-15));
	REQUIRE (m.con[0].a == 1); REQUIRE (m.con[0].b == 0);
	REQUIRE (Close (m.con[0].p, (r.origin - O) + (r.pt[0].pA + r.pt[0].pB)*0.5, 1e-15));
	REQUIRE (Close (m.con[0].vkb_1, det.SurfaceVel (r, 0, 1, 1.0), 1e-15));
	REQUIRE (det.SurfaceVel (r, 0, 1, 1.0).length () > 0.1);
	REQUIRE (m.con[0].e0 == 0.5); REQUIRE (m.con[0].vy == 0.5); REQUIRE (std::fabs (m.con[0].mu - std::sqrt (0.24)) < 1e-15);
	CollFrameSolver fs;
	fs.check = true;
	std::vector<CollBodyDelta> delta;
	std::vector<CollImpactEvent> ev;
	LogCapture lc;
	std::vector<CollPairResult> res { r };
	fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
	REQUIRE (lc.Count ("Collision check") == 0);
	const CollBodyDelta *d = DeltaOf (delta, 1);
	REQUIRE (d);
	CollDelta md;
	m.Delta (0, md);
	std::printf ("U20: dv adapter (%.9f %.9f %.9f) mirror (%.9f %.9f %.9f); dth (%.3e %.3e %.3e)\n", d->d.dv.x, d->d.dv.y, d->d.dv.z, md.dv.x, md.dv.y, md.dv.z, d->d.dth.x, d->d.dth.y, d->d.dth.z);
	REQUIRE (Close (d->d.dv, md.dv, 1e-12)); REQUIRE (Close (d->d.dx, md.dx, 1e-12));
	REQUIRE (Close (d->d.dLw, md.dLw, 1e-12)); REQUIRE (Close (d->d.dth, md.dth, 1e-12));
	REQUIRE (ev.size () == 1);
	const CollImpactEvent &e = ev[0];
	REQUIRE ((e.flags & COLLEV_DEGENERATE)); REQUIRE ((e.flags & COLLEV_SURFVEL)); REQUIRE ((e.flags & COLLEV_FIRST));
	REQUIRE (e.s[0].owner.vesselId == 7); REQUIRE (e.s[1].owner.vesselId == 3);
	REQUIRE (e.s[1].mesh == 2); REQUIRE (e.s[0].tri == 3); REQUIRE (e.s[1].tri == 7);
	double first = 0;
	for (const CollSContact &c : m.con) if (c.flags & COLLP_FIRST) first -= c.Wn + c.Wt;
	REQUIRE (std::fabs (e.dKE - first) <= 1e-9*std::fabs (first));
	REQUIRE (e.meff > 0.0); REQUIRE (e.meff < 1500.0);
	REQUIRE (std::fabs (e.s[1].n.length () - 1.0) < 1e-12);
}

TEST_CASE ("LANDED wake by the kinematic impulse share (6.6)", "[CollSolveFrame]")
{
	Geo hull (BoxMesh (Vector (1, 1, 1)));
	for (double u : { 2.0, 0.02 }) {
		const double h = 0.05;
		CollDetect det;
		det.Begin (CollParams (), h);
		CollBody A = MakeBody (Vector (), Vector (), h, 1, COLLB_LANDED, { MakePart (&hull.geom, 1, 1) });
		CollBody B = MakeBody (Vector (u > 1.0 ? 2.12 : 2.04, 0, 0), Vector (-u, 0, 0), h, 2, COLLB_DYNAMIC, { MakePart (&hull.geom, 2, 1) }); // slow one touching (fix1 R4)
		REQUIRE (det.AddBody (A) == 0);
		REQUIRE (det.AddBody (B) == 1);
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		det.Detect (res, st);
		REQUIRE (!res.empty ());
		std::vector<CollFrameBody> fb { MakeFB (A, false, 2000, Vector (1, 1, 1)), MakeFB (B, true, 1000, Vector (1, 1, 1)) };
		fb[0].wakeable = true; fb[0].wakeV1 = Vector (0.1, 0, 0); fb[0].wakeWb1 = Vector ();  // takeoff state (planet field)
		Host host; host.det = &det;
		CollFrameSolver fs;
		fs.check = true;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		LogCapture lc;
		int kind = res[0].kind;
		fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		Fin f (fb, delta);
		std::printf ("LANDED wake: approach %.2f m/s (kind %d): woke %d, deltas %zu, v landed %.6f, v other %.6f\n", u, kind, (int)fb[0].woke, delta.size (), f.v[0].x, f.v[1].x);
		REQUIRE (lc.Count ("Collision check") == 0);
		if (u > 1.0) {
			REQUIRE (fb[0].woke);
			REQUIRE (DeltaOf (delta, 0));
			REQUIRE ((ev.size () == 1 && (ev[0].flags & COLLEV_WOKE_LANDED)));
			Vector P0 = fb[0].wakeV1*2000.0 + fb[1].v1*1000.0, P1 = f.v[0]*2000.0 + f.v[1]*1000.0; // against the takeoff pre-state
			REQUIRE ((P1 - P0).length () <= 1e-12*P0.length ());
			REQUIRE (f.v[1].x - f.v[0].x > 0.0);                          // separating
			REQUIRE (fb[0].impulsive);
		} else {
			REQUIRE (!fb[0].woke);
			REQUIRE (!DeltaOf (delta, 0));
			REQUIRE (std::fabs (f.v[1].x) < 1e-9);                         // stopped against the kinematic body (e = 0 below vrest)
		}
	}
}

TEST_CASE ("Position correction selection (4.6) and frame outputs: loadRest, contact counts, supports", "[CollSolveFrame]")
{
	Geo ground (BoxMesh (Vector (5, 0.1, 5))), box (BoxMesh (Vector (1, 1, 1)));
	const CollOwnerKey bkey { COLLO_BUILDING, 0, 3, 1, 4, 0 };
	for (uint8_t fl : { (uint8_t)0, (uint8_t)COLLP_FIRST, (uint8_t)(COLLP_FIRST | COLLP_SUPPORT) }) {
		const double h = 0.1, gap = -0.01;
		CollDetect det;
		det.Begin (CollParams (), h);
		CollPartRef gp = MakePart (&ground.geom, 0, 0);
		gp.owner = bkey;
		CollBody A = MakeBody (Vector (-1000, 0, 0), Vector (), h, 9, COLLB_BASE, { gp });
		CollBody B = MakeBody (Vector (1000, 0, 0), Vector (), h, 2, COLLB_DYNAMIC, { MakePart (&box.geom, 2, 1) });
		REQUIRE (det.AddBody (A) == 0);
		REQUIRE (det.AddBody (B) == 1);
		Vector at (0, 1.1 + 2*COLL_SKIN_DEFAULT + gap, 0);
		std::vector<CollFrameBody> fb { MakeFB (A, false, 0, Vector (1, 1, 1)), MakeFB (B, true, 1000, Vector (0.667, 0.667, 0.667)) };
		fb[1].x1 = at + Vector (0, -0.5*9.81*h*h, 0); fb[1].v1 = Vector (0, -9.81*h, 0);  // free fall over the step
		CollPairResult r {};
		r.kind = COLL_RESTING; r.bodyA = 0; r.bodyB = 1; r.tau = 0; r.origin = Vector ();
		r.b.c = at;
		for (double x : { -1.0, 1.0 })
			for (double z : { -1.0, 1.0 }) {
				CollContact &c = r.pt[r.npt++];
				c.n = Vector (0, -1, 0); c.pB = Vector (x, 0.1 + 2*COLL_SKIN_DEFAULT + gap, z); c.pA = c.pB + c.n*(gap + 2*COLL_SKIN_DEFAULT);
				c.gap = gap; c.flags = fl;
			}
		std::vector<CollPairResult> res { r };
		Host host; host.det = &det;
		CollFrameSolver fs;
		fs.check = true;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		LogCapture lc;
		fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		const CollBodyDelta *pc = DeltaOf (delta, 1, true);
		std::printf ("Correction: flags 0x%x: solve deltas %d, correction dx.y %.6f m, loadRest %d, building contacts %d, supports %zu, events %zu\n",
			fl, DeltaOf (delta, 1) ? 1 : 0, pc ? pc->d.dx.y : 0.0, (int)fb[1].loadRest, fb[1].nBuildingContacts, fb[1].sup.size (), ev.size ());
		REQUIRE (lc.Count ("Collision check") == 0);
		REQUIRE (fb[1].loadRest);
		REQUIRE (fb[1].allSlow);
		REQUIRE (fb[1].nBuildingContacts == 4); REQUIRE (fb[1].nVesselContacts == 0);
		REQUIRE (fb[1].sup.size () == 1);
		REQUIRE (SameBits4 (fb[1].sup[0].building, bkey));
		REQUIRE ((fb[1].sup[0].n - Vector (0, 1, 0)).length () < 1e-12);
		if (fl == COLLP_FIRST) REQUIRE (!pc);                           // a new overlap is never pushed out (GRACE's job)
		else {
			REQUIRE (pc);
			REQUIRE (std::fabs (pc->d.dx.y - 0.2*(-gap - COLL_SLOP)) < 1e-9);
			REQUIRE (pc->d.dv.length () == 0.0);
		}
		REQUIRE (ev.size () == (fl & COLLP_FIRST ? 1u : 0u));             // FIRST or not SLOW
		if (!ev.empty ()) REQUIRE ((ev[0].flags & COLLEV_POSCORR) == (fl & COLLP_SUPPORT ? COLLEV_POSCORR : 0u));
	}
}

TEST_CASE ("Two islands in one round keep tau order: a re-swept body does not pass through a thin plate (D2 6.4 item 4)", "[CollSolveFrame]")
{
	// frozen wall X (x = 0); octahedron H: X at tau 0.04, plate F (x = 1) at 0.22; cube G: F at 0.35
	CollGroupData wg, pg;
	Quad (wg, Vector (0, -3, -3), Vector (0, 3, -3), Vector (0, 3, 3), Vector (0, -3, 3));
	Quad (pg, Vector (0, -1, -1), Vector (0, 1, -1), Vector (0, 1, 1), Vector (0, -1, 1));
	Geo wall (wg), plate (pg), oct (OctaMesh (0.1)), cube (BoxMesh (Vector (0.1, 0.1, 0.1)));
	const double h = 0.1;
	for (bool withG : { false, true }) {
		CollDetect det;
		det.Begin (CollParams (), h);
		CollBody X = MakeBody (Vector (), Vector (), h, 1, COLLB_FROZEN, { MakePart (&wall.geom, 1, 1) });
		CollBody H = MakeBody (Vector (0.3, -0.5, 0), Vector (-40, 0, 0), h, 2, COLLB_DYNAMIC, { MakePart (&oct.geom, 2, 1) });
		CollBody F = MakeBody (Vector (1.0, 0, 0), Vector (), h, 3, COLLB_DYNAMIC, { MakePart (&plate.geom, 3, 1) });
		CollBody G = MakeBody (withG ? Vector (1.0 + 0.045 + 0.1 + 20*0.035, 0.5, 0) : Vector (50, 0.5, 0), Vector (withG ? -20.0 : 0.0, 0, 0), h, 4, COLLB_DYNAMIC, { MakePart (&cube.geom, 4, 1) });
		REQUIRE (det.AddBody (X) == 0); REQUIRE (det.AddBody (H) == 1); REQUIRE (det.AddBody (F) == 2); REQUIRE (det.AddBody (G) == 3);
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		det.Detect (res, st);
		std::vector<CollFrameBody> fb { MakeFB (X, false, 10, Vector (0.1, 0.1, 0.1)), MakeFB (H, true, 10, Vector (0.1, 0.1, 0.1)), MakeFB (F, true, 10, Vector (100, 100, 100)), MakeFB (G, true, 10, Vector (0.1, 0.1, 0.1)) };
		Host host; host.det = &det;
		host.ma = host.mb = CollSMat { 1.0, 1e9, 0.0 };
		CollFrameSolver fs;
		fs.check = true;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		LogCapture lc;
		fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		Fin f (fb, delta);
		double tauHF = -1;
		for (const CollPairResult &r : res) if (r.bodyA == 1 && r.bodyB == 2 && tauHF < 0) tauHF = r.tau;
		std::printf ("Tau order (G %d): rounds %d, islands %d, resweeps %d, H-F solved at tau %.4f; t1: H x %.3f v %.2f, F x %.3f v %.2f\n",
			(int)withG, fs.stats.rounds, fs.stats.islands, fs.stats.resweeps, tauHF, f.x[1].x, f.v[1].x, f.x[2].x, f.v[2].x);
		REQUIRE (lc.Count ("Collision check") == 0);
		REQUIRE (tauHF > 0.15);                                         // the re-swept H-F hit is solved
		REQUIRE (tauHF < 0.3);
		REQUIRE (f.x[1].x < f.x[2].x);                                  // H stays on its side of F
	}
}

TEST_CASE ("LANDED body woken by an impact from above is re-swept against its roof (6.6, D2 6.4)", "[CollSolveFrame]")
{
	Geo roof (BoxMesh (Vector (5, 0.5, 5))), box (BoxMesh (Vector (1, 1, 1)));
	const double h = 0.1;
	CollDetect det;
	det.Begin (CollParams (), h);
	CollPartRef rp = MakePart (&roof.geom, 0, 0);
	rp.owner = CollOwnerKey { COLLO_BUILDING, 0, 0, 0, 0, 0 };
	CollBody R = MakeBody (Vector (0, -0.5, 0), Vector (), h, 1, COLLB_BASE, { rp });
	R.planet = 0;
	CollBody L = MakeBody (Vector (0, 1.045, 0), Vector (), h, 2, COLLB_LANDED, { MakePart (&box.geom, 2, 1) });
	L.planet = 0;
	CollBody D = MakeBody (Vector (0, 3.2, 0), Vector (0, -10, 0), h, 3, COLLB_DYNAMIC, { MakePart (&box.geom, 3, 1) });
	REQUIRE (det.AddBody (R) == 0); REQUIRE (det.AddBody (L) == 1); REQUIRE (det.AddBody (D) == 2);
	std::vector<CollPairResult> res;
	CollFrameStats st {};
	det.Detect (res, st);
	std::vector<CollFrameBody> fb { MakeFB (R, false, 1, Vector (1, 1, 1)), MakeFB (L, false, 1000, Vector (0.667, 0.667, 0.667)), MakeFB (D, true, 1000, Vector (0.667, 0.667, 0.667)) };
	fb[1].wakeable = true; fb[1].wakeV1 = Vector (); fb[1].wakeWb1 = Vector ();
	Host host; host.det = &det;
	CollFrameSolver fs;
	fs.check = true;
	std::vector<CollBodyDelta> delta;
	std::vector<CollImpactEvent> ev;
	LogCapture lc;
	fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
	Fin f (fb, delta);
	bool roofHit = false;
	for (const CollPairResult &r : res) roofHit = roofHit || (r.bodyA == 0 && r.bodyB == 1 && (r.flags & COLLF_RESWEEP));
	std::printf ("Wake on a roof: woke %d, rounds %d, resweeps %d, roof result solved %d; landed body at t1: y %.4f (resting at 1.04) vy %.3f; dropped body y %.4f\n",
		(int)fb[1].woke, fs.stats.rounds, fs.stats.resweeps, (int)roofHit, f.x[1].y, f.v[1].y, f.x[2].y);
	REQUIRE (lc.Count ("Collision check") == 0);
	REQUIRE (fb[1].woke);
	REQUIRE (det.Body (1).kind == COLLB_DYNAMIC);                       // dynamic for the rest of the frame
	REQUIRE (roofHit);
	REQUIRE (f.x[1].y > 1.0);                                           // raw separation from the roof kept (the round cap may leave skin overlap, 5.4)
	REQUIRE (f.x[2].y > f.x[1].y + 2.0);                                // and from the dropped body
}

TEST_CASE ("Position correction is built at t1: a spinning body is pushed out about the right world axis (4.6)", "[CollSolveFrame]")
{
	Geo ground (BoxMesh (Vector (5, 0.1, 5))), box (BoxMesh (Vector (1, 1, 1)));
	const double h = 0.1, gap = -0.01;
	for (double wz : { 0.0, -3.0 }) {
		const Vector wg (0, 0, wz);
		CollDetect det;
		det.Begin (CollParams (), h);
		CollBody A = MakeBody (Vector (-1000, 0, 0), Vector (), h, 9, COLLB_FROZEN, { MakePart (&ground.geom, 9, 1) });
		CollBody B = MakeBody (Vector (1000, 0, 0), Vector (), h, 2, COLLB_DYNAMIC, { MakePart (&box.geom, 2, 1) }, wg);
		REQUIRE (det.AddBody (A) == 0);
		REQUIRE (det.AddBody (B) == 1);
		Vector at (0, 1.1 + 2*COLL_SKIN_DEFAULT + gap, 0);
		std::vector<CollFrameBody> fb { MakeFB (A, false, 1, Vector (1, 1, 1)), MakeFB (B, true, 1000, Vector (0.667, 0.667, 0.667)) };
		fb[1].x1 = at;
		// one non-FIRST corner below -slop, separating by the spin (no impulse): only the correction acts
		CollPairResult r {};
		r.kind = COLL_RESTING; r.bodyA = 0; r.bodyB = 1; r.tau = 0; r.origin = Vector ();
		r.b.c = at; r.b.w = wg;
		CollContact &c = r.pt[r.npt++];
		c.n = Vector (0, -1, 0); c.pB = at + Vector (1, -1, 1); c.pA = c.pB + c.n*(gap + 2*COLL_SKIN_DEFAULT);
		c.gap = gap; c.flags = 0;
		std::vector<CollPairResult> res { r };
		Host host; host.det = &det;
		CollFrameSolver fs;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		REQUIRE (!DeltaOf (delta, 1));
		const CollBodyDelta *pc = DeltaOf (delta, 1, true);
		REQUIRE (pc);
		// design's island at t1: bodies at t1, point carried by each side and averaged, n by mean turn
		Matrix R1 = QMatrix (fb[1].q1);
		Vector mid = (c.pA + c.pB)*0.5, p1 = ((mid - fb[1].x1) + mul (R1, mid - at))*0.5, n1 = (c.n + mul (R1, c.n)).unit ();
		CollIsland ci;
		ci.tau = 1.0; ci.h = h;
		CollSBody sb {};
		sb.dyn = true; sb.m = 1000; sb.pmi = fb[1].pmi; sb.Rt = sb.R1 = R1;
		ci.body.push_back (sb);
		CollSBody sk {};
		sk.dyn = false; sk.m = 1; sk.pmi = Vector (1, 1, 1); sk.Rt = sk.R1 = IMatrix (); sk.xt = sk.x1 = -fb[1].x1;
		ci.body.push_back (sk);
		CollSContact k {};
		k.a = 1; k.b = 0; k.p = p1; k.n = k.n2 = n1; k.gap = gap; k.kind = COLL_RESTING;
		ci.con.push_back (k);
		std::vector<CollDelta> md;
		REQUIRE (ci.Correct (CollSolveParams (), md));
		Vector axis = mul (R1, pc->d.dth);                                   // world axis of the applied correction turn
		std::printf ("Correction at t1, spin %.1f rad/s: dth (%.3e %.3e %.3e), world axis . n1 %.2e, mirror dth (%.3e %.3e %.3e)\n",
			wz, pc->d.dth.x, pc->d.dth.y, pc->d.dth.z, dotp (axis, n1)/axis.length (), md[0].dth.x, md[0].dth.y, md[0].dth.z);
		REQUIRE (Close (pc->d.dx, md[0].dx, 1e-12));
		REQUIRE (Close (pc->d.dth, md[0].dth, 1e-12));
		REQUIRE (std::fabs (dotp (axis, n1)) <= 1e-12*axis.length ());    // isotropic body: turned about an axis normal to the pushing direction
	}
}

TEST_CASE ("INACCURATE results: one warning per owner pair per minute of sim time (3.2)", "[CollSolveFrame]")
{
	const double h = 0.1, tau = 0.5, tr = (1 - tau)*h;
	CollFrameSolver fs;
	LogCapture lc;
	int lines[3];
	const double t0[3] = { 0.0, 1.0, 61.0 };
	for (int k = 0; k < 3; k++) {
		Vector at (0, 1.1 + 2*COLL_SKIN_DEFAULT, 0), vb (0, -3.0, 0);
		Scene s (h, vb, at + vb*tr);
		std::vector<Vector> pts { Vector (-1, 0.1 + 2*COLL_SKIN_DEFAULT, -1), Vector (1, 0.1 + 2*COLL_SKIN_DEFAULT, 1) };
		std::vector<CollPairResult> res { BoxResult (COLL_TOI, tau, at, vb, pts, { -0.002, -0.002 }, { COLLP_FIRST, COLLP_FIRST }) };
		res[0].flags = COLLF_INACCURATE;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		fs.Run (s.det, res, s.fb, h, t0[k], CollSolveParams (), COLL_TOI_ROUNDS, s.host, delta, ev);
		REQUIRE (ev.size () == 1);
		REQUIRE ((ev[0].flags & COLLEV_INACCURATE));
		lines[k] = lc.Count ("2 Collision: INACCURATE");
	}
	std::printf ("INACCURATE warnings after runs at t = 0, 1, 61 s: %d, %d, %d\n", lines[0], lines[1], lines[2]);
	REQUIRE (lines[0] == 1);
	REQUIRE (lines[1] == 1);
	REQUIRE (lines[2] == 2);
}

TEST_CASE ("D2 U24 through the driver: a result replaced by a re-sweep marks nothing (3.6)", "[CollSolveFrame]")
{
	// ball B heads for wall W (tau 0.55); ball D hits B first (tau 0.03); B's re-sweep drops the wall
	CollGroupData wg;
	Quad (wg, Vector (0, -3, -3), Vector (0, 3, -3), Vector (0, 3, 3), Vector (0, -3, 3));
	Geo wall (wg), ball (SphereMesh (0.3, 12, 16));
	const double h = 0.1;
	CollDetect det;
	det.Begin (CollParams (), h);
	CollBody B = MakeBody (Vector (), Vector (30, 0, 0), h, 1, COLLB_DYNAMIC, { MakePart (&ball.geom, 1, 1) });
	CollBody D = MakeBody (Vector (0.9, 0, 0), Vector (-60, 0, 0), h, 2, COLLB_DYNAMIC, { MakePart (&ball.geom, 2, 1) });
	CollBody W = MakeBody (Vector (2.0, 0, 0), Vector (), h, 3, COLLB_FROZEN, { MakePart (&wall.geom, 3, 1) });
	REQUIRE (det.AddBody (B) == 0); REQUIRE (det.AddBody (D) == 1); REQUIRE (det.AddBody (W) == 2);
	std::vector<CollPairResult> res;
	CollFrameStats st {};
	det.Detect (res, st);
	bool wallFound = false;
	for (const CollPairResult &r : res) wallFound = wallFound || (r.bodyA == 0 && r.bodyB == 2);
	REQUIRE (wallFound);
	std::vector<CollFrameBody> fb { MakeFB (B, true, 100, Vector (0.036, 0.036, 0.036)), MakeFB (D, true, 100, Vector (0.036, 0.036, 0.036)), MakeFB (W, false, 1, Vector (1, 1, 1)) };
	Host host; host.det = &det;
	CollFrameSolver fs;
	fs.check = true;
	std::vector<CollBodyDelta> delta;
	std::vector<CollImpactEvent> ev;
	LogCapture lc;
	fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
	Fin f (fb, delta);
	bool wallSolved = false;
	for (const CollPairResult &r : res) wallSolved = wallSolved || (r.bodyA == 0 && r.bodyB == 2);
	const CollPairEntry *e = det.Pairs ().Find (CollOwnerKey { COLLO_VESSEL, 1, -1, -1, -1, -1 }, CollOwnerKey { COLLO_VESSEL, 3, -1, -1, -1, -1 });
	bool marked = false;
	if (e) for (const CollTouch &t : e->touch) marked = marked || t.solved || t.state == COLLT_TOUCHING;
	std::printf ("U24 driver: B after %.2f m/s, wall result solved %d, wall pair marked %d, events %zu\n", f.v[0].x, (int)wallSolved, (int)marked, ev.size ());
	REQUIRE (lc.Count ("Collision check") == 0);
	REQUIRE (f.v[0].x < 0.0);
	REQUIRE (!wallSolved);
	REQUIRE (!marked);
	REQUIRE (ev.size () == 1);
}

TEST_CASE ("fix1 n2: a near half turn between tau and t1 keeps the contact normal (|n2| < 0.5 falls back to n)", "[CollSolveFrame]")
{
	const double h = 0.1;
	Vector at (0, 1.1 + 2*COLL_SKIN_DEFAULT, 0);
	for (double deg : { 30.0, 170.0 }) {
		Scene s (h, Vector (), at);
		Quaternion q1;
		CollRotate (q1, Vector (0, 0, deg*3.14159265358979323846/180.0));    // box turned about z by t1, n = -y lies across the axis
		s.fb[1].q1.Set (q1); s.fb[1].v1 = Vector (0, -1, 0);
		std::vector<Vector> pts;
		for (double x : { -1.0, 1.0 })
			for (double z : { -1.0, 1.0 }) pts.push_back (Vector (x, 0.1 + 2*COLL_SKIN_DEFAULT, z));
		std::vector<CollPairResult> res { BoxResult (COLL_RESTING, 0.0, at, Vector (), pts, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }) };
		CollFrameSolver fs;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		fs.Run (s.det, res, s.fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, s.host, delta, ev);
		Fin f (s.fb, delta);
		std::printf ("fix1 n2: box turned %.0f deg by t1, pressing at 1 m/s: velocity after (%.3e %.3e %.3e) m/s\n", deg, f.v[1].x, f.v[1].y, f.v[1].z);
		if (deg > 150.0) {
			REQUIRE (std::fabs (f.v[1].x) < 1e-6);                            // pushed along the plate normal, not sideways
			REQUIRE (std::fabs (f.v[1].y) < 1e-6);
		} else REQUIRE (std::fabs (f.v[1].y) < 0.5);                          // a moderate turn keeps the rotated mean normal
	}
}

TEST_CASE ("dmg3 L1/L4 through the driver: glancing spheres give the slip direction per side and one contact with the total impulse", "[CollSolveFrame]")
{
	Geo sph (SphereMesh (1.0, 12, 16));
	const double h = 1.0/64.0, d0 = 1.0390625;
	for (double vy : { 0.0, 1.0 }) {
		CollDetect det;
		det.Begin (CollParams (), h);
		CollBody A = MakeBody (Vector (-d0, 0, 0), Vector (2, vy, 0), h, 1, COLLB_DYNAMIC, { MakePart (&sph.geom, 1, 1) });
		CollBody B = MakeBody (Vector (d0, 0, 0), Vector (-2, 0, 0), h, 2, COLLB_DYNAMIC, { MakePart (&sph.geom, 2, 1) });
		REQUIRE (det.AddBody (A) == 0);
		REQUIRE (det.AddBody (B) == 1);
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		det.Detect (res, st);
		REQUIRE (res.size () == 1);
		std::vector<CollFrameBody> fb { MakeFB (A, true, 500, Vector (2.28, 2.31, 0.79)), MakeFB (B, true, 500, Vector (2.28, 2.31, 0.79)) };
		Host host; host.det = &det;
		CollFrameSolver fs;
		std::vector<CollContactRec> con;
		fs.contacts = &con;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		fs.Run (det, res, fb, h, 100.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		REQUIRE (ev.size () == 1);
		REQUIRE (con.size () == 1);
		const CollContactRec &c = con[0];
		CAPTURE (vy, ev[0].Jn, ev[0].vt, c.Jn, c.Jt, c.vt, c.vn, ev[0].s[0].tdir.y, c.s[0].tdir.y);
		REQUIRE (c.dt == h);
		REQUIRE (c.Jn >= ev[0].Jn*(1.0 - 1e-9));
		REQUIRE (std::fabs (c.vn - ev[0].vn) <= 0.01*ev[0].vn);
		REQUIRE (c.s[0].owner.vesselId == ev[0].s[0].owner.vesselId);
		REQUIRE ((c.s[0].c - ev[0].s[0].c).length () <= 1e-6);
		if (vy == 0.0) {
			for (int s = 0; s < 2; s++) { REQUIRE (ev[0].s[s].tdir.length () == 0.0); REQUIRE (c.s[s].tdir.length () == 0.0); }
			continue;
		}
		REQUIRE (std::fabs (c.vt - ev[0].vt) <= 1e-9);                 // facetted sphere: the normal tilts, slip above vy
		REQUIRE (c.vt > 0.5*vy);
		REQUIRE (c.Jt > 0.0);
		REQUIRE (ev[0].s[0].tdir.y > 0.95);                           // A slides +y over B
		REQUIRE (ev[0].s[1].tdir.y < -0.95);
		REQUIRE (c.s[0].tdir.y > 0.95);
		REQUIRE (c.s[1].tdir.y < -0.95);
		REQUIRE (std::fabs (dotp (ev[0].s[0].tdir, ev[0].s[0].n)) <= 1e-9);
	}
}

TEST_CASE ("P3 a wake is committed only with a solved pass 1: a non-finite wake state drops the island and leaves the body LANDED", "[CollSolveFrame]")
{
	Geo hull (BoxMesh (Vector (1, 1, 1)));
	const double h = 0.05;
	for (bool bad : { false, true }) {
		CollDetect det;
		det.Begin (CollParams (), h);
		CollBody A = MakeBody (Vector (), Vector (), h, 1, COLLB_LANDED, { MakePart (&hull.geom, 1, 1) });
		CollBody B = MakeBody (Vector (2.12, 0, 0), Vector (-2, 0, 0), h, 2, COLLB_DYNAMIC, { MakePart (&hull.geom, 2, 1) });
		REQUIRE (det.AddBody (A) == 0);
		REQUIRE (det.AddBody (B) == 1);
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		det.Detect (res, st);
		REQUIRE (!res.empty ());
		std::vector<CollFrameBody> fb { MakeFB (A, false, 2000, Vector (1, 1, 1)), MakeFB (B, true, 1000, Vector (1, 1, 1)) };
		fb[0].wakeable = true; fb[0].wakeV1 = bad ? Vector (std::nan (""), 0, 0) : Vector (0.1, 0, 0); fb[0].wakeWb1 = Vector ();
		Host host; host.det = &det;
		CollFrameSolver fs;
		fs.check = true;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> ev;
		LogCapture lc;
		fs.Run (det, res, fb, h, 0.0, CollSolveParams (), COLL_TOI_ROUNDS, host, delta, ev);
		CAPTURE (bad, (int)fb[0].woke, delta.size (), ev.size (), lc.Count ("dropped"), lc.Count ("woken"));
		if (!bad) {
			REQUIRE (fb[0].woke);
			REQUIRE (DeltaOf (delta, 0));
			REQUIRE (lc.Count ("woken") == 1);
			continue;
		}
		REQUIRE (lc.Count ("dropped") >= 1);
		REQUIRE (!fb[0].woke);
		REQUIRE (!DeltaOf (delta, 0));
		REQUIRE (lc.Count ("woken") == 0);
		for (const CollImpactEvent &e : ev) REQUIRE (!(e.flags & COLLEV_WOKE_LANDED));
		REQUIRE (det.Body (0).kind == COLLB_LANDED);
	}
}
