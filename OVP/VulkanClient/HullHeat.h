// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: re-entry hull heating (design-reentry-heat): wall temperatures with lag per vessel, gHeat of the vessel's exterior draws (VolPlumes)

#pragma once

#include "D3D9Util.h"

class vVessel;

struct HeatPrm {				// shaders/HeatPrm.glsl (scalar layout)
	float vFlow[4];				// downstream direction, w 1 on
	float vStag[4];				// stagnation point (camera-relative), w nose radius
	float vCoef[4];				// flat-plate factor, lee share, visible emissivity floor, edge level
	float vBin[8][4];			// ln glow radiance per bin, w temperature
};
static_assert(sizeof(HeatPrm) == 176, "HeatPrm must match shaders/HeatPrm.glsl");

class HullHeat {
public:
	static void GlobalInit ();				// vVessel::GlobalInit, after D3D9Client.fx exists
	static void GlobalExit ();
	static bool On ();						// VolPlumes and gHeat in D3D9Client.fx
	static void Step (vVessel *vv);			// vVessel::Update: the wall temperatures in sim time
	struct Scope {							// vVessel::Render: gHeat for this vessel's exterior draws, zero again after
		Scope (vVessel *vv, bool internal);
		~Scope ();
		bool set = false;
	};
	struct State;							// one vessel's heating, HullHeat.cpp
private:
	static bool Upstream (vVessel *vv, const double up[3], double out[3]);	// the hull's upstream-most point from its meshes (friend of vVessel)
};
