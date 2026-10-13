// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space global illumination (design-ssgi)

#include "SceneFx.h"

struct SceneFx::GiState {};

void SceneFx::FreeGi (GiState *s)
{
	delete s;
}

void SceneFx::InitGi () {}

void SceneFx::Gi (VkSurf *target, vPlanet *vp) {}
