// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: DDS top mip to RGBA8 (claude/design-gltf-export.md 6)

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GxImage {
	int w = 0, h = 0;
	std::vector<uint8_t> rgba; // rows top down
	bool dxt1 = false;         // BC1 source: punch-through alpha only
};

enum class GxAlpha { Opaque, Mask, Blend };

bool GxDecodeDds (const uint8_t *data, size_t size, GxImage &out, std::string &err);
GxAlpha GxImageAlpha (const GxImage &img); // Mask when every alpha is 0 or 255
