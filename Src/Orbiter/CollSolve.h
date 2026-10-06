// not upstream: contact response (D3): island solver, detect -> solve adapter, impacts; no Orbiter

#ifndef __COLLSOLVE_H
#define __COLLSOLVE_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include "CollDetect.h"

// D3 values (4.3-4.6, 5.4, 6, 7, 8, 10)
constexpr int    COLL_ITERATIONS   = 20;     // CollisionIterations default: base SI sweeps per phase
constexpr int    COLL_TOI_ROUNDS   = 4;      // CollisionTOIRounds default
constexpr double COLL_ITER_FRAME   = 0.167;  // nit = clamp(ceil(iters * h / this), iters, 3 * iters) [s]
constexpr double COLL_V_REST       = 0.1;    // e = 0 below this approach [m/s]
constexpr double COLL_E0           = 0.3;    // restitution default
constexpr double COLL_VY           = 1.0;    // first-yield speed default [m/s]
constexpr double COLL_VP           = 50.0;   // restitution fade start [m/s]
constexpr double COLL_VD           = 300.0;  // destructive regime, e = 0 [m/s]
constexpr double COLL_MU           = 0.5;    // friction default
constexpr double COLL_SLOP         = 0.002;  // position correction target [m]
constexpr double COLL_BETA         = 0.2;    // position correction factor
constexpr double COLL_DX_MAX       = 0.05;   // position correction cap per frame [m]
constexpr int    COLL_PC_SWEEPS    = 8;      // position correction Gauss-Seidel sweeps
constexpr double COLL_IMPULSE_END  = 1e-9;   // a round whose impulses are all below this ends the loop [N s]
constexpr double COLL_NONCONV      = 1e-3;   // a contact still approaching faster than this after phase 2 is logged [m/s]
constexpr double COLL_V_WAKE       = 0.05;   // LANDED wake by its impulse share [m/s] (6.6)
constexpr double COLL_W_WAKE       = 0.02;   // LANDED wake by its angular share [rad/s]
constexpr double COLL_A_LOAD       = 0.1;    // load-bearing rest: phase-2 press of at least this [m/s^2] (10.7)
constexpr double COLL_T_REST       = 3.0;    // rest on buildings before LANDED [s] (7.3, CollWorld)
constexpr double COLL_LAND_POS_TOL = 0.002;  // LandedPoseMatches position tolerance [m] (7.4, CollWorld)
constexpr double COLL_LAND_ROT_TOL = 1e-4;   // LandedPoseMatches rotation tolerance [rad]
constexpr double COLL_ROT_SUBSTEP  = 0.05;   // CollRotate substep [rad] (6.1)
constexpr double COLL_OMEGA_MAX    = 100.0 * 3.14159265358979323846; // omega clamp as Rigidbody.cpp:279-294 [rad/s]
constexpr double COLL_FR_DV        = 1e-3;   // forced recorder sample: phase-1 velocity change above this [m/s] (10.8)
constexpr double COLL_FR_DW        = 1e-4;   // same for angular velocity [rad/s]
constexpr double COLL_SURFVEL_EV   = 1e-3;   // COLLEV_SURFVEL above this surface speed [m/s] (8.1)

// impact event (D3 fills at P1, D4 consumes at P2); DWORD spelled uint32_t to stay Orbiter-free
struct CollOwnerRef { uint32_t vesselId; int planet, base, obj, part; }; // vesselId 0 = building
struct CollImpactSide {
	CollOwnerRef owner;      // the vessel actually hit (component, not assembly) or the building
	int mesh, grp, tri;      // hit render feature, original triangle index, -1 if unknown
	Vector c, n;             // contact centroid and outward normal in the hit part's rest frame (base frame for buildings)
	double a;                // contact patch radius [m]
};
struct CollImpactEvent {
	CollImpactSide s[2];
	double t;                // sim time of impact
	double dKE, Wf;          // phase-1 impulse work of a first touch, friction work [J] (Y3)
	double vn, vn_post, vt;  // impulse-weighted approach, separation and slip speed [m/s]
	double Jn, Jt, meff;     // impulses [N s], effective mass along n [kg]
	uint32_t flags;          // FIRST, RESTING, WOKE_LANDED, POSCORR (COLLEV_*)
};
enum : uint32_t {            // CollImpactEvent::flags (D3 8.1)
	COLLEV_FIRST = 0x01, COLLEV_RESTING = 0x02, COLLEV_WOKE_LANDED = 0x04, COLLEV_POSCORR = 0x08, COLLEV_SPECULATIVE = 0x10,
	COLLEV_INACCURATE = 0x20, COLLEV_DEGENERATE = 0x40, COLLEV_SURFVEL = 0x80, COLLEV_SLOW = 0x100
};
inline CollOwnerRef CollOwnerRefOf (const CollOwnerKey &k)  // D2 owner key -> master owner ref
{
	if (k.kind == COLLO_VESSEL) return CollOwnerRef { k.id, -1, -1, -1, -1 };
	return CollOwnerRef { 0, k.planet, k.base, k.obj, k.part };
}

// rigid body as seen by one island solve (4.1)
struct CollSBody {
	bool   dyn;                 // false: building, landed, playback
	double m;                   // root mass [kg]
	Vector pmi;                 // mass-normalised diagonal PMI [m^2], <= 0 axes locked
	Matrix Rt, R1;              // root rotation at tau and at t1
	Vector xt, vt, wt;          // CollBodyAt at tau (D2 6.1): CG, velocity, world angular velocity
	Vector x1, v1, wb1;         // t1 state, island frame; wb1 in the body frame
	Vector dP1, dL1, dP2, dL2;  // out: phase impulses and angular impulses (world)
};
struct CollSContact {
	int    a, b;                // body indices, n points from b to a
	Vector p, n, n2;            // point and normal at tau; phase-2 normal rotated to t1
	double gap;                 // signed gap at tau; SPECULATIVE: specGap; positive-gap INACCURATE: the point's gap (3.2)
	uint8_t kind, flags;        // TOI, RESTING, SPECULATIVE (solve kind after the INACCURATE rule); D2's COLLP_* point flags (BYTE)
	double mu, e0, vy;          // combined material
	Vector vka_t, vkb_t, vka_1, vkb_1; // kinematic or surface velocity per side, at tau and at the carried point at t1
	double vapp, ln1, ln2, Wn, Wt;     // out: approach, accumulated impulses, phase-1 work 0.5 J.(u_pre + u_post), normal and tangential
	Vector J1, J2;              // out: total impulse on a per phase
	double bias, kn, ln;        // solver scratch (4.5): velocity bias, effective mass along n, running phase's normal impulse
	Vector t1, t2; double kt[2], lt[2]; // solver scratch: tangents, their effective masses, accumulated tangent impulses
};
struct CollSolveParams { int iters = COLL_ITERATIONS; double vrest = COLL_V_REST, vp = COLL_VP, vd = COLL_VD, slop = COLL_SLOP, beta = COLL_BETA, dxmax = COLL_DX_MAX, vsmax = COLL_V_PART_MAX; }; // vsmax = D2 CollParams::vPartMax (one value)
struct CollDelta { Vector dv, dx, dLw, dth; }; // world dv, dx, spin-momentum change; body-frame rotation vector (6.1)

// solver math (4.3, 4.5, 6.1)
double CollRestitution (double v, double e0, double vy, const CollSolveParams &p); // e(v) of 4.3
int    CollIterations (double h, int iters);                                      // nit of 4.5
void   CollRotate (Quaternion &q, const Vector &dth);                             // body-frame rotation vector, substeps <= 0.05 rad with Quaternion::Rotate
bool   CollApplyDeltaState (Vector &x, Vector &v, Quaternion &q, Vector &wb, const Vector &Ib, const CollDelta &d); // 6.2 state math; Ib = m * pmi, axes <= 0 locked; omega clamped; false: non-finite, state unchanged

// one island at its tau, island frame (4.2-4.6; container chosen here)
struct CollIsland {
	std::vector<CollSBody> body;
	std::vector<CollSContact> con;
	double tau = 0, h = 0;                       // island time of impact, frame step; tr = (1 - tau) h
	bool Solve (const CollSolveParams &p);       // phase 1 with the energy guard, then phase 2; false: non-finite, response dropped
	void Delta (int i, CollDelta &d) const;      // write-back deltas of dynamic body i (4.2)
	bool Correct (const CollSolveParams &p, std::vector<CollDelta> &d); // 4.6 split impulse over contacts in con with gap < -slop (caller picks); d per body, dx, dth only
private:
	friend class CollFrameSolver;                // events, wake share and checks read the last solve
	bool guard = false;                          // last Solve: phase 1 redone without restitution (4.2 step 5)
	int nonconv = 0;                             // last Solve: contacts still approaching after phase 2 (4.5)
	double W1 = 0, W2 = 0, S1 = 0, S2 = 0;       // last Solve: work of each phase and its scale sum |ln| (|un| + 1e-3) (9)
	std::vector<Vector> xs, xm;                  // last Solve: lever origins per body, phase 1 and 2
	std::vector<Vector> upre, upost;             // last Solve: relative velocity per contact at tau before and after phase 1
	double Meff (const Vector &p, const Vector &n, int a, int b) const; // effective mass along n at p, phase-1 lever origins and inertia
};

// detect -> solve adapter, islands and TOI rounds (3, 5.4, 6.6, 8; driver chosen here)
struct CollSMat { double e0 = COLL_E0, vy = COLL_VY, mu = COLL_MU; }; // material of one side at one point (4.3, 4.4)
class CollSolveHost {                        // what the driver needs from CollWorld; tests use a stub
public:
	virtual ~CollSolveHost () {}
	virtual CollSMat Material (const CollPairResult &r, int i, int side) = 0;               // side 0 = A, 1 = B
	virtual void Feature (const CollPairResult &r, int i, int side, CollImpactSide &s) = 0; // owner, mesh, grp, tri of point i (D1 back map)
};
struct CollFrameBody {                       // one CollDetect body as the driver sees it, global axes; same index as in CollDetect
	bool dyn;                                // dynamic at the start of the frame (2.2)
	bool wakeable;                           // LANDED: woken inside the solve by a large share (6.6)
	uint32_t id;                             // assembly id (island origin, 3.2)
	double m; Vector pmi;                    // root mass, mass-normalised diagonal PMI (2.3)
	Vector x1, v1, wb1;                      // t1 CG position and velocity, body-frame angular velocity
	Quaternion q1;                           // t1 orientation (s->Q convention)
	Vector wakeV1, wakeWb1;                  // t1 velocity and body omega after the takeoff block (6.6)
	bool woke = false;                       // out: woken this frame (CollWorld runs CollWake before the deltas)
	bool impulsive = false;                  // out: phase-1 change above COLL_FR_DV or COLL_FR_DW (10.8)
	bool loadRest = false;                   // out: slow contact pressed at >= COLL_A_LOAD this frame (10.7)
	int nVesselContacts = 0, nBuildingContacts = 0; // out: solved contacts by partner kind (7.3)
	bool allSlow = true;                     // out: every phase-1 approach below vrest (7.3)
	std::vector<CollSupport> sup;            // out: buildings touched, mean outward normal in the base frame (7.5)
};
struct CollBodyDelta { int body; CollDelta d; bool poscorr; }; // one write-back in application order; poscorr: position correction only
struct CollSolveStats { int islands, rounds, resweeps, nonconverged, exhausted, guards; };
class CollFrameSolver {                      // own translation unit CollSolveFrame.cpp, so CollSolve.Test links without CollDetect.cpp
public:
	// one P1 frame: sort, islands, re-sweep rounds, Solved calls, wake, correction, one event per pair
	void Run (CollDetect &det, std::vector<CollPairResult> &res, std::vector<CollFrameBody> &body, double h, double simt0,
		const CollSolveParams &p, int rounds, CollSolveHost &host, std::vector<CollBodyDelta> &delta, std::vector<CollImpactEvent> &ev);
	bool check = false;                      // CollisionCheck: momentum and energy checks in release builds (9)
	CollSolveStats stats = {};               // last Run
private:
	int nWarn = 0, nQuiet = 0;               // check warnings written, and suppressed since the last summary (9)
	double tWarn = 0;                        // sim time of the last summary
	void Warn (double t, const char *msg);   // first 10 lines, then one summary per minute of sim time
	struct InaccLog { CollOwnerKey a, b; double t; };
	std::vector<InaccLog> inacc;             // owner pairs warned for INACCURATE results and when, sorted; one line per pair per sim minute
};

#endif // !__COLLSOLVE_H
