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

#endif // !__GLTFLOADER_H
