// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the scene the ray-traced GI's rays see: terrain tiles, vessels and bases as acceleration structures, with their albedo (design-ddgi 3)

#ifndef __DDGISCENE_H
#define __DDGISCENE_H

#include "D3D9Util.h"
#include <unordered_map>
#include <vector>

class Scene;
class vPlanet;
class D3D9Mesh;
class DdgiPass;
struct GiTileRec;

// the anchor frame: x east, y up, z north at a point on the ground, fixed to the body; it moves in whole 1024 m steps
struct DdgiAnchor {
	OBJHANDLE body = NULL;
	VECTOR3 o0;				// the point it was set at, body frame
	MATRIX3 f;				// its axes in the body frame (columns east, up, north)
	int k[3] = { 0, 0, 0 };	// steps from o0 along the axes
	bool valid = false;
	// this frame
	VECTOR3 al;				// anchor, body frame
	VECTOR3 ag;				// anchor, global frame
	VECTOR3 ax, ay, az;		// axes, global frame
	VECTOR3 cam;			// camera, anchor frame
	VECTOR3 ctr;			// body centre, anchor frame
	VECTOR3 geo;			// anchor from the body centre, global axes
	VECTOR3 sun;			// to the sun, anchor frame
	double ground = 0.0;	// the ground under the camera, anchor frame y
	double radius = 0.0;	// body radius
	bool reset = false;		// set again this frame: every probe starts over
};

class DdgiScene {
public:
	static const int MAXINST = 4096;	// TLAS instances
	static const int ATLAS = 2048, CELL = 64; // terrain albedo atlas and its cells
	static const int TABLE = 256;		// mesh albedo table side; texel 0 holds the far ground
	static constexpr double RANGE = 16e3;	// the traced region around the camera, and the rays' reach [m]

	struct Row { float a[4], u[4], v[4]; }; // DDGI.glsl DgRow: kind and albedo index; the terrain's uv as planes

	explicit DdgiScene (VkDev *dev);
	~DdgiScene ();
	bool OK () const { return ok; }

	// anchor, geometry and albedo for this frame, outside a rendering pass; false: nothing to trace
	bool Build (Scene *scn, vPlanet *vp, double range);

	const DdgiAnchor &Anchor () const { return anc; }
	VkAccelerationStructureKHR Tlas () const { return tlas; }
	VkBuf *Rows () const { return rows; }
	VkTex *Atlas () const { return tAtlas; }
	VkTex *Table () const { return tTable; }
	int Instances () const { return nInst; }
	float AlbGamma () const { return albG; }
	float AlbScale () const { return albS; }

private:
	struct Blas {
		VkAccelerationStructureKHR as = VK_NULL_HANDLE;
		VkBuf *buf = nullptr;
		VkDeviceAddress addr = 0;
		int query = -1;		// compaction size query slot, -1 none
		uint64_t frame = 0;	// built in
	};
	struct TileEntry {
		Blas b;
		int cell = -1;
		VkTex *tex = nullptr;
		uint64_t texWrites = ~0ull;
		float range[4] = {};	// its part of the texture: u, v, width, height
		uint64_t used = 0;
		bool albedo = false;
	};
	struct MeshEntry {
		Blas b;
		uint64_t geo = 0, tf = 0, alb = 0; // geometry, group matrices and albedo hashes of the last build and write
		int slot = -1, nslot = 0;
		int refits = 0;
		uint64_t used = 0;
		std::vector<DWORD> groups;		// traced groups, by geometry index
	};
	struct MeshUse { D3D9Mesh *mesh; D3DXMATRIX W; };

	void Anchor (Scene *scn, vPlanet *vp);
	void Gather (Scene *scn, vPlanet *vp, double range, std::vector<GiTileRec> &tiles, std::vector<MeshUse> &meshes);
	void FreeBlas (Blas &b);
	bool MakeBlas (Blas &b, VkDeviceSize size);
	void Transform (const double W[4][4], float out[3][4]) const; // camera-relative global to anchor frame, 3x4 rows
	int CellAlloc ();
	int SlotAlloc (int n);
	void SlotFree (int base, int n);
	void Evict ();
	int Compact (VkCommandBuffer cmd, bool fenced); // the ready tiles' compacted copies, the barrier before them unless fenced; their count
	void WriteTile (const GiTileRec &t, int cell);
	void WriteMesh (D3D9Mesh *m, const MeshEntry &e);
	void WriteFar (vPlanet *vp, const std::vector<GiTileRec> &tiles);
	VkBuf *Scratch (VkDeviceSize size);

	VkDev *dev;
	bool ok = false;
	DdgiAnchor anc;
	VECTOR3 lastCam = { 0, 0, 0 };	// body frame
	uint64_t frame = 0;
	VkDeviceSize scratchAlign = 256;
	VkBuf *scratch = nullptr;
	VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
	VkBuf *tlasBuf = nullptr;
	VkDeviceSize tlasScratch = 0;
	VkBuf *rows = nullptr;
	int nInst = 0;
	std::vector<VkAccelerationStructureInstanceKHR> lastInst;	// the TLAS's instances and rows as last built or refit: a frame within a millimetre keeps them
	std::vector<Row> lastRows;
	int tlasRefits = 0;
	VkQueryPool queries = VK_NULL_HANDLE;
	std::vector<int> freeQueries;
	struct Pending { int query; uint64_t key, frame; };	// compaction size query, tile serial, frame it was written in
	std::vector<Pending> pendingCompact;
	std::unordered_map<uint64_t, TileEntry> tiles;
	std::unordered_map<const D3D9Mesh*, MeshEntry> meshes;
	std::vector<int> freeCells;
	std::vector<std::pair<int, int>> freeSlots;	// base, count
	VkTex *tAtlas = nullptr, *tTable = nullptr;
	VkSurf *sAtlas = nullptr, *sTable = nullptr;
	DdgiPass *pTile = nullptr, *pMean = nullptr;
	const VkTex *farTex = nullptr;
	uint64_t farWrites = ~0ull;
	float albG = 1.0f, albS = 1.0f;
};

#endif // !__DDGISCENE_H
