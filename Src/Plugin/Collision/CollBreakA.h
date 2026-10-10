// not upstream: collision addon, dmg3 area P: parts tear off, glass breaks, interiors hidden, debris vessels (design-CA-dmg3-P)
#ifndef COLLBREAKA_H
#define COLLBREAKA_H
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "CollBlastA.h"
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
constexpr double BRK_SEP          = 0.05;   // pair counted apart at this bound gap [m]
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
struct CollDebrisA { uint32_t id = ~0u, parent = 0, other = 0, event = 0; CollH h = nullptr, mesh = nullptr; double birth = 0; DentDebris row; uint32_t fnv = 0; bool restored = false; }; // restored: rebuilt on load, its pairs count as apart (no stuck delete)
struct CollSpawnA { uint32_t parent = 0, other = 0, event = 0; std::string mesh; DentDebris row; Vector cv, dv, dw; double mass = 0; CollSdk::DebrisCaps caps; bool blast = false; }; // blast: the parent takes the opposite impulse
struct CollCutPlan { bool ok = false; DentRecord rec; std::vector<uint16_t> front, straddle; double d = 0, f = 0, area = 0; const char *why = ""; }; // dmg3 tear: planned cut
struct CollFreeA { CollH mesh = nullptr, h = nullptr; bool dropped = false; };
struct CollPairA { uint32_t a = 0, b = 0; double t = 0; uint32_t debris = 0; bool sep = false; }; // sep: apart once; the filter stays (debris never collide with their parents, impactors or other debris)
struct CollBlastSlotA {                                   // blast: one vessel slot (design-CA-blast 2)
	std::unique_ptr<CollBlastA> b; uint32_t key = 0; double lastHit = -1e300; CollDamageHit hit; bool haveHit = false;
	std::vector<uint16_t> groups;                         // static-class groups in the cells
	std::vector<uint32_t> recorded;                       // broken bonds already stored
	double cutMass = 0;                                   // mass of the chunks gone at build (load) [kg]
	std::vector<uint32_t> cut;                            // chunks gone at build (load)
	bool full = false;                                    // record limit reached, logged
	std::vector<uint32_t> weak;                           // weakened bonds already stored (W rows)
	std::set<uint32_t> held;                              // chunk keys of held pieces already logged
};
bool CollPieceHeld (const CollPieceA &p, const CollDamageHit &h, bool hitNow = true); // blast: dmg3 part gates for an actor of animated pieces only (approach speed, dock pin); without a hit this frame only the dock pin
struct CollParentA { bool read = false; CollVesselRead rd {}; Vector rp, rv, J, H; double M = 0; std::vector<std::pair<Vector, Vector>> jf; }; // pre-step: one read and one write per parent; jf: impulse and point of each debris (stacks)
struct CollKickA { uint32_t parent = 0; Vector dv, dw; double M = 0; }; // blast: parent velocity and spin change of one debris kick, parent mass (tests)

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
	void PreStep (double simt, double simdt) override;                  // spawns queued by the last post-step: debris vessels and the parents' kicks
	void Boot (double simt);                              // first pass after load: rebuild debris, adopt rows, queue the load mass cut
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
	void SpawnCells (const CollBlastBreak &b);            // blast: cell cuts, torn rows, one debris with KEEP VCUT copies, parent impulse
	CollBlastA *Blast (uint32_t id, uint32_t mesh) { auto it = blast.find ({ id, mesh }); return it == blast.end () ? nullptr : it->second.b.get (); }
	const CollBlastSlotA *BlastState (uint32_t id, uint32_t mesh) const { auto it = blast.find ({ id, mesh }); return it == blast.end () ? nullptr : &it->second; }
	uint64_t blastBreaks = 0, blastSteps = 0, blastRebuilds = 0; double blastMs = 0; std::vector<CollKickA> kicks;
	struct MassCutA { double m0 = 0, cut = 0; Vector pmi0, icut; }; // blast: empty mass and PMI before cuts, mass and inertia removed
	double MassCut (uint32_t id) const { auto it = massCut.find (id); return it == massCut.end () ? 0 : it->second.cut; } // blast: empty mass removed [kg]
	static std::vector<DentVtx> PieceVertices (const std::vector<DentVtx> &rest, uint16_t g, const DentDebrisPose &p, const std::vector<DentRecord> &rec, const DentSites *sites = nullptr); // A(q) (rest + records with the slot's sites) + p
	static CollSdk::DebrisCaps Caps (const std::vector<std::pair<std::vector<DentVtx>, std::vector<uint16_t>>> &geo, double mass, uint32_t *fnv); // uniform shell: exact triangle second moments
	double CutMass (uint32_t id, CollH vh, double m, const Vector &icut); // blast: lower the parent's empty mass and PMI (total mass M); returns the mass removed
private:
	struct VesB { std::vector<DentTorn> rows; size_t adopted = 0; CollShape *sh = nullptr; bool seen = false; CollDamageHit last; bool haveLast = false; };
	void Assert (uint32_t id, VesB &b);                   // visual flags of hidden groups
	void Collider (uint32_t id, VesB &b, bool force);     // collider hides
	void Adopt ();                                        // rows loaded by the session
	void Rebuild (double simt);                           // first Post: debris from saved rows
	void Spawn (CollSpawnA &sp, double simt, std::map<uint32_t, CollParentA> &pc);
	void Kill (size_t i, const char *why);
	void SyncRows (uint32_t parent);
	void AddPairs (const CollDebrisA &d, double simt);   // pair filters of a debris with an id: parent, impactor, other debris (apart already when restored)
	std::string IdName (uint32_t id);                     // vessel name of a session id, "" none
	uint32_t NameId (const std::string &name);            // session id of a vessel by name or "#<fnv8>", 0 none
	void PairCheck (double simt);
	void Tear (uint32_t id, CollH h, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<int> &pk, uint32_t event, bool playback);
	bool BuildMesh (CollH mesh, const DentDebris &d, std::vector<std::pair<std::vector<DentVtx>, std::vector<uint16_t>>> &geo, uint32_t parent); // flags and piece vertices of a private copy
	bool Section (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const CollCutPlan &pl, uint32_t event); // dmg3 tear: apply a planned tear
	bool MakeTearSpawn (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<uint16_t> &front, const std::vector<uint16_t> &straddle, const DentRecord &cut, uint32_t event, CollSpawnA &sp);
	std::vector<CollAffine> Poses (uint32_t id, uint32_t mesh, size_t ng);
	static CollAffine StaticPose (const CollSlotA &sl, const std::vector<CollAffine> &F); // dmg3 tear: pose of the static class
	bool MakeSpawn (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<int> &pk, uint32_t event, CollSpawnA &sp);
	double Mass (uint32_t parent, const CollSlotA &sl, const std::vector<int> &pk);
	std::string NewName (const std::string &parent);
	uint32_t FindId (CollH h);
	void Log (const char *fmt, ...);
	CollBlastSlotA *BlastSlot (uint32_t id, uint32_t mesh, CollH vh, const CollSlotA &sl); // blast: lazy build, restore from the session
	void BlastHit (const CollDamageHit &h, CollH vh, const CollSlotA &sl);
	void BlastStep (uint32_t id, uint32_t mesh, CollBlastSlotA &bs, CollH vh, bool hit); // hit: a hit this frame, else spin loads only
	static std::vector<uint16_t> StatGroups (const CollSlotA &sl); // blast: groups of the static class that form cells
	bool MakeCellSpawn (const CollBlastBreak &bk, const CollSlotA &sl, CollH vh, const std::vector<Vector> &site, const std::vector<uint16_t> &stat, uint32_t event, CollSpawnA &sp);
	CollSdk &sdk; CollDmgSession &s; const CollCfgValues &cfg;
	std::map<uint32_t, VesB> ves;
	std::map<std::pair<uint32_t, uint32_t>, CollSlotA> slots;
	std::vector<CollDebrisA> live;
	std::vector<CollSpawnA> spawn;
	std::vector<CollPairA> pairs;
	std::map<std::pair<uint32_t, uint32_t>, CollBlastSlotA> blast; // blast: (vessel, slot)
	double postDt = 0, preDt = 0;                                  // blast: last post-step frame [s]
	std::map<uint32_t, MassCutA> massCut;                 // blast: per vessel
	std::set<uint32_t> massPending;                       // blast: loaded vessels whose saved cut is not applied yet
	void LoadMass ();                                     // blast: after load, cut mass from the saved cells
	double BlastMass (uint32_t id, uint32_t mesh, CollH vh); // blast: the slot's share of the empty mass before cuts
	struct PlayedA { size_t idx = SIZE_MAX; std::vector<int> pk; bool cell = false; DentTorn row; }; // row: the merged payloads of a cell debris (kick, cells, pieces)
	std::map<std::pair<uint32_t, uint32_t>, PlayedA> played; // playback: queued spawns by parent and debris name hash (a #fnv8 name is its hash), until the pre-step
	std::vector<CollFreeA> freeMesh;                      // meshes of deleted debris, freed at the Post after OnDeleteVessel, or at End
	uint32_t maxId = 0, events = 0, debrisSeq = 0;
	int cfgOk = -1;                                       // CollDebris.cfg probe: -1 not yet
	bool quiet = false, rebuilt = false, loggedNoCfg = false;
};
#endif
