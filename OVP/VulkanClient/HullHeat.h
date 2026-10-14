// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: re-entry hull heating (design-reentry-heat): wall temperatures with lag per vessel, gHeat of the vessel's exterior draws (VolPlumes)

#pragma once

#include "D3D9Util.h"

class vVessel;

const int HEAT_NSEG = 16;		// attachment segments in the block (HeatPrm.glsl HEAT_NSEG, HeatMath::Tune::edgeSegs)

struct HeatPrm {				// shaders/HeatPrm.glsl (scalar layout)
	float vFlow[4];				// downstream direction, w 1 on
	float vStag[4];				// stagnation point (camera-relative), w nose radius
	float vCoef[4];				// flat-plate factor, lee share, visible emissivity floor, spare
	float vBin[8][4];			// ln glow radiance per bin, w temperature
	float vEdgeN[4];			// number of attachment segments
	float vEdge[3 * HEAT_NSEG][4];	// per segment: end a and band half-width, end b and level, attachment normal (Rev 3)
};
static_assert(sizeof(HeatPrm) == 176 + 16 + 3 * HEAT_NSEG * 16, "HeatPrm must match shaders/HeatPrm.glsl");

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
	static void Edges (vVessel *vv, State &s);	// the attachment segments on the meshes' real edges for the flow (Rev 3)
};
