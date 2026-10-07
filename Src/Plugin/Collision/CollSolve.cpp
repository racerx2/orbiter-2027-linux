// not upstream: contact response (D3 4, 6): two-phase island solver, friction, correction; Vecmat

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include "CollSolve.h"

namespace {

constexpr double TR_MIN = 1e-12; // smallest time span for a SPECULATIVE bias [s]

// phase lever/position: xs = x1 - tr v1, dx = tr dP1/m; xm = x1a - 0.5 tr v1a, dx = 0.5 tr dP2/m
double PhaseShift (int phase)
{
	return phase == 1 ? 1.0 : 0.5;
}

bool Finite (const Vector &v)
{
	return std::isfinite (v.x) && std::isfinite (v.y) && std::isfinite (v.z);
}

bool Movable (const CollSBody &b)
{
	return b.dyn && b.m > 0.0;
}

// R diag(1/Ib) R^T; an axis with Ib <= 0 is locked (2.3)
Matrix InvInertiaW (const Matrix &R, const Vector &Ib)
{
	double ix = Ib.x > 0.0 ? 1.0/Ib.x : 0.0, iy = Ib.y > 0.0 ? 1.0/Ib.y : 0.0, iz = Ib.z > 0.0 ? 1.0/Ib.z : 0.0;
	Matrix M;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			M(i,j) = R(i,0)*R(j,0)*ix + R(i,1)*R(j,1)*iy + R(i,2)*R(j,2)*iz;
	return M;
}

// body-frame Ib^-1 L, locked axes 0
Vector InvIb (const Vector &Ib, const Vector &L)
{
	return Vector (Ib.x > 0.0 ? L.x/Ib.x : 0.0, Ib.y > 0.0 ? L.y/Ib.y : 0.0, Ib.z > 0.0 ? L.z/Ib.z : 0.0);
}

// working velocities of one phase
struct Work {
	std::vector<double> im;
	std::vector<Matrix> Iw;
	std::vector<Vector> v, w;
	void Resize (size_t n) { im.assign (n, 0.0); Iw.assign (n, Matrix ()); v.assign (n, Vector ()); w.assign (n, Vector ()); }
};

// per-contact geometry of one phase
struct Geo {
	Vector ra, rb, n, vka, vkb, J;
	double wn, wt;
};

Vector RelVel (const Work &k, int a, int b, const Geo &g)
{
	return (k.v[a] + Xc (k.w[a], g.ra) + g.vka) - (k.v[b] + Xc (k.w[b], g.rb) + g.vkb);
}

double EffMass (const Work &k, int a, int b, const Vector &ra, const Vector &rb, const Vector &d)
{
	double s = k.im[a] + k.im[b] + dotp (d, Xc (mul (k.Iw[a], Xc (ra, d)), ra)) + dotp (d, Xc (mul (k.Iw[b], Xc (rb, d)), rb));
	return s > 0.0 ? 1.0/s : 0.0;
}

void Apply (Work &k, int a, int b, const Geo &g, const Vector &J)
{
	k.v[a] += J*k.im[a]; k.w[a] += mul (k.Iw[a], Xc (g.ra, J));
	k.v[b] -= J*k.im[b]; k.w[b] -= mul (k.Iw[b], Xc (g.rb, J));
}

void Tangents (const Vector &n, Vector &t1, Vector &t2)
{
	Vector a = std::fabs (n.x) < 0.6 ? Vector (1, 0, 0) : Vector (0, 1, 0);
	t1 = Xc (n, a); t1 /= t1.length ();
	t2 = Xc (n, t1);
}

void SetupContact (const Work &k, CollSContact &c, Geo &g)
{
	Tangents (g.n, c.t1, c.t2);
	c.kn = EffMass (k, c.a, c.b, g.ra, g.rb, g.n);
	c.kt[0] = EffMass (k, c.a, c.b, g.ra, g.rb, c.t1);
	c.kt[1] = EffMass (k, c.a, c.b, g.ra, g.rb, c.t2);
	c.ln = 0.0; c.lt[0] = c.lt[1] = 0.0;
	g.J = Vector (); g.wn = g.wt = 0.0;
}

// sequential impulses of one phase with work accumulation (4.5); count-based, early out on converge
void RunPhase (Work &k, std::vector<CollSContact> &con, std::vector<Geo> &geo, int nit)
{
	double scale = 0.0;
	for (size_t i = 0; i < con.size (); i++) {
		const CollSContact &c = con[i];
		Vector u = RelVel (k, c.a, c.b, geo[i]);
		scale = std::max (scale, c.kn*(u.length () + std::fabs (c.bias)));
	}
	double tol = 1e-12*scale;
	for (int it = 0; it < nit; it++) {
		double dmax = 0.0;
		for (size_t i = 0; i < con.size (); i++) {
			CollSContact &c = con[i];
			Geo &g = geo[i];
			if (c.kn <= 0.0) continue;
			Vector u0 = RelVel (k, c.a, c.b, g);
			double l0 = c.lt[0] - dotp (u0, c.t1)*c.kt[0], l1 = c.lt[1] - dotp (u0, c.t2)*c.kt[1];
			double lim = c.mu*c.ln, nr = std::sqrt (l0*l0 + l1*l1);
			if (nr > lim) { double s = lim/nr; l0 *= s; l1 *= s; }
			double d0 = l0 - c.lt[0], d1 = l1 - c.lt[1];
			c.lt[0] = l0; c.lt[1] = l1;
			Vector dJt = c.t1*d0 + c.t2*d1;
			Apply (k, c.a, c.b, g, dJt); g.J += dJt;
			Vector u1 = RelVel (k, c.a, c.b, g);
			g.wt += dotp (dJt, u0 + u1)*0.5;
			double ln = std::max (0.0, c.ln - (dotp (u1, g.n) - c.bias)*c.kn);
			double dl = ln - c.ln; c.ln = ln;
			Vector dJn = g.n*dl;
			Apply (k, c.a, c.b, g, dJn); g.J += dJn;
			Vector u2 = RelVel (k, c.a, c.b, g);
			g.wn += dotp (dJn, u1 + u2)*0.5;
			dmax = std::max (dmax, std::max (std::fabs (dl), std::max (std::fabs (d0), std::fabs (d1))));
		}
		if (dmax <= tol) break;
	}
}

} // namespace

// e(v) of D3 4.3
double CollRestitution (double v, double e0, double vy, const CollSolveParams &p)
{
	if (!(v >= p.vrest) || v >= p.vd) return 0.0;
	double e = e0;
	if (v > vy) e *= std::sqrt (std::sqrt (std::max (vy, 0.0)/v));
	if (v > p.vp) e *= (p.vd - v)/(p.vd - p.vp);
	return e;
}

// nit = clamp(ceil(iters h / 0.167), iters, 3 iters); 1e-9 keeps exact multiples from rounding up
int CollIterations (double h, int iters)
{
	if (iters < 1) iters = 1;
	if (!(h > 0.0)) return iters;
	double x = std::ceil (iters*h/COLL_ITER_FRAME - 1e-9);
	if (!(x > iters)) return iters;
	if (x >= 3.0*iters) return 3*iters;
	return (int)x;
}

// body-frame rotation vector in substeps <= COLL_ROT_SUBSTEP by the integrator's Quaternion::Rotate
void CollRotate (Quaternion &q, const Vector &dth)
{
	double a = dth.length ();
	if (!(a > 0.0) || !std::isfinite (a)) return;
	double ns = std::ceil (a/COLL_ROT_SUBSTEP);
	int n = ns > 65536.0 ? 65536 : (int)ns;
	Vector step = dth/(double)n;
	for (int i = 0; i < n; i++) q.Rotate (step);
}

// 6.2 state math: v, x, attitude, omega from world spin momentum; locked axes keep omega; clamped
bool CollApplyDeltaState (Vector &x, Vector &v, Quaternion &q, Vector &wb, const Vector &Ib, const CollDelta &d)
{
	Matrix R; R.Set (q);
	Vector Lw = mul (R, Ib*wb) + d.dLw;
	Vector dth (Ib.x > 0.0 ? d.dth.x : 0.0, Ib.y > 0.0 ? d.dth.y : 0.0, Ib.z > 0.0 ? d.dth.z : 0.0);
	Vector nx = x + d.dx, nv = v + d.dv;
	Quaternion nq (q);
	CollRotate (nq, dth);
	Matrix R2; R2.Set (nq);
	Vector Lb = tmul (R2, Lw);
	Vector nw (Ib.x > 0.0 ? Lb.x/Ib.x : wb.x, Ib.y > 0.0 ? Lb.y/Ib.y : wb.y, Ib.z > 0.0 ? Lb.z/Ib.z : wb.z);
	if (!Finite (nx) || !Finite (nv) || !Finite (nw) || !std::isfinite (nq.qs) || !std::isfinite (nq.qvx) || !std::isfinite (nq.qvy) || !std::isfinite (nq.qvz)) {
		CollLog (COLLLOG_ERROR, "CollApplyDeltaState: non-finite result, delta dropped");
		return false;
	}
	double wm = nw.length ();
	if (wm > COLL_OMEGA_MAX) {
		nw *= COLL_OMEGA_MAX/wm;
		CollLog (COLLLOG_WARN, "CollApplyDeltaState: angular velocity %g rad/s clamped to %g", wm, COLL_OMEGA_MAX);
	}
	x = nx; v = nv; q.Set (nq); wb = nw;
	return true;
}

// phase 1 at tau with restitution and energy guard, then phase 2 over [tau, t1] at carried point
bool CollIsland::Solve (const CollSolveParams &p)
{
	const size_t nb = body.size (), nc = con.size ();
	const double tr = (1.0 - tau)*h;
	const int nit = CollIterations (h, p.iters);
	guard = false; nonconv = 0; W1 = W2 = S1 = S2 = 0.0;
	xs.assign (nb, Vector ()); xm.assign (nb, Vector ());
	upre.assign (nc, Vector ()); upost.assign (nc, Vector ());
	for (CollSBody &b : body) b.dP1 = b.dL1 = b.dP2 = b.dL2 = Vector ();

	Work k; k.Resize (nb);
	for (size_t i = 0; i < nb; i++) {
		const CollSBody &b = body[i];
		if (Movable (b)) {
			xs[i] = b.x1 - b.v1*(tr*PhaseShift (1));
			k.im[i] = 1.0/b.m; k.Iw[i] = InvInertiaW (b.Rt, b.pmi*b.m);
			k.v[i] = b.vt; k.w[i] = b.wt;
		} else xs[i] = b.xt;
	}
	std::vector<Geo> geo (nc);
	for (size_t i = 0; i < nc; i++) {
		CollSContact &c = con[i];
		Geo &g = geo[i];
		g.ra = c.p - xs[c.a]; g.rb = c.p - xs[c.b]; g.n = c.n; g.vka = c.vka_t; g.vkb = c.vkb_t;
		upre[i] = RelVel (k, c.a, c.b, g);
		c.vapp = std::max (0.0, -dotp (upre[i], c.n));
	}

	// phase 1; norest: the energy guard's redo without restitution
	auto phase1 = [&] (bool norest) {
		for (size_t i = 0; i < nb; i++)
			if (Movable (body[i])) { k.v[i] = body[i].vt; k.w[i] = body[i].wt; }
		for (size_t i = 0; i < nc; i++) {
			CollSContact &c = con[i];
			if (c.kind == COLL_SPECULATIVE) c.bias = -c.gap/std::max (tr, TR_MIN);
			else if (norest || (c.flags & COLLP_DEGENERATE)) c.bias = 0.0;
			else c.bias = CollRestitution (c.vapp, c.e0, c.vy, p)*c.vapp;
			SetupContact (k, c, geo[i]);
		}
		RunPhase (k, con, geo, nit);
		W1 = S1 = 0.0;
		for (size_t i = 0; i < nc; i++) {
			W1 += geo[i].wn + geo[i].wt;
			S1 += std::fabs (con[i].ln)*(con[i].vapp + 1e-3);
		}
	};
	phase1 (false);
	if (W1 > 1e-9*S1) {
		CollLog (COLLLOG_FINE, "CollSolve: phase-1 work %g J > 0 with restitution, redone with e = 0", W1);
		guard = true;
		phase1 (true);
	}
	for (size_t i = 0; i < nc; i++) {
		CollSContact &c = con[i];
		const Geo &g = geo[i];
		c.ln1 = c.ln; c.J1 = g.J;
		upost[i] = RelVel (k, c.a, c.b, g);
		// per-point work from total impulse and pre/post relative velocity (Y3'); sums to W1, no fake slip
		Vector us = upre[i] + upost[i];
		double jn = dotp (g.J, g.n);
		c.Wn = 0.5*jn*dotp (us, g.n);
		c.Wt = 0.5*dotp (g.J - g.n*jn, us);
		body[c.a].dP1 += g.J; body[c.a].dL1 += Xc (g.ra, g.J);
		body[c.b].dP1 -= g.J; body[c.b].dL1 -= Xc (g.rb, g.J);
	}

	// phase 2: tentative t1 state after phase 1, lever origins xm, carried point, n2
	for (size_t i = 0; i < nb; i++) {
		const CollSBody &b = body[i];
		k.im[i] = 0.0; k.Iw[i] = Matrix (); k.v[i] = k.w[i] = Vector ();
		if (!Movable (b)) { xm[i] = b.xt; continue; }
		Vector Ib = b.pmi*b.m;
		Vector v1a = b.v1 + b.dP1/b.m;
		Vector x1a = b.x1 + b.dP1*(tr*PhaseShift (1)/b.m);
		Vector Lb = tmul (b.R1, mul (b.R1, Ib*b.wb1) + b.dL1);
		Vector wb1a (Ib.x > 0.0 ? Lb.x/Ib.x : b.wb1.x, Ib.y > 0.0 ? Lb.y/Ib.y : b.wb1.y, Ib.z > 0.0 ? Lb.z/Ib.z : b.wb1.z);
		xm[i] = x1a - v1a*(tr*PhaseShift (2));
		k.im[i] = 1.0/b.m; k.Iw[i] = InvInertiaW (b.R1, Ib);
		k.v[i] = v1a; k.w[i] = mul (b.R1, wb1a);
	}
	for (size_t i = 0; i < nc; i++) {
		CollSContact &c = con[i];
		Geo &g = geo[i];
		Vector sh;
		int nd = 0;
		if (Movable (body[c.a])) { sh += xm[c.a] - xs[c.a]; nd++; }
		if (Movable (body[c.b])) { sh += xm[c.b] - xs[c.b]; nd++; }
		Vector pm = nd ? c.p + sh/(double)nd : c.p;
		double n2l = c.n2.length ();
		g.ra = pm - xm[c.a]; g.rb = pm - xm[c.b];
		g.n = n2l > 0.5 ? c.n2/n2l : c.n;
		g.vka = c.vka_1; g.vkb = c.vkb_1;
		c.bias = c.kind == COLL_SPECULATIVE ? -c.gap/std::max (tr, TR_MIN) : 0.0;
		SetupContact (k, c, g);
	}
	std::vector<double> un2 (nc);
	for (size_t i = 0; i < nc; i++) un2[i] = std::fabs (dotp (RelVel (k, con[i].a, con[i].b, geo[i]), geo[i].n));
	RunPhase (k, con, geo, nit);
	for (size_t i = 0; i < nc; i++) {
		CollSContact &c = con[i];
		const Geo &g = geo[i];
		c.ln2 = c.ln; c.J2 = g.J;
		W2 += g.wn + g.wt;
		S2 += std::fabs (c.ln)*(un2[i] + 1e-3);
		body[c.a].dP2 += g.J; body[c.a].dL2 += Xc (g.ra, g.J);
		body[c.b].dP2 -= g.J; body[c.b].dL2 -= Xc (g.rb, g.J);
		if (c.kn > 0.0 && dotp (RelVel (k, c.a, c.b, g), g.n) - c.bias < -COLL_NONCONV) nonconv++;
	}
	if (nonconv) CollLog (COLLLOG_FINE, "CollSolve: %d contacts still approaching after phase 2", nonconv);

	bool ok = std::isfinite (W1) && std::isfinite (W2);
	for (const CollSBody &b : body) ok = ok && Finite (b.dP1) && Finite (b.dL1) && Finite (b.dP2) && Finite (b.dL2);
	for (const CollSContact &c : con) ok = ok && Finite (c.J1) && Finite (c.J2) && std::isfinite (c.Wn) && std::isfinite (c.Wt);
	if (!ok) {
		CollLog (COLLLOG_ERROR, "CollSolve: non-finite island response dropped (%d bodies, %d contacts)", (int)nb, (int)nc);
		for (CollSBody &b : body) b.dP1 = b.dL1 = b.dP2 = b.dL2 = Vector ();
	}
	return ok;
}

// write-back deltas of dynamic body i (4.2): phase factors from PhaseShift, as Solve's lever origin
void CollIsland::Delta (int i, CollDelta &d) const
{
	d = CollDelta ();
	const CollSBody &b = body[i];
	if (!Movable (b)) return;
	const double tr = (1.0 - tau)*h, f1 = PhaseShift (1), f2 = PhaseShift (2);
	d.dv = (b.dP1 + b.dP2)/b.m;
	d.dx = (b.dP1*f1 + b.dP2*f2)*(tr/b.m);
	d.dLw = b.dL1 + b.dL2;
	d.dth = InvIb (b.pmi*b.m, tmul (b.R1, b.dL1*f1 + b.dL2*f2))*tr;
}

// effective mass along n at p with the phase-1 lever origins and inertia at tau (8.1 meff)
double CollIsland::Meff (const Vector &p, const Vector &n, int a, int b) const
{
	Work k; k.Resize (body.size ());
	for (int i : { a, b }) {
		const CollSBody &s = body[i];
		if (Movable (s)) { k.im[i] = 1.0/s.m; k.Iw[i] = InvInertiaW (s.Rt, s.pmi*s.m); }
	}
	Vector oa = (size_t)a < xs.size () ? xs[a] : body[a].xt, ob = (size_t)b < xs.size () ? xs[b] : body[b].xt;
	return EffMass (k, a, b, p - oa, p - ob, n);
}

// split-impulse correction (4.6): solver effective masses at tau, levers xt; v and spin untouched
bool CollIsland::Correct (const CollSolveParams &p, std::vector<CollDelta> &d)
{
	const size_t nb = body.size ();
	d.assign (nb, CollDelta ());
	Work k; k.Resize (nb);
	for (size_t i = 0; i < nb; i++) {
		const CollSBody &b = body[i];
		if (Movable (b)) { k.im[i] = 1.0/b.m; k.Iw[i] = InvInertiaW (b.Rt, b.pmi*b.m); }
	}
	struct Pc { int a, b; Vector ra, rb, n; double kn, target, P; };
	std::vector<Pc> pc;
	for (const CollSContact &c : con) {
		if (!(c.gap < -p.slop)) continue;
		Pc e { c.a, c.b, c.p - body[c.a].xt, c.p - body[c.b].xt, c.n, 0.0, std::min (p.beta*(-p.slop - c.gap), p.dxmax), 0.0 };
		e.kn = EffMass (k, e.a, e.b, e.ra, e.rb, e.n);
		if (e.kn > 0.0) pc.push_back (e);
	}
	if (pc.empty ()) return true;
	std::vector<Vector> dx (nb), dq (nb);
	for (int sw = 0; sw < COLL_PC_SWEEPS; sw++)
		for (Pc &e : pc) {
			double s = dotp (e.n, (dx[e.a] + Xc (dq[e.a], e.ra)) - (dx[e.b] + Xc (dq[e.b], e.rb)));
			double P = std::max (0.0, e.P + (e.target - s)*e.kn);
			Vector J = e.n*(P - e.P);
			e.P = P;
			dx[e.a] += J*k.im[e.a]; dq[e.a] += mul (k.Iw[e.a], Xc (e.ra, J));
			dx[e.b] -= J*k.im[e.b]; dq[e.b] -= mul (k.Iw[e.b], Xc (e.rb, J));
		}
	bool ok = true;
	for (size_t i = 0; i < nb; i++) {
		const CollSBody &b = body[i];
		if (!Movable (b)) continue;
		Vector Ib = b.pmi*b.m, th = tmul (b.Rt, dq[i]);
		d[i].dx = dx[i];
		d[i].dth = Vector (Ib.x > 0.0 ? th.x : 0.0, Ib.y > 0.0 ? th.y : 0.0, Ib.z > 0.0 ? th.z : 0.0);
		ok = ok && Finite (d[i].dx) && Finite (d[i].dth);
	}
	if (!ok) {
		CollLog (COLLLOG_ERROR, "CollSolve: non-finite position correction dropped");
		d.assign (nb, CollDelta ());
	}
	return ok;
}
