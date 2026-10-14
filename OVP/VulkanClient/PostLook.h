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
class ImageProcessing;
struct TerrainSunCache;
namespace oapi { class D3D9Client; }

class PostLook {
public:
	PostLook (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~PostLook ();
	void Haze (VkSurf *target);  // after the TAA resolve, before markers, exhausts and the cockpit; main pass
	void Final (VkSurf *bb);     // the finished 3D image in the backbuffer, after LightBlur and the measure, before RenderGlares

private:
	static const int MAXSEG = 32;
	int Exhausts (D3DXVECTOR4 *seg, int rect[4]); // haze segments, nearest first (3 vec4 each); their pixel bounds
	VkTex *Copy (VkSurf *src, int slot);          // single-sample copy of src (MSAA resolved)
	VkTex *FxDepth () const;                      // SceneFx's camera distance when written this frame, else NULL
	VkTex *FxVolume () const;                     // SceneFx's volume clouds when they ran this frame, else NULL

	oapi::D3D9Client *gc;
	Scene *scn;
	VkDev *dev;
	DWORD W, H;
	ImageProcessing *pHaze = nullptr;   // PostLook.glsl PSHaze
	ImageProcessing *pShaft = nullptr;  // PSShaftMask, PSShaftBlur
	ImageProcessing *pFinal = nullptr;  // PSFinal: shafts, flare, sharpen, vignette as compiled in
	VkTex *tCopy[2] = {};               // 0 the haze target's copy, 1 the backbuffer's
	VkSurf *sCopy[2] = {};
	VkTex *tShaft[2] = {};              // half resolution: mask, rays
	VkSurf *sShaft[2] = {};
	TerrainSunCache *tsc = nullptr;     // the flare's terrain test from the camera
};

#endif // !__POSTLOOK_H
