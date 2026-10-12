
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
	static const int NC = 4;              // a tile's buildings in NC x NC cells, each lit by its own terrain sun
	struct Run { DWORD grp, v0, nv; };    // a building's vertices in one group
	struct Tile {
		int x = 0, y = 0;
		D3D9Mesh *mesh = nullptr;
		VECTOR3 p0, e0, u0, n0;           // tile frame at the tile centre, planet-local
		double rad = 0.0;                 // bounding radius around p0, m
		std::vector<double> lat, lng, ground; // per building: anchor (rad), the ground it stands on (m)
		std::vector<std::vector<Run>> runs;   // per building
		VECTOR3 cellUp[NC * NC];          // cell centres, planet-local unit vectors
		int mtrl[NC * NC][2];             // per cell: wall and roof material, -1 none
		D3D9MatExt base[2];               // wall and roof as lit by the full sun
		float vis[NC * NC];               // the terrain sun the cell's materials carry
		TerrainSunCache ts[NC * NC];
	};
	void Make (Tile &t, const TSBLDGMESH *m);
	void Reground (Tile &t);
	void Sun (Tile &t, const MATRIX3 &rot);
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
