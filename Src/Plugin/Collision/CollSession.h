// not upstream: collision addon, the session object: cfg values, pending scenario block, vessel ids, counters (Design CA E4 11.3)
#ifndef COLLSESSION_H
#define COLLSESSION_H
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "CollCfg.h"
#include "CollPlatform.h"

struct CollCounters { uint64_t frames = 0, contacts = 0, events = 0, writes = 0, probes = 0, vtx = 0, matrix = 0, notices = 0, spec = 0, free = 0, missed = 0; };

struct CollTimers {                         // microseconds summed over the frames; the stages of E1, E2, E3 add theirs
	double prestep = 0, prestepMax = 0, e1 = 0, e2 = 0, e3 = 0, post = 0, build = 0, firstFrame = 0;
	uint64_t posts = 0;
};

class CollSession {
public:
	typedef std::chrono::steady_clock Clock;
	CollSession (uint32_t serial, const CollCfgValues &cfg);
	uint32_t Serial () const { return serial; }
	const CollCfgValues &Cfg () const { return cfg; }
	bool Started () const { return started; }
	uint32_t IdOf (OBJHANDLE h);            // first sight gives the next id; ids are never reused in the session
	OBJHANDLE Vessel (uint32_t id) const;   // NULL once deleted
	uint32_t IdCount () const { return (uint32_t)vessel.size (); }
	void Forget (OBJHANDLE h);              // clbkDeleteVessel: the id keeps NULL, a reused handle value gets a new id
	void Load (FILEHANDLE scn);             // opcLoadState: the block into the pending store
	void Save (FILEHANDLE scn);             // opcSaveState
	void Start (int renderMode);            // clbkSimulationStart: ids of the scenario vessels, the pending block adopted, A1
	void FrameBegin ();                     // pre-step timer
	void FrameEnd ();
	void PostBegin ();
	void PostEnd ();
	void Close ();                          // session end: A3 from counters only, no world access
	CollCounters n;
	CollTimers t;
private:
	uint32_t serial;
	CollCfgValues cfg;
	bool started = false;
	std::vector<OBJHANDLE> vessel;          // by id
	std::unordered_map<OBJHANDLE, uint32_t> idOf; // live handles only; looked up, never iterated
	std::vector<std::string> pending;       // the pending scenario-block store: raw lines until E3's CollStore (E3 step 2)
	std::vector<std::string> block;         // the adopted block, written back by Save until E3 owns it
	Clock::time_point t0;
};
#endif
