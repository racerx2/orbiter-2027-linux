// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space reflections: the colour copy and depth pyramid the shaders trace (design-ssr)

#ifndef __SCENESSR_H
#define __SCENESSR_H

#include "D3D9Util.h"

class Scene;
namespace oapi { class D3D9Client; }

class SceneSsr {
public:
	SceneSsr (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~SceneSsr ();
	void Capture (VkSurf *target); // after the TAA resolve, before the late labels

private:
	oapi::D3D9Client *gc;
	Scene *scn;
	DWORD W, H;
};

#endif // !__SCENESSR_H
