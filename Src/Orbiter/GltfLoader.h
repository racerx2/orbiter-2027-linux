// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// not upstream: glTF 2.0 reader for meshes (claude/design-gltf-loader.md 2)

#ifndef __GLTFLOADER_H
#define __GLTFLOADER_H

#include "OrbiterAPI.h"
#include <string>
#include <vector>

struct GltfGroup {
	std::vector<NTVERTEX> vtx;
	std::vector<WORD> idx;
	DWORD mtrl = 0;        // index into GltfModel::materials
	int tex = -1;          // index into GltfModel::textures, -1 none
};

struct GltfMaterial {
	MATERIAL mat;          // legacy approximation of the PBR material
	std::string pbr;       // PBR_* config lines (PbrMaterial.cpp syntax)
	int tex = -1;          // base colour texture, index into GltfModel::textures
};

struct GltfModel {
	std::vector<GltfGroup> groups;
	std::vector<GltfMaterial> materials;
	std::vector<std::string> textures;   // "@gltf:" image references (GltfImageRef)
	std::vector<std::string> warnings;
};

bool GltfIsName (const char *name);    // ends in .glb or .gltf, any case
bool GltfRead (const std::string &path, GltfModel &out, std::string &err);

// "@gltf:<off>:<len>:<b64>:<sliceoff>:<slicelen>:<file>": bytes off..off+len of file, base64-decoded if b64, then the slice (slicelen 0: to the end)
struct GltfImageRef {
	unsigned long long off = 0, len = 0, sliceOff = 0, sliceLen = 0;
	bool b64 = false;
	std::string file;
};
bool GltfParseImageRef (const char *name, GltfImageRef &ref);
std::string GltfMakeImageRef (const GltfImageRef &ref);
bool GltfBase64 (const char *s, size_t n, std::vector<unsigned char> &out); // skips '\' and whitespace
bool GltfImageBytes (const std::string &ref, std::vector<unsigned char> &out); // the bytes an image reference points to

// not upstream: the whole scene, node by node, for the round trip (claude/design-gltf-roundtrip.md 6)
struct GltfPrim {
	std::vector<double> pos, nrm, uv; // per vertex 3, 3, 2 (nrm, uv empty: none), node space, glTF axes
	std::vector<uint32_t> tri;        // triangle list, counterclockwise fronts as in the file
	int material = -1;                // glTF material, -1 the default material
};

struct GltfNodeSrc {
	std::string name;
	int parent = -1, mesh = -1;
	std::vector<int> children;
	bool hasMatrix = false;
	double matrix[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; // column-major
	double t[3] = { 0, 0, 0 }, r[4] = { 0, 0, 0, 1 }, s[3] = { 1, 1, 1 };
	std::string extras;               // JSON text, "" none
};

enum { GLTF_LINEAR, GLTF_STEP, GLTF_CUBIC };
enum { GLTF_TRANSLATION, GLTF_ROTATION, GLTF_SCALE };

struct GltfSamplerSrc {
	std::vector<double> in, out;      // out: comps per key, CUBICSPLINE in-tangent, value, out-tangent
	int comps = 0, interp = GLTF_LINEAR;
};

struct GltfChannelSrc { int node = -1, path = GLTF_TRANSLATION, sampler = -1; };

struct GltfAnimSrc {
	std::string name;
	std::vector<GltfSamplerSrc> samplers;
	std::vector<GltfChannelSrc> channels;
};

struct GltfScene {
	std::vector<GltfNodeSrc> nodes;
	std::vector<int> roots;           // the default scene's nodes
	std::vector<std::vector<GltfPrim>> meshes;
	std::vector<GltfMaterial> materials;   // G0 conversion, one per glTF material, then the default material
	std::vector<std::string> materialJson; // each glTF material's JSON text
	std::vector<int> materialImage;        // each glTF material's base colour image, -1 none
	std::vector<std::string> textures;     // image references, indexed by GltfMaterial::tex
	std::vector<std::string> images;       // each glTF image's reference, "" unusable
	std::vector<GltfAnimSrc> anims;
	std::string sceneExtras;               // JSON text of the default scene's extras
	std::vector<std::string> warnings;
};

bool GltfReadScene (const std::string &path, GltfScene &out, std::string &err);
bool GltfReadJson (const std::string &path, std::string &json, std::string &err); // the JSON text only (GLB JSON chunk, or the .gltf)

#endif // !__GLTFLOADER_H
