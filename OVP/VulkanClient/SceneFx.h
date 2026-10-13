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
	float VolShadowFade () const { return volShdFade; } // volume shadow weight drawn last frame (the proxy's 2D cloud shadow fades by it)
	ShaderClass *TerrainDepth () const { return pTerrain; }
	bool DepthOK () const { return depthOK; }   // the SceneDepth targets exist
	bool DepthValid () const;                   // tDepth was written this frame (design-visual-batch S4)
	VkTex *Depth () const;                      // tDepth when written this frame, else NULL
	VkTex *VolumeTex () const;                  // the volume clouds, premultiplied, half res, when they drew this frame, else NULL
	VkTex *Flag () const;                       // TAA: 1 on the camera target vessel, when written this frame, else NULL
	VkTex *VolumeDist () const;                 // TAA: the volume clouds' mean distance, half res, when written this frame, else NULL
	void CameraRays (D3DXVECTOR4 &camX, D3DXVECTOR4 &camY, D3DXVECTOR4 &camZ) const; // VolCloud.glsl PixelRay's vCamX/Y/Z; the TAA jitter folded into camZ
	static bool DepthWanted ();                 // a feature reads tDepth: the depth targets exist and the terrain records for it (ue5-integration INT-4)
	bool DepthRanThisFrame () const { return depthFrame != 0 && DepthValid(); }
	float FogAlongRay (const VECTOR3 &from, const VECTOR3 &dir, double dist) const; // SceneFxFog.cpp: fog transmittance along a ray (global frame), 1 without fog
	float FogTowardSun () const;                // SceneFxFog.cpp: fog transmittance from the camera toward the sun, 1 without fog
	VkTex *FogTex () const;                     // SceneFxFog.cpp: half-res fog (rgb in-scatter premultiplied, a opacity) when drawn this frame, else NULL

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
	void FlagPass (const std::list<vVessel*> &vessels); // TAA: the camera target vessel's pixels
	void VolShadow (VkSurf *target, vPlanet *vp, float wV); // SceneFxShadow.cpp: ground shadows of the volume clouds (design-visual-batch 8)
	struct VolShadowState;
	static void FreeVolShadow (VolShadowState *s); // SceneFxShadow.cpp
	void InitVolShadow ();                  // SceneFxShadow.cpp
	void Gi (VkSurf *target, vPlanet *vp);  // SceneFxGi.cpp: screen-space GI (design-ssgi), after VolShadow
	struct GiState;
	static void FreeGi (GiState *s);        // SceneFxGi.cpp
	void InitGi ();                         // SceneFxGi.cpp, at start when SSGI is on
	void FogAndClouds (VkSurf *target, vPlanet *vp, bool clouds, float wV); // SceneFxFog.cpp: the height fog and the volume clouds, in the order the camera altitude needs
	struct FogState;
	static void FreeFog (FogState *s);      // SceneFxFog.cpp
	void InitFog ();                        // SceneFxFog.cpp, at start when HeightFog is on
	struct VolPrm { D3DXVECTOR4 camX, camY, camZ, mapE, mapN, mapU, clA, clB, clC, slab, fade, noise; };
	void VolParams (vPlanet *vp, VolPrm &v) const;       // VolCloud.glsl's VolPS fields of the cloud layer, shared by the march and the shadow
	void SetVolParams (ImageProcessing *ip, const VolPrm &v) const; // all but vMarch and vTex

	oapi::D3D9Client *gc;
	Scene *scn;
	VkDev *dev;
	DWORD W, H;
	DWORD frame = 0;

	VkTex *tDepth = nullptr;               // camera distance, R32F, 0 none
	VkSurf *sDepth = nullptr, *sDepthDS = nullptr;
	ShaderClass *pTerrain = nullptr;       // SceneDepth.glsl: tiles
	bool depthOK = false;
	DWORD depthFrame = 0, volFrame = 0, flagFrame = 0; // scene frame of the last write
	VkTex *tFlag = nullptr;                // TAA: camera target vessel, R8
	VkSurf *sFlag = nullptr;
	VkTex *tVolD = nullptr;                // TAA: volume cloud distance, half res R32F (VolMarch MRT)
	VkSurf *sVolD = nullptr;

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
	VolShadowState *vsh = nullptr;         // SceneFxShadow.cpp's targets and shaders
	GiState *gi = nullptr;                 // SceneFxGi.cpp
	FogState *fog = nullptr;               // SceneFxFog.cpp
	float volShdFade = 0.0f;

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
