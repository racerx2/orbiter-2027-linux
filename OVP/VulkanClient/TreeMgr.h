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
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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

	struct Inst { float x, y, z, s; uint8_t yaw, kind, t0, t1; uint8_t h[8]; }; // 28 bytes, stream 1
	struct Base {               // a base's structures, every triangle; planet-local to its frame through rp, then rb
		MATRIX3 rp, rb;
		VECTOR3 p;
		BldgGeom::BaseIndex idx{true};
	};
	struct Job {                // render thread to worker
		long long key = 0;
		double lat0 = 0, lat1 = 0, lng0 = 0, lng1 = 0;
		std::vector<std::shared_ptr<const Base>> bases;
		std::vector<std::shared_ptr<const FixedVes::Rec>> fixed; // fixed structures within 25 km, every triangle (design fixed-structures)
		std::vector<ClearZones::Zone> zones; // the base files' clear zones within 25 km (design base-clear)
		std::shared_ptr<const Horizon::Map> hor;
		double hb[4] = {0, 0, 0, 0}; // its owner's bounds
		std::vector<std::pair<int, int>> bldg; // z14 tiles to read
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
		int miss = 0;          // results in a row without the L16 Class tile
	};
	void Work ();
	void MakeMeshes ();
	const std::shared_ptr<const Base> &BaseOf (OBJHANDLE hBase);
	void Cells (double lat, double lng, double range, std::vector<long long> &out) const;
	void Bounds (long long key, double &lat0, double &lat1, double &lng0, double &lng1) const;
	bool Ask (long long key);
	void DrawCells (VkDev *dev, int mode, const D3DXMATRIX *mVPdepth);

	vPlanet *vp;
	OBJHANDLE hObj;
	const Scene *scn;
	double R;
	std::string root;           // the body's texture folder, as PlanetTexturePath (backslashes, oapiResolvePath)
	std::map<long long, Cell> cells;
	std::map<OBJHANDLE, std::shared_ptr<const Base>> bases;
	FixedVes::Set fixed{true};  // the fixed structures on this planet, every triangle
	std::vector<ClearZones::Zone> zones; // the base files' clear zones on this planet, read at the first job (design base-clear)
	bool zonesRead = false;
	double tWant = 0.0, camLat = 0.0, camLng = 0.0;
	bool active = false;        // camera low enough: cells kept and drawn

	VkBuf *vb = nullptr, *ib = nullptr;   // the five meshes and the impostor quad
	int baseV[TreeGeom::NKIND + 1] = {}, startI[TreeGeom::NKIND + 1] = {}, countI[TreeGeom::NKIND + 1] = {};
	VkVertexDecl *decl = nullptr;
	ShaderClass *sh = nullptr;

	std::thread worker;
	std::mutex mx;
	std::condition_variable cv;
	std::deque<Job> todo;       // under mx
	std::deque<Result> ready;   // under mx
	bool quit = false;          // under mx
};

#endif // !__TREEMGR_H
