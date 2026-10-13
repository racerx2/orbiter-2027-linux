// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: heat haze, light shafts, lens flare, sharpening and vignette (design-visual-batch 4-7)

#include "PostLook.h"
#include "Scene.h"

PostLook::PostLook (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h) : gc(_gc), scn(_scn), W(w), H(h) {}

PostLook::~PostLook () {}

void PostLook::Haze (VkSurf *target) {}

void PostLook::Final (VkSurf *bb) {}
