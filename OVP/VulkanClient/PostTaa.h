// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: temporal anti-aliasing of the 3D world (design-visual-batch 3)

#ifndef __POSTTAA_H
#define __POSTTAA_H

#include "D3D9Util.h"
#include "PostMath.h"

class Scene;
class ImageProcessing;
namespace oapi { class D3D9Client; }

class PostTaa {
public:
	PostTaa (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~PostTaa ();
	void BeginFrame (bool *jitter, float *jx, float *jy); // main pass, before UpdateCameraFromOrbiter: this frame's jitter in pixels, or none
	void Resolve (VkSurf *target);                        // after SceneFx::AfterOpaque, main pass; restores the main target
	bool Active () const { return !skip; }                // after BeginFrame: TAA runs this frame (reset frames too)
	VkTex *Output () const;                               // this frame's resolved scene (RGBA16F, single sample) once Resolve ran, else NULL

private:
	struct Body { OBJHANDLE h = NULL; PostMath::Mat3 R; PostMath::Vec3 o; };
	struct View { bool valid = false; PostMath::Vec3 cam; Body pl, ves; PostMath::Mat4 VP; }; // VP: view times the unjittered projection
	void Capture (View &v) const;

	oapi::D3D9Client *gc;
	Scene *scn;
	VkDev *dev;
	DWORD W, H;
	ImageProcessing *pIP = nullptr;      // PostTaa.glsl: PSTaa
	VkTex *tCur = nullptr, *tHist[2] = {}; // this frame resolved; outputs, RGBA16F
	VkSurf *sCur = nullptr, *sHist[2] = {};
	bool ok = false;
	int cur = 0;                         // tHist[cur] is the last output
	bool reset = true, skip = false;     // this frame: history dropped; no pass (debug camera, wireframe)
	float jx = 0.0f, jy = 0.0f;
	unsigned seq = 0;                    // jittered frames
	DWORD lastFrame = 0, outFrame = 0;   // scene frame of the last Resolve
	OBJHANDLE lastProxy = NULL, lastFocus = NULL, lastTarget = NULL;
	bool lastInternal = false;
	View prev;
};

#endif // !__POSTTAA_H
