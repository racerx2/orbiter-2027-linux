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
constexpr double   COLL_GROUND_GAP    = 0.25;  // no second ground event of a vessel within this [s]
constexpr double   COLL_GROUND_PATCH  = 0.5;   // contact patch radius of both sides [m]
constexpr double   COLL_GROUND_NEAR   = 0.5;   // near test: CG altitude below bound radius + |v| dt + this [m]
constexpr double   COLL_GROUND_REFINE = 0.1;   // the hit vertex above its own terrain by more than |v| dt + this: no event [m]
constexpr double   COLL_GROUND_REACH  = 25e3;  // no terrain query this far above the planet radius plus the near distance [m]
constexpr uint32_t COLL_GROUND_VTX    = 4096;  // collider vertices tested per vessel and frame, at most

struct CollGroundVessel { uint32_t id; CollH h; const CollShape *shape; }; // one live vessel with its collider

inline bool CollGroundSide (const CollOwnerRef &o) { return !o.vesselId && o.planet >= 0 && o.base < 0; } // the ground side of an impact event
double CollGroundMeff (double m, const Vector &pmi, const Vector &r, const Vector &n); // 1 / (1/m + (r x n) . I^-1 (r x n)), I = m pmi, vessel frame; pmi axes <= 0 locked

class CollGroundA {
public:
	CollGroundA (CollSdk &sdk, const CollCfgValues &cfg);
	void Frame (double simt, double simdt, const std::vector<CollGroundVessel> &v, std::vector<CollImpactEvent> &ev, std::vector<CollFxContact> &fx); // appends; reads only
	void TimeJump () { last.clear (); }
	void Drop (uint32_t id) { last.erase (id); }  // vessel deleted
	uint64_t events = 0;                          // ground events made
	uint32_t tested = 0;                          // vertices tested for the last vessel that reached the vertex pass
private:
	bool Vessel (const CollGroundVessel &x, double simt, double simdt, CollImpactEvent &e, CollFxContact &c);
	CollSdk &sdk;
	const CollCfgValues &cfg;
	std::map<uint32_t, double> last;              // sim time of each vessel's last ground event
};
#endif
