// not upstream: collision addon E1 5, the Orbiter-free frame driver at t0 (speculative contacts, records, past check, delivery on the mirror)
#ifndef __COLLADDONFRAME_H
#define __COLLADDONFRAME_H
#include <cstdint>
#include <map>
#include <vector>
#include "CollSolve.h"
#include "CollOrbMirror.h"

constexpr double COLLA_DEV_TOL  = COLL_DELTA_CT - COLL_DELTA_TOI; // 0.025 m (3.2)
constexpr double COLLA_A_JUMP   = 50.0;    // JUMP above this unseen acceleration [m/s^2] (3.2)
constexpr double COLLA_D_FAR    = 1000.0;  // near or far JUMP [m] (2.5)
constexpr double COLLA_TURN     = 0.05;    // TOUCH path only below this turn per step [rad] (2.2)
constexpr double COLLA_V_WRITE  = 0.01;    // linear change written with SetState above this [m/s] (6.3)
constexpr double COLLA_E_COMP   = 1e-4;    // accepted first-stage position lag [m] (6.2)
constexpr double COLLA_SPIN_TOL = 1e-6;    // spin write tolerance at rmax [m/s] (6.3)

// one solver body of this frame; CollWorldA fills the inputs from the snapshot (1.4), the session records and E2's parts
struct CollABody {
	uint32_t id = 0; uint8_t kind = COLLB_DYNAMIC; // assembly id (bases: base id), COLLB_*
	std::vector<uint32_t> member;                  // session vessel ids; member[0] is the write reference
	uint64_t memberHash = 0;
	double m = 1.0; Vector pmi = Vector (1, 1, 1); // solver mass, mass-normalised PMI (body frame)
	Vector x, v, wb; Quaternion q;                 // actual t0 state: CG, velocity (global), body omega, attitude
	Vector aTot, arot;                             // cached total acceleration (global) and body angular acceleration
	Vector gEst;                                   // gravity estimate at x for the reset of a state write (6.3)
	double rmax = 1.0;                             // CG-centred bound incl. skin [m]
	bool stack = false, ground = false, groundNew = false, thrust = false;
	CollMotion kin {};                             // kinematic bodies over [t0, t0 + h] (4.3)
	bool wakeable = false; Vector wakeV, wakeWb;   // LANDED wake state (6.6)
	std::vector<CollPartRef> parts;                // forward pass: P0 = pose[1] (t0), P1 = next (4.3)
	std::vector<CollPartRef> pastParts;            // past check: P0 = pose[0], P1 = pose[1]
	uint16_t entry = 0; bool jump1 = false;        // entry bits (1.5); in-step jump
	int planet = -1;
	// out
	double dev = 0;                                // largest deviation from the mirror prediction (3.2)
	bool woke = false, loaded = false, jump = false;
};
struct CollAWrite {                                // what CollWorldA does through CollSdk for body i (6.3, 6.4)
	int body; bool state, attitude, spin, force, weight;
	Vector x, v, wb; Quaternion q;                 // written state (compensation included), global
	Vector Fb, Mb;                                 // AddForce at the CG and couple, body frame
	Vector cdx, cdv, cdth, cdw;                    // compensation terms (written minus physical, 3.1)
};
struct CollAStats { int spec, real, touchPath, freePath, past, missed, reapply, writes, forceWrites, attWrites, deliveryIt, rounds, rec, recPos, turnFree, featFree, jumps, retries, clampE, checkFail, deliveryRelevel, groundWrites; };
struct CollAContactRec { uint64_t ka, kb; Vector ra, rb, J; };        // body keys (kind << 32 | id); points in each body's frame at t0; J on a, global
struct CollAPairRec { uint64_t ka, kb; Vector ra, rb, n; double g0, u0; }; // one speculative pair: smallest-gap point, normal, gap and approach at t0
struct CollABodyRec {
	uint64_t key; uint64_t memberHash;
	Vector dx, dv, dLs, dth;                       // effect of the speculative part at the end of the step (mirror difference, 6.2)
	Vector xs, vs, ws; Quaternion qs;              // free-path start at t0 (physical, without the speculative part)
	CollOrbState aw, fw;                           // the two configured mirror states (planned, planned minus speculative)
	bool zero;
};
struct CollAIslandRec { std::vector<CollAContactRec> c; std::vector<CollAPairRec> p; std::vector<CollABodyRec> b; double h; };
struct CollASupRow { uint64_t ka, kb; Vector ra, rb, nb; double J2n; }; // resting row (7.5): points and partner normal in the body frames
struct CollAPlanEdit { uint32_t id; Vector dv, dwb, F, M; };                 // tests: added to body id's plan before its delivery
struct CollAMembersChange { uint32_t assembly; };  // 2.5: a body whose members changed (its records are dropped)
struct CollAMem {                                  // per solver body, kept across frames
	uint64_t memberHash = 0;
	bool hasP = false;
	CollOrbState P, Pw;                            // mirror prediction of this t0 and the written state it started from
	Vector xs, vs, ws; Quaternion qs;              // physical written state at the start of the last step (S+)
	Vector vw;                                     // written velocity (stacks: a_free from the step)
	Vector Fprev, Mprev;                           // own force and torque applied last frame (body frame)
	double hPrev = 0, tRec = -1e100;               // last step; sim time of the last reconciliation
	bool loadPrev = false, seen = false;
};

Vector CollNoSpecSpin (const Matrix &R, const Vector &I, const Vector &wPlan, const Vector &dL1); // body spin of the plan without the speculative impulse dL1 (global), from the unclamped plan, clamped to 100 pi
class CollAddonFrame {
public:
	CollSolveParams prm; CollParams dprm; int rounds = 4; bool check = true;
	// one pre-step (5.3); b is indexed as the caller built it, zones refer to those indices
	void Run (CollDetect &fwd, CollDetect &ver, const CollOrbMirror &mir, std::vector<CollABody> &b, const std::vector<CollZone> &zones,
		double h, double simt0, CollSolveHost &host, std::vector<CollAWrite> &out, std::vector<CollImpactEvent> &ev);
	// after the state and attitude writes and the weight reads: delivery again with the exact gravity (6.3); gExact per body index
	void Finish (const CollOrbMirror &mir, std::vector<CollABody> &b, std::vector<CollAWrite> &out, const std::vector<Vector> &gExact);
	void OnMembers (const CollAMembersChange &c);  // 2.5 (records of that body dropped; entry next frame)
	void OnDelete (uint32_t vesselId);             // 2.5
	void OnTimeJump ();                            // 2.5: records and resting rows dropped, no write
	void Reset ();                                 // collisions toggled
	const CollAStats &Stats () const { return st; }
	const CollWarpInput &WarpIn () const { return warp; } // 9: look-ahead and load caps of the last Run
	double hRest = 0.1;                            // 7.4 load cap for bodies with engines engaged, from the mirror
	const CollDetect *FeatDet () const { return featDet; } // the detector whose results the host is asked about now, else NULL
	std::vector<CollSContact> *conProbe = nullptr; // tests: every contact the island builder made
	const std::vector<CollAPlanEdit> *planEdit = nullptr; // tests: plan changes of the next Run's delivery
	std::vector<CollContactRec> contacts;          // dmg3 L4: owner pairs that exchanged impulse in the last Run, slides included
	static uint64_t Key (uint8_t kind, uint32_t id) { return ((uint64_t)(kind == COLLB_BASE ? 1 : 0) << 32) | id; }
private:
	std::vector<CollAIslandRec> isl;               // last frame's speculative islands (2.1)
	std::vector<CollASupRow> sup;                  // last frame's resting rows (7.5)
	std::map<uint64_t, CollAMem> mem;              // per solver body
	CollAStats st {};
	CollWarpInput warp;
	CollDetect kt;                                 // scratch detector of the kinematic touch (2.3)
	struct Pend { uint64_t key; int body; CollOrbState conf, cf; Vector tgtP; double h; }; // a single body written with SetState: Finish re-solves its force
	std::vector<Pend> pend;
	const CollDetect *featDet = nullptr;
	struct Impl;
};
#endif
