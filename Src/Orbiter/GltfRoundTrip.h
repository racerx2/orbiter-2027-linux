// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// not upstream: a .glb made from an Orbiter vessel and edited in Blender, read back (claude/design-gltf-roundtrip.md)

#ifndef __GLTFROUNDTRIP_H
#define __GLTFROUNDTRIP_H

#include "GltfLoader.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

struct RtVec { double x = 0, y = 0, z = 0; };

enum { RT_NULL, RT_ROTATE, RT_TRANSLATE, RT_SCALE };

// what the exporter recorded (scene extras, version 2)
struct RtRecComp {
	int type = RT_NULL;
	double state0 = 0, state1 = 1;
	int mesh = 0;                 // vessel mesh index, -1 a vertex list
	bool all = false;             // the whole mesh
	std::vector<int> groups;
	RtVec ref, axis, shift, scale{1, 1, 1};
	double angle = 0;
	int parentAnim = -1, parentComp = -1;
};

struct RtRecAnim {
	double defstate = 0, time0 = 0;
	std::vector<RtRecComp> comps;
};

struct RtRecTex {
	std::string name;
	bool dynamic = false;
	std::string hash;             // "" not exported
};

struct RtRecMesh {
	int index = 0;                // vessel mesh index
	std::string name;
	RtVec ofs;
	int vis = 0, groups = 0;
	std::vector<int> skipped;
	std::vector<DWORD> flags;
	std::vector<MATERIAL> mtrls;
	std::vector<RtRecTex> tex;
};

struct RtRecord {
	std::string vessel, cls;
	std::vector<RtRecMesh> meshes;
	std::vector<RtRecAnim> anims;
};

bool RtParseRecord (const std::string &sceneExtras, RtRecord &out, std::string &err); // false: no version 2 data
bool RtListedMeshes (const std::string &gltfJson, std::vector<std::string> &names);   // the mesh names a file's default scene records
bool RtSameMeshName (const std::string &a, const std::string &b);                     // any case, '/' same as '\'
int RtAnimIndex (const std::string &name);                                             // "n" or "n text": n, else -1

// node extras
enum { RT_TAG_NONE, RT_TAG_MESH, RT_TAG_GROUP, RT_TAG_PIVOT };
struct RtTag {
	int kind = RT_TAG_NONE;
	int mesh = -1, group = -1;    // vessel mesh index, group
	int anim = -1, comp = -1;
	std::vector<std::pair<int, int>> chain; // group: recorded pivot path, outer first
};

// one output group of a file mesh: a primitive (or a part of one over 65535 vertices) at the default pose, mesh-local Orbiter frame
struct RtOut {
	int group = -1;               // Orbiter group index, -1 a new group
	int added = -1;               // index among the new groups
	int node = -1, prim = 0, part = 0;
	int material = -1;            // glTF material, -1 the default
	std::vector<NTVERTEX> vtx;
	std::vector<WORD> idx;
};

// a component for the vessel; groups >= 0 are group indices, -1-j the j-th new group
struct RtCompSpec {
	int anim = 0, type = RT_ROTATE;
	double state0 = 0, state1 = 1;
	RtVec ref, axis, shift, scale{1, 1, 1};
	double angle = 0;
	int parent = -1;              // index into the same list
	std::vector<int> groups;
	int pivotAnim = -1, pivotComp = -1; // the innermost component of that pivot node
};

struct RtMeshPlan {
	std::vector<RtOut> outs;
	std::map<int, int> primary;   // group -> first output of its node
	int nadded = 0;
	std::vector<int> replaced;    // animations taken from the file, ascending
	std::vector<int> fitted;      // of those, animations with fitted parts
	std::vector<RtCompSpec> comps;
	std::map<int, int> recCount;  // recorded components per animation on this mesh
};

struct RtFile {
	std::string path;
	GltfScene scene;
	RtRecord rec;
	std::vector<RtTag> tags;
	std::vector<int> animIndex;   // glTF animation -> Orbiter animation, -1 none
	std::vector<std::vector<double>> restLocal, restWorld; // default pose, glTF frame, column-major 4x4
	std::vector<RtMeshPlan> plans; // per recorded mesh
	std::vector<std::string> warnings;
};

bool RtLoad (const std::string &path, RtFile &f, std::string &err);
bool RtAnalyse (RtFile &f, std::string &err); // default pose, outputs, units, components (RtLoad calls it)

// a base (.msh) group
struct RtBaseGroup {
	const NTVERTEX *vtx = nullptr;
	DWORD nvtx = 0;
	const WORD *idx = nullptr;
	DWORD nidx = 0;
};
bool RtSameGeometry (const std::vector<const RtOut*> &parts, const RtBaseGroup &base); // design 3.1 triangle match

// file materials
struct RtMatTag {
	bool tagged = false;
	int mesh = -1, mtrl = -1, tex = -1;
	bool unlit = false;
	bool edited = false;          // values or maps differ from what the exporter wrote
};
RtMatTag RtMaterialTag (const std::string &materialJson);

std::string RtImageHash (const std::vector<unsigned char> &encoded); // FNV-1a 64 of width, height, RGBA; "" not decodable
std::string RtPixelHash (int w, int h, const unsigned char *rgba);

// a node's local matrix (glTF frame, column-major) with each recorded animation at states[a] (the lowest animation on a path), for tests
void RtLocalAt (const RtFile &f, int node, const std::vector<double> &states, std::vector<double> &m);

// Orbiter's component semantics (the client's Animate), for the fitter's tests
double RtFrac (double state0, double state1, double s);
void RtQuatGltf (const RtVec &axis, double angle, double q[4]); // Orbiter rotation as a glTF quaternion (x, y, z, w)

#endif // !__GLTFROUNDTRIP_H
