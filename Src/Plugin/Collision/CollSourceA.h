// not upstream: collision addon, E2's per-session vessel sources: slots, CollAnim, CollShape feeding, outputs, assemblies (design E2 2-9)
#ifndef __COLLSOURCEA_H
#define __COLLSOURCEA_H
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "CollAnim.h"
#include "CollCfg.h"
#include "CollMeshFile.h"
#include "CollSdk.h"
#include "CollShape.h"

class CollBaseA;
class CollBldgA;
struct CollBaseRec;
struct CollBaseObjView;

enum : uint8_t { SLOT_DEAD = 0, SLOT_TPL = 1, SLOT_NAME = 2 };
enum : uint8_t { SLOTEV_REPLACED, SLOTEV_GONE, SLOTEV_REBUILT };   // slot replaced (serial++), killed, client rebuild applied (serial++)
enum : uint8_t { E2_POLL, E2_GCCORE, E3_SENTINEL };                // source of the event
enum : uint8_t { VIS_NOVIS, VIS_VIS, VIS_HEADLESS };               // 6.2
enum : uint8_t { ASM_STACK = 1, ASM_MIXED = 2, ASM_CHANGED = 4 };  // CollAssembly::flags; CHANGED: memberHash differs from the last pre-step

struct CollSlotEvent { uint32_t mesh; uint8_t what, src; };
class CollShapeSink {
public:
	virtual ~CollShapeSink () = default;
	virtual void ShapesUpdated (uint32_t id, CollShape *shape, const std::vector<CollSlotEvent> &ev) = 0; // once per vessel per pre-step
};

struct CollSlotView {
	uint8_t kind = SLOT_DEAD; bool present = false;
	uint32_t key = 0; uint16_t ngrp = 0; uint32_t nvtx = 0;
	std::shared_ptr<const CollRestMesh> rest;
	std::string name;
	uint32_t serial = 0;
};

struct CollVesselGeom {
	uint32_t id = 0; CollH h = nullptr; uint32_t flags = 0;
	const CollShape *shape = nullptr;
	std::vector<CollAffine> next;
	std::vector<Vector> nextSc; std::vector<double> nextSr;
	std::vector<double> motionNext, predErr;
	Vector nextC; double nextR = 0;
	bool animating = false;
};

struct CollAssembly { std::vector<uint32_t> member; uint32_t root; CollH sv; uint64_t memberHash; uint8_t flags; };
struct CollPortRec { uint32_t vessel, port; Vector pos, dir, rot; CollH mate; int32_t matePort; };
struct CollAttRec  { uint32_t vessel, index; bool toparent; Vector pos, dir, rot; char id[9]; CollH mate; int32_t mateIndex; };
struct CollClassKeys { bool enableCollider = true; double dockZoneRadius = 1.5, destroyEnergy = -1; uint8_t meshProbe = 0; int8_t fixed = -1; double fixedClear = 0; }; // meshProbe 1 = ONCE; fixed: FixedStructure, -1 not set; fixedClear: FixedStructureClear [m] as read

constexpr double COLL_FIXED_RHO = 1e-4; // [kg/m^3] a landed vessel lighter than this per m^3 of its size sphere has a placeholder mass (design fixed-structures)
bool CollFixed (int key, bool landed, double mEmpty, double size); // landed and FixedStructure TRUE, or no key and a placeholder mass
constexpr double COLL_FIXED_CLEAR_MAX = 5000.0; // [m] FixedStructureClear's cap (FixedVes::MaxClear)
double CollFixedClear (double key);           // the clear zone radius a FixedStructureClear value gives: 0 when not finite or not positive, at most the cap

class CollGeomView {
public:
	virtual ~CollGeomView () = default;
	virtual const CollVesselGeom *Geom (uint32_t id) const = 0;
	virtual const std::vector<CollSlotView> &Slots (uint32_t id) const = 0;
	virtual bool SlotNow (uint32_t id, uint32_t mesh, uint32_t &serial) = 0;
	virtual void ClientMeshRebuilt (uint32_t id, uint32_t mesh, uint8_t src) = 0;
	virtual void WantSlots (uint32_t id, bool on) = 0;
	virtual bool GroupAnimated (uint32_t id, uint32_t mesh, uint32_t grp) const = 0;
	virtual const CollClassKeys &Keys (uint32_t id) const = 0;
	virtual bool Fixed (uint32_t id) const { (void)id; return false; } // a fixed structure now (CollFixed)
	virtual const std::vector<CollAssembly> &Assemblies () const = 0;
	virtual const std::vector<CollPortRec> &Ports () const = 0;
	virtual const std::vector<CollAttRec> &AttachPoints () const = 0;
	virtual const CollBaseRec *BaseRec (int planet, int base) const = 0;
	virtual const CollBaseObjView *BaseObject (int planet, int base, int obj) const = 0;
	virtual void Bases (std::vector<const CollBaseObjView *> &all) const = 0;
};

// one slot as last polled (3.2)
struct CollSlotRec {
	uint8_t kind = SLOT_DEAD; CollH tpl = nullptr; std::string name;
	Vector ofs; uint16_t mode = 0;
	uint32_t serial = 0;
	bool present = false;
	CollMeshInfo info {};
};

// one tracked animation component (6.5)
struct CollCompRec {
	const ANIMATIONCOMP *ac; uint32_t an;
	const void *trans; double s0, s1;
	const ANIMATIONCOMP *parent;
	uint64_t fpStatic, fpMove;
};

struct CollVesselSrc {
	uint32_t id = 0; CollH h = nullptr; std::string name, cls;
	std::vector<CollSlotRec> slot;
	std::vector<CollSlotView> view;
	std::vector<uint8_t> present;
	uint32_t lastCount = 0;
	CollAnim anim;
	std::vector<CollCompRec> comp;
	uint32_t nanimSeen = 0; int busy = 0; bool layoutChanged = false;
	uint8_t vis = VIS_NOVIS; bool everStepped = false, needRewind = false, firstVisCheck = false;
	std::vector<double> sPrev, sCur, sPrev2; double dtPrev = 0, dtCur = 0; bool histValid = false;
	std::unique_ptr<CollShape> shape;
	CollVesselGeom geom;
	std::vector<CollAffine> lastNext; std::vector<uint32_t> lastNextVer;
	std::vector<CollSlotEvent> ev;
	std::vector<std::pair<uint32_t,uint8_t>> rebuildQ;
	CollClassKeys keys;
	bool wantSlots = false, polled = false, probeOnce = false, collider = true;
	int8_t fixedLogged = -1;                     // the fixed verdict last logged
	bool hiddenLogged = false;                   // its hidden groups logged once
};

// E2's part of CollSession (2.1); every stage function named as E4 11.4 calls it
class CollGeomSession : public CollGeomView {
public:
	CollGeomSession (CollSdk &sdk, const CollCfgValues &cfg);
	~CollGeomSession () override;
	// session lifecycle (2.2)
	void ReadOrbiterCfg ();                         // at session creation: MeshDir, ConfigDir, E1's keys
	void SimulationStart (int renderMode, bool renderWindow); // client mode, bases, scan of unknown vessels
	void NewVessel (uint32_t id, CollH h);          // pending record
	void DeleteVessel (uint32_t id);
	void BeginFrame (double simt, double simdt);    // PS1: E2 PreStep (2.3)
	void Deliver (CollShapeSink &sink);             // PS2: ShapesUpdated once per vessel
	void PostStepPoll ();                           // post-step: animation snapshots only
	void TimeJump ();                               // MarkJump, prediction history cleared
	void EndSession ();                             // frees memory only, no SDK call
	std::function<uint32_t (CollH)> idOf;           // the session's vessel id (CollSession::IdOf); unset: own counter
	// CollGeomView
	const CollVesselGeom *Geom (uint32_t id) const override;
	const std::vector<CollSlotView> &Slots (uint32_t id) const override;
	bool SlotNow (uint32_t id, uint32_t mesh, uint32_t &serial) override;
	void ClientMeshRebuilt (uint32_t id, uint32_t mesh, uint8_t src) override;
	void WantSlots (uint32_t id, bool on) override;
	bool GroupAnimated (uint32_t id, uint32_t mesh, uint32_t grp) const override;
	const CollClassKeys &Keys (uint32_t id) const override;
	bool Fixed (uint32_t id) const override;
	const std::vector<CollAssembly> &Assemblies () const override { return asm_; }
	const std::vector<CollPortRec> &Ports () const override { return port; }
	const std::vector<CollAttRec> &AttachPoints () const override { return att; }
	const CollBaseRec *BaseRec (int planet, int base) const override;
	const CollBaseObjView *BaseObject (int planet, int base, int obj) const override;
	bool BuildingRest (int planet, int base, int obj, std::vector<CollGroupData> &grp, double &rb); // coll-bounded 5.2: an object's or streamed building's undamaged collider (base or tile frame)
	bool BuildingSet (int planet, int base, int obj, const std::vector<CollGroupData> &grp); // coll-bounded 5.4: its damaged collider
	void Bases (std::vector<const CollBaseObjView *> &all) const override;
	// state
	CollSdk &sdk;
	CollCfgValues cfg;
	CollDirs dirs;
	CollOrbCfg orbCfg;
	CollMeshCache cache;
	CollTemplateCache tpl;
	std::unique_ptr<CollBaseA> bases;
	std::unique_ptr<CollBldgA> bldg;               // bldg: streamed 3D buildings near the vessels
	bool started = false, absAnim = false;
	uint32_t frame = 0;
	CollVesselSrc *Rec (uint32_t id) const;
	static void Rewind (CollAnim &ca, const ANIMATION *a, uint32_t na, const uint8_t *present, uint32_t nmesh);
private:
	void Merge ();
	void FirstSight (CollVesselSrc &r);
	void PollSlots (CollVesselSrc &r);
	void Replace (CollVesselSrc &r, uint32_t i);
	void Kill (CollVesselSrc &r, uint32_t i);
	void PollAnims (CollVesselSrc &r, bool check);
	void StepVessel (CollVesselSrc &r, const CollAnim *&src, CollAnim &tmp);
	void Predict (CollVesselSrc &r, const CollAnim &src);
	void UpdateInfo (CollVesselSrc &r, uint32_t i);
	void ClientCheck (CollVesselSrc &r);
	void PollHidden (CollVesselSrc &r);             // the client's hidden groups (user flag 0x2) leave the collider (design fixed-structures rev 2)
	void PollAssemblies ();
	void ReadKeys (CollVesselSrc &r);
	bool Active (const CollVesselSrc &r) const { return cfg.model != 0 && r.keys.enableCollider; }
	std::vector<std::unique_ptr<CollVesselSrc>> vessel, pending; // by id (sparse), pending: created inside callbacks
	std::unordered_map<CollH, uint32_t> byHandle;
	std::vector<CollAssembly> asm_; std::vector<CollPortRec> port; std::vector<CollAttRec> att;
	std::unordered_map<std::string, CollClassKeys> classKeys;
	std::unordered_map<uint64_t, uint64_t> lastHash; // assembly root id -> memberHash
	uint32_t ownId = 0, rr = 0;
	bool inPreStep = false;
	bool noGroupFlags = false;                      // the core or client gives no device mesh group flags
	int renderMode = -1;
	CollSlotView noView; std::vector<CollSlotView> noSlots; CollClassKeys noKeys;
};
#endif
