// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: volumetric plumes, smoke puffs and re-entry plasma (design-volumetric-plumes)

#ifndef __POSTPLUME_H
#define __POSTPLUME_H

#include "D3D9Util.h"
#include "PlumeMath.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Scene;
class vVessel;
class vPlanet;
class ImageProcessing;
class D3D9ParticleStream;
namespace oapi { class D3D9Client; }

class PostPlume {
public:
	PostPlume (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~PostPlume ();
	void Collect (); // main pass, before SceneFx::AfterOpaque: plumes, plasma and puffs of this frame; claims what it will draw
	void Render (VkSurf *target); // after PostLook::Haze, before the exhausts and particles
	static double LegacyWeight (const vVessel *vv, DWORD idx); // the legacy exhaust's level factor: 1 legacy, 0 the volume draws it
	static float LegacyReentry (const vVessel *vv);            // the legacy re-entry billboard's factor
	static const D3DXMATRIX *FlowVP ();                        // RENDERPASS_FLOWMAP's view projection while a flow map renders
	static void HazeShape (const vVessel *vv, DWORD idx, double *r0, double *r1, double *len); // PostLook's haze: the volume plume's radii and length

private:
	struct NoiseBox;
	struct ClassCfg;
	struct Nozzle;
	struct Plume;
	struct Plasma;
	struct GroundPuff;
	struct Slot { SURFHANDLE h = nullptr; VkTex *tex = nullptr; VkImage img = VK_NULL_HANDLE; DWORD used = 0; bool baked = false; };

	bool NoiseReady ();
	const ClassCfg &CfgOf (VESSEL *v);
	Nozzle &NozzleOf (VESSEL *v, DWORD idx, const EXHAUSTSPEC &es, const ClassCfg &cfg);
	int SlotOf (SURFHANDLE h); // atlas slot when baked, else -1 (the bake is queued)
	void GatherPlumes ();
	void UpdateGround ();
	void GatherPlasma ();
	void GatherPuffs ();
	void FillPlume (int i, const Plume &p);
	void FillPlasma (int k, const Plasma &z);
	void ScreenRect ();
	void Revoke ();
	void Bakes ();
	void FlowMaps ();
	void Puffs (VkTex *dep);
	bool March (VkTex *dep);
	void Composite (VkSurf *target, VkTex *dep, bool vol);
	void SetCommon (ImageProcessing *ip);

	oapi::D3D9Client *gc;
	Scene *scn;
	VkDev *dev;
	DWORD W, H, hw, hh;
	static PostPlume *inst;

	bool ok = false;
	ImageProcessing *ipi = nullptr;           // Plume.glsl: march, blur, composite, atlas bake, flow prep, reduce, blur
	ShaderClass *pPuff = nullptr;             // Plume.glsl: PuffVS, PuffPS
	VkTex *tVolP = nullptr, *tVolPD = nullptr, *tVolP2 = nullptr, *tPuff[2] = {};
	VkSurf *sVolP = nullptr, *sVolPD = nullptr, *sVolP2 = nullptr, *sPuff[2] = {};
	VkTex *tAtlas = nullptr;                  // 16 slots of 32x64: flame strip, flare, mean colour
	VkSurf *sAtlas = nullptr;
	VkTex *tNoise = nullptr;                  // 32^3 Perlin-Worley
	VkTex *tPuffData[3] = {};                 // 384 x 8, 32, 128 RGBA32F: 6 texels per puff
	VkTex *tFlowRaw[2][2] = {}, *tFlowP[2] = {}, *tFlowB[2] = {}, *tFlowBT = nullptr, *tFlowR8[2] = {}, *tFlowR[2] = {};
	VkSurf *sFlowRaw[2][2] = {}, *sFlowP[2] = {}, *sFlowB[2] = {}, *sFlowBT = nullptr, *sFlowR8[2] = {}, *sFlowR[2] = {}, *sFlowDS = nullptr;
	bool flowOK = false;
	std::shared_ptr<NoiseBox> nbox;
	int poolId = 0;
	bool noiseAsked = false;
	SURFHANDLE hExhaustTex = nullptr;
	D3DXVECTOR4 pal[24];

	DWORD frame = 0, claimFrame = 0, reentryFrame = 0;
	double pxs = 1.0;                         // full-res pixels per metre at 1 m
	double planes[6][4];
	float time = 0.0f;
	std::vector<Plume> plumes;
	std::vector<Plasma> plasma;
	std::vector<D3DXVECTOR4> vPl, vPz, puffData;
	std::vector<int> puffLayer;
	int nPuff = 0, puffRows = 0;
	std::vector<D3D9ParticleStream *> claimed;
	std::unordered_map<const vVessel *, std::vector<float>> legacy;
	std::unordered_map<const vVessel *, std::vector<int>> plumeOf;
	std::unordered_map<const vVessel *, float> reentryW;
	bool reentryOff = false;	// the plasma runs: no legacy re-entry billboard at all
	std::map<std::pair<OBJHANDLE, DWORD>, Nozzle> *nozzles = nullptr;
	std::map<std::string, ClassCfg> *classes = nullptr;
	std::map<OBJHANDLE, std::vector<GroundPuff>> *ground = nullptr;
	std::map<OBJHANDLE, double> groundAcc;
	double lastSimt = -1e30;
	OBJHANDLE lastFocus = NULL;
	Slot atlasSlot[16];
	std::vector<int> bakeQueue;
	D3DXMATRIX flowVP;
	bool flowOn = false;
	float rectF[4] = { 0, 0, 1, 1 };          // screen rect of everything, uv x, y, w, h
	double collectMs = 0.0;
	int collectN = 0;
	int nLit = 0, diagN = 0, drawN = 0; // lit exhausts this frame; the first frames with any, and the first draws, are logged
};

#endif // !__POSTPLUME_H
