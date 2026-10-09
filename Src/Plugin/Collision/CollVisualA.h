// not upstream: collision addon, E3 client mirrors of dented vessel meshes and the visual sync rule (Design CA E3 3)
#ifndef COLLVISUALA_H
#define COLLVISUALA_H
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "CollCfg.h"
#include "CollDmgHost.h"
#include "DentMath.h"

// client mirror of one dented vessel mesh slot; values as the client stores them
struct DentMeshCopyA {
	std::shared_ptr<const CollRestMesh> rest;           // E2's rest mesh, never edited
	uint32_t serial = 0, slot = 0, key = 0; uint16_t ngrp = 0; uint32_t nvtx = 0;
	std::vector<std::vector<DentVtx>> rp, cur;          // per group: client rest, rest + records (DentMath::Apply arrays)
	struct Grp {
		std::vector<DentVtx> pushed;                    // what the client holds
		std::vector<uint16_t> edit;                     // vertices where cur differs from rp
		bool module = false;                            // a write the addon did not make was seen: no visual dent
		bool big = false;                               // over 65536 vertices: WORD indices cannot reach all, no visual dent
		uint32_t skips = 0;                             // passes skipped with return 1
	};
	std::vector<Grp> g;
	std::vector<std::vector<uint16_t>> idx;
	std::vector<std::vector<uint32_t>> weld; uint32_t nweld = 0; std::vector<Vector> restSum;
	std::vector<uint32_t> cls;                          // pose class per group; the weld map stays within one class
	std::vector<DentRecord> done;                       // records in cur, in order: a list that only grows is applied incrementally
	std::vector<std::vector<uint8_t>> cutDirty;         // dmg3 tear: vertices a cut record moved (Apply's dirty set)
	std::vector<Vector> sites;                          // blast: sites cur was built with
	bool norec = false;                                 // no record left (repair): freed once nothing is sent
	uint32_t nullWait = 0; bool loggedNull = false;     // DevMesh NULL: retry every 64 passes
	uint32_t skips = 0;                                 // consecutive passes the slot was skipped with return 1
};

struct CollVisCounters { uint64_t reads = 0, writes = 0, rebuilds = 0, module = 0, refresh = 0, pushes = 0, builds = 0, incr = 0; }; // builds: cur from rest, incr: new records only

class CollVisualA {
public:
	enum { PASS_ALL = 0, PASS_PENDING = 1 };
	enum { MODE_FULL = 0, MODE_ABSOLUTE = 1, MODE_OFF = 2 };                // 3.11
	CollVisualA (CollSdk &sdk, CollDmgHost &host, const CollCfgValues &cfg) : sdk (sdk), host (host), cfg (cfg) {}
	void SetRecords (uint32_t id, const std::string &name, const CollDmgSlot &s, uint32_t slot, const std::vector<const DentRecord *> &rec, const DentSites *sites = nullptr); // build or recompute cur; pushed kept; sites: blast VCUT
	static std::vector<uint16_t> StaticGroups (const CollShape *sh, uint32_t mesh, const CollRestMesh &rest); // blast: groups of the pose class with the most vertices (all when one class)
	void DropSlot (uint32_t id, uint32_t slot);
	void DropVessel (uint32_t id);
	void Rebuilt (uint32_t id, uint32_t slot, uint32_t serial, bool gccore); // SLOTEV_REBUILT: copy takes the serial; gccore: pushed := rp
	void Pass (int which);                                                  // PASS_ALL (post-step, pause) or PASS_PENDING (keyboard)
	bool Has (uint32_t id) const { auto it = ves.find (id); return it != ves.end () && !it->second.copy.empty (); }
	const DentMeshCopyA *Copy (uint32_t id, uint32_t slot) const;
	uint32_t ModuleGroups (uint32_t id) const;
	int Mode () const { return mode; }
	CollVisCounters n;
private:
	struct Ves { CollH lastVis = nullptr; std::string name; std::map<uint32_t, DentMeshCopyA> copy; bool pending = false; };
	bool SyncCopy (uint32_t id, CollH h, CollH vis, Ves &v, DentMeshCopyA &c); // false: stop the pass (no client)
	CollSdk &sdk;
	CollDmgHost &host;
	const CollCfgValues &cfg;
	std::map<uint32_t, Ves> ves;
	int mode = MODE_FULL;
	bool warned = false, loggedBig = false;
};
#endif
