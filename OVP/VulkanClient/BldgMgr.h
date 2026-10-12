
// not upstream: streamed 3D buildings on a planet's surface (TileStreamMvt.cpp in the core makes the tiles)

#ifndef __BLDGMGR_H
#define __BLDGMGR_H

#include "Mesh.h"
#include <map>
#include <vector>

class vPlanet;
class Scene;
class D3D9Mesh;

class BldgMgr {
public:
	BldgMgr (vPlanet *vp, OBJHANDLE hObj, const Scene *scn);
	~BldgMgr ();
	void Update ();             // camera below 15 km: want, load, place, unload
	void Render (VkDev *dev);   // after the base structures, depth tested

private:
	struct Tile {
		int x = 0, y = 0;
		TSBLDGMESH *src = nullptr;
		D3D9Mesh *mesh = nullptr;
		MESHHANDLE hMesh = nullptr;
		VECTOR3 p0, e0, u0, n0;     // tile frame at the tile centre, planet-local
		std::vector<double> ground; // per building, metres
	};
	void Make (Tile &t, bool reground);
	void Drop (Tile &t);
	double Ground (double lat, double lng) const;

	vPlanet *vp;
	OBJHANDLE hObj;
	const Scene *scn;
	double R;
	std::map<long long, Tile> tiles;
	double tWant = 0.0, tGround = 0.0;
	double camLat = 0.0, camLng = 0.0, range = 0.0;
};

#endif // !__BLDGMGR_H
