// not upstream: collision geometry (D1 1-2): weld, filters, AABB tree, refit, queries, pose paths

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include "CollGeom.h"

namespace {

const double BIG = 1e300;
const uint32_t NONE = 0xffffffffu;
const double BARY_EPS = 1e-12;   // ray cast: barycentric slack so rays through shared edges cannot slip between welded triangles

struct Box { double mn[3], mx[3]; };

inline void BoxEmpty (Box &b)
{
	for (int k = 0; k < 3; k++) b.mn[k] = BIG, b.mx[k] = -BIG;
}

inline void BoxAdd (Box &b, const Vector &p)
{
	for (int k = 0; k < 3; k++) {
		if (p.data[k] < b.mn[k]) b.mn[k] = p.data[k];
		if (p.data[k] > b.mx[k]) b.mx[k] = p.data[k];
	}
}

inline void BoxAdd (Box &b, const double *mn, const double *mx)
{
	for (int k = 0; k < 3; k++) {
		if (mn[k] < b.mn[k]) b.mn[k] = mn[k];
		if (mx[k] > b.mx[k]) b.mx[k] = mx[k];
	}
}

inline double BoxArea (const Box &b)
{
	double dx = b.mx[0]-b.mn[0], dy = b.mx[1]-b.mn[1], dz = b.mx[2]-b.mn[2];
	return dx*dy + dy*dz + dz*dx;
}

// sum of extents: grows for flat and line-like boxes too (a volume stays 0 there)
inline double NodeSize (const CollNode &n)
{
	return (n.mx[0]-n.mn[0]) + (n.mx[1]-n.mn[1]) + (n.mx[2]-n.mn[2]);
}

inline double BoxDist2 (const Box &a, const Box &b)
{
	double s = 0.0;
	for (int k = 0; k < 3; k++) {
		double d = std::max (b.mn[k] - a.mx[k], a.mn[k] - b.mx[k]);
		if (d > 0.0) s += d*d;
	}
	return s;
}

inline bool IsIdentity (const CollAffine &X)
{
	static_assert (sizeof (X.A.data) == 9 * sizeof (double), "Matrix layout");
	const double *a = X.A.data;
	return a[0] == 1.0 && a[1] == 0.0 && a[2] == 0.0 && a[3] == 0.0 && a[4] == 1.0 && a[5] == 0.0 &&
		a[6] == 0.0 && a[7] == 0.0 && a[8] == 1.0 && X.t.x == 0.0 && X.t.y == 0.0 && X.t.z == 0.0;
}

// AABB of an affine image of a node box (Arvo), padded a few ulps so rounded transforms stay inside
inline void XBox (const CollNode &n, const CollAffine &X, bool ident, Box &o)
{
	if (ident) {
		for (int k = 0; k < 3; k++) o.mn[k] = n.mn[k], o.mx[k] = n.mx[k];
		return;
	}
	double c[3], h[3];
	for (int k = 0; k < 3; k++) c[k] = 0.5*(n.mn[k]+n.mx[k]), h[k] = 0.5*(n.mx[k]-n.mn[k]);
	const double *a = X.A.data;
	for (int i = 0; i < 3; i++) {
		double cc = X.t.data[i] + a[3*i]*c[0] + a[3*i+1]*c[1] + a[3*i+2]*c[2];
		double e = std::fabs (a[3*i])*h[0] + std::fabs (a[3*i+1])*h[1] + std::fabs (a[3*i+2])*h[2];
		e += 1e-14 * (e + std::fabs (cc) + std::fabs (a[3*i])*std::fabs (c[0]) + std::fabs (a[3*i+1])*std::fabs (c[1]) + std::fabs (a[3*i+2])*std::fabs (c[2]));
		o.mn[i] = cc - e, o.mx[i] = cc + e;
	}
}

inline double BoxSize (const Box &b)
{
	return (b.mx[0]-b.mn[0]) + (b.mx[1]-b.mn[1]) + (b.mx[2]-b.mn[2]);
}

inline void TriBox (const Vector *t, Box &b)
{
	for (int k = 0; k < 3; k++) {
		b.mn[k] = std::min (std::min (t[0].data[k], t[1].data[k]), t[2].data[k]);
		b.mx[k] = std::max (std::max (t[0].data[k], t[1].data[k]), t[2].data[k]);
	}
}

inline uint64_t Mix (uint64_t x)
{
	x += 0x9e3779b97f4a7c15ull;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
	return x ^ (x >> 31);
}

inline size_t Pow2Above (size_t n)
{
	size_t c = 16;
	while (c < n) c <<= 1;
	return c;
}

inline double Clamp01 (double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

// closest point on triangle abc to p (Ericson 5.1.5); abc must not be a sliver
Vector ClosestPtTri (const Vector &p, const Vector &a, const Vector &b, const Vector &c)
{
	Vector ab = b - a, ac = c - a, ap = p - a;
	double d1 = dotp (ab, ap), d2 = dotp (ac, ap);
	if (d1 <= 0.0 && d2 <= 0.0) return a;
	Vector bp = p - b;
	double d3 = dotp (ab, bp), d4 = dotp (ac, bp);
	if (d3 >= 0.0 && d4 <= d3) return b;
	double vc = d1*d4 - d3*d2;
	if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab * (d1 / (d1 - d3));
	Vector cp = p - c;
	double d5 = dotp (ab, cp), d6 = dotp (ac, cp);
	if (d6 >= 0.0 && d5 <= d6) return c;
	double vb = d5*d2 - d1*d6;
	if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac * (d2 / (d2 - d6));
	double va = d3*d6 - d5*d4;
	if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
	double den = 1.0 / (va + vb + vc);
	return a + ab * (vb*den) + ac * (vc*den);
}

// closest point on segment pq to x
inline Vector ClosestPtSeg (const Vector &x, const Vector &p, const Vector &q)
{
	Vector d = q - p;
	double dd = dotp (d, d);
	if (!(dd > 0.0)) return p;
	return p + d * Clamp01 (dotp (x - p, d) / dd);
}

// closest points of segments p1q1 and p2q2 (Ericson 5.1.9) with one re-projection pass
void SegSeg (const Vector &p1, const Vector &q1, const Vector &p2, const Vector &q2, Vector &c1, Vector &c2)
{
	Vector d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
	double a = dotp (d1, d1), e = dotp (d2, d2), f = dotp (d2, r);
	double s, t;
	if (a <= 0.0 && e <= 0.0) { c1 = p1; c2 = p2; return; }
	if (a <= 0.0) {
		s = 0.0; t = Clamp01 (f / e);
	} else {
		double c = dotp (d1, r);
		if (e <= 0.0) {
			t = 0.0; s = Clamp01 (-c / a);
		} else {
			double b = dotp (d1, d2), den = a*e - b*b;
			s = (den > 0.0) ? Clamp01 ((b*f - c*e) / den) : 0.0;
			t = (b*s + f) / e;
			if (t < 0.0) { t = 0.0; s = Clamp01 (-c / a); }
			else if (t > 1.0) { t = 1.0; s = Clamp01 ((b - c) / a); }
			else {
				s = Clamp01 ((b*t - c) / a);
				t = Clamp01 ((b*s + f) / e);
			}
		}
	}
	c1 = p1 + d1 * s;
	c2 = p2 + d2 * t;
}

// segment pq exactly in triangle t's plane: 2D overlap, projected without the dominant normal axis
bool CoplanarEdgeFace (const Vector &p, const Vector &q, const Vector *t, const Vector &n, Vector &x)
{
	double ax = std::fabs (n.x), ay = std::fabs (n.y), az = std::fabs (n.z);
	int k = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
	int iu = (k + 1) % 3, iv = (k + 2) % 3;
	auto orient = [&](const Vector &a, const Vector &b, const Vector &c) {
		return (b.data[iu] - a.data[iu]) * (c.data[iv] - a.data[iv]) - (b.data[iv] - a.data[iv]) * (c.data[iu] - a.data[iu]);
	};
	double s = orient (t[0], t[1], t[2]) > 0.0 ? 1.0 : -1.0;
	auto inside = [&](const Vector &c) {
		for (int i = 0; i < 3; i++) if (s * orient (t[i], t[(i+1)%3], c) < 0.0) return false;
		return true;
	};
	if (inside (p)) { x = p; return true; }
	if (inside (q)) { x = q; return true; }
	for (int i = 0; i < 3; i++) {
		const Vector &a = t[i], &b = t[(i+1)%3];
		double oa = orient (p, q, a), ob = orient (p, q, b), op = orient (a, b, p), oq = orient (a, b, q);
		if ((oa > 0.0 && ob > 0.0) || (oa < 0.0 && ob < 0.0) || (op > 0.0 && oq > 0.0) || (op < 0.0 && oq < 0.0)) continue;
		if (op == 0.0 && oq == 0.0) {
			// collinear: with p and q outside, an overlap puts a or b on pq
			Vector d = q - p;
			double dd = dotp (d, d);
			for (const Vector *c : { &a, &b }) {
				double tc = dd > 0.0 ? dotp (*c - p, d) / dd : -1.0;
				if (tc >= 0.0 && tc <= 1.0) { x = *c; return true; }
			}
			continue;
		}
		x = p + (q - p) * (op / (op - oq));
		return true;
	}
	return false;
}

// segment pq crossing the face of triangle t (normal n, not a sliver); x = crossing point
bool EdgeThroughFace (const Vector &p, const Vector &q, const Vector *t, const Vector &n, Vector &x)
{
	double dp = dotp (n, p - t[0]), dq = dotp (n, q - t[0]);
	if ((dp > 0.0 && dq > 0.0) || (dp < 0.0 && dq < 0.0)) return false;
	if (dp == 0.0 && dq == 0.0) return CoplanarEdgeFace (p, q, t, n, x);
	x = p + (q - p) * (dp / (dp - dq));
	for (int i = 0; i < 3; i++) {
		const Vector &u = t[i], &v = t[(i+1)%3];
		if (dotp (n, crossp (v - u, x - u)) < 0.0) return false;
	}
	return true;
}

// closest point on a triangle given by vertices; slivers as their edges
inline Vector TriClosest (const Vector &p, const Vector *t, bool face)
{
	if (face) return ClosestPtTri (p, t[0], t[1], t[2]);
	Vector best = ClosestPtSeg (p, t[0], t[1]);
	double b2 = (best - p).length2();
	for (int i = 1; i < 3; i++) {
		Vector q = ClosestPtSeg (p, t[i], t[(i+1)%3]);
		double d2 = (q - p).length2();
		if (d2 < b2) b2 = d2, best = q;
	}
	return best;
}

inline bool HasFace (const Vector *t)
{
	return crossp (t[1] - t[0], t[2] - t[0]).length2() >= COLL_DEGEN_CROSS2;
}

// scratch stamps for the lazy vertex transform (D1 1.5)
void PrepScratch (CollScratch &s, size_t na, size_t nb)
{
	if (++s.stamp == 0) {
		std::fill (s.sa.begin(), s.sa.end(), 0u);
		std::fill (s.sb.begin(), s.sb.end(), 0u);
		s.stamp = 1;
	}
	if (s.xa.size() < na) s.xa.resize (na);
	if (s.sa.size() < na) s.sa.resize (na, 0u);
	if (s.xb.size() < nb) s.xb.resize (nb);
	if (s.sb.size() < nb) s.sb.resize (nb, 0u);
}

inline const Vector &XVtx (const CollGeom &G, const CollAffine &X, bool ident, std::vector<Vector> &xv, std::vector<uint32_t> &st, uint32_t stamp, uint32_t v)
{
	if (st[v] != stamp) {
		xv[v] = ident ? G.vtx[v] : CollApply (X, G.vtx[v]);
		st[v] = stamp;
	}
	return xv[v];
}

// one triangle of a leaf in the pair frame
struct LeafTri { uint32_t id; Vector v[3]; Box b; };

inline int LoadLeaf (const CollGeom &G, const CollNode &n, const CollAffine &X, bool ident, const uint8_t *mask,
	std::vector<Vector> &xv, std::vector<uint32_t> &st, uint32_t stamp, LeafTri *out)
{
	int k = 0;
	for (uint32_t i = n.first; i < n.first + n.count; i++) {
		uint32_t t = G.perm[i];
		const CollTri &T = G.tri[t];
		if (mask && mask[T.src]) continue;
		LeafTri &L = out[k++];
		L.id = t;
		for (int j = 0; j < 3; j++) L.v[j] = XVtx (G, X, ident, xv, st, stamp, T.v[j]);
		TriBox (L.v, L.b);
	}
	return k;
}

// node pair traversal (distance, pairs, overlap); leaf(ta, tb, d, pa, pb) true stops, may cut bound
template <class LEAF>
void PairWalk (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB, CollScratch &s,
	const uint8_t *maskA, const uint8_t *maskB, const double &bound, LEAF leaf)
{
	if (A.node.empty() || B.node.empty()) return;
	PrepScratch (s, A.vtx.size(), B.vtx.size());
	bool idA = IsIdentity (XA), idB = IsIdentity (XB);
	s.stack.clear();
	s.stack.push_back (std::make_pair (0u, 0u));
	LeafTri la[COLL_BVH_LEAF*8], lb[COLL_BVH_LEAF*8];
	while (!s.stack.empty()) {
		std::pair<uint32_t,uint32_t> pr = s.stack.back();
		s.stack.pop_back();
		const CollNode &na = A.node[pr.first], &nb = B.node[pr.second];
		Box ba, bb;
		XBox (na, XA, idA, ba);
		XBox (nb, XB, idB, bb);
		if (BoxDist2 (ba, bb) >= bound*bound) continue;
		if (na.count && nb.count) {
			if (na.count > COLL_BVH_LEAF*8 || nb.count > COLL_BVH_LEAF*8) {
				for (uint32_t i = na.first; i < na.first + na.count; i++) {
					uint32_t ta = A.perm[i];
					if (maskA && maskA[A.tri[ta].src]) continue;
					Vector va[3];
					for (int j = 0; j < 3; j++) va[j] = XVtx (A, XA, idA, s.xa, s.sa, s.stamp, A.tri[ta].v[j]);
					for (uint32_t k = nb.first; k < nb.first + nb.count; k++) {
						uint32_t tb = B.perm[k];
						if (maskB && maskB[B.tri[tb].src]) continue;
						Vector vb[3];
						for (int j = 0; j < 3; j++) vb[j] = XVtx (B, XB, idB, s.xb, s.sb, s.stamp, B.tri[tb].v[j]);
						Vector pa, pb;
						double d = CollTriTriDistance (va, vb, pa, pb);
						if (d < bound && leaf (ta, tb, d, pa, pb)) return;
					}
				}
				continue;
			}
			int nla = LoadLeaf (A, na, XA, idA, maskA, s.xa, s.sa, s.stamp, la);
			if (!nla) continue;
			int nlb = LoadLeaf (B, nb, XB, idB, maskB, s.xb, s.sb, s.stamp, lb);
			for (int i = 0; i < nla; i++)
				for (int k = 0; k < nlb; k++) {
					if (BoxDist2 (la[i].b, lb[k].b) >= bound*bound) continue;
					Vector pa, pb;
					double d = CollTriTriDistance (la[i].v, lb[k].v, pa, pb);
					if (d < bound && leaf (la[i].id, lb[k].id, d, pa, pb)) return;
				}
			continue;
		}
		bool splitA = nb.count ? true : (na.count ? false : BoxSize (ba) >= BoxSize (bb));
		std::pair<uint32_t,uint32_t> c0, c1;
		Box x0, x1;
		double l0, l1;
		if (splitA) {
			c0 = std::make_pair (na.first, pr.second), c1 = std::make_pair (na.first + 1, pr.second);
			XBox (A.node[na.first], XA, idA, x0);
			XBox (A.node[na.first + 1], XA, idA, x1);
			l0 = BoxDist2 (x0, bb), l1 = BoxDist2 (x1, bb);
		} else {
			c0 = std::make_pair (pr.first, nb.first), c1 = std::make_pair (pr.first, nb.first + 1);
			XBox (B.node[nb.first], XB, idB, x0);
			XBox (B.node[nb.first + 1], XB, idB, x1);
			l0 = BoxDist2 (ba, x0), l1 = BoxDist2 (ba, x1);
		}
		double b2 = bound*bound;
		if (l0 <= l1) {
			if (l1 < b2) s.stack.push_back (c1);
			if (l0 < b2) s.stack.push_back (c0);
		} else {
			if (l0 < b2) s.stack.push_back (c0);
			if (l1 < b2) s.stack.push_back (c1);
		}
	}
}

inline bool HitLess (const CollHit &a, const CollHit &b)
{
	return a.ta < b.ta || (a.ta == b.ta && a.tb < b.tb);
}

// rotation vector of a (near) rotation matrix, angle in [0, pi] (shortest arc)
Vector RotLog (const Matrix &Q)
{
	double t = Q.m11 + Q.m22 + Q.m33, w, x, y, z, s;
	if (t >= Q.m11 && t >= Q.m22 && t >= Q.m33) {
		s = 2.0 * std::sqrt (std::max (1.0 + t, 0.0));
		w = 0.25*s; x = (Q.m32 - Q.m23)/s; y = (Q.m13 - Q.m31)/s; z = (Q.m21 - Q.m12)/s;
	} else if (Q.m11 >= Q.m22 && Q.m11 >= Q.m33) {
		s = 2.0 * std::sqrt (std::max (1.0 + Q.m11 - Q.m22 - Q.m33, 0.0));
		w = (Q.m32 - Q.m23)/s; x = 0.25*s; y = (Q.m12 + Q.m21)/s; z = (Q.m13 + Q.m31)/s;
	} else if (Q.m22 >= Q.m33) {
		s = 2.0 * std::sqrt (std::max (1.0 + Q.m22 - Q.m11 - Q.m33, 0.0));
		w = (Q.m13 - Q.m31)/s; x = (Q.m12 + Q.m21)/s; y = 0.25*s; z = (Q.m23 + Q.m32)/s;
	} else {
		s = 2.0 * std::sqrt (std::max (1.0 + Q.m33 - Q.m11 - Q.m22, 0.0));
		w = (Q.m21 - Q.m12)/s; x = (Q.m13 + Q.m31)/s; y = (Q.m23 + Q.m32)/s; z = 0.25*s;
	}
	if (w < 0.0) w = -w, x = -x, y = -y, z = -z;
	double vn = std::sqrt (x*x + y*y + z*z);
	if (!(vn > 0.0)) return Vector (0, 0, 0);
	double th = 2.0 * std::atan2 (vn, w);
	return Vector (x, y, z) * (th / vn);
}

// exp of the skew matrix of phi (Rodrigues)
Matrix RotExp (const Vector &phi)
{
	double th = phi.length();
	if (!(th > 0.0)) return IMatrix ();
	Vector n = phi / th;
	double s = std::sin (th), h = std::sin (0.5*th), c1 = 2.0*h*h;
	return Matrix (
		1.0 + c1*(n.x*n.x - 1.0), -s*n.z + c1*n.x*n.y,       s*n.y + c1*n.x*n.z,
		s*n.z + c1*n.x*n.y,        1.0 + c1*(n.y*n.y - 1.0), -s*n.x + c1*n.y*n.z,
		-s*n.y + c1*n.x*n.z,       s*n.x + c1*n.y*n.z,        1.0 + c1*(n.z*n.z - 1.0));
}

inline Matrix MatLin (const Matrix &A, double a, const Matrix &B, double b)
{
	Matrix M;
	for (int i = 0; i < 9; i++) M.data[i] = A.data[i]*a + B.data[i]*b;
	return M;
}

inline double Frob (const Matrix &A)
{
	double s = 0.0;
	for (int i = 0; i < 9; i++) s += A.data[i]*A.data[i];
	return std::sqrt (s);
}

// Frobenius norm of A^T A - I
double OrthoDev (const Matrix &A)
{
	double s = 0.0;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			double v = A.data[i]*A.data[j] + A.data[3+i]*A.data[3+j] + A.data[6+i]*A.data[6+j] - (i == j ? 1.0 : 0.0);
			s += v*v;
		}
	return std::sqrt (s);
}

inline double Det (const Matrix &A)
{
	return A.m11*(A.m22*A.m33 - A.m32*A.m23) - A.m12*(A.m21*A.m33 - A.m31*A.m23) + A.m13*(A.m21*A.m32 - A.m31*A.m22);
}

inline bool SamePose (const CollAffine &P0, const CollAffine &P1)
{
	for (int i = 0; i < 9; i++) if (P0.A.data[i] != P1.A.data[i]) return false;
	return P0.t.x == P1.t.x && P0.t.y == P1.t.y && P0.t.z == P1.t.z;
}

// rigid path: P(tau)(x) = lerp(P0(c), P1(c)) + (exp(tau [phi]) R0 + tau E)(x - c); E: P(1) = P1
struct RigidPath { Vector c0, c1, phi; Matrix R0, E; };

void RigidSetup (const CollAffine &P0, const CollAffine &P1, const Vector &c, RigidPath &r)
{
	r.c0 = CollApply (P0, c);
	r.c1 = CollApply (P1, c);
	r.R0 = P0.A;
	r.phi = RotLog (P1.A * transp (P0.A));
	r.E = MatLin (P1.A, 1.0, RotExp (r.phi) * P0.A, -1.0);
}

} // namespace

bool CollGeom::Build (const CollSrcGroup *src, size_t nsrc, double weldTol, CollBuildStats *st)
{
	auto clk0 = std::chrono::steady_clock::now();
	CollBuildStats S;
	std::memset (&S, 0, sizeof (S));
	vtx.clear(); rest.clear(); tri.clear(); perm.clear(); node.clear(); srcTab.clear(); refOfs.clear(); ref.clear();
	bsCentre = Vector (0, 0, 0); bsRadius = 0.0; builtSize = 0.0;
	version++;
	if (!src) nsrc = 0;

	// render vertex numbering: base[i] + k
	std::vector<size_t> base (nsrc + 1, 0);
	for (size_t i = 0; i < nsrc; i++) {
		srcTab.push_back (src[i].src);
		size_t nv = src[i].g ? src[i].g->vtx.size() : 0;
		size_t nt = src[i].g ? src[i].g->idx.size() / 3 : 0;
		base[i+1] = base[i] + nv;
		S.ntriIn += (uint32_t)nt;
	}
	size_t nrv = base[nsrc];
	S.nrender = (uint32_t)nrv;

	// weld per scope: grid cell 2*tol, 8 cells probed, lowest welded index within tol wins (D1 2)
	double tol = (weldTol > 0.0) ? weldTol : 0.0;
	double tol2 = tol*tol;
	double cell = std::max (2.0*tol, 1e-6);
	std::vector<uint32_t> wmap (nrv, NONE);
	std::vector<int64_t> wcell;
	std::vector<uint32_t> wscope, wnext;
	std::vector<uint32_t> table (Pow2Above (2*nrv + 1), NONE);
	size_t tmask = table.size() - 1;
	auto cellHash = [](int64_t x, int64_t y, int64_t z, uint32_t sc) {
		return Mix ((uint64_t)x * 0x9e3779b97f4a7c15ull ^ Mix ((uint64_t)y + 0x632be59bd9b4e019ull) ^ Mix ((uint64_t)z * 0x85ebca6bull + 0xc2b2ae3d27d4eb4full) ^ Mix ((uint64_t)sc + 0x165667b19e3779f9ull));
	};
	vtx.reserve (nrv);
	for (size_t i = 0; i < nsrc; i++) {
		if (!src[i].g) continue;
		const std::vector<CollVtx> &gv = src[i].g->vtx;
		uint32_t sc = src[i].src.scope;
		for (size_t k = 0; k < gv.size(); k++) {
			Vector p ((double)gv[k].x, (double)gv[k].y, (double)gv[k].z);
			bool ok = true;
			for (int a = 0; a < 3; a++)
				if (!std::isfinite (p.data[a]) || std::fabs (p.data[a]) > COLL_COORD_MAX) ok = false;
			if (!ok) continue;
			int64_t c[3], o[3];
			for (int a = 0; a < 3; a++) {
				double u = p.data[a] / cell, f = std::floor (u);
				c[a] = (int64_t)f;
				o[a] = (u - f < 0.5) ? -1 : 1;
			}
			uint32_t found = NONE;
			for (int m = 0; m < 8; m++) {
				int64_t x = c[0] + ((m & 1) ? o[0] : 0), y = c[1] + ((m & 2) ? o[1] : 0), z = c[2] + ((m & 4) ? o[2] : 0);
				size_t h = (size_t)cellHash (x, y, z, sc) & tmask;
				for (;; h = (h + 1) & tmask) {
					uint32_t hd = table[h];
					if (hd == NONE) break;
					if (wcell[3*hd] != x || wcell[3*hd+1] != y || wcell[3*hd+2] != z || wscope[hd] != sc) continue;
					for (uint32_t w = hd; w != NONE; w = wnext[w])
						if (w < found && (vtx[w] - p).length2() <= tol2) found = w;
					break;
				}
			}
			if (found == NONE) {
				found = (uint32_t)vtx.size();
				vtx.push_back (p);
				wcell.push_back (c[0]); wcell.push_back (c[1]); wcell.push_back (c[2]);
				wscope.push_back (sc);
				size_t h = (size_t)cellHash (c[0], c[1], c[2], sc) & tmask;
				for (;; h = (h + 1) & tmask) {
					uint32_t hd = table[h];
					if (hd == NONE) { table[h] = found; wnext.push_back (NONE); break; }
					if (wcell[3*hd] == c[0] && wcell[3*hd+1] == c[1] && wcell[3*hd+2] == c[2] && wscope[hd] == sc) {
						wnext.push_back (hd); table[h] = found; break;
					}
				}
			}
			wmap[base[i] + k] = found;
		}
	}
	std::vector<uint32_t> ().swap (table);
	std::vector<int64_t> ().swap (wcell);
	S.nweld = (uint32_t)vtx.size();

	// render refs, CSR in render order
	refOfs.assign (vtx.size() + 1, 0u);
	for (size_t r = 0; r < nrv; r++) if (wmap[r] != NONE) refOfs[wmap[r] + 1]++;
	for (size_t w = 0; w < vtx.size(); w++) refOfs[w+1] += refOfs[w];
	ref.resize (refOfs[vtx.size()]);
	{
		std::vector<uint32_t> cur (refOfs.begin(), refOfs.end() - 1);
		for (size_t i = 0; i < nsrc; i++)
			for (size_t k = 0; k < base[i+1] - base[i]; k++) {
				uint32_t w = wmap[base[i] + k];
				if (w != NONE) ref[cur[w]++] = CollRef { (uint32_t)i, (uint32_t)k };
			}
	}

	// triangles in source order, filters of D1 1.2 step 3
	tri.reserve (S.ntriIn);
	std::vector<uint32_t> dup (Pow2Above (2*(size_t)S.ntriIn + 1), NONE);
	size_t dmask = dup.size() - 1;
	std::vector<uint32_t> skey;
	skey.reserve (3*(size_t)S.ntriIn);
	for (size_t i = 0; i < nsrc; i++) {
		if (!src[i].g) continue;
		const CollGroupData &g = *src[i].g;
		size_t nv = g.vtx.size(), nt = g.idx.size() / 3;
		for (size_t t = 0; t < nt; t++) {
			uint16_t i0 = g.idx[3*t], i1 = g.idx[3*t+1], i2 = g.idx[3*t+2];
			if (i0 >= nv || i1 >= nv || i2 >= nv) { S.dropIndex++; continue; }
			uint32_t w0 = wmap[base[i] + i0], w1 = wmap[base[i] + i1], w2 = wmap[base[i] + i2];
			if (w0 == NONE || w1 == NONE || w2 == NONE) { S.dropInvalid++; continue; }
			if (w0 == w1 || w1 == w2 || w0 == w2) { S.dropDegen++; continue; }
			if (crossp (vtx[w1] - vtx[w0], vtx[w2] - vtx[w0]).length2() < COLL_DEGEN_CROSS2) { S.dropDegen++; continue; }
			uint32_t k0 = std::min (std::min (w0, w1), w2), k2 = std::max (std::max (w0, w1), w2), k1 = w0 ^ w1 ^ w2 ^ k0 ^ k2;
			size_t h = (size_t)Mix (((uint64_t)k0 << 32 | k1) ^ Mix (k2)) & dmask;
			bool isdup = false;
			for (;; h = (h + 1) & dmask) {
				uint32_t e = dup[h];
				if (e == NONE) { dup[h] = (uint32_t)tri.size(); break; }
				if (skey[3*e] == k0 && skey[3*e+1] == k1 && skey[3*e+2] == k2) { isdup = true; break; }
			}
			if (isdup) { S.dropDup++; continue; }
			skey.push_back (k0); skey.push_back (k1); skey.push_back (k2);
			CollTri T;
			T.v[0] = w0; T.v[1] = w1; T.v[2] = w2;
			T.src = (uint32_t)i;
			T.otri = (uint32_t)t;
			T.ov[0] = i0; T.ov[1] = i1; T.ov[2] = i2;
			T.flags = 0;
			tri.push_back (T);
		}
	}
	std::vector<uint32_t> ().swap (dup);
	std::vector<uint32_t> ().swap (skey);
	auto clk1 = std::chrono::steady_clock::now();

	BuildTree ();
	if (!node.empty()) {
		const CollNode &r = node[0];
		bsCentre = Vector (0.5*(r.mn[0]+r.mx[0]), 0.5*(r.mn[1]+r.mx[1]), 0.5*(r.mn[2]+r.mx[2]));
		double r2 = 0.0;
		for (const CollTri &T : tri)
			for (int j = 0; j < 3; j++) r2 = std::max (r2, (vtx[T.v[j]] - bsCentre).length2());
		bsRadius = std::sqrt (r2);
	}
	tri.shrink_to_fit(); perm.shrink_to_fit(); vtx.shrink_to_fit(); ref.shrink_to_fit(); refOfs.shrink_to_fit();
	auto clk2 = std::chrono::steady_clock::now();

	S.ntri = (uint32_t)tri.size();
	S.nnode = (uint32_t)node.size();
	S.msWeld = std::chrono::duration<double, std::milli> (clk1 - clk0).count();
	S.msTree = std::chrono::duration<double, std::milli> (clk2 - clk1).count();
	if (st) *st = S;
	return !tri.empty();
}

void CollGeom::BuildTree ()
{
	size_t n = tri.size();
	perm.resize (n);
	for (size_t i = 0; i < n; i++) perm[i] = (uint32_t)i;
	node.clear();
	builtSize = 0.0;
	if (!n) return;

	// per triangle box and centroid
	std::vector<double> tb (6*n), tc (3*n);
	for (size_t i = 0; i < n; i++) {
		const Vector &a = vtx[tri[i].v[0]], &b = vtx[tri[i].v[1]], &c = vtx[tri[i].v[2]];
		for (int k = 0; k < 3; k++) {
			tb[6*i+k] = std::min (std::min (a.data[k], b.data[k]), c.data[k]);
			tb[6*i+3+k] = std::max (std::max (a.data[k], b.data[k]), c.data[k]);
			tc[3*i+k] = (a.data[k] + b.data[k] + c.data[k]) / 3.0;
		}
	}
	std::vector<uint32_t> tmp (n);
	struct Job { uint32_t node, first, count, depth; };
	std::vector<Job> stack;
	node.reserve (n);
	node.push_back (CollNode {});
	stack.push_back (Job { 0, 0, (uint32_t)n, 0 });
	const int NB = COLL_BVH_BINS;
	while (!stack.empty()) {
		Job j = stack.back();
		stack.pop_back();
		Box b, cb;
		BoxEmpty (b); BoxEmpty (cb);
		for (uint32_t i = j.first; i < j.first + j.count; i++) {
			uint32_t t = perm[i];
			BoxAdd (b, &tb[6*t], &tb[6*t+3]);
			BoxAdd (cb, &tc[3*t], &tc[3*t]);
		}
		CollNode &N = node[j.node];
		for (int k = 0; k < 3; k++) N.mn[k] = b.mn[k], N.mx[k] = b.mx[k];
		if (j.count <= (uint32_t)COLL_BVH_LEAF || j.depth >= (uint32_t)COLL_BVH_DEPTH) {
			N.first = j.first; N.count = j.count;
			continue;
		}

		// binned SAH on centroids
		double best = BIG;
		int bax = -1, bsp = 0;
		for (int ax = 0; ax < 3; ax++) {
			double lo = cb.mn[ax], ext = cb.mx[ax] - lo;
			if (!(ext > 0.0)) continue;
			double sc = NB / ext;
			Box bb[COLL_BVH_BINS];
			uint32_t bc[COLL_BVH_BINS] = {};
			for (int q = 0; q < NB; q++) BoxEmpty (bb[q]);
			for (uint32_t i = j.first; i < j.first + j.count; i++) {
				uint32_t t = perm[i];
				int q = std::min (NB - 1, (int)((tc[3*t+ax] - lo) * sc));
				BoxAdd (bb[q], &tb[6*t], &tb[6*t+3]);
				bc[q]++;
			}
			double la[COLL_BVH_BINS];
			uint32_t lc[COLL_BVH_BINS];
			Box acc;
			BoxEmpty (acc);
			uint32_t cnt = 0;
			for (int q = 0; q < NB; q++) {
				BoxAdd (acc, bb[q].mn, bb[q].mx);
				cnt += bc[q];
				la[q] = cnt ? BoxArea (acc) : 0.0;
				lc[q] = cnt;
			}
			BoxEmpty (acc);
			cnt = 0;
			for (int q = NB - 1; q > 0; q--) {
				BoxAdd (acc, bb[q].mn, bb[q].mx);
				cnt += bc[q];
				if (!cnt || !lc[q-1]) continue;
				double cost = la[q-1]*lc[q-1] + BoxArea (acc)*cnt;
				if (cost < best || (cost == best && (ax < bax || (ax == bax && q-1 < bsp)))) best = cost, bax = ax, bsp = q-1;
			}
		}
		uint32_t mid = 0;
		if (bax >= 0 && best < BoxArea (b) * j.count) {
			double lo = cb.mn[bax], sc = NB / (cb.mx[bax] - lo);
			uint32_t nl = j.first, nr = 0;
			for (uint32_t i = j.first; i < j.first + j.count; i++) {
				uint32_t t = perm[i];
				if (std::min (NB - 1, (int)((tc[3*t+bax] - lo) * sc)) <= bsp) perm[nl++] = t;
				else tmp[nr++] = t;
			}
			std::copy (tmp.begin(), tmp.begin() + nr, perm.begin() + nl);
			mid = nl;
		}
		if (mid <= j.first || mid >= j.first + j.count) {
			// no split improves cost: median on the longest centroid axis, or half the range if centroids equal
			int ax = -1;
			double ext = 0.0;
			for (int k = 0; k < 3; k++) if (cb.mx[k] - cb.mn[k] > ext) ext = cb.mx[k] - cb.mn[k], ax = k;
			if (ax >= 0)
				std::sort (perm.begin() + j.first, perm.begin() + j.first + j.count, [&](uint32_t p, uint32_t q) {
					return tc[3*p+ax] < tc[3*q+ax] || (tc[3*p+ax] == tc[3*q+ax] && p < q); });
			mid = j.first + j.count / 2;
		}
		uint32_t l = (uint32_t)node.size();
		node.push_back (CollNode {});
		node.push_back (CollNode {});
		node[j.node].first = l;
		node[j.node].count = 0;
		stack.push_back (Job { l + 1, mid, j.first + j.count - mid, j.depth + 1 });
		stack.push_back (Job { l, j.first, mid - j.first, j.depth + 1 });
	}
	node.shrink_to_fit();
	builtSize = NodeSize (node[0]);
}

bool CollGeom::Refit ()
{
	if (node.empty()) return false;
	for (size_t k = node.size(); k-- > 0;) {
		CollNode &n = node[k];
		Box b;
		BoxEmpty (b);
		if (n.count) {
			for (uint32_t i = n.first; i < n.first + n.count; i++) {
				const CollTri &T = tri[perm[i]];
				for (int j = 0; j < 3; j++) BoxAdd (b, vtx[T.v[j]]);
			}
		} else {
			BoxAdd (b, node[n.first].mn, node[n.first].mx);
			BoxAdd (b, node[n.first+1].mn, node[n.first+1].mx);
		}
		for (int j = 0; j < 3; j++) n.mn[j] = b.mn[j], n.mx[j] = b.mx[j];
	}
	double r2 = 0.0;
	for (const CollTri &T : tri)
		for (int j = 0; j < 3; j++) r2 = std::max (r2, (vtx[T.v[j]] - bsCentre).length2());
	bsRadius = std::sqrt (r2);
	// no SAH rebuild: it would renumber leaves under D2's GRACE scopes and fronts; Build makes it tight
	return NodeSize (node[0]) > COLL_REBUILD_GROWTH * builtSize;
}

double CollTriTriDistance (const Vector a[3], const Vector b[3], Vector &pa, Vector &pb)
{
	// a NaN or inf coordinate would pass the crossing tests as 0: no finite answer, so no hit
	for (int i = 0; i < 3; i++)
		for (int k = 0; k < 3; k++)
			if (!std::isfinite (a[i].data[k]) || !std::isfinite (b[i].data[k])) { pa = a[0]; pb = b[0]; return std::numeric_limits<double>::infinity(); }
	Vector na = crossp (a[1] - a[0], a[2] - a[0]), nb = crossp (b[1] - b[0], b[2] - b[0]);
	bool fa = na.length2() >= COLL_DEGEN_CROSS2, fb = nb.length2() >= COLL_DEGEN_CROSS2;
	Vector x;
	if (fb)
		for (int i = 0; i < 3; i++)
			if (EdgeThroughFace (a[i], a[(i+1)%3], b, nb, x)) { pa = pb = x; return 0.0; }
	if (fa)
		for (int i = 0; i < 3; i++)
			if (EdgeThroughFace (b[i], b[(i+1)%3], a, na, x)) { pa = pb = x; return 0.0; }
	double best = BIG;
	if (fb)
		for (int i = 0; i < 3; i++) {
			Vector q = ClosestPtTri (a[i], b[0], b[1], b[2]);
			double d2 = (a[i] - q).length2();
			if (d2 < best) best = d2, pa = a[i], pb = q;
		}
	if (fa)
		for (int i = 0; i < 3; i++) {
			Vector q = ClosestPtTri (b[i], a[0], a[1], a[2]);
			double d2 = (b[i] - q).length2();
			if (d2 < best) best = d2, pa = q, pb = b[i];
		}
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			Vector p, q;
			SegSeg (a[i], a[(i+1)%3], b[j], b[(j+1)%3], p, q);
			double d2 = (p - q).length2();
			if (d2 < best) best = d2, pa = p, pb = q;
		}
	return (pa - pb).length();
}

double CollDistance (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB,
	double cap, CollHit *best, CollScratch &s, const uint8_t *maskA, const uint8_t *maskB)
{
	CollHit h { NONE, NONE, cap, Vector (), Vector () };
	double bound = cap;
	if (cap > 0.0)
		PairWalk (A, XA, B, XB, s, maskA, maskB, bound, [&](uint32_t ta, uint32_t tb, double d, const Vector &pa, const Vector &pb) {
			if (d < bound) { bound = d; h = CollHit { ta, tb, d, pa, pb }; }
			return bound <= 0.0;
		});
	if (best) *best = h;
	return bound;
}

size_t CollPairsWithin (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB,
	double d, std::vector<CollHit> &out, size_t maxOut, CollScratch &s, const uint8_t *maskA, const uint8_t *maskB)
{
	out.clear();
	if (!maxOut || !(d > 0.0)) return 0;
	PairWalk (A, XA, B, XB, s, maskA, maskB, d, [&](uint32_t ta, uint32_t tb, double dist, const Vector &pa, const Vector &pb) {
		out.push_back (CollHit { ta, tb, dist, pa, pb });
		return out.size() >= maxOut;
	});
	std::sort (out.begin(), out.end(), HitLess);
	return out.size();
}

bool CollOverlap (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB,
	double d, CollScratch &s, const uint8_t *maskA, const uint8_t *maskB)
{
	bool hit = false;
	if (d > 0.0)
		PairWalk (A, XA, B, XB, s, maskA, maskB, d, [&](uint32_t, uint32_t, double, const Vector &, const Vector &) {
			hit = true;
			return true;
		});
	return hit;
}

bool CollRayCast (const CollGeom &G, const CollAffine &X, const Vector &o, const Vector &d, double tmin, double tmax,
	CollRayHit &hit, const uint8_t *mask)
{
	if (G.node.empty() || !(tmax >= tmin)) return false;
	double det = Det (X.A);
	if (!(det != 0.0) || !std::isfinite (det)) return false;
	Matrix Ai = inv (X.A);
	Vector lo = mul (Ai, o - X.t), ld = mul (Ai, d);
	for (int k = 0; k < 3; k++) if (!std::isfinite (lo.data[k]) || !std::isfinite (ld.data[k])) return false;
	double id[3];
	for (int k = 0; k < 3; k++) id[k] = (ld.data[k] != 0.0) ? 1.0 / ld.data[k] : 0.0;
	double best = tmax;
	uint32_t bt = NONE;
	auto slab = [&](const CollNode &n, double &tn) {
		double t0 = tmin, t1 = best;
		for (int k = 0; k < 3; k++) {
			double pad = 1e-12 * (std::fabs (n.mn[k]) + std::fabs (n.mx[k]));
			double mn = n.mn[k] - pad, mx = n.mx[k] + pad;
			if (ld.data[k] == 0.0) {
				if (lo.data[k] < mn || lo.data[k] > mx) return false;
				continue;
			}
			double a = (mn - lo.data[k]) * id[k], b = (mx - lo.data[k]) * id[k];
			if (a > b) std::swap (a, b);
			if (a > t0) t0 = a;
			if (b < t1) t1 = b;
			if (t0 > t1) return false;
		}
		tn = t0;
		return true;
	};
	uint32_t stack[2*COLL_BVH_DEPTH + 8];
	int sp = 0;
	double tn;
	if (slab (G.node[0], tn)) stack[sp++] = 0;
	while (sp) {
		const CollNode &n = G.node[stack[--sp]];
		if (!slab (n, tn)) continue;
		if (n.count) {
			for (uint32_t i = n.first; i < n.first + n.count; i++) {
				uint32_t t = G.perm[i];
				const CollTri &T = G.tri[t];
				if (mask && mask[T.src]) continue;
				const Vector &v0 = G.vtx[T.v[0]], &v1 = G.vtx[T.v[1]], &v2 = G.vtx[T.v[2]];
				Vector e1 = v1 - v0, e2 = v2 - v0, p = crossp (ld, e2);
				double dt = dotp (e1, p);
				if (!(std::fabs (dt) > 1e-12 * ld.length() * e1.length() * e2.length())) continue;
				double inv = 1.0 / dt;
				Vector sv = lo - v0;
				double u = dotp (sv, p) * inv;
				if (u < -BARY_EPS || u > 1.0 + BARY_EPS) continue;
				Vector q = crossp (sv, e1);
				double v = dotp (ld, q) * inv;
				if (v < -BARY_EPS || u + v > 1.0 + BARY_EPS) continue;
				double th = dotp (e2, q) * inv;
				if (th < tmin || th > best) continue;
				if (th < best || t < bt) best = th, bt = t;
			}
			continue;
		}
		double ta, tb;
		bool ha = slab (G.node[n.first], ta), hb = slab (G.node[n.first+1], tb);
		if (ha && hb) {
			if (ta <= tb) { stack[sp++] = n.first + 1; stack[sp++] = n.first; }
			else { stack[sp++] = n.first; stack[sp++] = n.first + 1; }
		} else if (ha) stack[sp++] = n.first;
		else if (hb) stack[sp++] = n.first + 1;
	}
	if (bt == NONE) return false;
	hit.tri = bt;
	hit.t = best;
	hit.p = o + d * best;
	return true;
}

double CollClosestPoint (const CollGeom &G, const Vector &p, double cap, uint32_t &tri, Vector &q)
{
	tri = NONE;
	if (G.node.empty() || !(cap > 0.0)) return cap;
	double best2 = cap*cap;
	auto bdist2 = [&](const CollNode &n) {
		double s = 0.0;
		for (int k = 0; k < 3; k++) {
			double d = std::max (n.mn[k] - p.data[k], p.data[k] - n.mx[k]);
			if (d > 0.0) s += d*d;
		}
		return s;
	};
	uint32_t stack[2*COLL_BVH_DEPTH + 8];
	int sp = 0;
	stack[sp++] = 0;
	while (sp) {
		const CollNode &n = G.node[stack[--sp]];
		if (bdist2 (n) > best2) continue;
		if (n.count) {
			for (uint32_t i = n.first; i < n.first + n.count; i++) {
				uint32_t t = G.perm[i];
				const CollTri &T = G.tri[t];
				Vector v[3] = { G.vtx[T.v[0]], G.vtx[T.v[1]], G.vtx[T.v[2]] };
				Vector c = TriClosest (p, v, HasFace (v));
				double d2 = (c - p).length2();
				if (d2 < best2 || (d2 == best2 && tri != NONE && t < tri)) best2 = d2, tri = t, q = c;
			}
			continue;
		}
		double d0 = bdist2 (G.node[n.first]), d1 = bdist2 (G.node[n.first+1]);
		if (d0 <= d1) {
			if (d1 <= best2) stack[sp++] = n.first + 1;
			if (d0 <= best2) stack[sp++] = n.first;
		} else {
			if (d0 <= best2) stack[sp++] = n.first;
			if (d1 <= best2) stack[sp++] = n.first + 1;
		}
	}
	if (tri == NONE) return cap;
	double d = (q - p).length();
	if (!(d < cap)) { tri = NONE; return cap; }
	return d;
}

bool CollIsRigid (const CollAffine &P)
{
	return OrthoDev (P.A) < COLL_RIGID_TOL && Det (P.A) > 0.0;
}

CollAffine CollPoseAt (const CollAffine &P0, const CollAffine &P1, const Vector &c, double tau)
{
	if (tau == 0.0 || SamePose (P0, P1)) return P0;
	if (tau == 1.0) return P1;
	CollAffine P;
	if (CollIsRigid (P0) && CollIsRigid (P1)) {
		RigidPath r;
		RigidSetup (P0, P1, c, r);
		P.A = MatLin (RotExp (r.phi * tau) * r.R0, 1.0, r.E, tau);
		P.t = r.c0 * (1.0 - tau) + r.c1 * tau - mul (P.A, c);
	} else {
		P.A = MatLin (P0.A, 1.0 - tau, P1.A, tau);
		P.t = P0.t * (1.0 - tau) + P1.t * tau;
	}
	return P;
}

Vector CollPoseVel (const CollAffine &P0, const CollAffine &P1, const Vector &c, double tau, const Vector &x)
{
	if (SamePose (P0, P1)) return Vector (0, 0, 0);
	if (CollIsRigid (P0) && CollIsRigid (P1)) {
		RigidPath r;
		RigidSetup (P0, P1, c, r);
		Vector xc = x - c;
		return (r.c1 - r.c0) + crossp (r.phi, mul (RotExp (r.phi * tau) * r.R0, xc)) + mul (r.E, xc);
	}
	return CollApply (P1, x) - CollApply (P0, x);
}

double CollPoseMotion (const CollGeom &G, const CollAffine &P0, const CollAffine &P1)
{
	if (SamePose (P0, P1)) return 0.0;
	double m, scale;
	if (CollIsRigid (P0) && CollIsRigid (P1)) {
		const Vector &c = G.bsCentre;
		double rad = G.bsRadius;
		RigidPath r;
		RigidSetup (P0, P1, c, r);
		double th = std::min (r.phi.length(), Pi);
		m = (r.c1 - r.c0).length() + 2.0 * std::sin (0.5*th) * rad * std::sqrt (1.0 + OrthoDev (P0.A)) + Frob (r.E) * rad;
		scale = r.c0.length() + r.c1.length() + (Frob (P0.A) + Frob (P1.A)) * (c.length() + rad);
	} else {
		m = 0.0;
		double xmax = 0.0;
		if (G.node.empty()) {
			m = (CollApply (P1, G.bsCentre) - CollApply (P0, G.bsCentre)).length();
			xmax = G.bsCentre.length();
		} else {
			const CollNode &n = G.node[0];
			for (int k = 0; k < 8; k++) {
				Vector x ((k & 1) ? n.mx[0] : n.mn[0], (k & 2) ? n.mx[1] : n.mn[1], (k & 4) ? n.mx[2] : n.mn[2]);
				m = std::max (m, (CollApply (P1, x) - CollApply (P0, x)).length());
				xmax = std::max (xmax, x.length());
			}
		}
		scale = P0.t.length() + P1.t.length() + (Frob (P0.A) + Frob (P1.A)) * xmax;
	}
	return m + 1e-13 * scale;
}
