// not upstream: collision detection (D2): motion, broad phase, GRACE, zones, CA, manifold

#include <algorithm>
#include <cmath>
#include "CollDetect.h"

constexpr double MERGE_GAP_TOL = 1e-9;   // a duplicate replaces the kept manifold point only when deeper by more than this [m]
constexpr double SPEC_LOG_INTERVAL = 60.0; // one CA-cap INFO line per body pair per this sim time [s]
constexpr double ORDER_GAP_TOL = 1e-3;     // manifold points this close in gap to their group's deepest point are ordered by feature key [m]

// owner keys (Y14)

bool CollOwnerKey::operator< (const CollOwnerKey &o) const
{
	if (kind != o.kind) return kind < o.kind;
	if (id != o.id) return id < o.id;
	if (planet != o.planet) return planet < o.planet;
	if (base != o.base) return base < o.base;
	if (obj != o.obj) return obj < o.obj;
	return part < o.part;
}

bool CollOwnerKey::operator== (const CollOwnerKey &o) const
{
	return kind == o.kind && id == o.id && planet == o.planet && base == o.base && obj == o.obj && part == o.part;
}

// rotation helpers in Orbiter's conventions (Quaternion::Rotate, Matrix::Set (Quaternion), D3 2.4)

static double Det3 (const Matrix &M)
{
	return M.m11*(M.m22*M.m33 - M.m23*M.m32) - M.m12*(M.m21*M.m33 - M.m23*M.m31) + M.m13*(M.m21*M.m32 - M.m22*M.m31);
}

// X.A invertible: finite determinant not negligible against its largest entry cubed
static bool Invertible (const Matrix &M)
{
	double s = 0.0;
	for (int i = 0; i < 9; i++) s = std::max (s, std::fabs (M.data[i]));
	double d = Det3 (M);
	return std::isfinite (d) && std::isfinite (s) && std::fabs (d) > 1e-12*s*s*s;
}

static Quaternion QInv (const Quaternion &q)
{
	double n = q.norm2 ();
	if (n <= 0.0) return Quaternion ();
	return Quaternion (-q.qvx/n, -q.qvy/n, -q.qvz/n, q.qs/n);
}

// exact body increment of Quaternion::Rotate's generator: q.Rotate (phi) ~ q * QExp (phi)
static Quaternion QExp (const Vector &phi)
{
	double a = phi.length ();
	if (a < 1e-300) return Quaternion ();
	double s = sin (0.5*a)/a;
	return Quaternion (phi.x*s, phi.y*s, phi.z*s, cos (0.5*a));
}

// body rotation vector of q on the shortest arc
static Vector QLog (const Quaternion &q)
{
	double s = q.qs;
	Vector v (q.qvx, q.qvy, q.qvz);
	if (s < 0.0) { s = -s; v = -v; }
	double n = v.length ();
	if (n < 1e-300) return Vector ();
	return v * (2.0*atan2 (n, s)/n);
}

static Matrix QMat (const Quaternion &q)
{
	Matrix R;
	R.Set (q);
	return R;
}

// q turned by the global rotation vector psi
static Quaternion RotG (const Quaternion &q, const Vector &psi)
{
	return q * QExp (tmul (QMat (q), psi));
}

static double Frob (const Matrix &A)
{
	double s = 0.0;
	for (int i = 0; i < 9; i++) s += A.data[i]*A.data[i];
	return sqrt (s);
}

static Vector Col (const Matrix &A, int k)
{
	return Vector (A.data[k], A.data[3+k], A.data[6+k]);
}

static Matrix MSub (const Matrix &A, const Matrix &B)
{
	Matrix C;
	for (int i = 0; i < 9; i++) C.data[i] = A.data[i]-B.data[i];
	return C;
}

// rotation angle of an orthonormal matrix, [0, pi]
static double MAngle (const Matrix &R)
{
	Vector v (R.m32-R.m23, R.m13-R.m31, R.m21-R.m12);
	return atan2 (v.length (), R.m11+R.m22+R.m33-1.0);
}

static double SegDist (const Vector &p, const Vector &a, const Vector &b)
{
	Vector ab = b-a;
	double l2 = ab.length2 ();
	double t = l2 > 0.0 ? ((p-a) & ab)/l2 : 0.0;
	t = std::min (1.0, std::max (0.0, t));
	return (a + ab*t - p).length ();
}

// motion model (1.8)

static void Herm (double s, double &h10, double &h01, double &h11)
{
	double s2 = s*s, s3 = s2*s;
	h10 = s3 - 2.0*s2 + s;
	h01 = -2.0*s3 + 3.0*s2;
	h11 = s3 - s2;
}

static void HermD (double s, double &d10, double &d01, double &d11)
{
	double s2 = s*s;
	d10 = 3.0*s2 - 4.0*s + 1.0;
	d01 = -6.0*s2 + 6.0*s;
	d11 = 3.0*s2 - 2.0*s;
}

static double LocalS (const CollMotion &m, double tau)
{
	double w = m.tb - m.ta;
	return w > 0.0 ? (tau - m.ta)/w : 0.0;
}

// Pos (tau) - c0 without the large absolute term (precision at 1 AU, 4)
static Vector Off (const CollMotion &m, double tau)
{
	double H = m.h*(m.tb - m.ta), h10, h01, h11;
	Herm (LocalS (m, tau), h10, h01, h11);
	return m.v0*(H*h10) + (m.c1 - m.c0)*h01 + m.v1*(H*h11);
}

static Vector AccAt (const CollMotion &m, double tau)
{
	double s = LocalS (m, tau);
	return m.a0*(1.0-s) + m.a1*s;
}

// spin path end correction: d = q0^-1 Exp(-H wm) q1 as a body rotation vector
static Vector SpinCorr (const CollMotion &m, const Vector &wm, double H)
{
	return QLog (QInv (m.q0) * RotG (m.q1, -wm*H));
}

void CollMotion::Setup ()
{
	if (!a0ok) a0 = a1;
	double H = h*(tb - ta);
	spin = std::max (w0g.length (), w1g.length ())*H > COLL_SPIN_PATH;
	if (spin) {
		Vector wm = (w0g + w1g)*0.5;
		theta = wm.length ()*H + SpinCorr (*this, wm, H).length ();
	} else
		theta = QLog (QInv (q0) * q1).length ();
}

Vector CollMotion::Pos (double tau) const
{
	return c0 + Off (*this, tau);
}

Vector CollMotion::Vel (double tau) const
{
	double H = h*(tb - ta), d10, d01, d11;
	if (H <= 0.0) return v0;
	HermD (LocalS (*this, tau), d10, d01, d11);
	return v0*d10 + (c1 - c0)*(d01/H) + v1*d11;
}

Quaternion CollMotion::Rot (double tau) const
{
	double s = LocalS (*this, tau), H = h*(tb - ta);
	if (!spin) return q0 * QExp (QLog (QInv (q0) * q1)*s);
	Vector wm = (w0g + w1g)*0.5;
	return RotG (q0 * QExp (SpinCorr (*this, wm, H)*s), wm*(H*s));
}

Vector CollMotion::Omega (double tau) const
{
	double H = h*(tb - ta);
	if (H <= 0.0) return w0g;
	if (!spin) return mul (QMat (q0), QLog (QInv (q0) * q1))/H;
	Vector wm = (w0g + w1g)*0.5;
	return wm + mul (QMat (RotG (q0, wm*(H*LocalS (*this, tau)))), SpinCorr (*this, wm, H))/H;
}

CollMotion CollMotion::Split (double tau, const CollRestart &r) const
{
	if (tau < ta) {                       // never extrapolate the old path backwards
		CollLog (COLLLOG_FINE, "Collision: split at tau %.6f before the motion start %.6f, clamped", tau, ta);
		tau = ta;
	}
	CollMotion m = *this;
	m.c0 = Pos (tau);
	m.v0 = Vel (tau) + r.dv;
	double span = (tb - tau)*h;
	Vector a2 = span > 0.0 ? ((r.v1 - v1) - r.dv)/span : Vector (); // D3's constant phase-2 acceleration over [tau, tb]
	m.a0 = AccAt (*this, tau) + a2;
	m.a1 = a1 + a2;
	m.q0.Set (Rot (tau));
	m.w0g = Omega (tau) + r.dwg;
	m.c1 = r.c1; m.v1 = r.v1; m.q1.Set (r.q1); m.w1g = r.w1g;
	m.ta = tau;
	m.a0ok = true;
	m.Setup ();
	return m;
}

// member-wise copy (Quaternion has no copy assignment)
static void MotionCopy (CollMotion &d, const CollMotion &s)
{
	d.c0 = s.c0; d.v0 = s.v0; d.a0 = s.a0;
	d.c1 = s.c1; d.v1 = s.v1; d.a1 = s.a1;
	d.q0.Set (s.q0); d.q1.Set (s.q1);
	d.w0g = s.w0g; d.w1g = s.w1g;
	d.h = s.h; d.ta = s.ta; d.tb = s.tb; d.theta = s.theta; d.spin = s.spin; d.a0ok = s.a0ok;
}

// relative path data of B seen from A over the common interval [s0, tb]
static void RelData (const CollMotion &A, const CollMotion &B, double &s0, double &H, Vector &r0, Vector &w0, Vector &r1, Vector &w1)
{
	s0 = std::max (A.ta, B.ta);
	double tb = std::min (A.tb, B.tb);
	H = A.h*(tb - s0);
	if (A.ta == s0 && B.ta == s0) { r0 = B.c0 - A.c0; w0 = B.v0 - A.v0; }
	else { r0 = (B.c0 - A.c0) + Off (B, s0) - Off (A, s0); w0 = B.Vel (s0) - A.Vel (s0); }
	r1 = B.c1 - A.c1;
	w1 = B.v1 - A.v1;
}

void CollRelBezier (const CollMotion &A, const CollMotion &B, Vector Q[4], Vector M[3])
{
	double s0, H;
	Vector r0, w0, r1, w1;
	RelData (A, B, s0, H, r0, w0, r1, w1);
	Q[0] = r0; Q[1] = r0 + w0*(H/3.0); Q[2] = r1 - w1*(H/3.0); Q[3] = r1;
	M[0] = w0*H; M[1] = (r1 - r0)*3.0 - (w0 + w1)*H; M[2] = w1*H;
}

bool CollCapsuleHit (const Vector Q[4], double reach)
{
	double rc = std::max (SegDist (Q[1], Q[0], Q[3]), SegDist (Q[2], Q[0], Q[3]));
	return SegDist (Vector (), Q[0], Q[3]) - rc <= reach;
}

double CollErrorT (const CollMotion &A, const CollMotion &B)
{
	double s0, H;
	Vector r0, w0, r1, w1;
	RelData (A, B, s0, H, r0, w0, r1, w1);
	if (H <= 0.0) return 0.0;
	Vector ar0 = AccAt (B, s0) - AccAt (A, s0), ar1 = B.a1 - A.a1;
	Vector hpp0 = ((r1 - r0)*6.0 - (w0*4.0 + w1*2.0)*H)/(H*H);
	Vector hpp1 = ((r0 - r1)*6.0 + (w0*2.0 + w1*4.0)*H)/(H*H);
	return H*H/16.0*std::max ((ar0 - hpp0).length (), (ar1 - hpp1).length ());
}

double CollErrorR (const CollMotion &m, double rmax)
{
	return rmax*m.h*(m.tb - m.ta)*0.25*(m.w1g - m.w0g).length ();
}

static double ThetaEff (const CollMotion &m)
{
	double H = m.h*(m.tb - m.ta);
	return std::max (m.theta, std::max (m.w0g.length (), m.w1g.length ())*H);
}

bool CollLookAheadHit (const CollEnd &a, const CollEnd &b, double h, double deltaCt)
{
	double H = COLL_LOOKAHEAD*h;
	Vector r1 = b.c1 - a.c1, w1 = b.v1 - a.v1, ar = b.a1 - a.a1;
	return SegDist (Vector (), r1, r1 + w1*H) - ar.length ()*H*H*0.5 <= a.rmax + b.rmax + deltaCt + a.disp + b.disp;
}

// warp guard, jump and zone helpers (7.3, 1.7, 3.3, 3.4)

double CollFloorPow10 (double w)
{
	double p = 1.0;
	if (!(w >= 10.0)) return 1.0;
	while (p*10.0 <= w && p < 1e300) p *= 10.0;
	return p;
}

double CollWarpAllowed (const CollWarpInput &w, double warp, double dtSys)
{
	double W = 1e100;
	if (dtSys > 0.0) {
		if (w.hContact < 1e99) W = std::min (W, CollFloorPow10 (w.hContact/dtSys));
		if (w.hLoad < 1e99) W = std::min (W, CollFloorPow10 (w.hLoad/dtSys));
	}
	if (w.accF < 1.0) W = std::min (W, CollFloorPow10 (warp*w.accF));
	return W;
}

int CollJumpPhase (bool s1IsS0, bool p1Ran)
{
	return (!s1IsS0 && !p1Ran) ? COLLJP_INSTEP : COLLJP_T0;
}

double CollGeomJump (const Vector &dr, const Vector *dofs, size_t n)
{
	if (!n) return dr.length ();
	double m = 0.0;
	for (size_t i = 0; i < n; i++) m = std::max (m, (dr + dofs[i]).length ());
	return m;
}

static double AngleOf (const Vector &a, const Vector &b)
{
	double la = a.length (), lb = b.length ();
	if (la <= 0.0 || lb <= 0.0) return Pi;
	double c = (a & b)/(la*lb);
	return acos (std::min (1.0, std::max (-1.0, c)));
}

bool CollDockZoneActive (const CollPortAt &a, const CollPortAt &b, double rdz, double angleDeg, double vmax)
{
	Vector dg = b.g - a.g;
	if (!(dg.length () < COLL_DOCK_ZONE_ON)) return false;
	if (AngleOf (a.d, -b.d) > angleDeg*_RAD_) return false;
	double ld = a.d.length ();
	if (ld <= 0.0) return false;
	Vector da = a.d/ld;
	if ((dg - da*(dg & da)).length () > rdz) return false;
	return -((b.v - a.v) & da) < vmax;
}

bool CollAttachZoneActive (const CollPortAt &p, const CollPortAt &c, double angleDeg, double vmax)
{
	if (!((c.g - p.g).length () < COLL_ATTACH_ZONE_ON)) return false;
	if (AngleOf (c.d, -p.d) > angleDeg*_RAD_) return false;
	if (AngleOf (c.r, p.r) > angleDeg*_RAD_) return false;
	return (c.v - p.v).length () < vmax;
}

bool CollAttachIdMatch (const char *parentId, const char *childId)
{
	if (!parentId || !childId) return false;
	size_t n = 0;
	while (n < 8 && parentId[n]) n++;
	if (!n) return false;
	for (size_t i = 0; i < n; i++)
		if (childId[i] != parentId[i]) return false;
	return true;
}

// pair store (6.1, R22)

bool CollLeafPair::operator< (const CollLeafPair &o) const
{
	if (partKeyA != o.partKeyA) return partKeyA < o.partKeyA;
	if (verA != o.verA) return verA < o.verA;
	if (leafA != o.leafA) return leafA < o.leafA;
	if (partKeyB != o.partKeyB) return partKeyB < o.partKeyB;
	if (verB != o.verB) return verB < o.verB;
	return leafB < o.leafB;
}

bool CollLeafPair::operator== (const CollLeafPair &o) const
{
	return partKeyA == o.partKeyA && verA == o.verA && leafA == o.leafA && partKeyB == o.partKeyB && verB == o.verB && leafB == o.leafB;
}

static bool EntryLess (const CollPairEntry &e, const std::pair<CollOwnerKey, CollOwnerKey> &k)
{
	if (e.a == k.first) return e.b < k.second;
	return e.a < k.first;
}

CollPairEntry *CollPairStore::Find (const CollOwnerKey &a, const CollOwnerKey &b)
{
	std::pair<CollOwnerKey, CollOwnerKey> k = b < a ? std::make_pair (b, a) : std::make_pair (a, b);
	auto it = std::lower_bound (e.begin (), e.end (), k, EntryLess);
	return (it != e.end () && it->a == k.first && it->b == k.second) ? &*it : nullptr;
}

const CollPairEntry *CollPairStore::Find (const CollOwnerKey &a, const CollOwnerKey &b) const
{
	std::pair<CollOwnerKey, CollOwnerKey> k = b < a ? std::make_pair (b, a) : std::make_pair (a, b);
	auto it = std::lower_bound (e.begin (), e.end (), k, EntryLess);
	return (it != e.end () && it->a == k.first && it->b == k.second) ? &*it : nullptr;
}

CollPairEntry &CollPairStore::Get (const CollOwnerKey &a, const CollOwnerKey &b)
{
	std::pair<CollOwnerKey, CollOwnerKey> k = b < a ? std::make_pair (b, a) : std::make_pair (a, b);
	auto it = std::lower_bound (e.begin (), e.end (), k, EntryLess);
	if (it != e.end () && it->a == k.first && it->b == k.second) return *it;
	CollPairEntry n;
	n.a = k.first; n.b = k.second;
	return *e.insert (it, std::move (n));
}

void CollPairStore::PurgeVessel (uint32_t id)
{
	e.erase (std::remove_if (e.begin (), e.end (), [id] (const CollPairEntry &x) {
		return (x.a.kind == COLLO_VESSEL && x.a.id == id) || (x.b.kind == COLLO_VESSEL && x.b.id == id); }), e.end ());
}

void CollPairStore::FlushFront ()
{
	for (auto &x : e) { x.front.clear (); x.frontCut = -1.0; x.frontMotion = 0.0; }
}

void CollPairStore::Clear ()
{
	e.clear ();
}

size_t CollPairStore::Size () const
{
	return e.size ();
}

const CollPairEntry &CollPairStore::At (size_t i) const
{
	return e[i];
}

// query machinery

struct CollDetect::Impl {
	enum { POSE_T0, POSE_T1, POSE_MODEL };
	enum { Q_SCOPES = 1, Q_ZONES = 2, Q_FIRST = 4, Q_RAWX = 8, Q_RAW = 16, Q_FRONT = 32 };

	struct Placed {                  // one part placed for a query
		const CollPartRef *r;
		const CollGeom *g;
		int idx;                     // part index in its body
		CollAffine X;                // part local -> query frame (global axes, origin at A's CG at the pose)
		bool orth;                   // X.A orthonormal: box axes are candidate axes
		double marg;                 // disp of a part held at P1, else 0 (5.2)
		size_t vbase;                // first slot in the vertex cache
		CollAffine Pb;               // pose for rho: P0 if interpolated, else P1
		bool rigidPath;              // interpolated rigid path (turn about c)
		double thPart;               // its turn angle
		Vector dc;                   // P1(c) - P0(c)
		Matrix dA; Vector dt;        // interpolated affine path: A1 - A0, t1 - t0
		Vector pc; double rp;        // part sphere in the body frame: centre, radius incl. skin
	};
	struct ZoneAt { CollOwnerKey oa, ob; Vector ca, cb; double ra, rb; Vector ga, gb; }; // zone oriented to the pair; ga, gb in the query frame
	struct PP {                      // one part pair of a body pair
		int a, b;
		OwnerPair key;               // owner pair in store order
		bool swap;                   // A's owner is the store's b
		bool culled;                 // part spheres apart over the interval (2.5)
		bool support; Vector supN;   // support pair (Y10), stored normal in the base frame (0 = none)
		const std::vector<CollLeafPair> *grace;
		const std::vector<CollLeafPair> *front;
		std::vector<int> zones;      // indices into Ctx::zl
	};
	struct Raw { int k; uint32_t ta, tb, la, lb; double d; Vector pa, pb; };
	struct Cand { Vector pA, pB, n; double d, gap; int k; uint32_t ta, tb, k1, k2; uint8_t flags, patch, kc; }; // feature key: kc 0 A's vertex k1, 1 B's vertex k1, 2 triangles k1, k2
	struct Ctx {
		CollDetect *d;
		int ia, ib;
		const CollBody *A, *B;
		double s0, span;
		Vector Q[4], M[3];
		double thA, thB;             // body rotation bound per unit frame tau
		double E, Em, thetaEff, Dstep;
		std::vector<Placed> pa, pb;
		std::vector<PP> pp;
		std::vector<int> ord;        // pp indices sorted by owner pair (stable)
		std::vector<size_t> grp;     // start of each owner-pair group in ord, plus ord.size ()
		std::vector<ZoneAt> zl;
		Matrix RA, RB; Vector r, cA; // pose: rotations, B's CG minus A's CG, A's CG minus the origin
		int mode; double tau;
		uint32_t flags;              // COLLF_GRACE, COLLF_ZONE seen
		long long tt, bv;
		std::vector<std::pair<uint32_t, uint32_t>> stk;
	};
	struct Box { Vector g, e, cl; Matrix L; bool orth; };

	// setup

	static void Place (Placed &p, const CollPartRef &r, int idx)
	{
		p.r = &r; p.g = r.geom; p.idx = idx;
		p.marg = r.interp ? 0.0 : r.disp;
		p.Pb = r.interp ? r.P0 : r.P1;
		p.orth = r.rigid;
		p.rigidPath = false; p.thPart = 0.0;
		p.dA = Matrix (); p.dt = Vector (); p.dc = Vector ();
		if (r.interp) {
			if (r.rigid) {
				p.rigidPath = true;
				p.thPart = MAngle (r.P1.A * transp (r.P0.A));
				p.dc = CollApply (r.P1, r.c) - CollApply (r.P0, r.c);
			} else {
				p.dA = MSub (r.P1.A, r.P0.A);
				p.dt = r.P1.t - r.P0.t;
			}
		}
		p.pc = CollApply (p.Pb, r.c);
		double sc = r.rigid ? 1.0 : std::max (Frob (r.P0.A), Frob (r.P1.A));
		p.rp = (r.geom ? r.geom->bsRadius : 0.0)*sc + r.skin;
	}

	static const CollSupport *SupportOf (const CollDetect &d, const CollBody &as, const CollOwnerKey &bo)
	{
		auto it = std::lower_bound (d.m_support.begin (), d.m_support.end (), as.id,
			[] (const std::pair<uint32_t, std::vector<CollSupport>> &x, uint32_t id) { return x.first < id; });
		if (it == d.m_support.end () || it->first != as.id) return nullptr;
		for (const CollSupport &s : it->second) {
			const CollOwnerKey &k = s.building;
			if (k.kind != COLLO_BUILDING || bo.kind != COLLO_BUILDING || k.planet != bo.planet) continue;
			if (k.base == -1) return &s;
			if (k.base != bo.base) continue;
			if (k.obj == -1 || (k.obj == bo.obj && k.part == bo.part)) return &s;
		}
		return nullptr;
	}

	static void Prepare (CollDetect &d, Ctx &cx, int ia, int ib)
	{
		const CollParams &p = d.m_prm;
		cx.d = &d; cx.ia = ia; cx.ib = ib;
		cx.A = &d.m_body[ia]; cx.B = &d.m_body[ib];
		const CollMotion &ma = cx.A->m, &mb = cx.B->m;
		cx.s0 = std::max (ma.ta, mb.ta);
		cx.span = std::max (1e-300, std::min (ma.tb, mb.tb) - cx.s0);
		CollRelBezier (ma, mb, cx.Q, cx.M);
		cx.thA = ma.tb > ma.ta ? ma.theta/(ma.tb - ma.ta) : 0.0;
		cx.thB = mb.tb > mb.ta ? mb.theta/(mb.tb - mb.ta) : 0.0;
		cx.E = CollErrorT (ma, mb) + CollErrorR (ma, cx.A->rmax) + CollErrorR (mb, cx.B->rmax);
		cx.Em = std::min (cx.E, p.eMax);
		cx.thetaEff = std::max (ThetaEff (ma), ThetaEff (mb));
		cx.flags = 0; cx.tt = 0; cx.bv = 0; cx.mode = -1; cx.tau = 0.0;
		size_t vb = 0;
		double dmax = 0.0;
		cx.pa.resize (cx.A->parts.size ());
		for (size_t k = 0; k < cx.pa.size (); k++) {
			Place (cx.pa[k], cx.A->parts[k], (int)k);
			cx.pa[k].vbase = vb; vb += cx.pa[k].g ? cx.pa[k].g->vtx.size () : 0;
			dmax = std::max (dmax, cx.A->parts[k].disp);
		}
		cx.pb.resize (cx.B->parts.size ());
		for (size_t k = 0; k < cx.pb.size (); k++) {
			Place (cx.pb[k], cx.B->parts[k], (int)k);
			cx.pb[k].vbase = vb; vb += cx.pb[k].g ? cx.pb[k].g->vtx.size () : 0;
			dmax = std::max (dmax, cx.B->parts[k].disp);
		}
		if (d.m_vx.size () < vb) { d.m_vx.resize (vb); d.m_vs.resize (vb, 0); }
		double mm = std::max (cx.M[0].length (), std::max (cx.M[1].length (), cx.M[2].length ()));
		cx.Dstep = mm + cx.thA*cx.span*cx.A->rmax + cx.thB*cx.span*cx.B->rmax + cx.E + dmax;

		// zones of this body pair, oriented A -> B (3.3, 3.4)
		cx.zl.clear ();
		for (const CollZone &z : d.m_zone) {
			ZoneAt za;
			if (z.bodyA == ia && z.bodyB == ib) { za.oa = z.ownerA; za.ob = z.ownerB; za.ca = z.ca; za.cb = z.cb; za.ra = z.ra; za.rb = z.rb; }
			else if (z.bodyA == ib && z.bodyB == ia) { za.oa = z.ownerB; za.ob = z.ownerA; za.ca = z.cb; za.cb = z.ca; za.ra = z.rb; za.rb = z.ra; }
			else continue;
			cx.zl.push_back (za);
		}

		// part pairs with part-level culling (2.5): part spheres swept with the body rotation
		Matrix Ra = QMat (ma.Rot (cx.s0)), Rb = QMat (mb.Rot (cx.s0));
		double reach0 = p.deltaCt + std::min (cx.E, COLL_E_CAND);
		const CollBody *as = cx.A->kind != COLLB_BASE && cx.B->kind == COLLB_BASE ? cx.A : cx.B->kind != COLLB_BASE && cx.A->kind == COLLB_BASE ? cx.B : nullptr;
		cx.pp.clear ();
		for (size_t a = 0; a < cx.pa.size (); a++) {
			const Placed &A = cx.pa[a];
			if (!A.g || A.g->node.empty ()) continue;
			for (size_t b = 0; b < cx.pb.size (); b++) {
				const Placed &B = cx.pb[b];
				if (!B.g || B.g->node.empty ()) continue;
				PP q;
				q.a = (int)a; q.b = (int)b;
				const CollOwnerKey &oa = A.r->owner, &ob = B.r->owner;
				q.swap = ob < oa;
				q.key = q.swap ? OwnerPair (ob, oa) : OwnerPair (oa, ob);
				Vector off = mul (Rb, B.pc) - mul (Ra, A.pc), Qp[4];
				for (int i = 0; i < 4; i++) Qp[i] = cx.Q[i] + off;
				double eps = cx.thA*cx.span*A.pc.length () + cx.thB*cx.span*B.pc.length ()
					+ (A.r->interp ? A.r->disp : 0.0) + (B.r->interp ? B.r->disp : 0.0);
				q.culled = !CollCapsuleHit (Qp, A.rp + B.rp + reach0 + eps + A.marg + B.marg);
				q.support = false;
				if (as) {
					const CollSupport *s = as == cx.A ? SupportOf (d, *as, ob) : SupportOf (d, *as, oa);
					if (s) { q.support = true; q.supN = s->n; }
				}
				q.grace = nullptr; q.front = nullptr;
				for (size_t z = 0; z < cx.zl.size (); z++)
					if (cx.zl[z].oa == oa && cx.zl[z].ob == ob) q.zones.push_back ((int)z);
				cx.pp.push_back (q);
			}
		}
		cx.ord.resize (cx.pp.size ());
		for (size_t k = 0; k < cx.ord.size (); k++) cx.ord[k] = (int)k;
		std::stable_sort (cx.ord.begin (), cx.ord.end (), [&cx] (int x, int y) { return cx.pp[x].key < cx.pp[y].key; });
		cx.grp.clear ();
		for (size_t k = 0; k < cx.ord.size (); k++)
			if (!k || !(cx.pp[cx.ord[k]].key == cx.pp[cx.ord[k - 1]].key)) cx.grp.push_back (k);
		cx.grp.push_back (cx.ord.size ());
	}

	// a group has a part pair that is not culled
	static bool Live (const Ctx &cx, size_t g)
	{
		for (size_t k = cx.grp[g]; k < cx.grp[g + 1]; k++) if (!cx.pp[cx.ord[k]].culled) return true;
		return false;
	}

	// distinct parts of group g per side (both owners' parts with geometry in this body pair)
	static void GroupParts (const Ctx &cx, size_t g, std::vector<int> &a, std::vector<int> &b)
	{
		a.clear (); b.clear ();
		for (size_t k = cx.grp[g]; k < cx.grp[g + 1]; k++) { a.push_back (cx.pp[cx.ord[k]].a); b.push_back (cx.pp[cx.ord[k]].b); }
		std::sort (a.begin (), a.end ()); a.erase (std::unique (a.begin (), a.end ()), a.end ());
		std::sort (b.begin (), b.end ()); b.erase (std::unique (b.begin (), b.end ()), b.end ());
	}

	// binds GRACE scopes; with front, also the valid front caches (5.6)
	static void Bind (Ctx &cx, bool front)
	{
		CollDetect &d = *cx.d;
		for (PP &q : cx.pp) {
			CollPairEntry *e = d.m_store.Find (q.key.first, q.key.second);
			q.grace = (e && !e->grace.empty ()) ? &e->grace : nullptr;
			q.front = nullptr;
			if (q.grace) cx.flags |= COLLF_GRACE;
		}
		if (!front || d.m_prm.mFront <= 0.0) return;
		for (size_t g = 0; g + 1 < cx.grp.size (); g++) {
			double mot;
			if (!Live (cx, g) || !FrontValid (cx, g, mot)) continue;  // an owner pair with every part pair culled runs no query
			const OwnerPair &k = cx.pp[cx.ord[cx.grp[g]]].key;
			CollPairEntry *e = d.m_store.Find (k.first, k.second);
			e->frontMotion = mot;
			for (size_t i = cx.grp[g]; i < cx.grp[g + 1]; i++) cx.pp[cx.ord[i]].front = &e->front;
		}
	}

	static CollAffine FrontX (const Ctx &cx, bool sideA, bool refA)
	{
		Matrix RA = QMat (cx.A->m.q0), RB = QMat (cx.B->m.q0);
		Vector r = cx.B->m.c0 - cx.A->m.c0;
		if (sideA == refA) return CollAffine ();
		if (refA) return CollAffine { transp (RA) * RB, tmul (RA, r) };
		return CollAffine { transp (RB) * RA, -tmul (RB, r) };
	}

	static double CornerMove (const CollGeom &g, const CollAffine &X0, const CollAffine &X1)
	{
		const CollNode &n = g.node[0];
		double m = 0.0;
		for (int i = 0; i < 8; i++) {
			Vector x (i & 1 ? n.mx[0] : n.mn[0], i & 2 ? n.mx[1] : n.mn[1], i & 4 ? n.mx[2] : n.mn[2]);
			m = std::max (m, (CollApply (X1, x) - CollApply (X0, x)).length ());
		}
		return m;
	}

	static const FrontPose *FindFront (const CollDetect &d, const OwnerPair &k)
	{
		auto it = std::lower_bound (d.m_front.begin (), d.m_front.end (), k, [] (const FrontPose &f, const OwnerPair &k) {
			if (f.a == k.first) return f.b < k.second;
			return f.a < k.first; });
		return (it != d.m_front.end () && it->a == k.first && it->b == k.second) ? &*it : nullptr;
	}

	static bool FrontValid (const Ctx &cx, size_t g, double &mot)
	{
		const CollDetect &d = *cx.d;
		const PP &q0 = cx.pp[cx.ord[cx.grp[g]]];
		const OwnerPair &key = q0.key;
		const CollPairEntry *e = d.m_store.Find (key.first, key.second);
		if (!e || e->frontCut < 0.0) return false;
		const FrontPose *fp = FindFront (d, key);
		if (!fp) return false;
		bool refA = !q0.swap;
		CollAffine Ta = FrontX (cx, true, refA), Tb = FrontX (cx, false, refA);
		double ma = 0.0, mb = 0.0;
		size_t found = 0;
		std::vector<int> ia, ib;
		GroupParts (cx, g, ia, ib);
		for (int side = 0; side < 2; side++) {
			for (int pi : side ? ib : ia) {
				const Placed &p = side ? cx.pb[pi] : cx.pa[pi];
				const FrontPart *f = nullptr;
				for (const FrontPart &x : fp->part)
					if (x.owner == p.r->owner && x.partKey == p.r->partKey) { f = &x; break; }
				if (!f || f->version != p.r->version || f->geom != p.g || !p.g || f->nnode != p.g->node.size ()) return false;
				found++;
				CollAffine X = CollCompose (side ? Tb : Ta, p.r->P0);
				double m = CornerMove (*p.g, f->X, X);
				if (p.r->owner == key.first) ma = std::max (ma, m); else mb = std::max (mb, m);
			}
		}
		if (found != fp->part.size ()) return false;
		mot = ma + mb;
		return mot + cx.Dstep < d.m_prm.mFront;
	}

	static void FrontStore (Ctx &cx, size_t g, std::vector<CollLeafPair> &lp)
	{
		CollDetect &d = *cx.d;
		const PP &q0 = cx.pp[cx.ord[cx.grp[g]]];
		const OwnerPair &key = q0.key;
		if (lp.empty ()) {
			CollPairEntry *e = d.m_store.Find (key.first, key.second);
			if (e) { e->front.clear (); e->frontCut = -1.0; e->frontMotion = 0.0; }
			return;
		}
		std::sort (lp.begin (), lp.end ());
		lp.erase (std::unique (lp.begin (), lp.end ()), lp.end ());
		CollPairEntry &e = d.m_store.Get (key.first, key.second);
		e.front = lp; e.frontCut = d.m_prm.deltaCt; e.frontMotion = 0.0;
		FrontPose fp;
		fp.a = key.first; fp.b = key.second;
		bool refA = !q0.swap;
		CollAffine Ta = FrontX (cx, true, refA), Tb = FrontX (cx, false, refA);
		std::vector<int> ia, ib;
		GroupParts (cx, g, ia, ib);
		for (int side = 0; side < 2; side++) {
			for (int pi : side ? ib : ia) {
				const Placed &p = side ? cx.pb[pi] : cx.pa[pi];
				FrontPart f;
				f.owner = p.r->owner; f.partKey = p.r->partKey; f.version = p.r->version;
				f.nnode = (uint32_t)p.g->node.size (); f.geom = p.g;
				f.X = CollCompose (side ? Tb : Ta, p.r->P0);
				fp.part.push_back (f);
			}
		}
		auto it = std::lower_bound (d.m_front.begin (), d.m_front.end (), key, [] (const FrontPose &f, const OwnerPair &k) {
			if (f.a == k.first) return f.b < k.second;
			return f.a < k.first; });
		if (it != d.m_front.end () && it->a == key.first && it->b == key.second) *it = fp;
		else d.m_front.insert (it, fp);
	}

	// poses

	static Vector Bez (const Vector Q[4], double s)
	{
		double u = 1.0 - s;
		return Q[0]*(u*u*u) + Q[1]*(3.0*u*u*s) + Q[2]*(3.0*u*s*s) + Q[3]*(s*s*s);
	}

	static void Stamp (CollDetect &d)
	{
		if (++d.m_stamp == 0) { std::fill (d.m_vs.begin (), d.m_vs.end (), 0u); d.m_stamp = 1; }
	}

	static CollAffine PartPose (const CollPartRef &r, int mode, double tau)
	{
		if (mode == POSE_T0) return r.P0;
		if (mode == POSE_T1 || !r.interp) return r.P1;
		return CollPoseAt (r.P0, r.P1, r.c, tau);
	}

	static void SetPose (Ctx &cx, int mode, double tau)
	{
		const CollMotion &ma = cx.A->m, &mb = cx.B->m;
		const Vector &org = cx.d->m_org[cx.ia];
		if (mode == POSE_T0) {
			cx.RA = QMat (ma.q0); cx.RB = QMat (mb.q0); cx.r = mb.c0 - ma.c0; cx.cA = ma.c0 - org; tau = ma.ta;
		} else if (mode == POSE_T1) {
			cx.RA = QMat (ma.q1); cx.RB = QMat (mb.q1); cx.r = mb.c1 - ma.c1; cx.cA = ma.c1 - org; tau = 1.0;
		} else {
			cx.RA = QMat (ma.Rot (tau)); cx.RB = QMat (mb.Rot (tau));
			cx.r = Bez (cx.Q, (tau - cx.s0)/cx.span);
			cx.cA = (ma.c0 - org) + Off (ma, tau);
		}
		cx.mode = mode; cx.tau = tau;
		for (Placed &p : cx.pa) {
			CollAffine P = PartPose (*p.r, mode, tau);
			p.X = CollAffine { cx.RA * P.A, mul (cx.RA, P.t) };
		}
		for (Placed &p : cx.pb) {
			CollAffine P = PartPose (*p.r, mode, tau);
			p.X = CollAffine { cx.RB * P.A, mul (cx.RB, P.t) + cx.r };
		}
		for (ZoneAt &z : cx.zl) { z.ga = mul (cx.RA, z.ca); z.gb = cx.r + mul (cx.RB, z.cb); }
		Stamp (*cx.d);
	}

	static Vector Vtx (CollDetect &d, const Placed &p, uint32_t v)
	{
		size_t s = p.vbase + v;
		if (d.m_vs[s] != d.m_stamp) { d.m_vx[s] = CollApply (p.X, p.g->vtx[v]); d.m_vs[s] = d.m_stamp; }
		return d.m_vx[s];
	}

	static void TriAt (CollDetect &d, const Placed &p, uint32_t t, Vector v[3])
	{
		const CollTri &T = p.g->tri[t];
		for (int i = 0; i < 3; i++) v[i] = Vtx (d, p, T.v[i]);
	}

	// bounds (5.2)

	static void MakeBox (const Placed &p, const CollNode &n, Box &b)
	{
		b.cl = Vector ((n.mn[0]+n.mx[0])*0.5, (n.mn[1]+n.mx[1])*0.5, (n.mn[2]+n.mx[2])*0.5);
		b.e = Vector ((n.mx[0]-n.mn[0])*0.5, (n.mx[1]-n.mn[1])*0.5, (n.mx[2]-n.mn[2])*0.5);
		b.g = CollApply (p.X, b.cl);
		b.L = p.X.A;
		b.orth = p.orth;
	}

	static double HalfW (const Box &b, const Vector &u)
	{
		Vector t = tmul (b.L, u);
		return fabs (t.x)*b.e.x + fabs (t.y)*b.e.y + fabs (t.z)*b.e.z;
	}

	static double BoxRad (const Box &b)
	{
		if (b.orth) return b.e.length ();
		return Col (b.L, 0).length ()*b.e.x + Col (b.L, 1).length ()*b.e.y + Col (b.L, 2).length ()*b.e.z;
	}

	static int Axes (const Box &a, const Box &b, Vector ax[7])
	{
		int n = 0;
		Vector d = b.g - a.g;
		double l = d.length ();
		if (l > 0.0) ax[n++] = d/l;
		if (a.orth) for (int k = 0; k < 3; k++) ax[n++] = Col (a.L, k);
		else { ax[n++] = Vector (1, 0, 0); ax[n++] = Vector (0, 1, 0); ax[n++] = Vector (0, 0, 1); }
		if (b.orth) for (int k = 0; k < 3; k++) ax[n++] = Col (b.L, k);
		else if (a.orth) { ax[n++] = Vector (1, 0, 0); ax[n++] = Vector (0, 1, 0); ax[n++] = Vector (0, 0, 1); }
		return n;
	}

	static double Sep (const Box &a, const Box &b, const Vector &u)
	{
		return fabs ((b.g - a.g) & u) - HalfW (a, u) - HalfW (b, u);
	}

	static double LowerBound (const Box &a, const Box &b)
	{
		Vector ax[7];
		int n = Axes (a, b, ax);
		double s = -1e300;
		for (int k = 0; k < n; k++) s = std::max (s, Sep (a, b, ax[k]));
		return s;
	}

	// node distance from the body CG over the step (rho) and part motion per unit tau (mu)
	static void NodeMotion (const Placed &p, const CollNode &n, double &rho, double &mu)
	{
		Vector c ((n.mn[0]+n.mx[0])*0.5, (n.mn[1]+n.mx[1])*0.5, (n.mn[2]+n.mx[2])*0.5);
		Vector e ((n.mx[0]-n.mn[0])*0.5, (n.mx[1]-n.mn[1])*0.5, (n.mx[2]-n.mn[2])*0.5);
		double er = p.r->rigid ? e.length () : Col (p.Pb.A, 0).length ()*e.x + Col (p.Pb.A, 1).length ()*e.y + Col (p.Pb.A, 2).length ()*e.z;
		rho = CollApply (p.Pb, c).length () + er;
		mu = 0.0;
		if (!p.r->interp) return;
		if (p.rigidPath) mu = (p.thPart + 1e-8)*((c - p.r->c).length () + e.length ()) + p.dc.length (); // 1e-8: CollPoseAt's near-rigid residual term
		else {
			for (int i = 0; i < 8; i++) {
				Vector x (c.x + (i & 1 ? e.x : -e.x), c.y + (i & 2 ? e.y : -e.y), c.z + (i & 4 ? e.z : -e.z));
				mu = std::max (mu, (mul (p.dA, x) + p.dt).length ());
			}
		}
		rho += mu;
	}

	static double Speed (const Ctx &cx, const Vector &u, double rhoA, double muA, double rhoB, double muB)
	{
		double m = std::max (fabs (cx.M[0] & u), std::max (fabs (cx.M[1] & u), fabs (cx.M[2] & u)))/cx.span;
		return m + cx.thA*rhoA + cx.thB*rhoB + muA + muB;
	}

	// filters

	static bool ZoneDrop (const Ctx &cx, const PP &q, const Vector &wa, const Vector &wb)
	{
		for (int z : q.zones) {
			const ZoneAt &Z = cx.zl[z];
			if ((wa - Z.ga).length () <= Z.ra && (wb - Z.gb).length () <= Z.rb) return true;
		}
		return false;
	}

	static bool ZonePrune (const Ctx &cx, const PP &q, const Box &a, const Box &b)
	{
		for (int z : q.zones) {
			const ZoneAt &Z = cx.zl[z];
			if ((a.g - Z.ga).length () + BoxRad (a) <= Z.ra && (b.g - Z.gb).length () + BoxRad (b) <= Z.rb) return true;
		}
		return false;
	}

	static CollLeafPair LeafKey (const Ctx &cx, const PP &q, uint32_t la, uint32_t lb)
	{
		const CollPartRef &A = *cx.pa[q.a].r, &B = *cx.pb[q.b].r;
		if (q.swap) return CollLeafPair { B.partKey, B.version, lb, A.partKey, A.version, la };
		return CollLeafPair { A.partKey, A.version, la, B.partKey, B.version, lb };
	}

	static bool Scoped (const Ctx &cx, const PP &q, uint32_t la, uint32_t lb)
	{
		if (!q.grace) return false;
		return std::binary_search (q.grace->begin (), q.grace->end (), LeafKey (cx, q, la, lb));
	}

	// cached leaf pairs of part pair q, in this pair's orientation
	static void FrontLeaves (const Ctx &cx, const PP &q, std::vector<std::pair<uint32_t, uint32_t>> &out)
	{
		out.clear ();
		const CollPartRef &A = *cx.pa[q.a].r, &B = *cx.pb[q.b].r;
		for (const CollLeafPair &l : *q.front) {
			if (q.swap) {
				if (l.partKeyA == B.partKey && l.verA == B.version && l.partKeyB == A.partKey && l.verB == A.version) out.push_back ({ l.leafB, l.leafA });
			} else {
				if (l.partKeyA == A.partKey && l.verA == A.version && l.partKeyB == B.partKey && l.verB == B.version) out.push_back ({ l.leafA, l.leafB });
			}
		}
	}

	static bool IsLeaf (const CollGeom &g, uint32_t n)
	{
		return n < g.node.size () && g.node[n].count > 0;
	}

	// conservative advancement (5.2, 5.3)

	// one traversal at the current pose: 1 hit, 0 no hit (best = safe step), -1 triangle cap reached
	static int CaPass (Ctx &cx, bool core, double &best, std::vector<std::pair<double, double>> &gs, Raw &hit, long long ttCap)
	{
		CollDetect &d = *cx.d;
		const CollParams &p = d.m_prm;
		std::vector<std::pair<uint32_t, uint32_t>> fl;
		for (size_t k = 0; k < cx.pp.size (); k++) {
			const PP &q = cx.pp[k];
			if (q.culled) continue;
			const Placed &a = cx.pa[q.a], &b = cx.pb[q.b];
			double rs = a.r->skin + b.r->skin;
			double mg = core ? std::min (cx.Em, 0.5*(rs - p.kappaCore)) + a.marg + b.marg : rs + cx.Em + a.marg + b.marg;
			double tg = core ? p.kappaCore : p.deltaToi;
			double gm = rs + cx.Em + a.marg + b.marg;
			bool cache = core && q.front;
			if (cache) FrontLeaves (cx, q, fl);
			size_t fi = 0;
			cx.stk.clear ();
			if (!cache) cx.stk.push_back ({ 0u, 0u });
			for (;;) {
				uint32_t i, j;
				if (cache) {
					if (fi >= fl.size ()) break;
					i = fl[fi].first; j = fl[fi].second; fi++;
					if (!IsLeaf (*a.g, i) || !IsLeaf (*b.g, j)) continue;
				} else {
					if (cx.stk.empty ()) break;
					i = cx.stk.back ().first; j = cx.stk.back ().second; cx.stk.pop_back ();
				}
				cx.bv++;
				const CollNode &na = a.g->node[i], &nb = b.g->node[j];
				Box Ba, Bb;
				MakeBox (a, na, Ba); MakeBox (b, nb, Bb);
				double rhoA, muA, rhoB, muB;
				NodeMotion (a, na, rhoA, muA); NodeMotion (b, nb, rhoB, muB);
				Vector ax[7];
				int n = Axes (Ba, Bb, ax);
				double dtMax = -1.0, gB = 0.0, sB = 0.0;
				for (int t = 0; t < n; t++) {
					double s = Sep (Ba, Bb, ax[t]);
					double g = s - mg - 0.5*tg;
					if (g <= 0.0) continue;
					double S = Speed (cx, ax[t], rhoA, muA, rhoB, muB);
					double dt = S > 0.0 ? g/S : 1e300;
					if (dt > dtMax) { dtMax = dt; gB = s - gm; sB = S; }
				}
				if (dtMax >= best) { gs.push_back ({ gB, sB }); continue; }
				if (!q.zones.empty () && ZonePrune (cx, q, Ba, Bb)) { cx.flags |= COLLF_ZONE; continue; }
				bool lA = na.count > 0, lB = nb.count > 0;
				if (lA && lB) {
					if (Scoped (cx, q, i, j)) { cx.flags |= COLLF_GRACE; continue; }
					for (uint32_t x = na.first; x < na.first + na.count; x++) {
						uint32_t ta = a.g->perm[x];
						if (a.r->mask && a.r->mask[a.g->tri[ta].src]) continue;
						Vector va[3];
						TriAt (d, a, ta, va);
						for (uint32_t y = nb.first; y < nb.first + nb.count; y++) {
							uint32_t tb = b.g->perm[y];
							if (b.r->mask && b.r->mask[b.g->tri[tb].src]) continue;
							Vector vb[3], pa, pb;
							TriAt (d, b, tb, vb);
							cx.tt++;
							double dd = CollTriTriDistance (va, vb, pa, pb);
							if (!q.zones.empty () && ZoneDrop (cx, q, pa, pb)) { cx.flags |= COLLF_ZONE; continue; }
							if (dd - mg <= tg) { hit = Raw { (int)k, ta, tb, i, j, dd, pa, pb }; return 1; }
							Vector u = (pb - pa)/dd;
							double S = Speed (cx, u, rhoA, muA, rhoB, muB);
							double dt = S > 0.0 ? (dd - mg - 0.5*tg)/S : 1e300;
							gs.push_back ({ dd - gm, S });
							if (dt < best) best = dt;
						}
					}
					if (cx.tt > ttCap) return -1;
					continue;
				}
				bool splitA = lB || (!lA && Ba.e.length2 () >= Bb.e.length2 ());
				if (splitA) {
					uint32_t c1 = na.first, c2 = na.first + 1;
					double d1 = (CollApply (a.X, NodeCentre (a.g->node[c1])) - Bb.g).length2 ();
					double d2 = (CollApply (a.X, NodeCentre (a.g->node[c2])) - Bb.g).length2 ();
					if (d1 <= d2) { cx.stk.push_back ({ c2, j }); cx.stk.push_back ({ c1, j }); }
					else { cx.stk.push_back ({ c1, j }); cx.stk.push_back ({ c2, j }); }
				} else {
					uint32_t c1 = nb.first, c2 = nb.first + 1;
					double d1 = (CollApply (b.X, NodeCentre (b.g->node[c1])) - Ba.g).length2 ();
					double d2 = (CollApply (b.X, NodeCentre (b.g->node[c2])) - Ba.g).length2 ();
					if (d1 <= d2) { cx.stk.push_back ({ i, c2 }); cx.stk.push_back ({ i, c1 }); }
					else { cx.stk.push_back ({ i, c1 }); cx.stk.push_back ({ i, c2 }); }
				}
			}
		}
		return 0;
	}

	static Vector NodeCentre (const CollNode &n)
	{
		return Vector ((n.mn[0]+n.mx[0])*0.5, (n.mn[1]+n.mx[1])*0.5, (n.mn[2]+n.mx[2])*0.5);
	}

	// CA from tau0 (5.3, 5.4); skin target, or core target on raw distance (5.7)
	static CollKind RunCa (Ctx &cx, double tau0, bool core, double &tau, double &spec, Raw &hit, int &iters)
	{
		const CollParams &p = cx.d->m_prm;
		std::vector<std::pair<double, double>> gs;
		tau = tau0; spec = 0.0; iters = 0;
		double guar = 0.0;
		long long tt0 = cx.tt;
		for (;;) {
			iters++;
			SetPose (cx, POSE_MODEL, tau);
			double best = 1.0 - tau;
			gs.clear ();
			int res = CaPass (cx, core, best, gs, hit, tt0 + p.nTt);
			if (res > 0) return COLL_TOI;
			if (res < 0) { spec = guar; return COLL_SPECULATIVE; }
			double g = 1e300;
			for (const auto &x : gs) g = std::min (g, x.first - x.second*best);
			guar = std::max (0.0, g);
			tau += best;
			if (tau >= 1.0) { tau = 1.0; return COLL_NONE; }
			if (iters >= p.nCa || cx.tt - tt0 > p.nTt) { spec = guar; return COLL_SPECULATIVE; }
		}
	}

	// cutoff query (5.6)

	// tri pairs of part pair k closer than rs + extra (raw: extra; RAWX: crossing); false at maxOut
	static bool Cutoff (Ctx &cx, size_t k, double extra, int opt, std::vector<Raw> &out, size_t maxOut, std::vector<CollLeafPair> *build)
	{
		CollDetect &d = *cx.d;
		const PP &q = cx.pp[k];
		const Placed &a = cx.pa[q.a], &b = cx.pb[q.b];
		double cut = (opt & (Q_RAW | Q_RAWX)) ? extra : a.r->skin + b.r->skin + extra;
		double ncut = build ? cut + d.m_prm.mFront : cut;
		bool cache = (opt & Q_FRONT) && q.front && !build;
		std::vector<std::pair<uint32_t, uint32_t>> fl;
		if (cache) FrontLeaves (cx, q, fl);
		size_t fi = 0;
		cx.stk.clear ();
		if (!cache) cx.stk.push_back ({ 0u, 0u });
		for (;;) {
			uint32_t i, j;
			if (cache) {
				if (fi >= fl.size ()) break;
				i = fl[fi].first; j = fl[fi].second; fi++;
				if (!IsLeaf (*a.g, i) || !IsLeaf (*b.g, j)) continue;
			} else {
				if (cx.stk.empty ()) break;
				i = cx.stk.back ().first; j = cx.stk.back ().second; cx.stk.pop_back ();
			}
			cx.bv++;
			const CollNode &na = a.g->node[i], &nb = b.g->node[j];
			Box Ba, Bb;
			MakeBox (a, na, Ba); MakeBox (b, nb, Bb);
			double lb = LowerBound (Ba, Bb);
			if ((opt & Q_RAWX) ? lb > 0.0 : lb >= ncut) continue;
			bool zones = (opt & Q_ZONES) && !q.zones.empty ();
			if (zones && !build && ZonePrune (cx, q, Ba, Bb)) { cx.flags |= COLLF_ZONE; continue; }
			bool lA = na.count > 0, lB = nb.count > 0;
			if (lA && lB) {
				if (build) build->push_back (LeafKey (cx, q, i, j)); // a front holds every leaf pair within its cut, zones or not (it outlives them)
				if (!(opt & Q_RAWX) && lb >= cut) continue;
				if (zones && build && ZonePrune (cx, q, Ba, Bb)) { cx.flags |= COLLF_ZONE; continue; }
				if ((opt & Q_SCOPES) && Scoped (cx, q, i, j)) { cx.flags |= COLLF_GRACE; continue; }
				for (uint32_t x = na.first; x < na.first + na.count; x++) {
					uint32_t ta = a.g->perm[x];
					if (a.r->mask && a.r->mask[a.g->tri[ta].src]) continue;
					Vector va[3];
					TriAt (d, a, ta, va);
					for (uint32_t y = nb.first; y < nb.first + nb.count; y++) {
						uint32_t tb = b.g->perm[y];
						if (b.r->mask && b.r->mask[b.g->tri[tb].src]) continue;
						Vector vb[3], pa, pb;
						TriAt (d, b, tb, vb);
						cx.tt++;
						double dd = CollTriTriDistance (va, vb, pa, pb);
						if ((opt & Q_RAWX) ? dd > 0.0 : dd >= cut) continue;
						if ((opt & Q_ZONES) && !q.zones.empty () && ZoneDrop (cx, q, pa, pb)) { cx.flags |= COLLF_ZONE; continue; }
						out.push_back (Raw { (int)k, ta, tb, i, j, dd, pa, pb });
						if (opt & Q_FIRST) return true;
						if (out.size () >= maxOut) return false;
					}
				}
				continue;
			}
			bool splitA = lB || (!lA && Ba.e.length2 () >= Bb.e.length2 ());
			if (splitA) { cx.stk.push_back ({ na.first + 1, j }); cx.stk.push_back ({ na.first, j }); }
			else { cx.stk.push_back ({ i, nb.first + 1 }); cx.stk.push_back ({ i, nb.first }); }
		}
		return true;
	}

	// manifold (5.5)

	// closest point of p on triangle abc; face: it lies inside the face (a true vertex-face feature)
	static Vector ClosestOnTri (const Vector &p, const Vector &a, const Vector &b, const Vector &c, bool &face)
	{
		face = false;
		Vector ab = b - a, ac = c - a, ap = p - a;
		double d1 = ab & ap, d2 = ac & ap;
		if (d1 <= 0.0 && d2 <= 0.0) return a;
		Vector bp = p - b;
		double d3 = ab & bp, d4 = ac & bp;
		if (d3 >= 0.0 && d4 <= d3) return b;
		double vc = d1*d4 - d3*d2;
		if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab*(d1/(d1 - d3));
		Vector cp = p - c;
		double d5 = ab & cp, d6 = ac & cp;
		if (d6 >= 0.0 && d5 <= d6) return c;
		double vb = d5*d2 - d1*d6;
		if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac*(d2/(d2 - d6));
		double va = d3*d6 - d5*d4;
		if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) return b + (c - b)*((d4 - d3)/((d4 - d3) + (d5 - d6)));
		double s = va + vb + vc;
		if (!(fabs (s) > 0.0)) return a;
		double den = 1.0/s;
		face = true;
		return a + ab*(vb*den) + ac*(vc*den);
	}

	struct BodyV { Vector v, w; };

	static void PoseVel (const Ctx &cx, BodyV &va, BodyV &vb)
	{
		const CollMotion &ma = cx.A->m, &mb = cx.B->m;
		if (cx.mode == POSE_T0) { va.v = ma.v0; va.w = ma.Omega (ma.ta); vb.v = mb.v0; vb.w = mb.Omega (mb.ta); }
		else if (cx.mode == POSE_T1) { va.v = ma.v1; va.w = ma.Omega (ma.tb); vb.v = mb.v1; vb.w = mb.Omega (mb.tb); }
		else { va.v = ma.Vel (cx.tau); va.w = ma.Omega (cx.tau); vb.v = mb.Vel (cx.tau); vb.w = mb.Omega (cx.tau); }
	}

	// surface velocity of a query-frame point on a placed part at the current pose (1.5)
	static Vector SurfV (const Ctx &cx, const Placed &p, bool sideB, const Vector &x)
	{
		const CollPartRef &r = *p.r;
		double h = cx.d->m_h;
		if (h <= 0.0) return Vector ();
		Vector xl = CollApply (CollInverse (p.X), x);
		Vector v = CollPoseVel (r.P0, r.P1, r.c, cx.tau, xl);
		if (v.length2 () == 0.0) return Vector ();
		return mul (sideB ? cx.RB : cx.RA, v)/h;
	}

	static void TouchKey (const Ctx &cx, const PP &q, uint32_t &ka, uint32_t &kb)
	{
		ka = cx.pa[q.a].r->partKey; kb = cx.pb[q.b].r->partKey;
		if (q.swap) std::swap (ka, kb);
	}

	static const CollTouch *FindTouch (const CollPairEntry &e, uint32_t ka, uint32_t kb)
	{
		auto it = std::lower_bound (e.touch.begin (), e.touch.end (), std::make_pair (ka, kb), [] (const CollTouch &t, const std::pair<uint32_t, uint32_t> &k) {
			return t.partKeyA != k.first ? t.partKeyA < k.first : t.partKeyB < k.second; });
		return (it != e.touch.end () && it->partKeyA == ka && it->partKeyB == kb) ? &*it : nullptr;
	}

	static bool IsFirst (const Ctx &cx, const PP &q)
	{
		const CollPairEntry *e = cx.d->m_store.Find (q.key.first, q.key.second);
		if (!e) return true;
		uint32_t ka, kb;
		TouchKey (cx, q, ka, kb);
		const CollTouch *t = FindTouch (*e, ka, kb);
		return !t || t->state == COLLT_APART;
	}

	static bool RawLess (const Raw &x, const Raw &y)
	{
		if (x.k != y.k) return x.k < y.k;
		if (x.ta != y.ta) return x.ta < y.ta;
		return x.tb < y.tb;
	}

	// feature key: a witness at a mesh vertex keys by its welded vertex id, else by the triangle pair
	static void FeatureKey (Cand &y, const Vector ta[3], const Vector tb[3], const CollTri &TA, const CollTri &TB)
	{
		auto at = [] (const Vector &p, const Vector &v) { double e = 1e-9*(1.0 + fabs (v.x) + fabs (v.y) + fabs (v.z)); return (p - v).length2 () <= e*e; };
		y.kc = 2; y.k1 = y.ta; y.k2 = y.tb;
		for (int j = 0; j < 3; j++) if (at (y.pA, ta[j])) { y.kc = 0; y.k1 = TA.v[j]; y.k2 = 0; return; }
		for (int j = 0; j < 3; j++) if (at (y.pB, tb[j])) { y.kc = 1; y.k1 = TB.v[j]; y.k2 = 0; return; }
	}

	static bool KeyLess (const Cand &x, const Cand &y)
	{
		if (x.k != y.k) return x.k < y.k;
		if (x.kc != y.kc) return x.kc < y.kc;
		if (x.k1 != y.k1) return x.k1 < y.k1;
		return x.k2 < y.k2;
	}

	// contact points at the current pose from raw pairs; cut: rs + extra (+ held margins); count
	static int Manifold (Ctx &cx, std::vector<Raw> &raws, double extra, bool margins, CollContact *pt, uint32_t &rflags)
	{
		CollDetect &d = *cx.d;
		std::sort (raws.begin (), raws.end (), RawLess);
		raws.erase (std::unique (raws.begin (), raws.end (), [] (const Raw &x, const Raw &y) { return x.k == y.k && x.ta == y.ta && x.tb == y.tb; }), raws.end ());
		BodyV va, vb;
		PoseVel (cx, va, vb);
		std::vector<Cand> c;
		std::vector<std::pair<Vector, double>> vmin[2];             // per side: vertex, its smallest distance to a paired triangle
		for (const Raw &w : raws) {
			const PP &q = cx.pp[w.k];
			const Placed &a = cx.pa[q.a], &b = cx.pb[q.b];
			double rs = a.r->skin + b.r->skin;
			double cut = rs + extra + (margins ? a.marg + b.marg : 0.0);
			Vector ta[3], tb[3];
			TriAt (d, a, w.ta, ta); TriAt (d, b, w.tb, tb);
			Cand x;
			x.k = w.k; x.ta = w.ta; x.tb = w.tb; x.patch = 0;
			x.flags = w.d <= 0.0 ? COLLP_INTERSECT : 0;
			int n0 = (int)c.size ();
			x.pA = w.pa; x.pB = w.pb; x.d = w.d;
			c.push_back (x);
			// vertex-face features of the pair within the cut (face contacts get their corners)
			for (int i = 0; i < 3; i++) {
				bool f;
				Vector qb = ClosestOnTri (ta[i], tb[0], tb[1], tb[2], f);
				double dd = (ta[i] - qb).length ();
				vmin[0].push_back ({ ta[i], dd });
				if (w.d > 0.0 && f && dd < cut) { x.pA = ta[i]; x.pB = qb; x.d = dd; c.push_back (x); }
				Vector qa = ClosestOnTri (tb[i], ta[0], ta[1], ta[2], f);
				dd = (tb[i] - qa).length ();
				vmin[1].push_back ({ tb[i], dd });
				if (w.d > 0.0 && f && dd < cut) { x.pA = qa; x.pB = tb[i]; x.d = dd; c.push_back (x); }
			}
			const CollTri &TA = a.g->tri[w.ta], &TB = b.g->tri[w.tb];
			for (size_t i = (size_t)n0; i < c.size (); i++) {
				Cand &y = c[i];
				FeatureKey (y, ta, tb, TA, TB);
				y.gap = y.d - rs;
				if (y.d > COLL_DEGEN_DIST) { y.n = (y.pA - y.pB)/y.d; continue; }
				y.flags |= COLLP_DEGENERATE;
				Vector p = (y.pA + y.pB)*0.5;
				bool done = false;
				if (q.support && q.supN.length () > 0.0) {
					bool baseA = cx.A->kind == COLLB_BASE;
					Vector ng = mul (baseA ? cx.RA : cx.RB, q.supN);
					double l = ng.length ();
					if (l > 0.0) { y.n = (baseA ? -ng : ng)/l; done = true; }
				}
				if (!done) {                                       // crossing faces: minimum-penetration face normal, A pushed along it clears B (5.5, fix1)
					Vector nf[2] = { crossp (ta[1] - ta[0], ta[2] - ta[0]), crossp (tb[1] - tb[0], tb[2] - tb[0]) };
					double best = 1e300;
					for (const Vector &f : nf) {
						double l = f.length ();
						if (!(l > 0.0)) continue;
						for (double sg : { 1.0, -1.0 }) {
							Vector u = f*(sg/l);
							double amin = 1e300, bmax = -1e300;
							for (int j = 0; j < 3; j++) { amin = std::min (amin, ta[j] & u); bmax = std::max (bmax, tb[j] & u); }
							double dep = bmax - amin;
							if (dep < best || (dep == best && (u & -cx.r) > (y.n & -cx.r))) { best = dep; y.n = u; done = true; }
						}
					}
				}
				if (!done) {                                       // both faces degenerate: relative velocity
					Vector pva = va.v + Xc (va.w, p) + SurfV (cx, a, false, y.pA);
					Vector pvb = vb.v + Xc (vb.w, p - cx.r) + SurfV (cx, b, true, y.pB);
					Vector rv = pva - pvb;
					double l = rv.length ();
					if (l >= COLL_DEGEN_SPEED) { y.n = -rv/l; done = true; }
				}
				if (!done) {
					double l = cx.r.length ();
					y.n = l > 0.0 ? -cx.r/l : Vector (0, 0, 1);
				}
			}
		}
		// a witness closer to another feature (paired triangle, other candidate) is not a closest pair
		std::vector<uint8_t> drop (c.size (), 0);
		for (int side = 0; side < 2; side++) {
			std::vector<std::pair<Vector, double>> &vm = vmin[side];
			for (const Cand &x : c) vm.push_back ({ side ? x.pB : x.pA, x.d });
			std::stable_sort (vm.begin (), vm.end (), [] (const std::pair<Vector, double> &x, const std::pair<Vector, double> &y) { return x.first.x < y.first.x; });
			for (size_t i = 0; i < c.size (); i++) {
				const Vector &p = side ? c[i].pB : c[i].pA;
				double eps = 1e-9*(1.0 + fabs (p.x) + fabs (p.y) + fabs (p.z)), dmin = 1e300;
				auto it = std::lower_bound (vm.begin (), vm.end (), p.x - eps, [] (const std::pair<Vector, double> &x, double v) { return x.first.x < v; });
				for (; it != vm.end () && it->first.x <= p.x + eps; ++it)
					if ((it->first - p).length () <= eps) dmin = std::min (dmin, it->second);
				if (c[i].d > dmin + 1e-6) drop[i] = 1;
			}
		}
		// merge duplicates: keep the deepest; within rounding the smallest feature key, so it does not flip
		double cm = cos (COLL_MERGE_ANGLE*_RAD_), cp = cos (COLL_PATCH_ANGLE*_RAD_);
		std::vector<Cand> m;
		for (size_t ci = 0; ci < c.size (); ci++) {
			if (drop[ci]) continue;
			const Cand &x = c[ci];
			bool merged = false;
			Vector sx = (x.pA + x.pB)*0.5;
			for (Cand &y : m) {
				if (((y.pA + y.pB)*0.5 - sx).length () <= COLL_MERGE_DIST && (x.n & y.n) >= cm) {
					if (x.gap < y.gap - MERGE_GAP_TOL || (x.gap <= y.gap + MERGE_GAP_TOL && KeyLess (x, y))) y = x;
					merged = true;
					break;
				}
			}
			if (!merged) m.push_back (x);
		}
		// patches by normal, seeded by the deepest point
		std::vector<int> ord (m.size ());
		for (size_t i = 0; i < m.size (); i++) ord[i] = (int)i;
		std::stable_sort (ord.begin (), ord.end (), [&m] (int x, int y) { return m[x].gap < m[y].gap; });
		std::vector<int> pid (m.size (), -1);
		int np = 0;
		for (int s : ord) {
			if (pid[s] >= 0) continue;
			if (np == COLL_MAX_PATCHES) break;
			Vector n = m[s].n;
			for (int t : ord) if (pid[t] < 0 && (m[t].n & n) >= cp) pid[t] = np;
			np++;
		}
		std::vector<std::pair<int, int>> keep;                      // selected candidate, its patch
		for (int pi = 0; pi < np; pi++) {
			std::vector<int> v;
			for (int t : ord) if (pid[t] == pi) v.push_back (t);
			std::vector<int> sel;
			if (v.size () <= (size_t)COLL_PATCH_POINTS) { for (int t : v) sel.push_back (t); }
			else {
				auto S = [&m] (int t) { return (m[t].pA + m[t].pB)*0.5; };
				int p1 = v[0], p2 = -1, p3 = -1, p4 = -1;
				double best = -1.0;
				for (int t : v) if (t != p1) { double x = (S (t) - S (p1)).length2 (); if (x > best) { best = x; p2 = t; } }
				best = -1.0;
				for (int t : v) if (t != p1 && t != p2) { double x = crossp (S (p2) - S (p1), S (t) - S (p1)).length2 (); if (x > best) { best = x; p3 = t; } }
				Vector N = crossp (S (p2) - S (p1), S (p3) - S (p1));
				double ln = N.length ();
				if (ln > 0.0) N = N/ln;
				best = 0.0;
				for (int t : v) {
					if (t == p1 || t == p2 || t == p3) continue;
					Vector x = S (t);
					double a12 = crossp (S (p2) - S (p1), x - S (p1)) & N;
					double a23 = crossp (S (p3) - S (p2), x - S (p2)) & N;
					double a31 = crossp (S (p1) - S (p3), x - S (p3)) & N;
					double inc = std::max (0.0, -a12) + std::max (0.0, -a23) + std::max (0.0, -a31);
					if (inc > best) { best = inc; p4 = t; }
				}
				sel.push_back (p1); sel.push_back (p2); sel.push_back (p3);
				if (p4 >= 0) sel.push_back (p4);
			}
			for (int t : sel) keep.push_back ({ t, pi });
		}
		// deepest first; within ORDER_GAP_TOL of the group's deepest, feature-key order (stable at rest)
		std::stable_sort (keep.begin (), keep.end (), [&m] (const std::pair<int, int> &x, const std::pair<int, int> &y) { return m[x.first].gap < m[y.first].gap; });
		for (size_t i = 0; i < keep.size ();) {
			size_t j = i + 1;
			while (j < keep.size () && m[keep[j].first].gap - m[keep[i].first].gap < ORDER_GAP_TOL) j++;
			std::stable_sort (keep.begin () + i, keep.begin () + j, [&m] (const std::pair<int, int> &x, const std::pair<int, int> &y) { return KeyLess (m[x.first], m[y.first]); });
			i = j;
		}
		int npt = 0;
		for (const std::pair<int, int> &s : keep) {
			const Cand &x = m[s.first];
			const PP &q = cx.pp[x.k];
			const Placed &a = cx.pa[q.a], &b = cx.pb[q.b];
			CollContact &o = pt[npt++];
			o.pA = x.pA + cx.cA; o.pB = x.pB + cx.cA; o.n = x.n; o.gap = x.gap;
			o.vsA = SurfV (cx, a, false, x.pA); o.vsB = SurfV (cx, b, true, x.pB);
			o.triA = x.ta; o.triB = x.tb;
			o.partA = (uint16_t)a.idx; o.partB = (uint16_t)b.idx;
			o.patch = (uint8_t)s.second;
			o.flags = x.flags;
			if (q.support) o.flags |= COLLP_SUPPORT;
			if (IsFirst (cx, q)) o.flags |= COLLP_FIRST;
			if (o.flags & COLLP_DEGENERATE) rflags |= COLLF_DEGENERATE;
		}
		return npt;
	}

	// results (6)

	static void BodyAt (const Ctx &cx, bool sideB, CollBodyAt &s)
	{
		const CollMotion &m = sideB ? cx.B->m : cx.A->m;
		s.c = sideB ? cx.cA + cx.r : cx.cA;
		if (cx.mode == POSE_T0) { s.v = m.v0; s.q.Set (m.q0); s.w = m.Omega (m.ta); }
		else if (cx.mode == POSE_T1) { s.v = m.v1; s.q.Set (m.q1); s.w = m.Omega (m.tb); }
		else { s.v = m.Vel (cx.tau); s.q.Set (m.Rot (cx.tau)); s.w = m.Omega (cx.tau); }
	}

	static void AddInfo (CollDetect &d, int a, int b, uint8_t res, double E, double th)
	{
		auto it = std::lower_bound (d.m_info.begin (), d.m_info.end (), std::make_pair (a, b), [] (const PairInfo &x, const std::pair<int, int> &k) {
			return x.a != k.first ? x.a < k.first : x.b < k.second; });
		if (it != d.m_info.end () && it->a == a && it->b == b) { it->res |= res; return; }
		d.m_info.insert (it, PairInfo { a, b, res, E, th });
	}

	static void SortedAdd (std::vector<OwnerPair> &v, const OwnerPair &k)
	{
		auto it = std::lower_bound (v.begin (), v.end (), k);
		if (it == v.end () || !(*it == k)) v.insert (it, k);
	}

	static bool SortedHas (const std::vector<OwnerPair> &v, const OwnerPair &k)
	{
		return std::binary_search (v.begin (), v.end (), k);
	}

	// bookkeeping of a passed-on result: NEWPAIR, owner pairs with results, last kind, pair TOI info
	static void Book (Ctx &cx, CollPairResult &r)
	{
		CollDetect &d = *cx.d;
		bool seen = false;
		for (int i = 0; i < r.npt; i++) {
			const PP *q = nullptr;
			for (const PP &x : cx.pp) if (x.a == r.pt[i].partA && x.b == r.pt[i].partB) { q = &x; break; }
			if (!q) continue;
			if (SortedHas (d.m_resPrev, q->key)) seen = true;
			SortedAdd (d.m_resCur, q->key);
			CollPairEntry &e = d.m_store.Get (q->key.first, q->key.second);
			e.lastKind = r.kind; e.lastFrame = d.m_frame;
		}
		if (!seen) r.flags |= COLLF_NEWPAIR;
		AddInfo (d, cx.ia, cx.ib, r.kind == COLL_RESTING ? 1 : 2, cx.E, cx.thetaEff);
	}

	// one result from raw pairs at the current pose; false if no point survives; book = false: probe
	static bool Result (Ctx &cx, CollKind kind, uint32_t flags, double spec, std::vector<Raw> &raws, double extra, bool margins, std::vector<CollPairResult> &out, bool book = true)
	{
		CollDetect &d = *cx.d;
		const CollParams &p = d.m_prm;
		CollPairResult r {};
		uint32_t rf = 0;
		int npt = Manifold (cx, raws, extra, margins, r.pt, rf);
		if (!npt) return false;
		r.kind = kind;
		r.bodyA = cx.ia; r.bodyB = cx.ib;
		r.tau = cx.tau;
		r.specGap = spec;
		r.E = cx.E;
		r.origin = d.m_org[cx.ia];
		BodyAt (cx, false, r.a); BodyAt (cx, true, r.b);
		r.npt = npt;
		r.flags = flags | rf | (cx.flags & (COLLF_GRACE | COLLF_ZONE));
		if (cx.E > p.eTol || cx.thetaEff > p.thetaMax) r.flags |= COLLF_INACCURATE;
		if (cx.A->entry | cx.B->entry) r.flags |= COLLF_ENTRY;
		if (book) Book (cx, r);
		out.push_back (r);
		return true;
	}

	// manifold at the current pose (5.5 step 1): d_tri - rs <= extra + held margins, scopes, zones
	static void Collect (Ctx &cx, double extra, bool margins, std::vector<Raw> &raws)
	{
		for (size_t i = 0; i < cx.pp.size (); i++) {
			const PP &q = cx.pp[i];
			if (q.culled) continue;
			double mg = margins ? cx.pa[q.a].marg + cx.pb[q.b].marg : 0.0;
			Cutoff (cx, i, extra + mg, Q_SCOPES | Q_ZONES, raws, raws.size () + 4096, nullptr);
		}
	}

	// CA outcome to a result (TOI: manifold at the hit pose; SPECULATIVE: cutoff at tau_cap, 5.4)
	static void CaResult (Ctx &cx, CollKind k, double spec, uint32_t flags, std::vector<CollPairResult> &out)
	{
		const CollParams &p = cx.d->m_prm;
		std::vector<Raw> raws;
		if (k == COLL_TOI) {
			Collect (cx, p.deltaCt + cx.Em, true, raws);
			Result (cx, COLL_TOI, flags, 0.0, raws, p.deltaCt + cx.Em, true, out);
			return;
		}
		if (k != COLL_SPECULATIVE) return;
		SetPose (cx, POSE_MODEL, cx.tau);
		double ext = spec + p.deltaCt, lim = cx.Dstep*(1.0 - cx.tau) + p.deltaCt;
		if (!std::isfinite (lim)) lim = ext;                     // NaN or inf step bound: no widening (fix2)
		for (int it = 0; it < 64; it++) {
			raws.clear ();
			Collect (cx, ext, false, raws);
			if (!raws.empty () || !(ext < lim)) break;
			ext = std::min (2.0*ext + p.deltaCt, lim);
		}
		if (raws.empty ()) return;
		Result (cx, COLL_SPECULATIVE, flags, spec, raws, ext, false, out);
	}

	static void RunAndReport (Ctx &cx, double tau0, bool core, uint32_t flags, std::vector<CollPairResult> &out, CollFrameStats *st)
	{
		double tau, spec;
		Raw hit;
		int it;
		CollKind k = RunCa (cx, tau0, core, tau, spec, hit, it);
		if (st) st->iterations += it;
		if (k == COLL_NONE) return;
		if (k == COLL_SPECULATIVE) {
			cx.tau = tau;
			CollDetect &d = *cx.d;
			CollLog (COLLLOG_FINE, "Collision: CA cap, bodies %u-%u at tau %.6f, %d iterations, %lld triangle pairs", cx.A->id, cx.B->id, tau, it, cx.tt);
			std::pair<uint32_t, uint32_t> key (cx.A->id, cx.B->id);
			auto lt = std::lower_bound (d.m_specLog.begin (), d.m_specLog.end (), key, [] (const std::pair<std::pair<uint32_t, uint32_t>, double> &x, const std::pair<uint32_t, uint32_t> &k) { return x.first < k; });
			bool have = lt != d.m_specLog.end () && lt->first == key;
			if (!have || d.m_simt - lt->second >= SPEC_LOG_INTERVAL) {
				CollLog (COLLLOG_INFO, "Collision: CA cap, bodies %u - %u, %d iterations; contact handled as speculative", cx.A->id, cx.B->id, it);
				if (have) lt->second = d.m_simt;
				else d.m_specLog.insert (lt, { key, d.m_simt });
			}
		}
		CaResult (cx, k, spec, flags | (core ? (uint32_t)COLLF_CORE : 0u), out);
	}

	// frame steps

	static CollPairEntry &EntryAt (CollPairStore &s, size_t i)
	{
		const CollPairEntry &x = s.At (i);
		return *s.Find (x.a, x.b);
	}

	// unreferenced pair or body state: fronts of purged or flushed pairs, stale CA-cap log times
	static void Purge (CollDetect &d)
	{
		d.m_front.erase (std::remove_if (d.m_front.begin (), d.m_front.end (), [&d] (const FrontPose &f) {
			const CollPairEntry *e = d.m_store.Find (f.a, f.b);
			return !e || e->frontCut < 0.0; }), d.m_front.end ());
		d.m_specLog.erase (std::remove_if (d.m_specLog.begin (), d.m_specLog.end (), [&d] (const std::pair<std::pair<uint32_t, uint32_t>, double> &x) {
			return d.m_simt - x.second >= SPEC_LOG_INTERVAL; }), d.m_specLog.end ());
	}

	static void BuildIndex (CollDetect &d)
	{
		d.m_pidx.clear ();
		for (size_t i = 0; i < d.m_body.size (); i++)
			for (size_t k = 0; k < d.m_body[i].parts.size (); k++)
				d.m_pidx.push_back (PartIdx { d.m_body[i].parts[k].owner, d.m_body[i].parts[k].partKey, (int)i, (int)k });
		std::stable_sort (d.m_pidx.begin (), d.m_pidx.end (), [] (const PartIdx &x, const PartIdx &y) {
			if (!(x.owner == y.owner)) return x.owner < y.owner;
			return x.partKey < y.partKey; });
	}

	static const PartIdx *FindPart (const CollDetect &d, const CollOwnerKey &o, uint32_t key, bool &ownerSeen)
	{
		auto it = std::lower_bound (d.m_pidx.begin (), d.m_pidx.end (), o, [] (const PartIdx &x, const CollOwnerKey &o) { return x.owner < o; });
		ownerSeen = it != d.m_pidx.end () && it->owner == o;
		for (; it != d.m_pidx.end () && it->owner == o; ++it)
			if (it->partKey == key) return &*it;
		return nullptr;
	}

	// part version changes drop that part's scope entries and give its body MESH (3.2)
	static void CheckVersions (CollDetect &d)
	{
		for (size_t i = 0; i < d.m_store.Size (); i++) {
			CollPairEntry &e = EntryAt (d.m_store, i);
			if (e.grace.empty ()) continue;
			size_t n = e.grace.size ();
			e.grace.erase (std::remove_if (e.grace.begin (), e.grace.end (), [&] (const CollLeafPair &l) {
				bool sa, sb;
				const PartIdx *pa = FindPart (d, e.a, l.partKeyA, sa), *pb = FindPart (d, e.b, l.partKeyB, sb);
				bool drop = false;
				if (pa && d.m_body[pa->body].parts[pa->part].version != l.verA) { d.m_body[pa->body].entry |= COLLE_MESH; drop = true; }
				if (pb && d.m_body[pb->body].parts[pb->part].version != l.verB) { d.m_body[pb->body].entry |= COLLE_MESH; drop = true; }
				return drop; }), e.grace.end ());
			if (n && e.grace.empty ()) { e.graceAge = 0.0; e.graceLogged = false; }
		}
	}

	static bool Skip (const CollBody &A, const CollBody &B)
	{
		return A.parts.empty () || B.parts.empty () || (A.kind != COLLB_DYNAMIC && B.kind != COLLB_DYNAMIC);
	}

	static bool Capsule (const CollDetect &d, int i, int j)
	{
		const CollBody &A = d.m_body[i], &B = d.m_body[j];
		Vector Q[4], M[3];
		CollRelBezier (A.m, B.m, Q, M);
		double E = CollErrorT (A.m, B.m) + CollErrorR (A.m, A.rmax) + CollErrorR (B.m, B.rmax);
		return CollCapsuleHit (Q, A.rmax + B.rmax + d.m_prm.deltaCt + std::min (E, COLL_E_CAND));
	}

	// candidate body pairs (2.2, 2.3); b >= 0: only pairs with b
	static void Broad (const CollDetect &d, int b, std::vector<std::pair<int, int>> &cand)
	{
		cand.clear ();
		int n = (int)d.m_body.size ();
		std::vector<std::pair<int, int>> pre;
		if (b < 0 && n > d.m_prm.sapThreshold) {
			struct Iv { double lo, hi; int i; };
			std::vector<Iv> iv;
			CollMotion Z {};
			for (int i = 0; i < n; i++) {
				const CollMotion &m = d.m_body[i].m;
				Z.h = m.h; Z.ta = m.ta; Z.tb = m.tb; Z.a0ok = true;
				double H = m.h*(m.tb - m.ta);
				double e = CollErrorT (Z, m) + CollErrorR (m, d.m_body[i].rmax);
				double pad = d.m_body[i].rmax + 0.5*d.m_prm.deltaCt + std::min (e, COLL_E_CAND);
				double x[4] = { m.c0.x, m.c0.x + m.v0.x*H/3.0, m.c1.x - m.v1.x*H/3.0, m.c1.x };
				iv.push_back (Iv { *std::min_element (x, x+4) - pad, *std::max_element (x, x+4) + pad, i });
			}
			std::stable_sort (iv.begin (), iv.end (), [] (const Iv &x, const Iv &y) { return x.lo < y.lo; });
			for (size_t k = 0; k < iv.size (); k++)
				for (size_t l = k+1; l < iv.size () && iv[l].lo <= iv[k].hi; l++)
					pre.push_back ({ std::min (iv[k].i, iv[l].i), std::max (iv[k].i, iv[l].i) });
			std::sort (pre.begin (), pre.end ());
		} else {
			for (int i = 0; i < n; i++)
				for (int j = i+1; j < n; j++)
					if (b < 0 || i == b || j == b) pre.push_back ({ i, j });
		}
		for (const auto &p : pre) {
			if (Skip (d.m_body[p.first], d.m_body[p.second])) continue;
			if (Capsule (d, p.first, p.second)) cand.push_back (p);
		}
	}

	// touch-state update at the start of Detect (3.6 rules 1-4)
	static void UpdateTouch (CollDetect &d, const std::vector<std::pair<int, int>> &cand)
	{
		struct Req { size_t e, t; int i, j, pa, pb; };
		std::vector<Req> req;
		for (size_t ei = 0; ei < d.m_store.Size (); ei++) {
			CollPairEntry &e = EntryAt (d.m_store, ei);
			for (size_t ti = 0; ti < e.touch.size (); ti++) {
				CollTouch &t = e.touch[ti];
				if (t.solved) { t.state = COLLT_TOUCHING; t.solved = false; continue; }
				bool sa, sb;
				const PartIdx *pa = FindPart (d, e.a, t.partKeyA, sa), *pb = FindPart (d, e.b, t.partKeyB, sb);
				if (!pa || !pb) {
					if (sa && sb) t.state = 0xff;
					continue;
				}
				if (pa->body == pb->body) continue;
				const CollBody &A = d.m_body[pa->body], &B = d.m_body[pb->body];
				if (A.kind != COLLB_DYNAMIC && B.kind != COLLB_DYNAMIC) continue;
				bool swap = pb->body < pa->body;
				std::pair<int, int> bp = swap ? std::make_pair (pb->body, pa->body) : std::make_pair (pa->body, pb->body);
				if (!std::binary_search (cand.begin (), cand.end (), bp)) { t.state = 0xff; continue; }
				req.push_back (Req { ei, ti, bp.first, bp.second, swap ? pb->part : pa->part, swap ? pa->part : pb->part });
			}
		}
		std::stable_sort (req.begin (), req.end (), [] (const Req &x, const Req &y) { return x.i != y.i ? x.i < y.i : x.j < y.j; });
		std::vector<Raw> raws;
		for (size_t k = 0; k < req.size (); ) {
			size_t l = k;
			while (l < req.size () && req[l].i == req[k].i && req[l].j == req[k].j) l++;
			Ctx cx;
			Prepare (d, cx, req[k].i, req[k].j);
			Bind (cx, true);
			SetPose (cx, POSE_T0, 0.0);
			for (size_t m = k; m < l; m++) {
				size_t pi = 0;
				for (; pi < cx.pp.size (); pi++) if (cx.pp[pi].a == req[m].pa && cx.pp[pi].b == req[m].pb) break;
				uint8_t st = 0xff;
				if (pi < cx.pp.size () && !cx.pp[pi].culled) {
					raws.clear ();
					Cutoff (cx, pi, d.m_prm.sTouch, Q_RAW | Q_FIRST | Q_FRONT, raws, 1, nullptr);
					if (!raws.empty ()) st = COLLT_LEFT;
				}
				EntryAt (d.m_store, req[m].e).touch[req[m].t].state = st;
			}
			k = l;
		}
		for (size_t ei = 0; ei < d.m_store.Size (); ei++) {
			CollPairEntry &e = EntryAt (d.m_store, ei);
			e.touch.erase (std::remove_if (e.touch.begin (), e.touch.end (), [] (const CollTouch &t) { return t.state == 0xff; }), e.touch.end ());
		}
	}

	// entry check (3.2): raw intersection at the reference pose starts or extends GRACE; not supports
	static void EntryCheck (Ctx &cx, int mode)
	{
		CollDetect &d = *cx.d;
		SetPose (cx, mode, mode == POSE_T1 ? 1.0 : 0.0);
		std::vector<Raw> raws;
		for (size_t k = 0; k < cx.pp.size (); k++)
			if (!cx.pp[k].culled && !cx.pp[k].support) Cutoff (cx, k, 0.0, Q_RAWX, raws, raws.size () + COLL_ENTRY_RAW_MAX, nullptr);
		if (raws.empty ()) return;
		std::vector<std::pair<OwnerPair, CollLeafPair>> lp;
		for (const Raw &w : raws) lp.push_back ({ cx.pp[w.k].key, LeafKey (cx, cx.pp[w.k], w.la, w.lb) });
		std::sort (lp.begin (), lp.end (), [] (const std::pair<OwnerPair, CollLeafPair> &x, const std::pair<OwnerPair, CollLeafPair> &y) {
			if (!(x.first == y.first)) return x.first < y.first;
			return x.second < y.second; });
		for (size_t k = 0; k < lp.size (); ) {
			size_t l = k;
			CollPairEntry &e = d.m_store.Get (lp[k].first.first, lp[k].first.second);
			bool fresh = e.grace.empty ();
			size_t n0 = e.grace.size ();
			for (; l < lp.size () && lp[l].first == lp[k].first; l++) e.grace.push_back (lp[l].second);
			std::sort (e.grace.begin (), e.grace.end ());
			e.grace.erase (std::unique (e.grace.begin (), e.grace.end ()), e.grace.end ());
			if (fresh) { e.graceAge = 0.0; e.graceLogged = false; }
			CollLog (COLLLOG_INFO, "Collision: GRACE %s, owners %u/%d.%d.%d - %u/%d.%d.%d, %u leaf pairs (entry 0x%x)", fresh ? "start" : "extend",
				e.a.id, e.a.planet, e.a.base, e.a.obj, e.b.id, e.b.planet, e.b.base, e.b.obj, (unsigned)(e.grace.size () - n0), (unsigned)(cx.A->entry | cx.B->entry));
			k = l;
		}
	}

	// GRACE release per leaf pair at t1 (3.2) and scope ageing
	static void Release (CollDetect &d)
	{
		for (size_t ei = 0; ei < d.m_store.Size (); ei++) {
			CollPairEntry &e = EntryAt (d.m_store, ei);
			if (e.grace.empty ()) continue;
			e.graceAge += d.m_h;
			if (e.graceAge > COLL_GRACE_LOG_AGE && !e.graceLogged) {
				e.graceLogged = true;
				CollLog (COLLLOG_WARN, "Collision: GRACE scope older than %g s, owners %u/%d.%d.%d - %u/%d.%d.%d, %u leaf pairs", COLL_GRACE_LOG_AGE,
					e.a.id, e.a.planet, e.a.base, e.a.obj, e.b.id, e.b.planet, e.b.base, e.b.obj, (unsigned)e.grace.size ());
			}
			e.grace.erase (std::remove_if (e.grace.begin (), e.grace.end (), [&] (const CollLeafPair &l) { return Released (d, e, l); }), e.grace.end ());
			if (e.grace.empty ()) {
				e.graceAge = 0.0; e.graceLogged = false;
				CollLog (COLLLOG_INFO, "Collision: GRACE end, owners %u/%d.%d.%d - %u/%d.%d.%d", e.a.id, e.a.planet, e.a.base, e.a.obj, e.b.id, e.b.planet, e.b.base, e.b.obj);
			}
		}
	}

	static bool Released (const CollDetect &d, const CollPairEntry &e, const CollLeafPair &l)
	{
		bool sa, sb;
		const PartIdx *pa = FindPart (d, e.a, l.partKeyA, sa), *pb = FindPart (d, e.b, l.partKeyB, sb);
		if (!pa || !pb) return sa && sb;
		const CollBody &A = d.m_body[pa->body], &B = d.m_body[pb->body];
		const CollPartRef &ra = A.parts[pa->part], &rb = B.parts[pb->part];
		if (!ra.geom || !rb.geom || !IsLeaf (*ra.geom, l.leafA) || !IsLeaf (*rb.geom, l.leafB)) return true;
		CollAffine XA = ra.P1, XB = rb.P1;
		if (pa->body != pb->body) {
			Matrix RA = QMat (A.m.q1), RB = QMat (B.m.q1);
			XA = CollAffine { RA * ra.P1.A, mul (RA, ra.P1.t) };
			XB = CollAffine { RB * rb.P1.A, mul (RB, rb.P1.t) + (B.m.c1 - A.m.c1) };
		}
		const CollNode &na = ra.geom->node[l.leafA], &nb = rb.geom->node[l.leafB];
		double dmin = 1e300;
		int n = 0;
		for (uint32_t x = na.first; x < na.first + na.count; x++) {
			const CollTri &ta = ra.geom->tri[ra.geom->perm[x]];
			if (ra.mask && ra.mask[ta.src]) continue;
			Vector va[3] = { CollApply (XA, ra.geom->vtx[ta.v[0]]), CollApply (XA, ra.geom->vtx[ta.v[1]]), CollApply (XA, ra.geom->vtx[ta.v[2]]) };
			for (uint32_t y = nb.first; y < nb.first + nb.count; y++) {
				const CollTri &tb = rb.geom->tri[rb.geom->perm[y]];
				if (rb.mask && rb.mask[tb.src]) continue;
				if (n >= COLL_GRACE_RELEASE_TRI) return false;          // leaf pair too big to judge: keep the scope, re-checked next frame (fix2 S6)
				Vector vb[3] = { CollApply (XB, rb.geom->vtx[tb.v[0]]), CollApply (XB, rb.geom->vtx[tb.v[1]]), CollApply (XB, rb.geom->vtx[tb.v[2]]) };
				Vector pa2, pb2;
				dmin = std::min (dmin, CollTriTriDistance (va, vb, pa2, pb2));
				n++;
			}
		}
		return dmin - ra.skin - rb.skin >= d.m_prm.sRel;
	}

	// first pass of one candidate pair (3.2, 5.7)
	static void FirstPass (CollDetect &d, int ia, int ib, std::vector<CollPairResult> &out, CollFrameStats &st)
	{
		const CollParams &p = d.m_prm;
		Ctx cx;
		Prepare (d, cx, ia, ib);
		AddInfo (d, ia, ib, 0, cx.E, cx.thetaEff);
		st.candidates++;
		bool jump = cx.A->jump1 || cx.B->jump1;
		if ((cx.A->entry | cx.B->entry) || jump) EntryCheck (cx, jump ? POSE_T1 : POSE_T0);
		if (jump || cx.pp.empty ()) { st.bvPairs += (int)cx.bv; st.triPairs += (int)cx.tt; return; }
		Bind (cx, true);
		SetPose (cx, POSE_T0, 0.0);
		// touching test at the exact t0 poses (5.7 step 1), with front caches (5.6)
		std::vector<Raw> raws;
		std::vector<std::pair<size_t, std::vector<CollLeafPair>>> builds;  // owner-pair group, its new front
		std::vector<int> bi (cx.pp.size (), -1);
		if (p.mFront > 0.0)
			for (size_t g = 0; g + 1 < cx.grp.size (); g++) {
				if (cx.pp[cx.ord[cx.grp[g]]].front || !Live (cx, g)) continue;
				builds.push_back ({ g, {} });
				for (size_t i = cx.grp[g]; i < cx.grp[g + 1]; i++) bi[cx.ord[i]] = (int)builds.size () - 1;
			}
		for (size_t k = 0; k < cx.pp.size (); k++) {
			const PP &q = cx.pp[k];
			std::vector<CollLeafPair> *bl = bi[k] >= 0 ? &builds[bi[k]].second : nullptr;
			if (q.culled && !bl) continue;
			Cutoff (cx, k, p.deltaCt, Q_SCOPES | Q_ZONES | Q_FRONT, raws, 4096, bl);
		}
		for (auto &b : builds) FrontStore (cx, b.first, b.second);
		if (!builds.empty ()) Bind (cx, true);
		// support distance (Y10)
		for (const Raw &w : raws) {
			const PP &q = cx.pp[w.k];
			if (!q.support) continue;
			uint32_t id = cx.A->kind == COLLB_BASE ? cx.B->id : cx.A->id;
			auto it = std::lower_bound (d.m_supDist.begin (), d.m_supDist.end (), id, [] (const std::pair<uint32_t, double> &x, uint32_t id) { return x.first < id; });
			if (it != d.m_supDist.end () && it->first == id) it->second = std::min (it->second, w.d);
			else d.m_supDist.insert (it, { id, w.d });
		}
		if (!raws.empty ()) {
			size_t n0 = out.size ();
			Result (cx, COLL_RESTING, 0, 0.0, raws, p.deltaCt, false, out);
			if (out.size () > n0) {
				const CollPairResult &r = out.back ();
				for (int i = 0; i < r.npt; i++) {
					if (!(r.pt[i].flags & COLLP_INTERSECT) || (r.pt[i].flags & COLLP_SUPPORT)) continue;
					for (const PP &q : cx.pp) {
						if (q.a != r.pt[i].partA || q.b != r.pt[i].partB) continue;
						if (!SortedHas (d.m_warnPrev, q.key) && !SortedHas (d.m_warnCur, q.key))
							CollLog (COLLLOG_WARN, "Collision: raw intersection in a normal pair, bodies %u - %u", cx.A->id, cx.B->id);
						SortedAdd (d.m_warnCur, q.key);
						break;
					}
				}
			}
			Bind (cx, true);
			RunAndReport (cx, 0.0, true, 0, out, &st);
		} else
			RunAndReport (cx, 0.0, false, 0, out, &st);
		st.bvPairs += (int)cx.bv;
		st.triPairs += (int)cx.tt;
	}
};

// CollDetect

void CollDetect::Begin (const CollParams &prm, double h)
{
	m_prm = prm;
	m_h = h;
	m_frame++;
	m_simt += h;
	m_body.clear ();
	m_org.clear ();
	m_zone.clear ();
	m_info.clear ();
	m_supDist.clear ();
	m_resPrev.swap (m_resCur);
	m_resCur.clear ();
	m_warnPrev.swap (m_warnCur);
	m_warnCur.clear ();
	m_badPrev.swap (m_badCur);
	m_badCur.clear ();
}

static bool Finite (const Vector &v) { return std::isfinite (v.x) && std::isfinite (v.y) && std::isfinite (v.z); }
static bool Finite (const Quaternion &q) { return std::isfinite (q.qvx) && std::isfinite (q.qvy) && std::isfinite (q.qvz) && std::isfinite (q.qs); }
static bool Finite (const CollAffine &P) { for (int i = 0; i < 9; i++) if (!std::isfinite (P.A.data[i])) return false; return Finite (P.t); }

int CollDetect::AddBody (const CollBody &b)
{
	m_body.push_back (b);
	CollBody &c = m_body.back ();
	c.m.h = m_h;
	// a non-finite state skips the body this frame (10.1): no parts, so every pair with it is skipped
	const CollMotion &m = c.m;
	bool ok = Finite (m.c0) && Finite (m.v0) && Finite (m.a0) && Finite (m.c1) && Finite (m.v1) && Finite (m.a1) && Finite (m.q0) && Finite (m.q1) && Finite (m.w0g) && Finite (m.w1g);
	for (const CollPartRef &p : c.parts) ok = ok && Finite (p.P0) && Finite (p.P1);
	if (!ok) {
		if (!std::binary_search (m_badPrev.begin (), m_badPrev.end (), c.id)) CollLog (COLLLOG_ERROR, "Collision: body %u has a non-finite state, skipped", c.id);
		auto it = std::lower_bound (m_badCur.begin (), m_badCur.end (), c.id);
		if (it == m_badCur.end () || *it != c.id) m_badCur.insert (it, c.id);
		c.parts.clear ();
		c.m.c0 = c.m.c1 = Vector (); c.m.v0 = c.m.v1 = c.m.a0 = c.m.a1 = c.m.w0g = c.m.w1g = Vector ();
		c.m.q0.Set (Quaternion ()); c.m.q1.Set (Quaternion ());
	}
	double rmax = 0.0;
	for (size_t k = 0; k < c.parts.size (); k++) {
		CollPartRef &p = c.parts[k];
		p.skin = std::min (COLL_SKIN_MAX, std::max (COLL_SKIN_MIN, p.skin));
		if (!p.geom) { p.disp = p.rho = 0.0; p.interp = false; p.rigid = true; continue; }
		p.c = p.geom->bsCentre;
		p.rigid = CollIsRigid (p.P0) && CollIsRigid (p.P1);
		p.disp = CollPoseMotion (*p.geom, p.P0, p.P1);
		if (p.disp > m_prm.deltaCt && m_h > 0.0 && p.disp/m_h > m_prm.vPartMax) {
			CollLog (COLLLOG_FINE, "Collision: part jump, body %u part %u, %.3f m in %.4f s", c.id, (unsigned)k, p.disp, m_h);
			p.P0 = p.P1;
			p.rigid = CollIsRigid (p.P1);
			p.disp = 0.0;
			c.entry |= COLLE_MESH;
		}
		p.interp = p.disp > m_prm.deltaCt;
		double cmax = std::max (CollApply (p.P0, p.c).length (), CollApply (p.P1, p.c).length ());
		double sc = p.rigid ? 1.0 : std::max (Frob (p.P0.A), Frob (p.P1.A));
		p.rho = cmax + sc*p.geom->bsRadius;
		rmax = std::max (rmax, p.rho + p.skin);
	}
	if (c.rmax <= 0.0) c.rmax = rmax;
	c.m.Setup ();
	m_org.push_back (c.m.c0);
	return (int)m_body.size () - 1;
}

void CollDetect::SetZones (const std::vector<CollZone> &z)
{
	m_zone = z;
}

void CollDetect::SetSupports (uint32_t assemblyId, const std::vector<CollSupport> &s)
{
	auto it = std::lower_bound (m_support.begin (), m_support.end (), assemblyId,
		[] (const std::pair<uint32_t, std::vector<CollSupport>> &x, uint32_t id) { return x.first < id; });
	bool have = it != m_support.end () && it->first == assemblyId;
	if (s.empty ()) { if (have) m_support.erase (it); return; }
	if (have) it->second = s;
	else m_support.insert (it, { assemblyId, s });
}

void CollDetect::Detect (std::vector<CollPairResult> &out, CollFrameStats &st)
{
	st = CollFrameStats {};
	m_info.clear ();
	m_supDist.clear ();
	Impl::Purge (*this);
	Impl::BuildIndex (*this);
	Impl::CheckVersions (*this);
	std::vector<std::pair<int, int>> cand;
	Impl::Broad (*this, -1, cand);
	Impl::UpdateTouch (*this, cand);
	size_t n0 = out.size ();
	for (const auto &c : cand) Impl::FirstPass (*this, c.first, c.second, out, st);
	Impl::Release (*this);
	st.results = (int)(out.size () - n0);
	for (size_t i = 0; i < m_store.Size (); i++) if (!m_store.At (i).grace.empty ()) st.graceScopes++;
}

void CollDetect::Resweep (int b, double tau, const CollRestart &r, std::vector<CollPairResult> &out)
{
	if (b < 0 || b >= (int)m_body.size ()) return;
	MotionCopy (m_body[b].m, m_body[b].m.Split (tau, r));
	if (m_body[b].kind != COLLB_DYNAMIC) {   // D3 changed it (a woken LANDED body): dynamic from here on, so its supports are swept too
		CollLog (COLLLOG_FINE, "Collision: body %u re-swept as dynamic (kind %d before)", m_body[b].id, (int)m_body[b].kind);
		m_body[b].kind = COLLB_DYNAMIC;
	}
	if (m_body[b].jump1) return;
	std::vector<std::pair<int, int>> cand;
	Impl::Broad (*this, b, cand);
	for (const auto &c : cand) {
		if (m_body[c.first].jump1 || m_body[c.second].jump1) continue;
		Impl::Ctx cx;
		Impl::Prepare (*this, cx, c.first, c.second);
		if (cx.pp.empty ()) continue;
		Impl::Bind (cx, false);
		Impl::SetPose (cx, Impl::POSE_MODEL, cx.s0);
		std::vector<Impl::Raw> raws;
		Impl::Collect (cx, m_prm.deltaCt + cx.Em, true, raws);
		if (raws.empty ()) { Impl::RunAndReport (cx, cx.s0, false, COLLF_RESWEEP, out, nullptr); continue; }
		std::vector<CollPairResult> tmp;
		if (!Impl::Result (cx, COLL_TOI, COLLF_RESWEEP, 0.0, raws, m_prm.deltaCt + cx.Em, true, tmp, false)) continue;
		Impl::Bind (cx, false);
		CollPairResult &t = tmp.back ();
		bool appr = false;
		for (int i = 0; i < t.npt && !appr; i++) {
			const CollContact &p = t.pt[i];
			Vector va = t.a.v + Xc (t.a.w, p.pA - t.a.c) + p.vsA;
			Vector vb = t.b.v + Xc (t.b.w, p.pB - t.b.c) + p.vsB;
			if (((va - vb) & p.n) < -COLL_APPROACH_TOL) appr = true;
		}
		if (appr) { Impl::Book (cx, t); out.push_back (t); }
		else Impl::RunAndReport (cx, cx.s0, true, COLLF_RESWEEP, out, nullptr);
	}
}

void CollDetect::Solved (const CollPairResult &r)
{
	if (r.kind != COLL_TOI && r.kind != COLL_RESTING) return;
	if (r.bodyA < 0 || r.bodyB < 0 || r.bodyA >= (int)m_body.size () || r.bodyB >= (int)m_body.size ()) return;
	const CollBody &A = m_body[r.bodyA], &B = m_body[r.bodyB];
	for (int i = 0; i < r.npt; i++) {
		const CollContact &c = r.pt[i];
		if (c.partA >= A.parts.size () || c.partB >= B.parts.size ()) continue;
		const CollPartRef &pa = A.parts[c.partA], &pb = B.parts[c.partB];
		bool swap = pb.owner < pa.owner;
		CollPairEntry &e = swap ? m_store.Get (pb.owner, pa.owner) : m_store.Get (pa.owner, pb.owner);
		uint32_t ka = swap ? pb.partKey : pa.partKey, kb = swap ? pa.partKey : pb.partKey;
		auto it = std::lower_bound (e.touch.begin (), e.touch.end (), std::make_pair (ka, kb), [] (const CollTouch &t, const std::pair<uint32_t, uint32_t> &k) {
			return t.partKeyA != k.first ? t.partKeyA < k.first : t.partKeyB < k.second; });
		if (it != e.touch.end () && it->partKeyA == ka && it->partKeyB == kb) it->solved = true;
		else e.touch.insert (it, CollTouch { ka, kb, COLLT_APART, true });
	}
}

bool CollDetect::SupportDist (uint32_t assemblyId, double &minRaw) const
{
	auto it = std::lower_bound (m_supDist.begin (), m_supDist.end (), assemblyId, [] (const std::pair<uint32_t, double> &x, uint32_t id) { return x.first < id; });
	if (it == m_supDist.end () || it->first != assemblyId) return false;
	minRaw = it->second;
	return true;
}

void CollDetect::LookAhead (const std::vector<CollEnd> &end, CollWarpInput &w) const
{
	int n = (int)std::min (end.size (), m_body.size ());
	for (int i = 0; i < n; i++) {
		for (int j = i+1; j < n; j++) {
			const CollBody &A = m_body[i], &B = m_body[j];
			if (Impl::Skip (A, B)) continue;
			auto it = std::lower_bound (m_info.begin (), m_info.end (), std::make_pair (i, j), [] (const PairInfo &x, const std::pair<int, int> &k) {
				return x.a != k.first ? x.a < k.first : x.b < k.second; });
			const PairInfo *pi = (it != m_info.end () && it->a == i && it->b == j) ? &*it : nullptr;
			if (pi && (pi->E > m_prm.eTol || pi->thetaEff > m_prm.thetaMax)) {
				double f = 1.0;
				if (pi->E > m_prm.eTol) f = std::min (f, sqrt (m_prm.eTol/pi->E));
				if (pi->thetaEff > m_prm.thetaMax) f = std::min (f, m_prm.thetaMax/pi->thetaEff);
				if (f < w.accF) { w.accF = f; w.idAcc[0] = A.id; w.idAcc[1] = B.id; }
			}
			if (pi && (pi->res & 1)) continue;
			bool appr = pi && (pi->res & 2);
			if (!appr) {
				int bi = A.kind == COLLB_BASE ? i : B.kind == COLLB_BASE ? j : -1;
				if (bi < 0) appr = CollLookAheadHit (end[i], end[j], m_h, m_prm.deltaCt);
				else {
					int vi = bi == i ? j : i;
					const CollBody &Bs = m_body[bi];
					// whole base first: its bound, inflated by the turn of its buildings over the look-ahead span (2.4)
					CollEnd eb { end[bi].c1, end[bi].v1, end[bi].a1, Bs.rmax*(1.0 + Bs.m.w1g.length ()*COLL_LOOKAHEAD*m_h), 0.0 };
					if (!CollLookAheadHit (eb, end[vi], m_h, m_prm.deltaCt)) continue;
					Matrix R1 = QMat (Bs.m.q1);
					for (const CollPartRef &p : Bs.parts) {
						if (!p.geom) continue;
						Vector o = mul (R1, CollApply (p.P1, p.c));
						CollEnd e { end[bi].c1 + o, end[bi].v1 + Xc (Bs.m.w1g, o), end[bi].a1, p.geom->bsRadius + p.skin, 0.0 };
						if (CollLookAheadHit (e, end[vi], m_h, m_prm.deltaCt)) { appr = true; break; }
					}
				}
			}
			if (appr && COLL_H_CONTACT < w.hContact) { w.hContact = COLL_H_CONTACT; w.idContact[0] = A.id; w.idContact[1] = B.id; }
		}
	}
}

// pose of a result's part at the result time: RESTING at the exact t0 pose, else the CA pose
static CollAffine ResultPose (const CollPairResult &r, const CollPartRef &p)
{
	if (r.kind == COLL_RESTING) return p.P0;
	if (!p.interp) return p.P1;
	return CollPoseAt (p.P0, p.P1, p.c, r.tau);
}

// in: p, n in the pair frame (from r.origin, global axes); out: rest frame of point i's part
void CollDetect::ToPartFrame (const CollPairResult &r, int i, int side, Vector &p, Vector &n) const
{
	const CollBody &B = m_body[side ? r.bodyB : r.bodyA];
	const CollContact &c = r.pt[i];
	const CollPartRef &pr = B.parts[side ? c.partB : c.partA];
	const CollBodyAt &s = side ? r.b : r.a;
	CollAffine P = ResultPose (r, pr);
	if (!Invertible (P.A)) P = pr.P0;                       // pose interpolated through a singular scale: rest pose (fix2)
	Matrix R = QMat (s.q);
	Vector xb = tmul (R, p - s.c);
	p = mul (inv (P.A), xb - P.t);
	Vector np = tmul (P.A, tmul (R, n));
	double l = np.length ();
	n = l > 0.0 ? np/l : np;
}

Vector CollDetect::SurfaceVel (const CollPairResult &r, int i, int side, double tau) const
{
	const CollBody &B = m_body[side ? r.bodyB : r.bodyA];
	const CollContact &c = r.pt[i];
	const CollPartRef &pr = B.parts[side ? c.partB : c.partA];
	if (m_h <= 0.0) return Vector ();
	Vector p = side ? c.pB : c.pA, n;
	ToPartFrame (r, i, side, p, n);
	Vector v = CollPoseVel (pr.P0, pr.P1, pr.c, tau, p);
	if (v.length2 () == 0.0) return Vector ();
	return mul (QMat (B.m.Rot (tau)), v)/m_h;
}

CollOwnerKey CollDetect::Owner (const CollPairResult &r, int i, int side) const
{
	const CollBody &B = m_body[side ? r.bodyB : r.bodyA];
	return B.parts[side ? r.pt[i].partB : r.pt[i].partA].owner;
}

CollPairStore &CollDetect::Pairs ()
{
	return m_store;
}

void CollDetect::Reset ()
{
	m_store.Clear ();
	m_support.clear ();
	m_front.clear ();
	m_resPrev.clear (); m_resCur.clear ();
	m_warnPrev.clear (); m_warnCur.clear ();
	m_supDist.clear ();
	m_info.clear ();
	m_specLog.clear ();
	m_badPrev.clear (); m_badCur.clear ();
}

int CollDetect::nBody () const
{
	return (int)m_body.size ();
}

const CollBody &CollDetect::Body (int i) const
{
	return m_body[i];
}

bool CollDetect::Embedded (const CollEmbedQuery &q)
{
	{
		CollPairEntry &e = m_store.Get (q.parent, q.child);
		auto it = std::lower_bound (e.embed.begin (), e.embed.end (), q.key, [] (const std::pair<uint64_t, uint8_t> &x, uint64_t k) { return x.first < k; });
		if (it != e.embed.end () && it->first == q.key) return it->second != 0;
	}
	Impl::Ctx cx;
	cx.d = this; cx.ia = cx.ib = -1; cx.A = cx.B = nullptr;
	cx.s0 = 0.0; cx.span = 1.0; cx.flags = 0; cx.tt = cx.bv = 0; cx.mode = Impl::POSE_T1; cx.tau = 1.0;
	size_t vb = 0;
	bool mountRigid = CollIsRigid (q.mount);
	for (size_t k = 0; k < q.np; k++) {
		Impl::Placed p {};
		p.r = q.pp + k; p.g = q.pp[k].geom; p.idx = (int)k;
		if (!p.g || p.g->node.empty ()) continue;
		p.X = q.pp[k].P1; p.orth = CollIsRigid (p.X);
		p.vbase = vb; vb += p.g->vtx.size ();
		cx.pa.push_back (p);
	}
	for (size_t k = 0; k < q.nc; k++) {
		Impl::Placed p {};
		p.r = q.cp + k; p.g = q.cp[k].geom; p.idx = (int)k;
		if (!p.g || p.g->node.empty ()) continue;
		p.X = CollCompose (q.mount, q.cp[k].P1); p.orth = mountRigid && CollIsRigid (q.cp[k].P1);
		p.vbase = vb; vb += p.g->vtx.size ();
		cx.pb.push_back (p);
	}
	if (m_vx.size () < vb) { m_vx.resize (vb); m_vs.resize (vb, 0); }
	for (size_t a = 0; a < cx.pa.size (); a++)
		for (size_t b = 0; b < cx.pb.size (); b++) {
			Impl::PP pp {};
			pp.a = (int)a; pp.b = (int)b; pp.key = OwnerPair (q.parent, q.child);
			cx.pp.push_back (pp);
		}
	Impl::Stamp (*this);
	bool hit = false;
	std::vector<Impl::Raw> raws;
	for (size_t k = 0; k < cx.pp.size () && !hit; k++) {
		Impl::Cutoff (cx, k, 0.0, Impl::Q_RAWX | Impl::Q_FIRST, raws, 1, nullptr);
		hit = !raws.empty ();
	}
	CollPairEntry &e = m_store.Get (q.parent, q.child);
	auto it = std::lower_bound (e.embed.begin (), e.embed.end (), q.key, [] (const std::pair<uint64_t, uint8_t> &x, uint64_t k) { return x.first < k; });
	e.embed.insert (it, { q.key, (uint8_t)(hit ? 1 : 0) });
	return hit;
}
