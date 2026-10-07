// not upstream: collision addon, E3 session part: impact events to dents, collider re-apply, destroyed state, thrust cut, notices, repair, block and recorder (Design CA E3)
#ifndef COLLDAMAGEA_H
#define COLLDAMAGEA_H
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "CollCfg.h"
#include "CollDmgHost.h"
#include "CollSdk.h"
#include "CollShape.h"
#include "CollSolve.h"
#include "CollStore.h"
#include "CollVisualA.h"
#include "DentMath.h"

constexpr double COLL_DUMMY_MAXMASS = 1e-6;                  // kg; positive, so no fuel level is 0/0 (5.3)
enum : uint32_t { XDMG_DESTROYED = 1, XDMG_MODULEFX = 2, XDMG_CATASTROPHIC = 4 };
enum : int { CDMG_KIND_DENT = 16, CDMG_KIND_DESTROYED = 17, CDMG_KIND_RESTORED = 18, CDMG_KIND_REPAIRED = 19 }; // COLLA_KIND_*
constexpr int CDMG_HANDLED = 0x58464C43;                      // COLLA_HANDLED

// thrust cut of one destroyed vessel through a dummy tank, kept as the last tank; re-applied every pre-step
struct ThrustCutA { CollH dummy = nullptr; std::vector<std::pair<CollH, CollH>> wish; uint32_t relinks = 0, run = 0, remakes = 0; bool loggedRefill = false, loggedRun = false; };

struct VesselDamageA {
	uint32_t id = 0; std::string name, cls;                   // cached for logs and the end of the session
	DentVesselText d;                                         // eabs, flags, records in application order
	std::vector<int> match;                                   // per record: live slot, -1 waits (2.3)
	std::map<uint32_t, std::vector<size_t>> applied;          // per mesh: records on the collider since its last Replaced, in order
	ThrustCutA cut;
	uint32_t alias = ~0u;                                     // recorder side-file alias
	bool loggedCap = false, playback = false;
};
struct BuildingDamageA { CollDmgBaseObj obj; double eabs = 0; uint32_t flags = 0; };
struct NoticeA {                                              // one notice of this pre-step
	uint32_t id = 0; int kind = 0; uint32_t flags = 0;
	CollH hOther = nullptr; int32_t otherObj = -1, mesh = -1, group = -1;
	double simt = 0, energy = 0, depth = 0; Vector pos, nml;
};
struct RepairReq { uint32_t id = 0; bool building = false; std::string planetBase; int obj = -1; };
struct CollE3Counters { uint64_t dents = 0, coalesced = 0, repairs = 0, relinks = 0, remakes = 0, notices = 0, playback = 0, side = 0; };

struct CollRecLink  { bool active = false, failed = false; std::string id; double t0 = 0; std::ofstream file; std::vector<std::string> buf; uint32_t nalias = 0; };
struct CollPlayLink { bool active = false, read = false, warned = false; std::string id; CollSideFile f; size_t cursor = 0; std::map<uint32_t, uint32_t> aliasId; };

class CollDmgSession {
public:
	CollDmgSession (CollSdk &sdk, CollDmgHost &host, const CollCfgValues &cfg);
	// lifecycle (1.6): E4 calls these
	static bool Parse (CollSdk &sdk, CollH scn, CollStoreBlock &pending);    // opcLoadState: the block into E4's pending store
	void Begin (CollStoreBlock &&pending);                                   // clbkSimulationStart
	void End ();                                                             // clbkSimulationEnd: log only, no world call
	void Save (CollH scn);                                                   // opcSaveState: the block body after the core's BEGIN line
	void SaveLines (std::vector<std::string> &out);                         // same, as lines (header first)
	// pre-step stages (E4 11.4)
	template <class Ev> void ShapesUpdated (uint32_t id, CollShape *shape, const std::vector<Ev> &ev) // PS2, from E2's Deliver
	{
		std::vector<CollDmgSlotEv> e;
		e.reserve (ev.size ());
		for (const auto &x : ev) e.push_back (CollDmgSlotEv { x.mesh, x.what, x.src });
		ShapesUpdatedEv (id, shape, e);
	}
	void ShapesUpdatedEv (uint32_t id, CollShape *shape, const std::vector<CollDmgSlotEv> &ev);
	void PrePhysics ();                                                      // PS2b: repairs, playback, recording link check
	void Commit (const std::vector<CollImpactEvent> &ev, double simt);       // PS4
	void SendNotices ();                                                     // PS5, after E1's CONTACT notices
	void EndFrame ();                                                        // PS7: thrust cut, last
	// other callbacks
	void PostStep ();                                                        // PO2: dummy tanks kept last, then the main visual pass
	void KeyPass ();                                                         // clbkProcessKeyboardImmediate (CollisionKeyPass)
	void PausePass ();                                                       // clbkPause (true)
	void OnNewVessel (CollH h);                                              // nothing to attach (7.5)
	void OnDeleteVessel (uint32_t id);
	// exported API (10)
	int RepairVessel (CollH h);
	int RepairBuilding (const char *planetBase, int obj);
	int GetVesselDamage (CollH h, void *info);                               // COLLA_DAMAGEINFO *
	int GetBuildingDamage (const char *planetBase, int obj, double *eabs, uint32_t *flags);
	// dialog content and the 2024 report
	void Report (std::vector<std::string> &lines) const;
	const VesselDamageA *Damage (uint32_t id) const { auto it = vessel.find (id); return it == vessel.end () ? nullptr : &it->second; }
	const std::map<uint32_t, VesselDamageA> &Vessels () const { return vessel; }
	const std::map<std::pair<std::pair<int, int>, uint32_t>, BuildingDamageA> &Buildings () const { return building; }
	size_t QueuedRepairs () const { return repairs.size (); }
	CollH VesselHandle (uint32_t id) { return host.Vessel (id); }
	CollSdk &Sdk () { return sdk; }
	std::string sideDir = "Flights/_Collision";                             // recorder side files (8.3)
	CollE3Counters n;
	CollVisualA vis;
private:
	VesselDamageA *Find (uint32_t id);
	VesselDamageA &Get (uint32_t id);
	void MatchAll ();
	void Rematch (VesselDamageA &v);
	void SyncCollider (VesselDamageA &v, CollShape *sh, uint32_t mesh, bool force);
	void SyncMirror (VesselDamageA &v, uint32_t mesh);
	void Dent (VesselDamageA &v, CollH h, const CollImpactSide &s, double E, const DentMaterial &mat, double t, NoticeA &note);
	void DestroyedTest (VesselDamageA &v, CollH h, double Ei, double t, uint32_t extraFlags);
	void DoRepair (VesselDamageA &v, bool playback);
	double Threshold (uint32_t id) const;
	void Cut (CollH h, VesselDamageA &v, bool want);
	void KeepLast (CollH h, ThrustCutA &c);
	bool IsDummy (CollH h, CollH tk);
	bool HasTank (CollH h, CollH tk);
	bool HasThruster (CollH h, CollH th);
	bool AnyVesselHasTank (CollH tk);
	void Queue (uint32_t id, int kind, const NoticeA *base);
	void Side (const std::string &line);
	uint32_t Alias (VesselDamageA &v, CollH h);
	void StartRecording ();
	void Playback (double simt);
	void Log (const char *fmt, ...);
	CollSdk &sdk;
	CollDmgHost &host;
	const CollCfgValues &cfg;
	CollStoreBlock blk;                                                      // adopted block: dormant lines until matched
	std::map<uint32_t, VesselDamageA> vessel;
	std::map<std::pair<std::pair<int, int>, uint32_t>, BuildingDamageA> building; // (planet, base) indices, object index
	std::vector<std::vector<std::string>> dormantVessel;                     // raw sections
	std::vector<DentBaseText> dormantBase;                                   // unmatched OBJ lines and ODENT lines
	std::vector<NoticeA> notices;
	std::map<uint32_t, NoticeA> frameNote;                                   // DENT accumulation of this pre-step
	std::vector<RepairReq> repairs;
	CollRecLink rec;
	CollPlayLink play;
	double frameT = 0;                                                       // pre-step time of Commit's frame
	bool begun = false, matched = false, loggedNoFirst = false;
};

namespace CollUiA {
	const char *Label ();                                                    // "Collision damage"
	const char *Description ();                                              // E4 copies it into its static char[]
	void OnCommand (CollSdk &proc, CollDmgSession *s, void *dialog);         // open E4's dialog object, or the 2024 log report; nothing without a session
}
#endif
