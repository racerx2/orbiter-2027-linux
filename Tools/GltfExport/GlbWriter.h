// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: snapshot, nodes and images to GLB bytes (claude/design-gltf-export.md 3, 6, 7)

#pragma once
#include "AnimModel.h"
#include "DdsDecode.h"
#include <map>
#include <tuple>

struct GxPng {
	std::vector<uint8_t> png;
	GxAlpha alpha = GxAlpha::Opaque;
};

struct GxTexSet {           // image indices of one mesh texture and its maps, -1 none
	int base = -1, normal = -1, emissive = -1, mr = -1;
	bool rough = false, metal = false; // which of _rghn and _metal the mr image carries
};

struct GxGlbInput {
	const GxSnapshot *snap = nullptr;
	const GxModel *model = nullptr;
	std::vector<std::vector<GxTexSet>> tex;           // [snapshot mesh][texture index]
	std::vector<std::vector<std::string>> mtrlNames;  // [snapshot mesh][material index]
	std::vector<std::vector<std::string>> texNames;   // [snapshot mesh][texture index], "0" none
	std::vector<std::vector<bool>> texDynamic;        // [snapshot mesh][texture index]
	std::vector<std::vector<std::string>> texHash;    // [snapshot mesh][texture index]: GxPixelHash of the base image, "" none
	std::map<std::tuple<int, int, int>, int> glow;    // (snapshot mesh, material, texture) -> image of max (emis, base * emissive), lit groups
	std::vector<GxPng> images;
	std::string generator = "Orbiter GltfExport 0.3";
};

bool GxWriteGlb (const GxGlbInput &in, std::vector<uint8_t> &out, std::string &err);
std::string GxPixelHash (int w, int h, const uint8_t *rgba); // FNV-1a 64 of width, height and RGBA, as the core's RtPixelHash
double GxSrgbToLinear (double c);
double GxRoughness (const float specular[4], float power);
