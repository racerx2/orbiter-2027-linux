// not upstream: collision geometry (D1): welded triangles, AABB tree, queries, poses; Orbiter-free

#ifndef __COLLGEOM_H
#define __COLLGEOM_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include "Vecmat.h"

// default values (D1 1.2, 1.4, 2)
constexpr double COLL_SKIN_DEFAULT   = 0.02;  // skin per collider [m]; contact gap 2x
constexpr double COLL_SKIN_MIN       = 0.01;  // sidecar SKIN range [m]
constexpr double COLL_SKIN_MAX       = 0.5;
constexpr double COLL_WELD_DEFAULT   = 1e-4;  // weld tolerance [m] (0.1 mm)
constexpr double COLL_WELD_MAX       = 0.01;  // sidecar WELD upper limit [m]
constexpr double COLL_COORD_MAX      = 1e6;   // a vertex beyond this |coordinate| is invalid [m]
constexpr double COLL_DEGEN_CROSS2   = 1e-24; // |cross|^2 below this: degenerate triangle, dropped at Build, edges only in distance code [m^4]
constexpr int    COLL_BVH_BINS       = 16;    // binned SAH
constexpr int    COLL_BVH_LEAF       = 4;     // triangles per leaf
constexpr int    COLL_BVH_DEPTH      = 64;    // depth cap
constexpr double COLL_REBUILD_GROWTH = 2.0;   // Refit flags a loose tree when root box size (sum of extents) > this x built size; never rebuilds
constexpr double COLL_RIGID_TOL      = 1e-9;  // CollIsRigid: |A^T A - I| below this and det A > 0

// log sink of the collision libs: CollWorld routes it to Orbiter.log, tests capture it, null drops
enum { COLLLOG_FINE = 0, COLLLOG_INFO = 1, COLLLOG_WARN = 2, COLLLOG_ERROR = 3 };
typedef void (*CollLogFn) (int level, const char *msg);
inline CollLogFn g_collLog = nullptr;
inline void CollLog (int level, const char *fmt, ...)
{
	if (!g_collLog) return;
	char buf[512];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (buf, sizeof (buf), fmt, ap);
	va_end (ap);
	g_collLog (level, buf);
}

// physical cross product a x b in Orbiter's left-handed frames (D3 2.4); one helper for D2 and D3
inline Vector Xc (const Vector &a, const Vector &b) { return crossp (b, a); }

// collision geometry types (D1 1.1)
struct CollVtx { float x, y, z, nx, ny, nz, tu, tv; };   // same layout as NTVERTEX (static_assert in CollSource.cpp)
static_assert (sizeof (CollVtx) == 32, "CollVtx must keep the NTVERTEX layout");
struct CollGroupData {                                   // one mesh or base-object group
	std::vector<CollVtx> vtx; std::vector<uint16_t> idx;
	uint32_t usrflag = 0;                                // core UsrFlag (FLAG line)
	int64_t texid = 0; uint8_t undersh = 0, groundsh = 0; // base objects: GetGroupSpec values
};
struct CollRestMesh {                                    // rest geometry of one mesh (X12)
	std::string name;                                    // mesh name, "" if anonymous
	std::vector<CollGroupData> grp;                      // core group order
	uint32_t nvtx = 0;                                   // total vertices (D4 record key)
};
struct CollAffine { Matrix A = IMatrix (); Vector t; };  // p' = mul(A,p) + t; rigid or not; identity by default (Matrix () is zero; chosen here)
struct CollSrc { uint32_t owner, grp; uint16_t mat, flags; uint32_t scope; }; // owner: base object index, 0 for vessels; mat: interned id (CollShape.h); flags: none; scope: weld
struct CollTri { uint32_t v[3]; uint32_t src; uint32_t otri; uint16_t ov[3]; uint16_t flags; }; // 28 B; src: srcTab index; otri: render triangle in its group; ov: its render vertices; flags: none
struct CollNode { double mn[3], mx[3]; uint32_t first, count; };              // 56 B; count 0 = inner (children first, first+1)
static_assert (sizeof (CollTri) == 28, "CollTri is 28 bytes (D1 1.1)");
static_assert (sizeof (CollNode) == 56, "CollNode is 56 bytes (D1 1.1)");
struct CollRef { uint32_t src, vtx; };                   // one render vertex: source group, vertex index
struct CollSrcGroup { const CollGroupData *g; CollSrc src; };                 // builder input
struct CollBuildStats { uint32_t ntriIn, ntri, nrender, nweld, nnode; uint32_t dropIndex, dropInvalid, dropDegen, dropDup; double msWeld, msTree; };

// affine helpers (column form: CollCompose (X, Y) applies Y first)
inline Vector CollApply (const CollAffine &X, const Vector &p) { return mul (X.A, p) + X.t; }
inline Vector CollApplyDir (const CollAffine &X, const Vector &d) { return mul (X.A, d); }
inline CollAffine CollCompose (const CollAffine &X, const CollAffine &Y) { return CollAffine { X.A * Y.A, mul (X.A, Y.t) + X.t }; }
inline CollAffine CollTranslate (const Vector &t) { CollAffine X; X.t = t; return X; }
inline CollAffine CollInverse (const CollAffine &X) { Matrix Ai = inv (X.A); return CollAffine { Ai, -mul (Ai, X.t) }; }

// welded triangle soup with an AABB tree; triangle ids stable for the life of the geometry
class CollGeom {
public:
	bool Build (const CollSrcGroup *src, size_t nsrc, double weldTol, CollBuildStats *st); // 1.2; false if no triangle survives; no log (callers log st once per mesh)
	bool Refit ();                                       // bottom-up boxes only: triangle, perm and leaf ids never change (D2 keys on them); true if loose
	const Vector &Pos (uint32_t v) const { return vtx[v]; }
	const Vector &RestPos (uint32_t v) const { return rest.empty() ? vtx[v] : rest[v]; }
	void KeepRest () { if (rest.empty()) rest = vtx; }  // called before the first dent
	std::vector<Vector>   vtx;                           // welded, current, local frame (mesh rest frame or base frame)
	std::vector<Vector>   rest;                          // empty until the first dent
	std::vector<CollTri>  tri;                           // source order after filters; index = triangle id
	std::vector<uint32_t> perm;                          // tree order of triangle ids; leaves index perm[first..first+count)
	std::vector<CollNode> node;                          // node[0] = root; children stored after their parent
	std::vector<CollSrc>  srcTab;                        // per source group
	std::vector<uint32_t> refOfs; std::vector<CollRef> ref; // CSR: welded vertex -> render vertices
	double skin = COLL_SKIN_DEFAULT;                     // m, 1.4
	Vector bsCentre; double bsRadius = 0;                // bounding sphere, local frame
	uint32_t version = 0;                                // bumps when Build replaces the triangles
private:
	void BuildTree ();                                   // binned SAH over tri, perm restarts from identity
	double builtSize = 0;                                // root box size (sum of extents) at the last tree build (Refit rule)
};

// query results and caller scratch (D1 1.5); queries keep no statics, so they are re-entrant
struct CollHit { uint32_t ta, tb; double d; Vector pa, pb; };   // triangle ids, pair frame
struct CollRayHit { uint32_t tri; double t; Vector p; };        // triangle id, ray parameter, point (ray frame)
struct CollScratch { std::vector<Vector> xa, xb; std::vector<uint32_t> sa, sb; uint32_t stamp = 0;
	std::vector<std::pair<uint32_t,uint32_t>> stack; };

// masks: indexed by CollTri::src (srcTab index), nonzero = skip that group's triangles; null = none
double CollDistance (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB,
	double cap, CollHit *best, CollScratch &s, const uint8_t *maskA = nullptr, const uint8_t *maskB = nullptr); // min(distance, cap); exact below cap
// every triangle pair closer than d, at most maxOut; returns the count found
size_t CollPairsWithin (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB,
	double d, std::vector<CollHit> &out, size_t maxOut, CollScratch &s, const uint8_t *maskA = nullptr, const uint8_t *maskB = nullptr);
bool   CollOverlap (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB,
	double d, CollScratch &s, const uint8_t *maskA = nullptr, const uint8_t *maskB = nullptr); // any pair closer than d
double CollTriTriDistance (const Vector a[3], const Vector b[3], Vector &pa, Vector &pb); // 0 if they cross; slivers as their edges; +inf (no hit) if a coordinate is not finite
bool   CollRayCast (const CollGeom &G, const CollAffine &X, const Vector &o, const Vector &d, double tmin, double tmax,
	CollRayHit &hit, const uint8_t *mask = nullptr);  // o, d in the frame X maps into; first hit
double CollClosestPoint (const CollGeom &G, const Vector &p, double cap, uint32_t &tri, Vector &q); // local frame; min(distance, cap)

// part pose inside a step; c = part sphere centre in its rest frame (bsCentre) (D1 1.6)
bool       CollIsRigid (const CollAffine &P);                 // |A^T A - I| < 1e-9 and det A > 0
CollAffine CollPoseAt (const CollAffine &P0, const CollAffine &P1, const Vector &c, double tau);
Vector     CollPoseVel (const CollAffine &P0, const CollAffine &P1, const Vector &c, double tau, const Vector &x); // d/dtau of P(tau)(x); divide by h
double     CollPoseMotion (const CollGeom &G, const CollAffine &P0, const CollAffine &P1); // max |P(tau)(x) - P0(x)|, tau in [0,1], x in G

#endif // !__COLLGEOM_H
