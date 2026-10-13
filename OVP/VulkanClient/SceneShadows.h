// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: whole-scene sun shadows, camera-centred cascades in one atlas (design-scene-shadows)

#ifndef __SCENESHADOWS_H
#define __SCENESHADOWS_H

#include "D3D9Util.h"

class Scene;
namespace oapi { class D3D9Client; }

class SceneShadows {
public:
	SceneShadows (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~SceneShadows ();
	void BeforeMain (); // main pass, before the main target is pushed: the cascades
	void AfterResolve (); // after the TAA resolve
	void BeforeCockpit (); // after the exterior glass, before the cockpit interior

private:
	oapi::D3D9Client *gc;
	Scene *scn;
	DWORD W, H;
};

#endif // !__SCENESHADOWS_H
