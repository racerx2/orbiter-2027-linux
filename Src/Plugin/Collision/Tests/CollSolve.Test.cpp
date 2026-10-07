// not upstream: unit tests for Src/Orbiter/CollSolve (D3 U1-U12, U14 island, U15, U16 island, U17)
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "CollSolve.h"

namespace {

// deterministic generator (splitmix64); tests never call rand()
struct Rng {
	uint64_t s;
	explicit Rng (uint64_t seed) : s (seed) {}
	double U ()
	{
		uint64_t z = (s += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30))*0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27))*0x94D049BB133111EBull;
		z ^= z >> 31;
		return (double)(z >> 11)*(1.0/9007199254740992.0);
	}
	double U (double a, double b) { return a + (b - a)*U (); }
	Vector V (double r) { return Vector (U (-r, r), U (-r, r), U (-r, r)); }
	Vector Unit () { for (;;) { Vector v = V (1.0); double l = v.length (); if (l > 0.1 && l <= 1.0) return v/l; } }
	Quaternion Q () { Quaternion q (U (-1, 1), U (-1, 1), U (-1, 1), U (-1, 1)); q.normalise (); return q; }
};

std::vector<std::string> g_log;
void LogSink (int level, const char *msg) { g_log.push_back (std::to_string (level) + " " + msg); }
struct LogCapture {
	LogCapture () { g_log.clear (); g_collLog = LogSink; }
	~LogCapture () { g_collLog = nullptr; }
	int Count (const char *s) const { int n = 0; for (const std::string &l : g_log) if (l.find (s) != std::string::npos) n++; return n; }
};

Matrix RotOf (const Quaternion &q) { Matrix R; R.Set (q); return R; }

// world rotation for a world rotation vector: d/dt (E r) = Xc (phi, E r) per unit time (proto expK)
Matrix ExpW (const Vector &phi)
{
	Matrix E = IMatrix ();
	double th = phi.length ();
	if (th < 1e-300) return E;
	Vector u = phi/th;
	for (int j = 0; j < 3; j++) {
		Vector e (j == 0, j == 1, j == 2), c1 = Xc (u, e), c2 = Xc (u, c1);
		Vector col = e + c1*std::sin (th) + c2*(1.0 - std::cos (th));
		E(0,j) = col.x; E(1,j) = col.y; E(2,j) = col.z;
	}
	return E;
}

// inverse of ExpW (proto logK)
Vector LogW (const Matrix &R)
{
	double c = std::max (-1.0, std::min (1.0, (R(0,0) + R(1,1) + R(2,2) - 1.0)*0.5)), th = std::acos (c);
	if (th < 1e-12) return Vector ();
	Vector u (R(2,1) - R(1,2), R(0,2) - R(2,0), R(1,0) - R(0,1));
	return u*(-th/(2.0*std::sin (th)));
}

// dynamic body whose t1 state follows its tau state under constant acceleration acc over tr
CollSBody Dyn (double m, const Vector &pmi, const Vector &xt, const Vector &vt, const Matrix &R, const Vector &wb, double tr, const Vector &acc = Vector ())
{
	CollSBody b {};
	b.dyn = true; b.m = m; b.pmi = pmi; b.Rt = R; b.R1 = R;
	b.xt = xt; b.vt = vt; b.wt = mul (R, wb);
	b.x1 = xt + vt*tr + acc*(0.5*tr*tr); b.v1 = vt + acc*tr; b.wb1 = wb;
	return b;
}

CollSBody Kin (const Vector &xt)
{
	CollSBody b {};
	b.dyn = false; b.m = 1.0; b.pmi = Vector (1, 1, 1); b.Rt = IMatrix (); b.R1 = IMatrix (); b.xt = xt; b.x1 = xt;
	return b;
}

CollSContact Con (int a, int b, const Vector &p, const Vector &n, uint8_t kind = COLL_TOI, double mu = COLL_MU, double e0 = COLL_E0, double vy = COLL_VY)
{
	CollSContact c {};
	c.a = a; c.b = b; c.p = p; c.n = n; c.n2 = n; c.kind = kind; c.mu = mu; c.e0 = e0; c.vy = vy;
	return c;
}

// rigid state of one body for the t1 checks
struct St {
	double m; Vector Ib, x, v, wb; Quaternion q;
	St (double m_, const Vector &Ib_, const Vector &x_, const Vector &v_, const Vector &wb_, const Quaternion &q_) : m (m_), Ib (Ib_), x (x_), v (v_), wb (wb_), q (q_) {}
	Vector Lspin () const { return mul (RotOf (q), Ib*wb); }
	bool Apply (const CollDelta &d) { return CollApplyDeltaState (x, v, q, wb, Ib, d); }
};

// P, L about the island origin; scales: sum m|v - v_com|, sum |Xc(x, m(v - v_com))| + |Ib w|
void Totals (const std::vector<St> &s, Vector &P, Vector &L, double &Ps, double &Ls)
{
	P = L = Vector ();
	double M = 0;
	for (const St &b : s) { P += b.v*b.m; L += Xc (b.x, b.v*b.m) + b.Lspin (); M += b.m; }
	Vector vc = P/M;
	Ps = Ls = 0;
	for (const St &b : s) { Ps += b.m*(b.v - vc).length (); Ls += Xc (b.x, (b.v - vc)*b.m).length () + b.Lspin ().length (); }
}

// change of P, L about the island origin from write-back deltas, not re-read positions; spin: state
void DeltaPL (const std::vector<St> &s0, const std::vector<St> &s1, const std::vector<CollDelta> &d, Vector &dP, Vector &dL)
{
	dP = dL = Vector ();
	for (size_t i = 0; i < s0.size (); i++) {
		const St &a = s0[i];
		Vector mdv = d[i].dv*a.m;
		dP += mdv;
		dL += Xc (a.x, mdv) + Xc (d[i].dx, a.v*a.m) + Xc (d[i].dx, mdv) + (s1[i].Lspin () - a.Lspin ());
	}
}

// kinetic energy at tau in the frame moving with vc (world inertia from Rt)
double KE (const CollSBody &b, const Vector &v, const Vector &w, const Vector &vc)
{
	Vector wb = tmul (b.Rt, w), Ib = b.pmi*b.m;
	return 0.5*b.m*(v - vc).length2 () + 0.5*(Ib.x*wb.x*wb.x + Ib.y*wb.y*wb.y + Ib.z*wb.z*wb.z);
}

// velocity change of a dynamic body after phase 1 (inertia at Rt)
void Phase1Vel (const CollSBody &b, Vector &v, Vector &w)
{
	Vector Ib = b.pmi*b.m, Lb = tmul (b.Rt, b.dL1);
	v = b.vt + b.dP1/b.m;
	w = b.wt + mul (b.Rt, Vector (Lb.x/Ib.x, Lb.y/Ib.y, Lb.z/Ib.z));
}

// phase-1 work of the contacts (8.2): dKE = -sum (Wn + Wt), firstOnly keeps FIRST points
double Work1 (const CollIsland &isl, bool firstOnly = false)
{
	double w = 0;
	for (const CollSContact &c : isl.con) if (!firstOnly || (c.flags & COLLP_FIRST)) w -= c.Wn + c.Wt;
	return w;
}

bool SameBits (const Vector &a, const Vector &b) { return std::memcmp (a.data, b.data, sizeof (a.data)) == 0; }

} // namespace

TEST_CASE ("U1 physical cross product matches Quaternion::Rotate and Matrix::Set", "[CollSolve]")
{
	Rng r (1);
	for (int t = 0; t < 50; t++) {
		Quaternion q (r.Q ());
		Vector wb = r.V (1.0), rb = r.V (3.0);
		double eps = 1e-5;
		Quaternion qp (q), qm (q);
		qp.Rotate (wb*eps); qm.Rotate (wb*(-eps));
		Vector vnum = (mul (RotOf (qp), rb) - mul (RotOf (qm), rb))/(2.0*eps);
		Matrix R = RotOf (q);
		Vector vx = Xc (mul (R, wb), mul (R, rb));
		REQUIRE ((vnum - vx).length () <= 1e-6*vx.length ());
		REQUIRE ((vx - mul (R, crossp (rb, wb))).length () <= 1e-12*vx.length ()); // Orbiter's point velocity form (SuperVessel.cpp)
	}
	// ExpW / LogW used by the stand-in below
	Vector phi (0.3, -0.2, 0.5);
	Matrix E = ExpW (phi);
	REQUIRE ((LogW (E) - phi).length () < 1e-12);
	Vector p (1, 2, 3), dnum = (mul (ExpW (phi*1e-6), p) - mul (ExpW (phi*-1e-6), p))/2e-6;
	REQUIRE ((dnum - Xc (phi, p)).length () < 1e-8);
}

TEST_CASE ("CollRotate substeps and CollApplyDeltaState rules", "[CollSolve]")
{
	Rng r (2);
	Quaternion q (r.Q ()), q0 (q), qm (q);
	Vector dth (0.07, -0.08, 0.05); // |dth| = 0.118: 3 substeps
	CollRotate (q, dth);
	for (int i = 0; i < 3; i++) qm.Rotate (dth/3.0);
	REQUIRE (std::memcmp (q.data, qm.data, sizeof (q.data)) == 0);
	Matrix Rx = ExpW (mul (RotOf (q0), dth))*RotOf (q0), Rq = RotOf (q);
	for (int i = 0; i < 9; i++) REQUIRE (std::fabs (Rx.data[i] - Rq.data[i]) < 1e-4);
	Quaternion qz (q0);
	CollRotate (qz, Vector ());
	REQUIRE (std::memcmp (qz.data, q0.data, sizeof (q0.data)) == 0);

	// locked axis: omega and attitude unchanged about it
	Vector x (1, 2, 3), v (4, 5, 6), wb (0.1, 0.2, 0.3), Ib (10, 20, 0);
	Quaternion qa (q0);
	CollDelta d; d.dv = Vector (1, 0, 0); d.dx = Vector (0, 0.1, 0); d.dLw = Vector (); d.dth = Vector (0, 0, 0.2);
	REQUIRE (CollApplyDeltaState (x, v, qa, wb, Ib, d));
	REQUIRE (std::memcmp (qa.data, q0.data, sizeof (q0.data)) == 0);
	REQUIRE (wb.z == 0.3);
	REQUIRE ((x - Vector (1, 2.1, 3)).length () < 1e-15);
	// omega clamp at 100 pi
	Vector w2 (0, 0, 0), Ib2 (1, 1, 1);
	Quaternion qb (q0);
	CollDelta big; big.dLw = Vector (1e4, 0, 0);
	{
		LogCapture lc;
		REQUIRE (CollApplyDeltaState (x, v, qb, w2, Ib2, big));
		REQUIRE (std::fabs (w2.length () - COLL_OMEGA_MAX) < 1e-9);
		REQUIRE (lc.Count ("clamped") == 1);
	}
	// non-finite: state unchanged
	Vector x0 = x, v0 = v, wb0 = w2;
	Quaternion qc (qb);
	CollDelta bad; bad.dv = Vector (NAN, 0, 0);
	{
		LogCapture lc;
		REQUIRE_FALSE (CollApplyDeltaState (x, v, qc, w2, Ib2, bad));
	}
	REQUIRE (SameBits (x, x0)); REQUIRE (SameBits (v, v0)); REQUIRE (SameBits (w2, wb0));
	REQUIRE (std::memcmp (qc.data, qb.data, sizeof (qb.data)) == 0);
}

TEST_CASE ("U2 head-on, two 500 kg bodies at 1 m/s", "[CollSolve]")
{
	double h = 1.0/60.0, tau = 0.4, tr = (1 - tau)*h;
	Vector pmi (2.28, 2.31, 0.79);
	CollIsland isl; isl.tau = tau; isl.h = h;
	isl.body.push_back (Dyn (500, pmi, Vector (0, 0, 0), Vector (0.5, 0, 0), IMatrix (), Vector (), tr));
	isl.body.push_back (Dyn (500, pmi, Vector (4, 0, 0), Vector (-0.5, 0, 0), IMatrix (), Vector (), tr));
	isl.con.push_back (Con (0, 1, Vector (2, 0, 0), Vector (-1, 0, 0)));
	REQUIRE (isl.Solve (CollSolveParams ()));
	CollDelta da, db;
	isl.Delta (0, da); isl.Delta (1, db);
	Vector va = isl.body[0].v1 + da.dv, vb = isl.body[1].v1 + db.dv;
	REQUIRE (std::fabs ((va - vb).x - (-0.3)) < 1e-12); // v after = -e v, e(1 m/s) = e0
	double ke0 = 0.5*500*(0.25 + 0.25), ke1 = 0.5*500*(va.length2 () + vb.length2 ());
	REQUIRE (std::fabs (ke1/ke0 - 0.09) < 1e-12);
	Vector dP = isl.body[0].dP1 + isl.body[0].dP2 + isl.body[1].dP1 + isl.body[1].dP2;
	REQUIRE (dP.length () == 0.0);
	REQUIRE (std::fabs (isl.con[0].vapp - 1.0) < 1e-15);
}

TEST_CASE ("U3 off-centre spin-up with e = 0: closed form, L about the contact point", "[CollSolve]")
{
	Rng r (3);
	for (int t = 0; t < 20; t++) {
		double h = 0.1, tau = 0.3, tr = (1 - tau)*h;
		Matrix R = RotOf (r.Q ());
		Vector pmi (2.0, 2.5, 1.0), n = Vector (0.9, 0.3, 0.1)/Vector (0.9, 0.3, 0.1).length ();
		CollSBody A = Dyn (2000, pmi, Vector (0, 0, 0), -n*5.0 + r.V (1.0), R, r.V (0.3), tr);
		CollIsland isl; isl.tau = tau; isl.h = h;
		isl.body.push_back (A);
		bool kin = t % 2 == 0; // plane, or a second dynamic body
		Vector xsa = A.x1 - A.v1*tr, p = xsa + Vector (-1.2, 0.6, -0.4) - n*1.0;
		if (kin) isl.body.push_back (Kin (p - n*2.0));
		else isl.body.push_back (Dyn (12000, Vector (8, 9, 3), p - n*2.0, r.V (0.5), RotOf (r.Q ()), r.V (0.1), tr));
		isl.con.push_back (Con (0, 1, p, n, COLL_TOI, 0.0, 0.0));
		REQUIRE (isl.Solve (CollSolveParams ()));
		const CollSBody &a = isl.body[0], &b = isl.body[1];
		// closed form J = vapp kn (e = 0, mu = 0, one contact)
		Vector ra = p - (a.x1 - a.v1*tr), rb = p - (b.dyn ? b.x1 - b.v1*tr : b.xt);
		auto term = [] (const CollSBody &s, const Vector &rr, const Vector &nn) {
			if (!s.dyn) return 0.0;
			Vector Ib = s.pmi*s.m, Lb = tmul (s.Rt, Xc (rr, nn));
			return 1.0/s.m + dotp (nn, Xc (mul (s.Rt, Vector (Lb.x/Ib.x, Lb.y/Ib.y, Lb.z/Ib.z)), rr));
		};
		Vector ua = a.vt + Xc (a.wt, ra), ub = b.dyn ? b.vt + Xc (b.wt, rb) : Vector ();
		double vapp = -dotp (ua - ub, n);
		REQUIRE (vapp > 0);
		double J = vapp/(term (a, ra, n) + term (b, rb, n));
		REQUIRE ((isl.con[0].J1 - n*J).length () <= 1e-12*J);
		// L of A about the contact point unchanged by the impulse (phase 1)
		Vector v1, w1;
		Phase1Vel (a, v1, w1);
		auto Lp = [&] (const Vector &v, const Vector &w) {
			Vector wb = tmul (a.Rt, w), Ib = a.pmi*a.m;
			return Xc (-ra, v*a.m) + mul (a.Rt, Ib*wb);
		};
		Vector L0 = Lp (a.vt, a.wt), L1 = Lp (v1, w1);
		REQUIRE ((L1 - L0).length () <= 1e-12*(L0.length () + J*ra.length ()));
	}
}

TEST_CASE ("U4 friction: slide 2 m/s and 0.2 m/s, approach 1 m/s, mu 0.5", "[CollSolve]")
{
	for (double slide : { 2.0, 0.2 }) {
		double h = 1.0/60.0, tau = 0.5, tr = (1 - tau)*h, m = 1000;
		Vector pmi (0.667, 0.667, 0.667);
		CollIsland isl; isl.tau = tau; isl.h = h;
		isl.body.push_back (Dyn (m, pmi, Vector (0, 1, 0), Vector (slide, -1, 0), IMatrix (), Vector (), tr));
		isl.body.push_back (Kin (Vector (0, -1, 0)));
		Vector xs = isl.body[0].x1 - isl.body[0].v1*tr, p = xs + Vector (0, -1, 0);
		isl.con.push_back (Con (0, 1, p, Vector (0, 1, 0)));
		REQUIRE (isl.Solve (CollSolveParams ()));
		double e = CollRestitution (1.0, COLL_E0, COLL_VY, CollSolveParams ());
		double Jn = (1 + e)*m;                                         // r parallel to n: kn = m
		double kt = 1.0/(1.0/m + 1.0/(m*pmi.x));                       // tangential effective mass at r = 1 m below the CG
		double expect = std::min (0.5*Jn/m, slide*kt/m);
		REQUIRE (std::fabs (isl.con[0].ln1 - Jn) < 1e-9*Jn);
		REQUIRE (std::fabs (-isl.body[0].dP1.x/m - expect) < 1e-9);
		REQUIRE (isl.body[0].dP2.length () == 0.0);                   // separating after phase 1
		if (slide == 0.2) {                                             // stick: point slip 0
			Vector v1, w1;
			Phase1Vel (isl.body[0], v1, w1);
			REQUIRE (std::fabs ((v1 + Xc (w1, p - xs)).x) < 1e-9);
		}
	}
}

TEST_CASE ("U5 two-phase write-back conserves P and L at t1; 0.5 factor negative control", "[CollSolve]")
{
	Rng r (5);
	double worstP = 0, worstL = 0, worstBad = 0;
	int nBad = 0;
	for (int t = 0; t < 2000; t++) {
		double h = t % 2 ? 0.1 : 1.0/60.0, tau = r.U (0.0, 0.95), tr = (1 - tau)*h;
		double thrust = (t/2) % 2 ? 9.0 : 2.0;
		CollIsland isl; isl.tau = tau; isl.h = h;
		std::vector<Quaternion> q1;
		Vector xa = r.V (5.0), xb = xa + r.Unit ()*4.0;
		for (int i = 0; i < 2; i++) {
			Quaternion q (r.Q ());
			q1.push_back (q);
			double m = std::pow (10.0, r.U (1.0, 5.0));
			Vector pmi (r.U (0.2, 20), r.U (0.2, 20), r.U (0.2, 20));
			CollSBody b = Dyn (m, pmi, i ? xb : xa, r.V (5.0), RotOf (r.Q ()), r.V (0.5), tr, r.Unit ()*thrust);
			b.R1 = RotOf (q); b.wb1 = r.V (0.5);                       // attitude and spin at t1 independent of tau (any forces in the step)
			isl.body.push_back (b);
		}
		int nc = 1 + t % 4;
		Vector base = (xa + xb)*0.5, n0 = (xa - xb).unit ();
		for (int k = 0; k < nc; k++) {
			Vector n = (n0 + r.V (0.3)).unit ();
			uint8_t kind = (uint8_t)(1 + (t + k) % 3);
			CollSContact c = Con (0, 1, base + r.V (1.5), n, kind, r.U (0, 1), r.U (0, 1), r.U (0.5, 2));
			c.gap = kind == COLL_SPECULATIVE ? r.U (0, 0.05) : r.U (-0.01, 0.01);
			c.n2 = (n + r.V (0.05)).unit ();
			c.vka_t = r.V (0.1); c.vkb_t = r.V (0.1); c.vka_1 = r.V (0.1); c.vkb_1 = r.V (0.1);
			if (k == 3) c.flags = COLLP_DEGENERATE;
			isl.con.push_back (c);
		}
		REQUIRE (isl.Solve (CollSolveParams ()));
		std::vector<St> s0;
		for (int i = 0; i < 2; i++) {
			const CollSBody &b = isl.body[i];
			s0.push_back (St (b.m, b.pmi*b.m, b.x1, b.v1, b.wb1, q1[i]));
		}
		std::vector<St> s1 (s0), sb (s0);
		std::vector<CollDelta> dg (2), db (2);
		for (int i = 0; i < 2; i++) {
			const CollSBody &b = isl.body[i];
			isl.Delta (i, dg[i]);
			REQUIRE (s1[i].Apply (dg[i]));
			db[i] = dg[i];                                              // phase 2 with dx = tr dv (factor 1)
			Vector Ib = b.pmi*b.m, Lb = tmul (b.R1, b.dL1 + b.dL2);
			db[i].dx = (b.dP1 + b.dP2)*(tr/b.m);
			db[i].dth = Vector (Lb.x/Ib.x, Lb.y/Ib.y, Lb.z/Ib.z)*tr;
			REQUIRE (sb[i].Apply (db[i]));
		}
		Vector P0, L0, dP, dL, dPb, dLb;
		double Ps, Ls;
		Totals (s0, P0, L0, Ps, Ls);
		DeltaPL (s0, s1, dg, dP, dL); DeltaPL (s0, sb, db, dPb, dLb);
		worstP = std::max (worstP, dP.length ()/Ps);
		worstL = std::max (worstL, dL.length ()/Ls);
		if (isl.body[0].dP2.length () > 1e-3*isl.body[0].m*0.01) {
			double e = dLb.length ()/Ls;
			worstBad = std::max (worstBad, e);
			if (e > 1e-9) nBad++;
		}
	}
	std::printf ("U5: worst relative dP %.2e, dL %.2e; negative control worst dL %.2e (%d pairs above 1e-9)\n", worstP, worstL, worstBad, nBad);
	REQUIRE (worstP <= 1e-12);
	REQUIRE (worstL <= 1e-12);
	REQUIRE (worstBad > 1e-6);
	REQUIRE (nBad > 100);
}

TEST_CASE ("U6 4000 random islands: the work-based guard keeps phase 1 from adding energy", "[CollSolve]")
{
	Rng r (11);
	LogCapture lc;
	int nup = 0;
	double worst = -1e300;
	for (int t = 0; t < 4000; t++) {
		CollIsland isl; isl.tau = 1.0; isl.h = 1.0/60.0;
		for (int i = 0; i < 2; i++) {
			double m = std::pow (10.0, r.U (1.0, 6.0));
			Vector pmi (r.U (0.2, 20), r.U (0.2, 20), r.U (0.2, 20));
			isl.body.push_back (Dyn (m, pmi, Vector (0, 0, i ? -5.0 : 5.0), r.Unit ()*r.U (0.1, 10), RotOf (r.Q ()), r.V (1.0)*r.U (0, 0.5), 0.0));
		}
		int nc = 1 + (int)(r.U ()*4.0);
		Vector n = r.Unit (), base = r.V (3.0);
		double e = r.U (0, 1), mu = r.U (0, 1);
		for (int k = 0; k < nc; k++) {
			Vector nk = r.U () < 0.7 ? n : (n + r.V (0.3)).unit ();
			isl.con.push_back (Con (0, 1, base + r.V (1.5), nk, COLL_TOI, mu, e, 1e9)); // vy huge: e = e0 above vrest
		}
		REQUIRE (isl.Solve (CollSolveParams ()));
		Vector P;
		double M = 0;
		for (const CollSBody &b : isl.body) { P += b.vt*b.m; M += b.m; }
		Vector vc = P/M;
		double E0 = 0, E1 = 0;
		for (const CollSBody &b : isl.body) {
			Vector v1, w1;
			Phase1Vel (b, v1, w1);
			E0 += KE (b, b.vt, b.wt, vc); E1 += KE (b, v1, w1, vc);
		}
		double rel = (E1 - E0)/std::max (E0, 1e-12);
		worst = std::max (worst, rel);
		if (rel > 1e-9) nup++;
	}
	int guards = lc.Count ("redone with e = 0");
	std::printf ("U6: KE rises above 1e-9: %d of 4000, worst relative change %.2e, guard redos %d\n", nup, worst, guards);
	REQUIRE (nup == 0);
	REQUIRE (guards > 0);
}

namespace {

// stand-in for D2 and adapter: dynamic box on a kinematic plate, RESTING at tau = 0 (t0 manifold)
constexpr double RS = 0.04, DCT = COLL_DELTA_CT;

struct TBox {
	double m = 0; Vector pmi, x, v, wb; Quaternion q;
	TBox () = default;
	TBox (const TBox &) = default;
	TBox &operator= (const TBox &o) { m = o.m; pmi = o.pmi; x = o.x; v = o.v; wb = o.wb; q.Set (o.q); return *this; }
};

struct TKin {
	Vector x0, vo, W, c; bool aboutC = false; Matrix R0 = IMatrix ();
	void Pose (double t, Vector &x, Matrix &R) const { Matrix E = ExpW (W*t); R = E*R0; x = aboutC ? c + mul (E, x0 - c) : x0 + vo*t; }
	Vector Vel (const Vector &p, double t) const
	{
		if (aboutC) return Xc (W, p - c);
		Vector x; Matrix R; Pose (t, x, R);
		return vo + Xc (W, p - x);
	}
};

struct TScene {
	TBox box; TKin kin;
	Vector pc, pn, pu; double eu = 10, ew = 10;       // plate in the kinematic frame: centre, normal, axis, half extents
	std::vector<Vector> corner;                        // box corners relative to the CG, body frame
	std::function<Vector (const Vector &)> acc;
	bool carry = true;                                 // phase-2 kinematic velocity at the carried point (2.5)
};

Vector EulerFree (const Vector &pmi, const Vector &w)
{
	return Vector (-(pmi.y - pmi.z)*w.y*w.z/pmi.x, -(pmi.z - pmi.x)*w.z*w.x/pmi.y, -(pmi.x - pmi.y)*w.x*w.y/pmi.z);
}

// RK2 translation and torque-free Euler rotation in substeps (BodyIntegrator.cpp pattern)
void Integrate (TBox &b, double h, const std::function<Vector (const Vector &)> &acc, int n = 20)
{
	double d = h/n;
	for (int i = 0; i < n; i++) {
		Vector a0 = acc (b.x), xm = b.x + b.v*(0.5*d), vm = b.v + a0*(0.5*d), am = acc (xm);
		b.x += vm*d; b.v += am*d;
		Vector wm = b.wb + EulerFree (b.pmi, b.wb)*(0.5*d);
		b.q.Rotate (wm*d);
		b.wb += EulerFree (b.pmi, wm)*d;
	}
}

double MinGap (const TScene &s, double t)
{
	Vector xk; Matrix Rk;
	s.kin.Pose (t, xk, Rk);
	Matrix R = RotOf (s.box.q);
	Vector cw = xk + mul (Rk, s.pc), nw = mul (Rk, s.pn);
	double g = 1e300;
	for (const Vector &c : s.corner) g = std::min (g, dotp (s.box.x + mul (R, c) - cw, nw) - RS);
	return g;
}

// one frame: integrate, RESTING contacts within delta_ct, island at tau = 0, write-back; returns n
int TFrame (TScene &s, double t0, double h, const CollSolveParams &p)
{
	TBox b1 = s.box;
	Integrate (b1, h, s.acc);
	Vector xk0, xk1; Matrix Rk0, Rk1;
	s.kin.Pose (t0, xk0, Rk0); s.kin.Pose (t0 + h, xk1, Rk1);
	Matrix R0 = RotOf (s.box.q), R1 = RotOf (b1.q);
	Vector nw = mul (Rk0, s.pn), uw = mul (Rk0, s.pu), ww = crossp (nw, uw), cw = xk0 + mul (Rk0, s.pc);
	Vector O = b1.x;
	CollSBody A {}, K {};
	A.dyn = true; A.m = b1.m; A.pmi = b1.pmi; A.Rt = R0; A.R1 = R1;
	A.xt = s.box.x - O; A.vt = s.box.v; A.wt = LogW (R1*transp (R0))/h;
	A.x1 = Vector (); A.v1 = b1.v; A.wb1 = b1.wb;
	K.dyn = false; K.m = 1; K.pmi = Vector (1, 1, 1); K.Rt = Rk0; K.R1 = Rk1; K.xt = xk0 - O; K.x1 = xk1 - O;
	CollIsland isl; isl.tau = 0; isl.h = h;
	isl.body.push_back (A); isl.body.push_back (K);
	Matrix Ra = R1*transp (R0), Rb = Rk1*transp (Rk0);
	for (const Vector &c : s.corner) {
		Vector pw = s.box.x + mul (R0, c), rr = pw - cw;
		if (std::fabs (dotp (rr, uw)) > s.eu + 1e-9 || std::fabs (dotp (rr, ww)) > s.ew + 1e-9) continue;
		double d = dotp (rr, nw);
		if (d - RS > DCT) continue;
		Vector pg = pw - nw*(0.5*d);
		CollSContact k = Con (0, 1, pg - O, nw, COLL_RESTING);
		k.gap = d - RS;
		Vector n2 = mul (Ra, nw) + mul (Rb, nw);
		k.n2 = n2/n2.length ();
		k.vkb_t = s.kin.Vel (pg, t0);
		k.vkb_1 = s.kin.Vel (s.carry ? xk1 + mul (Rb, pg - xk0) : pg, t0 + h);
		isl.con.push_back (k);
	}
	if (isl.con.empty ()) { s.box = b1; return 0; }
	REQUIRE (isl.Solve (p));
	CollDelta d;
	isl.Delta (0, d);
	REQUIRE (CollApplyDeltaState (b1.x, b1.v, b1.q, b1.wb, b1.pmi*b1.m, d));
	s.box = b1;
	return (int)isl.con.size ();
}

std::vector<Vector> BoxCorners (double hx, double hy, double hz, const Vector &ofs = Vector ())
{
	std::vector<Vector> c;
	for (int sx = -1; sx <= 1; sx += 2)
		for (int sy = -1; sy <= 1; sy += 2)
			for (int sz = -1; sz <= 1; sz += 2) c.push_back (Vector (sx*hx, sy*hy, sz*hz) - ofs);
	return c;
}

// 2 m box resting 5 mm above a plate (gate case A), plate moving at V along x
TScene RoofScene (double V)
{
	TScene s;
	s.kin.vo = Vector (V, 0, 0);
	s.pn = Vector (0, 1, 0); s.pu = Vector (1, 0, 0);
	s.box.m = 1000; s.box.pmi = Vector (0.667, 0.667, 0.667);
	s.box.x = Vector (0, 1 + RS + 0.005, 0); s.box.v = Vector (V, 0, 0);
	s.corner = BoxCorners (1, 1, 1);
	s.acc = [] (const Vector &) { return Vector (0, -9.81, 0); };
	return s;
}

// vessel on an equator roof, Earth rotation, central gravity, CG 0.5 m off support centre (case B)
TScene PlanetScene ()
{
	const double GM = 3.986004418e14, Rp = 6.371e6, W = 7.2921159e-5;
	TScene s;
	s.kin.aboutC = true; s.kin.x0 = Vector (Rp + 20.0, 0, 0); s.kin.W = Vector (0, W, 0);
	s.pn = Vector (1, 0, 0); s.pu = Vector (0, 0, 1);
	Matrix Rb (0, 1, 0, -1, 0, 0, 0, 0, 1);                          // box y -> world radial
	s.box.m = 2000; s.box.pmi = Vector (1.2, 1.4, 1.0);
	s.box.q.Set (Rb);
	s.box.x = s.kin.x0 + Vector (1 + RS + 0.005, 0, 0);
	s.box.v = Xc (s.kin.W, s.box.x); s.box.wb = tmul (Rb, s.kin.W);
	s.corner = BoxCorners (1.5, 1, 1, Vector (0.4, 0, -0.3));
	s.acc = [GM] (const Vector &x) { return x*(-GM/std::pow (x.length (), 3)); };
	return s;
}

// rest metrics: support = mean gap of initial supporting corners, corner = lowest corner (tilted)
struct RestResult { double supDrift, cornerDrift, vnLate, vLate, vtEnd, tang; };

double SupportGap (const TScene &s, double t, const std::vector<int> &sup)
{
	Vector xk; Matrix Rk;
	s.kin.Pose (t, xk, Rk);
	Matrix R = RotOf (s.box.q);
	Vector cw = xk + mul (Rk, s.pc), nw = mul (Rk, s.pn);
	double g = 0;
	for (int i : sup) g += dotp (s.box.x + mul (R, s.corner[i]) - cw, nw) - RS;
	return g/sup.size ();
}

RestResult RunRest (TScene s, double h, int nfr)
{
	CollSolveParams p;
	std::vector<int> sup;
	{
		Vector xk; Matrix Rk;
		s.kin.Pose (0.0, xk, Rk);
		Matrix R = RotOf (s.box.q);
		for (size_t i = 0; i < s.corner.size (); i++)
			if (dotp (s.box.x + mul (R, s.corner[i]) - xk - mul (Rk, s.pc), mul (Rk, s.pn)) - RS < DCT) sup.push_back ((int)i);
	}
	double s0 = SupportGap (s, 0.0, sup), c0 = MinGap (s, 0.0), t = 0;
	Vector xk0; Matrix Rk0;
	s.kin.Pose (0.0, xk0, Rk0);
	Vector rel0 = tmul (Rk0, s.box.x - xk0);
	RestResult r { 0, 0, 0, 0, 0, 0 };
	for (int k = 0; k < nfr; k++) {
		TFrame (s, t, h, p);
		t += h;
		r.supDrift = std::max (r.supDrift, std::fabs (SupportGap (s, t, sup) - s0));
		r.cornerDrift = std::max (r.cornerDrift, std::fabs (MinGap (s, t) - c0));
		Vector xk; Matrix Rk;
		s.kin.Pose (t, xk, Rk);
		Vector vr = s.box.v - s.kin.Vel (s.box.x, t), nw = mul (Rk, s.pn);
		double vn = dotp (vr, nw);
		if (k >= nfr/2) { r.vnLate = std::max (r.vnLate, std::fabs (vn)); r.vLate = std::max (r.vLate, vr.length ()); }
		r.vtEnd = (vr - nw*vn).length ();
		Vector rel = tmul (Rk, s.box.x - xk) - rel0;
		r.tang = (rel - s.pn*dotp (rel, s.pn)).length ();
	}
	return r;
}

void PrintRest (const char *what, double h, const RestResult &r)
{
	std::printf ("%s h %.4f (%d it): support gap drift %.2e m, |vn| late %.2e m/s; lowest corner drift %.2e m, |v| late %.2e m/s, tangential drift %.2e m\n",
		what, h, CollIterations (h, COLL_ITERATIONS), r.supDrift, r.vnLate, r.cornerDrift, r.vLate, r.tang);
}

} // namespace

TEST_CASE ("U7 resting box on a static plane, 600 frames, scaled iterations", "[CollSolve]")
{
	for (double h : { 1.0/60.0, 0.1, 0.17, 0.5 }) {
		RestResult r = RunRest (RoofScene (0.0), h, 600);
		PrintRest ("U7: plate at rest", h, r);
		REQUIRE (r.supDrift < 1e-4);                                     // 4.8 gap and hop columns
		REQUIRE (r.vnLate < 1e-6);
		if (h < 0.2) { REQUIRE (r.cornerDrift < 1e-4); REQUIRE (r.vLate < 1e-6); } // full vector and tilt; at 0.5 s the tangential creep is 1.5e-6 m/s (prototype 1.4e-6)
	}
}

TEST_CASE ("U8 box on a moving plane; vessel on an Earth-rotating roof; carried-point rule", "[CollSolve]")
{
	for (double V : { 465.0, 3.0e4 })
		for (double h : { 1.0/60.0, 0.1, 0.17, 0.5 }) {
			RestResult r = RunRest (RoofScene (V), h, 600);
			char what[64];
			std::snprintf (what, sizeof (what), "U8: plate at %g m/s", V);
			PrintRest (what, h, r);
			REQUIRE (r.supDrift < 1e-4);
			REQUIRE (r.vnLate < 1e-6);
			if (h < 0.2) { REQUIRE (r.cornerDrift < 1e-4); REQUIRE (r.vLate < 1e-6); }
		}
	for (double h : { 1.0/60.0, 0.1, 0.17, 0.25, 0.5 }) {
		RestResult r = RunRest (PlanetScene (), h, 600);
		PrintRest ("U8: roof, Earth rotation", h, r);
		if (h > COLL_H_REST) continue;                                   // beyond the 0.25 s load cap (Y6'); one solve per frame as D2 6.4 gives
		REQUIRE (r.supDrift < 1e-4);
		REQUIRE (r.vnLate < 1e-6);
		if (h <= 0.1) { REQUIRE (r.cornerDrift < 1e-4); REQUIRE (r.tang < 1e-3); } // G3: tangential creep bounded by drift (4.8 B: 2.6e-4 m in 60 s at 0.1 s)
	}
	TScene off = PlanetScene ();
	off.carry = false;
	RestResult r = RunRest (off, 1.0/60.0, 600);
	PrintRest ("U8: roof, carried point off", 1.0/60.0, r);
	REQUIRE (r.vnLate > 1e-5);
}

TEST_CASE ("U9 mass ratio 2000 and 1e5, 4-point manifold", "[CollSolve]")
{
	Rng r (9);
	for (double ratio : { 2000.0, 1e5 })
		for (double e0 : { 0.3, 0.0 }) {
			double h = 1.0/60.0, tau = 0.7, tr = (1 - tau)*h;
			CollIsland isl; isl.tau = tau; isl.h = h;
			Quaternion qs (r.Q ()), qb (r.Q ());
			isl.body.push_back (Dyn (200, Vector (0.6, 0.6, 0.5), Vector (0, 30, 0), Vector (0.3, -1, 0), RotOf (qs), Vector (0, 0, 0.2), tr));
			isl.body.push_back (Dyn (200*ratio, Vector (400, 500, 300), Vector (0, 0, 0), Vector (), RotOf (qb), Vector (0, 0.001, 0), tr));
			Vector base = isl.body[0].x1 - isl.body[0].v1*tr + Vector (0, -0.8, 0);
			for (double dx : { -0.5, 0.5 })
				for (double dz : { -0.5, 0.5 }) isl.con.push_back (Con (0, 1, base + Vector (dx, 0, dz), Vector (0, 1, 0), COLL_TOI, 0.5, e0));
			REQUIRE (isl.Solve (CollSolveParams ()));
			std::vector<St> s0;
			Quaternion q[2] = { qs, qb };
			for (int i = 0; i < 2; i++) {
				const CollSBody &b = isl.body[i];
				s0.push_back (St (b.m, b.pmi*b.m, b.x1, b.v1, b.wb1, q[i]));
			}
			std::vector<St> s1 (s0);
			std::vector<CollDelta> ds (2);
			for (int i = 0; i < 2; i++) { isl.Delta (i, ds[i]); REQUIRE (s1[i].Apply (ds[i])); }
			Vector P0, L0, dP, dL;
			double Ps, Ls;
			Totals (s0, P0, L0, Ps, Ls); DeltaPL (s0, s1, ds, dP, dL);
			// residual approach after phase 1 (tau) and after phase 2 (t1, carried point) as Solve defines them
			double worst1 = 1e300, worst2 = 1e300;
			Vector v1[2], w1[2], xs[2], xm[2], vf[2], wf[2];
			for (int i = 0; i < 2; i++) {
				const CollSBody &s = isl.body[i];
				Phase1Vel (s, v1[i], w1[i]);
				xs[i] = s.x1 - s.v1*tr;
				Vector v1a = s.v1 + s.dP1/s.m, Ib = s.pmi*s.m;
				xm[i] = s.x1 + s.dP1*(tr/s.m) - v1a*(0.5*tr);
				vf[i] = v1a + s.dP2/s.m;
				Vector Lb = tmul (s.R1, mul (s.R1, Ib*s.wb1) + s.dL1 + s.dL2);
				wf[i] = mul (s.R1, Vector (Lb.x/Ib.x, Lb.y/Ib.y, Lb.z/Ib.z));
			}
			for (const CollSContact &c : isl.con) {
				Vector u1 = (v1[0] + Xc (w1[0], c.p - xs[0])) - (v1[1] + Xc (w1[1], c.p - xs[1]));
				worst1 = std::min (worst1, dotp (u1, c.n));
				Vector pm = c.p + ((xm[0] - xs[0]) + (xm[1] - xs[1]))*0.5;
				Vector u2 = (vf[0] + Xc (wf[0], pm - xm[0])) - (vf[1] + Xc (wf[1], pm - xm[1]));
				worst2 = std::min (worst2, dotp (u2, c.n2));
			}
			std::printf ("U9: ratio %g e0 %.1f: dP %.2e dL %.2e, min separation speed phase 1 %.2e, phase 2 %.2e m/s\n", ratio, e0, dP.length ()/Ps, dL.length ()/Ls, worst1, worst2);
			REQUIRE (dP.length () <= 1e-12*Ps);
			REQUIRE (dL.length () <= 1e-12*Ls);
			if (e0 > 0) REQUIRE (worst1 > -1e-6);                            // restitution target reached at tau
			REQUIRE (worst2 > -1e-6);                                        // no approach left after phase 2
		}
}

TEST_CASE ("U10 restitution e(v) and scaled iteration count", "[CollSolve]")
{
	CollSolveParams p;
	REQUIRE (CollRestitution (0.05, 0.3, 1.0, p) == 0.0);
	REQUIRE (CollRestitution (0.5, 0.3, 1.0, p) == 0.3);
	REQUIRE (std::fabs (CollRestitution (5.0, 0.3, 1.0, p) - 0.3*std::pow (0.2, 0.25)) < 1e-15);
	REQUIRE (std::fabs (CollRestitution (100.0, 0.3, 1.0, p) - 0.3*std::pow (0.01, 0.25)*200.0/250.0) < 1e-15);
	REQUIRE (CollRestitution (400.0, 0.3, 1.0, p) == 0.0);
	REQUIRE (CollRestitution (300.0, 0.3, 1.0, p) == 0.0);
	REQUIRE (CollRestitution (0.1, 0.3, 1.0, p) == 0.3);
	REQUIRE (CollRestitution (NAN, 0.3, 1.0, p) == 0.0);
	REQUIRE (CollIterations (1.0/60.0, 20) == 20);
	REQUIRE (CollIterations (1.0/6.0, 20) == 20);
	REQUIRE (CollIterations (0.167, 20) == 20);
	REQUIRE (CollIterations (0.17, 20) == 21);
	REQUIRE (CollIterations (0.25, 20) == 30);
	REQUIRE (CollIterations (0.5, 20) == 60);
	REQUIRE (CollIterations (1.0, 20) == 60);
	REQUIRE (CollIterations (0.0, 20) == 20);
}

TEST_CASE ("U11 position correction: CoM unchanged, velocities bitwise unchanged", "[CollSolve]")
{
	Rng r (12);
	for (double gap : { -0.02, -0.5 }) {
		CollIsland isl; isl.tau = 0; isl.h = 0.1;
		Quaternion qa (r.Q ()), qb (r.Q ());
		isl.body.push_back (Dyn (1000, Vector (1.0, 1.5, 0.8), Vector (0, 1.2, 0), r.V (1.0), RotOf (qa), r.V (0.2), 0.1));
		isl.body.push_back (Dyn (3000, Vector (2.0, 1.0, 1.4), Vector (0.3, -1.0, 0.2), r.V (1.0), RotOf (qb), r.V (0.2), 0.1));
		Vector p (0.1, 0.0, 0.05), n (0, 1, 0);
		CollSContact c = Con (0, 1, p, n, COLL_RESTING); c.gap = gap;
		CollSContact ign = Con (0, 1, p + Vector (0.5, 0, 0), n, COLL_RESTING); ign.gap = -0.001; // within slop: untouched
		isl.con.push_back (c); isl.con.push_back (ign);
		std::vector<CollDelta> d;
		CollSolveParams prm;
		REQUIRE (isl.Correct (prm, d));
		Quaternion q[2] = { qa, qb };
		double M = 0;
		Vector com0, com1, pt[2];
		std::vector<St> s;
		for (int i = 0; i < 2; i++) {
			const CollSBody &b = isl.body[i];
			s.push_back (St (b.m, b.pmi*b.m, b.xt, b.vt, tmul (b.Rt, b.wt), q[i]));
			REQUIRE (d[i].dv.length () == 0.0); REQUIRE (d[i].dLw.length () == 0.0);
		}
		for (int i = 0; i < 2; i++) {
			St a = s[i];
			com0 += a.x*a.m; M += a.m;
			Vector L0 = a.Lspin (), v0 = a.v;
			Matrix R0 = RotOf (a.q);
			REQUIRE (a.Apply (d[i]));
			REQUIRE (SameBits (a.v, v0));
			REQUIRE ((a.Lspin () - L0).length () <= 1e-12*L0.length ());
			com1 += a.x*a.m;
			pt[i] = a.x + mul (RotOf (a.q), tmul (R0, p - s[i].x)) - p; // displacement of the material point at p
		}
		double target = std::min (prm.beta*(-prm.slop - gap), prm.dxmax);
		double sep = dotp (pt[0] - pt[1], n);
		std::printf ("U11: gap %.3f: CoM shift %.2e m, separation %.6f m (target %.6f)\n", gap, (com1 - com0).length ()/M, sep, target);
		REQUIRE ((com1 - com0).length ()/M <= 1e-12);
		REQUIRE (std::fabs (sep - target) < 1e-3*target);
	}
}

TEST_CASE ("U12 determinism: the same island twice gives identical bits", "[CollSolve]")
{
	Rng r (13);
	CollIsland isl; isl.tau = 0.3; isl.h = 0.1;
	for (int i = 0; i < 3; i++)
		isl.body.push_back (Dyn (std::pow (10.0, r.U (1, 5)), Vector (r.U (0.2, 20), r.U (0.2, 20), r.U (0.2, 20)), r.V (5.0), r.V (5.0), RotOf (r.Q ()), r.V (0.5), 0.07, r.V (3.0)));
	isl.body.push_back (Kin (r.V (5.0)));
	for (int k = 0; k < 12; k++) {
		int a = k % 4, b = (k + 1 + k/4) % 4;
		if (a == b) b = (b + 1) % 4;
		CollSContact c = Con (a, b, r.V (3.0), r.Unit (), (uint8_t)(1 + k % 3), r.U (0, 1), r.U (0, 1), 1.0);
		c.gap = r.U (0, 0.03); c.vka_t = r.V (0.2); c.vkb_1 = r.V (0.2);
		isl.con.push_back (c);
	}
	CollIsland a = isl, b = isl;
	REQUIRE (a.Solve (CollSolveParams ()));
	REQUIRE (b.Solve (CollSolveParams ()));
	for (size_t i = 0; i < isl.body.size (); i++) {
		REQUIRE (SameBits (a.body[i].dP1, b.body[i].dP1)); REQUIRE (SameBits (a.body[i].dL1, b.body[i].dL1));
		REQUIRE (SameBits (a.body[i].dP2, b.body[i].dP2)); REQUIRE (SameBits (a.body[i].dL2, b.body[i].dL2));
	}
	for (size_t i = 0; i < isl.con.size (); i++) {
		REQUIRE (SameBits (a.con[i].J1, b.con[i].J1)); REQUIRE (SameBits (a.con[i].J2, b.con[i].J2));
		REQUIRE (std::memcmp (&a.con[i].Wn, &b.con[i].Wn, sizeof (double)) == 0);
		REQUIRE (std::memcmp (&a.con[i].Wt, &b.con[i].Wt, sizeof (double)) == 0);
	}
}

TEST_CASE ("U14 SPECULATIVE limit in both phases; positive-gap point like SPECULATIVE, the other as TOI", "[CollSolve]")
{
	for (double g : { 0.0, 9.81 })
		for (double u : { 0.5, 1.0, 3.0 }) {
			double h = 0.1, tau = 0.5, tr = (1 - tau)*h, gap = 0.05;
			CollIsland isl; isl.tau = tau; isl.h = h;
			isl.body.push_back (Dyn (1000, Vector (0.667, 0.667, 0.667), Vector (0, 1, 0), Vector (0, -u, 0), IMatrix (), Vector (), tr, Vector (0, -g, 0)));
			isl.body.push_back (Kin (Vector (0, -1, 0)));
			Vector xs = isl.body[0].x1 - isl.body[0].v1*tr;
			for (double dx : { -1.0, 1.0 })
				for (double dz : { -1.0, 1.0 }) {
					CollSContact c = Con (0, 1, xs + Vector (dx, -1, dz), Vector (0, 1, 0), COLL_SPECULATIVE);
					c.gap = gap;
					isl.con.push_back (c);
				}
			REQUIRE (isl.Solve (CollSolveParams ()));
			Vector v1, w1;
			Phase1Vel (isl.body[0], v1, w1);
			CollDelta d;
			isl.Delta (0, d);
			double allowed = gap/tr, ua = -v1.y, uf = -(isl.body[0].v1 + d.dv).y;
			double gapEnd = gap - tr*0.5*(ua + uf);
			std::printf ("U14: g %.2f u %.1f: approach after phase 1 %.6f, at t1 %.6f (allowed %.3f), final gap %.2e, dKE %.3f J\n", g, u, ua, uf, allowed, gapEnd, Work1 (isl));
			if (u <= allowed && g == 0.0) REQUIRE (isl.body[0].dP1.length () + isl.body[0].dP2.length () == 0.0);
			REQUIRE (ua <= allowed + 1e-6);
			REQUIRE (uf <= allowed + 1e-6);
			if (u > allowed) REQUIRE (std::fabs (ua - allowed) < 1e-6);
			REQUIRE (gapEnd >= -1e-9);
			if (u == 3.0 && g == 0.0) REQUIRE (std::fabs (Work1 (isl) - 0.5*1000*(9.0 - 1.0)) < 4000.0*1e-6); // 8.3: 4000 J
		}
	// INACCURATE per adapter rule: gap +0.03 point SPECULATIVE (e = 0), gap -0.002 stays TOI with e
	double h = 0.1, tau = 0.5, tr = (1 - tau)*h;
	CollIsland isl; isl.tau = tau; isl.h = h;
	isl.body.push_back (Dyn (500, Vector (1, 1, 1), Vector (-5, 1, 0), Vector (0, -3, 0), IMatrix (), Vector (), tr));
	isl.body.push_back (Dyn (500, Vector (1, 1, 1), Vector (5, 1, 0), Vector (0, -3, 0), IMatrix (), Vector (), tr));
	isl.body.push_back (Kin (Vector (0, -1, 0)));
	CollSContact c0 = Con (0, 2, isl.body[0].x1 - isl.body[0].v1*tr + Vector (0, -1, 0), Vector (0, 1, 0), COLL_SPECULATIVE); c0.gap = 0.03;
	CollSContact c1 = Con (1, 2, isl.body[1].x1 - isl.body[1].v1*tr + Vector (0, -1, 0), Vector (0, 1, 0), COLL_TOI); c1.gap = -0.002;
	isl.con.push_back (c0); isl.con.push_back (c1);
	REQUIRE (isl.Solve (CollSolveParams ()));
	Vector va, vb, w;
	Phase1Vel (isl.body[0], va, w); Phase1Vel (isl.body[1], vb, w);
	REQUIRE (std::fabs (va.y + 0.03/tr) < 1e-9);
	REQUIRE (std::fabs (vb.y - CollRestitution (3.0, COLL_E0, COLL_VY, CollSolveParams ())*3.0) < 1e-9);
}

TEST_CASE ("U15 damage energy is frame invariant; two dynamic bodies: CoM-frame KE loss", "[CollSolve]")
{
	double ref = 0, refWf = 0;
	for (double V : { 0.0, 465.0, 3.0e4 }) {
		double h = 1.0/60.0, tau = 0.5, tr = (1 - tau)*h;
		Matrix R = ExpW (Vector (0, 0, 0.2));
		CollIsland isl; isl.tau = tau; isl.h = h;
		Vector xt (V*0.3, 0, 0);
		isl.body.push_back (Dyn (1000, Vector (0.667, 0.8, 0.5), xt, Vector (V + 2.0, -5.0, 0), R, Vector (), tr));
		isl.body.push_back (Kin (xt - Vector (0, 2, 0)));
		std::vector<Vector> cw;
		double ymin = 1e300;
		for (const Vector &c : BoxCorners (1, 1, 1)) { cw.push_back (mul (R, c)); ymin = std::min (ymin, cw.back ().y); }
		for (const Vector &c : cw)
			if (c.y < ymin + 1e-9) {
				CollSContact k = Con (0, 1, xt + c, Vector (0, 1, 0));
				k.vkb_t = k.vkb_1 = Vector (V, 0, 0); k.flags = COLLP_FIRST;
				isl.con.push_back (k);
			}
		REQUIRE (isl.con.size () == 2);
		REQUIRE (isl.Solve (CollSolveParams ()));
		double dKE = Work1 (isl, true), Wf = 0;
		for (const CollSContact &c : isl.con) Wf -= c.Wt;
		std::printf ("U15: roof %8g m/s: dKE %.10e J, Wf %.6e J\n", V, dKE, Wf);
		if (V == 0.0) { ref = dKE; refWf = Wf; }
		REQUIRE (std::fabs (dKE - ref) <= 1e-9*ref);
		REQUIRE (std::fabs (Wf - refWf) <= 1e-9*ref);
		REQUIRE (dKE > 0);
	}
	Rng r (15);
	double worst = 0;
	for (int t = 0; t < 200; t++) {
		CollIsland isl; isl.tau = r.U (0, 0.9); isl.h = 0.05;
		double tr = (1 - isl.tau)*isl.h;
		for (int i = 0; i < 2; i++)
			isl.body.push_back (Dyn (std::pow (10.0, r.U (1, 5)), Vector (r.U (0.2, 20), r.U (0.2, 20), r.U (0.2, 20)), Vector (0, 0, i ? -3.0 : 3.0), r.V (5.0), RotOf (r.Q ()), r.V (0.5), tr));
		Vector n = Vector (0, 0, 1) + r.V (0.2);
		n = n/n.length ();
		for (int k = 0; k < 1 + t % 4; k++) isl.con.push_back (Con (0, 1, r.V (1.0), n, COLL_TOI, r.U (0, 1), r.U (0, 0.6), 1.0));
		REQUIRE (isl.Solve (CollSolveParams ()));
		Vector P; double M = 0;
		for (const CollSBody &b : isl.body) { P += b.vt*b.m; M += b.m; }
		Vector vc = P/M;
		double E0 = 0, E1 = 0;
		for (const CollSBody &b : isl.body) { Vector v1, w1; Phase1Vel (b, v1, w1); E0 += KE (b, b.vt, b.wt, vc); E1 += KE (b, v1, w1, vc); }
		worst = std::max (worst, std::fabs (Work1 (isl) - (E0 - E1))/E0);
	}
	std::printf ("U15: two dynamic bodies: worst |dKE - KE loss| / KE %.2e\n", worst);
	REQUIRE (worst <= 1e-12);
}

TEST_CASE ("U16 FIRST-point work independent of frame length and kind; resting box 0 J", "[CollSolve]")
{
	for (double u : { 0.5, 1.4, 3.0, 5.0 }) {
		double ref = -1;
		for (double h : { 1.0/200.0, 1.0/60.0, 0.1 })
			for (uint8_t kind : { (uint8_t)COLL_RESTING, (uint8_t)COLL_TOI }) {
				double tau = kind == COLL_RESTING ? 0.0 : 0.37, tr = (1 - tau)*h;
				CollIsland isl; isl.tau = tau; isl.h = h;
				isl.body.push_back (Dyn (1000, Vector (0.667, 0.667, 0.667), Vector (0, 1.02, 0), Vector (0, -u, 0), IMatrix (), Vector (), tr));
				isl.body.push_back (Kin (Vector (0, -1, 0)));
				Vector xs = isl.body[0].x1 - isl.body[0].v1*tr;
				for (const Vector &c : BoxCorners (1, 1, 1))
					if (c.y < 0) {
						CollSContact k = Con (0, 1, xs + c, Vector (0, 1, 0), kind);
						k.flags = COLLP_FIRST;
						isl.con.push_back (k);
					}
				CollSContact nf = Con (0, 1, xs + Vector (0, -1, 0), Vector (0, 1, 0), kind); // a non-FIRST point adds nothing
				isl.con.push_back (nf);
				REQUIRE (isl.Solve (CollSolveParams ()));
				double dKE = Work1 (isl, true);
				if (ref < 0) ref = dKE;
				REQUIRE (std::fabs (dKE - ref) <= 1e-9*ref);
			}
		double e = CollRestitution (u, COLL_E0, COLL_VY, CollSolveParams ()), closed = 0.5*1000*u*u*(1 - e*e);
		std::printf ("U16: u %.1f m/s: FIRST-point dKE %.6f J equal over h and kind (all-points closed form %.6f J)\n", u, ref, closed);
		REQUIRE (ref > 0);
	}
	// all points FIRST: the closed form 0.5 m u^2 (1 - e^2) (8.3 rows)
	for (double u : { 0.5, 1.4, 3.0, 5.0 }) {
		double h = 1.0/60.0, tr = h;
		CollIsland isl; isl.tau = 0; isl.h = h;
		isl.body.push_back (Dyn (1000, Vector (0.667, 0.667, 0.667), Vector (0, 1.02, 0), Vector (0, -u, 0), IMatrix (), Vector (), tr));
		isl.body.push_back (Kin (Vector (0, -1, 0)));
		Vector xs = isl.body[0].x1 - isl.body[0].v1*tr;
		for (const Vector &c : BoxCorners (1, 1, 1))
			if (c.y < 0) { CollSContact k = Con (0, 1, xs + c, Vector (0, 1, 0), COLL_RESTING); k.flags = COLLP_FIRST; isl.con.push_back (k); }
		REQUIRE (isl.Solve (CollSolveParams ()));
		double e = CollRestitution (u, COLL_E0, COLL_VY, CollSolveParams ()), closed = 0.5*1000*u*u*(1 - e*e);
		std::printf ("U16: u %.1f m/s, 4 FIRST corners: dKE %.6f J, closed form %.6f J\n", u, Work1 (isl, true), closed);
		REQUIRE (std::fabs (Work1 (isl, true) - closed) <= 1e-6*closed);
	}
	// resting box under gravity: no approach, no phase-1 work
	for (double h : { 1.0/60.0, 0.17 }) {
		CollIsland isl; isl.tau = 0; isl.h = h;
		isl.body.push_back (Dyn (1000, Vector (0.667, 0.667, 0.667), Vector (0, 1.045, 0), Vector (), IMatrix (), Vector (), h, Vector (0, -9.81, 0)));
		isl.body.push_back (Kin (Vector (0, -1, 0)));
		for (const Vector &c : BoxCorners (1, 1, 1))
			if (c.y < 0) { CollSContact k = Con (0, 1, isl.body[0].xt + c, Vector (0, 1, 0), COLL_RESTING); k.flags = COLLP_FIRST; isl.con.push_back (k); }
		REQUIRE (isl.Solve (CollSolveParams ()));
		REQUIRE (Work1 (isl, true) == 0.0);
		CollDelta d;
		isl.Delta (0, d);
		REQUIRE (std::fabs ((isl.body[0].v1 + d.dv).y) < 1e-6);
	}
}

TEST_CASE ("U17 surface velocity: kinematic term on one side", "[CollSolve]")
{
	double h = 0.1, tau = 0.4, tr = (1 - tau)*h;
	Matrix Rc = RotOf (Rng (17).Q ());
	CollIsland w[2];
	for (int k = 0; k < 2; k++) {
		CollIsland &isl = w[k];
		isl.tau = tau; isl.h = h;
		Vector vp = k ? Vector (0, 0.2, 0.05) : Vector ();                   // equal relative velocity on the payload instead of the door
		isl.body.push_back (Dyn (2000, Vector (0.8, 0.9, 0.6), Vector (0.2, 3.7, 0), vp, IMatrix (), Vector (), tr));
		isl.body.push_back (Dyn (1e5, Vector (20, 25, 8), Vector (0, 0, 0), Vector (), Rc, Vector (), tr));
		Vector p = Vector (0.2, 4.3, 0.1);
		CollSContact c = Con (0, 1, p, Vector (0, -1, 0));
		if (!k) { c.vkb_t = Vector (0, -0.2, -0.05); c.vkb_1 = Vector (0, -0.2, -0.05); }
		isl.con.push_back (c);
		REQUIRE (isl.Solve (CollSolveParams ()));
	}
	REQUIRE ((w[0].con[0].J1 - w[1].con[0].J1).length () <= 1e-12*w[1].con[0].J1.length ());
	REQUIRE (w[0].con[0].J1.length () > 0);
	for (CollIsland &isl : w) {
		Vector dP = isl.body[0].dP1 + isl.body[0].dP2 + isl.body[1].dP1 + isl.body[1].dP2;
		REQUIRE (dP.length () == 0.0);                                       // the part gains nothing: all of it is on the two rigid bodies
		std::vector<St> s0;
		for (const CollSBody &b : isl.body) s0.push_back (St (b.m, b.pmi*b.m, b.x1, b.v1, b.wb1, Quaternion (b.R1)));
		std::vector<St> s1 (s0);
		std::vector<CollDelta> ds (2);
		for (int i = 0; i < 2; i++) { isl.Delta (i, ds[i]); REQUIRE (s1[i].Apply (ds[i])); }
		Vector dP1, dL1;
		DeltaPL (s0, s1, ds, dP1, dL1);
		double J = isl.con[0].J1.length () + isl.con[0].J2.length ();
		REQUIRE (dP1.length () <= 1e-12*J);
		REQUIRE (dL1.length () <= 1e-12*J*10.0);
	}
}

TEST_CASE ("Y3' point work: a centred normal impact on four corners does no friction work; a sliding one keeps the energy sum", "[CollSolve]")
{
	for (double slip : { 0.0, 2.0 })
		for (double u : { 1.308, 3.0 }) {
			double h = 1.0/60.0, tr = h;
			CollIsland isl; isl.tau = 0; isl.h = h;
			isl.body.push_back (Dyn (1000, Vector (0.667, 0.667, 0.667), Vector (0, 1.02, 0), Vector (slip, -u, 0), IMatrix (), Vector (), tr));
			isl.body.push_back (Kin (Vector (0, -1, 0)));
			Vector xs = isl.body[0].x1 - isl.body[0].v1*tr;
			for (const Vector &c : BoxCorners (1, 1, 1))
				if (c.y < 0) { CollSContact k = Con (0, 1, xs + c, Vector (0, 1, 0), COLL_RESTING); k.flags = COLLP_FIRST; isl.con.push_back (k); }
			REQUIRE (isl.Solve (CollSolveParams ()));
			double Wn = 0, Wt = 0;
			for (const CollSContact &c : isl.con) { Wn -= c.Wn; Wt -= c.Wt; }
			Vector v, w;
			Phase1Vel (isl.body[0], v, w);
			double loss = KE (isl.body[0], isl.body[0].vt, isl.body[0].wt, Vector ()) - KE (isl.body[0], v, w, Vector ());
			double e = CollRestitution (u, COLL_E0, COLL_VY, CollSolveParams ()), closed = 0.5*1000*u*u*(1 - e*e);
			std::printf ("Y3' point work: slip %.1f m/s, approach %.3f m/s: normal %.6f J, friction %.3e J, KE loss %.6f J (normal closed form %.6f J)\n", slip, u, Wn, Wt, loss, closed);
			REQUIRE (std::fabs ((Wn + Wt) - loss) <= 1e-9*loss);    // the point sum is the kinetic energy lost (8.2)
			if (slip == 0.0) {
				REQUIRE (std::fabs (Wt) <= 1e-9*loss);              // no slip, no friction work (iteration transients do not count)
				REQUIRE (std::fabs (Wn - closed) <= 1e-6*closed);
			} else REQUIRE (Wt > 0.01*loss);
		}
}
