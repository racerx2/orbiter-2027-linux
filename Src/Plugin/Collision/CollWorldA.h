// not upstream: collision addon E1, the SDK-facing part: snapshot, solver bodies, write-back through CollSdk, notices, warp guard
#ifndef __COLLWORLDA_H
#define __COLLWORLDA_H
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "CollAddonFrame.h"
#include "CollCfg.h"
#include "CollSdk.h"

struct CollPhysAsm { std::vector<uint32_t> member; uint32_t root = 0; bool stack = false, mixed = false; uint64_t memberHash = 0; }; // E2 CollAssembly
struct CollPhysVessel { uint32_t id; CollH h; };    // session vessel id and its handle this frame

// what E1 needs from E2 each frame; the integrator adapts CollGeomView (E2 8) to it
class CollPhysGeom {
public:
	virtual ~CollPhysGeom () = default;
	// collider parts in the vessel frame: fwd P0 = pose[1], P1 = next; past P0 = pose[0], P1 = pose[1] (E1 4.3); false: no collider
	virtual bool Parts (uint32_t id, std::vector<CollPartRef> &fwd, std::vector<CollPartRef> &past, double &rmax) = 0;
	virtual void Assemblies (std::vector<CollPhysAsm> &out) { out.clear (); } // empty: every vessel its own body
	virtual void Bases (double h, std::vector<CollABody> &out) { out.clear (); (void)h; } // kinematic base bodies (kind COLLB_BASE) with parts and motion
	virtual void Zones (const std::vector<CollABody> &b, std::vector<CollZone> &out) { out.clear (); (void)b; } // active docking and attachment zones (8)
	virtual CollH BaseHandle (int planet, int base) { (void)planet; (void)base; return nullptr; }
	virtual void PartToVessel (uint32_t id, int mesh, Vector &p, Vector &n) { (void)id; (void)mesh; (void)p; (void)n; } // part rest frame -> vessel frame with pose[1]
};

// E1's part of CollSession (E4 11.3); stage functions as E4 11.4
class CollPhysSession {
public:
	CollPhysSession (CollSdk &sdk, CollPhysGeom &geom, const CollCfgValues &cfg);
	void Start (const std::function<bool (const char *key, std::string &val)> &orbCfg); // clbkSimulationStart: mirror from Orbiter.cfg (CollOrbCfg::String)
	void OnNewVessel (uint32_t id);                // clbkNewVessel (queued while in the pre-step)
	void OnDeleteVessel (uint32_t id);             // clbkDeleteVessel
	void OnTimeJump ();                            // clbkTimeJump, or PS8 for one raised inside the pre-step
	void OnVesselJump (uint32_t id);               // clbkVesselJump
	void OnTimeAccChanged (double newWarp);        // clbkTimeAccChanged: clamp to the last computed W (9.3)
	void PS1Snapshot (double simt, double simdt, const std::vector<CollPhysVessel> &v); // reads only
	void PS3Physics (CollSolveHost &host);         // classification, frame driver, every write
	const std::vector<CollImpactEvent> &Events () const { return ev; } // PS4: E3 Commit
	void PS5Notices ();                            // CONTACT notices (11)
	void PS6Warp ();                               // warp guard (9.2)
	void End ();                                   // memory only
	const CollAStats &Stats () const { return frame.Stats (); }
	uint64_t Notices () const { return nNotices; }
	double LastW () const { return lastW; }
	CollAddonFrame frame;
	CollOrbMirror mirror;
	CollDetect fwd, ver;
private:
	struct Snap { uint32_t id; CollH h; CollVesselRead rd; };
	struct Last { uint32_t id; uint8_t kind; uint64_t hash; };
	CollSdk &sdk;
	CollPhysGeom &geom;
	CollCfgValues cfg;
	std::vector<Snap> snap;
	std::vector<CollABody> body;
	std::vector<CollImpactEvent> ev;
	std::vector<uint32_t> queuedNew, jumped;
	std::vector<Last> last;                        // kind and member hash of each body at the last pre-step (entry bits)
	double simt = 0, simdt = 0, lastW = 1e100, sysPrev = -1, warpMsgSys = -1e100;
	std::vector<std::pair<double, double>> frameDt;// system time, real frame time: the longest of the last 2 s (9.2)
	bool started = false, inClamp = false, jumpPending = false;
	uint64_t nNotices = 0;
	const Snap *SnapOf (uint32_t id) const;
	void Log (int level, const char *fmt, ...);
};
#endif
