
// not upstream: streamed 3D buildings on a planet's surface (TileStreamMvt.cpp in the core makes the tiles)

#ifndef __BLDGMGR_H
#define __BLDGMGR_H

#include "Mesh.h"
#include "VObject.h"
#include "BldgGeom.h"
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
	void RenderDepth (VkDev *dev, const D3DXMATRIX &mVP); // SceneFx distance pass: the cells in range and view
	void RenderCasters (VkDev *dev, const class SceneShadows &s); // scene shadow casters: the cells in the cascade's light box

private:
	static const int NC = 4;              // a tile's buildings in NC x NC cells, a mesh each, lit by its own terrain sun
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
		VECTOR3 p0, e0, u0, n0;           // tile frame at the tile centre, planet-local
		double rad = 0.0;                 // bounding radius around p0, m
		std::vector<double> lat, lng, ground, placed; // per building: anchor (rad), its ground, the ground its mesh has (m)
		std::vector<std::vector<Run>> runs;   // per building
		Cell cell[NC * NC];
	};
	struct BaseRec {                      // a base's structures; planet-local to its frame through rp, then rb
		MATRIX3 rp, rb;
		VECTOR3 p;                        // its origin, planet-local
		BldgGeom::BaseIndex idx;
	};
	void Make (Tile &t, const TSBLDGMESH *m);
	bool Reground (Tile &t);
	void Edit (Tile &t, Cell &c, double now);
	void Drop (Tile &t);
	bool Near (int x, int y) const;
	void Grounds (const double *lng, const double *lat, size_t n, double *g) const;
	const BaseRec &BaseOf (OBJHANDLE hBase);
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
	std::set<long long> bad;     // tiles whose file did not load
	std::set<long long> busy;    // render thread: given to the worker, result not taken yet
	long long lastGround = -1;   // the last tile re-grounded
	long long lastEdit = -1;     // the tile the next frame's edits start at
	int settle = 0;              // tiles left to re-ground every frame while the terrain under them refines
	int sunLeft[2] = {0, 0};     // marches left this frame, [0] main [1] custom/env passes; < 0 some put off
	long long sunFrom[2] = {-1, -1}, sunOff[2] = {-1, -1}; // per kind: the tile the marches start at, the first put off
	double tWant = 0.0, tGround = 0.0;
	std::vector<double> scratch; // Reground's grounds, kept between frames
	double camLat = 0.0, camLng = 0.0, range = 0.0;

	std::thread worker;          // reads and parses the .bld files, nothing else
	std::mutex mx;
	std::condition_variable cv;
	std::deque<long long> todo;  // under mx
	std::deque<std::pair<long long, TSBLDGMESH*>> ready; // under mx
	bool quit = false;           // under mx
};

#endif // !__BLDGMGR_H
