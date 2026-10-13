// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: SSR parameters of one draw (design-ssr 4; SceneSsr.h SsrPrm, 224 bytes), appended as gSsr after gCsm (INT-2)

#ifndef SSR_PRM_GLSL
#define SSR_PRM_GLSL

struct SsrPrm
{
	mat4 mPrev;		// camera-relative point now to the source clip space, rigid with the drawn body
	mat4 mSky;		// direction now to the source clip space (w = 0)
	vec4 vHiZ;		// screen W, H; top level; iterations
	vec4 vJit;		// source depth jitter in px (xy); start lift in pixel angles (z); near w in m (w)
	vec4 vMode;		// valid, stochastic, noise phase, sky weight
	vec4 vThick;	// thickness m, thickness per m of depth, roughness fade start, end
	vec4 vDecode;	// planet exposure ratio e_now / e_src, mesh 1 / e_src, colour top mip, edge fade width
	vec4 vSurf;		// kind (0 vessel, 1 base surface, 2 base building), alpha, F0, albedo factor
};

#endif
