// not upstream: collision addon, ground impacts: collider vertices against the terrain under a vessel make impact events for the damage commit (design CA-ground 1)
#ifndef COLLGROUNDA_H
#define COLLGROUNDA_H
#include <cstdint>
#include <map>
#include <vector>
#include "CollCfg.h"
#include "CollDmgTypes.h"
#include "CollSdk.h"
#include "CollShape.h"
#include "CollSolve.h"

constexpr double   COLL_GROUND_E      = 0.2;   // restitution of a ground impact
constexpr double   COLL_GROUND_GAP    = 0.25;  // no second event of one contact region within this [s]
constexpr double   COLL_GROUND_REGION = 0.3;   // contact region radius / bound radius, within one part
constexpr double   COLL_GROUND_REARM  = 1.5;   // a locked region fires again above this times its last approach
constexpr double   COLL_GROUND_PATCH  = 0.5;   // contact patch radius of both sides [m]
constexpr double   COLL_GROUND_NEAR   = 0.5;   // near test: distance to the terrain below bound radius + point speed * horizon + this [m]
constexpr double   COLL_GROUND_REFINE = 0.1;   // the hit vertex above its own terrain by more than |v| horizon + this: no event [m]
constexpr double   COLL_GROUND_REACH  = 25e3;  // no terrain query this far above the planet radius plus the near distance [m]
constexpr double   COLL_GROUND_HZ     = 0.05;  // prediction horizon cap, so time warp makes no event at altitude [s]
constexpr double   COLL_GROUND_DN     = 3.0;   // spacing of the terrain samples for the normal at the hit vertex [m]
constexpr int      COLL_GROUND_TRIES  = 4;     // candidates refined per vessel and frame, fastest first
constexpr uint32_t COLL_GROUND_VTX    = 4096;  // collider vertices tested per vessel and frame, at most, plus each part's lowest

struct CollGroundVessel { uint32_t id; CollH h; const CollShape *shape; }; // one live vessel with its collider

inline bool CollGroundSide (const CollOwnerRef &o) { return !o.vesselId && o.planet >= 0 && o.base < 0; } // the ground side of an impact event
double CollGroundMeff (double m, const Vector &pmi, const Vector &r, const Vector &n); // 1 / (1/m + (r x n) . I^-1 (r x n)), I = m pmi, vessel frame; pmi axes <= 0 locked

class CollGroundA {
public:
	CollGroundA (CollSdk &sdk, const CollCfgValues &cfg);
	void Frame (double simt, double simdt, const std::vector<CollGroundVessel> &v, std::vector<CollImpactEvent> &ev, std::vector<CollFxContact> &fx); // appends; reads only
	void TimeJump () { locks.clear (); }
	void Drop (uint32_t id) { locks.erase (id); } // vessel deleted
	uint64_t events = 0;                          // ground events made
	uint32_t tested = 0;                          // vertices tested for the last vessel that reached the vertex pass
	uint32_t pass = 0;                            // frames run: the stride start turns with it
private:
	struct Lock { uint32_t part; Vector p; double t, vn; };            // contact region of a recent event: part, vessel frame point, sim time, approach
	struct Cand { uint32_t part, vtx; Vector p, g, vr; double vn, hp; }; // vertex: vessel frame, global, relative velocity, approach and predicted height on the plane
	int Vessel (const CollGroundVessel &x, double simt, double simdt, CollImpactEvent &e, CollFxContact &c); // 0 no candidate, 1 none fired, 2 event
	static bool Locked (const std::vector<Lock> &l, const Cand &c, double rb);
	CollSdk &sdk;
	const CollCfgValues &cfg;
	std::map<uint32_t, std::vector<Lock>> locks;  // per vessel: regions of the events of the last COLL_GROUND_GAP
};
#endif
