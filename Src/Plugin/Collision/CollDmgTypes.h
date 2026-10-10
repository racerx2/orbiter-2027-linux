// not upstream: collision addon, dmg3 interfaces between damage (area S), parts (area P) and effects (area F) (design-CA-dmg3)
#ifndef COLLDMGTYPES_H
#define COLLDMGTYPES_H
#include <cstdint>
#include <memory>
#include <vector>
#include "CollSdk.h"
#include "DentMath.h"
#include "Vecmat.h"

class CollShape;
class CollDmgSession;
struct CollCfgValues;

struct CollDamageHit {                                   // one vessel side of a dent event, vessel frame (L2)
	uint32_t id = 0, other = 0; CollH h = nullptr;       // vessel; other vessel id, 0 building
	uint32_t mesh = 0; int grp = -1, tri = -1, rec = -1; // hit render feature; record index in the vessel's list
	Vector c, n, tdir;                                   // contact centre, outward normal, slip direction of this side
	double E = 0, eSpec = 0, Esurplus = 0;               // J, J/kg of empty mass, J the capped solve could not place
	double R = 0, depth = 0;                             // dent radius, max displacement of this event [m]
	double Mp = 0;                                       // dmg3 tear: hinge plastic moment of this event [J/rad]
	double Jn = 0, dt = 0;                               // blast: normal impulse of the event [N s], frame step [s]
	uint32_t mode = 0;                                   // DentParams mode
	double vn = 0, vt = 0;                               // approach and slip speed [m/s]
	uint32_t evflags = 0;                                // COLLEV_*
	double simt = 0; bool playback = false;
	const DentMaterial *mat = nullptr;
};
enum : int { CBRK_PART = 0, CBRK_GLASS = 1, CBRK_INTERIOR = 2, CBRK_SECTION = 3, CBRK_RESTHIDDEN = 4, CBRK_CELL = 5 }; // CBRK_CELL: blast cell debris, recorder T rows only
enum : uint32_t { CBRK_FN_TD = 1, CBRK_FN_THR = 2, CBRK_FN_DOCK = 4, CBRK_FN_GEAR = 8 }; // dmg3 tear: CollPieceA::functional bits
struct CollBlastBreak {                                  // blast: one actor that separated from the main structure of a vessel slot (design-CA-blast 2, 4)
	uint32_t id = 0, slot = 0, other = 0;                // vessel, mesh slot, impactor id (0 none)
	std::vector<uint32_t> cells;                         // Voronoi cells (sorted)
	std::vector<uint32_t> pieces;                        // animated pieces (CollBreakA piece indices of the slot)
	double mass = 0;                                     // [kg]
	Vector centroid, dv, dw;                             // vessel frame: centre of mass; separation velocity and spin relative to the parent
	Vector n;                                            // vessel frame: unit outward direction of the split
	Vector inertia;                                      // vessel frame: point-mass inertia diagonal of the chunks about the vessel origin [kg m^2]
	bool crushed = false;                                // torn off by the crush: the debris keeps no crush record
	double simt = 0;
};
struct CollBreakEvent { uint32_t id = 0; int kind = 0; Vector c, n; double r = 0; bool playback = false; }; // vessel frame (L3)
struct CollFxContact {                                   // one vessel side of a contact pair this frame, slides included (L4)
	uint32_t id = 0; CollH h = nullptr;
	Vector c, n, tdir;                                   // vessel frame
	double vn = 0, vt = 0, Jn = 0, Jt = 0, dt = 0;       // speeds [m/s]; total normal and tangent impulse of the frame [N s]; frame step [s]
	uint32_t flags = 0; bool building = false, playback = false;
	Vector nOther;                                       // building frame normal (buildings)
	const DentMaterial *mat = nullptr, *matOther = nullptr;
};

// hooks of the parts (P) and effects (F) units; CollDmgSession calls them, defaults do nothing
class CollDmgSink {
public:
	virtual ~CollDmgSink () {}
	virtual void Hit (const CollDamageHit &) {}
	virtual void Break (const CollBreakEvent &) {}
	virtual void Contact (const CollFxContact &) {}
	virtual void Destroyed (uint32_t) {}
	virtual void Shapes (uint32_t, CollShape *) {}       // PS2: shapes updated (collider hide re-apply)
	virtual void Post (double, double) {}                // post-step after the visual pass: simt, simdt
	virtual void PreStep (double, double) {}                     // pre-step before the solver: vessel creation and state writes (a post-step write lands a step off)
	virtual void Pass () {}                              // key and pause passes
	virtual void Torn (uint32_t, const DentTorn &) {}    // playback or load: one torn row to apply
	virtual void Repair (uint32_t) {}
	virtual void DropVessel (uint32_t, CollH) {}         // vessel about to be deleted, still alive
	virtual void TimeJump () {}
	virtual void End () {}                               // clbkSimulationEnd: no client call except mesh frees allowed by the unit
	virtual void Quiet () {}                             // failure path: stop emitting, no SDK call
	virtual bool Hidden (uint32_t, uint32_t, uint32_t) const { return false; } // id, mesh, group hidden by parts
};
std::unique_ptr<CollDmgSink> CollMakeBreak (CollSdk &sdk, CollDmgSession &s, const CollCfgValues &cfg); // CollBreakA.cpp
std::unique_ptr<CollDmgSink> CollMakeFx (CollSdk &sdk, CollDmgSession &s, const CollCfgValues &cfg);    // CollFxA.cpp
#endif
