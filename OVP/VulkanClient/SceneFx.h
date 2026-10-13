// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the SceneDepth pass, SSAO, volumetric clouds and the exposure measure (design-ground-graphics 1, 3, 4, 6, 7)

#ifndef __SCENEFX_H
#define __SCENEFX_H

#include "D3D9Util.h"
#include "AutoExpo.h"
#include "VolCloudMap.h"
#include <list>
#include <memory>

class Scene;
class vVessel;
class vPlanet;
class ImageProcessing;
namespace oapi { class D3D9Client; }

class SceneFx {
public:
	SceneFx (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~SceneFx ();

	float BeginFrame ();                   // main scene start: reads the measure two frames back, returns e
	void AfterOpaque (const std::list<vVessel*> &vessels, VkSurf *target); // after the opaque vessels, main pass
	void Measure (VkSurf *target);         // after the cockpit, main pass
	float CloudFade () const { return cloudFade; } // volume weight drawn last frame (the proxy's 2D layer fades by it)
	ShaderClass *TerrainDepth () const { return pTerrain; }

private:
	void InitDepth ();
	void InitSSAO ();
	void InitClouds ();
	void InitExposure ();
	bool WantSSAO (const std::list<vVessel*> &vessels) const;
	bool CloudsWanted (vPlanet *vp, float *wV) const;
	void CloudUpdate (vPlanet *vp);
	void DepthPass (const std::list<vVessel*> &vessels);
	void Ssao (VkSurf *target);
	void Clouds (VkSurf *target, vPlanet *vp, float wV);

	oapi::D3D9Client *gc;
	Scene *scn;
	VkDev *dev;
	DWORD W, H;
	DWORD frame = 0;

	VkTex *tDepth = nullptr;               // camera distance, R32F, 0 none
	VkSurf *sDepth = nullptr, *sDepthDS = nullptr;
	ShaderClass *pTerrain = nullptr;       // SceneDepth.glsl: tiles
	bool depthOK = false;

	VkTex *tAO[2] = {};                    // half resolution, ping-pong
	VkSurf *sAO[2] = {};
	ImageProcessing *pAO = nullptr;        // SSAO.glsl: PSAO, PSBlur, PSComposite

	ImageProcessing *pVol = nullptr;       // VolCloud.glsl: VolMarch, VolBlur, VolComposite
	VkTex *tVol[2] = {};                   // half resolution RGBA16F, premultiplied
	VkSurf *sVol[2] = {};
	VkTex *tShape = nullptr, *tDetail = nullptr; // noise volumes, 64^3 and 32^3
	VkTex *tCov = nullptr;                 // coverage map
	struct CloudBox;                       // job results
	std::shared_ptr<CloudBox> cbox;
	int poolId = 0;
	bool noiseAsked = false, mapAsked = false, mapValid = false;
	VolCloud::Frame mapFrame;              // tCov's centre, cloud frame
	OBJHANDLE mapObj = NULL;
	double mapTime = -1e9;
	size_t mapSig = 0;
	float cloudFade = 0.0f;

	VkTex *tHalf[3] = {};                  // w/2, w/4, w/8 of the scene, RGBA16F
	VkSurf *sHalf[3] = {};
	VkTex *tBlocks = nullptr, *tTotal = nullptr; // 16x9 sums, 1x1 result
	VkSurf *sBlocks = nullptr, *sTotal = nullptr;
	ImageProcessing *pExpo = nullptr;      // Exposure.glsl: PSBlocks, PSTotal
	AutoExpo::Controller expo;
	OBJHANDLE expoFocus = NULL;            // a focus or camera mode change resets e
	bool expoInternal = false;
};

#endif // !__SCENEFX_H
