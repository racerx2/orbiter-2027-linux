// not upstream: collision addon, dmg3 area P: parts tear off, glass breaks, interiors hidden, debris vessels (design-CA-dmg3-P)
#ifndef COLLBREAKA_H
#define COLLBREAKA_H
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "CollCfg.h"
#include "CollDmgTypes.h"
#include "CollGeom.h"

constexpr double BRK_VN_PART      = 20.0;   // parts need approach speed >= this [m/s]: 200 J/kg closing, above the 113 J/kg of a DG-DG 15 m/s hit
constexpr double BRK_RATIO_PART   = 0.6;    // crush / own depth: thin-walled crush zones bottom out at 0.6-0.75 stroke
constexpr double BRK_VN_INTERIOR  = 25.0;   // interior hidden only on destructive hits [m/s]
constexpr double BRK_RATIO_INT    = 0.5;
constexpr double BRK_GLASS_MIN    = 0.01;   // glass fails at max(this, 0.02 r) of one event's deflection [m]
constexpr double BRK_GLASS_REL    = 0.02;
constexpr double BRK_FIXED_R      = 0.35;   // pieces wider than this * mesh radius stay
constexpr double BRK_FIXED_A      = 0.2;    // or with more than this share of the slot area
constexpr double BRK_MASS_MAX     = 0.05;   // piece mass <= this * parent empty mass
constexpr double BRK_MIN_MASS     = 2.0;    // lighter or smaller pieces are hidden without debris [kg]
constexpr double BRK_MIN_R        = 0.25;   // [m]
constexpr double BRK_DEBRIS_R     = 0.5;    // dmg3 tear: debris only for pieces this wide or more; smaller ones are hidden [m]
constexpr double BRK_TEAR_VN      = 40.0;   // dmg3 tear: section tear needs vn >= this [m/s]
constexpr double BRK_TEAR_E       = 300.0;  // and eSpec >= this [J/kg]; the dock pin is released from here
constexpr double BRK_TEAR_DMAX    = 0.75;   // cut depth <= this * L
constexpr double BRK_TEAR_FMIN    = 0.01;   // section area share >= this
constexpr double BRK_TEAR_FMAX    = 0.3;    // and <= this
constexpr double BRK_TEAR_RX      = 0.4;    // cut radius <= this * L
constexpr double BRK_TEAR_MMAX    = 0.3;    // section mass <= this * parent empty mass
constexpr double BRK_TEAR_KICK    = 0.1;    // section kick = this * vn along +-t or +-e
constexpr double BRK_KICK         = 0.15;   // debris speed = this * vn
constexpr double BRK_SEP          = 0.05;   // pair filter released at this bound gap [m]
constexpr double BRK_STUCK        = 10.0;   // debris still overlapping after this is deleted [s]
constexpr double BRK_FUNC_DIST    = 0.15;   // a touchdown, thruster or dock point this near a vertex holds the piece [m]
constexpr int    BRK_PER_EVENT    = 4;      // debris per event
constexpr const char *BRK_CLASS   = "CollDebris";

struct CollPieceA { std::vector<uint16_t> grp; int tier = CBRK_PART; Vector c; double r = 0, area = 0; uint32_t comps = 1; bool fixed = false; uint32_t functional = 0; }; // functional: CBRK_FN_* bits
struct CollSlotA {                                        // rest geometry of one slot and its pieces
	bool ok = false; std::string name; uint32_t key = 0, nvtx = 0; uint16_t ngrp = 0; CollH tpl = nullptr;
	std::vector<std::vector<DentVtx>> v; std::vector<std::vector<uint16_t>> idx; std::vector<uint32_t> usr;
	std::vector<int> pieceOf; std::vector<CollPieceA> piece; double area = 0, rad = 0;
	std::set<uint16_t> keep;                              // sidecar ";@KEEPFLAGS": module-owned flag groups P never touches
	std::vector<int> tier; std::vector<uint32_t> cls;     // dmg3 tear: per group tier and pose class
	uint32_t staticCls = 0;                               // dmg3 tear: pose class of the hull (largest piece)
};
struct CollDebrisA { uint32_t id = ~0u, parent = 0, other = 0, event = 0; CollH h = nullptr, mesh = nullptr; double birth = 0; DentDebris row; uint32_t fnv = 0; };
struct CollSpawnA { uint32_t parent = 0, other = 0, event = 0; std::string mesh; DentDebris row; Vector cv, dv, dw; double mass = 0; CollSdk::DebrisCaps caps; };
struct CollCutPlan { bool ok = false; DentRecord rec; std::vector<uint16_t> front, straddle; double d = 0, f = 0, area = 0; const char *why = ""; }; // dmg3 tear: planned cut
struct CollFreeA { CollH mesh = nullptr, h = nullptr; bool dropped = false; };
struct CollPairA { uint32_t a = 0, b = 0; double t = 0; uint32_t debris = 0; };

class CollBreakA : public CollDmgSink {
public:
	CollBreakA (CollSdk &sdk, CollDmgSession &s, const CollCfgValues &cfg) : sdk (sdk), s (s), cfg (cfg) {}
	~CollBreakA () override;
	void Hit (const CollDamageHit &h) override;
	void Shapes (uint32_t id, CollShape *sh) override;
	void Post (double simt, double simdt) override;
	void Pass () override;
	void Torn (uint32_t id, const DentTorn &t) override;
	void Repair (uint32_t id) override;
	void DropVessel (uint32_t id, CollH h) override;
	void End () override;
	void Quiet () override { quiet = true; }
	bool Hidden (uint32_t id, uint32_t mesh, uint32_t g) const override;
	const CollSlotA *Slot (uint32_t id, uint32_t mesh);   // pieces of a slot, cached
	const std::vector<CollDebrisA> &Debris () const { return live; }
	const std::vector<CollPairA> &Pairs () const { return pairs; }
	size_t Pending () const { return spawn.size (); }
	uint64_t reasserts = 0, breaks = 0;
	static bool TearGate (const CollDamageHit &h, const DentParams &crush, double L); // dmg3 tear: section gate
	static bool TipGate (const CollDamageHit &h, const DentParams &hinge);         // dmg3 tear: wing/fin tip gate
	CollCutPlan PlanCut (uint32_t id, const CollSlotA &sl, const CollDamageHit &h, const DentParams &src, double L, bool tip, uint32_t event); // dmg3 tear: cut plane, groups, record
	uint64_t tears = 0;
	static std::vector<DentVtx> PieceVertices (const std::vector<DentVtx> &rest, uint16_t g, const DentDebrisPose &p, const std::vector<DentRecord> &rec); // A(q) (rest + records) + p
private:
	struct VesB { std::vector<DentTorn> rows; size_t adopted = 0; CollShape *sh = nullptr; bool seen = false; CollDamageHit last; bool haveLast = false; };
	void Assert (uint32_t id, VesB &b);                   // visual flags of hidden groups
	void Collider (uint32_t id, VesB &b, bool force);     // collider hides
	void Adopt ();                                        // rows loaded by the session
	void Rebuild (double simt);                           // first Post: debris from saved rows
	void Spawn (CollSpawnA &sp, double simt);
	void Kill (size_t i, const char *why);
	void SyncRows (uint32_t parent);
	void PairCheck (double simt);
	void Tear (uint32_t id, CollH h, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<int> &pk, uint32_t event, bool playback);
	bool BuildMesh (CollH mesh, const DentDebris &d, std::vector<std::pair<std::vector<DentVtx>, std::vector<uint16_t>>> &geo); // flags and piece vertices of a private copy
	bool Section (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const CollCutPlan &pl, uint32_t event); // dmg3 tear: apply a planned tear
	bool MakeTearSpawn (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<uint16_t> &front, const std::vector<uint16_t> &straddle, const DentRecord &cut, uint32_t event, CollSpawnA &sp);
	std::vector<CollAffine> Poses (uint32_t id, uint32_t mesh, size_t ng);
	static CollAffine StaticPose (const CollSlotA &sl, const std::vector<CollAffine> &F); // dmg3 tear: pose of the static class
	bool MakeSpawn (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<int> &pk, uint32_t event, CollSpawnA &sp);
	static CollSdk::DebrisCaps Caps (const std::vector<std::pair<std::vector<DentVtx>, std::vector<uint16_t>>> &geo, double mass, uint32_t *fnv);
	double Mass (uint32_t parent, const CollSlotA &sl, const std::vector<int> &pk);
	std::string NewName (const std::string &parent);
	uint32_t FindId (CollH h);
	void Log (const char *fmt, ...);
	CollSdk &sdk; CollDmgSession &s; const CollCfgValues &cfg;
	std::map<uint32_t, VesB> ves;
	std::map<std::pair<uint32_t, uint32_t>, CollSlotA> slots;
	std::vector<CollDebrisA> live;
	std::vector<CollSpawnA> spawn;
	std::vector<CollPairA> pairs;
	std::vector<CollFreeA> freeMesh;                      // meshes of deleted debris, freed at the Post after OnDeleteVessel, or at End
	uint32_t maxId = 0, events = 0, debrisSeq = 0;
	int cfgOk = -1;                                       // CollDebris.cfg probe: -1 not yet
	bool quiet = false, rebuilt = false, loggedNoCfg = false;
};
#endif
