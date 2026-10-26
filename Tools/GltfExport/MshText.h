// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: group labels, material names and texture names from .msh text, read as the core's reader (Mesh.cpp)

#pragma once
#include <istream>
#include <string>
#include <vector>

struct GxMshText {
	bool ok = false;                    // MSHX1 and GROUPS read
	std::vector<std::string> labels;    // per group the core loads, "" without LABEL
	std::vector<std::string> materials; // names from the MATERIALS list
	std::vector<std::string> textures;  // names from the TEXTURES list, "0" for none
	std::vector<bool> dynamic;          // per texture: a "D" flag (Mesh.cpp: loaded uncompressed)
};

bool GxReadMshText (std::istream &is, GxMshText &out);
