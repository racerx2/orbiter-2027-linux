// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: volumetric plumes, smoke puffs and re-entry plasma (design-volumetric-plumes)

#ifndef __POSTPLUME_H
#define __POSTPLUME_H

#include "D3D9Util.h"

class Scene;
namespace oapi { class D3D9Client; }

class PostPlume {
public:
	PostPlume (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~PostPlume ();
	void Collect (); // main pass, before SceneFx::AfterOpaque: what to draw this frame
	void Render (VkSurf *target); // after PostLook::Haze, before the exhausts and particles

private:
	oapi::D3D9Client *gc;
	Scene *scn;
	DWORD W, H;
};

#endif // !__POSTPLUME_H
