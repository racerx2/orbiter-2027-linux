// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: the worker part, snapshot to .glb file without any Orbiter API call (claude/design-gltf-export.md 8)

#pragma once
#include "GxTypes.h"
#include <functional>

struct GxResult {
	bool ok = false;
	std::string path, error;
	int parts = 0, anims = 0, images = 0;
	std::vector<std::string> warnings;
};

using GxResolver = std::function<std::string (const std::string &)>; // Orbiter's '\' and any-case path to the file on disk

GxResult GxExportFile (const GxSnapshot &snap, const std::string &outPath, const GxResolver &resolve, int threads);
