// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: temporal anti-aliasing of the 3D world (design-visual-batch 3)

#include "PostTaa.h"
#include "Scene.h"

PostTaa::PostTaa (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h) : gc(_gc), scn(_scn), W(w), H(h) {}

PostTaa::~PostTaa () {}

void PostTaa::BeginFrame (bool *jitter, float *jx, float *jy)
{
	*jitter = false; *jx = *jy = 0.0f;
}

void PostTaa::Resolve (VkSurf *target) {}
