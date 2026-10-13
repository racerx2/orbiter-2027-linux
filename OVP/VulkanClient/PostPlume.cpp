// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: volumetric plumes, smoke puffs and re-entry plasma (design-volumetric-plumes)

#include "PostPlume.h"
#include "Scene.h"

PostPlume::PostPlume (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h) : gc(_gc), scn(_scn), W(w), H(h) {}

PostPlume::~PostPlume () {}

void PostPlume::Collect () {}

void PostPlume::Render (VkSurf *target) {}
