// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: heat haze, light shafts, lens flare, sharpening and vignette (design-visual-batch 4-7)

#ifndef __POSTLOOK_H
#define __POSTLOOK_H

#include "D3D9Util.h"

class Scene;
namespace oapi { class D3D9Client; }

class PostLook {
public:
	PostLook (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~PostLook ();
	void Haze (VkSurf *target);  // after the TAA resolve, before markers, exhausts and the cockpit; main pass
	void Final (VkSurf *bb);     // the finished 3D image in the backbuffer, after LightBlur and the measure, before RenderGlares

private:
	oapi::D3D9Client *gc;
	Scene *scn;
	DWORD W, H;
};

#endif // !__POSTLOOK_H
