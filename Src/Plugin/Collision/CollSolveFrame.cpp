// not upstream: detect -> solve adapter (D3 3, 5.4): islands, TOI rounds, wake, correction, impacts

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include "CollSolve.h"

namespace {

Matrix QMatrix (const Quaternion &q)
{
	Matrix R; R.Set (q);
	return R;
}

bool Zero (const CollDelta &d)
{
	return d.dv.length2 () == 0.0 && d.dx.length2 () == 0.0 && d.dLw.length2 () == 0.0 && d.dth.length2 () == 0.0;
}

// surface velocity clamp |vs| <= vsmax (2.6)
Vector ClampVs (const Vector &vs, double vmax)
{
	double l = vs.length ();
	if (!(l > vmax)) return vs;
	CollLog (COLLLOG_FINE, "Collision: surface velocity %.2f m/s clamped to %.2f", l, vmax);
	return vs*(vmax/l);
}

// world inverse inertia R diag(1/Ib) R^T, locked axes 0
Vector InvInertiaMul (const Matrix &R, const Vector &Ib, const Vector &L)
{
	Vector Lb = tmul (R, L);
	return mul (R, Vector (Ib.x > 0.0 ? Lb.x/Ib.x : 0.0, Ib.y > 0.0 ? Lb.y/Ib.y : 0.0, Ib.z > 0.0 ? Lb.z/Ib.z : 0.0));
}

// approach speed of a result at its tau and its smallest skin sum (3.3 join rule)
void ResultApproach (const CollDetect &det, const CollPairResult &r, double &vapp, double &skin)
{
	vapp = 0.0; skin = 2.0*COLL_SKIN_DEFAULT;
	const CollBody &A = det.Body (r.bodyA), &B = det.Body (r.bodyB);
	for (int i = 0; i < r.npt; i++) {
		const CollContact &c = r.pt[i];
		Vector va = r.a.v + Xc (r.a.w, c.pA - r.a.c) + c.vsA, vb = r.b.v + Xc (r.b.w, c.pB - r.b.c) + c.vsB;
		vapp = std::max (vapp, -dotp (va - vb, c.n));
		if (c.partA < A.parts.size () && c.partB < B.parts.size ()) {
			double s = A.parts[c.partA].skin + B.parts[c.partB].skin;
			skin = i ? std::min (skin, s) : s;
		}
	}
}

bool SameKey (const CollOwnerKey &a, const CollOwnerKey &b)
{
	return !(a < b) && !(b < a);
}

// smallest-index root of a union-find
int Root (std::vector<int> &u, int i)
{
	while (u[i] != i) { u[i] = u[u[i]]; i = u[i]; }
	return i;
}

void Join (std::vector<int> &u, int a, int b)
{
	a = Root (u, a); b = Root (u, b);
	if (a != b) { if (a < b) u[b] = a; else u[a] = b; }
}

using Rec = CollEventRec;

} // namespace

// D3 9: first 10 warnings verbatim, then one summary per minute of sim time
void CollFrameSolver::Warn (double t, const char *msg)
{
	if (nWarn < 10) {
		nWarn++;
		CollLog (COLLLOG_WARN, "Collision check: %s", msg);
		if (nWarn == 10) tWarn = t;
		return;
	}
	nQuiet++;
	if (t - tWarn >= 60.0) {
		CollLog (COLLLOG_WARN, "Collision check: %d more warnings in the last %.0f s", nQuiet, t - tWarn);
		nQuiet = 0; tWarn = t;
	}
}

// one P1 frame (D3 3.2-3.4, 5.4, 6.6, 4.6, 8, 9); on return res holds the solved results in order
void CollFrameSolver::Run (CollDetect &det, std::vector<CollPairResult> &res, std::vector<CollFrameBody> &body, double h, double simt0,
	const CollSolveParams &p, int rounds, CollSolveHost &host, std::vector<CollBodyDelta> &delta, std::vector<CollImpactEvent> &ev)
{
	stats = CollSolveStats {};
	delta.clear (); ev.clear ();
	inacc.erase (std::remove_if (inacc.begin (), inacc.end (), [simt0] (const CollInaccLog &x) { return simt0 - x.t >= 60.0; }), inacc.end ());
	const int nb = (int)body.size ();
	if (rounds < 1) rounds = 1;
	for (CollFrameBody &b : body) {
		b.woke = b.impulsive = b.loadRest = false;
		b.nVesselContacts = b.nBuildingContacts = 0;
		b.allSlow = true;
		b.sup.clear ();
	}
	// working t1 state per body, kept in step with the deltas as CollWorld applies them
	std::vector<Vector> sx (nb), sv (nb), swb (nb);
	std::vector<Quaternion> sq;
	std::vector<char> dyn (nb);
	for (int i = 0; i < nb; i++) {
		const CollFrameBody &b = body[i];
		sx[i] = b.x1; sv[i] = b.v1; swb[i] = b.wb1; sq.push_back (b.q1);
		dyn[i] = b.dyn && b.m > 0.0;
	}
	auto valid = [&] (const CollPairResult &r) {
		return r.kind != COLL_NONE && r.npt > 0 && r.npt <= 16 && r.bodyA >= 0 && r.bodyB >= 0 && r.bodyA < nb && r.bodyB < nb && r.bodyA < det.nBody () && r.bodyB < det.nBody ();
	};
	auto sorted = [] (std::vector<CollPairResult> &v) {
		std::vector<int> ix (v.size ());
		std::iota (ix.begin (), ix.end (), 0);
		std::stable_sort (ix.begin (), ix.end (), [&] (int a, int b) {
			const CollPairResult &x = v[a], &y = v[b];
			if (x.tau != y.tau) return x.tau < y.tau;
			if (x.bodyA != y.bodyA) return x.bodyA < y.bodyA;
			return x.bodyB < y.bodyB;
		});
		std::vector<CollPairResult> o;
		o.reserve (v.size ());
		for (int i : ix) o.push_back (v[i]);
		v.swap (o);
	};
	std::vector<CollPairResult> pending, solved;
	for (const CollPairResult &r : res) if (valid (r)) pending.push_back (r);
	std::vector<Rec> rec;

	// a CORE result waits while a RESTING result of its pair is pending (re-sweep replaces it, rule 2)
	auto coreWaits = [] (const std::vector<CollPairResult> &v, const CollPairResult &r) {
		if (!(r.flags & COLLF_CORE)) return false;
		for (const CollPairResult &q : v) if (q.kind == COLL_RESTING && q.bodyA == r.bodyA && q.bodyB == r.bodyB) return true;
		return false;
	};

	// solve one island: results ix of cur at tauI; false if dropped; rs: re-sweep picks (b < 0: rule 2)
	struct Restart { int b; CollRestart r; };
	auto solveIsland = [&] (const std::vector<CollPairResult> &cur, const std::vector<int> &ix, double tauI, std::vector<Restart> &rs) -> bool {
		rs.clear ();
		std::vector<int> map (nb, -1), member, atRes (nb, -1);
		std::vector<char> wokeHere (nb, 0);
		bool resting = false;
		for (int k : ix) {
			const CollPairResult &r = cur[k];
			if (atRes[r.bodyA] < 0) atRes[r.bodyA] = k;
			if (atRes[r.bodyB] < 0) atRes[r.bodyB] = k;
			resting = resting || r.kind == COLL_RESTING;
		}
		for (int pass = 0; pass < 2; pass++) {             // pass 1 re-solves with woken LANDED bodies dynamic (6.6)
			member.clear ();
			std::fill (map.begin (), map.end (), -1);
			for (int i = 0; i < nb; i++) if (atRes[i] >= 0 && dyn[i]) member.push_back (i);
			for (int i = 0; i < nb; i++) if (atRes[i] >= 0 && !dyn[i]) member.push_back (i);
			int o = -1;
			for (int i : member) if (dyn[i] && (o < 0 || body[i].id < body[o].id)) o = i;
			if (o < 0) return false;
			const Vector O = sx[o];                        // island origin: x1 of the dynamic body with the smallest id (3.2)
			CollIsland isl;
			isl.tau = tauI; isl.h = h;
			for (size_t j = 0; j < member.size (); j++) {
				int f = member[j];
				map[f] = (int)j;
				const CollPairResult &r = cur[atRes[f]];
				const CollBodyAt &at = f == r.bodyA ? r.a : r.b;
				CollSBody sb {};
				sb.dyn = dyn[f] != 0; sb.m = body[f].m; sb.pmi = body[f].pmi;
				sb.Rt = QMatrix (at.q);
				sb.xt = (r.origin - O) + at.c; sb.vt = at.v; sb.wt = at.w;
				if (sb.dyn) {
					sb.R1 = QMatrix (sq[f]);
					sb.x1 = sx[f] - O; sb.v1 = sv[f]; sb.wb1 = swb[f];
				} else {
					sb.R1 = QMatrix (det.Body (f).m.Rot (1.0));
					sb.x1 = sb.xt;
				}
				isl.body.push_back (sb);
			}
			std::vector<int> srcRes, srcPt;
			std::vector<double> vsMax;
			for (int k : ix) {
				const CollPairResult &r = cur[k];
				const Vector off = r.origin - O;
				const CollMotion &ma = det.Body (r.bodyA).m, &mb = det.Body (r.bodyB).m;
				Matrix RA = (dyn[r.bodyA] ? QMatrix (sq[r.bodyA]) : QMatrix (ma.Rot (1.0)))*transp (QMatrix (r.a.q)); // rotation of each side from tau to t1
				Matrix RB = (dyn[r.bodyB] ? QMatrix (sq[r.bodyB]) : QMatrix (mb.Rot (1.0)))*transp (QMatrix (r.b.q));
				for (int i = 0; i < r.npt; i++) {
					const CollContact &pt = r.pt[i];
					Vector mid = (pt.pA + pt.pB)*0.5;
					CollSContact c {};
					c.a = map[r.bodyA]; c.b = map[r.bodyB];
					c.p = off + mid; c.n = pt.n;
					c.n2 = mul (RA, pt.n) + mul (RB, pt.n);         // unnormalised: CollSolve takes pt.n when the two turns cancel (|n2| < 0.5)
					if (r.kind == COLL_SPECULATIVE) { c.kind = COLL_SPECULATIVE; c.gap = r.specGap; }
					else if ((r.flags & COLLF_INACCURATE) && pt.gap > 0.0) { c.kind = COLL_SPECULATIVE; c.gap = pt.gap; }
					else { c.kind = r.kind; c.gap = pt.gap; }
					c.flags = pt.flags;
					CollSMat sa = host.Material (r, i, 0), sb = host.Material (r, i, 1);
					c.e0 = std::max (sa.e0, sb.e0); c.vy = std::min (sa.vy, sb.vy); c.mu = std::sqrt (std::max (0.0, sa.mu*sb.mu));
					Vector vsA = ClampVs (pt.vsA, p.vsmax), vsB = ClampVs (pt.vsB, p.vsmax);
					Vector vs1A = ClampVs (det.SurfaceVel (r, i, 0, 1.0), p.vsmax), vs1B = ClampVs (det.SurfaceVel (r, i, 1, 1.0), p.vsmax);
					if (dyn[r.bodyA]) { c.vka_t = vsA; c.vka_1 = vs1A; }
					else {                                     // kinematic field at the point, and at the carried point at t1 (2.5)
						c.vka_t = r.a.v + Xc (r.a.w, mid - r.a.c) + vsA;
						c.vka_1 = ma.Vel (1.0) + Xc (ma.Omega (1.0), mul (RA, mid - r.a.c)) + vs1A;
					}
					if (dyn[r.bodyB]) { c.vkb_t = vsB; c.vkb_1 = vs1B; }
					else {
						c.vkb_t = r.b.v + Xc (r.b.w, mid - r.b.c) + vsB;
						c.vkb_1 = mb.Vel (1.0) + Xc (mb.Omega (1.0), mul (RB, mid - r.b.c)) + vs1B;
					}
					isl.con.push_back (c);
					srcRes.push_back (k); srcPt.push_back (i);
					vsMax.push_back (std::max (std::max (vsA.length (), vsB.length ()), std::max (vs1A.length (), vs1B.length ())));
				}
			}
			if (!isl.Solve (p)) {
				CollLog (COLLLOG_ERROR, "Collision: island at tau %.4f dropped (non-finite response)", tauI);
				return false;
			}
			// LANDED wake by impulse share on the kinematic solve (6.6), then one more solve with it dynamic
			bool woke = false;
			if (pass == 0)
				for (size_t j = 0; j < member.size (); j++) {
					int f = member[j];
					if (dyn[f] || !body[f].wakeable || !(body[f].m > 0.0)) continue;
					const CollSBody &s = isl.body[j];
					if ((s.dP1 + s.dP2).length ()/body[f].m > COLL_V_WAKE || InvInertiaMul (s.R1, body[f].pmi*body[f].m, s.dL1 + s.dL2).length () > COLL_W_WAKE) {
						body[f].woke = true; wokeHere[f] = 1; dyn[f] = 1; woke = true;
						sv[f] = body[f].wakeV1; swb[f] = body[f].wakeWb1;
						CollLog (COLLLOG_INFO, "Collision: LANDED body %u woken", body[f].id);
					}
				}
			if (woke) continue;
			stats.islands++;
			stats.guards += isl.guard ? 1 : 0;
			stats.nonconverged += isl.nonconv;

			const double tr = (1.0 - tauI)*h, tw = simt0 + tauI*h;
			double sumJ = 0.0, rmax = 0.0;
			for (const CollSContact &c : isl.con) { sumJ += c.J1.length () + c.J2.length (); rmax = std::max (rmax, c.p.length ()); }
			CollLog (COLLLOG_FINE, "Collision: island at tau %.4f: %d bodies, %d contacts, sum |J| %.3e N s", tauI, (int)member.size (), (int)isl.con.size (), sumJ);

			// write-back deltas in application order, all or nothing; nothing below COLL_IMPULSE_END (5.4)
			std::vector<CollDelta> d (member.size ());
			std::vector<Vector> nx, nv, nw;
			std::vector<Quaternion> nq;
			for (size_t j = 0; j < member.size (); j++) {
				int f = member[j];
				if (sumJ >= COLL_IMPULSE_END) isl.Delta ((int)j, d[j]);
				nx.push_back (sx[f]); nv.push_back (sv[f]); nw.push_back (swb[f]); nq.push_back (sq[f]);
				if (!dyn[f] || Zero (d[j])) continue;
				if (!CollApplyDeltaState (nx[j], nv[j], nq[j], nw[j], body[f].pmi*body[f].m, d[j])) {
					CollLog (COLLLOG_ERROR, "Collision: island at tau %.4f dropped (write-back)", tauI);
					return false;
				}
			}
			if (check) {                                       // D3 9
				char m[200];
				Vector sP, sL;
				for (size_t j = 0; j < member.size (); j++) {
					const CollSBody &s = isl.body[j];
					sP += s.dP1 + s.dP2;
					sL += Xc (isl.xs[j], s.dP1) + s.dL1 + Xc (isl.xm[j], s.dP2) + s.dL2;
				}
				if (sP.length () > 1e-10*sumJ || sL.length () > 1e-10*sumJ*(rmax + 1.0)) {
					std::snprintf (m, sizeof (m), "impulse-level momentum %.3e N s, %.3e N m s (sum |J| %.3e)", sP.length (), sL.length (), sumJ);
					Warn (tw, m);
				}
				if (isl.W1 > 1e-9*isl.S1 || isl.W2 > 1e-9*isl.S2) {
					std::snprintf (m, sizeof (m), "phase work rises: %.3e J, %.3e J", isl.W1, isl.W2);
					Warn (tw, m);
				}
				bool allDyn = true;
				for (int f : member) allDyn = allDyn && dyn[f];
				if (allDyn) {                                  // t1 level from the deltas in the island frame
					Vector P, dP, dL;
					double M = 0.0, Ps = 0.0, Ls = 0.0;
					for (const CollSBody &s : isl.body) { P += s.v1*s.m; M += s.m; }
					for (size_t j = 0; j < member.size (); j++) {
						const CollSBody &s = isl.body[j];
						Vector Ib = s.pmi*s.m, L0 = mul (s.R1, Ib*s.wb1), mdv = d[j].dv*s.m;
						Ps += s.m*(s.v1 - P/M).length ();
						Ls += Xc (s.x1, (s.v1 - P/M)*s.m).length () + L0.length ();
						dP += mdv;
						dL += Xc (s.x1, mdv) + Xc (d[j].dx, s.v1*s.m) + Xc (d[j].dx, mdv) + (mul (QMatrix (nq[j]), Ib*nw[j]) - L0);
					}
					if (dP.length () > 1e-9*(Ps + sumJ) || dL.length () > 1e-9*(Ls + sumJ*(rmax + 1.0))) {
						std::snprintf (m, sizeof (m), "t1 momentum %.3e N s, %.3e N m s", dP.length (), dL.length ());
						Warn (tw, m);
					}
				}
			}
			for (size_t j = 0; j < member.size (); j++) {
				int f = member[j];
				if (!dyn[f]) continue;
				const CollSBody &s = isl.body[j];
				Vector Ib = body[f].pmi*body[f].m;
				if (s.dP1.length ()/body[f].m > COLL_FR_DV || InvInertiaMul (s.Rt, Ib, s.dL1).length () > COLL_FR_DW) body[f].impulsive = true;
				bool slow = true;
				for (const CollSContact &c : isl.con) if (((int)j == c.a || (int)j == c.b) && c.vapp >= p.vrest) slow = false;
				if (slow && tr > 0.0 && s.dP2.length ()/(body[f].m*tr) >= COLL_A_LOAD) body[f].loadRest = true;
				if (Zero (d[j])) continue;
				sx[f] = nx[j]; sv[f] = nv[j]; swb[f] = nw[j]; sq[f].Set (nq[j]);
				delta.push_back (CollBodyDelta { f, d[j], false });
			}
			// solved results, then one record per point for events, supports and correction
			std::vector<int> pos (cur.size (), -1);
			for (int k : ix) {
				det.Solved (cur[k]);
				pos[k] = (int)solved.size ();
				solved.push_back (cur[k]);
			}
			size_t r0 = rec.size ();
			for (size_t ci = 0; ci < isl.con.size (); ci++) {
				const CollSContact &c = isl.con[ci];
				const CollPairResult &r = cur[srcRes[ci]];
				int i = srcPt[ci];
				Rec e {};
				e.t = -1.0;
				e.res = pos[srcRes[ci]]; e.pt = i; e.con = (int)ci;
				e.oa = det.Owner (r, i, 0); e.ob = det.Owner (r, i, 1);
				e.kind = c.kind; e.gap = c.gap;
				e.vapp = c.vapp; e.ln1 = c.ln1; e.Wn = c.Wn; e.Wt = c.Wt;
				e.vpost = dotp (isl.upost[ci], c.n);
				e.slip = (isl.upre[ci] - c.n*dotp (isl.upre[ci], c.n)).length ();
				e.Jt = (c.J1 - c.n*c.ln1).length ();
				e.jsum = c.J1.length () + c.J2.length ();
				e.surf = vsMax[ci] > COLL_SURFVEL_EV;
				e.woke = wokeHere[r.bodyA] || wokeHere[r.bodyB];
				rec.push_back (e);
				for (int side = 0; side < 2; side++) {
					int f = side ? r.bodyB : r.bodyA, g = side ? r.bodyA : r.bodyB;
					if (!dyn[f]) continue;
					if (det.Body (g).kind == COLLB_BASE) body[f].nBuildingContacts++;
					else body[f].nVesselContacts++;
					if (c.vapp >= p.vrest) body[f].allSlow = false;
				}
			}
			// effective mass along n per owner pair at its impulse-weighted centroid in this island (8.1 meff)
			std::vector<char> mdone (rec.size () - r0, 0);
			for (size_t a = r0; a < rec.size (); a++) {
				if (mdone[a - r0]) continue;
				Vector cp, cn;
				double ws = 0.0;
				for (size_t b = a; b < rec.size (); b++) {
					if (!SameKey (rec[a].oa, rec[b].oa) || !SameKey (rec[a].ob, rec[b].ob)) continue;
					const CollSContact &c = isl.con[rec[b].con];
					double w = rec[b].ln1 > 0.0 ? rec[b].ln1 : 1e-30;
					cp += c.p*w; cn += c.n*w; ws += w;
				}
				double l = cn.length ();
				const CollSContact &ca = isl.con[rec[a].con];
				double me = l > 0.0 ? isl.Meff (cp/ws, cn/l, ca.a, ca.b) : 0.0;
				for (size_t b = a; b < rec.size (); b++)
					if (SameKey (rec[a].oa, rec[b].oa) && SameKey (rec[a].ob, rec[b].ob)) { rec[b].meff = me; mdone[b - r0] = 1; }
			}
			// re-sweep candidates (3.1): rule 1 for bodies the island changed, rule 2 for RESTING island bodies
			for (size_t j = 0; j < member.size (); j++) {
				int f = member[j];
				if (!dyn[f]) continue;
				const CollSBody &s = isl.body[j];
				double sj = 0.0;
				for (const CollSContact &c : isl.con) if ((int)j == c.a || (int)j == c.b) sj += c.J1.length () + c.J2.length ();
				bool changed = wokeHere[f] || sj >= COLL_IMPULSE_END;
				if (!changed && !resting) continue;
				Restart q;
				q.b = changed ? f : -1 - f;
				if (sumJ >= COLL_IMPULSE_END) {                // the phase-1 jump at tau that was written back
					q.r.dv = s.dP1/body[f].m;
					q.r.dwg = InvInertiaMul (s.Rt, body[f].pmi*body[f].m, s.dL1);
				}
				q.r.c1 = sx[f]; q.r.v1 = sv[f]; q.r.q1.Set (sq[f]); q.r.w1g = mul (QMatrix (sq[f]), swb[f]);
				rs.push_back (q);
			}
			return true;
		}
		return false;
	};

	// TOI rounds (5.4)
	int round = 0;
	for (; round < rounds && !pending.empty (); round++) {
		stats.rounds++;
		sorted (pending);
		std::vector<CollPairResult> cur;
		cur.swap (pending);
		std::vector<int> islOf (nb, -1);
		std::vector<std::vector<int>> isl;
		std::vector<double> islTau;
		std::vector<char> wait (cur.size (), 0);
		for (size_t k = 0; k < cur.size (); k++) {
			const CollPairResult &r = cur[k];
			if (coreWaits (cur, r)) { wait[k] = 1; continue; }
			int a = dyn[r.bodyA] ? r.bodyA : -1, b = dyn[r.bodyB] ? r.bodyB : -1;
			if (a < 0 && b < 0) { CollLog (COLLLOG_FINE, "Collision: result without a dynamic body dropped"); continue; }
			int ia = a >= 0 ? islOf[a] : -1, ib = b >= 0 ? islOf[b] : -1;
			if (ia < 0 && ib < 0) {
				isl.push_back (std::vector<int> (1, (int)k)); islTau.push_back (r.tau);
				if (a >= 0) islOf[a] = (int)isl.size () - 1;
				if (b >= 0) islOf[b] = (int)isl.size () - 1;
				continue;
			}
			int I = ia >= 0 ? ia : ib;
			if (ia >= 0 && ib >= 0 && ia != ib) { wait[k] = 1; continue; }
			double vapp, skin;
			ResultApproach (det, r, vapp, skin);
			if ((r.tau - islTau[I])*h > skin/std::max (vapp, 1e-3)) { wait[k] = 1; continue; }
			isl[I].push_back ((int)k);
			if (a >= 0) islOf[a] = I;
			if (b >= 0) islOf[b] = I;
		}
		for (size_t k = 0; k < cur.size (); k++) if (wait[k]) pending.push_back (cur[k]);
		std::vector<char> gone (cur.size (), 0);           // replaced by a re-sweep of this round (D2 6.4 item 4)
		for (size_t I = 0; I < isl.size (); I++) {
			std::vector<int> ix;
			for (int k : isl[I]) if (!gone[k]) ix.push_back (k);
			if (ix.empty ()) continue;
			const double tauI = cur[ix[0]].tau;
			// tau order: an island waits a round while one of its dynamic bodies has an earlier pending result
			std::vector<char> mine (nb, 0);
			for (int k : ix) { mine[cur[k].bodyA] = dyn[cur[k].bodyA]; mine[cur[k].bodyB] = dyn[cur[k].bodyB]; }
			bool defer = false;
			for (const CollPairResult &x : pending) defer = defer || (x.tau < tauI && (mine[x.bodyA] || mine[x.bodyB]));
			if (defer) {
				for (int k : ix) pending.push_back (cur[k]);
				CollLog (COLLLOG_FINE, "Collision: island at tau %.4f waits for an earlier result of its bodies", tauI);
				continue;
			}
			std::vector<Restart> rs;
			if (!solveIsland (cur, ix, tauI, rs)) continue;
			for (const Restart &q : rs) {
				int f = q.b < 0 ? -1 - q.b : q.b;
				if (q.b < 0) {                                 // rule 2: a body of a RESTING island with a CORE result still pending
					bool core = false;
					for (const CollPairResult &x : pending) core = core || ((x.flags & COLLF_CORE) && (x.bodyA == f || x.bodyB == f));
					if (!core) continue;
				}
				std::vector<CollPairResult> out, keep;
				det.Resweep (f, tauI, q.r, out);
				stats.resweeps++;
				for (const CollPairResult &x : pending) if (x.bodyA != f && x.bodyB != f) keep.push_back (x);
				for (const CollPairResult &x : out) if (valid (x)) keep.push_back (x);
				pending.swap (keep);
				for (size_t J = I + 1; J < isl.size (); J++)     // results of later islands with f (a woken body sits in several) came from its old motion
					for (int k : isl[J]) if (cur[k].bodyA == f || cur[k].bodyB == f) gone[k] = 1;
			}
		}
	}
	// after the round cap: results sharing a dynamic body form one island at earliest tau, no re-sweep
	if (!pending.empty ()) {
		sorted (pending);
		std::vector<CollPairResult> cur;
		cur.swap (pending);
		std::vector<int> u (nb);
		std::iota (u.begin (), u.end (), 0);
		std::vector<char> use (cur.size (), 0);
		for (size_t k = 0; k < cur.size (); k++) {
			const CollPairResult &r = cur[k];
			if (coreWaits (cur, r) || (!dyn[r.bodyA] && !dyn[r.bodyB])) continue;
			use[k] = 1;
			if (dyn[r.bodyA] && dyn[r.bodyB]) Join (u, r.bodyA, r.bodyB);
		}
		std::vector<int> done (cur.size (), 0);
		for (size_t k = 0; k < cur.size (); k++) {
			if (!use[k] || done[k]) continue;
			const CollPairResult &r0 = cur[k];
			int root = Root (u, dyn[r0.bodyA] ? r0.bodyA : r0.bodyB);
			std::vector<int> ix;
			for (size_t j = k; j < cur.size (); j++) {
				const CollPairResult &r = cur[j];
				if (!use[j] || done[j]) continue;
				if (Root (u, dyn[r.bodyA] ? r.bodyA : r.bodyB) == root) { ix.push_back ((int)j); done[j] = 1; }
			}
			std::vector<Restart> rs;
			if (solveIsland (cur, ix, r0.tau, rs)) stats.exhausted += (int)ix.size ();
		}
		CollLog (COLLLOG_INFO, "Collision: TOI round cap %d reached, %d results solved without re-sweep", rounds, stats.exhausted);
	}

	// position correction (4.6): non-FIRST points below -slop, SUPPORT points; one Correct per group
	{
		std::vector<int> u (nb);
		std::iota (u.begin (), u.end (), 0);
		std::vector<int> sel;
		for (size_t k = 0; k < rec.size (); k++) {
			const CollPairResult &r = solved[rec[k].res];
			uint8_t fl = r.pt[rec[k].pt].flags;
			if (!(rec[k].gap < -p.slop) || ((fl & COLLP_FIRST) && !(fl & COLLP_SUPPORT))) continue;
			if (!dyn[r.bodyA] && !dyn[r.bodyB]) continue;
			sel.push_back ((int)k);
			if (dyn[r.bodyA] && dyn[r.bodyB]) Join (u, r.bodyA, r.bodyB);
		}
		std::vector<char> done (sel.size (), 0);
		for (size_t s = 0; s < sel.size (); s++) {
			if (done[s]) continue;
			const CollPairResult &r0 = solved[rec[sel[s]].res];
			int root = Root (u, dyn[r0.bodyA] ? r0.bodyA : r0.bodyB);
			std::vector<int> grp;
			for (size_t t = s; t < sel.size (); t++) {
				const CollPairResult &r = solved[rec[sel[t]].res];
				if (!done[t] && Root (u, dyn[r.bodyA] ? r.bodyA : r.bodyB) == root) { grp.push_back (sel[t]); done[t] = 1; }
			}
			std::vector<int> map (nb, -1), member, atRes (nb, -1);
			for (int k : grp) {
				const CollPairResult &r = solved[rec[k].res];
				if (atRes[r.bodyA] < 0) atRes[r.bodyA] = rec[k].res;
				if (atRes[r.bodyB] < 0) atRes[r.bodyB] = rec[k].res;
			}
			for (int i = 0; i < nb; i++) if (atRes[i] >= 0 && dyn[i]) member.push_back (i);
			for (int i = 0; i < nb; i++) if (atRes[i] >= 0 && !dyn[i]) member.push_back (i);
			int o = -1;
			for (int i : member) if (dyn[i] && (o < 0 || body[i].id < body[o].id)) o = i;
			const Vector O = sx[o];
			// island at t1 for the correction: t1 states, points and normals carried rigidly from result tau
			auto at1 = [&] (int f, const CollPairResult &r, Matrix &R, Vector &x) {
				if (dyn[f]) { R = QMatrix (sq[f]); x = sx[f] - O; return; }
				const CollMotion &m = det.Body (f).m;     // kinematic: its state at the result tau moved on along its path
				R = QMatrix (m.q1);
				x = (r.origin - O) + (f == r.bodyA ? r.a.c : r.b.c) + (m.c1 - m.Pos (r.tau));
			};
			CollIsland ci;
			ci.tau = 1.0; ci.h = h;
			for (size_t j = 0; j < member.size (); j++) {
				int f = member[j];
				map[f] = (int)j;
				CollSBody sb {};
				sb.dyn = dyn[f] != 0; sb.m = body[f].m; sb.pmi = body[f].pmi;
				at1 (f, solved[atRes[f]], sb.Rt, sb.xt);
				sb.R1 = sb.Rt; sb.x1 = sb.xt;
				ci.body.push_back (sb);
			}
			for (int k : grp) {
				const CollPairResult &r = solved[rec[k].res];
				const CollContact &pt = r.pt[rec[k].pt];
				Matrix RA, RB;
				Vector xA, xB, mid = (pt.pA + pt.pB)*0.5;
				at1 (r.bodyA, r, RA, xA); at1 (r.bodyB, r, RB, xB);
				RA = RA*transp (QMatrix (r.a.q)); RB = RB*transp (QMatrix (r.b.q)); // turn of each side from the result tau to t1
				Vector n1 = mul (RA, pt.n) + mul (RB, pt.n);
				double l = n1.length ();
				CollSContact c {};
				c.a = map[r.bodyA]; c.b = map[r.bodyB];
				c.p = ((xA + mul (RA, mid - r.a.c)) + (xB + mul (RB, mid - r.b.c)))*0.5;
				c.n = l > 0.0 ? n1/l : pt.n; c.n2 = c.n;
				c.gap = rec[k].gap; c.kind = rec[k].kind; c.flags = pt.flags;
				ci.con.push_back (c);
			}
			std::vector<CollDelta> d;
			if (!ci.Correct (p, d)) continue;
			bool moved = false;
			for (size_t j = 0; j < member.size (); j++) {
				int f = member[j];
				if (!dyn[f] || Zero (d[j])) continue;
				if (!CollApplyDeltaState (sx[f], sv[f], sq[f], swb[f], body[f].pmi*body[f].m, d[j])) continue;
				delta.push_back (CollBodyDelta { f, d[j], true });
				moved = true;
			}
			if (moved) for (int k : grp) rec[k].corrected = true;
		}
	}

	CollFillEvents (det, solved, rec, h, simt0, p, host, ev, &body, &inacc);
	res.swap (solved);
}

// events per owner pair and supports per dynamic body from the solved points (8.1, 8.2, 7.5); shared by Run and the addon driver
void CollFillEvents (const CollDetect &det, const std::vector<CollPairResult> &solved, const std::vector<CollEventRec> &rec, double h, double simt0,
	const CollSolveParams &p, CollSolveHost &host, std::vector<CollImpactEvent> &ev, std::vector<CollFrameBody> *body, std::vector<CollInaccLog> *inacc)
{
	// impact events, one per owner pair (8.1, 8.2); queued if FIRST or not SLOW, only with an impulse
	std::vector<int> order (rec.size ());
	std::iota (order.begin (), order.end (), 0);
	std::stable_sort (order.begin (), order.end (), [&] (int a, int b) {
		if (rec[a].oa < rec[b].oa) return true;
		if (rec[b].oa < rec[a].oa) return false;
		return rec[a].ob < rec[b].ob;
	});
	for (size_t g0 = 0; g0 < order.size ();) {
		size_t g1 = g0 + 1;
		while (g1 < order.size () && SameKey (rec[order[g1]].oa, rec[order[g0]].oa) && SameKey (rec[order[g1]].ob, rec[order[g0]].ob)) g1++;
		CollImpactEvent e {};
		double sumLn = 0.0, tauMin = 1e300, jsum = 0.0, tEv = -1.0;
		bool first = false, resting = true, slow = true;
		int best = order[g0];
		for (size_t k = g0; k < g1; k++) {
			const Rec &q = rec[order[k]];
			const CollPairResult &r = solved[q.res];
			uint8_t fl = r.pt[q.pt].flags;
			if (fl & COLLP_FIRST) { first = true; e.dKE -= q.Wn + q.Wt; e.Wf -= q.Wt; }
			if (r.kind != COLL_RESTING) resting = false;
			if (q.vapp >= p.vrest) slow = false;
			if (q.ln1 > rec[best].ln1) best = order[k];
			sumLn += q.ln1; jsum += q.jsum;
			e.vn += q.ln1*q.vapp; e.vn_post += q.ln1*q.vpost; e.vt += q.ln1*q.slip;
			e.Jt += q.Jt;
			tauMin = std::min (tauMin, r.tau);
			if (q.t >= 0.0) tEv = tEv < 0.0 ? q.t : std::min (tEv, q.t);
			if (r.kind == COLL_SPECULATIVE) e.flags |= COLLEV_SPECULATIVE;
			if (r.flags & COLLF_INACCURATE) e.flags |= COLLEV_INACCURATE;
			if (fl & COLLP_DEGENERATE) e.flags |= COLLEV_DEGENERATE;
			if (q.surf) e.flags |= COLLEV_SURFVEL;
			if (q.woke) e.flags |= COLLEV_WOKE_LANDED;
			if (q.corrected) e.flags |= COLLEV_POSCORR;
		}
		if (first) e.flags |= COLLEV_FIRST;
		if (resting) e.flags |= COLLEV_RESTING;
		if (slow) e.flags |= COLLEV_SLOW;
		const CollOwnerKey &ka = rec[order[g0]].oa, &kb = rec[order[g0]].ob;
		if (e.flags & COLLEV_INACCURATE) {                         // rate-limited warning per owner pair (3.2)
			std::vector<CollInaccLog> dummy;
			std::vector<CollInaccLog> &il = inacc ? *inacc : dummy;
			auto it = std::lower_bound (il.begin (), il.end (), std::make_pair (ka, kb), [] (const CollInaccLog &x, const std::pair<CollOwnerKey, CollOwnerKey> &k) {
				return x.a < k.first || (!(k.first < x.a) && x.b < k.second); });
			if (it == il.end () || !SameKey (it->a, ka) || !SameKey (it->b, kb)) {
				CollLog (COLLLOG_WARN, "Collision: INACCURATE contact (motion model error above tolerance), owners %u/%d.%d.%d - %u/%d.%d.%d at t %.3f",
					ka.id, ka.planet, ka.base, ka.obj, kb.id, kb.planet, kb.base, kb.obj, simt0 + tauMin*h);
				il.insert (it, CollInaccLog { ka, kb, simt0 });
			}
		}
		if (e.flags & COLLEV_DEGENERATE)
			CollLog (COLLLOG_FINE, "Collision: fallback normal, owners %u/%d.%d.%d - %u/%d.%d.%d", ka.id, ka.planet, ka.base, ka.obj, kb.id, kb.planet, kb.base, kb.obj);
		if (sumLn > 0.0) { e.vn /= sumLn; e.vn_post /= sumLn; e.vt /= sumLn; }
		else e.vn = e.vn_post = e.vt = 0.0;
		e.Jn = sumLn;
		e.meff = rec[best].meff;
		e.t = tEv >= 0.0 ? tEv : simt0 + tauMin*h;
		const CollPairResult &rb = solved[rec[best].res];
		for (int side = 0; side < 2; side++) {
			CollImpactSide &s = e.s[side];
			s.owner = CollOwnerRefOf (side ? rec[best].ob : rec[best].oa);
			s.mesh = s.grp = s.tri = -1;
			host.Feature (rb, rec[best].pt, side, s);
			int fb = side ? rb.bodyB : rb.bodyA;
			uint16_t part = side ? rb.pt[rec[best].pt].partB : rb.pt[rec[best].pt].partA;
			std::vector<Vector> pp, nn;
			std::vector<double> w;
			double ws = 0.0;
			for (size_t k = g0; k < g1; k++) {
				const Rec &q = rec[order[k]];
				const CollPairResult &r = solved[q.res];
				const CollContact &pt = r.pt[q.pt];
				if ((side ? r.bodyB : r.bodyA) != fb || (side ? pt.partB : pt.partA) != part) continue;
				Vector x = (pt.pA + pt.pB)*0.5, n = pt.n;
				det.ToPartFrame (r, q.pt, side, x, n);
				pp.push_back (x); nn.push_back (side ? n : -n); w.push_back (q.ln1); ws += q.ln1;
			}
			Vector c, n;
			for (size_t k = 0; k < pp.size (); k++) {
				double wk = ws > 0.0 ? w[k]/ws : 1.0/pp.size ();
				c += pp[k]*wk; n += nn[k]*wk;
			}
			double l = n.length ();
			s.c = c; s.n = l > 0.0 ? n/l : n;
			s.a = 0.0;
			for (const Vector &x : pp) s.a = std::max (s.a, (x - c).length ());
		}
		if ((first || !slow) && jsum > 0.0) ev.push_back (e);       // queue rule of 8.1; a pair that exchanged no impulse made no impact
		g0 = g1;
	}

	// supports: buildings touched by each dynamic body, mean outward normal in the base frame (7.5)
	for (int f = 0; body && f < (int)body->size (); f++) {
		CollFrameBody &bf = (*body)[f];
		if (!((bf.dyn && bf.m > 0.0) || bf.woke)) continue;
		std::vector<CollOwnerKey> key;
		std::vector<Vector> nsum;
		for (const Rec &q : rec) {
			const CollPairResult &r = solved[q.res];
			int side;
			if (r.bodyA == f && det.Body (r.bodyB).kind == COLLB_BASE) side = 1;
			else if (r.bodyB == f && det.Body (r.bodyA).kind == COLLB_BASE) side = 0;
			else continue;
			const CollContact &pt = r.pt[q.pt];
			Vector x = (pt.pA + pt.pB)*0.5, n = pt.n;
			det.ToPartFrame (r, q.pt, side, x, n);
			const CollOwnerKey &k = side ? q.ob : q.oa;
			size_t j = 0;
			while (j < key.size () && !SameKey (key[j], k)) j++;
			if (j == key.size ()) { key.push_back (k); nsum.push_back (Vector ()); }
			nsum[j] += side ? n : -n;
		}
		std::vector<int> ix (key.size ());
		std::iota (ix.begin (), ix.end (), 0);
		std::stable_sort (ix.begin (), ix.end (), [&] (int a, int b) { return key[a] < key[b]; });
		for (int j : ix) {
			double l = nsum[j].length ();
			bf.sup.push_back (CollSupport { key[j], l > 0.0 ? nsum[j]/l : nsum[j] });
		}
	}
}
