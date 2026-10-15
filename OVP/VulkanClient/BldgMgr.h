

// not upstream: streamed 3D buildings on a planet's surface (TileStreamMvt.cpp in the core makes the tiles)

#ifndef __BLDGMGR_H
#define __BLDGMGR_H

#include "Mesh.h"
#include "VObject.h"
#include "BldgGeom.h"
#include "FixedVes.h"
#include "ClearZones.h"
#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

class vPlanet;
class Scene;
class D3D9Mesh;

class BldgMgr {
public:
	BldgMgr (vPlanet *vp, OBJHANDLE hObj, const Scene *scn);
	~BldgMgr ();
	void Update ();             // main scene, camera below 15 km: want, load, place, unload
	void Render (VkDev *dev);   // after the base structures, depth tested: main scene, custom cameras, env maps near by
	void RenderDepth (VkDev *dev, const D3DXMATRIX &mVP); // SceneFx distance pass: the cells in view
	void RenderCasters (VkDev *dev, const class SceneShadows &s); // scene shadow casters: the cells in the cascade's light box

private:
	static const int NC = 4;              // a class-0 tile's buildings in NC x NC cells, a mesh each, lit by its own terrain sun; a far tile has one
	static const int NClass = BldgGeom::Lod::NClass;
	struct Run { DWORD grp, v0, nv; };    // a building's vertices in one group of its cell's mesh
	struct Cell {
		D3D9Mesh *mesh = nullptr;
		VECTOR3 up;                       // centre, planet-local unit vector
		std::vector<DWORD> bldg;          // its buildings
		float vis = 1.0f;                 // its terrain sun
		TerrainSunCache ts;
		bool dirty = false;               // ground moved under some of its buildings, the mesh not edited yet
		double tEdit = 0.0;               // the mesh is edited at most every 0.25 s
		float night = 0.0f;               // the window light its mesh has
		float nightAt = 0.0f;             // where on the dusk ramp its windows start to light
	};
	struct Tile {
		int x = 0, y = 0;
		int cls = 0;                      // its distance class (BldgGeom::Lod): 0 every building in full, c >= 1 those of H[c] m and up, simpler
		int nc = NC;                      // cells a side
		bool empty = false;               // no buildings in the file: right at any class
		unsigned gen = 0;                 // new at each upload: ground results for an older build are dropped
		size_t nv = 0;                    // vertices in its meshes
		VECTOR3 p0, e0, u0, n0;           // tile frame at the tile centre, planet-local
		double rad = 0.0;                 // bounding radius around p0, m
		double half = 0.0;                // the tile's half diagonal on the ground, m
		std::vector<double> lat, lng, ground, placed; // per building: anchor (rad), its ground, the ground its mesh has (m)
		std::vector<std::vector<Run>> runs;   // per building
		std::vector<uint16_t> cellOf;     // per building: its cell
		std::vector<Cell> cell;           // nc x nc
	};
	struct BaseRec {                      // a base's structures; planet-local to its frame through rp, then rb
		MATRIX3 rp, rb;
		VECTOR3 p;                        // its origin, planet-local
		BldgGeom::BaseIndex idx;
	};
	struct Grp { std::vector<NTVERTEX> v; std::vector<WORD> i; int kind; };
	struct Job {                          // a worker's: build a tile at a class, or read a far tile's grounds again
		long long key = -1;
		int cls = 0;
		bool ground = false;
		unsigned gen = 0;
		std::vector<const BaseRec *> bases;   // build: the bases within 25 km, made on the render thread
		std::vector<std::shared_ptr<const FixedVes::Rec>> fixed; // build: the fixed structures within 25 km (design fixed-structures)
		std::vector<ClearZones::Zone> zones;  // build: the base files' clear zones within 25 km (design base-clear)
		std::vector<double> lat, lng, g;      // ground: the anchors and their grounds now
	};
	struct Built {                        // a worker's tile: frame, buildings and cell groups, no meshes yet
		long long key = -1;
		int cls = 0;
		bool ok = false;                  // the file loaded
		Tile t;
		std::vector<std::vector<Grp>> grps; // per cell
		size_t nv = 0;                    // vertices in grps
		std::array<float, NClass> hist {}; // .bld vertices of the buildings of H[c] m and up
		DWORD hidden = 0;                 // left out inside base or fixed structures, or clear zones
	};
	struct Moved { long long key = -1; unsigned gen = 0; std::vector<std::pair<DWORD, double>> g; }; // a ground job's grounds that moved

	void Prepare (const Job &j, Built &o) const;
	void Fill (const Job &j, const TSBLDGMESH &m, Built &o) const;
	void Regrounds (const Job &j, Moved &o) const;
	void Upload (Built &b);
	void Want (double t);
	bool Reground (Tile &t);
	void Edit (Tile &t, Cell &c, double now);
	void Drop (Tile &t);
	double Dist (int x, int y) const;
	void GroundsLoaded (const double *lng, const double *lat, size_t n, double *g, int *rc) const;
	void Grounds (const double *lng, const double *lat, size_t n, double *g) const;
	const BaseRec &BaseOf (OBJHANDLE hBase);
	std::vector<const BaseRec *> Bases (int x, int y);
	void Stale (const std::vector<VECTOR3> &moved); // the built and busy tiles within 25 km of these planet-local places: built again
	int NearTiles () const;
	void Night ();
	void Work ();

	vPlanet *vp;
	OBJHANDLE hObj;
	const Scene *scn;
	double R;
	SURFHANDLE texFacade = nullptr, texRoof = nullptr; // Textures/Bldg, made by Utils/bldgtex
	bool nite = false;           // NightLook: texFacade's emission map is facade_nite.dds, windows on by Night()'s dusk progress
	std::map<long long, Tile> tiles;
	std::map<OBJHANDLE, BaseRec> bases;
	std::map<long long, std::array<float, NClass>> hist; // per tile ever built this session
	std::map<long long, double> later; // tiles the stream refused: not asked again before this time
	std::set<long long> bad;     // tiles whose file did not load
	std::set<long long> busy;    // render thread: a build job out, its result not uploaded yet
	FixedVes::Set fixed{false};  // the fixed structures on this planet: steep faces, as a base's
	std::vector<ClearZones::Zone> zones; // the base files' clear zones on this planet, read at the first build (design base-clear)
	bool zonesRead = false;
	std::set<long long> stale;   // tiles built before a fixed structure near them changed: built again at their class
	std::vector<std::pair<long long, int>> dispatch; // the want pass's tiles on disk to build, nearest first, at their class
	size_t di = 0;               // the next in dispatch
	std::deque<Built> got;       // results waiting for their upload
	bool groundOut = false;      // a ground job is out
	long long lastGround = -1;   // the last class-0 tile re-grounded
	long long lastFar = -1;      // the last far tile given a ground job
	long long lastEdit = -1;     // the tile the next frame's edits start at
	int settle = 0;              // class-0 tiles left to re-ground every frame while the terrain under them refines
	int sunLeft[2] = {0, 0};     // marches left this frame, [0] main [1] custom/env passes; < 0 some put off
	long long sunFrom[2] = {-1, -1}, sunOff[2] = {-1, -1}; // per kind: the tile the marches start at, the first put off
	double tWant = 0.0, tGround = 0.0, tFar = 0.0, tLog = 0.0;
	std::vector<double> scratch; // Reground's grounds, kept between frames
	double camLat = 0.0, camLng = 0.0;
	VECTOR3 camL {0, 0, 0};      // the camera, planet-local
	double px = 0.0;             // rad a pixel on screen, at a 40 deg field
	double rangeM = 20e3;        // the building range, m
	int scale = 0;               // the budget scale step (BldgGeom::Lod)
	unsigned gens = 0;
	size_t farVtx = 0;           // vertices of the class 1 and up tiles
	double estNow = 0.0;         // the want pass's estimate of them
	int logScale = -1;

	std::vector<std::thread> workers; // read, ground and build tiles
	std::mutex mx;
	std::condition_variable cv;
	std::deque<Job> todo;        // under mx
	std::deque<Built> ready;     // under mx
	std::deque<Moved> moved;     // under mx
	bool quit = false;           // under mx
};

#endif // !__BLDGMGR_H
