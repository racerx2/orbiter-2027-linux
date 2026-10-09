// not upstream: unit tests for Src/Orbiter/CollDetect (D2 U1-U22, U24, U25; U23 in CollLoop.Test)
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "CollDetect.h"
#include "CollTestMsh.h"

using Catch::Matchers::WithinAbs;

// helpers

struct Rng {                                   // splitmix64, deterministic
	uint64_t s;
	explicit Rng (uint64_t seed) : s (seed) {}
	uint64_t Next () { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30))*0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27))*0x94D049BB133111EBull; return z ^ (z >> 31); }
	double U () { return (double)(Next () >> 11)*(1.0/9007199254740992.0); }
	double U (double a, double b) { return a + (b - a)*U (); }
	Vector V (double sc) { double x = U (-sc, sc), y = U (-sc, sc), z = U (-sc, sc); return Vector (x, y, z); }
	Vector Dir () { for (;;) { Vector v = V (1.0); double l = v.length (); if (l > 0.1 && l <= 1.0) return v/l; } }
	void Q (Quaternion &q) { for (;;) { double a = U (-1, 1); double b = U (-1, 1); double c = U (-1, 1); double d = U (-1, 1); double n = sqrt (a*a + b*b + c*c + d*d); if (n > 0.1 && n <= 1.0) { q.Set (a/n, b/n, c/n, d/n); return; } } }
};

static void RequireVec (const Vector &a, const Vector &b, double eps)
{
	REQUIRE_THAT (a.x, WithinAbs (b.x, eps));
	REQUIRE_THAT (a.y, WithinAbs (b.y, eps));
	REQUIRE_THAT (a.z, WithinAbs (b.z, eps));
}

static Quaternion TExp (const Vector &phi)
{
	double a = phi.length ();
	if (a < 1e-300) return Quaternion ();
	double s = sin (0.5*a)/a;
	return Quaternion (phi.x*s, phi.y*s, phi.z*s, cos (0.5*a));
}

static Matrix TMat (const Quaternion &q)
{
	Matrix R;
	R.Set (q);
	return R;
}

static Quaternion TRotG (const Quaternion &q, const Vector &psi)
{
	return q * TExp (tmul (TMat (q), psi));
}

static double MatDiff (const Matrix &A, const Matrix &B)
{
	double m = 0.0;
	for (int i = 0; i < 9; i++) m = std::max (m, fabs (A.data[i] - B.data[i]));
	return m;
}

// rotation angle between two attitudes
static double QAngle (const Quaternion &a, const Quaternion &b)
{
	Quaternion r = Quaternion (-a.qvx, -a.qvy, -a.qvz, a.qs) * b;
	return 2.0*atan2 (sqrt (r.qvx*r.qvx + r.qvy*r.qvy + r.qvz*r.qvz), fabs (r.qs));
}

static Vector ClosestTri (const Vector &p, const Vector &a, const Vector &b, const Vector &c)
{
	Vector ab = b - a, ac = c - a, ap = p - a;
	double d1 = ab & ap, d2 = ac & ap;
	if (d1 <= 0 && d2 <= 0) return a;
	Vector bp = p - b;
	double d3 = ab & bp, d4 = ac & bp;
	if (d3 >= 0 && d4 <= d3) return b;
	double vc = d1*d4 - d3*d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab*(d1/(d1 - d3));
	Vector cp = p - c;
	double d5 = ab & cp, d6 = ac & cp;
	if (d6 >= 0 && d5 <= d6) return c;
	double vb = d5*d2 - d1*d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac*(d2/(d2 - d6));
	double va = d3*d6 - d5*d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b)*((d4 - d3)/((d4 - d3) + (d5 - d6)));
	double den = 1.0/(va + vb + vc);
	return a + ab*(vb*den) + ac*(vc*den);
}

// meshes
struct Mesh { std::vector<Vector> v; std::vector<int> t; };

static void Tri (Mesh &m, int a, int b, int c) { m.t.push_back (a); m.t.push_back (b); m.t.push_back (c); }

static Mesh BoxM (const Vector &h, const Vector &c = Vector ())
{
	Mesh m;
	for (int i = 0; i < 8; i++) m.v.push_back (c + Vector (i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z));
	const int f[12][3] = { {0,2,3},{0,3,1},{4,5,7},{4,7,6},{0,1,5},{0,5,4},{2,6,7},{2,7,3},{0,4,6},{0,6,2},{1,3,7},{1,7,5} };
	for (const auto &x : f) Tri (m, x[0], x[1], x[2]);
	return m;
}

static Mesh PlateM (double hx, double hy, const Vector &c = Vector ())
{
	Mesh m;
	m.v = { c + Vector (-hx, -hy, 0), c + Vector (hx, -hy, 0), c + Vector (hx, hy, 0), c + Vector (-hx, hy, 0) };
	Tri (m, 0, 1, 2); Tri (m, 0, 2, 3);
	return m;
}

static Mesh TetraM (double s)
{
	Mesh m;
	m.v = { Vector (s, s, s), Vector (s, -s, -s), Vector (-s, s, -s), Vector (-s, -s, s) };
	Tri (m, 0, 1, 2); Tri (m, 0, 3, 1); Tri (m, 0, 2, 3); Tri (m, 1, 3, 2);
	return m;
}

static Mesh IcoM (int level, double r)
{
	Mesh m;
	double t = (1.0 + sqrt (5.0))*0.5;
	Vector v0[12] = { Vector (-1, t, 0), Vector (1, t, 0), Vector (-1, -t, 0), Vector (1, -t, 0), Vector (0, -1, t), Vector (0, 1, t),
		Vector (0, -1, -t), Vector (0, 1, -t), Vector (t, 0, -1), Vector (t, 0, 1), Vector (-t, 0, -1), Vector (-t, 0, 1) };
	for (const Vector &v : v0) m.v.push_back (v/v.length ());
	const int f[20][3] = { {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
		{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1} };
	for (const auto &x : f) Tri (m, x[0], x[1], x[2]);
	for (int l = 0; l < level; l++) {
		Mesh n;
		n.v = m.v;
		std::vector<std::pair<std::pair<int, int>, int>> mid;
		auto Mid = [&] (int a, int b) {
			std::pair<int, int> k (std::min (a, b), std::max (a, b));
			for (const auto &x : mid) if (x.first == k) return x.second;
			Vector p = (n.v[a] + n.v[b])*0.5;
			n.v.push_back (p/p.length ());
			mid.push_back ({ k, (int)n.v.size () - 1 });
			return (int)n.v.size () - 1;
		};
		for (size_t i = 0; i < m.t.size (); i += 3) {
			int a = m.t[i], b = m.t[i+1], c = m.t[i+2];
			int ab = Mid (a, b), bc = Mid (b, c), ca = Mid (c, a);
			Tri (n, a, ab, ca); Tri (n, b, bc, ab); Tri (n, c, ca, bc); Tri (n, ab, bc, ca);
		}
		m.t.swap (n.t); m.v.swap (n.v);
	}
	for (Vector &v : m.v) v = v*r;
	return m;
}

static Mesh XForm (const Mesh &m, const Matrix &R, const Vector &t)
{
	Mesh o = m;
	for (Vector &v : o.v) v = mul (R, v) + t;
	return o;
}

static void Merge (Mesh &a, const Mesh &b)
{
	int n = (int)a.v.size ();
	a.v.insert (a.v.end (), b.v.begin (), b.v.end ());
	for (int i : b.t) a.t.push_back (i + n);
}

static Matrix RotAxis (const Vector &axis, double ang)
{
	return TMat (TExp (axis.unit ()*ang));
}

static std::deque<CollGeom> g_geoms;

static const CollGeom *Geom (const Mesh &m, double skin = COLL_SKIN_DEFAULT)
{
	CollGroupData gd;
	for (const Vector &v : m.v) gd.vtx.push_back (CollVtx { (float)v.x, (float)v.y, (float)v.z, 0, 0, 0, 0, 0 });
	for (int i : m.t) gd.idx.push_back ((uint16_t)i);
	CollSrcGroup sg { &gd, CollSrc { 0, 0, 0, 0, 0 } };
	g_geoms.emplace_back ();
	CollGeom &G = g_geoms.back ();
	CollBuildStats st;
	REQUIRE (G.Build (&sg, 1, COLL_WELD_DEFAULT, &st));
	G.skin = skin;
	return &G;
}

static CollOwnerKey VKey (uint32_t id) { return CollOwnerKey { COLLO_VESSEL, id, -1, -1, -1, -1 }; }
static CollOwnerKey BKey (int planet, int base, int obj) { return CollOwnerKey { COLLO_BUILDING, 0, planet, base, obj, 0 }; }

static void AddPart (CollBody &b, const CollGeom *g, const CollOwnerKey &o, uint32_t key = 0)
{
	CollPartRef p {};
	p.geom = g; p.skin = g->skin; p.owner = o; p.partKey = key; p.version = 1; p.mesh = 0; p.mask = nullptr;
	b.parts.push_back (p);
}

static CollBody Body (uint32_t id, const Vector &c0, const Vector &v0, const Vector &c1, const Vector &v1, double h, uint8_t kind = COLLB_DYNAMIC)
{
	CollBody b {};
	b.id = id; b.kind = kind; b.planet = -1; b.rmax = 0.0; b.entry = 0; b.jump1 = false;
	b.m.c0 = c0; b.m.v0 = v0; b.m.c1 = c1; b.m.v1 = v1; b.m.h = h; b.m.a0ok = true;
	return b;
}

static CollBody Lin (uint32_t id, const Vector &c0, const Vector &v, double h, uint8_t kind = COLLB_DYNAMIC)
{
	return Body (id, c0, v, c0 + v*h, v, h, kind);
}

static CollBody Still (uint32_t id, const Vector &c, double h, uint8_t kind = COLLB_DYNAMIC)
{
	return Body (id, c, Vector (), c, Vector (), h, kind);
}

static CollFrameStats Frame (CollDetect &d, const CollParams &p, double h, const std::vector<CollBody> &bodies, std::vector<CollPairResult> &out)
{
	d.Begin (p, h);
	for (const CollBody &b : bodies) d.AddBody (b);
	out.clear ();
	CollFrameStats st {};
	d.Detect (out, st);
	return st;
}

static const CollPairResult *FindRes (const std::vector<CollPairResult> &v, int kind, int a = -1, int b = -1)
{
	for (const CollPairResult &r : v)
		if (r.kind == kind && (a < 0 || r.bodyA == a) && (b < 0 || r.bodyB == b)) return &r;
	return nullptr;
}

// part placement of a body's part at tau in the frame of A's c0 (the model CA follows)
static CollAffine ModelX (const CollBody &B, size_t k, double tau, const Vector &org)
{
	const CollPartRef &p = B.parts[k];
	CollAffine P = p.interp ? CollPoseAt (p.P0, p.P1, p.c, tau) : p.P1;
	Matrix R = TMat (B.m.Rot (tau));
	return CollAffine { R * P.A, mul (R, P.t) + (B.m.Pos (tau) - org) };
}

static bool SameV (const Vector &a, const Vector &b) { return memcmp (a.data, b.data, sizeof (a.data)) == 0; }
static bool SameQ (const Quaternion &a, const Quaternion &b) { return memcmp (a.data, b.data, sizeof (a.data)) == 0; }
static bool SameD (double a, double b) { return memcmp (&a, &b, sizeof (a)) == 0; }

static bool SameRes (const CollPairResult &a, const CollPairResult &b)
{
	if (a.kind != b.kind || a.flags != b.flags || a.bodyA != b.bodyA || a.bodyB != b.bodyB || a.npt != b.npt) return false;
	if (!SameD (a.tau, b.tau) || !SameD (a.specGap, b.specGap) || !SameD (a.E, b.E) || !SameV (a.origin, b.origin)) return false;
	const CollBodyAt *x[2] = { &a.a, &a.b }, *y[2] = { &b.a, &b.b };
	for (int s = 0; s < 2; s++)
		if (!SameV (x[s]->c, y[s]->c) || !SameV (x[s]->v, y[s]->v) || !SameQ (x[s]->q, y[s]->q) || !SameV (x[s]->w, y[s]->w)) return false;
	for (int i = 0; i < a.npt; i++) {
		const CollContact &p = a.pt[i], &q = b.pt[i];
		if (!SameV (p.pA, q.pA) || !SameV (p.pB, q.pB) || !SameV (p.n, q.n) || !SameD (p.gap, q.gap) || !SameV (p.vsA, q.vsA) || !SameV (p.vsB, q.vsB)) return false;
		if (p.triA != q.triA || p.triB != q.triB || p.partA != q.partA || p.partB != q.partB || p.patch != q.patch || p.flags != q.flags) return false;
	}
	return true;
}

static std::vector<std::string> g_log;
static void LogSink (int level, const char *msg) { (void)level; g_log.push_back (msg); }

// U1-U4: motion model and swept tests

TEST_CASE("U1 Hermite ends, slopes and constant acceleration", "[colldetect][U1]")
{
	Rng r (1);
	for (int k = 0; k < 200; k++) {
		CollMotion m {};
		double h = r.U (0.01, 5.0);
		Vector c0 = r.V (100.0), v0 = r.V (10.0), a = r.V (10.0);
		m.c0 = c0; m.v0 = v0; m.a0 = a; m.a1 = a; m.a0ok = true; m.h = h;
		m.c1 = c0 + v0*h + a*(0.5*h*h); m.v1 = v0 + a*h;
		m.Setup ();
		RequireVec (m.Pos (0.0), c0, 1e-12);
		RequireVec (m.Pos (1.0), m.c1, 1e-9);
		RequireVec (m.Vel (0.0), v0, 1e-9);
		RequireVec (m.Vel (1.0), m.v1, 1e-9);
		for (int i = 0; i <= 20; i++) {
			double s = i/20.0, t = s*h;
			RequireVec (m.Pos (s), c0 + v0*t + a*(0.5*t*t), 1e-9);
			RequireVec (m.Vel (s), v0 + a*t, 1e-9);
		}
		CollMotion z {};
		z.h = h; z.a0ok = true; z.Setup ();
		REQUIRE (CollErrorT (z, m) < 1e-9);
	}
}

TEST_CASE("U2 derivative hull contains sampled relative velocities", "[colldetect][U2]")
{
	Rng r (2);
	for (int k = 0; k < 100; k++) {
		CollMotion A {}, B {};
		double h = r.U (0.01, 5.0);
		for (CollMotion *m : { &A, &B }) {
			m->h = h; m->a0ok = true;
			m->c0 = r.V (50.0); m->c1 = r.V (50.0); m->v0 = r.V (20.0); m->v1 = r.V (20.0);
			m->Setup ();
		}
		Vector Q[4], M[3];
		CollRelBezier (A, B, Q, M);
		double sc = std::max (M[0].length (), std::max (M[1].length (), M[2].length ())) + 1.0;
		for (int i = 0; i < 1000; i++) {
			double s = i/999.0;
			Vector D = (B.Vel (s) - A.Vel (s))*h;
			Vector c = ClosestTri (D, M[0], M[1], M[2]);
			REQUIRE ((D - c).length () <= 1e-9*sc);
			double u = 1.0 - s;
			Vector rB = Q[0]*(u*u*u) + Q[1]*(3*u*u*s) + Q[2]*(3*u*s*s) + Q[3]*(s*s*s);
			RequireVec (rB, B.Pos (s) - A.Pos (s), 1e-9*(sc + 100.0));
		}
	}
}

// two-body Kepler propagation for U3 (RK4, small steps)
static const double KMU = 3.986004418e14, KRE = 6371e3;
struct KSt { Vector r, v; };
static Vector KAcc (const Vector &r) { double l = r.length (); return r*(-KMU/(l*l*l)); }
static KSt KStep (const KSt &s, double dt)
{
	Vector k1v = KAcc (s.r), k1r = s.v;
	Vector k2v = KAcc (s.r + k1r*(0.5*dt)), k2r = s.v + k1v*(0.5*dt);
	Vector k3v = KAcc (s.r + k2r*(0.5*dt)), k3r = s.v + k2v*(0.5*dt);
	Vector k4v = KAcc (s.r + k3r*dt), k4r = s.v + k3v*dt;
	return KSt { s.r + (k1r + k2r*2.0 + k3r*2.0 + k4r)*(dt/6.0), s.v + (k1v + k2v*2.0 + k3v*2.0 + k4v)*(dt/6.0) };
}
static KSt KProp (KSt s, double t)
{
	if (t <= 0.0) return s;
	int n = (int)ceil (t/0.25);
	for (int i = 0; i < n; i++) s = KStep (s, t/n);
	return s;
}
static KSt KCirc (double alt, double inc, double u)
{
	double r = KRE + alt, v = sqrt (KMU/r), c = cos (u), s = sin (u), ci = cos (inc), si = sin (inc);
	return KSt { Vector (c, s*ci, s*si)*r, Vector (-s, c*ci, c*si)*v };
}

TEST_CASE("U3 E_T against Kepler relative paths", "[colldetect][U3]")
{
	struct Case { const char *name; KSt a, b; double tc; };
	KSt fa = KCirc (400e3, 0.9, 0.0), fb = fa;
	fb.r = fb.r + Vector (0, -100, 30); fb.v = fb.v + Vector (0.05, 0.1, 0);
	double T = 2.0*Pi*sqrt (pow (KRE + 400e3, 3.0)/KMU), tm = T/4.0;
	Case cs[2] = { { "formation", fa, fb, 2000.0 }, { "crossing", KCirc (400e3, 0.9, -Pi05), KCirc (400e3, 0.9 + Pi025, -Pi05), tm } };
	int checked = 0;
	for (const Case &c : cs) {
		for (double h : { 1.667, 16.67, 166.7, 1667.0 }) {
			double t0 = c.tc - 0.5*h;
			KSt a = KProp (c.a, t0), b = KProp (c.b, t0);
			CollMotion A {}, B {};
			A.h = B.h = h; A.a0ok = B.a0ok = true;
			A.c0 = a.r; A.v0 = a.v; A.a0 = KAcc (a.r);
			B.c0 = b.r; B.v0 = b.v; B.a0 = KAcc (b.r);
			std::vector<Vector> rel;
			const int ns = 80;
			rel.push_back (b.r - a.r);
			for (int i = 1; i <= ns; i++) { a = KProp (a, h/ns); b = KProp (b, h/ns); rel.push_back (b.r - a.r); }
			A.c1 = a.r; A.v1 = a.v; A.a1 = KAcc (a.r);
			B.c1 = b.r; B.v1 = b.v; B.a1 = KAcc (b.r);
			A.Setup (); B.Setup ();
			double err = 0.0;
			for (int i = 0; i <= ns; i++) {
				double s = (double)i/ns;
				err = std::max (err, ((B.Pos (s) - A.Pos (s)) - rel[i]).length ());
			}
			double E = CollErrorT (A, B);
			std::printf ("U3 %-9s h = %7.3f s: Hermite error %.3e m, E_T %.3e m\n", c.name, h, err, E);
			if (err > 1e-6) {
				checked++;
				CHECK (E >= err);
				CHECK (E <= 10.0*err);
			}
		}
	}
	REQUIRE (checked >= 4);
}

TEST_CASE("U4 capsule and look-ahead tests never reject a sampled hit", "[colldetect][U4]")
{
	Rng r (4);
	int hits = 0;
	for (int k = 0; k < 10000; k++) {
		CollMotion A {}, B {};
		double h = r.U (0.01, 5.0);
		for (CollMotion *m : { &A, &B }) {
			m->h = h; m->a0ok = true;
			m->c0 = r.V (20.0); m->c1 = r.V (20.0); m->v0 = r.V (10.0/h); m->v1 = r.V (10.0/h);
			m->Setup ();
		}
		double ra = r.U (0.1, 5.0), rb = r.U (0.1, 5.0);
		Vector Q[4], M[3];
		CollRelBezier (A, B, Q, M);
		double dmin = 1e300;
		for (int i = 0; i <= 500; i++) dmin = std::min (dmin, (B.Pos (i/500.0) - A.Pos (i/500.0)).length ());
		if (dmin <= ra + rb) { hits++; REQUIRE (CollCapsuleHit (Q, ra + rb)); }
	}
	REQUIRE (hits > 500);
	int lhits = 0;
	for (int k = 0; k < 10000; k++) {
		double h = r.U (0.01, 5.0);
		CollEnd a { r.V (20.0), r.V (5.0), r.V (1.0), r.U (0.1, 5.0), r.U (0.0, 0.1) };
		CollEnd b { r.V (20.0), r.V (5.0), r.V (1.0), r.U (0.1, 5.0), r.U (0.0, 0.1) }; // braced lists evaluate left to right
		double dmin = 1e300, H = 2.0*h;
		Vector r1 = b.c1 - a.c1, w1 = b.v1 - a.v1, ar = b.a1 - a.a1;
		for (int i = 0; i <= 500; i++) { double s = H*i/500.0; dmin = std::min (dmin, (r1 + w1*s + ar*(0.5*s*s)).length ()); }
		if (dmin <= a.rmax + b.rmax + COLL_DELTA_CT + a.disp + b.disp) { lhits++; REQUIRE (CollLookAheadHit (a, b, h, COLL_DELTA_CT)); }
	}
	REQUIRE (lhits > 200);
	std::printf ("U4: %d capsule and %d look-ahead sampled hits, none rejected\n", hits, lhits);
}

// U5-U11: conservative advancement and manifolds

TEST_CASE("U5 icospheres head-on and offset", "[colldetect][U5]")
{
	const CollGeom *G = Geom (IcoM (3, 1.0));
	REQUIRE (G->tri.size () == 1280);
	double rc = 0.0, ri = 1e300;
	for (const Vector &v : G->vtx) rc = std::max (rc, v.length ());
	for (const CollTri &t : G->tri) {
		Vector a = G->vtx[t.v[0]], b = G->vtx[t.v[1]], c = G->vtx[t.v[2]];
		Vector n = crossp (b - a, c - a).unit ();
		ri = std::min (ri, fabs (n & a));
	}
	double h = 1.0/60.0, v = 300.0, x0 = 6.0, rs = 2.0*G->skin;
	for (double y : { 0.0, 1.0 }) {
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), G, VKey (1));
		bs.push_back (Lin (2, Vector (x0, y, 0), Vector (-v, 0, 0), h)); AddPart (bs.back (), G, VKey (2));
		std::vector<CollPairResult> res;
		CollFrameStats st = Frame (d, CollParams (), h, bs, res);
		const CollPairResult *t = FindRes (res, COLL_TOI);
		REQUIRE (t);
		// polyhedron distance lies in [D - 2 rc, D - 2 ri]; CA stops with d_tri in [rs, rs + delta_toi]
		auto TauAt = [&] (double D) { return (x0 - sqrt (D*D - y*y))/(v*h); };
		double tlo = TauAt (rs + COLL_DELTA_TOI + 2.0*rc), thi = TauAt (rs + 2.0*ri);
		std::printf ("U5 offset %.1f: tau %.6f in [%.6f, %.6f], %d iterations, %d BV pairs, %d tri pairs\n", y, t->tau, tlo, thi, st.iterations, st.bvPairs, st.triPairs);
		CHECK (t->tau >= tlo);
		CHECK (t->tau <= thi);
	}
}

// boxes with an edge up (A, along x) and an edge down (B, along y)
TEST_CASE("U6 edge-edge grazing at 3 km/s", "[colldetect][U6]")
{
	double s2 = sqrt (0.5);
	const CollGeom *GA = Geom (XForm (BoxM (Vector (5, 0.5, 0.5)), RotAxis (Vector (1, 0, 0), Pi025), Vector ()));
	const CollGeom *GB = Geom (XForm (BoxM (Vector (0.5, 5, 0.5)), RotAxis (Vector (0, 1, 0), Pi025), Vector ()));
	double top = 0.0, bot = 0.0;
	for (const Vector &v : GA->vtx) top = std::max (top, v.z);
	for (const Vector &v : GB->vtx) bot = std::min (bot, v.z);
	REQUIRE_THAT (top, WithinAbs (s2, 1e-6));
	double h = 1.0/60.0, v = 3000.0, margin = 2.0*COLL_SKIN_DEFAULT + 0.5*COLL_DELTA_TOI; // separation CA guarantees (5.2)
	for (int sign : { 1, -1 }) {
		double g = margin + sign*0.001;
		double zb = top - bot + g;
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), GA, VKey (1));
		bs.push_back (Lin (2, Vector (-25, 0, zb), Vector (v, 0, 0), h)); AddPart (bs.back (), GB, VKey (2));
		std::vector<CollPairResult> res;
		CollFrameStats st = Frame (d, CollParams (), h, bs, res);
		std::printf ("U6 gap margin %+.0f mm: %zu results, %d iterations\n", sign*1.0, res.size (), st.iterations);
		if (sign > 0) { CHECK (res.empty ()); continue; }
		const CollPairResult *t = FindRes (res, COLL_TOI);
		REQUIRE (t);
		// A's edge ends at x = -5 (float vertices); CA aims at sqrt (dx^2 + g^2) = margin
		double xend = 1e300;
		for (const Vector &p : GA->vtx) xend = std::min (xend, p.x);
		double xr = 1e300;
		for (const Vector &p : GB->vtx) if (p.z < bot + 1e-6) xr = std::min (xr, p.x);
		double dx = sqrt (margin*margin - g*g);
		double ta = ((xend - dx) - (-25.0 + xr))/(v*h);
		std::printf ("U6: tau %.7f analytic %.7f\n", t->tau, ta);
		CHECK (fabs (t->tau - ta) <= 1e-4);
		CHECK (st.iterations <= 8);
	}
}

TEST_CASE("U7 thin plate, cube at 3 km/s, normal side, U11 at 1 AU and Neptune", "[colldetect][U7][U11]")
{
	const CollGeom *cube = Geom (BoxM (Vector (0.25, 0.25, 0.25)));
	const CollGeom *plate = Geom (PlateM (5, 5));
	double h = 1.0/60.0, v = 3000.0;
	double tau0 = 0.0;
	Vector p0A, p0B;
	for (double S : { 0.0, 1.5e11, 4.5e12 }) {
		for (int side : { 1, -1 }) {
			Vector sh (S, S, S);
			CollDetect d;
			std::vector<CollBody> bs;
			bs.push_back (Lin (1, sh + Vector (0, 0, side*10.25), Vector (0, 0, -side*v), h)); AddPart (bs.back (), cube, VKey (1));
			bs.push_back (Still (2, sh, h)); AddPart (bs.back (), plate, VKey (2));
			std::vector<CollPairResult> res;
			Frame (d, CollParams (), h, bs, res);
			// discrete t0 and t1 tests see nothing
			CollScratch sc;
			for (double t : { 0.0, 1.0 }) {
				CollAffine XA = ModelX (d.Body (0), 0, t, d.Body (0).m.c0), XB = ModelX (d.Body (1), 0, t, d.Body (0).m.c0);
				CHECK (CollDistance (*cube, XA, *plate, XB, 100.0, nullptr, sc) > 1.0);
			}
			const CollPairResult *r = FindRes (res, COLL_TOI);
			REQUIRE (r);
			REQUIRE (res.size () == 1);
			CHECK_THAT (r->tau, WithinAbs (0.19915, 1e-5));
			for (int i = 0; i < r->npt; i++) RequireVec (r->pt[i].n, Vector (0, 0, side), 1e-9);
			if (S == 0.0 && side == 1) { tau0 = r->tau; p0A = r->pt[0].pA; p0B = r->pt[0].pB; }
			else if (side == 1) {
				std::printf ("U11 shift %.1e: tau %.12f vs %.12f, point offset %.2e m\n", S, r->tau, tau0, (r->pt[0].pA - p0A).length ());
				CHECK (fabs (r->tau - tau0) <= 1e-9);
				CHECK ((r->pt[0].pA - p0A).length () <= 1e-4);
				CHECK ((r->pt[0].pB - p0B).length () <= 1e-4);
			}
		}
	}
}

// 20 m bar spinning about its centre into a post; spin path at 0.6 and 3 rad per step
static void BarScene (double turn, int nCa, CollDetect &d, std::vector<CollPairResult> &res, CollFrameStats &st, const CollGeom *&bar, const CollGeom *&post)
{
	bar = Geom (BoxM (Vector (10, 0.25, 0.25)));
	post = Geom (BoxM (Vector (0.25, 0.25, 2.0)));
	double w = 5.0, h = turn/w;
	Vector wv (0, 0, w);
	CollBody A = Still (1, Vector (), h);
	A.m.w0g = wv; A.m.w1g = wv;
	A.m.q1.Set (TRotG (Quaternion (), wv*h));
	AddPart (A, bar, VKey (1));
	// post centre where the bar's +x end points at tau 0.6
	Quaternion qp = TRotG (Quaternion (), wv*(0.6*h));
	Vector pc = mul (TMat (qp), Vector (6, 0, 0));
	CollBody B = Still (2, pc, h);
	AddPart (B, post, VKey (2));
	std::vector<CollBody> bs;
	bs.push_back (A); bs.push_back (B);
	CollParams p;
	p.nCa = nCa;
	st = Frame (d, p, h, bs, res);
}

static double ModelGap (const CollDetect &d, double tau, CollScratch &sc)
{
	const CollBody &A = d.Body (0), &B = d.Body (1);
	CollAffine XA = ModelX (A, 0, tau, A.m.c0), XB = ModelX (B, 0, tau, A.m.c0);
	return CollDistance (*A.parts[0].geom, XA, *B.parts[0].geom, XB, 1e6, nullptr, sc) - A.parts[0].skin - B.parts[0].skin;
}

TEST_CASE("U8 spinning bar into a post, spin path ends and rate bound", "[colldetect][U8]")
{
	for (double turn : { 0.6, 3.0 }) {
		CollDetect d;
		std::vector<CollPairResult> res;
		CollFrameStats st;
		const CollGeom *bar, *post;
		BarScene (turn, COLL_N_CA, d, res, st, bar, post);
		const CollMotion &m = d.Body (0).m;
		REQUIRE (m.spin);
		CHECK (MatDiff (TMat (m.Rot (0.0)), TMat (m.q0)) <= 1e-12);
		CHECK (MatDiff (TMat (m.Rot (1.0)), TMat (m.q1)) <= 1e-12);
		double maxRate = 0.0;
		for (int i = 0; i < 1000; i++) maxRate = std::max (maxRate, QAngle (m.Rot (i/1000.0), m.Rot ((i + 1)/1000.0))*1000.0);
		CHECK (maxRate <= m.theta*(1.0 + 1e-9) + 1e-9);
		const CollPairResult *t = FindRes (res, COLL_TOI);
		REQUIRE (t);
		// reference: first sampled tau with gap <= 3.75 mm (delta_toi/2..delta_toi), refined by bisection
		CollScratch sc;
		double lo = 0.0, hi = 1.0;
		for (int i = 1; i <= 2000; i++) if (ModelGap (d, i/2000.0, sc) <= 0.00375) { hi = i/2000.0; lo = (i - 1)/2000.0; break; }
		REQUIRE (hi < 1.0);
		for (int i = 0; i < 50; i++) { double mid = 0.5*(lo + hi); if (ModelGap (d, mid, sc) <= 0.00375) hi = mid; else lo = mid; }
		std::printf ("U8 turn %.1f rad/step: tau %.6f reference %.6f, %d iterations\n", turn, t->tau, hi, st.iterations);
		CHECK (fabs (t->tau - hi) <= 1e-3);
		CHECK (ModelGap (d, t->tau, sc) > 0.0);
	}
}

TEST_CASE("U9 iteration cap gives SPECULATIVE with a guaranteed gap", "[colldetect][U9]")
{
	CollDetect full, capped;
	std::vector<CollPairResult> rf, rc;
	CollFrameStats st;
	const CollGeom *bar, *post;
	BarScene (3.0, COLL_N_CA, full, rf, st, bar, post);
	BarScene (3.0, 2, capped, rc, st, bar, post);
	const CollPairResult *t = FindRes (rf, COLL_TOI), *s = FindRes (rc, COLL_SPECULATIVE);
	REQUIRE (t);
	REQUIRE (s);
	CHECK (st.iterations == 2);
	CollScratch sc;
	double gap = ModelGap (capped, s->tau, sc);
	std::printf ("U9: tau_cap %.6f < TOI %.6f, specGap %.4f m <= true gap %.4f m, %d points\n", s->tau, t->tau, s->specGap, gap, s->npt);
	CHECK (s->tau < t->tau);
	CHECK (s->specGap >= 0.0);
	CHECK (s->specGap <= gap);
	CHECK (s->npt >= 1);
}

TEST_CASE("U10 box on a plate and in a V groove: manifold points and patches", "[colldetect][U10]")
{
	double h = 1.0/60.0;
	{
		const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
		const CollGeom *plate = Geom (PlateM (5, 5));
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (0, 0, 0.53125), h)); AddPart (bs.back (), box, VKey (1));
		bs.push_back (Still (2, Vector (), h)); AddPart (bs.back (), plate, VKey (2));
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		REQUIRE (res.size () == 1);
		const CollPairResult &r = res[0];
		REQUIRE (r.kind == COLL_RESTING);
		REQUIRE (r.npt == 4);
		for (int i = 0; i < 4; i++) {
			RequireVec (r.pt[i].n, Vector (0, 0, 1), 1e-12);
			CHECK_THAT (r.pt[i].gap, WithinAbs (0.03125 - 0.04, 1e-9));
			CHECK_THAT (fabs (r.pt[i].pA.x), WithinAbs (0.5, 1e-9));
			CHECK_THAT (fabs (r.pt[i].pA.y), WithinAbs (0.5, 1e-9));
			CHECK (r.pt[i].patch == 0);
		}
	}
	{
		// V groove: walls at +-45 deg about x meeting at z = 0, box turned 45 deg resting in it
		double s2 = sqrt (0.5);
		Mesh v;
		double e = 4.0*s2;
		v.v = { Vector (-5, 0, 0), Vector (5, 0, 0), Vector (5, e, e), Vector (-5, e, e), Vector (5, -e, e), Vector (-5, -e, e) };
		Tri (v, 0, 1, 2); Tri (v, 0, 2, 3); Tri (v, 0, 1, 4); Tri (v, 0, 4, 5);
		const CollGeom *groove = Geom (v);
		const CollGeom *box = Geom (XForm (BoxM (Vector (0.5, 0.5, 0.5)), RotAxis (Vector (1, 0, 0), Pi025), Vector ()));
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (0, 0, 0.5/s2 + 0.03/s2), h)); AddPart (bs.back (), box, VKey (1));
		bs.push_back (Still (2, Vector (), h)); AddPart (bs.back (), groove, VKey (2));
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		REQUIRE (res.size () == 1);
		const CollPairResult &r = res[0];
		REQUIRE (r.kind == COLL_RESTING);
		int np = 0;
		for (int i = 0; i < r.npt; i++) np = std::max (np, r.pt[i].patch + 1);
		std::printf ("U10 V groove: %d points in %d patches\n", r.npt, np);
		CHECK (np == 2);
		for (int i = 0; i < r.npt; i++) CHECK_THAT (fabs (r.pt[i].n.y), WithinAbs (s2, 1e-6));
	}
}

// U12-U14, U20: zones, entry events, warp math, jump helpers

TEST_CASE("U12 zone gates and attachment ids", "[colldetect][U12]")
{
	auto Port = [] (const Vector &g, const Vector &d, const Vector &v) { CollPortAt p; p.g = g; p.d = d; p.r = Vector (0, 1, 0); p.v = v; return p; };
	CollPortAt a = Port (Vector (), Vector (1, 0, 0), Vector ());
	auto B = [&] (double angDeg, const Vector &g, double close) {
		return Port (g, Vector (-cos (angDeg*_RAD_), sin (angDeg*_RAD_), 0), Vector (-close, 0, 0)); };
	CHECK (CollDockZoneActive (a, B (14, Vector (1, 0, 0), 0.1), 1.5, COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK_FALSE (CollDockZoneActive (a, B (16, Vector (1, 0, 0), 0.1), 1.5, COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK (CollDockZoneActive (a, B (55, Vector (1, 0, 0), 0.1), 1.5, 60.0, COLL_ZONE_SPEED));
	CHECK_FALSE (CollDockZoneActive (a, B (65, Vector (1, 0, 0), 0.1), 1.5, 60.0, COLL_ZONE_SPEED));
	CHECK (CollDockZoneActive (a, B (0, Vector (1, 0, 0), 0.9), 1.5, COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK_FALSE (CollDockZoneActive (a, B (0, Vector (1, 0, 0), 1.1), 1.5, COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK (CollDockZoneActive (a, B (0, Vector (1, 1.0, 0), 0.1), 1.5, COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK_FALSE (CollDockZoneActive (a, B (0, Vector (1, 2.0, 0), 0.1), 1.5, COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK_FALSE (CollDockZoneActive (a, B (0, Vector (3.1, 0, 0), 0.1), 1.5, COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CollPortAt p = Port (Vector (), Vector (0, 1, 0), Vector ());
	p.r = Vector (1, 0, 0);
	auto C = [&] (double dist, double angDeg, double speed) {
		CollPortAt c = Port (Vector (0, dist, 0), Vector (sin (angDeg*_RAD_), -cos (angDeg*_RAD_), 0), Vector (0, -speed, 0));
		c.r = Vector (1, 0, 0);
		return c; };
	CHECK (CollAttachZoneActive (p, C (2.4, 0, 0.5), COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK_FALSE (CollAttachZoneActive (p, C (2.6, 0, 0.5), COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK_FALSE (CollAttachZoneActive (p, C (1.0, 16, 0.5), COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK_FALSE (CollAttachZoneActive (p, C (1.0, 0, 1.1), COLL_ZONE_ANGLE, COLL_ZONE_SPEED));
	CHECK (CollAttachIdMatch ("SH", "SH"));
	CHECK (CollAttachIdMatch ("G", "GS"));
	CHECK (CollAttachIdMatch ("X", "XS"));
	CHECK (CollAttachIdMatch ("SH", "SH1"));
	CHECK_FALSE (CollAttachIdMatch ("X", "GS"));
	CHECK_FALSE (CollAttachIdMatch ("", "SH"));
	const char full[8] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H' };
	CHECK (CollAttachIdMatch (full, full));
}

// vessel A: port ring at +x, wing at y = 6; vessel B: ring facing A's ring, a tip touching the wing
static void ZoneScene (std::vector<CollBody> &bs, double h)
{
	Mesh ra = BoxM (Vector (0.05, 0.6, 0.6), Vector (1.0, 0, 0));
	Mesh wa = PlateM (1.0, 1.0, Vector (0, 6, 0));
	Mesh rb = BoxM (Vector (0.05, 0.6, 0.6), Vector (-1.0, 0, 0));
	Mesh tb = BoxM (Vector (0.2, 0.2, 0.2), Vector (-2.12, 6, 0.22));
	Merge (ra, wa);
	Merge (rb, tb);
	bs.clear ();
	bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), Geom (ra), VKey (1));
	bs.push_back (Still (2, Vector (2.12, 0, 0), h)); AddPart (bs.back (), Geom (rb), VKey (2));
}

TEST_CASE("U12 docking zone AND rule, owner restriction and embedded attachments", "[colldetect][U12]")
{
	double h = 1.0/60.0;
	std::vector<CollBody> bs;
	ZoneScene (bs, h);
	auto Ring = [] (const CollContact &c) { return fabs (c.pA.y) < 1.0; };
	auto Count = [&] (const std::vector<CollZone> &z, int &ring, int &wing, uint32_t &flags) {
		CollDetect d;
		d.Begin (CollParams (), h);
		for (const CollBody &b : bs) d.AddBody (b);
		d.SetZones (z);
		std::vector<CollPairResult> res;
		CollFrameStats st;
		d.Detect (res, st);
		ring = wing = 0; flags = 0;
		for (const CollPairResult &r : res) { flags |= r.flags; for (int i = 0; i < r.npt; i++) (Ring (r.pt[i]) ? ring : wing)++; }
	};
	CollZone z { 0, 1, VKey (1), VKey (2), Vector (1.05, 0, 0), Vector (-1.05, 0, 0), 1.5, 1.5 };
	int ring, wing;
	uint32_t fl;
	Count ({}, ring, wing, fl);
	CHECK (ring > 0);
	CHECK (wing > 0);
	Count ({ z }, ring, wing, fl);
	CHECK (ring == 0);
	CHECK (wing > 0);
	CHECK ((fl & COLLF_ZONE) != 0);
	CollZone zb = z;
	zb.cb = Vector (-1.05, 0, 4.0);
	Count ({ zb }, ring, wing, fl);
	CHECK (ring > 0);
	CollZone zo = z;
	zo.ownerB = VKey (99);
	Count ({ zo }, ring, wing, fl);
	CHECK (ring > 0);
	CollZone zr { 1, 0, VKey (2), VKey (1), Vector (-1.05, 0, 0), Vector (1.05, 0, 0), 1.5, 1.5 };
	Count ({ zr }, ring, wing, fl);
	CHECK (ring == 0);
	CHECK (wing > 0);

	// attachment embedding: pod mounted into a slot frame (intersecting) vs payload clear of carrier
	const CollGeom *frame = Geom (BoxM (Vector (2, 0.1, 2)));
	const CollGeom *pod = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	CollPartRef pp {}, cp {};
	pp.geom = frame; pp.skin = 0.02; pp.owner = VKey (10); pp.version = 1;
	cp.geom = pod; cp.skin = 0.02; cp.owner = VKey (11); cp.version = 1;
	CollDetect d;
	CollEmbedQuery q { VKey (10), VKey (11), 1234, &pp, 1, &cp, 1, CollTranslate (Vector (0, 0.3, 0)) };
	CHECK (d.Embedded (q));
	CHECK (d.Embedded (q));
	CollEmbedQuery q2 { VKey (10), VKey (11), 5678, &pp, 1, &cp, 1, CollTranslate (Vector (0, 0.7, 0)) };
	CHECK_FALSE (d.Embedded (q2));
	const CollPairEntry *e = d.Pairs ().Find (VKey (11), VKey (10));
	REQUIRE (e);
	CHECK (e->embed.size () == 2);
}

TEST_CASE("U13 entry events, GRACE scopes and supports", "[colldetect][U13]")
{
	double h = 1.0/60.0;
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	const CollGeom *slab = Geom (XForm (IcoM (2, 3.0), RotAxis (Vector (0, 0, 1), 0.3), Vector ()));
	SECTION("touching at every entry event: RESTING, no GRACE") {
		for (uint16_t ev : { COLLE_NEW, COLLE_MEMBERS, COLLE_JUMP, COLLE_ACTIVATED, COLLE_MESH }) {
			CollDetect d;
			std::vector<CollBody> bs;
			bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), box, VKey (1));
			bs.push_back (Still (2, Vector (0, 0, 1.02), h)); AddPart (bs.back (), box, VKey (2));
			bs.back ().entry = ev;
			std::vector<CollPairResult> res;
			Frame (d, CollParams (), h, bs, res);
			REQUIRE (res.size () == 1);
			CHECK (res[0].kind == COLL_RESTING);
			CHECK ((res[0].flags & COLLF_ENTRY) != 0);
			CHECK ((res[0].flags & COLLF_GRACE) == 0);
			const CollPairEntry *e = d.Pairs ().Find (VKey (1), VKey (2));
			CHECK ((!e || e->grace.empty ()));
		}
	}
	SECTION("intersecting: scope = the intersecting leaf pairs, release per leaf pair, 60 s log") {
		const CollGeom *box = Geom (XForm (BoxM (Vector (0.5, 0.5, 0.5)), RotAxis (Vector (1, 0, 0), 0.3), Vector ()));
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), slab, VKey (1));
		bs.push_back (Still (2, Vector (0, 0, 3.2), h)); AddPart (bs.back (), box, VKey (2));
		bs.back ().entry = COLLE_NEW;
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		const CollPairEntry *e = d.Pairs ().Find (VKey (1), VKey (2));
		REQUIRE (e);
		REQUIRE (!e->grace.empty ());
		// brute force: every leaf pair with a crossing triangle pair
		std::vector<CollLeafPair> want;
		CollAffine XB = CollTranslate (Vector (0, 0, 3.2));
		for (uint32_t i = 0; i < slab->node.size (); i++) {
			if (!slab->node[i].count) continue;
			for (uint32_t j = 0; j < box->node.size (); j++) {
				if (!box->node[j].count) continue;
				bool x = false;
				for (uint32_t a = slab->node[i].first; a < slab->node[i].first + slab->node[i].count && !x; a++)
					for (uint32_t b = box->node[j].first; b < box->node[j].first + box->node[j].count && !x; b++) {
						const CollTri &ta = slab->tri[slab->perm[a]], &tb = box->tri[box->perm[b]];
						Vector va[3] = { slab->vtx[ta.v[0]], slab->vtx[ta.v[1]], slab->vtx[ta.v[2]] };
						Vector vb[3] = { CollApply (XB, box->vtx[tb.v[0]]), CollApply (XB, box->vtx[tb.v[1]]), CollApply (XB, box->vtx[tb.v[2]]) };
						Vector p, q;
						if (CollTriTriDistance (va, vb, p, q) == 0.0) x = true;
					}
				if (x) want.push_back (CollLeafPair { 0, 1, i, 0, 1, j });
			}
		}
		std::sort (want.begin (), want.end ());
		CHECK (e->grace == want);
		std::printf ("U13: GRACE scope %zu leaf pairs\n", e->grace.size ());
		// other leaf pairs still collide: the result's points come from unscoped leaf pairs
		REQUIRE (res.size () >= 1);
		CHECK ((res[0].flags & COLLF_GRACE) != 0);
		for (const CollPairResult &r : res) CHECK (r.npt >= 1);
		// 60 s log once
		g_log.clear ();
		g_collLog = LogSink;
		bs.back ().entry = 0;
		for (int f = 0; f < 70; f++) Frame (d, CollParams (), 1.0, bs, res);
		int n60 = 0;
		for (const std::string &s : g_log) if (s.find ("older than") != std::string::npos) n60++;
		CHECK (n60 == 1);
		// move away step by step: a leaf pair leaves the scope exactly when its d_eff reaches s_rel at t1
		auto LeafGap = [&] (const CollLeafPair &l, double z) {
			double dm = 1e300;
			const CollNode &na = slab->node[l.leafA], &nb = box->node[l.leafB];
			for (uint32_t a = na.first; a < na.first + na.count; a++)
				for (uint32_t b = nb.first; b < nb.first + nb.count; b++) {
					const CollTri &ta = slab->tri[slab->perm[a]], &tb = box->tri[box->perm[b]];
					Vector va[3] = { slab->vtx[ta.v[0]], slab->vtx[ta.v[1]], slab->vtx[ta.v[2]] };
					Vector vb[3] = { box->vtx[tb.v[0]] + Vector (0, 0, z), box->vtx[tb.v[1]] + Vector (0, 0, z), box->vtx[tb.v[2]] + Vector (0, 0, z) };
					Vector p, q;
					dm = std::min (dm, CollTriTriDistance (va, vb, p, q));
				}
			return dm - 2.0*COLL_SKIN_DEFAULT;
		};
		std::vector<CollLeafPair> prev = e->grace;
		int partial = 0;
		for (int f = 1; f <= 80; f++) {
			double z = 3.2 + 0.01*f;
			bs.back ().m.c0 = Vector (0, 0, z); bs.back ().m.c1 = Vector (0, 0, z);
			Frame (d, CollParams (), h, bs, res);
			e = d.Pairs ().Find (VKey (1), VKey (2));
			std::vector<CollLeafPair> now = e ? e->grace : std::vector<CollLeafPair> ();
			for (const CollLeafPair &l : prev) {
				bool kept = std::binary_search (now.begin (), now.end (), l);
				CHECK ((LeafGap (l, z) >= COLL_S_REL) == !kept);
			}
			if (!now.empty () && now.size () < prev.size ()) partial++;
			prev = now;
		}
		g_collLog = nullptr;
		std::printf ("U13: scope shrank leaf by leaf in %d frames\n", partial);
		CHECK (partial >= 1);
		CHECK (prev.empty ());
	}
	SECTION("supports: no GRACE, SUPPORT and INTERSECT, stored normal, SupportDist, wildcard, clear") {
		const CollGeom *roof = Geom (BoxM (Vector (3, 3, 0.5)));
		auto Scene = [&] (double z, uint16_t ev, std::vector<CollBody> &bs) {
			bs.clear ();
			CollBody base = Still (5, Vector (), h, COLLB_BASE);
			base.planet = 0;
			AddPart (base, roof, BKey (0, 3, 7));
			AddPart (base, Geom (BoxM (Vector (3, 3, 0.5), Vector (6.5, 0, 0))), BKey (0, 3, 8));
			bs.push_back (base);
			bs.push_back (Still (1, Vector (3.25, 0, z), h)); AddPart (bs.back (), box, VKey (1));
			bs.back ().entry = ev;
		};
		CollDetect d;
		std::vector<CollBody> bs;
		std::vector<CollPairResult> res;
		Scene (0.9, COLLE_ACTIVATED, bs);
		d.SetSupports (1, { CollSupport { BKey (0, 3, 7), Vector (0, 0, 1) } });
		Frame (d, CollParams (), h, bs, res);
		CHECK ((!d.Pairs ().Find (VKey (1), BKey (0, 3, 7)) || d.Pairs ().Find (VKey (1), BKey (0, 3, 7))->grace.empty ()));
		const CollPairEntry *e8 = d.Pairs ().Find (VKey (1), BKey (0, 3, 8));
		REQUIRE (e8);
		CHECK (!e8->grace.empty ());
		REQUIRE (res.size () >= 1);
		int sup = 0, inter = 0, stored = 0;
		for (const CollPairResult &r : res)
			for (int i = 0; i < r.npt; i++) {
				const CollContact &c = r.pt[i];
				if (!(c.flags & COLLP_SUPPORT)) continue;
				sup++;
				if (c.flags & COLLP_INTERSECT) inter++;
				if ((c.flags & COLLP_DEGENERATE) && (c.n - Vector (0, 0, -1)).length () < 1e-12) stored++;
				CHECK (d.Owner (r, i, 0) == BKey (0, 3, 7));
			}
		CHECK (sup > 0);
		CHECK (inter > 0);
		CHECK (stored > 0);
		double md = -1.0;
		REQUIRE (d.SupportDist (1, md));
		CHECK (md == 0.0);
		// touching support pair gives its raw distance
		Scene (1.02, 0, bs);
		Frame (d, CollParams (), h, bs, res);
		REQUIRE (d.SupportDist (1, md));
		CHECK_THAT (md, WithinAbs (0.02, 1e-6));
		// planet wildcard: both buildings within delta_ct count
		CollDetect w;
		Scene (1.02, COLLE_ACTIVATED, bs);
		w.SetSupports (1, { CollSupport { CollOwnerKey { COLLO_BUILDING, 0, 0, -1, -1, -1 }, Vector () } });
		Frame (w, CollParams (), h, bs, res);
		int owners = 0;
		bool o7 = false, o8 = false;
		for (const CollPairResult &r : res)
			for (int i = 0; i < r.npt; i++) {
				CHECK ((r.pt[i].flags & COLLP_SUPPORT) != 0);
				CollOwnerKey o = w.Owner (r, i, 0);
				o7 |= o == BKey (0, 3, 7); o8 |= o == BKey (0, 3, 8);
			}
		owners = (o7 ? 1 : 0) + (o8 ? 1 : 0);
		CHECK (owners == 2);
		// empty list clears: the intersecting pair now enters GRACE
		d.SetSupports (1, {});
		Scene (0.9, COLLE_ACTIVATED, bs);
		Frame (d, CollParams (), h, bs, res);
		const CollPairEntry *e7 = d.Pairs ().Find (VKey (1), BKey (0, 3, 7));
		REQUIRE (e7);
		CHECK (!e7->grace.empty ());
		CHECK_FALSE (d.SupportDist (1, md));
	}
}

TEST_CASE("U14 warp guard math and look-ahead", "[colldetect][U14]")
{
	CollWarpInput c;
	c.hContact = COLL_H_CONTACT;
	CHECK (CollWarpAllowed (c, 1000.0, 1.0/60.0) == 1.0);
	CHECK (CollWarpAllowed (c, 1.0, 1.0/60.0) == 1.0);
	CHECK (CollWarpAllowed (c, 100.0, 1.0/144.0) == 10.0);
	CollWarpInput l;
	l.hLoad = COLL_H_REST;
	CHECK (CollWarpAllowed (l, 10.0, 1.0/60.0) >= 10.0);
	CHECK (CollWarpAllowed (l, 100.0, 1.0/60.0) == 10.0);
	CollWarpInput n;
	CHECK (CollWarpAllowed (n, 1e5, 1.0/60.0) >= 1e5);
	CollWarpInput e;
	e.accF = sqrt (COLL_E_TOL/0.16);
	CHECK (CollWarpAllowed (e, 100.0, 1.0/60.0) == 10.0);
	CollWarpInput t;
	t.accF = COLL_THETA_MAX/0.9;
	CHECK (CollWarpAllowed (t, 10.0, 1.0/60.0) == 1.0);
	CollWarpInput tiny;
	tiny.hContact = 1e-9; tiny.accF = 1e-9;
	CHECK (CollWarpAllowed (tiny, 1e5, 1.0/60.0) == 1.0);
	CHECK (CollFloorPow10 (0.5) == 1.0);
	CHECK (CollFloorPow10 (1000.0) == 1000.0);
	CHECK (CollFloorPow10 (999.999) == 100.0);
	// 7.4: two vessels 20 m apart closing at 1 m/s, warp 1000 at 60 fps
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	double h = 1000.0/60.0;
	CollDetect d;
	std::vector<CollBody> bs;
	bs.push_back (Lin (1, Vector (), Vector (0.5, 0, 0), h)); AddPart (bs.back (), box, VKey (1));
	bs.push_back (Lin (2, Vector (21, 0, 0), Vector (-0.5, 0, 0), h)); AddPart (bs.back (), box, VKey (2));
	std::vector<CollPairResult> res;
	Frame (d, CollParams (), h, bs, res);
	CHECK (res.empty ());
	std::vector<CollEnd> end;
	for (int i = 0; i < d.nBody (); i++) { const CollBody &b = d.Body (i); end.push_back (CollEnd { b.m.c1, b.m.v1, b.m.a1, b.rmax, 0.0 }); }
	CollWarpInput w;
	d.LookAhead (end, w);
	CHECK (w.hContact == COLL_H_CONTACT);
	CHECK (w.idContact[0] == 1);
	CHECK (w.idContact[1] == 2);
	CHECK (CollWarpAllowed (w, 1000.0, 1.0/60.0) == 1.0);
	// accuracy input: a spinning candidate pair at 0.9 rad per step
	CollDetect s;
	bs.clear ();
	bs.push_back (Still (1, Vector (), 1.0)); AddPart (bs.back (), box, VKey (1));
	bs.back ().m.w0g = bs.back ().m.w1g = Vector (0, 0.9, 0);
	bs.back ().m.q1.Set (TRotG (Quaternion (), Vector (0, 0.9, 0)));
	bs.push_back (Still (2, Vector (1.5, 0, 0), 1.0)); AddPart (bs.back (), box, VKey (2));
	Frame (s, CollParams (), 1.0, bs, res);
	end.clear ();
	for (int i = 0; i < s.nBody (); i++) { const CollBody &b = s.Body (i); end.push_back (CollEnd { b.m.c1, b.m.v1, b.m.a1, b.rmax, 0.0 }); }
	CollWarpInput wa;
	s.LookAhead (end, wa);
	CHECK_THAT (wa.accF, WithinAbs (COLL_THETA_MAX/0.9, 1e-9));
	CHECK (CollWarpAllowed (wa, 10.0, 1.0) == 1.0);
}

TEST_CASE("U20 jump classification helpers", "[colldetect][U20]")
{
	CHECK (CollJumpPhase (false, false) == COLLJP_INSTEP);
	CHECK (CollJumpPhase (false, true) == COLLJP_T0);
	CHECK (CollJumpPhase (true, false) == COLLJP_T0);
	CHECK (CollJumpPhase (true, true) == COLLJP_T0);
	Vector dr (0.3, -0.2, 0.1);
	Vector comp[2] = { -dr, -dr };
	CHECK (CollGeomJump (dr, comp, 2) == 0.0);
	Vector none[1] = { Vector () };
	CHECK_THAT (CollGeomJump (dr, none, 1), WithinAbs (dr.length (), 1e-15));
	CHECK_THAT (CollGeomJump (dr, nullptr, 0), WithinAbs (dr.length (), 1e-15));
	Vector mesh[2] = { Vector (), Vector (0, 0, 0.004) };
	CHECK_THAT (CollGeomJump (Vector (), mesh, 2), WithinAbs (0.004, 1e-15));
	CollVesselFlags f;
	CHECK (f.jump == 0);
	CHECK_FALSE (f.inStep);
}

// U15-U18: regimes, contract, surface velocity, Resweep

static void RestBox (double h, std::vector<CollBody> &bs, const CollGeom *box, const CollGeom *plate, const Vector &v = Vector ())
{
	Vector g (0, 0, -9.81), c0 (0, 0, 0.54);
	bs.clear ();
	bs.push_back (Body (1, c0, v, c0 + v*h + g*(0.5*h*h), v + g*h, h)); AddPart (bs.back (), box, VKey (1));
	bs.back ().m.a0 = g; bs.back ().m.a1 = g;
	bs.push_back (Still (2, Vector (), h)); AddPart (bs.back (), plate, VKey (2));
}

TEST_CASE("U15 resting box under gravity: RESTING at tau 0, CORE TOI only at h = 0.1", "[colldetect][U15]")
{
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	const CollGeom *plate = Geom (PlateM (5, 5));
	for (double h : { 1.0/60.0, 0.1 }) {
		CollDetect d;
		std::vector<CollBody> bs;
		RestBox (h, bs, box, plate);
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		const CollPairResult *r = FindRes (res, COLL_RESTING);
		REQUIRE (r);
		CHECK (r->tau == 0.0);
		CHECK (r->npt == 4);
		for (int i = 0; i < r->npt; i++) { CHECK_THAT (r->pt[i].pA.z, WithinAbs (-0.5, 1e-6)); CHECK_THAT (r->pt[i].pB.z, WithinAbs (-0.54, 1e-6)); }
		const CollPairResult *c = FindRes (res, COLL_TOI);
		if (h < 0.05) CHECK (c == nullptr);
		else {
			REQUIRE (c);
			CHECK ((c->flags & COLLF_CORE) != 0);
			std::printf ("U15 h = 0.1: CORE TOI at tau %.4f\n", c->tau);
			CHECK (c->tau > 0.75);
			CHECK (c->tau < 0.86);
		}
	}
}

TEST_CASE("U16 contract: normal, origin, body states, point velocities", "[colldetect][U16]")
{
	const CollGeom *ball = Geom (IcoM (2, 1.0));
	double h = 0.05;
	for (double y : { 0.0, 0.8 }) {
		CollDetect d;
		std::vector<CollBody> bs;
		Vector wa (0.3, 0.5, -0.2), wb (-0.4, 0.1, 0.6);
		CollBody A = Lin (1, Vector (100, 200, 300), Vector (20, 0, 0), h);
		A.m.w0g = A.m.w1g = wa; A.m.q1.Set (TRotG (Quaternion (), wa*h));
		AddPart (A, ball, VKey (1));
		CollBody B = Lin (2, Vector (102.5, 200 + y, 300), Vector (-20, 0, 0), h);
		B.m.w0g = B.m.w1g = wb;
		Quaternion qb0 = TExp (Vector (0.1, 0.2, 0.3));
		B.m.q0.Set (qb0); B.m.q1.Set (TRotG (qb0, wb*h));
		AddPart (B, ball, VKey (2));
		bs.push_back (A); bs.push_back (B);
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		const CollPairResult *r = FindRes (res, COLL_TOI);
		REQUIRE (r);
		const CollMotion &ma = d.Body (0).m, &mb = d.Body (1).m;
		RequireVec (r->origin, ma.c0, 0.0);
		RequireVec (r->a.c, ma.Pos (r->tau) - ma.c0, 1e-9);
		RequireVec (r->b.c, mb.Pos (r->tau) - ma.c0, 1e-9);
		RequireVec (r->a.v, ma.Vel (r->tau), 1e-12);
		RequireVec (r->b.v, mb.Vel (r->tau), 1e-12);
		CHECK (MatDiff (TMat (r->a.q), TMat (ma.Rot (r->tau))) <= 1e-15);
		CHECK (MatDiff (TMat (r->b.q), TMat (mb.Rot (r->tau))) <= 1e-15);
		Vector dc = (r->a.c - r->b.c).unit ();
		for (int i = 0; i < r->npt; i++) {
			const CollContact &c = r->pt[i];
			CHECK ((c.n & dc) > 0.9);
			CHECK ((c.pA - r->a.c).length () <= 1.0 + 1e-6);
			CHECK ((c.pB - r->b.c).length () <= 1.0 + 1e-6);
			CHECK (d.Owner (*r, i, 0) == VKey (1));
			CHECK (d.Owner (*r, i, 1) == VKey (2));
		}
		// point velocity v + Xc (w, p - c) equals the finite difference of the modelled path
		for (const CollMotion *m : { &ma, &mb }) {
			Vector xb (0.7, -0.4, 0.5);
			double t = r->tau, eps = 1e-6;
			Vector p1 = m->Pos (t + eps) + mul (TMat (m->Rot (t + eps)), xb), p0 = m->Pos (t - eps) + mul (TMat (m->Rot (t - eps)), xb);
			Vector fd = (p1 - p0)/(2.0*eps*h);
			Vector an = m->Vel (t) + Xc (m->Omega (t), mul (TMat (m->Rot (t)), xb));
			CHECK ((fd - an).length () <= 1e-6*an.length ());
		}
	}
	// spin path rates too
	CollMotion s {};
	s.h = 0.2; s.a0ok = true;
	s.w0g = Vector (0, 4, 1); s.w1g = Vector (0.5, 3.5, 1.2);
	Quaternion q0 = TExp (Vector (0.4, -0.3, 0.2));
	s.q0.Set (q0); s.q1.Set (TRotG (q0, Vector (0.2, 3.8, 1.1)*s.h));
	s.Setup ();
	REQUIRE (s.spin);
	for (double t : { 0.1, 0.5, 0.9 }) {
		Vector xb (1.0, 2.0, -0.5);
		double eps = 1e-6;
		Vector fd = (mul (TMat (s.Rot (t + eps)), xb) - mul (TMat (s.Rot (t - eps)), xb))/(2.0*eps*s.h);
		Vector an = Xc (s.Omega (t), mul (TMat (s.Rot (t)), xb));
		CHECK ((fd - an).length () <= 1e-6*an.length ());
	}
}

TEST_CASE("U17 surface velocity of moving parts, part jumps", "[colldetect][U17]")
{
	double h = 1.0/60.0;
	const CollGeom *door = Geom (BoxM (Vector (2.5, 0.05, 1.0), Vector (2.5, 0, 0)));
	const CollGeom *box = Geom (BoxM (Vector (0.3, 0.3, 0.3)));
	SECTION("door turning 0.05 rad/s about its hinge") {
		CollBody A = Still (1, Vector (), h);
		AddPart (A, door, VKey (1));
		CollPartRef &p = A.parts[0];
		p.P1 = CollAffine { RotAxis (Vector (0, 0, 1), 0.05*h), Vector () };
		CollBody B = Still (2, Vector (4.0, 0.05 + 0.3 + 0.03, 0), h);
		AddPart (B, box, VKey (2));
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (A); bs.push_back (B);
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		const CollPairResult *r = FindRes (res, COLL_RESTING);
		REQUIRE (r);
		const CollPartRef &q = d.Body (0).parts[0];
		CHECK_FALSE (q.interp);
		for (int i = 0; i < r->npt; i++) {
			Vector x = r->pt[i].pA, n = r->pt[i].n;
			d.ToPartFrame (*r, i, 0, x, n);
			Vector want = mul (TMat (r->a.q), CollPoseVel (q.P0, q.P1, q.c, 0.0, x))/h;
			RequireVec (r->pt[i].vsA, want, 1e-12);
			double eps = 1e-6;
			Vector fd = mul (TMat (r->a.q), CollApply (CollPoseAt (q.P0, q.P1, q.c, eps), x) - CollApply (CollPoseAt (q.P0, q.P1, q.c, 0.0), x))/(eps*h);
			CHECK ((fd - want).length () <= 1e-6*want.length () + 1e-12);
			CHECK_THAT (want.length (), WithinAbs (0.05*sqrt (x.x*x.x + x.y*x.y), 1e-5*want.length ())); // the path turns about c, not the hinge
			Vector w1 = mul (TMat (d.Body (0).m.Rot (1.0)), CollPoseVel (q.P0, q.P1, q.c, 1.0, x))/h;
			RequireVec (d.SurfaceVel (*r, i, 0, 1.0), w1, 1e-12);
			RequireVec (r->pt[i].vsB, Vector (), 0.0);
		}
	}
	SECTION("child moved between frames: motion over the step") {
		CollBody A = Still (1, Vector (), h);
		AddPart (A, box, VKey (1));
		A.parts[0].P0 = CollTranslate (Vector (-0.1, 0, 0));
		CollBody B = Still (2, Vector (0.63, 0, 0), h);
		AddPart (B, box, VKey (2));
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (A); bs.push_back (B);
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		CHECK (d.Body (0).parts[0].interp);
		CHECK ((d.Body (0).entry & COLLE_MESH) == 0);
		const CollPairResult *r = res.empty () ? nullptr : &res[0];
		REQUIRE (r);
		for (int i = 0; i < r->npt; i++) RequireVec (r->pt[i].vsA, Vector (0.1/h, 0, 0), 1e-9);
	}
	SECTION("snapped animation above 20 m/s: part jump") {
		CollBody A = Still (1, Vector (), h);
		AddPart (A, box, VKey (1));
		A.parts[0].P0 = CollTranslate (Vector (-1.0, 0, 0));
		CollBody B = Still (2, Vector (0.63, 0, 0), h);
		AddPart (B, box, VKey (2));
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (A); bs.push_back (B);
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		const CollPartRef &p = d.Body (0).parts[0];
		CHECK_FALSE (p.interp);
		CHECK (p.disp == 0.0);
		CHECK ((d.Body (0).entry & COLLE_MESH) != 0);
		REQUIRE (!res.empty ());
		for (int i = 0; i < res[0].npt; i++) RequireVec (res[0].pt[i].vsA, Vector (), 0.0);
	}
}

TEST_CASE("U18 Resweep: bounce into a second wall, CORE replaced or reproduced", "[colldetect][U18]")
{
	SECTION("ball bounces off a wall into a second wall in one step") {
		double h = 0.1;
		const CollGeom *ball = Geom (IcoM (2, 0.3));
		const CollGeom *wall = Geom (XForm (PlateM (3, 3), RotAxis (Vector (0, 1, 0), Pi05), Vector ()));
		CollDetect d;
		std::vector<CollBody> bs;
		bs.push_back (Lin (1, Vector (), Vector (30, 0, 0), h)); AddPart (bs.back (), ball, VKey (1));
		bs.push_back (Still (2, Vector (1.0, 0, 0), h)); AddPart (bs.back (), wall, VKey (2));
		bs.push_back (Still (3, Vector (-1.0, 0, 0), h)); AddPart (bs.back (), wall, VKey (3));
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		const CollPairResult *t1 = FindRes (res, COLL_TOI, 0, 1);
		REQUIRE (t1);
		REQUIRE (res.size () == 1);
		double tau = t1->tau;
		CollMotion old = d.Body (0).m;
		CollRestart rs;
		rs.dv = Vector (-60, 0, 0); rs.dwg = Vector ();
		rs.c1 = old.Pos (tau) + Vector (-30, 0, 0)*((1.0 - tau)*h);
		rs.v1 = Vector (-30, 0, 0); rs.w1g = Vector ();
		rs.q1.Set (old.q1);
		std::vector<CollPairResult> out;
		d.Resweep (0, tau, rs, out);
		const CollMotion &m = d.Body (0).m;
		RequireVec (m.Pos (tau), old.Pos (tau), 1e-12);
		CHECK (MatDiff (TMat (m.Rot (tau)), TMat (old.Rot (tau))) <= 1e-15);
		RequireVec (m.Vel (tau), old.Vel (tau) + rs.dv, 1e-12);
		const CollPairResult *t2 = FindRes (out, COLL_TOI, 0, 2);
		REQUIRE (t2);
		CHECK ((t2->flags & COLLF_RESWEEP) != 0);
		CHECK (t2->tau > tau);
		CHECK (FindRes (out, COLL_TOI, 0, 1) == nullptr);
		CHECK_THAT (t2->origin.x, WithinAbs (0.0, 0.0));
		std::printf ("U18: first TOI %.5f, second TOI %.5f\n", tau, t2->tau);
	}
	SECTION("RESTING island with zero impulse replaces the CORE TOI") {
		const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
		const CollGeom *plate = Geom (PlateM (5, 5));
		double h = 0.1;
		CollDetect d;
		std::vector<CollBody> bs;
		RestBox (h, bs, box, plate);
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		REQUIRE (FindRes (res, COLL_TOI));
		const CollMotion &m0 = d.Body (0).m;
		CollRestart rs;
		rs.dv = Vector (); rs.dwg = Vector ();
		rs.c1 = m0.c0; rs.v1 = Vector (); rs.w1g = Vector (); rs.q1.Set (m0.q0);
		std::vector<CollPairResult> out;
		d.Resweep (0, 0.0, rs, out);
		CHECK (FindRes (out, COLL_TOI) == nullptr);
		CHECK (FindRes (out, COLL_SPECULATIVE) == nullptr);
	}
	SECTION("box sliding into a step gets its CORE TOI back") {
		Mesh floor = PlateM (5, 5);
		Merge (floor, BoxM (Vector (0.5, 5, 0.25), Vector (2.0, 0, 0.25)));
		const CollGeom *step = Geom (floor);
		const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
		double h = 0.1;
		CollDetect d;
		std::vector<CollBody> bs;
		Vector c0 (0.5, 0, 0.53), v (5, 0, 0);
		bs.push_back (Lin (1, c0, v, h)); AddPart (bs.back (), box, VKey (1));
		bs.push_back (Still (2, Vector (), h)); AddPart (bs.back (), step, VKey (2));
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		REQUIRE (FindRes (res, COLL_RESTING));
		const CollPairResult *c = FindRes (res, COLL_TOI);
		REQUIRE (c);
		CHECK ((c->flags & COLLF_CORE) != 0);
		const CollMotion &m0 = d.Body (0).m;
		CollRestart rs;
		rs.dv = Vector (); rs.dwg = Vector ();
		rs.c1 = m0.c1; rs.v1 = m0.v1; rs.w1g = Vector (); rs.q1.Set (m0.q1);
		std::vector<CollPairResult> out;
		d.Resweep (0, 0.0, rs, out);
		const CollPairResult *c2 = FindRes (out, COLL_TOI);
		REQUIRE (c2);
		CHECK ((c2->flags & COLLF_CORE) != 0);
		CHECK ((c2->flags & COLLF_RESWEEP) != 0);
		CHECK_THAT (c2->tau, WithinAbs (c->tau, 1e-9));
	}
}

// U19: CA against dense sampling

static const CollGeom *FuzzMesh (Rng &r)
{
	double s = r.U (0.3, 2.0);
	switch ((int)(r.U ()*4.0)) {
	case 0: { double x = s*r.U (0.3, 1.0); double y = s*r.U (0.3, 1.0); double z = s*r.U (0.3, 1.0); return Geom (BoxM (Vector (x, y, z))); }
	case 1: return Geom (TetraM (s*0.6));
	case 2: return Geom (IcoM (1, s*0.6));
	default: { double y = s*r.U (0.3, 1.0); return Geom (PlateM (s, y)); }
	}
}

static void FuzzPart (Rng &r, CollPartRef &p)
{
	Quaternion q;
	r.Q (q);
	CollAffine P0 { TMat (q), r.V (1.0) };
	int kind = (int)(r.U ()*3.0);
	if (kind == 0) { p.P0 = P0; p.P1 = P0; return; }
	if (kind == 1) {
		Vector ax = r.Dir ();
		Quaternion q1 = q * TExp (ax*r.U (0.0, 0.6));
		p.P0 = P0; p.P1 = CollAffine { TMat (q1), P0.t + r.V (0.5) };
		return;
	}
	double sx = r.U (0.5, 2.0); double sy = r.U (0.5, 2.0); double sz = r.U (0.5, 2.0);
	Matrix S (sx, 0, 0, 0, sy, 0, 0, 0, sz);
	p.P0 = CollAffine { TMat (q) * S, P0.t };
	Matrix A1 = p.P0.A;
	for (int i = 0; i < 9; i++) A1.data[i] += r.U (-0.2, 0.2);
	p.P1 = CollAffine { A1, P0.t + r.V (0.5) };
}

static void FuzzMotion (Rng &r, CollMotion &m, double h, const Vector &c0, const Vector &c1)
{
	m.h = h; m.a0ok = true;
	m.c0 = c0; m.c1 = c1;
	Vector vm = (c1 - c0)/h;
	m.v0 = vm + r.V (0.5)*vm.length ();
	m.v1 = vm + r.V (0.5)*vm.length ();
	// accelerations of the Hermite itself, so E_T = 0
	m.a0 = ((c1 - c0)*6.0 - (m.v0*4.0 + m.v1*2.0)*h)/(h*h);
	m.a1 = ((c0 - c1)*6.0 + (m.v0*2.0 + m.v1*4.0)*h)/(h*h);
	Quaternion q0;
	r.Q (q0);
	m.q0.Set (q0);
	if (r.U () < 0.5) {
		Vector ax = r.Dir ();
		Vector dl = ax*r.U (0.0, 0.45);
		m.q1.Set (q0 * TExp (dl));
		Vector w = mul (TMat (q0), dl)/h;
		m.w0g = w; m.w1g = w;
	} else {
		Vector ax = r.Dir ();
		Vector w = ax*(r.U (0.6, 3.0)/h);
		m.q1.Set (TRotG (q0, w*h) * TExp (r.V (0.05)));
		m.w0g = w; m.w1g = w;
	}
	m.Setup ();
}

static void Fuzz (int ncase, uint64_t seed)
{
	Rng r (seed);
	int done = 0, toi = 0, spec = 0, none = 0, viol = 0, maxIt = 0;
	long long its = 0;
	auto t0 = std::chrono::steady_clock::now ();
	while (done < ncase) {
		double h = r.U (0.01, 5.0);
		std::vector<CollBody> bs;
		bs.push_back (CollBody {});
		bs.push_back (CollBody {});
		for (int k = 0; k < 2; k++) {
			CollBody &b = bs[k];
			b.id = k + 1; b.kind = COLLB_DYNAMIC; b.planet = -1;
			AddPart (b, FuzzMesh (r), VKey (k + 1));
			FuzzPart (r, b.parts[0]);
		}
		FuzzMotion (r, bs[0].m, h, Vector (), r.V (1.0));
		Vector dB = r.Dir ();
		Vector sB = dB*r.U (3.0, 8.0);
		FuzzMotion (r, bs[1].m, h, sB, r.V (2.0));
		CollDetect d;
		CollParams p;
		d.Begin (p, h);
		d.AddBody (bs[0]); d.AddBody (bs[1]);
		const CollBody &A = d.Body (0), &B = d.Body (1);
		CollScratch sc;
		const CollPartRef &pa = A.parts[0], &pb = B.parts[0];
		double rs = pa.skin + pb.skin;
		// separated at the exact t0 poses (skin-target CA from 0)
		CollAffine XA0 { TMat (A.m.q0) * pa.P0.A, mul (TMat (A.m.q0), pa.P0.t) };
		CollAffine XB0 { TMat (B.m.q0) * pb.P0.A, mul (TMat (B.m.q0), pb.P0.t) + (B.m.c0 - A.m.c0) };
		if (CollDistance (*pa.geom, XA0, *pb.geom, XB0, 1e6, nullptr, sc) <= rs + p.deltaCt + 1e-6) continue;
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		d.Detect (res, st);
		done++;
		its += st.iterations;
		maxIt = std::max (maxIt, st.iterations);
		double E = CollErrorT (A.m, B.m) + CollErrorR (A.m, A.rmax) + CollErrorR (B.m, B.rmax);
		double Em = std::min (E, p.eMax), mg = (pa.interp ? 0.0 : pa.disp) + (pb.interp ? 0.0 : pb.disp);
		double first = 2.0;
		for (int i = 0; i < 1000; i++) {
			double t = i/999.0;
			if (CollOverlap (*pa.geom, ModelX (A, 0, t, A.m.c0), *pb.geom, ModelX (B, 0, t, A.m.c0), rs, sc)) { first = t; break; }
		}
		const CollPairResult *res0 = res.empty () ? nullptr : &res[0];
		bool bad = false;
		if (res0 && res0->kind == COLL_TOI) {
			toi++;
			double dd = CollDistance (*pa.geom, ModelX (A, 0, res0->tau, A.m.c0), *pb.geom, ModelX (B, 0, res0->tau, A.m.c0), 1e6, nullptr, sc) - rs;
			if (dd - Em - mg > p.deltaToi + 1e-9) bad = true;
			if (res0->tau > first + 1e-3) bad = true;
		} else if (res0 && res0->kind == COLL_SPECULATIVE) {
			spec++;
			if (st.iterations < p.nCa && st.triPairs <= p.nTt) bad = true;
			if (res0->tau > first + 1e-3) bad = true;
		} else {
			none++;
			if (first <= 1.0) bad = true;
		}
		if (bad) {
			viol++;
			UNSCOPED_INFO ("fuzz case " << done << ": h " << h << " kind " << (res0 ? (int)res0->kind : 0) << " tau " << (res0 ? res0->tau : -1.0) << " first " << first);
		}
		CHECK_FALSE (bad);
	}
	double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ();
	std::printf ("U19 fuzz %d cases: TOI %d, SPECULATIVE %d, NONE %d, violations %d; CA iterations mean %.2f max %d; %.0f ms\n",
		ncase, toi, spec, none, viol, (double)its/ncase, maxIt, ms);
	CHECK (viol == 0);
	CHECK (toi > ncase/10);
}

TEST_CASE("U19 CA against dense sampling", "[colldetect][U19]")
{
	Fuzz (200, 19);
}

TEST_CASE("U19 CA against dense sampling, 2000 cases", "[.fuzz][colldetect][U19]")
{
	Fuzz (2000, 1919);
}

// U21, U22, U24, U25: front cache, determinism, touch states, owner keys

static bool LoadAtlantis (CollGeom &G)
{
	CollRestMesh m;
	std::string err;
	if (!CollTestLoadMsh (CollTestPath ("Src/Vessel/Atlantis/Atlantis/Meshes/Atlantis.msh").c_str (), m, &err)) return false;
	std::vector<CollSrcGroup> src;
	for (size_t i = 0; i < m.grp.size (); i++) src.push_back (CollSrcGroup { &m.grp[i], CollSrc { 0, (uint32_t)i, 0, 0, 0 } });
	CollBuildStats st;
	return G.Build (src.data (), src.size (), COLL_WELD_DEFAULT, &st);
}

TEST_CASE("U21 front cache gives identical results and rebuilds after m_f", "[colldetect][U21]")
{
	g_geoms.emplace_back ();
	CollGeom &atl = g_geoms.back ();
	REQUIRE (LoadAtlantis (atl));
	const CollGeom *box = Geom (BoxM (Vector (0.25, 0.25, 0.25)));
	double h = 1.0/60.0;
	CollParams pc, pn;
	pn.mFront = 0.0;
	CollDetect dc, dn;
	int rebuilds = 0, used = 0, nres = 0;
	double maxMot = 0.0, msc = 0.0, msn = 0.0;
	for (int f = 0; f < 600; f++) {
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), &atl, VKey (1));
		Vector c (0, -1.88 + 0.25 + 0.03, -2.0 + 0.001*f);
		bs.push_back (Lin (2, c, Vector (0, 0, 0.06), h)); AddPart (bs.back (), box, VKey (2));
		if (!f) bs.back ().entry = COLLE_NEW;
		std::vector<CollPairResult> rc, rn;
		auto t0 = std::chrono::steady_clock::now ();
		Frame (dc, pc, h, bs, rc);
		auto t1 = std::chrono::steady_clock::now ();
		Frame (dn, pn, h, bs, rn);
		auto t2 = std::chrono::steady_clock::now ();
		msc += std::chrono::duration<double, std::milli> (t1 - t0).count ();
		msn += std::chrono::duration<double, std::milli> (t2 - t1).count ();
		REQUIRE (rc.size () == rn.size ());
		for (size_t i = 0; i < rc.size (); i++) REQUIRE (SameRes (rc[i], rn[i]));
		for (const CollPairResult &r : rc) { dc.Solved (r); nres++; }
		for (const CollPairResult &r : rn) dn.Solved (r);
		const CollPairEntry *e = dc.Pairs ().Find (VKey (1), VKey (2));
		REQUIRE (e);
		REQUIRE (e->frontCut >= 0.0);
		if (e->frontMotion == 0.0) rebuilds++; else used++;
		maxMot = std::max (maxMot, e->frontMotion);
	}
	std::printf ("U21: 600 frames, %d results, cache rebuilt %d times, used %d frames, largest accumulated motion %.4f m; %.3f ms per frame with the cache, %.3f without\n",
		nres, rebuilds, used, maxMot, msc/600.0, msn/600.0);
	CHECK (nres >= 600);
	CHECK (rebuilds >= 600/20);
	CHECK (rebuilds <= 600/10);
	CHECK (maxMot < COLL_M_FRONT);
}

static void DetScene (std::vector<CollBody> &bs, double h)
{
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	const CollGeom *ball = Geom (IcoM (2, 0.6));
	const CollGeom *plate = Geom (PlateM (8, 8));
	bs.clear ();
	CollBody base = Still (9, Vector (), h, COLLB_BASE);
	base.planet = 0;
	AddPart (base, plate, BKey (0, 0, 0));
	bs.push_back (base);
	bs.push_back (Still (1, Vector (0, 0, 0.53), h)); AddPart (bs.back (), box, VKey (1));
	bs.push_back (Lin (2, Vector (3, 0, 0.62), Vector (-60, 0, 0), h)); AddPart (bs.back (), ball, VKey (2));
	bs.push_back (Lin (3, Vector (-3, 2, 1.5), Vector (0, -30, -20), h)); AddPart (bs.back (), ball, VKey (3));
	bs.push_back (Lin (4, Vector (0, 0, 1.55), Vector (0, 0, -0.5), h)); AddPart (bs.back (), box, VKey (4));
	bs.back ().entry = COLLE_NEW;
}

TEST_CASE("U22 pair store determinism", "[colldetect][U22]")
{
	double h = 0.05;
	std::vector<CollBody> bs;
	DetScene (bs, h);
	CollDetect d1, d2, d3;
	std::vector<CollPairResult> r1, r2, r3;
	for (int f = 0; f < 3; f++) {
		Frame (d1, CollParams (), h, bs, r1);
		Frame (d2, CollParams (), h, bs, r2);
		// another insertion order, sorted back (bases first, then by id) before AddBody
		std::vector<int> ord = { 4, 2, 0, 3, 1 };
		std::vector<CollBody> sh;
		for (int i : ord) sh.push_back (bs[i]);
		std::vector<int> idx (sh.size ());
		for (size_t i = 0; i < idx.size (); i++) idx[i] = (int)i;
		std::sort (idx.begin (), idx.end (), [&] (int a, int b) {
			bool ba = sh[a].kind == COLLB_BASE, bb = sh[b].kind == COLLB_BASE;
			if (ba != bb) return ba;
			return sh[a].id < sh[b].id; });
		std::vector<CollBody> so;
		for (int i : idx) so.push_back (sh[i]);
		Frame (d3, CollParams (), h, so, r3);
		REQUIRE (r1.size () == r2.size ());
		REQUIRE (r1.size () == r3.size ());
		REQUIRE (r1.size () >= 3);
		for (size_t i = 0; i < r1.size (); i++) { CHECK (SameRes (r1[i], r2[i])); CHECK (SameRes (r1[i], r3[i])); }
		for (const CollPairResult &r : r1) d1.Solved (r);
		for (const CollPairResult &r : r2) d2.Solved (r);
		for (const CollPairResult &r : r3) d3.Solved (r);
		REQUIRE (d1.Pairs ().Size () == d2.Pairs ().Size ());
		REQUIRE (d1.Pairs ().Size () == d3.Pairs ().Size ());
		for (size_t i = 0; i < d1.Pairs ().Size (); i++) {
			const CollPairEntry &a = d1.Pairs ().At (i), &b = d3.Pairs ().At (i);
			CHECK (a.a == b.a);
			CHECK (a.b == b.b);
			CHECK (a.grace == b.grace);
			CHECK (a.front == b.front);
			REQUIRE (a.touch.size () == b.touch.size ());
			for (size_t k = 0; k < a.touch.size (); k++) CHECK ((a.touch[k].partKeyA == b.touch[k].partKeyA && a.touch[k].partKeyB == b.touch[k].partKeyB && a.touch[k].state == b.touch[k].state));
		}
		for (size_t i = 1; i < d1.Pairs ().Size (); i++) {
			const CollPairEntry &p = d1.Pairs ().At (i-1), &q = d1.Pairs ().At (i);
			CHECK ((p.a < q.a || (p.a == q.a && p.b < q.b)));
		}
	}
}

TEST_CASE("U22 non-finite state skips the body, logged once (D2 10.1)", "[colldetect][U22]")
{
	double h = 1.0/60.0;
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	g_log.clear ();
	g_collLog = LogSink;
	CollDetect d;
	std::vector<CollPairResult> res;
	for (int f = 0; f < 3; f++) {
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), box, VKey (1));
		bs.push_back (Still (2, Vector (0, 0, 1.02), h)); AddPart (bs.back (), box, VKey (2));
		bs.back ().m.v1 = Vector (0, NAN, 0);
		Frame (d, CollParams (), h, bs, res);
		CHECK (res.empty ());
		CHECK (d.Body (1).parts.empty ());
	}
	g_collLog = nullptr;
	int n = 0;
	for (const std::string &s : g_log) if (s.find ("non-finite") != std::string::npos) n++;
	CHECK (n == 1);
}

static const CollTouch *Touch (const CollDetect &d, const CollOwnerKey &a, const CollOwnerKey &b)
{
	const CollPairEntry *e = const_cast<CollDetect &> (d).Pairs ().Find (a, b);
	if (!e || e->touch.empty ()) return nullptr;
	return &e->touch[0];
}

static bool AllFirst (const std::vector<CollPairResult> &res, bool want)
{
	bool any = false;
	for (const CollPairResult &r : res)
		for (int i = 0; i < r.npt; i++) { any = true; if (((r.pt[i].flags & COLLP_FIRST) != 0) != want) return false; }
	return any;
}

TEST_CASE("U24 touch states and FIRST points", "[colldetect][U24]")
{
	double h = 1.0/60.0;
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	CollDetect d0, d1;
	CollDetect *dp = &d0;
	std::vector<CollPairResult> res;
	auto Scene = [&] (double g0, double g1, uint8_t kindA, uint8_t kindB) {
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h, kindA)); AddPart (bs.back (), box, VKey (1));
		bs.push_back (Body (2, Vector (0, 0, 1.0 + g0), Vector (0, 0, (g1 - g0)/h), Vector (0, 0, 1.0 + g1), Vector (0, 0, (g1 - g0)/h), h, kindB)); AddPart (bs.back (), box, VKey (2));
		return bs;
	};
	auto Step = [&] (double g0, double g1, bool solve, uint8_t kA = COLLB_DYNAMIC, uint8_t kB = COLLB_DYNAMIC, const std::vector<CollZone> &z = {}) {
		std::vector<CollBody> bs = Scene (g0, g1, kA, kB);
		CollDetect &d = *dp;
		d.Begin (CollParams (), h);
		for (const CollBody &b : bs) d.AddBody (b);
		d.SetZones (z);
		res.clear ();
		CollFrameStats st;
		d.Detect (res, st);
		if (solve) for (const CollPairResult &r : res) d.Solved (r);
	};
	Step (0.02, 0.02, true);
	CHECK (AllFirst (res, true));
	Step (0.02, 0.02, true);
	CHECK (AllFirst (res, false));
	CHECK (Touch (*dp, VKey (1), VKey (2))->state == COLLT_TOUCHING);
	CollZone z { 0, 1, VKey (1), VKey (2), Vector (0, 0, 0.5), Vector (0, 0, -0.5), 2.0, 2.0 };
	Step (0.04, 0.04, true, COLLB_DYNAMIC, COLLB_DYNAMIC, { z });
	CHECK (res.empty ());
	Step (0.04, 0.04, true);
	CHECK (Touch (*dp, VKey (1), VKey (2))->state == COLLT_LEFT);
	CHECK (AllFirst (res, false));
	Step (0.08, 0.08, true);
	CHECK (res.empty ());
	CHECK (Touch (*dp, VKey (1), VKey (2))->state == COLLT_TOUCHING);
	Step (0.06, 0.02, true);
	CHECK (AllFirst (res, true));
	// SPECULATIVE frame then TOI: both FIRST; results replaced by Resweep (not solved) mark nothing
	CollDetect s;
	auto Fast = [&] (CollDetect &x, int nCa, double z0) {
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), box, VKey (1));
		bs.push_back (Lin (2, Vector (0.3, 0.2, z0), Vector (0, 0, -60), h)); AddPart (bs.back (), box, VKey (2));
		CollParams p;
		p.nCa = nCa;
		x.Begin (p, h);
		for (const CollBody &b : bs) x.AddBody (b);
		res.clear ();
		CollFrameStats st;
		x.Detect (res, st);
	};
	Fast (s, 1, 2.0);
	REQUIRE (FindRes (res, COLL_SPECULATIVE));
	CHECK (AllFirst (res, true));
	for (const CollPairResult &r : res) s.Solved (r);
	CHECK (Touch (s, VKey (1), VKey (2)) == nullptr);
	Fast (s, COLL_N_CA, 2.0);
	REQUIRE (FindRes (res, COLL_TOI));
	CHECK (AllFirst (res, true));
	Fast (s, COLL_N_CA, 2.0);
	CHECK (AllFirst (res, true));
	// kinematic for 100 frames, then woken: not FIRST
	dp = &d1;
	CollDetect &d = d1;
	Step (0.02, 0.02, true);
	Step (0.02, 0.02, true);
	for (int f = 0; f < 100; f++) Step (0.02, 0.02, true, COLLB_LANDED, COLLB_BASE);
	CHECK (Touch (d, VKey (1), VKey (2))->state == COLLT_TOUCHING);
	Step (0.02, 0.02, true);
	CHECK (AllFirst (res, false));
	// time jump keeps the states; broad-phase rejection gives APART; Reset clears
	d.Pairs ().FlushFront ();
	Step (0.02, 0.02, true);
	CHECK (AllFirst (res, false));
	Step (5.0, 5.0, true);
	CHECK (Touch (d, VKey (1), VKey (2))->state == COLLT_TOUCHING);
	Step (5.0, 5.0, true);
	CHECK (Touch (d, VKey (1), VKey (2)) == nullptr);
	Step (0.02, 0.02, true);
	CHECK (AllFirst (res, true));
	Step (0.02, 0.02, true);
	d.Reset ();
	CHECK (d.Pairs ().Size () == 0);
	Step (0.02, 0.02, false);
	CHECK (AllFirst (res, true));
}

TEST_CASE("U25 owner keys", "[colldetect][U25]")
{
	CollOwnerKey v = VKey (7), x = BKey (0, 1, 7), y = BKey (0, 2, 7);
	CHECK_FALSE (v == x);
	CHECK_FALSE (x == y);
	CHECK_FALSE (v == y);
	CHECK (v < x);
	CHECK (x < y);
	CHECK_FALSE (y < x);
	CHECK_FALSE (x < x);
	double h = 1.0/60.0;
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	const CollGeom *obj = Geom (BoxM (Vector (1.0, 1.0, 0.5)));
	// vessel 7 intersects object 7 of base X and touches object 7 of base Y; vessel 8 is apart
	std::vector<CollBody> bs;
	CollBody bx = Still (1, Vector (), h, COLLB_BASE); bx.planet = 0; AddPart (bx, obj, x); bs.push_back (bx);
	CollBody by = Still (2, Vector (2.05, 0, -0.12), h, COLLB_BASE); by.planet = 0; AddPart (by, obj, y); bs.push_back (by);
	bs.push_back (Still (7, Vector (1.0, 0, 0.9), h)); AddPart (bs.back (), box, VKey (7));
	bs.back ().entry = COLLE_NEW;
	CollDetect d;
	std::vector<CollPairResult> res;
	Frame (d, CollParams (), h, bs, res);
	const CollPairEntry *ex = d.Pairs ().Find (VKey (7), x), *ey = d.Pairs ().Find (VKey (7), y);
	REQUIRE (ex);
	CHECK (!ex->grace.empty ());
	CHECK ((!ey || ey->grace.empty ()));
	const CollPairResult *ry = nullptr;
	for (const CollPairResult &r : res) if (r.bodyA == 1) ry = &r;
	REQUIRE (ry);
	CHECK ((ry->flags & COLLF_GRACE) == 0);
	for (const CollPairResult &r : res) d.Solved (r);
	ey = d.Pairs ().Find (VKey (7), y);
	REQUIRE (ey);
	CHECK (ey->touch.size () == 1);
	CHECK (d.Pairs ().Find (VKey (8), x) == nullptr);
	d.Pairs ().PurgeVessel (7);
	CHECK (d.Pairs ().Size () == 0);
}

// code review C-A-b: regression tests

TEST_CASE("U10 manifold points come in feature-key order whatever the gap order", "[colldetect][U10]")
{
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	const CollGeom *plate = Geom (PlateM (5, 5));
	double h = 1.0/60.0;
	std::vector<std::vector<int>> corner;
	std::vector<int> lowest;
	CollDetect d;
	for (int k = 0; k < 6; k++) {
		// box resting with a micro tilt that makes a different corner the deepest each frame
		double ph = Pi*k/3.0;
		Quaternion q = TExp (Vector (cos (ph), sin (ph), 0)*2e-6);
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (0, 0, 0.54), h)); AddPart (bs.back (), box, VKey (1));
		bs.back ().m.q0.Set (q); bs.back ().m.q1.Set (q);
		bs.push_back (Still (2, Vector (), h)); AddPart (bs.back (), plate, VKey (2));
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		const CollPairResult *r = FindRes (res, COLL_RESTING);
		REQUIRE (r);
		REQUIRE (r->npt == 4);
		std::vector<int> cc;
		int low = 0;
		for (int i = 0; i < r->npt; i++) {
			cc.push_back ((r->pt[i].pA.x > 0.0) + 2*(r->pt[i].pA.y > 0.0));
			if (r->pt[i].gap < r->pt[low].gap) low = i;
		}
		corner.push_back (cc); lowest.push_back (cc[low]);
	}
	std::printf ("U10 order: deepest corner per frame %d %d %d %d %d %d, point order of frame 0 %d %d %d %d\n",
		lowest[0], lowest[1], lowest[2], lowest[3], lowest[4], lowest[5], corner[0][0], corner[0][1], corner[0][2], corner[0][3]);
	CHECK (std::count (lowest.begin (), lowest.end (), lowest[0]) < 6); // the gap order does change
	for (size_t k = 1; k < corner.size (); k++) CHECK (corner[k] == corner[0]); // the same corner in the same slot every frame
}

TEST_CASE("U18 Split carries D3's phase-2 acceleration: a held box keeps E_T", "[colldetect][U18]")
{
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	const CollGeom *plate = Geom (PlateM (5, 5));
	for (double h : { 1.0/60.0, 0.1, 0.17, 0.25 }) {
		CollDetect d;
		std::vector<CollBody> bs;
		RestBox (h, bs, box, plate);
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		double E0 = CollErrorT (d.Body (0).m, d.Body (1).m);
		const CollMotion &m0 = d.Body (0).m;
		CollRestart rs;                              // D3 holds the box: no phase-1 jump, resting t1 state (support force m g over the step)
		rs.dv = Vector (); rs.dwg = Vector ();
		rs.c1 = m0.c0; rs.v1 = Vector (); rs.w1g = Vector (); rs.q1.Set (m0.q0);
		std::vector<CollPairResult> out;
		d.Resweep (0, 0.0, rs, out);
		const CollMotion &m1 = d.Body (0).m;
		double E1 = CollErrorT (m1, d.Body (1).m);
		uint32_t fl = 0;
		for (const CollPairResult &r : out) fl |= r.flags;
		std::printf ("U18 split h %.4f: E_T before %.2e m, after the split %.2e m, a0 (%.3f %.3f %.3f)\n", h, E0, E1, m1.a0.x, m1.a0.y, m1.a0.z);
		CHECK (E1 <= E0 + 1e-12);
		CHECK (E1 < 1e-9);
		CHECK ((fl & COLLF_INACCURATE) == 0);
		RequireVec (m1.a0, Vector (), 1e-9);
		RequireVec (m1.a1, Vector (), 1e-9);
	}
	// a split before the interval start is clamped, never extrapolated backwards
	CollMotion m;
	m.c0 = Vector (); m.v0 = Vector (1, 0, 0); m.c1 = Vector (0.1, 0, 0); m.v1 = Vector (1, 0, 0); m.h = 0.1; m.a0ok = true;
	m.Setup ();
	CollRestart rs;
	rs.dv = Vector (); rs.dwg = Vector (); rs.c1 = m.c1; rs.v1 = m.v1; rs.w1g = Vector (); rs.q1.Set (m.q1);
	CollMotion s = m.Split (0.5, rs);
	s.Setup ();
	CollMotion t = s.Split (0.2, rs);
	CHECK (t.ta == 0.5);
	RequireVec (t.c0, m.Pos (0.5), 1e-15);
}

TEST_CASE("U18 Resweep probe of a touching pair that does not approach has no side effects", "[colldetect][U18]")
{
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	const CollGeom *plate = Geom (PlateM (5, 5));
	double h = 0.1, H = h;
	// box on a parabola: raw gap 0.1 m at t0, t1; 0.06 m at rest, tau 0.5 (touch band, not CA target)
	Vector v0 (0, 0, -0.16/H), a (0, 0, 0.32/(H*H));
	std::vector<CollBody> bs;
	bs.push_back (Body (1, Vector (0, 0, 0.6), v0, Vector (0, 0, 0.6), -v0, h)); AddPart (bs.back (), box, VKey (1));
	bs.back ().m.a0 = a; bs.back ().m.a1 = a;
	bs.push_back (Still (2, Vector (), h)); AddPart (bs.back (), plate, VKey (2));
	CollDetect d;
	std::vector<CollPairResult> res;
	Frame (d, CollParams (), h, bs, res);
	CHECK (res.empty ());
	RequireVec (d.Body (0).m.Pos (0.5), Vector (0, 0, 0.56), 1e-12);
	CollRestart rs;
	rs.dv = Vector (); rs.dwg = Vector ();
	rs.c1 = d.Body (0).m.c1; rs.v1 = d.Body (0).m.v1; rs.w1g = Vector (); rs.q1.Set (d.Body (0).m.q1);
	std::vector<CollPairResult> out;
	d.Resweep (0, 0.5, rs, out);
	CHECK (out.empty ());
	const CollPairEntry *e = d.Pairs ().Find (VKey (1), VKey (2));
	CHECK ((!e || e->lastKind == COLL_NONE));
	std::vector<CollEnd> end { CollEnd { Vector (0, 0, 1000), Vector (), Vector (), d.Body (0).rmax, 0.0 }, CollEnd { Vector (), Vector (), Vector (), d.Body (1).rmax, 0.0 } };
	CollWarpInput w;
	d.LookAhead (end, w);
	CHECK (w.hContact > 1e99);                       // the discarded probe did not mark the pair approaching
}

// one base of n box buildings (half size 10 m) on a 100 m grid, vessel boxes of half size 1 m
static CollBody BaseScene (int n, const CollGeom *bld, const Vector &at, double h)
{
	CollBody base = Still (1, at, h, COLLB_BASE);
	base.planet = 0;
	for (int k = 0; k < n; k++) {
		AddPart (base, bld, BKey (0, 0, k));
		base.parts.back ().P0 = base.parts.back ().P1 = CollTranslate (Vector ((k % 40)*100.0, (k/40)*100.0, 0));
	}
	return base;
}

TEST_CASE("U14 look-ahead tests a base as a whole before its buildings", "[colldetect][U14]")
{
	const CollGeom *bld = Geom (BoxM (Vector (10, 10, 10)));
	const CollGeom *ves = Geom (BoxM (Vector (1, 1, 1)));
	double h = 1.0/60.0, Re = 6.371e6;
	CollBody base = BaseScene (1000, bld, Vector (0, 0, Re), h);
	base.m.w0g = base.m.w1g = Vector (0, 0, 7.2921159e-5);
	// 50 vessels in LEO, and vessel 99 closing on the farthest building at 300 m/s
	std::vector<CollBody> bs { base };
	for (int i = 0; i < 50; i++) { bs.push_back (Lin (10 + i, Vector (i*5000.0, 0, Re + 400e3), Vector (7670, 0, 0), h)); AddPart (bs.back (), ves, VKey (10 + i)); }
	Vector far (39*100.0, 24*100.0, Re);
	bs.push_back (Lin (99, far + Vector (0, 0, 25.0), Vector (0, 0, -300), h)); AddPart (bs.back (), ves, VKey (99));
	CollDetect d;
	std::vector<CollPairResult> res;
	Frame (d, CollParams (), h, bs, res);
	std::vector<CollEnd> end;
	for (int i = 0; i < d.nBody (); i++) { const CollBody &b = d.Body (i); end.push_back (CollEnd { b.m.c1, b.m.v1, b.m.a1, b.rmax, 0.0 }); }
	end.back ().c1 = far + Vector (0, 0, 25.0);     // building sphere 17.3 m, vessel 1.75 m: reached within the look-ahead span (10 m)
	CollWarpInput w;
	d.LookAhead (end, w);
	CHECK (w.hContact == COLL_H_CONTACT);
	CHECK (w.idContact[0] == 1);
	CHECK (w.idContact[1] == 99);
	end.back ().c1 = far + Vector (0, 0, 35.0);     // 10 m higher: out of reach
	CollWarpInput w2;
	d.LookAhead (end, w2);
	CHECK (w2.hContact > 1e99);
	// cost: one sphere test per orbiting vessel and base (50 vessels in orbit, 0.5 ms budget per pass)
	end.pop_back ();
	std::vector<double> ms;
	for (int k = 0; k < 21; k++) {
		CollWarpInput wk;
		auto t0 = std::chrono::steady_clock::now ();
		d.LookAhead (end, wk);
		ms.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ());
	}
	std::sort (ms.begin (), ms.end ());
	std::printf ("U14 look-ahead, 1000 buildings, 50 vessels in orbit: median %.4f ms\n", ms[10]);
	CHECK (ms[10] < 1.0);                            // about 0.02 ms at -O2 and 0.3 ms at -O0; the per-building loop took 2.5-3.2 ms at -O2
}

TEST_CASE("U21 Detect near a base grows linearly with its buildings", "[colldetect][U21]")
{
	const CollGeom *bld = Geom (BoxM (Vector (10, 10, 10)));
	const CollGeom *ves = Geom (BoxM (Vector (1, 1, 1)));
	double h = 1.0/60.0;
	double t[2] = { 0, 0 };
	int ns[2] = { 200, 2000 }, nres[2] = { 0, 0 };
	for (int s = 0; s < 2; s++) {
		CollBody base = BaseScene (ns[s], bld, Vector (), h);
		CollDetect d;
		std::vector<double> ms;
		for (int f = 0; f < 60; f++) {
			Vector c (0, 0, 11.045), g (0, 0, -9.81);
			std::vector<CollBody> bs { base };
			bs.push_back (Body (10, c, Vector (), c + g*(0.5*h*h), g*h, h)); AddPart (bs.back (), ves, VKey (10));
			bs.back ().m.a0 = bs.back ().m.a1 = g;
			std::vector<CollPairResult> res;
			auto t0 = std::chrono::steady_clock::now ();
			Frame (d, CollParams (), h, bs, res);
			for (const CollPairResult &r : res) d.Solved (r);
			ms.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ());
			nres[s] = (int)res.size ();
		}
		std::sort (ms.begin (), ms.end ());
		t[s] = ms[30];
	}
	std::printf ("U21 resting on building 0: 200 buildings %.4f ms, 2000 buildings %.4f ms per frame (ratio %.1f)\n", t[0], t[1], t[1]/t[0]);
	CHECK (nres[0] >= 1);
	CHECK (nres[1] == nres[0]);
	CHECK (t[1] < 20.0*t[0]);                        // linear: 10x; the quadratic bookkeeping before review C-A-b 11 gave 37x
}

TEST_CASE("U21 front cache built while a zone prunes stays exact after the zone ends", "[colldetect][U21]")
{
	double h = 1.0/60.0;
	std::vector<CollBody> bs;
	ZoneScene (bs, h);
	CollZone z { 0, 1, VKey (1), VKey (2), Vector (1.05, 0, 0), Vector (-1.05, 0, 0), 1.5, 1.5 };
	CollParams pc, pn;
	pn.mFront = 0.0;
	CollDetect dc, dn;
	for (int f = 0; f < 3; f++) {
		std::vector<CollZone> zl;
		if (f == 0) zl.push_back (z);
		std::vector<CollPairResult> r[2];
		for (int k = 0; k < 2; k++) {
			CollDetect &d = k ? dn : dc;
			d.Begin (k ? pn : pc, h);
			for (const CollBody &b : bs) d.AddBody (b);
			d.SetZones (zl);
			CollFrameStats st;
			d.Detect (r[k], st);
			for (const CollPairResult &x : r[k]) d.Solved (x);
		}
		int n[2] = { 0, 0 };
		for (int k = 0; k < 2; k++) for (const CollPairResult &x : r[k]) n[k] += x.npt;
		std::printf ("U21 zone frame %d: points with the front cache %d, without %d\n", f, n[0], n[1]);
		REQUIRE (r[0].size () == r[1].size ());
		for (size_t i = 0; i < r[0].size (); i++) CHECK (SameRes (r[0][i], r[1][i]));
	}
}

TEST_CASE("U22 non-finite state is logged once per episode", "[colldetect][U22]")
{
	double h = 1.0/60.0;
	const CollGeom *box = Geom (BoxM (Vector (0.5, 0.5, 0.5)));
	g_log.clear ();
	g_collLog = LogSink;
	CollDetect d;
	std::vector<CollPairResult> res;
	const bool bad[6] = { true, true, false, true, true, false };
	for (int f = 0; f < 6; f++) {
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), box, VKey (1));
		bs.push_back (Still (2, Vector (0, 0, 3.0), h)); AddPart (bs.back (), box, VKey (2));
		if (bad[f]) bs.back ().m.v1 = Vector (0, NAN, 0);
		Frame (d, CollParams (), h, bs, res);
		CHECK (d.Body (1).parts.empty () == bad[f]);
	}
	g_collLog = nullptr;
	int n = 0;
	for (const std::string &s : g_log) if (s.find ("non-finite") != std::string::npos) n++;
	CHECK (n == 2);
}

TEST_CASE("U19 CA from split motions (Resweep) against dense sampling, 2000 cases", "[.fuzz][colldetect][U19]")
{
	Rng r (77);
	int done = 0, toi = 0, spec = 0, none = 0, viol = 0;
	while (done < 2000) {
		double h = r.U (0.01, 5.0);
		std::vector<CollBody> bs (2);
		for (int k = 0; k < 2; k++) {
			CollBody &b = bs[k];
			b.id = k + 1; b.kind = COLLB_DYNAMIC; b.planet = -1;
			AddPart (b, FuzzMesh (r), VKey (k + 1));
			FuzzPart (r, b.parts[0]);
		}
		FuzzMotion (r, bs[0].m, h, Vector (), r.V (1.0));
		Vector sB = r.Dir ()*r.U (3.0, 8.0);
		FuzzMotion (r, bs[1].m, h, sB, r.V (2.0));
		CollDetect d;
		CollParams p;
		d.Begin (p, h);
		d.AddBody (bs[0]); d.AddBody (bs[1]);
		// split one or both bodies at random taus with a velocity jump and a new end state
		double ts = r.U (0.05, 0.8);
		int which = (int)(r.U ()*3.0);
		std::vector<CollPairResult> out;
		for (int k = 0; k < 2; k++) {
			if (which != 2 && which != k) continue;
			const CollMotion &m = d.Body (k).m;
			double tk = which == 2 && k == 0 ? r.U (0.0, ts) : ts;
			CollRestart rs;
			rs.dv = r.V (1.0)*((m.c1 - m.c0).length ()/h + 1.0);
			rs.dwg = r.V (0.3/h);
			Quaternion q1; r.Q (q1);
			Vector p0 = m.Pos (tk), v0 = m.Vel (tk) + rs.dv;
			double rem = (1.0 - tk)*h;
			rs.v1 = v0 + r.V (0.2)*(v0.length () + 1.0);
			rs.c1 = p0 + (v0 + rs.v1)*(0.5*rem);
			rs.q1.Set (q1);
			rs.w1g = r.V (0.3/h);
			out.clear ();
			d.Resweep (k, tk, rs, out);
		}
		const CollBody &A = d.Body (0), &B = d.Body (1);
		double s0 = std::max (A.m.ta, B.m.ta);
		CollScratch sc;
		const CollPartRef &pa = A.parts[0], &pb = B.parts[0];
		double rs2 = pa.skin + pb.skin;
		double E = CollErrorT (A.m, B.m) + CollErrorR (A.m, A.rmax) + CollErrorR (B.m, B.rmax);
		double Em = std::min (E, p.eMax), mg = (pa.interp ? 0.0 : pa.disp) + (pb.interp ? 0.0 : pb.disp);
		if (CollDistance (*pa.geom, ModelX (A, 0, s0, A.m.c0), *pb.geom, ModelX (B, 0, s0, A.m.c0), 1e6, nullptr, sc) <= rs2 + p.deltaCt + Em + mg + 1e-6) continue;
		done++;
		double first = 2.0;
		for (int i = 0; i < 2000; i++) {
			double t = s0 + (1.0 - s0)*i/1999.0;
			if (CollOverlap (*pa.geom, ModelX (A, 0, t, A.m.c0), *pb.geom, ModelX (B, 0, t, A.m.c0), rs2, sc)) { first = t; break; }
		}
		const CollPairResult *r0 = out.empty () ? nullptr : &out[0];
		bool bad = false;
		if (r0 && r0->kind == COLL_TOI) {
			toi++;
			double dd = CollDistance (*pa.geom, ModelX (A, 0, r0->tau, A.m.c0), *pb.geom, ModelX (B, 0, r0->tau, A.m.c0), 1e6, nullptr, sc) - rs2;
			bad = dd - Em - mg > p.deltaToi + 1e-9 || r0->tau > first + 1e-3 || r0->tau < s0;
		} else if (r0 && r0->kind == COLL_SPECULATIVE) { spec++; bad = r0->tau > first + 1e-3; }
		else { none++; bad = first <= 1.0; }
		if (bad) { viol++; UNSCOPED_INFO ("resweep fuzz case " << done << ": h " << h << " split " << which << " at " << s0 << " first " << first); }
		CHECK_FALSE (bad);
	}
	std::printf ("U19 re-sweep fuzz 2000 cases: TOI %d, SPECULATIVE %d, NONE %d, violations %d\n", toi, spec, none, viol);
	CHECK (toi > 100);
}

TEST_CASE("U19 held parts (disp <= delta_ct) against the true part path, 2000 cases", "[.fuzz][colldetect][U19]")
{
	Rng r (5);
	int done = 0, toi = 0, spec = 0, none = 0, viol = 0, held = 0;
	auto TrueX = [] (const CollBody &B, double tau, const Vector &org) {
		const CollPartRef &p = B.parts[0];
		CollAffine P = CollPoseAt (p.P0, p.P1, p.c, tau);
		Matrix R = TMat (B.m.Rot (tau));
		return CollAffine { R * P.A, mul (R, P.t) + (B.m.Pos (tau) - org) };
	};
	while (done < 2000) {
		double h = r.U (0.01, 2.0);
		std::vector<CollBody> bs (2);
		for (int k = 0; k < 2; k++) {
			CollBody &b = bs[k];
			b.id = k + 1; b.kind = COLLB_DYNAMIC; b.planet = -1;
			AddPart (b, FuzzMesh (r), VKey (k + 1));
			Quaternion q; r.Q (q);
			CollPartRef &p = b.parts[0];
			p.P0 = CollAffine { TMat (q), r.V (1.0) };
			if (r.U () < 0.5) p.P1 = CollAffine { TMat (q * TExp (r.Dir ()*r.U (0.0, 0.012))), p.P0.t + r.V (0.008) };
			else { Matrix A1 = p.P0.A; for (int i = 0; i < 9; i++) A1.data[i] += r.U (-0.006, 0.006); p.P1 = CollAffine { A1, p.P0.t + r.V (0.008) }; }
		}
		FuzzMotion (r, bs[0].m, h, Vector (), r.V (1.0));
		FuzzMotion (r, bs[1].m, h, r.Dir ()*r.U (3.0, 8.0), r.V (2.0));
		CollDetect d;
		CollParams p;
		d.Begin (p, h);
		d.AddBody (bs[0]); d.AddBody (bs[1]);
		const CollBody &A = d.Body (0), &B = d.Body (1);
		const CollPartRef &pa = A.parts[0], &pb = B.parts[0];
		if (pa.interp || pb.interp) continue;
		CollScratch sc;
		double rs = pa.skin + pb.skin;
		if (CollDistance (*pa.geom, TrueX (A, 0.0, A.m.c0), *pb.geom, TrueX (B, 0.0, A.m.c0), 1e6, nullptr, sc) <= rs + p.deltaCt + 1e-6) continue;
		held += (pa.disp > 0.0) + (pb.disp > 0.0);
		std::vector<CollPairResult> res;
		CollFrameStats st {};
		d.Detect (res, st);
		done++;
		double first = 2.0;
		for (int i = 0; i < 2000; i++) {
			double t = i/1999.0;
			if (CollOverlap (*pa.geom, TrueX (A, t, A.m.c0), *pb.geom, TrueX (B, t, A.m.c0), rs, sc)) { first = t; break; }
		}
		const CollPairResult *r0 = res.empty () ? nullptr : &res[0];
		bool bad = false;
		if (r0 && r0->kind == COLL_TOI) { toi++; bad = r0->tau > first + 1e-3; }
		else if (r0 && r0->kind == COLL_SPECULATIVE) { spec++; bad = r0->tau > first + 1e-3; }
		else { none++; bad = first <= 1.0; }
		if (bad) { viol++; UNSCOPED_INFO ("held fuzz case " << done << ": h " << h << " first " << first << " disp " << pa.disp << " " << pb.disp); }
		CHECK_FALSE (bad);
	}
	std::printf ("U19 held-part fuzz 2000 cases (%d moving held parts): TOI %d, SPECULATIVE %d, NONE %d, violations %d\n", held, toi, spec, none, viol);
	CHECK (held > 1000);
}

TEST_CASE("fix1 5.5: crossing triangles of a sliding sunk box take the plate's normal, not the slide direction", "[colldetect][U16]")
{
	const double h = 1.0/60.0;
	const CollGeom *plate = Geom (BoxM (Vector (5, 5, 2), Vector (0, 0, -2))), *box = Geom (BoxM (Vector (1, 1, 1)));
	for (double sink : { 0.01, 1.1 })                                     // 1.1: deeper than half the box, the shallower side is the wrong one (review 2 M1)
	for (const Vector &v : { Vector (3, 0, 0), Vector (-2, 1.5, 0), Vector (0, 0, 0) }) {
		std::vector<CollBody> bs;
		bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), plate, VKey (1));
		bs.push_back (Lin (2, Vector (0, 0, 1 - sink), v, h)); AddPart (bs.back (), box, VKey (2));
		g_log.clear (); g_collLog = LogSink;
		CollDetect d;
		std::vector<CollPairResult> res;
		Frame (d, CollParams (), h, bs, res);
		g_collLog = nullptr;
		int deg = 0, up = 0;
		for (const CollPairResult &r : res)
			for (int i = 0; i < r.npt; i++) {
				const CollContact &c = r.pt[i];
				if (!(c.flags & COLLP_DEGENERATE)) continue;
				deg++;
				Vector out = bs[r.bodyA].id == 1 ? Vector (0, 0, -1) : Vector (0, 0, 1);   // plate surface normal, n points from B to A
				if (std::fabs (c.n.z) > 1.0 - 1e-9 && (c.n & out) > 0.0) up++;
			}
		std::printf ("fix1 5.5: box sunk %g sliding at (%g %g %g) m/s: %d degenerate points, %d along the plate normal\n", sink, v.x, v.y, v.z, deg, up);
		CHECK (deg > 0);
		CHECK (up == deg);
	}
}

TEST_CASE("fix2: ToPartFrame of a pose interpolated through a mirroring scale (det 0) stays finite", "[colldetect][fix2]")
{
	const double h = 1.0;
	const CollGeom *box = Geom (BoxM (Vector (0.3, 0.3, 0.3)));
	CollBody A = Still (1, Vector (), h);
	AddPart (A, box, VKey (1));
	Matrix M (-1, 0, 0, 0, 1, 0, 0, 0, 1);
	A.parts[0].P1 = CollAffine { M, Vector () };
	CollBody B = Still (2, Vector (0, 0.3 + 0.3 + 0.02, 0), h);
	AddPart (B, box, VKey (2));
	CollParams prm;
	prm.vPartMax = 1e9;
	CollDetect d;
	std::vector<CollBody> bs { A, B };
	std::vector<CollPairResult> res;
	Frame (d, prm, h, bs, res);
	REQUIRE (d.Body (0).parts[0].interp);
	REQUIRE (!res.empty ());
	CollPairResult r = res[0];
	r.kind = COLL_TOI; r.tau = 0.5;                          // P(0.5) = diag (0, 1, 1)
	for (int i = 0; i < r.npt; i++) {
		Vector p = r.pt[i].pA, n = r.pt[i].n;
		d.ToPartFrame (r, i, 0, p, n);
		CHECK ((std::isfinite (p.x) && std::isfinite (p.y) && std::isfinite (p.z)));
		CHECK ((std::isfinite (n.x) && std::isfinite (n.y) && std::isfinite (n.z)));
		Vector v = d.SurfaceVel (r, i, 0, 0.5);
		CHECK ((std::isfinite (v.x) && std::isfinite (v.y) && std::isfinite (v.z)));
	}
}

TEST_CASE("fix2 S6: a scoped leaf pair bigger than the release cap keeps its GRACE scope", "[colldetect][fix2]")
{
	const double h = 1.0/60.0;
	Mesh a;                                                  // one leaf: 16 tiny triangles and one big one, all with centroid 0
	for (int k = 1; k <= 16; k++) {
		double s = k/256.0;
		int b = (int)a.v.size ();
		a.v.push_back (Vector (-s, -s, 0)); a.v.push_back (Vector (2*s, -s, 0)); a.v.push_back (Vector (-s, 2*s, 0));
		Tri (a, b, b + 1, b + 2);
	}
	int b0 = (int)a.v.size ();
	a.v.push_back (Vector (-3, -1, 0)); a.v.push_back (Vector (3, -1, 0)); a.v.push_back (Vector (0, 2, 0));
	Tri (a, b0, b0 + 1, b0 + 2);
	Mesh m;                                                  // crosses the big triangle near x 2.5
	m.v = { Vector (2.5, -0.8, -1), Vector (2.5, -0.7, 1), Vector (2.5, -0.9, 1) };
	Tri (m, 0, 1, 2);
	CollGeom big = *Geom (a);                                // one root leaf of 17 (a depth-capped leaf), the big triangle checked last
	big.node.resize (1);
	big.node[0].first = 0; big.node[0].count = 17;
	for (uint32_t i = 0; i < 17; i++) big.perm[i] = i;
	const CollGeom *ga = &big, *gb = Geom (m);
	std::vector<CollBody> bs;
	bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), ga, VKey (1));
	bs.push_back (Still (2, Vector (), h)); AddPart (bs.back (), gb, VKey (2));
	bs.back ().entry = COLLE_NEW;
	CollDetect d;
	std::vector<CollPairResult> res;
	Frame (d, CollParams (), h, bs, res);
	const CollPairEntry *e = d.Pairs ().Find (VKey (1), VKey (2));
	REQUIRE (e);
	REQUIRE (e->grace.size () == 1);
	bs.back ().entry = 0;
	for (int f = 0; f < 3; f++) Frame (d, CollParams (), h, bs, res);
	e = d.Pairs ().Find (VKey (1), VKey (2));
	REQUIRE (e);
	CHECK (e->grace.size () == 1);
	bs.back () = Still (2, Vector (10, 0, 0), h); AddPart (bs.back (), gb, VKey (2));  // leaves 10 m apart: bounding spheres separate, the scope is released
	for (int f = 0; f < 2; f++) Frame (d, CollParams (), h, bs, res);
	e = d.Pairs ().Find (VKey (1), VKey (2));
	CHECK ((!e || e->grace.empty ()));
}

TEST_CASE("fix2: entry check keeps at most 4096 raw intersections per part pair", "[colldetect][fix2]")
{
	const double h = 1.0/60.0;
	auto grid = [] (int n, double off) {                     // n x n quads in z 0, two triangles each
		Mesh m;
		for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) m.v.push_back (Vector (i*0.1 + off, j*0.1 + off, 0));
		for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) { int v = j*(n + 1) + i; Tri (m, v, v + 1, v + n + 2); Tri (m, v, v + n + 2, v + n + 1); }
		return m;
	};
	const CollGeom *ga = Geom (grid (48, 0.0)), *gb = Geom (grid (48, 0.03));
	long long want = 0;                                      // coplanar overlapping grids: brute force count of crossing triangle pairs
	for (const CollTri &ta : ga->tri) {
		Vector va[3] = { ga->vtx[ta.v[0]], ga->vtx[ta.v[1]], ga->vtx[ta.v[2]] };
		for (const CollTri &tb : gb->tri) {
			Vector vb[3] = { gb->vtx[tb.v[0]], gb->vtx[tb.v[1]], gb->vtx[tb.v[2]] }, p, q;
			if (std::fabs (va[0].x - vb[0].x) > 0.3 || std::fabs (va[0].y - vb[0].y) > 0.3) continue;
			if (CollTriTriDistance (va, vb, p, q) <= 0.0) want++;
		}
	}
	REQUIRE (want > 4*(long long)COLL_ENTRY_RAW_MAX);
	std::vector<CollBody> bs;
	bs.push_back (Still (1, Vector (), h)); AddPart (bs.back (), ga, VKey (1));
	bs.push_back (Still (2, Vector (), h)); AddPart (bs.back (), gb, VKey (2));
	bs.back ().jump1 = true;                                 // entry check only, its triangle pairs reported
	CollDetect d;
	std::vector<CollPairResult> res;
	CollFrameStats st = Frame (d, CollParams (), h, bs, res);
	const CollPairEntry *e = d.Pairs ().Find (VKey (1), VKey (2));
	REQUIRE (e);
	CHECK (!e->grace.empty ());
	std::printf ("fix2 entry cap: %lld crossing pairs, %d triangle pairs tested, %zu scoped leaf pairs\n", want, st.triPairs, e->grace.size ());
	CHECK (st.triPairs < want);
}
