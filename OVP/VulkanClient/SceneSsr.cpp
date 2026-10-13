// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space reflections: the colour copy and depth pyramid the shaders trace (design-ssr)

#include "SceneSsr.h"
#include "Scene.h"

SceneSsr::SceneSsr (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h) : gc(_gc), scn(_scn), W(w), H(h) {}

SceneSsr::~SceneSsr () {}

void SceneSsr::Capture (VkSurf *target) {}
