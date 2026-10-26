// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: plain data copied from a vessel (claude/design-gltf-export.md)

#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct GxVec3 {
	double x = 0, y = 0, z = 0;
};

struct GxVertex {
	float p[3], n[3], uv[2]; // Orbiter frame, mesh local
};

struct GxGroup {
	std::vector<GxVertex> vtx;
	std::vector<uint16_t> idx;
	int mtrl = -1;           // material index after inherit, -1 the default material
	int tex = -1;            // texture index after inherit, -1 none
	uint32_t usrFlag = 0;
};

struct GxMaterial {
	float diffuse[4], ambient[4], specular[4], emissive[4];
	float power;
};

struct GxMesh {
	std::string name;        // mesh name as Orbiter has it ("" for a mesh made in code)
	std::string file;        // .msh file path, "" when unknown
	bool gltf = false;       // a .glb/.gltf mesh: no texture names
	int index = 0;           // vessel mesh index
	GxVec3 ofs;              // mesh offset in the vessel frame
	int vis = 0;             // MESHVIS_* flags
	int ntex = 0;            // texture count of the loaded mesh
	std::vector<GxGroup> groups;
	std::vector<GxMaterial> mtrls;
};

enum class GxType { Null, Rotate, Translate, Scale };

struct GxComp {
	int anim = 0, index = 0; // owning animation and position in its list
	double state0 = 0, state1 = 1;
	GxType type = GxType::Null;
	int mesh = 0;            // vessel mesh index, -1 a vertex list
	bool allGroups = false;  // grp NULL: the whole mesh
	std::vector<int> groups;
	GxVec3 ref, axis, shift, scale{1, 1, 1};
	double angle = 0;
	int parent = -1;         // index into GxAnimSet::comps
	std::vector<int> children;
	bool skip = false;       // left out of the nodes (warned)
};

struct GxAnim {
	double defstate = 0, state = 0;
	std::vector<int> comps;  // indices into GxAnimSet::comps, in list order
};

struct GxAnimSet {
	std::vector<GxAnim> anims;
	std::vector<GxComp> comps;
};

struct GxSnapshot {
	std::string vesselName, className;
	std::vector<GxMesh> meshes;
	GxAnimSet anims;         // rest parameters
	std::vector<std::string> texDirs; // HightexDir then TextureDir, as Orbiter.cfg has them
	std::vector<std::string> warnings;
};
