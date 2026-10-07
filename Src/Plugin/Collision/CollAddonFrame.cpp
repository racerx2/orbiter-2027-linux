// not upstream: collision addon E1 5.3, frame driver at t0: records, TOUCH and FREE paths, past check, speculative contacts, delivery
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <numeric>
#include "CollAddonFrame.h"

namespace {

Matrix QM (const Quaternion &q) { Matrix R; R.Set (q); return R; }
bool QEq (const Quaternion &a, const Quaternion &b) { return a.qs == b.qs && a.qvx == b.qvx && a.qvy == b.qvy && a.qvz == b.qvz; }
double QAngle (const Quaternion &a, const Quaternion &b)
{
	double d = std::fabs (a.qs*b.qs + a.qvx*b.qvx + a.qvy*b.qvy + a.qvz*b.qvz);
	return 2.0*std::acos (std::min (1.0, d));
}
Vector RotVec (const Matrix &Rrel)                      // log map of a rotation matrix
{
	double c = 0.5*(Rrel(0,0) + Rrel(1,1) + Rrel(2,2) - 1.0);
	c = std::max (-1.0, std::min (1.0, c));
	double a = std::acos (c);
	Vector w (Rrel(2,1) - Rrel(1,2), Rrel(0,2) - Rrel(2,0), Rrel(1,0) - Rrel(0,1));
	if (a < 1e-12) return w*0.5;
	return w*(a/(2.0*std::sin (a)));
}
Vector TurnOf (const Quaternion &from, const Quaternion &to) // body-frame th with CollRotate (from, th) ~ to
{
	Vector th = RotVec (transp (QM (from))*QM (to))*-1.0;
	for (int it = 0; it < 3; it++) {
		Quaternion t (from);
		CollRotate (t, th);
		th += RotVec (transp (QM (t))*QM (to))*-1.0;
	}
	return th;
}
Vector Mulc (const Vector &a, const Vector &b) { return Vector (a.x*b.x, a.y*b.y, a.z*b.z); }
Vector Divc (const Vector &a, const Vector &b) { return Vector (b.x > 0 ? a.x/b.x : 0.0, b.y > 0 ? a.y/b.y : 0.0, b.z > 0 ? a.z/b.z : 0.0); }
void Couple (CollOrbState &c, const Vector &Mb)          // torque through AddForce: two opposite forces (Vessel.h:1311-1315)
{
	double ml = Mb.length ();
	if (!(ml > 0)) return;
	Vector a = std::fabs (Mb.x) < 0.6*ml ? Vector (1, 0, 0) : Vector (0, 1, 0);
	Vector r1 = Xc (Mb, a); r1 /= r1.length ();
	Vector u = Xc (Mb, r1)*0.5;
	c.AddForce (u, r1); c.AddForce (-u, -r1);
}
Matrix InvM (const Matrix &M)
{
	double d = M(0,0)*(M(1,1)*M(2,2) - M(1,2)*M(2,1)) - M(0,1)*(M(1,0)*M(2,2) - M(1,2)*M(2,0)) + M(0,2)*(M(1,0)*M(2,1) - M(1,1)*M(2,0));
	if (!(std::fabs (d) > 1e-300) || !std::isfinite (d)) return Matrix ();
	return inv (M);
}
int Root (std::vector<int> &u, int i) { while (u[i] != i) { u[i] = u[u[i]]; i = u[i]; } return i; }

struct Wk {                                              // one body of this frame, working state at t0
	bool dyn = false;
	Vector X, V, W; Quaternion Q;                        // working state (W body frame)
	Vector aFree, alFree;                                // free accelerations (4.1)
	Vector x1, v1, w1; Quaternion q1;                    // predicted end
	CollOrbState o;                                      // Orbiter's state of the body now; configured by the delivery
	Vector Fprev, Mprev;
	bool posChanged = false, past = false, entry = false, jump = false, noRec = false;
	Vector xs, vs, ws; Quaternion qs;                    // physical start of the last step; free start after a rollback
	int det = -1, ver = -1;
	uint64_t key = 0;
};
struct APt {                                             // one contact point of the forward pass at t0
	int a, b, res, pt;
	Vector org, pa, pb, n;                               // points relative to org at t0
	double gap, tau;
	bool real;
	uint8_t flags;
	uint32_t triA, triB; uint16_t partA, partB;
	double t;                                            // event time (< 0: simt0)
};
struct Feat { bool ok = false; uint16_t pa, pb; uint32_t ta, tb; };
struct RapItem { CollAPairRec p; int a, b; double h; Feat f; double ut, tauT; Vector nt; };
struct Plan { Vector vNew, wNew, F, M, dxc, dthc; bool any = false; };

} // namespace

struct CollAddonFrame::Impl {
	CollAddonFrame &F;
	CollDetect &fwd, &ver;
	const CollOrbMirror &mir;
	std::vector<CollABody> &b;
	CollSolveHost &host;
	double h, simt0, hp = 0;
	std::vector<Wk> w;
	std::vector<int> fwdOf, verOf;                       // detector index -> body index
	CollScratch scr;

	Impl (CollAddonFrame &f, CollDetect &fd, CollDetect &vd, const CollOrbMirror &m, std::vector<CollABody> &bb, CollSolveHost &hs, double hh, double t0)
		: F (f), fwd (fd), ver (vd), mir (m), b (bb), host (hs), h (hh), simt0 (t0) {}

	Vector Ib (int i) const { return b[i].pmi*b[i].m; }
	Vector Pos (int i) const { return w[i].dyn ? w[i].X : b[i].kin.c0; }
	Quaternion Rot (int i) const { return w[i].dyn ? w[i].Q : b[i].kin.q0; }
	Vector SpinL (int i, const Quaternion &q, const Vector &wb) const { return mul (QM (q), Mulc (Ib (i), wb)); }
	Vector OmegaOf (int i, const Quaternion &q, const Vector &L) const { return Divc (tmul (QM (q), L), Ib (i)); }
	Vector PointVel (int i, const Vector &lever) const    // lever = p - Pos (i)
	{
		if (w[i].dyn) return w[i].V + Xc (mul (QM (w[i].Q), w[i].W), lever);
		return b[i].kin.v0 + Xc (b[i].kin.w0g, lever);
	}
	void Impulse (int i, const Vector &lever, const Vector &J)
	{
		if (i < 0 || !w[i].dyn) return;
		Wk &k = w[i];
		k.V += J/b[i].m;
		Vector L = SpinL (i, k.Q, k.W) + Xc (lever, J);
		k.W = OmegaOf (i, k.Q, L);
	}
	int Find (uint64_t key) const { for (size_t i = 0; i < w.size (); i++) if (w[i].key == key) return (int)i; return -1; }
	void Fail (const char *what, double a, double s)
	{
		F.st.checkFail++;
		CollLog (COLLLOG_ERROR, "Collision check failed: %s %.3e (scale %.3e)", what, a, s);
	}

	// ---- prediction (4.1)
	void Predict (int i)
	{
		Wk &k = w[i];
		k.x1 = k.X + k.V*h + k.aFree*(0.5*h*h);
		k.v1 = k.V + k.aFree*h;
		k.q1 = k.Q;
		CollRotate (k.q1, k.W*h + k.alFree*(0.5*h*h));
		k.w1 = k.W + k.alFree*h;
	}
	CollMotion FwdMotion (int i) const
	{
		const Wk &k = w[i];
		CollMotion m {};
		m.c0 = k.X; m.v0 = k.V; m.a0 = k.aFree; m.c1 = k.x1; m.v1 = k.v1; m.a1 = k.aFree;
		m.q0 = k.Q; m.q1 = k.q1; m.w0g = mul (QM (k.Q), k.W); m.w1g = mul (QM (k.q1), k.w1);
		m.h = h; m.ta = 0; m.tb = 1; m.a0ok = true;
		return m;
	}
	CollMotion PastKin (int i, double hh) const          // kinematic body over [t0 - hh, t0], extrapolated back
	{
		CollMotion m = b[i].kin;
		m.c1 = m.c0; m.v1 = m.v0; m.q1 = m.q0; m.w1g = m.w0g;
		m.c0 = m.c1 - m.v1*hh;
		Quaternion q (m.q1);
		CollRotate (q, tmul (QM (m.q1), m.w1g)*-hh);
		m.q0 = q; m.h = hh; m.ta = 0; m.tb = 1;
		return m;
	}
	CollBody DetBody (int i, const CollMotion &m, const std::vector<CollPartRef> &parts, bool rigidNow) const
	{
		CollBody d {};
		d.m = m; d.parts = parts; d.rmax = 0; d.id = b[i].id; d.kind = b[i].kind; d.entry = b[i].entry; d.jump1 = b[i].jump1; d.planet = b[i].planet;
		if (rigidNow) for (CollPartRef &p : d.parts) p.P1 = p.P0;
		return d;
	}

	// ---- t0 pair geometry
	void Hits (int i, int j, double extra, std::vector<CollHit> &out, std::vector<std::pair<int,int>> &parts, double &skinMax)
	{
		out.clear (); parts.clear (); skinMax = 0;
		const Vector O = Pos (i);
		CollAffine BA { QM (Rot (i)), Pos (i) - O }, BB { QM (Rot (j)), Pos (j) - O };
		for (size_t pa = 0; pa < b[i].parts.size (); pa++)
			for (size_t pb = 0; pb < b[j].parts.size (); pb++) {
				const CollPartRef &A = b[i].parts[pa], &B = b[j].parts[pb];
				if (!A.geom || !B.geom) continue;
				double sk = A.skin + B.skin;
				skinMax = std::max (skinMax, sk);
				std::vector<CollHit> o;
				CollPairsWithin (*A.geom, CollCompose (BA, A.P0), *B.geom, CollCompose (BB, B.P0), sk + extra, o, 256, scr, A.mask, B.mask);
				for (CollHit &x : o) { x.d -= sk; out.push_back (x); parts.push_back ({ (int)pa, (int)pb }); }
			}
	}
	double MinGap (int i, int j, Vector *p = nullptr)    // smallest skin gap at t0 within delta_ct + 0.1 m, else 1e9
	{
		std::vector<CollHit> hs; std::vector<std::pair<int,int>> pp; double sk;
		Hits (i, j, F.dprm.deltaCt + 0.1, hs, pp, sk);
		double m = 1e9;
		for (const CollHit &x : hs) if (x.d < m) { m = x.d; if (p) *p = Pos (i) + (x.pa + x.pb)*0.5; }
		return m;
	}

	void Run (std::vector<CollAWrite> &out, std::vector<CollImpactEvent> &ev, const std::vector<CollZone> &zones);
	void Snapshot ();
	void Decide (std::vector<RapItem> &rap);
	void Reconcile ();
	void PastCheck (std::vector<CollImpactEvent> &ev);
	bool KinTouch (const CollAIslandRec &I, const CollAPairRec &pr, int ia, int ib, RapItem &it);
	void Reapply (const std::vector<RapItem> &rap, const std::vector<APt> &pts);
	void Convert (const std::vector<CollPairResult> &res, std::vector<APt> &pts);
	CollAWrite Apply (int i, const Plan &pl, bool count, Vector *tgtPOut = nullptr, CollOrbState *cfOut = nullptr);
};

// 1: snapshot, free accelerations, JUMP test and deviation from P (3.1, 3.2, 4.1), terrain re-run (2.5)
void CollAddonFrame::Impl::Snapshot ()
{
	const int nb = (int)b.size ();
	w.assign (nb, Wk ());
	for (int i = 0; i < nb; i++) {
		CollABody &B = b[i];
		Wk &k = w[i];
		k.key = Key (B.kind, B.id);
		k.dyn = B.kind == COLLB_DYNAMIC && B.m > 0.0;
		B.dev = 0; B.woke = B.loaded = B.jump = false;
		if (!k.dyn) continue;
		k.X = B.x; k.V = B.v; k.W = B.wb; k.Q = B.q;
		CollAMem &m = F.mem[k.key];
		bool fresh = !m.seen || m.memberHash != B.memberHash;
		if (fresh) { m = CollAMem (); m.memberHash = B.memberHash; }
		m.seen = true;
		k.Fprev = m.Fprev; k.Mprev = m.Mprev;
		Matrix R = QM (B.q);
		if (B.stack && m.hasP && m.hPrev > 0) k.aFree = (B.v - m.vw)/m.hPrev - mul (R, k.Fprev/B.m);
		else k.aFree = B.aTot - mul (R, k.Fprev/B.m);
		k.alFree = B.arot - Divc (k.Mprev/B.m, B.pmi);
		CollOrbState &o = k.o;
		o.s.pos = B.x; o.s.vel = B.v; o.s.Q = B.q; o.s.R = R; o.s.omega = B.wb;
		o.acc = B.aTot; o.arot = B.arot; o.m = B.m; o.pmi = B.pmi;
		o.Fadd = o.Madd = Vector ();
		o.aC = k.aFree;
		Vector tauTot (B.arot.x*B.pmi.x + (B.pmi.y - B.pmi.z)*B.wb.y*B.wb.z, B.arot.y*B.pmi.y + (B.pmi.z - B.pmi.x)*B.wb.z*B.wb.x, B.arot.z*B.pmi.z + (B.pmi.x - B.pmi.y)*B.wb.x*B.wb.y);
		o.tauU = tauTot - k.Mprev/B.m;
		o.gReset = B.gEst; o.ground = B.ground; o.stack = B.stack;
		k.entry = (B.entry & (COLLE_NEW | COLLE_MEMBERS | COLLE_JUMP | COLLE_ACTIVATED)) != 0 || fresh;
		if (m.hasP && !k.entry) {
			if (B.groundNew) {                           // terrain re-run: P at the last level with PropSubMax substeps (2.5)
				m.P = m.Pw; mir.Step (m.P, m.hPrev, mir.nLevel - 1, mir.subMax);
				for (CollAIslandRec &I : F.isl) for (CollABodyRec &r : I.b) if (r.key == k.key) {
					CollOrbState a = r.aw, f = r.fw;
					mir.Step (a, m.hPrev, mir.nLevel - 1, mir.subMax); mir.Step (f, m.hPrev, mir.nLevel - 1, mir.subMax);
					r.dx = a.s.pos - f.s.pos; r.dv = a.s.vel - f.s.vel; r.dLs = a.SpinL () - f.SpinL (); r.dth = TurnOf (f.s.Q, a.s.Q);
					F.st.retries++;
					CollLog (COLLLOG_INFO, "Collision rerun: body %u records recomputed at the last level", B.id);
				}
			}
			B.dev = (B.x - m.P.s.pos).length () + B.rmax*QAngle (B.q, m.P.s.Q);
			double r = (B.x - m.xs - (m.vs + B.v)*(0.5*m.hPrev)).length ();
			double ra = QAngle (m.qs, B.q) - (m.ws + B.wb).length ()*0.5*m.hPrev;
			if (r > 1e-3 + 0.5*m.hPrev*m.hPrev*COLLA_A_JUMP || ra > 1e-6 + 0.5*m.hPrev*m.hPrev*10.0) { k.jump = true; F.st.jumps++; }
			k.xs = m.xs; k.vs = m.vs; k.ws = m.ws; k.qs = m.qs;
		} else { k.xs = B.x; k.vs = B.v; k.ws = B.wb; k.qs = B.q; }
		if (B.entry & COLLE_JUMP) k.jump = true;
		B.jump = k.jump;
		if (k.jump) k.entry = true;
	}
}

// 2.2-2.5: one decision per last-frame island; TOUCH undo with the W7 tests, else FREE rollback
void CollAddonFrame::Impl::Decide (std::vector<RapItem> &rap)
{
	enum { P_TOUCH, P_FREE };
	std::vector<int> path (F.isl.size (), P_FREE), jskip (F.isl.size (), -1);
	for (size_t k = 0; k < F.isl.size (); k++) {
		CollAIslandRec &I = F.isl[k];
		bool touch = true;
		for (CollABodyRec &r : I.b) {
			int i = Find (r.key);
			if (i < 0) { touch = false; continue; }
			if (!w[i].dyn || w[i].jump || r.memberHash != b[i].memberHash) {   // JUMP, LANDED, PLAYBACK, members (2.5)
				touch = false;
				bool far = !w[i].dyn || r.memberHash != b[i].memberHash || (b[i].x - F.mem[w[i].key].P.s.pos).length () > COLLA_D_FAR;
				if (far) jskip[k] = i;
				CollLog (COLLLOG_INFO, "Collision rollback: body %u JUMP (%s)", b[i].id, far ? "far" : "near");
			}
		}
		for (CollAPairRec &p : I.p) {
			int ia = Find (p.ka), ib = Find (p.kb);
			if (ia < 0 || ib < 0) { touch = false; continue; }
			double g = MinGap (ia, ib);
			if (g > F.dprm.deltaCt || g < -COLLA_DEV_TOL) { touch = false; CollLog (COLLLOG_FINE, "Collision rollback: no touch (gap %.4f)", g); }
			CollAMem *ma = w[ia].dyn ? &F.mem[w[ia].key] : nullptr, *mb = w[ib].dyn ? &F.mem[w[ib].key] : nullptr;
			Vector da = ma && ma->hasP ? b[ia].x - ma->P.s.pos : Vector (), db = mb && mb->hasP ? b[ib].x - mb->P.s.pos : Vector ();
			double dev = (da - db).length () + (ma && ma->hasP ? b[ia].rmax*QAngle (b[ia].q, ma->P.s.Q) : 0.0) + (mb && mb->hasP ? b[ib].rmax*QAngle (b[ib].q, mb->P.s.Q) : 0.0);
			if (dev > COLLA_DEV_TOL) { touch = false; CollLog (COLLLOG_INFO, "Collision rollback: deviation %.4f m", dev); }
		}
		path[k] = touch ? P_TOUCH : P_FREE;
		if (!touch) for (CollABodyRec &r : I.b) { int i = Find (r.key); if (i >= 0) w[i].noRec = true; }
	}
	for (Wk &k : w) if (k.dyn && k.entry) k.noRec = true;
	Reconcile ();                                        // 7.5 on the snapshot state, before any undo
	F.sup.clear ();
	for (size_t k = 0; k < F.isl.size (); k++) {
		CollAIslandRec &I = F.isl[k];
		if (path[k] == P_TOUCH) {
			std::vector<Wk> save = w;
			for (CollAContactRec &c : I.c) {                 // undo at the shared point: P and L exact
				int ia = Find (c.ka), ib = Find (c.kb);
				if (ia < 0) continue;
				Vector pA = Pos (ia) + mul (QM (Rot (ia)), c.ra), pB = ib >= 0 ? Pos (ib) + mul (QM (Rot (ib)), c.rb) : pA, s = (pA + pB)*0.5;
				Impulse (ia, s - Pos (ia), -c.J);
				if (ib >= 0) Impulse (ib, s - Pos (ib), c.J);
			}
			bool ok = true;
			for (CollABodyRec &r : I.b) {                    // turn test (W7)
				int i = Find (r.key);
				if (i < 0 || !w[i].dyn) continue;
				double turn = std::max (std::max (r.ws.length (), w[i].W.length ())*I.h, r.dth.length ());
				if (turn > COLLA_TURN) { ok = false; F.st.turnFree++; CollLog (COLLLOG_INFO, "Collision rollback: turn %.3f rad", turn); break; }
			}
			std::vector<RapItem> mine;
			for (size_t q = 0; ok && q < I.p.size (); q++) {
				RapItem it {};
				it.p = I.p[q]; it.h = I.h; it.a = Find (I.p[q].ka); it.b = Find (I.p[q].kb);
				if (!KinTouch (I, I.p[q], it.a, it.b, it)) { ok = false; F.st.featFree++; CollLog (COLLLOG_INFO, "Collision rollback: feature"); break; }
				mine.push_back (it);
			}
			if (ok) {
				for (RapItem &it : mine) rap.push_back (it);
				F.st.touchPath++;
				continue;
			}
			w = save;
		}
		for (CollABodyRec &r : I.b) {                        // FREE path: rollback by the mirror-measured effect (2.4)
			int i = Find (r.key);
			if (i < 0 || i == jskip[k] || !w[i].dyn) continue;
			if (!r.zero) {
				Wk &K = w[i];
				Vector Ls = SpinL (i, K.Q, K.W) - r.dLs;
				K.X -= r.dx; K.V -= r.dv; CollRotate (K.Q, -r.dth); K.W = OmegaOf (i, K.Q, Ls);
				K.posChanged = true;
			}
			w[i].xs = r.xs; w[i].vs = r.vs; w[i].ws = r.ws; w[i].qs = r.qs;
			if (!w[i].entry) w[i].past = true;
		}
		F.st.freePath++;
	}
	F.isl.clear ();
}

// 2.3: touch on the free path of the last step by rigid-body kinematics; feature test against the t0 gaps
bool CollAddonFrame::Impl::KinTouch (const CollAIslandRec &I, const CollAPairRec &pr, int ia, int ib, RapItem &it)
{
	const double hh = I.h;
	if (ia < 0 || ib < 0 || !(hh > 0)) return false;
	F.kt.Reset ();
	F.kt.Begin (F.dprm, hh);
	int idx[2] = { ia, ib }, dk[2] = { -1, -1 };
	for (int pass = 0; pass < 2; pass++)
		for (int s = 0; s < 2; s++) {
			int i = idx[s];
			if ((pass == 0) == w[i].dyn) continue;       // kinematic first
			CollMotion m;
			if (!w[i].dyn) m = PastKin (i, hh);
			else {
				const CollABodyRec *r = nullptr;
				for (const CollABodyRec &x : I.b) if (x.key == w[i].key) r = &x;
				if (!r) return false;
				Vector am = (w[i].V - r->vs)/hh, al = (w[i].W - r->ws)/hh;
				m = CollMotion {};
				m.c0 = r->xs; m.v0 = r->vs; m.a0 = am; m.c1 = r->xs + r->vs*hh + am*(0.5*hh*hh); m.v1 = w[i].V; m.a1 = am;
				m.q0 = r->qs; m.q1 = r->qs; CollRotate (m.q1, r->ws*hh + al*(0.5*hh*hh));
				m.w0g = mul (QM (m.q0), r->ws); m.w1g = mul (QM (m.q1), w[i].W);
				m.h = hh; m.ta = 0; m.tb = 1; m.a0ok = true;
			}
			CollBody d = DetBody (i, m, b[i].parts, true);
			d.entry = 0; d.jump1 = false;
			dk[s] = F.kt.AddBody (d);
		}
	std::vector<CollPairResult> res;
	CollFrameStats fs {};
	F.kt.Detect (res, fs);
	const CollPairResult *best = nullptr;
	for (const CollPairResult &r : res) if (r.kind != COLL_NONE && r.npt > 0 && (!best || r.tau < best->tau)) best = &r;
	std::vector<CollHit> hs; std::vector<std::pair<int,int>> pp; double sk;
	Hits (ia, ib, F.dprm.deltaCt + 0.1, hs, pp, sk);
	double gmin = 1e9;
	for (const CollHit &x : hs) gmin = std::min (gmin, x.d);
	if (!best) {                                         // no touch within the step: the free path decelerates before touching (du 0)
		int kmin = -1;
		for (size_t k = 0; k < hs.size (); k++) if (kmin < 0 || hs[k].d < hs[kmin].d) kmin = (int)k;
		if (kmin < 0) return false;
		it.f.ok = true; it.f.pa = (uint16_t)pp[kmin].first; it.f.pb = (uint16_t)pp[kmin].second; it.f.ta = hs[kmin].ta; it.f.tb = hs[kmin].tb;
		it.nt = pr.n; it.tauT = 1.0;
		Vector p = Pos (ia) + (hs[kmin].pa + hs[kmin].pb)*0.5;
		it.ut = std::max (0.0, -dotp (PointVel (ia, p - Pos (ia)) - PointVel (ib, p - Pos (ib)), it.nt));
		return true;
	}
	int ip = 0;
	for (int i = 1; i < best->npt; i++) if (best->pt[i].gap < best->pt[ip].gap) ip = i;
	const CollContact &c = best->pt[ip];
	bool swap = best->bodyA != dk[0];
	it.f.ok = true;
	it.f.pa = swap ? c.partB : c.partA; it.f.pb = swap ? c.partA : c.partB;
	it.f.ta = swap ? c.triB : c.triA; it.f.tb = swap ? c.triA : c.triB;
	Vector mid = (c.pA + c.pB)*0.5;
	Vector va = best->a.v + Xc (best->a.w, mid - best->a.c) + c.vsA, vb = best->b.v + Xc (best->b.w, mid - best->b.c) + c.vsB;
	Vector n = swap ? -c.n : c.n;
	it.nt = n; it.ut = -dotp (swap ? vb - va : va - vb, n); it.tauT = best->tau;
	for (size_t k = 0; k < hs.size (); k++)               // feature test: that feature's t0 gap within slop of the smallest
		if (pp[k].first == it.f.pa && pp[k].second == it.f.pb && hs[k].ta == it.f.ta && hs[k].tb == it.f.tb) return hs[k].d <= gmin + F.prm.slop;
	return false;
}

// 7.5: last frame's resting rows against the mirror prediction; unseen part taken out; position share
void CollAddonFrame::Impl::Reconcile ()
{
	const int nb = (int)w.size ();
	struct Row { int a, b; Vector p, n; double uP, lo, lam, kn; };
	std::vector<Row> rows;
	double maxd = 0;
	auto app = [&] (int a, int bb, const Vector &p, const Vector &n) { return -dotp (PointVel (a, p - Pos (a)) - PointVel (bb, p - Pos (bb)), n); };
	auto pvelP = [&] (int i, const Vector &r, Vector &v) {
		if (!w[i].dyn) { Vector p = Pos (i) + mul (QM (Rot (i)), r); v = PointVel (i, p - Pos (i)); return; }
		const CollOrbState &P = F.mem[w[i].key].P;
		Matrix R = QM (P.s.Q);
		v = P.s.vel + Xc (mul (R, P.s.omega), mul (R, r));
	};
	auto rowK = [&] (int a, int bb, const Vector &p, const Vector &n) {
		std::vector<Wk> s = w;
		double u0 = app (a, bb, p, n);
		Impulse (a, p - Pos (a), n); Impulse (bb, p - Pos (bb), -n);
		double du = app (a, bb, p, n) - u0;
		w.swap (s);
		return du < 0 ? -1.0/du : 0.0;
	};
	for (const CollASupRow &s : F.sup) {
		int a = Find (s.ka), bb = Find (s.kb);
		if (a < 0 || bb < 0 || !w[a].dyn || w[a].noRec || (w[bb].dyn && w[bb].noRec)) continue;
		if (!F.mem[w[a].key].hasP || (w[bb].dyn && !F.mem[w[bb].key].hasP)) continue;
		Vector pA = Pos (a) + mul (QM (Rot (a)), s.ra), pB = Pos (bb) + mul (QM (Rot (bb)), s.rb);
		Vector n = mul (QM (Rot (bb)), s.nb);
		Vector vPA, vPB;
		pvelP (a, s.ra, vPA); pvelP (bb, s.rb, vPB);
		Vector nP = w[bb].dyn ? mul (QM (F.mem[w[bb].key].P.s.Q), s.nb) : n;
		double uP = -dotp (vPA - vPB, nP);
		if (uP < -1e-3) continue;                        // planned to separate
		double u = -dotp (PointVel (a, pA - Pos (a)) - PointVel (bb, pB - Pos (bb)), n);
		Row r { a, bb, (pA + pB)*0.5, n, 0, -s.J2n, 0, 0 };
		r.uP = app (a, bb, r.p, r.n) - (u - uP);
		r.kn = rowK (a, bb, r.p, r.n);
		maxd = std::max (maxd, std::fabs (u - uP));
		if (r.kn > 0) rows.push_back (r);
	}
	if (rows.empty () || maxd < 1e-9) return;
	std::vector<Vector> V0 (nb), W0 (nb);
	std::vector<char> in (nb, 0);
	for (const Row &r : rows) { in[r.a] = 1; in[r.b] = 1; }
	for (int i = 0; i < nb; i++) { V0[i] = w[i].V; W0[i] = w[i].W; }
	for (int sw = 0; sw < 400; sw++) {
		double md = 0;
		for (Row &r : rows) {
			double u = app (r.a, r.b, r.p, r.n);
			double l = std::max (r.lo, r.lam + (u - r.uP)*r.kn);
			double dl = l - r.lam;
			r.lam = l;
			if (dl != 0.0) { Impulse (r.a, r.p - Pos (r.a), r.n*dl); Impulse (r.b, r.p - Pos (r.b), r.n*-dl); }
			md = std::max (md, std::fabs (dl));
		}
		if (md < 1e-13) break;
	}
	F.st.rec++;
	CollLog (COLLLOG_FINE, "Collision rec: %d rows, largest du %.3e m/s", (int)rows.size (), maxd);
	for (int i = 0; i < nb; i++) {
		if (!in[i] || !w[i].dyn) continue;
		Wk &X = w[i];
		const CollOrbState &P = F.mem[X.key].P;
		F.mem[X.key].tRec = simt0;
		Vector dV = X.V - V0[i], dW = X.W - W0[i], dx, dth;
		if (dV.length () > 0) {
			Vector e = dV/dV.length ();
			double dvn = dotp (V0[i] - P.s.vel, e);
			if (dvn < 0) dx = e*(-std::min (1.0, dV.length ()/-dvn)*dotp (X.X - P.s.pos, e));
		}
		if (dW.length () > 0) {
			Vector e = dW/dW.length ();
			double dwn = dotp (W0[i] - P.s.omega, e);
			if (dwn < 0) dth = e*(-std::min (1.0, dW.length ()/-dwn)*dotp (TurnOf (P.s.Q, X.Q), e));
		}
		if (dx.length () + b[i].rmax*dth.length () > COLLA_E_COMP || dV.length () > COLLA_V_WRITE) {
			X.X += dx;
			if (dth.length () > 0) CollRotate (X.Q, dth);
			if (dx.length () + dth.length () > 0) { X.posChanged = true; F.st.recPos++; }
		}
	}
}

// 3.3: the core's frame solver on [t_prev, t0] in the verifier, seeded from the forward store
void CollAddonFrame::Impl::PastCheck (std::vector<CollImpactEvent> &ev)
{
	const int nb = (int)w.size ();
	bool any = false;
	for (const Wk &k : w) any = any || (k.dyn && k.past && !k.entry);
	if (!any || !(hp > 0)) return;
	ver.Begin (F.dprm, hp);
	verOf.clear ();
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < nb; i++) {
			if ((pass == 0) == w[i].dyn) continue;
			CollMotion m {};
			if (!w[i].dyn) m = PastKin (i, hp);
			else {
				const Wk &k = w[i];
				m.c0 = k.xs; m.v0 = k.vs; m.a0 = k.aFree; m.c1 = k.X; m.v1 = k.V; m.a1 = k.aFree;
				m.q0 = k.qs; m.q1 = k.Q; m.w0g = mul (QM (k.qs), k.ws); m.w1g = mul (QM (k.Q), k.W);
				m.h = hp; m.ta = 0; m.tb = 1; m.a0ok = true;
			}
			CollBody d = DetBody (i, m, b[i].pastParts.empty () ? b[i].parts : b[i].pastParts, b[i].pastParts.empty ());
			d.entry = 0; d.jump1 = false;
			w[i].ver = ver.AddBody (d);
			verOf.push_back (i);
		}
	for (size_t e = 0; e < fwd.Pairs ().Size (); e++) { const CollPairEntry &x = fwd.Pairs ().At (e); ver.Pairs ().Get (x.a, x.b) = x; }
	std::vector<CollPairResult> res, keep;
	CollFrameStats fs {};
	ver.Detect (res, fs);
	for (const CollPairResult &r : res) {
		if ((r.kind != COLL_TOI && r.kind != COLL_SPECULATIVE) || !(r.tau > 0.0) || (r.flags & COLLF_CORE)) continue;
		int a = verOf[r.bodyA], c = verOf[r.bodyB];
		if (w[a].entry || w[c].entry) continue;
		if (!(w[a].dyn && w[a].past) && !(w[c].dyn && w[c].past)) continue;
		keep.push_back (r);
	}
	if (!keep.empty ()) {
		std::vector<CollFrameBody> fb (ver.nBody ());
		for (int d = 0; d < ver.nBody (); d++) {
			int i = verOf[d];
			CollFrameBody &f = fb[d];
			f.dyn = w[i].dyn; f.wakeable = false; f.id = b[i].id; f.m = b[i].m; f.pmi = b[i].pmi;
			if (w[i].dyn) { f.x1 = w[i].X; f.v1 = w[i].V; f.wb1 = w[i].W; f.q1 = w[i].Q; }
			else { f.x1 = b[i].kin.c0; f.v1 = b[i].kin.v0; f.q1 = b[i].kin.q0; f.wb1 = tmul (QM (b[i].kin.q0), b[i].kin.w0g); }
		}
		CollFrameSolver fs2;
		fs2.check = F.check;
		std::vector<CollBodyDelta> delta;
		std::vector<CollImpactEvent> pev;
		fs2.Run (ver, keep, fb, hp, simt0 - hp, F.prm, F.rounds, host, delta, pev);
		for (const CollBodyDelta &d : delta) {
			int i = verOf[d.body];
			if (!w[i].dyn) continue;
			CollApplyDeltaState (w[i].X, w[i].V, w[i].Q, w[i].W, Ib (i), d.d);
			w[i].posChanged = true;
		}
		for (CollImpactEvent &e : pev) if (e.flags & COLLEV_FIRST) { e.dKE = std::max (0.0, e.dKE); e.vn = std::max (0.0, e.vn); ev.push_back (e); }
		for (const CollPairResult &r : keep) {               // forward touch states of the solved owner pairs
			for (int k = 0; k < r.npt; k++) {
				CollOwnerKey oa = ver.Owner (r, k, 0), ob = ver.Owner (r, k, 1);
				const CollPairEntry *e = ver.Pairs ().Find (oa, ob);
				if (e) fwd.Pairs ().Get (oa, ob).touch = e->touch;
			}
			F.st.past++; F.st.missed++;
			CollLog (COLLLOG_WARN, "Collision missed: t=%.17g '%u' '%u' dev=%.4f", simt0 - hp + r.tau*hp, b[verOf[r.bodyA]].id, b[verOf[r.bodyB]].id,
				std::max (b[verOf[r.bodyA]].dev, b[verOf[r.bodyB]].dev));
		}
	}
	ver.Pairs ().Clear ();
}

// 2.3: one re-apply solve over all re-applied pairs and their reachable real neighbours
void CollAddonFrame::Impl::Reapply (const std::vector<RapItem> &rap, const std::vector<APt> &pts)
{
	const int nb = (int)w.size ();
	auto app = [&] (int a, int bb, const Vector &p, const Vector &n) { return -dotp (PointVel (a, p - Pos (a)) - PointVel (bb, p - Pos (bb)), n); };
	auto rowK = [&] (int a, int bb, const Vector &p, const Vector &n) {
		std::vector<Wk> s = w;
		double u0 = app (a, bb, p, n);
		Impulse (a, p - Pos (a), n); Impulse (bb, p - Pos (bb), -n);
		double du = app (a, bb, p, n) - u0;
		w.swap (s);
		return du < 0 ? -1.0/du : 0.0;
	};
	std::vector<double> du (rap.size ());
	std::vector<Vector> fp (rap.size ());
	std::vector<char> hasFp (rap.size (), 0);
	for (size_t k = 0; k < rap.size (); k++) {
		const RapItem &it = rap[k];
		std::vector<CollHit> hs; std::vector<std::pair<int,int>> pp; double sk;
		Hits (it.a, it.b, F.dprm.deltaCt + 0.1, hs, pp, sk);
		Vector p = Pos (it.a) + mul (QM (Rot (it.a)), it.p.ra);
		for (size_t q = 0; q < hs.size (); q++)
			if (pp[q].first == it.f.pa && pp[q].second == it.f.pb && hs[q].ta == it.f.ta && hs[q].tb == it.f.tb) { p = Pos (it.a) + (hs[q].pa + hs[q].pb)*0.5; hasFp[k] = 1; }
		fp[k] = p;
		double u1 = app (it.a, it.b, p, it.nt);
		du[k] = u1 - std::max (0.0, it.ut);
	}
	std::vector<char> reach (nb, 0);
	for (const RapItem &it : rap) { if (w[it.a].dyn) reach[it.a] = 1; if (w[it.b].dyn) reach[it.b] = 1; }
	for (bool grow = true; grow; ) {
		grow = false;
		for (const APt &q : pts) if (q.real && w[q.a].dyn && w[q.b].dyn && reach[q.a] != reach[q.b]) { reach[q.a] = reach[q.b] = 1; grow = true; }
	}
	struct Row { int a, b; Vector p, n; double tgt, lam, kn; int sign; };
	std::vector<Row> rows;
	for (const APt &q : pts) {
		if (!q.real || !(reach[q.a] || reach[q.b])) continue;
		Vector p = q.org + (q.pa + q.pb)*0.5;
		Row r { q.a, q.b, p, q.n, app (q.a, q.b, p, q.n), 0, rowK (q.a, q.b, p, q.n), 1 };
		for (size_t k = 0; k < rap.size (); k++)
			if ((rap[k].a == q.a && rap[k].b == q.b) || (rap[k].a == q.b && rap[k].b == q.a)) {
				r.tgt -= du[k]; r.sign = du[k] >= 0 ? 1 : -1;
			}
		if (r.kn > 0) rows.push_back (r);
	}
	for (size_t k = 0; k < rap.size (); k++) {
		bool has = false;
		for (const Row &r : rows) if ((r.a == rap[k].a && r.b == rap[k].b) || (r.a == rap[k].b && r.b == rap[k].a)) has = true;
		if (has) continue;
		const RapItem &it = rap[k];
		double kn = rowK (it.a, it.b, fp[k], it.nt);
		if (kn > 0) rows.push_back (Row { it.a, it.b, fp[k], it.nt, app (it.a, it.b, fp[k], it.nt) - du[k], 0, kn, du[k] >= 0 ? 1 : -1 });
	}
	if (rows.empty ()) return;
	Vector sP; double sJ = 0;
	for (int sw = 0; sw < 400; sw++) {
		double md = 0;
		for (Row &r : rows) {
			double u = app (r.a, r.b, r.p, r.n);
			double l = r.lam + (u - r.tgt)*r.kn;
			l = r.sign > 0 ? std::max (0.0, l) : std::min (0.0, l);
			double dl = l - r.lam;
			r.lam = l;
			if (dl != 0.0) { Impulse (r.a, r.p - Pos (r.a), r.n*dl); Impulse (r.b, r.p - Pos (r.b), r.n*-dl); sJ += std::fabs (dl); }
			md = std::max (md, std::fabs (dl));
		}
		if (md < 1e-13) break;
	}
	F.st.reapply += (int)rap.size ();
}

// 5.3 step 8: tau = 0 results are real, later ones speculative at tau = 0 (points carried back on each body's motion)
void CollAddonFrame::Impl::Convert (const std::vector<CollPairResult> &res, std::vector<APt> &pts)
{
	pts.clear ();
	for (size_t k = 0; k < res.size (); k++) {
		const CollPairResult &r = res[k];
		if (r.kind == COLL_NONE || r.npt <= 0) continue;
		int a = fwdOf[r.bodyA], c = fwdOf[r.bodyB];
		if (!w[a].dyn && !w[c].dyn) continue;
		bool real = r.kind == COLL_RESTING || (r.kind == COLL_TOI && r.tau == 0.0);
		bool entry = (b[a].entry | b[c].entry) != 0 || w[a].entry || w[c].entry;
		Matrix RA0 = QM (Rot (a)), RB0 = QM (Rot (c)), RAt = QM (r.a.q), RBt = QM (r.b.q);
		for (int i = 0; i < r.npt; i++) {
			const CollContact &pt = r.pt[i];
			APt q {};
			q.a = a; q.b = c; q.res = (int)k; q.pt = i; q.org = r.origin; q.n = pt.n; q.real = real; q.tau = r.tau; q.t = -1;
			q.triA = pt.triA; q.triB = pt.triB; q.partA = pt.partA; q.partB = pt.partB;
			q.flags = pt.flags;
			if (real) {
				q.pa = pt.pA; q.pb = pt.pB; q.gap = pt.gap;
				if (entry && r.kind == COLL_RESTING) q.flags &= (uint8_t)~COLLP_FIRST; // entry frame rule (1.5)
			} else {
				q.pa = (Pos (a) - r.origin) + mul (RA0, tmul (RAt, pt.pA - r.a.c));
				q.pb = (Pos (c) - r.origin) + mul (RB0, tmul (RBt, pt.pB - r.b.c));
				double gt = r.kind == COLL_SPECULATIVE ? r.specGap : pt.gap;
				q.gap = std::max (0.0, gt + dotp ((q.pa - q.pb) - (pt.pA - pt.pB), pt.n));
				q.flags = 0;
			}
			pts.push_back (q);
		}
	}
}

// 6.2: the plan delivered on the mirror for this body's integrator; the written state absorbs or the force channel carries
CollAWrite CollAddonFrame::Impl::Apply (int i, const Plan &in, bool count, Vector *tgtPOut, CollOrbState *cfOut)
{
	Wk &B = w[i];
	CollOrbState &o = B.o;
	const double m = o.m, rmax = b[i].rmax;
	CollAWrite wr {};
	wr.body = i;
	Vector Fv = in.F, M = in.M;
	CollOrbState cf = o;                                 // Orbiter's own step without the addon: cache without the own force
	cf.acc -= mul (o.s.R, B.Fprev/m);
	cf.arot -= Divc (B.Mprev/m, o.pmi);
	cf.Fadd = cf.Madd = Vector ();
	Vector Lnew = SpinL (i, B.Q, in.wNew);
	Vector tgtP = (in.vNew - o.s.vel)*m + Fv*h;
	Vector tgtL = (Lnew - o.SpinL ()) + M*h;
	bool posw = B.posChanged || in.dxc.length () > 0 || in.dthc.length () > 0;
	bool write = posw || (in.vNew - o.s.vel).length () > COLLA_V_WRITE;
	if (b[i].ground && !posw && (in.vNew - o.s.vel).length () <= COLL_V_WAKE) write = false; // no DefSetStateEx on ground contact (6.6)
	Vector Fe = write ? Fv : Fv + (in.vNew - o.s.vel)*(m/h);
	Quaternion qw (B.Q);
	if (in.dthc.length () > 0) CollRotate (qw, in.dthc);
	Vector wW = OmegaOf (i, qw, Lnew);
	int lv = 0, n = 1, meth = COLLM_RK2;
	double k = h, g0 = 0, cx = 0.5;
	Quaternion qa (qw);
	Vector wa = wW, dAl;
	Matrix Ml, Ma;
	auto probe = [&] (const Quaternion &q, const Vector &v, const Vector &wv) {
		CollOrbState base = o;
		base.s.pos = B.X; base.s.vel = v; base.s.Q = q; base.s.R = QM (q); base.s.omega = wv;
		base.acc = base.aC; base.Fadd = base.Madd = Vector ();
		base.arot = base.arot - Divc (B.Mprev/m, o.pmi);
		CollOrbState o0 = base;
		mir.Step (o0, h);
		Vector L0 = o0.SpinL ();
		for (int c = 0; c < 3; c++) {
			Vector e; e.data[c] = 1.0;
			CollOrbState o1 = base; o1.acc += mul (QM (q), e/m); o1.Fadd = e; mir.Step (o1, h);
			Vector dv = (o1.s.vel - o0.s.vel)*m;
			Ml(0,c) = dv.x; Ml(1,c) = dv.y; Ml(2,c) = dv.z;
			CollOrbState o2 = base; o2.arot += Divc (e/m, o2.pmi); o2.Madd = e; mir.Step (o2, h);
			Vector dL = o2.SpinL () - L0;
			Ma(0,c) = dL.x; Ma(1,c) = dL.y; Ma(2,c) = dL.z;
		}
	};
	for (int it = 0; it < 3; it++) {
		mir.Choose (h, wa.length (), o.ground, lv, n);
		meth = mir.mode[lv]; k = h/n; g0 = CollOrbMirror::Gamma0 (meth); cx = CollOrbMirror::DxCoef (meth);
		probe (qa, write ? in.vNew : o.s.vel, wa);
		wr.Fb = Fe.length () > 0 ? mul (InvM (Ml), Fe*h) : Vector ();
		wr.Mb = M.length () > 0 ? mul (InvM (Ma), M*h) : Vector ();
		dAl = Divc ((wr.Mb - B.Mprev)/m, o.pmi);
		wr.cdth = dAl*(cx*k*k);
		if (wr.cdth.length ()*rmax <= COLLA_E_COMP) wr.cdth = Vector ();
		qa = qw;
		if (wr.cdth.length () > 0) CollRotate (qa, wr.cdth);
		wa = wW + dAl*(g0*k);
	}
	Vector aMissN = mul (o.s.R, (wr.Fb - B.Fprev)/m);
	if (!write && !b[i].ground && (cx + g0)*k*k*aMissN.length () > COLLA_E_COMP) {   // force-change write (6.2 step 5)
		write = true; Fe = Fv;
		if (count) F.st.forceWrites++;
		wr.Fb = Fv.length () > 0 ? mul (InvM (Ml), Fv*h) : Vector ();
	}
	Vector aMissW = o.stack ? mul (QM (qa), (wr.Fb - B.Fprev)/m) : (o.aC - o.gReset) + mul (QM (qa), wr.Fb/m);
	wr.cdx = write ? aMissW*(cx*k*k) : Vector ();
	Vector xW = B.X + in.dxc + wr.cdx;
	Vector vW = write ? in.vNew : o.s.vel;
	Vector vWr = write ? vW + aMissW*(g0*k) : vW;
	Vector wWr = wa;
	auto configure = [&] (bool wrt, const Vector &x, const Vector &v, const Quaternion &q, const Vector &wv, const Vector &Fb, const Vector &Mb) {
		CollOrbState c = o;
		if (wrt) c.DefSetStateEx (x, v, wv);
		c.SetRotationMatrix (QM (q)); c.SetAngularVel (wv);
		c.Fadd = c.Madd = Vector ();
		c.AddForce (Fb, Vector ()); Couple (c, Mb);
		return c;
	};
	Matrix Js = Ml;
	double eLast = 0, eFloor = 0;
	for (int it = 0; it < 8; it++) {
		CollOrbState c = configure (write, xW, vWr, qa, wWr, wr.Fb, wr.Mb); mir.Step (c, h);
		CollOrbState cs = cf; mir.Step (cs, h, c.lv, c.nsub);
		Vector eP = tgtP - (c.s.vel - cs.s.vel)*m;
		Vector eL = tgtL - (c.SpinL () - cs.SpinL ());
		eLast = eP.length ()/(tgtP.length () + m*1e-3);
		eFloor = 4096.0*DBL_EPSILON*m*c.s.vel.length ()/(tgtP.length () + m*1e-3); // rounding floor of heliocentric velocities (30 km/s)
		if (eP.length () <= 1e-14*(tgtP.length () + m*1e-3) && eL.length () <= 1e-14*(tgtL.length () + 1e-3)) break;
		if (write) vWr += eP/m;
		else {
			CollOrbState c0 = configure (false, xW, vWr, qa, wWr, wr.Fb, wr.Mb), c0s = c0; mir.Step (c0s, h);
			for (int cc = 0; cc < 3; cc++) {
				Vector e; e.data[cc] = 1.0;
				CollOrbState c1 = c0; c1.AddForce (e, Vector ()); mir.Step (c1, h);
				Vector dv = (c1.s.vel - c0s.s.vel)*m;
				Js(0,cc) = dv.x; Js(1,cc) = dv.y; Js(2,cc) = dv.z;
			}
			wr.Fb += mul (InvM (Js), eP);
		}
		Matrix Jw;
		Vector L0 = c.SpinL ();
		for (int k2 = 0; k2 < 3; k2++) {
			Vector e; e.data[k2] = 1e-6*(1.0 + wWr.length ());
			CollOrbState c2 = configure (write, xW, vWr, qa, wWr + e, wr.Fb, wr.Mb); mir.Step (c2, h, c.lv, c.nsub);
			Vector d = (c2.SpinL () - L0)/e.data[k2];
			Jw(0,k2) = d.x; Jw(1,k2) = d.y; Jw(2,k2) = d.z;
		}
		wWr += mul (InvM (Jw), eL);
		if (count) F.st.deliveryIt++;
	}
	if (F.check && eLast > std::max (1e-12, eFloor)) Fail ("delivery", eLast, 1.0);
	wr.cdv = vWr - vW; wr.cdw = wWr - wW;
	wr.state = write;
	wr.attitude = !QEq (qa, o.s.Q);
	wr.spin = (wWr - o.s.omega).length ()*rmax > COLLA_SPIN_TOL;
	wr.force = wr.Fb.length () > 0 || wr.Mb.length () > 0;
	wr.weight = write && !o.stack;
	wr.x = xW; wr.v = vWr; wr.wb = wr.spin ? wWr : o.s.omega; wr.q = qa;
	if (tgtPOut) *tgtPOut = tgtP;
	if (cfOut) *cfOut = cf;
	// the configured state is what Orbiter steps from; physical start without the compensation (3.1)
	o = configure (write, xW, vWr, qa, wr.wb, wr.Fb, wr.Mb);
	if (count) {
		if (write) F.st.writes++;
		if (wr.attitude) F.st.attWrites++;
		CollAMem &mm = F.mem[B.key];
		mm.xs = xW - wr.cdx; mm.vs = vW; mm.ws = wW; mm.qs = qw; mm.vw = vWr;
		mm.Fprev = wr.Fb; mm.Mprev = wr.Mb;
	}
	B.xs = xW - wr.cdx; B.vs = vW; B.ws = wW; B.qs = qw;
	return wr;
}

void CollAddonFrame::Impl::Run (std::vector<CollAWrite> &out, std::vector<CollImpactEvent> &ev, const std::vector<CollZone> &zones)
{
	const int nb = (int)b.size ();
	Snapshot ();
	hp = 0;
	for (const Wk &k : w) if (k.dyn) { const CollAMem &m = F.mem[k.key]; if (m.hasP) hp = std::max (hp, m.hPrev); }
	for (CollAIslandRec &I : F.isl) hp = std::max (hp, I.h);
	// 2-4: decisions, reconciliation, TOUCH and FREE paths
	std::vector<RapItem> rap;
	Decide (rap);
	for (int i = 0; i < nb; i++) if (w[i].dyn && !w[i].entry && b[i].dev > COLLA_DEV_TOL) w[i].past = true;
	// 5: past check
	PastCheck (ev);
	// 6: predict, forward detection (kinematic bodies first)
	for (int i = 0; i < nb; i++) if (w[i].dyn) Predict (i);
	fwd.Begin (F.dprm, h);
	fwdOf.clear ();
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < nb; i++) {
			if ((pass == 0) == w[i].dyn) continue;
			w[i].det = fwd.AddBody (DetBody (i, w[i].dyn ? FwdMotion (i) : b[i].kin, b[i].parts, false));
			fwdOf.push_back (i);
		}
	std::vector<CollZone> zf;
	for (CollZone z : zones) if (z.bodyA >= 0 && z.bodyB >= 0 && z.bodyA < nb && z.bodyB < nb) { z.bodyA = w[z.bodyA].det; z.bodyB = w[z.bodyB].det; zf.push_back (z); }
	fwd.SetZones (zf);
	std::vector<CollPairResult> res;
	CollFrameStats fst {};
	fwd.Detect (res, fst);
	std::vector<APt> pts;
	Convert (res, pts);
	auto resweep = [&] (int i, const Vector &v0, const Vector &w0g, const Vector &c1, const Vector &v1, const Quaternion &q1, const Vector &w1g) {
		const CollMotion &m = fwd.Body (w[i].det).m;
		CollRestart r { v0 - m.Vel (0.0), w0g - m.Omega (0.0), c1, v1, w1g, q1 };
		std::vector<CollPairResult> o2, keep;
		fwd.Resweep (w[i].det, 0.0, r, o2);
		for (const CollPairResult &x : res) if (x.bodyA != w[i].det && x.bodyB != w[i].det) keep.push_back (x);
		for (const CollPairResult &x : o2) keep.push_back (x);
		res.swap (keep);
	};
	// 7: re-apply (2.3), predict again, re-sweep the changed bodies
	if (!rap.empty ()) {
		std::vector<Vector> V0 (nb), W0 (nb);
		for (int i = 0; i < nb; i++) { V0[i] = w[i].V; W0[i] = w[i].W; }
		Reapply (rap, pts);
		for (int i = 0; i < nb; i++) {
			if (!w[i].dyn) continue;
			Predict (i);
			if ((w[i].V - V0[i]).length () > 1e-3 || (w[i].W - W0[i]).length () > 1e-4)
				resweep (i, w[i].V, mul (QM (w[i].Q), w[i].W), w[i].x1, w[i].v1, w[i].q1, mul (QM (w[i].q1), w[i].w1));
		}
		Convert (res, pts);
		for (APt &q : pts) for (const RapItem &it : rap)      // event time of a TOUCH path: the touch (5.6)
			if (q.real && ((q.a == it.a && q.b == it.b) || (q.a == it.b && q.b == it.a)) && it.tauT < 1.0) q.t = simt0 - it.h + it.tauT*it.h;
	}
	// 9: islands over dynamic bodies
	std::vector<int> u (nb);
	std::iota (u.begin (), u.end (), 0);
	for (const APt &q : pts) if (w[q.a].dyn && w[q.b].dyn) { int x = Root (u, q.a), y = Root (u, q.b); if (x != y) u[std::max (x, y)] = std::min (x, y); }
	std::vector<Plan> plan (nb);
	std::vector<char> inIsl (nb, 0);
	struct SpecB { Vector dP1, dL1, dP2, dL2; bool on = false; };
	std::vector<SpecB> spec (nb);
	std::vector<CollAIslandRec> newIsl;
	std::vector<CollPairResult> solvedReal;
	std::vector<CollEventRec> erec;
	std::map<int, int> realIdx;                                // forward result -> solvedReal index
	std::vector<char> solvedRes (res.size (), 0);
	bool anySpecFrame = false, anyRealFrame = false;
	std::vector<char> done (nb, 0);
	for (int root = 0; root < nb; root++) {
		if (!w[root].dyn || Root (u, root) != root) continue;
		std::vector<int> pidx;
		for (size_t k = 0; k < pts.size (); k++) {
			int d = w[pts[k].a].dyn ? pts[k].a : pts[k].b;
			if (Root (u, d) == root) pidx.push_back ((int)k);
		}
		if (pidx.empty ()) continue;
		std::vector<int> memb;                                // dynamic members, then kinematic partners
		for (int i = 0; i < nb; i++) if (w[i].dyn && Root (u, i) == root) memb.push_back (i);
		for (int k : pidx) for (int s : { pts[k].a, pts[k].b }) if (!w[s].dyn && std::find (memb.begin (), memb.end (), s) == memb.end ()) memb.push_back (s);
		bool anySpec = false, anyReal = false;
		const Vector O = w[memb[0]].X;
		auto idx = [&] (int i) { return (int)(std::find (memb.begin (), memb.end (), i) - memb.begin ()); };
		auto build = [&] (CollIsland &is, bool withSpec, std::vector<int> &map) {
			is = CollIsland (); map.clear ();
			is.tau = 0.0; is.h = h;
			for (int i : memb) {
				CollSBody sb {};
				if (w[i].dyn) {
					sb.dyn = true; sb.m = b[i].m; sb.pmi = b[i].pmi;
					sb.Rt = QM (w[i].Q); sb.R1 = QM (w[i].q1);
					sb.xt = w[i].X - O; sb.vt = w[i].V; sb.wt = mul (sb.Rt, w[i].W);
					sb.v1 = w[i].v1; sb.x1 = sb.xt + sb.v1*h; sb.wb1 = w[i].w1;
				} else {
					sb.dyn = false; sb.Rt = QM (b[i].kin.q0); sb.R1 = QM (b[i].kin.q1); sb.xt = b[i].kin.c0 - O; sb.x1 = sb.xt;
				}
				is.body.push_back (sb);
			}
			for (int k : pidx) {
				const APt &q = pts[k];
				if (!q.real && !withSpec) { map.push_back (-1); continue; }
				CollSContact c {};
				c.a = idx (q.a); c.b = idx (q.b);
				Vector mid = q.org + (q.pa + q.pb)*0.5;
				c.p = mid - O; c.n = q.n;
				Matrix RA = QM (w[q.a].dyn ? w[q.a].q1 : b[q.a].kin.q1)*transp (QM (Rot (q.a))), RBm = QM (w[q.b].dyn ? w[q.b].q1 : b[q.b].kin.q1)*transp (QM (Rot (q.b)));
				Vector n2 = mul (RA, q.n) + mul (RBm, q.n);
				c.n2 = n2.length () > 0 ? n2/n2.length () : q.n;
				c.gap = q.gap;
				c.kind = q.real ? COLL_RESTING : COLL_SPECULATIVE;
				c.flags = (uint8_t)(q.flags | (q.real ? COLLP_BALLISTIC : COLLP_SPECTRAP));
				CollSMat sa = host.Material (res[q.res], q.pt, 0), sbm = host.Material (res[q.res], q.pt, 1);
				c.e0 = std::max (sa.e0, sbm.e0); c.vy = std::min (sa.vy, sbm.vy); c.mu = q.real ? std::sqrt (std::max (0.0, sa.mu*sbm.mu)) : 0.0;
				for (int s = 0; s < 2; s++) {
					int bi = s ? q.b : q.a;
					Vector &vt = s ? c.vkb_t : c.vka_t, &v1 = s ? c.vkb_1 : c.vka_1;
					if (w[bi].dyn) continue;
					const CollMotion &km = b[bi].kin;
					Matrix Rk = QM (km.q1)*transp (QM (km.q0));
					vt = km.v0 + Xc (km.w0g, mid - km.c0);
					v1 = km.v1 + Xc (km.w1g, mul (Rk, mid - km.c0));
				}
				map.push_back ((int)is.con.size ());
				is.con.push_back (c);
			}
		};
		for (int k : pidx) (pts[k].real ? anyReal : anySpec) = true;
		CollIsland all, ro;
		std::vector<int> mapAll, mapRo;
		build (all, true, mapAll);
		all.Solve (F.prm);
		// 10: rounds on the planned rigid motion (5.4)
		for (int pass = 0; anySpec && pass < 3; pass++) {
			bool redo = false;
			for (size_t j = 0; j < memb.size (); j++) {
				int i = memb[j];
				if (!w[i].dyn) continue;
				CollDelta d;
				all.Delta ((int)j, d);
				Vector x1 = w[i].x1, v1 = w[i].v1, w1 = w[i].w1;
				Quaternion q1 = w[i].q1;
				CollApplyDeltaState (x1, v1, q1, w1, Ib (i), d);
				Vector v0 = w[i].V + all.body[j].dP1/b[i].m;
				Vector w0g = mul (QM (w[i].Q), OmegaOf (i, w[i].Q, SpinL (i, w[i].Q, w[i].W) + all.body[j].dL1));
				const CollMotion &m = fwd.Body (w[i].det).m;
				CollRestart r { v0 - m.Vel (0.0), w0g - m.Omega (0.0), x1, v1, mul (QM (q1), w1), q1 };
				std::vector<CollPairResult> o2;
				fwd.Resweep (w[i].det, 0.0, r, o2);
				F.st.rounds++;
				for (const CollPairResult &x : o2) {
					if (x.kind == COLL_NONE || x.npt <= 0 || !(x.tau > 0.0)) continue;
					int a = fwdOf[x.bodyA], c = fwdOf[x.bodyB];
					if (idx (a) >= (int)memb.size () || idx (c) >= (int)memb.size ()) continue;
					bool held = false;
					for (int k : pidx) if (!pts[k].real && ((pts[k].a == a && pts[k].b == c) || (pts[k].a == c && pts[k].b == a))) held = true;
					if (held) {                                // the plan still overshoots: shorten by the remaining overshoot
						double over = 0;
						for (int q = 0; q < x.npt; q++) {
							const CollContact &c = x.pt[q];
							Vector va = x.a.v + Xc (x.a.w, c.pA - x.a.c) + c.vsA, vb = x.b.v + Xc (x.b.w, c.pB - x.b.c) + c.vsB;
							over = std::max (over, -dotp (va - vb, c.n)*(1.0 - x.tau)*h);
						}
						if (!(over > F.prm.slop)) continue;
						for (int k : pidx) if (!pts[k].real && ((pts[k].a == a && pts[k].b == c) || (pts[k].a == c && pts[k].b == a)) && pts[k].gap > 0) {
							pts[k].gap = std::max (0.0, pts[k].gap - over); redo = true;
						}
						continue;
					}
					std::vector<CollPairResult> one (1, x);
					std::vector<APt> add;
					Convert (one, add);
					for (APt &q : add) {
						if (q.real) continue;
						q.res = (int)res.size ();
						pidx.push_back ((int)pts.size ()); pts.push_back (q); redo = true;
					}
					res.push_back (x);
					solvedRes.push_back (0);
				}
			}
			if (!redo) break;
			build (all, true, mapAll);
			all.Solve (F.prm);
		}
		if (anySpec) { build (ro, false, mapRo); if (!ro.con.empty ()) ro.Solve (F.prm); }
		anySpecFrame = anySpecFrame || anySpec; anyRealFrame = anyRealFrame || anyReal;
		if (F.check) {                                        // island momentum of the impulsive parts (12)
			bool allDyn = true;
			Vector sP; double sJ = 0;
			for (size_t j = 0; j < memb.size (); j++) { allDyn = allDyn && w[memb[j]].dyn; sP += all.body[j].dP1 + all.body[j].dP2; }
			for (const CollSContact &c : all.con) sJ += c.J1.length () + c.J2.length ();
			if (allDyn && sP.length () > 1e-12*(sJ + 1.0)) Fail ("island P", sP.length (), sJ);
		}
		for (size_t j = 0; j < memb.size (); j++) {
			int i = memb[j];
			if (!w[i].dyn) continue;
			SpecB sp;
			if (anySpec) {
				sp.on = true;
				sp.dP1 = all.body[j].dP1; sp.dL1 = all.body[j].dL1; sp.dP2 = all.body[j].dP2; sp.dL2 = all.body[j].dL2;
				if (!ro.con.empty ()) { sp.dP1 -= ro.body[j].dP1; sp.dL1 -= ro.body[j].dL1; sp.dP2 -= ro.body[j].dP2; sp.dL2 -= ro.body[j].dL2; }
			}
			spec[i] = sp;
			Plan &pl = plan[i];
			inIsl[i] = 1; pl.any = true;
			pl.vNew = w[i].V + all.body[j].dP1/b[i].m;
			pl.wNew = OmegaOf (i, w[i].Q, SpinL (i, w[i].Q, w[i].W) + all.body[j].dL1);
			pl.F = all.body[j].dP2/h; pl.M = all.body[j].dL2/h;
			// load flag (7.4): slow contacts pressed at a_load
			bool slow = true;
			for (const CollSContact &c : all.con) if (((int)j == c.a || (int)j == c.b) && c.vapp >= F.prm.vrest) slow = false;
			if (slow && all.body[j].dP2.length ()/(b[i].m*h) >= COLL_A_LOAD) b[i].loaded = true;
		}
		// 12: position correction of pressed non-FIRST real points below -2 slop (5.4)
		{
			CollIsland pc;
			pc.tau = 0; pc.h = h;
			for (const CollSBody &sb0 : all.body) { CollSBody sb = sb0; sb.R1 = sb.Rt; sb.x1 = sb.xt; pc.body.push_back (sb); }
			for (size_t m = 0; m < pidx.size (); m++) {
				const APt &q = pts[pidx[m]];
				if (!q.real || mapAll[m] < 0) continue;
				const CollSContact &c = all.con[mapAll[m]];
				if ((c.flags & COLLP_FIRST) || !(dotp (c.J2, c.n) > 0) || !(c.gap < -2.0*F.prm.slop)) continue;
				pc.con.push_back (c);
			}
			if (!pc.con.empty ()) {
				std::vector<CollDelta> d;
				if (pc.Correct (F.prm, d)) for (size_t j = 0; j < memb.size (); j++) if (w[memb[j]].dyn) { plan[memb[j]].dxc = d[j].dx; plan[memb[j]].dthc = d[j].dth; }
			}
		}
		// 11, 13: real results solved; event records of real points
		for (size_t m = 0; m < pidx.size (); m++) {
			const APt &q = pts[pidx[m]];
			if (!q.real || mapAll[m] < 0) continue;
			const CollSContact &c = all.con[mapAll[m]];
			if (!solvedRes[q.res]) { solvedRes[q.res] = 1; fwd.Solved (res[q.res]); }
			CollEventRec e {};
			e.t = q.t; e.pt = q.pt; e.con = mapAll[m];
			auto ri = realIdx.find (q.res);
			if (ri == realIdx.end ()) { ri = realIdx.emplace (q.res, (int)solvedReal.size ()).first; solvedReal.push_back (res[q.res]); }
			e.res = ri->second;
			e.oa = fwd.Owner (res[q.res], q.pt, 0); e.ob = fwd.Owner (res[q.res], q.pt, 1);
			e.kind = c.kind; e.gap = c.gap;
			e.vapp = c.vapp; e.ln1 = c.ln1; e.Wn = c.Wn; e.Wt = c.Wt;
			e.vpost = dotp (all.UPost ()[mapAll[m]], c.n);
			e.slip = (all.UPre ()[mapAll[m]] - c.n*dotp (all.UPre ()[mapAll[m]], c.n)).length ();
			e.Jt = (c.J1 - c.n*c.ln1).length ();
			e.jsum = c.J1.length () + c.J2.length ();
			e.meff = all.MeffAt (c.p, c.n, c.a, c.b);
			e.corrected = false; e.surf = false; e.woke = false;
			erec.push_back (e);
			// 14: resting row for next frame (7.5)
			CollASupRow s {};
			s.ka = w[q.a].key; s.kb = w[q.b].key;
			Vector p = q.org + (q.pa + q.pb)*0.5;
			s.ra = tmul (QM (Rot (q.a)), p - Pos (q.a)); s.rb = tmul (QM (Rot (q.b)), p - Pos (q.b)); s.nb = tmul (QM (Rot (q.b)), q.n);
			s.J2n = std::max (0.0, dotp (c.J2, c.n));
			F.sup.push_back (s);
		}
		// 14: records of a speculative island (2.1)
		if (anySpec) {
			CollAIslandRec R;
			R.h = h;
			for (size_t m = 0; m < pidx.size (); m++) {
				const APt &q = pts[pidx[m]];
				if (mapAll[m] < 0) continue;
				const CollSContact &ca = all.con[mapAll[m]];
				Vector J = ca.J1 + ca.J2;
				if (q.real && !ro.con.empty () && mapRo[m] >= 0) { const CollSContact &cr = ro.con[mapRo[m]]; J -= cr.J1 + cr.J2; }
				if (J.length () == 0.0) continue;
				Vector p = q.org + (q.pa + q.pb)*0.5;
				R.c.push_back (CollAContactRec { w[q.a].key, w[q.b].key, tmul (QM (Rot (q.a)), p - Pos (q.a)), tmul (QM (Rot (q.b)), p - Pos (q.b)), J });
			}
			for (size_t m = 0; m < pidx.size (); m++) {
				const APt &q = pts[pidx[m]];
				if (q.real) continue;
				CollAPairRec *pp = nullptr;
				for (CollAPairRec &x : R.p) if (x.ka == w[q.a].key && x.kb == w[q.b].key) pp = &x;
				if (pp && pp->g0 <= q.gap) continue;
				if (!pp) { R.p.push_back (CollAPairRec {}); pp = &R.p.back (); }
				pp->ka = w[q.a].key; pp->kb = w[q.b].key; pp->n = q.n; pp->g0 = q.gap;
				pp->ra = tmul (QM (Rot (q.a)), q.org + q.pa - Pos (q.a));
				pp->rb = tmul (QM (Rot (q.b)), q.org + q.pb - Pos (q.b));
				Vector s = q.org + (q.pa + q.pb)*0.5;
				pp->u0 = -dotp (PointVel (q.a, s - Pos (q.a)) - PointVel (q.b, s - Pos (q.b)), q.n);
			}
			for (int i : memb) if (w[i].dyn) { CollABodyRec br {}; br.key = w[i].key; br.memberHash = b[i].memberHash; R.b.push_back (br); }
			if (!R.p.empty ()) newIsl.push_back (R);
		}
		for (int i : memb) done[i] = 1;
	}
	// events of the touch frames (5.6): FIRST real points, dKE and vn never negative
	if (!erec.empty ()) {
		std::vector<CollImpactEvent> e2;
		CollFillEvents (fwd, solvedReal, erec, h, simt0, F.prm, host, e2, nullptr, nullptr);
		for (CollImpactEvent &e : e2) {
			if (!(e.flags & COLLEV_FIRST)) continue;
			if (e.dKE < 0 || e.vn < 0) F.st.clampE++;
			e.dKE = std::max (0.0, e.dKE); e.vn = std::max (0.0, e.vn);
			ev.push_back (e);
		}
	}
	if (anySpecFrame) { F.st.spec++; CollLog (COLLLOG_FINE, "Collision spec: t=%.6f islands %d", simt0, (int)newIsl.size ()); }
	if (anyRealFrame) F.st.real++;
	// 15: plans and their delivery on the mirror; body records from the mirror difference (6.2, 2.1)
	F.pend.clear ();
	for (int i = 0; i < nb; i++) {
		if (!w[i].dyn) continue;
		Wk &B = w[i];
		CollABodyRec *br = nullptr;
		for (CollAIslandRec &R : newIsl) for (CollABodyRec &x : R.b) if (x.key == B.key) br = &x;
		Plan pl = plan[i];
		if (!inIsl[i]) { pl.vNew = B.V; pl.wNew = B.W; }
		bool needs = inIsl[i] || B.posChanged || (B.V - B.o.s.vel).length () > 0 || (B.W - B.o.s.omega).length () > 0 || B.Fprev.length () > 0 || B.Mprev.length () > 0;
		CollAMem &mm = F.mem[B.key];
		if (!needs) {
			mm.xs = B.X; mm.vs = B.V; mm.ws = B.W; mm.qs = B.Q; mm.vw = B.V; mm.Fprev = mm.Mprev = Vector ();
			mm.Pw = B.o; mm.P = mm.Pw; mir.Step (mm.P, h); mm.hasP = true; mm.hPrev = h;
			continue;
		}
		Wk Bf = B;
		Vector tgtP;
		CollOrbState cf;
		CollAWrite wr = Apply (i, pl, true, &tgtP, &cf);
		if (wr.state || wr.attitude || wr.spin || wr.force) out.push_back (wr);
		if (wr.weight) F.pend.push_back (Pend { B.key, i, B.o, cf, tgtP, h });
		if (br) {
			const SpecB &sp = spec[i];
			Plan pf = pl;
			pf.vNew -= sp.dP1/b[i].m;
			pf.wNew = OmegaOf (i, Bf.Q, SpinL (i, Bf.Q, pl.wNew) - sp.dL1);
			pf.F -= sp.dP2/h; pf.M -= sp.dL2/h;
			std::swap (w[i], Bf);
			Apply (i, pf, false);
			std::swap (w[i], Bf);
			CollOrbState a = B.o, f = Bf.o;
			br->aw = a; br->fw = f;
			mir.Step (a, h); mir.Step (f, h);
			br->dx = a.s.pos - f.s.pos; br->dv = a.s.vel - f.s.vel; br->dLs = a.SpinL () - f.SpinL (); br->dth = TurnOf (f.s.Q, a.s.Q);
			br->xs = Bf.xs; br->vs = Bf.vs; br->ws = Bf.ws; br->qs = Bf.qs;
			br->zero = br->dx.length () == 0 && br->dv.length () == 0 && br->dth.length () == 0 && br->dLs.length () == 0;
		}
		mm.Pw = B.o; mm.P = mm.Pw; mir.Step (mm.P, h); mm.hasP = true; mm.hPrev = h;
	}
	F.isl.swap (newIsl);
	// 16: warp inputs (9, 7.4)
	F.warp = CollWarpInput ();
	std::vector<CollEnd> end (fwd.nBody ());
	for (int d = 0; d < fwd.nBody (); d++) {
		int i = fwdOf[d];
		if (w[i].dyn) { const CollOrbState &P = F.mem[w[i].key].P; end[d] = CollEnd { P.s.pos, P.s.vel, w[i].aFree, b[i].rmax, 0.0 }; }
		else end[d] = CollEnd { b[i].kin.c1, b[i].kin.v1, b[i].kin.a1, b[i].rmax, 0.0 };
	}
	fwd.LookAhead (end, F.warp);
	for (int i = 0; i < nb; i++) {
		if (!w[i].dyn) continue;
		CollAMem &mm = F.mem[w[i].key];
		if (b[i].loaded && mm.loadPrev) {
			double hl = (b[i].thrust || simt0 - mm.tRec <= 2.0) ? F.hRest : COLL_H_REST;
			if (hl < F.warp.hLoad) { F.warp.hLoad = hl; F.warp.idLoad[0] = b[i].id; F.warp.idLoad[1] = 0; }
		}
		mm.loadPrev = b[i].loaded;
	}
	// non-dynamic bodies keep no prediction; bodies not seen this frame are forgotten
	for (auto it = F.mem.begin (); it != F.mem.end ();) {
		int i = Find (it->first);
		if (i < 0 || !w[i].dyn) it = F.mem.erase (it); else ++it;
	}
}

void CollAddonFrame::Run (CollDetect &fwd, CollDetect &ver, const CollOrbMirror &mir, std::vector<CollABody> &b, const std::vector<CollZone> &zones,
	double h, double simt0, CollSolveHost &host, std::vector<CollAWrite> &out, std::vector<CollImpactEvent> &ev)
{
	out.clear (); ev.clear (); pend.clear ();
	if (!(h > 0.0)) return;                                    // simdt == 0: no physics, records kept (1.2)
	Impl im (*this, fwd, ver, mir, b, host, h, simt0);
	im.Run (out, ev, zones);
}

// 6.3 step 4: the force of each single body written with SetState absorbs the gravity-estimate residual
void CollAddonFrame::Finish (const CollOrbMirror &mir, std::vector<CollABody> &b, std::vector<CollAWrite> &out, const std::vector<Vector> &gExact)
{
	for (Pend &p : pend) {
		if (p.body < 0 || p.body >= (int)gExact.size ()) continue;
		CollAWrite *wr = nullptr;
		for (CollAWrite &x : out) if (x.body == p.body) wr = &x;
		if (!wr) continue;
		CollOrbState c0 = p.conf;
		c0.acc = gExact[p.body]; c0.gReset = gExact[p.body];
		const double m = c0.m, h = p.h;
		for (int it = 0; it < 8; it++) {
			CollOrbState c = c0; c.Fadd = Vector (); c.Madd = p.conf.Madd; c.AddForce (wr->Fb, Vector ());
			CollOrbState cs = c; mir.Step (cs, h);
			CollOrbState cfs = p.cf; mir.Step (cfs, h, cs.lv, cs.nsub);
			Vector eP = p.tgtP - (cs.s.vel - cfs.s.vel)*m;
			if (eP.length () <= 1e-14*(p.tgtP.length () + m*1e-3)) break;
			Matrix Js;
			for (int k = 0; k < 3; k++) {
				Vector e; e.data[k] = 1.0;
				CollOrbState c1 = c; c1.AddForce (e, Vector ()); mir.Step (c1, h);
				Vector dv = (c1.s.vel - cs.s.vel)*m;
				Js(0,k) = dv.x; Js(1,k) = dv.y; Js(2,k) = dv.z;
			}
			wr->Fb += mul (InvM (Js), eP);
		}
		wr->force = wr->Fb.length () > 0 || wr->Mb.length () > 0;
		auto it = mem.find (p.key);
		if (it != mem.end ()) {
			CollOrbState c = c0; c.Fadd = Vector (); c.Madd = p.conf.Madd; c.AddForce (wr->Fb, Vector ());
			it->second.Fprev = wr->Fb;
			it->second.Pw = c; it->second.P = c; mir.Step (it->second.P, h);
		}
	}
	pend.clear ();
	(void)b;
}

void CollAddonFrame::OnMembers (const CollAMembersChange &c)
{
	uint64_t k = Key (COLLB_DYNAMIC, c.assembly);
	mem.erase (k);
	for (CollAIslandRec &I : isl) for (CollABodyRec &r : I.b) if (r.key == k) r.memberHash = ~r.memberHash;
}

void CollAddonFrame::OnDelete (uint32_t vesselId)
{
	uint64_t k = Key (COLLB_DYNAMIC, vesselId);
	mem.erase (k);
	for (CollAIslandRec &I : isl) {
		I.c.erase (std::remove_if (I.c.begin (), I.c.end (), [k] (const CollAContactRec &x) { return x.ka == k || x.kb == k; }), I.c.end ());
		I.p.erase (std::remove_if (I.p.begin (), I.p.end (), [k] (const CollAPairRec &x) { return x.ka == k || x.kb == k; }), I.p.end ());
		I.b.erase (std::remove_if (I.b.begin (), I.b.end (), [k] (const CollABodyRec &x) { return x.key == k; }), I.b.end ());
		if (I.p.empty ()) I.p.push_back (CollAPairRec { k, k, Vector (), Vector (), Vector (), 1e9, 0 }); // survivors take the FREE path
	}
	sup.erase (std::remove_if (sup.begin (), sup.end (), [k] (const CollASupRow &x) { return x.ka == k || x.kb == k; }), sup.end ());
}

void CollAddonFrame::OnTimeJump ()
{
	isl.clear (); sup.clear ();
	for (auto &m : mem) m.second.hasP = false;
}

void CollAddonFrame::Reset ()
{
	isl.clear (); sup.clear (); mem.clear (); pend.clear ();
	st = CollAStats {};
	kt.Reset ();
}
