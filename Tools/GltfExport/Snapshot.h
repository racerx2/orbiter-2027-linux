// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: a vessel's meshes and animations copied through the SDK (claude/design-gltf-export.md 4, 5.2)

#pragma once
#include "GxTypes.h"

class VESSEL;

// hasClient: a graphics client runs (a vessel without a visual then has moved parameters from a gone one)
bool GxTakeSnapshot (VESSEL *v, bool hasClient, GxSnapshot &out, std::string &err);
