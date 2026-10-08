// not upstream: collision addon, the session object: cfg values, pending scenario block, vessel ids, counters, the E1-E3 parts (Design CA E4 11.3)
#ifndef COLLSESSION_H
#define COLLSESSION_H
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "CollCfg.h"
#include "CollPlatform.h"
#include "CollSdk.h"
#include "CollStore.h"

class CollGeomSession;
class CollPhysSession;
class CollPhysGeom;
class CollSolveHost;
class CollDmgSession;
class CollDmgHost;
class CollShapeSink;
struct CollOwnerRef;

struct CollCounters { uint64_t frames = 0, contacts = 0, events = 0, writes = 0, probes = 0, vtx = 0, matrix = 0, notices = 0, spec = 0, free = 0, missed = 0; };

struct CollTimers {                         // microseconds summed over the frames; the stages of E1, E2, E3 add theirs
	double prestep = 0, prestepMax = 0, e1 = 0, e2 = 0, e3 = 0, post = 0, build = 0, firstFrame = 0;
	uint64_t posts = 0;
};

class CollSession {
public:
	typedef std::chrono::steady_clock Clock;
	CollSession (uint32_t serial, const CollCfgValues &cfg);
	~CollSession ();
	uint32_t Serial () const { return serial; }
	const CollCfgValues &Cfg () const { return cfg; }
	bool Started () const { return started; }
	uint32_t IdOf (OBJHANDLE h);            // first sight gives the next id (from 1; 0 means a building); ids are never reused in the session
	OBJHANDLE Vessel (uint32_t id) const;   // NULL once deleted
	uint32_t IdCount () const { return (uint32_t)vessel.size (); }
	void Load (FILEHANDLE scn);             // opcLoadState: E3 Parse into the pending store
	void Save (FILEHANDLE scn);             // opcSaveState
	void Start (int renderMode);            // clbkSimulationStart: gcCore, E2, E1, E3 Begin, A1
	void NewVessel (OBJHANDLE h, bool inStep);    // clbkNewVessel; queued to PS8 while in the pre-step
	void DeleteVessel (OBJHANDLE h, bool inStep); // clbkDeleteVessel; the id is dropped at once, the parts purged at PS8 while in the pre-step
	void PreStep (double simt, double simdt);     // PS1-PS7 (E4 11.4)
	void ApplyQueued ();                    // PS8: vessels created or deleted inside the pre-step
	void TimeJump ();                       // E1 time-jump rule, E2 MarkJump
	void VesselJump (OBJHANDLE h);
	void TimeAccChanged (double newWarp);
	void Pause (bool pause);
	void KeyPass ();
	void PostStep ();                       // PO1-PO4
	void FrameBegin ();                     // pre-step timer
	void FrameEnd ();
	void Close ();                          // session end: E3 End, A3 from counters, no world access
	CollDmgSession *Dmg () const { return started ? dmg.get () : nullptr; }
	CollSdk &Sdk () { return *sdk; }
	CollCounters n;
	CollTimers t;
private:
	struct Op { bool del; uint32_t id; OBJHANDLE h; };
	void AddVessel (uint32_t id, OBJHANDLE h);
	void PurgeVessel (uint32_t id);
	std::string Who (const CollOwnerRef &o);
	uint32_t serial;
	CollCfgValues cfg;
	bool started = false;
	std::vector<OBJHANDLE> vessel;          // by id; id 0 unused
	std::unordered_map<OBJHANDLE, uint32_t> idOf; // live handles only; looked up, never iterated
	std::vector<Op> queued;                 // vessel changes raised inside the pre-step
	CollStoreBlock pending;                 // the pending scenario-block store until E3 Begin
	std::unique_ptr<CollSdk> sdk;           // the session instance
	std::unique_ptr<CollGeomSession> geom;  // E2
	std::unique_ptr<CollPhysGeom> pgeom;    // E1's view of E2
	std::unique_ptr<CollSolveHost> shost;   // materials and hit features for the solver
	std::unique_ptr<CollPhysSession> phys;  // E1
	std::unique_ptr<CollDmgHost> dhost;     // E3's view of E2 and the id map
	std::unique_ptr<CollDmgSession> dmg;    // E3
	std::unique_ptr<CollShapeSink> sink;    // E2 Deliver -> E3 ShapesUpdated
	Clock::time_point t0;
};
#endif
