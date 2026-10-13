// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: ground shadows of the volumetric clouds (design-visual-batch 8)

#include "SceneFx.h"

struct SceneFx::VolShadowState {};

void SceneFx::FreeVolShadow (VolShadowState *s)
{
	delete s;
}

void SceneFx::VolShadow (VkSurf *target, vPlanet *vp, float wV)
{
	volShdFade = 0.0f;
}
