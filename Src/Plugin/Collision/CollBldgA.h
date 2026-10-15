// not upstream: collision addon, the streamed 3D buildings as colliders: z14 tiles near vessels, the buildings near them built in the tile frame (design bldg-collision 3)
#ifndef __COLLBLDGA_H
#define __COLLBLDGA_H
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "CollCfg.h"
#include "CollGeom.h"
#include "CollSdk.h"

class CollBaseA;
struct CollBaseRec;
struct CollABody;
namespace BldgGeom { struct Out; }

constexpr double   COLL_BLDG_ALT     = 2000;   // vessels higher above the ground want no tiles [m]
constexpr double   COLL_BLDG_TILES   = 800;    // tiles within max(this, COLL_BLDG_TILES_S x ground speed) of a vessel are wanted [m]
constexpr double   COLL_BLDG_TILES_S = 4;      // [s]
constexpr double   COLL_BLDG_WANT    = 0.5;    // system time between two want rounds [s]
constexpr double   COLL_BLDG_KEEP    = 5000;   // a tile no vessel is this near is dropped [m]
constexpr double   COLL_BLDG_REACH   = 300;    // a building is built when a vessel bound comes within this + COLL_BLDG_REACH_S x speed of its footprint [m]
constexpr double   COLL_BLDG_REACH_S = 2;      // [s]
constexpr double   COLL_BLDG_VMAX    = 2e4;    // ground speed cap for reach and tiles: faster than any entry, so only a runaway state [m/s]
constexpr double   COLL_BLDG_NEAR    = 100;    // buildings this near are built at once, past the budget [m]
constexpr uint32_t COLL_BLDG_BUDGET  = 20000;  // building vertices built a frame
constexpr double   COLL_BLDG_IDLE    = 10;     // a building beyond twice its reach this long is freed [s]
constexpr double   COLL_BLDG_GROUND  = 5;      // sim time between two ground checks of a building near a vessel [s]
constexpr uint32_t COLL_BLDG_ELEVS   = 32;     // core elevation calls a frame (about 0.1 ms each) for buildings past COLL_BLDG_NEAR and ground checks
constexpr double   COLL_BLDG_VMOVE   = 25;     // a vessel's ground is asked again after it moved this far [m]
constexpr double   COLL_BLDG_VAGE    = 1;      // or after this sim time [s]
constexpr double   COLL_BLDG_MOVE    = 0.25;   // a ground move that rebuilds a building [m] (BldgMgr)
constexpr double   COLL_BLDG_MARGIN  = 6;      // base exclusion: footprint points this near a base structure [m] (BldgMgr)
constexpr double   COLL_BLDG_BASES   = 25e3;   // bases this near a tile centre keep its buildings out [m] (BldgMgr)
constexpr uint32_t COLL_BLDG_SLOTS   = 0x7FFF; // tile slots of a session, never reused: body id 0x8000 + slot, owner base -2 - slot
constexpr double   COLL_BLDG_CELL    = 128;    // activation grid cell [m]
constexpr double   COLL_BLDG_ROOF    = 4.5;    // highest pitched roof above hmax [m] (BldgGeom)
constexpr double   COLL_FIXED_MOVE   = 0.5;    // a fixed structure moved this far makes its keep-out again [m] (FixedVes)
constexpr double   COLL_FIXED_TURN   = 1e-3;   // or turned this far [rad] (FixedVes)

class CollBldgA {
public:
	struct Live { CollH h; double r; };                              // a vessel with a collider and its bound radius
	struct FixedIn { CollH h; std::string name; Vector x; Matrix R; uint64_t sig; std::vector<std::pair<std::shared_ptr<const CollRestMesh>, Vector>> mesh; double clear = 0; }; // a landed FixedStructure vessel: global pose, its external meshes at rest with their offsets, their signature, its clear zone radius [m]
	struct Part { const CollGeom *geom; uint32_t version, index; };  // a built building near a vessel this frame
	struct Body { int planet; CollH hPlanet; uint32_t slot; Vector rposP; Matrix rrotP; double rmax; bool entry; std::vector<Part> part; }; // a tile with parts
	struct Stats {
		uint64_t loads = 0, drops = 0, built = 0, freed = 0, rebuilt = 0, excluded = 0, skipped = 0;
		uint64_t deferred = 0;                                          // ground moves not rebuilt while a vessel bound touched the building
		double loadMs = 0, loadMsMax = 0, buildMsMax = 0;               // tile loads; the building of one frame
		uint32_t buildVtxMax = 0;                                       // .bld vertices built in one frame
		uint64_t frames = 0, elevCalls = 0;                             // frames while on; the core's surface elevation calls
		double elevMs = 0, elevMsMax = 0;                               // their time: all frames, the worst frame
		uint32_t elevCallsMax = 0;                                      // calls in one frame
	};
	CollBldgA (CollSdk &sdk, const CollCfgValues &cfg);
	~CollBldgA ();
	static bool Built ();                                           // the build has BldgGeom (COLL_HAVE_BLDGGEOM)
	void Start (const CollBaseA *bases);                            // clbkSimulationStart, after the bases: on or idle
	void Fixed (std::vector<FixedIn> f) { fixedIn = std::move (f); } // PS1, before Frame: the fixed structures now; their keep-out is checked in the want rounds
	void Frame (double simt, const std::vector<Live> &v);           // PS1: tiles, building, freeing; the bodies of this frame
	void Clear ();                                                  // session end: every tile freed (BldgFree only frees memory), no log
	void TimeJump ();                                               // sim time jumped: idle and ground timers restart
	void Summary ();                                                // the session's counters, one log line
	static void Emit (const Body &t, CollABody &B);                 // a tile body's kind, id, rmax, entry bit and parts as PhysGeomA::Bases hands them on
	static void Split (const std::vector<CollVtx> &v, const std::vector<std::array<uint32_t, 3>> &tri, std::vector<CollGroupData> &grp); // groups of at most 65535 vertices (uint16 indices)
	const std::vector<Body> &Bodies () const { return bodies; }
	bool Name (int planet, int base, int obj, std::string &out) const; // "building <planet> z14 x/y #n" of a streamed building owner
	static uint32_t BodyId (int planet, uint32_t slot) { return ((uint32_t)(planet & 0xFFFF) << 16) | (0x8000u + slot); }
	static int32_t BaseOf (uint32_t slot) { return -2 - (int32_t)slot; }
	bool On () const { return on; }
	size_t Tiles () const { return tiles.size (); }
	size_t BuiltCount () const;                                     // buildings with a collider now
	Stats st;
private:
	struct Bldg;
	struct Tile;
	struct FixedRec;
	struct Planet { CollH h; int idx; double R, T; std::string name; Vector pp, pv; Matrix pR; };
	struct ZoneRec { const Planet *p; Vector u, rposP; Matrix rrotP; double r; }; // a base file's clear zone: centre unit vector, frame as a base record, radius [m] (design base-clear)
	struct Ves { const Planet *p; Vector loc; double alt, r, speed, reach; }; // planet frame position, height over its ground
	struct Cand { Tile *t; uint32_t i; };
	struct VGround { CollH h; const Planet *p; Vector loc; double g, t; }; // a vessel's last ground: planet frame position, height, sim time
	void Want (const Ves &v);
	void Load ();
	void Setup (Tile &t);
	double Gap (const Tile &t, uint32_t i, const Vector &q, double alt, double r) const; // q: tile frame
	uint32_t Activate (Tile &t, uint32_t i, double simt, double g); // .bld vertices built, 0 if none
	bool Excluded (const Tile &t, uint32_t i, double g, const std::vector<std::array<double, 2>> &ring) const;
	void Shape (const Tile &t, uint32_t i, BldgGeom::Out &o) const; // the drawn shape (BldgGeom::Build with the client's hash)
	void FixedPass ();                                              // the fixed structures' records: made, kept or dropped; the tiles near a change checked again
	void NearFixed (Tile &t) const;                                 // the fixed records within COLL_BLDG_BASES of the tile centre
	void Recheck (Tile &t);                                         // every building's keep-out again: a built one now excluded is freed, an excluded one is idle
	void Free (Tile &t, uint32_t i);
	void Drop (Tile &t);
	void Release ();
	double Elev (CollH planet, double lng, double lat);             // sdk.Elevation, timed into this frame's counters
	void Log (const char *fmt, ...);
	CollSdk &sdk;
	const CollCfgValues &cfg;
	const CollBaseA *bases = nullptr;
	bool on = false, full = false;
	std::vector<Planet> planets;
	std::map<uint32_t, std::unique_ptr<Tile>> tiles;                // by slot
	std::map<uint64_t, uint32_t> slotOf;                            // tile key -> slot of a kept tile
	std::set<uint64_t> bad;                                         // tiles that did not load: skipped this session
	std::vector<uint64_t> pending;                                  // tiles on disk to load, oldest first
	std::vector<Body> bodies;
	std::vector<Ves> ves;
	std::vector<VGround> vground;                                   // the vessels' ground cache
	std::vector<FixedIn> fixedIn;                                   // this frame's fixed structures
	std::vector<std::shared_ptr<const FixedRec>> fixedRec;          // their keep-out records
	std::vector<ZoneRec> zones;                                     // the base files' clear zones on the planets, made by Start
	uint32_t nextSlot = 0, verSerial = 0, frameNo = 0;
	double tWant = -1e100;
	double fElevMs = 0;                                             // this frame's elevation time and calls
	uint32_t fElevN = 0;
};
#endif
