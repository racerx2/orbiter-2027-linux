// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: height and valley fog (design-height-fog)

#include "SceneFx.h"

struct SceneFx::FogState {};

void SceneFx::FreeFog (FogState *s)
{
	delete s;
}

void SceneFx::InitFog () {}

void SceneFx::FogAndClouds (VkSurf *target, vPlanet *vp, bool clouds, float wV)
{
	if (clouds) Clouds(target, vp, wV);
}

float SceneFx::FogAlongRay (const VECTOR3 &from, const VECTOR3 &dir, double dist) const { return 1.0f; }

float SceneFx::FogTowardSun () const { return 1.0f; }

VkTex *SceneFx::FogTex () const { return nullptr; }
