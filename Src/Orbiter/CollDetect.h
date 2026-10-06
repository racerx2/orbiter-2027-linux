// not upstream: collision detection (D2): broad phase, GRACE, zones, TOI, manifold; Orbiter-free

#ifndef __COLLDETECT_H
#define __COLLDETECT_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include "CollGeom.h"

// tolerances of D2 5.1 (CollParams takes its defaults from these)
constexpr double COLL_DELTA_TOI  = 0.005;  // skin-target CA stop [m]
constexpr double COLL_DELTA_CT   = 0.03;   // manifold collection, touching test, part interpolation threshold [m]
constexpr double COLL_KAPPA_CORE = 0.01;   // core-target CA on raw distance [m]
constexpr double COLL_S_REL      = 0.05;   // GRACE leaf-pair release [m]
constexpr double COLL_S_TOUCH    = 0.05;   // touch state LEFT -> APART on raw distance [m] (Y3')
constexpr double COLL_E_TOL      = 0.01;   // INACCURATE above this error bound [m]
constexpr double COLL_E_MAX      = 0.5;    // E_m cap [m]
constexpr double COLL_THETA_MAX  = 0.3;    // INACCURATE above this turn per step [rad]
constexpr double COLL_V_PART_MAX = 20.0;   // part motion vs part jump [m/s]; D3's vsmax, one value
constexpr double COLL_M_FRONT    = 0.02;   // front-cache margin [m]; <= 0 disables the cache (chosen here, U21)
constexpr int    COLL_N_CA       = 32;     // CA iteration cap, then SPECULATIVE
constexpr int    COLL_N_TT       = 50000;  // triangle pairs per query cap
constexpr int    COLL_SAP_THRESHOLD = 256; // sort-and-sweep above this many bodies

// other D2 values
constexpr double COLL_E_CAND        = 1000.0; // broad-phase cap on E [m] (2.2)
constexpr double COLL_SPIN_PATH     = 0.5;    // spin path when a body turns more than this per step [rad] (1.8)
constexpr double COLL_JUMP_MIN      = 1e-3;   // geometry jump threshold [m] (1.7)
constexpr double COLL_GRACE_LOG_AGE = 60.0;   // a GRACE scope older than this gets one log line [s]
constexpr int    COLL_GRACE_RELEASE_TRI = 16; // triangle pairs checked per scoped leaf pair at release
constexpr double COLL_DEGEN_DIST    = 1e-6;   // fallback normal when d_tri is at most this [m] (5.5)
constexpr double COLL_DEGEN_SPEED   = 1e-6;   // relative-velocity fallback needs this speed [m/s] (5.5)
constexpr double COLL_MERGE_DIST    = 0.01;   // manifold duplicate merge distance [m]
constexpr double COLL_MERGE_ANGLE   = 10.0;   // manifold duplicate merge normal angle [deg]
constexpr double COLL_PATCH_ANGLE   = 20.0;   // manifold patch normal angle [deg]
constexpr int    COLL_MAX_PATCHES   = 4;
constexpr int    COLL_PATCH_POINTS  = 4;
constexpr double COLL_APPROACH_TOL  = 1e-3;   // Resweep: approaching if v_rel . n < -this [m/s] (6.4)
constexpr double COLL_H_CONTACT     = 0.1;    // contact cap: approaching pairs need h <= this [s] (7.3)
constexpr double COLL_H_REST        = 0.25;   // load cap while a non-LANDED body rests under load [s] (Y6')
constexpr double COLL_LOOKAHEAD     = 2.0;    // look-ahead span H = this * h (7.2)
constexpr double COLL_DOCK_ZONE_RADIUS = 1.5; // DockZoneRadius default [m] (3.3)
constexpr double COLL_DOCK_ZONE_ON  = 3.0;    // R_dz_on [m]
constexpr double COLL_ATTACH_ZONE_ON = 2.5;   // R_az_on [m] (3.4)
constexpr double COLL_ZONE_ANGLE    = 15.0;   // CollisionZoneAngle default [deg] (U4)
constexpr double COLL_ZONE_SPEED    = 1.0;    // CollisionZoneSpeed default [m/s]

// detect -> solve contract (6.1, normative)
enum CollKind : uint8_t { COLL_NONE, COLL_TOI, COLL_RESTING, COLL_SPECULATIVE }; // result kinds (X1)
enum : uint32_t {                 // CollPairResult::flags
	COLLF_INACCURATE = 1,         // E > E_tol or theta_eff > theta_max: solved, warned, feeds the warp guard
	COLLF_DEGENERATE = 2,         // at least one point has a fallback normal
	COLLF_CORE       = 4,         // TOI of a touching pair at the core target (5.7)
	COLLF_RESWEEP    = 8,         // produced by Resweep
	COLLF_NEWPAIR    = 16,        // pair had no result in the previous frame
	COLLF_GRACE      = 32,        // some leaf pairs excluded by a GRACE scope
	COLLF_ZONE       = 64,        // some triangle pairs dropped by a zone
	COLLF_ENTRY      = 128        // a side had an entry event this frame
};
enum : uint8_t {                  // CollContact::flags
	COLLP_DEGENERATE = 1,         // fallback normal (5.5)
	COLLP_INTERSECT  = 2,         // raw triangles cross
	COLLP_FIRST      = 4,         // part pair was APART at the previous P1 (3.6, Y3'); only FIRST points carry damage energy
	COLLP_SUPPORT    = 8          // pair of a support set (3.2, Y10)
};
// pair-store owner key (Y14); kind first, so vessel and building keys never collide
enum : uint8_t { COLLO_VESSEL = 1, COLLO_BUILDING = 2 };
struct CollOwnerKey {
	uint8_t kind;           // COLLO_VESSEL or COLLO_BUILDING
	uint32_t id;            // vessel collision id (D3 1.7); 0 for buildings
	int32_t planet, base, obj, part; // building (CollOwnerRef fields; part 0 = an object's one rigid part in v1); -1 for vessels
	bool operator< (const CollOwnerKey &o) const;  // lexicographic (kind, id, planet, base, obj, part)
	bool operator== (const CollOwnerKey &o) const;
};
// one support of a LANDED assembly (Y10, 3.2)
struct CollSupport {
	CollOwnerKey building;  // building owner; base or obj -1: wildcard, every building of planet/base within delta_ct at wake
	Vector n;               // outward building normal at the support, base frame (DEGENERATE fallback, 5.5)
};
// one contact point; positions relative to the pair-frame origin, global axes, SI units
struct CollContact {
	Vector pA, pB;          // witness points on A and on B; the shared point is (pA + pB)/2
	Vector n;               // unit normal from B to A
	double gap;             // d_tri - skinA - skinB at the result pose; < 0 = skin overlap
	Vector vsA, vsB;        // surface velocity of each side at its point (part motion minus body rigid motion)
	uint32_t triA, triB;    // CollGeom triangle index per side
	uint16_t partA, partB;  // index into CollBody::parts per side
	uint8_t patch;          // 0..3
	uint8_t flags;          // COLLP_*
};
// a body's modelled state at the result time
struct CollBodyAt {
	Vector c, v;            // CG position (relative to the origin) and velocity
	Quaternion q;           // orientation (s->Q convention)
	Vector w;               // angular velocity of the modelled path, global; point velocity = v + Xc(w, p - c) + vs
};
// detection result of one body pair (X1)
struct CollPairResult {
	CollKind kind;
	uint32_t flags;         // COLLF_*
	int bodyA, bodyB;       // CollBody indices, bodyA < bodyB
	double tau;             // TOI in [0,1) (0: Resweep from 0 or margin pass); RESTING 0; SPECULATIVE tau_cap; Resweep >= start
	double specGap;         // SPECULATIVE: guaranteed separation at tau, >= 0
	double E;               // motion-model error bound of the pair
	Vector origin;          // pair-frame origin = A's CG at t0, global
	CollBodyAt a, b;        // states of A and B at tau (CollMotion Pos, Vel, Rot, Omega)
	int npt;                // 1..16
	CollContact pt[16];
};
// D3's change of one body after an island solve at tau
struct CollRestart {
	Vector dv, dwg;         // velocity and global angular velocity jump at tau (phase 1)
	Vector c1, v1, w1g;     // corrected state at t1, global
	Quaternion q1;          // corrected orientation at t1
};
// tolerances of 5.1
struct CollParams { double deltaToi = COLL_DELTA_TOI, deltaCt = COLL_DELTA_CT, kappaCore = COLL_KAPPA_CORE, sRel = COLL_S_REL, sTouch = COLL_S_TOUCH, eTol = COLL_E_TOL, eMax = COLL_E_MAX, thetaMax = COLL_THETA_MAX, vPartMax = COLL_V_PART_MAX, mFront = COLL_M_FRONT; int nCa = COLL_N_CA, nTt = COLL_N_TT, sapThreshold = COLL_SAP_THRESHOLD; };
// one active zone, spheres in the two body frames (3.3, 3.4)
struct CollZone { int bodyA, bodyB; CollOwnerKey ownerA, ownerB; Vector ca, cb; double ra, rb; };
// corrected t1 state of one body for the look-ahead (7.2)
struct CollEnd { Vector c1, v1, a1; double rmax, disp; };
// smallest caps of the frame and the pair that set each; hLoad from CollWorld, rest by LookAhead
struct CollWarpInput { double hContact = 1e100, hLoad = 1e100, accF = 1.0; uint32_t idContact[2], idLoad[2], idAcc[2]; };
// per-frame counters for the FINE log (8)
struct CollFrameStats { int candidates, iterations, bvPairs, triPairs, results, graceScopes; };

// rigid motion of one body over (part of) the step, global axes, double (1.4, 1.8)
struct CollMotion {
	Vector c0, v0, a0;      // CG position, velocity, acceleration at the interval start
	Vector c1, v1, a1;      // same at the interval end
	Quaternion q0, q1;      // orientation (s->Q convention)
	Vector w0g, w1g;        // global angular velocity, v_point = v + Xc(w, r) (D3 2.4)
	double h;               // frame step [s]
	double ta = 0, tb = 1;  // interval of the frame step in tau (Split)
	double theta;           // rotation angle of the modelled path over [ta, tb] [rad]
	bool spin;              // constant-rate path with end correction (1.8), else slerp
	bool a0ok;              // a0 from the pose cache, else a0 = a1
	void Setup ();                     // theta and spin from q0, q1, w0g, w1g, h (chosen here; AddBody calls it)
	Vector Pos (double tau) const;     // Hermite position
	Vector Vel (double tau) const;     // Hermite derivative / h
	Quaternion Rot (double tau) const; // slerp or spin path
	Vector Omega (double tau) const;   // angular velocity of the modelled path, global
	CollMotion Split (double tau, const CollRestart &r) const; // [tau, 1] after D3's change (6.4)
};
// one collider part for this step; the caller fills inputs, AddBody derives disp, rho, c, interp
struct CollPartRef {
	const CollGeom *geom;   // D1 geometry (template or private copy)
	CollAffine P0, P1;      // part -> body frame at t0 and t1
	double disp;            // max part point displacement over the step [m]
	double rho;             // max distance of part points from the body CG over the step [m]
	double skin;            // D1 skin of this part [m], clamped to [0.01, 0.5]
	Vector c;               // part sphere centre in its rest frame (D1 1.6 `c` of CollPoseAt, CollPoseVel)
	bool interp;            // disp > delta_ct: CA interpolates P(tau); else P1 with margin disp
	bool rigid;             // P0 and P1 orthonormal within 1e-9
	CollOwnerKey owner;     // Y14: the vessel that owns the part; base part: (planet, base, obj, 0), one part per object
	uint32_t partKey;       // touch-state key (3.6): vessel mesh << 16 | representative group (CollPart mesh, rep); buildings 0
	uint32_t version;       // D1 part version; changes drop GRACE scopes and front caches
	uint16_t mesh;          // vessel mesh index or base object index
	const uint8_t *mask;    // D1 group mask or null
};
enum : uint8_t { COLLB_DYNAMIC = 0, COLLB_LANDED = 1, COLLB_BASE = 2, COLLB_PLAYBACK = 3, COLLB_FROZEN = 4 }; // CollBody::kind (values chosen here)
enum : uint16_t { COLLE_NEW = 1, COLLE_MEMBERS = 2, COLLE_JUMP = 4, COLLE_ACTIVATED = 8, COLLE_MESH = 16, COLLE_ENABLE = 32 }; // CollBody::entry bits (3.2, values chosen here)
// one assembly or one base for this frame (1.4)
struct CollBody {
	CollMotion m;
	std::vector<CollPartRef> parts;
	double rmax;            // CG-centred bound over both poses, incl. skin [m]; <= 0: AddBody computes it (1.4 step 8)
	uint32_t id;            // assembly id or base id
	uint8_t kind;           // COLLB_DYNAMIC, COLLB_LANDED, COLLB_BASE, COLLB_PLAYBACK, COLLB_FROZEN
	uint16_t entry;         // entry-event bits (3.2)
	bool jump1;             // in-step jump: entry check at t1 only, no results this frame (1.7)
	int planet;             // planet index for bases and LANDED, else -1
};

// pair store (6.1): sorted vector keyed by (CollOwnerKey A, CollOwnerKey B), kept across frames
enum : uint8_t { COLLT_APART = 0, COLLT_LEFT = 1, COLLT_TOUCHING = 2 }; // touch state (3.6); a missing entry is APART
struct CollLeafPair {                 // one leaf-node pair of a GRACE scope or a front cache: part key, part version, leaf node per side
	uint32_t partKeyA, verA, leafA, partKeyB, verB, leafB;
	bool operator< (const CollLeafPair &o) const;
	bool operator== (const CollLeafPair &o) const;
};
struct CollTouch { uint32_t partKeyA, partKeyB; uint8_t state; bool solved; }; // one part pair; solved: a point of it was solved at this P1
struct CollPairEntry {
	CollOwnerKey a, b;                       // a < b
	std::vector<CollLeafPair> grace;         // GRACE scope, sorted; empty = none (3.2)
	double graceAge = 0;                     // sim time since the scope started [s]
	bool graceLogged = false;                // 60 s line written
	std::vector<CollLeafPair> front;         // front cache: leaf pairs within cut + mFront (5.6)
	double frontCut = -1, frontMotion = 0;   // cut the cache was built for (-1 = none), motion bound accumulated since
	uint8_t lastKind = COLL_NONE;            // kind of the last result (NEWPAIR)
	uint32_t lastFrame = 0;                  // frame of the last result
	std::vector<CollTouch> touch;            // per part pair, sorted by (partKeyA, partKeyB) (3.6)
	std::vector<std::pair<uint64_t, uint8_t>> embed; // attachment embedding cache: query key -> 0/1 (3.4)
};
class CollPairStore {
public:
	CollPairEntry *Find (const CollOwnerKey &a, const CollOwnerKey &b);             // null if absent; a, b in either order
	const CollPairEntry *Find (const CollOwnerKey &a, const CollOwnerKey &b) const;
	CollPairEntry &Get (const CollOwnerKey &a, const CollOwnerKey &b);              // inserts in order
	void PurgeVessel (uint32_t id);                                                 // OnDeleteVessel: every entry with that vessel
	void FlushFront ();                                                             // time jump (D3 10.5)
	void Clear ();
	size_t Size () const;
	const CollPairEntry &At (size_t i) const;                                       // key order
private:
	std::vector<CollPairEntry> e;
};

// attachment embedding query (3.4 step 3): raw intersection at mounted pose, cached in pair store
struct CollEmbedQuery {
	CollOwnerKey parent, child;              // the two vessels
	uint64_t key;                            // hash of both point ids, ref, dir, rot and the part versions
	const CollPartRef *pp; size_t np;        // parent parts, P1 = part -> parent vessel frame
	const CollPartRef *cp; size_t nc;        // child parts, P1 = part -> child vessel frame
	CollAffine mount;                        // child vessel frame -> parent vessel frame at the mounted pose (InitAttachmentToParent)
};

// detection for one frame, Orbiter-free (CollGeom + Vecmat only)
class CollDetect {
public:
	void Begin (const CollParams &prm, double h);                       // per frame
	int  AddBody (const CollBody &b);                                    // bases first, then assemblies; derives part bounds, applies the part-jump rule (1.5)
	void SetZones (const std::vector<CollZone> &z);                      // active docking and attachment zones (3.3, 3.4)
	void SetSupports (uint32_t assemblyId, const std::vector<CollSupport> &s); // Y10, kept across frames; empty list clears (3.2)
	void Detect (std::vector<CollPairResult> &out, CollFrameStats &st); // touch-state update (3.6), entry checks, first pass at tau = 0
	void Resweep (int b, double tau, const CollRestart &r, std::vector<CollPairResult> &out); // 6.4
	void Solved (const CollPairResult &r);                               // D3 solved r: its TOI/RESTING points mark TOUCHING (3.6)
	bool SupportDist (uint32_t assemblyId, double &minRaw) const;        // after Detect: smallest raw distance of the support pairs; false if none (Y10)
	void LookAhead (const std::vector<CollEnd> &end, CollWarpInput &w) const; // 7.2, after write-back
	void ToPartFrame (const CollPairResult &r, int i, int side, Vector &p, Vector &n) const; // point and normal in the hit part's rest frame at tau (D4)
	Vector SurfaceVel (const CollPairResult &r, int i, int side, double tau) const; // vs of point i's material point at tau, global (1.5); tau = 1 for D3's phase 2
	CollOwnerKey Owner (const CollPairResult &r, int i, int side) const; // owner of point i's triangle (vessel, or building object from D1's back map)
	CollPairStore &Pairs ();                                             // GRACE scopes, front caches, last results, touch states; kept across frames
	void Reset ();                                                       // drop the pair store and support sets (collisions toggled, Clear)
	int  nBody () const;                                                 // bodies added since Begin (chosen here)
	const CollBody &Body (int i) const;                                  // as stored by AddBody, motion included (chosen here; D3 phase 2 kinematic fields)
	bool Embedded (const CollEmbedQuery &q);                             // 3.4 step 3 (chosen here)
private:
	struct Impl;                                                         // query machinery (CollDetect.cpp)
	struct PartIdx { CollOwnerKey owner; uint32_t partKey; int body, part; }; // owner part -> body part, sorted
	struct PairInfo { int a, b; uint8_t res; double E, thetaEff; };      // candidate pair of this frame; res: 1 RESTING, 2 TOI or SPECULATIVE seen
	struct FrontPart { CollOwnerKey owner; uint32_t partKey, version, nnode; const CollGeom *geom; CollAffine X; }; // part placement at a front-cache build
	struct FrontPose { CollOwnerKey a, b; std::vector<FrontPart> part; }; // per owner pair, in store order
	typedef std::pair<CollOwnerKey, CollOwnerKey> OwnerPair;
	CollParams m_prm;
	double m_h = 0;
	uint32_t m_frame = 0;
	std::vector<CollBody> m_body;
	std::vector<Vector> m_org;                                           // frame-start CG per body: pair-frame origins, kept through Resweep
	std::vector<CollZone> m_zone;
	std::vector<std::pair<uint32_t, std::vector<CollSupport>>> m_support; // by assembly id, sorted
	CollPairStore m_store;
	std::vector<PartIdx> m_pidx;
	std::vector<PairInfo> m_info;                                        // sorted by (a, b)
	std::vector<OwnerPair> m_resPrev, m_resCur;                          // owner pairs with results in the previous and this frame (NEWPAIR), sorted
	std::vector<OwnerPair> m_warnPrev, m_warnCur;                        // owner pairs warned for raw intersection (rate limit), sorted
	std::vector<FrontPose> m_front;                                      // sorted by (a, b)
	std::vector<std::pair<uint32_t, double>> m_supDist;                  // assembly id -> smallest raw support distance of this frame
	std::vector<Vector> m_vx; std::vector<uint32_t> m_vs; uint32_t m_stamp = 0; // transformed-vertex cache of the current query pose
	double m_simt = 0;                                                   // sum of frame steps (log rate limits)
	std::vector<std::pair<std::pair<uint32_t, uint32_t>, double>> m_specLog; // body id pair -> sim time of its last CA-cap INFO line, sorted; entries older than 60 s dropped
	std::vector<uint32_t> m_badPrev, m_badCur;                           // body ids with a non-finite state in the previous and this frame (logged on entry), sorted
};

// pure helpers (tested by U2-U4, U12, U14, U20); CollWorld calls the warp, jump and zone ones
void   CollRelBezier (const CollMotion &A, const CollMotion &B, Vector Q[4], Vector M[3]); // relative Bezier points (origin A's c0) and derivative control vectors (1.8)
bool   CollCapsuleHit (const Vector Q[4], double reach);          // 2.2 swept test; false = reject; reach = rA + rB + delta_ct + min(E, E_cand)
double CollErrorT (const CollMotion &A, const CollMotion &B);     // E_T of the relative path (1.8)
double CollErrorR (const CollMotion &m, double rmax);             // E_R of one body (1.8)
bool   CollLookAheadHit (const CollEnd &a, const CollEnd &b, double h, double deltaCt); // 7.2 item 2 over H = 2h
double CollFloorPow10 (double w);                                 // largest power of 10 <= w, at least 1
double CollWarpAllowed (const CollWarpInput &w, double warp, double dtSys); // 7.3: min over the contact, load and accuracy caps
enum { COLLJP_T0 = 1, COLLJP_INSTEP = 2 };
int    CollJumpPhase (bool s1IsS0, bool p1Ran);                   // 1.7 phase rule: INSTEP iff s1 != s0 and P1 has not run this frame
double CollGeomJump (const Vector &dr, const Vector *dofs, size_t n); // 1.7 FRAME: max over collider meshes |dr + d_ofs|; |dr| if n == 0
struct CollPortAt { Vector g, d, r, v; };                          // port or attachment point at one time, global: ref point, direction, rotation ref, point velocity
bool   CollDockZoneActive (const CollPortAt &a, const CollPortAt &b, double rdz, double angleDeg, double vmax); // 3.3 rule step 3 at one time
bool   CollAttachZoneActive (const CollPortAt &p, const CollPortAt &c, double angleDeg, double vmax);          // 3.4 rule step 4 at one time
bool   CollAttachIdMatch (const char *parentId, const char *childId); // 3.4 step 2: non-empty parent id, prefix rule over the 8-char fields

// collision jump flags (1.7): a Vessel member, set by CollHook forwarders, read and cleared at P1
struct CollVesselFlags { uint8_t jump = 0; bool inStep = false; Vector dr; };
enum { COLLJ_MOVE = 1, COLLJ_FRAME = 2, COLLJ_REBASE = 4 }; // explicit state write, CG frame shift, docking snap (Y12)

#endif // !__COLLDETECT_H
