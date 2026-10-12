
// not upstream: streamed 3D buildings on a planet's surface (TileStreamMvt.cpp in the core makes the tiles)

#ifndef __BLDGMGR_H
#define __BLDGMGR_H

#include "Mesh.h"
#include "VObject.h"
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
	};
	struct Tile {
		int x = 0, y = 0;
		VECTOR3 p0, e0, u0, n0;           // tile frame at the tile centre, planet-local
		double rad = 0.0;                 // bounding radius around p0, m
		std::vector<double> lat, lng, ground, placed; // per building: anchor (rad), its ground, the ground its mesh has (m)
		std::vector<std::vector<Run>> runs;   // per building
		Cell cell[NC * NC];
	};
	void Make (Tile &t, const TSBLDGMESH *m);
	bool Reground (Tile &t);
	void Edit (Tile &t, Cell &c, double now);
	void Drop (Tile &t);
	bool Near (int x, int y) const;
	double Ground (double lat, double lng) const;
	void Work ();

	vPlanet *vp;
	OBJHANDLE hObj;
	const Scene *scn;
	double R;
	std::map<long long, Tile> tiles;
	std::set<long long> bad;     // tiles whose file did not load
	std::set<long long> busy;    // render thread: given to the worker, result not taken yet
	long long lastGround = -1;   // the last tile re-grounded
	long long lastEdit = -1;     // the tile the next frame's edits start at
	int settle = 0;              // tiles left to re-ground every frame while the terrain under them refines
	int sunLeft = 0;             // fresh terrain sun marches left this frame, all passes; < 0 some were put off
	long long sunFrom = -1, sunOff = -1; // the tile the marches start at; the first tile put off this frame
	double tWant = 0.0, tGround = 0.0;
	double camLat = 0.0, camLng = 0.0, range = 0.0;

	std::thread worker;          // reads and parses the .bld files, nothing else
	std::mutex mx;
	std::condition_variable cv;
	std::deque<long long> todo;  // under mx
	std::deque<std::pair<long long, TSBLDGMESH*>> ready; // under mx
	bool quit = false;           // under mx
};

#endif // !__BLDGMGR_H
