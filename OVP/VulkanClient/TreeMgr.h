// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: 3D trees where the streamed land class says forest or shrub (design-ground-graphics 5)

#ifndef __TREEMGR_H
#define __TREEMGR_H

#include "D3D9Util.h"
#include "VObject.h"
#include "TreeGeom.h"
#include "BldgGeom.h"
#include "FixedVes.h"
#include "ClearZones.h"
#include "BaseShapes.h"
#include "WayGeom.h"
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class vPlanet;
class Scene;
namespace Horizon { struct Map; }

class TreeMgr {
public:
	TreeMgr (vPlanet *vp, OBJHANDLE hObj, const Scene *scn);
	~TreeMgr ();
	void Update ();             // main scene: want, results, uploads
	void Render (VkDev *dev);   // after BldgMgr: main scene and custom cameras
	void RenderDepth (VkDev *dev, const D3DXMATRIX &mVP); // SceneFx distance pass
	void RenderCasters (VkDev *dev, const class SceneShadows &s); // scene shadow casters in the cascade being drawn (design-scene-shadows)

	struct Inst { float x, y, z, s; uint8_t yaw, kind, t0, t1; uint8_t h[8]; }; // 28 bytes, stream 1; kind: the level in the high 4 bits (design-far-forest)
	struct Job {                // render thread to worker
		long long key = 0;
		double dist = 0.0;      // not upstream (design-far-forest): the nearest job first
		double lat0 = 0, lat1 = 0, lng0 = 0, lng1 = 0;
		BaseShapes::Keep keep;  // bases, fixed structures (design fixed-structures) and clear zones (design base-clear) within 25 km
		std::shared_ptr<const Horizon::Map> hor;
		double hb[4] = {0, 0, 0, 0}; // its owner's bounds
		std::vector<std::pair<int, int>> bldg; // z14 tiles to read
		std::vector<std::pair<int, int>> way;  // z14 way tiles to read: no tree on a road, rail, runway or river (design-ways)
	};
	struct Result {
		long long key = 0;
		std::vector<Inst> inst;
		UINT first[TreeGeom::NKIND + 1] = {}; // instances sorted by kind
		VECTOR3 p0, e0, u0, n0;
		double gc[5] = {};     // ground at the centre and 4 points, for the re-ground check
		bool complete = true;  // all ground loaded
		bool hasHor = false;
		bool classMiss = false; // no L16 Class tile yet
		double rad = 0.0;      // not upstream (design-far-forest): the instances' sphere about p0
	};

private:
	struct Cell {
		VkBuf *buf = nullptr;
		UINT first[TreeGeom::NKIND + 1] = {};
		VECTOR3 p0, e0, u0, n0;
		double gc[5] = {};
		double tCheck = 0.0;
		bool hasHor = false, complete = true;
		bool pending = true;   // given to the worker, result not taken yet
		bool stale = false;    // a fixed structure near it changed after it was asked
		bool classWait = false; // not upstream (design-preload): placed without its L16 Class tile, the file looked for every 2 s
		double tClass0 = 0.0;   // since when
		std::vector<std::pair<int, int>> vecWait; // z14 building and road tiles it was placed without, still coming
		bool placed = false;    // not upstream (design-far-forest): a result taken once
		double rad = 0.0, tSeen = 0.0; // the instances' sphere; last wanted or drawn
		D3D9Sun sun;            // the atmosphere at p0, kept (design-far-forest)
		VECTOR3 sunAt = {0, 0, 0};
		double sunD = 0.0, tSun = -1e9;
	};
	void Work ();
	void MakeMeshes ();
	bool Ask (long long key, double dist);
	bool ClassThere (long long key) const; // design-preload: its Class file is on disk (design-far-forest: at its level's)
	void WantClass (long long key);        // design-far-forest: the stream makes it
	void MakeDrawList (double t);
	void DrawCells (VkDev *dev, int mode, const D3DXMATRIX *mVPdepth);

	vPlanet *vp;
	OBJHANDLE hObj;
	const Scene *scn;
	double R;
	std::string root;           // the body's texture folder, as PlanetTexturePath (backslashes, oapiResolvePath)
	std::unordered_map<long long, Cell> cells;
	std::vector<long long> want;                     // not upstream (design-far-forest): the leaves, nearest first
	std::vector<std::pair<long long, Cell *>> draw;  // and what is drawn for them
	bool drawDirty = false;
	size_t chk = 0;                                  // the re-ground check's place in want
	double farLim = 0.0, aglKeep = 1.0;
	std::unordered_map<long long, double> classAsked; // Class tiles wanted from the stream, when
	BaseShapes::Set bases;      // the planet's bases, every triangle (BaseShapes.h)
	FixedVes::Set fixed{true};  // the fixed structures on this planet, every triangle
	std::vector<ClearZones::Zone> zones; // the base files' clear zones on this planet, read at the first job (design base-clear)
	bool zonesRead = false;
	double tWant = 0.0, camLat = 0.0, camLng = 0.0;
	bool active = false;        // camera low enough: cells kept and drawn

	VkBuf *vb = nullptr, *ib = nullptr;   // the five meshes and the impostor quad
	int baseV[TreeGeom::NKIND + 1] = {}, startI[TreeGeom::NKIND + 1] = {}, countI[TreeGeom::NKIND + 1] = {};
	VkVertexDecl *decl = nullptr;
	ShaderClass *sh = nullptr;

	std::vector<std::thread> workers; // not upstream (design-far-forest): 4
	std::mutex mx;
	std::condition_variable cv;
	std::vector<Job> todo;      // under mx
	std::deque<Result> ready;   // under mx
	bool quit = false;          // under mx
};

#endif // !__TREEMGR_H
