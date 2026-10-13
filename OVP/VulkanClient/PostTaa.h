// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: temporal anti-aliasing of the 3D world (design-visual-batch 3)

#ifndef __POSTTAA_H
#define __POSTTAA_H

#include "D3D9Util.h"

class Scene;
namespace oapi { class D3D9Client; }

class PostTaa {
public:
	PostTaa (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~PostTaa ();
	void BeginFrame (bool *jitter, float *jx, float *jy); // main pass, before UpdateCameraFromOrbiter: this frame's jitter in pixels, or none
	void Resolve (VkSurf *target);                        // after SceneFx::AfterOpaque, main pass; restores the main target

private:
	oapi::D3D9Client *gc;
	Scene *scn;
	DWORD W, H;
};

#endif // !__POSTTAA_H
