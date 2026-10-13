// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: whole-scene sun shadows, camera-centred cascades in one atlas (design-scene-shadows)

#include "SceneShadows.h"
#include "Scene.h"

SceneShadows::SceneShadows (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h) : gc(_gc), scn(_scn), W(w), H(h) {}

SceneShadows::~SceneShadows () {}

void SceneShadows::BeforeMain () {}

void SceneShadows::AfterResolve () {}

void SceneShadows::BeforeCockpit () {}
