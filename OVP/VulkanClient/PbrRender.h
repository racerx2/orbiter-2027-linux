// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// custom: PBR material path: shader variants, textures, environment prefilters, lookup texture, draw phase (design-pbr 3, 6, 7, 8)

#ifndef __PBRRENDER_H
#define __PBRRENDER_H

#include "PbrMaterial.h"
#include "VkShader.h"
#include <vector>

class ImageProcessing;

// environment cubes of one vessel
struct PbrEnv {
	VkTex *src = nullptr;     // linear copy of the legacy cube, full mip chain
	VkTex *ggx = nullptr;     // GGX-prefiltered, mip = roughness
	VkTex *sheen = nullptr;   // Charlie-prefiltered, mip = sheen roughness
	VkTex *irr = nullptr;     // cosine-convolved (irradiance / pi)
	bool ready = false;
	void Release ();
	~PbrEnv () { Release (); }
};

// one compiled feature combination of PbrMesh.fx
struct PbrVariant {
	VkEffect *fx = nullptr;
	bool failed = false;
	VkFxHandle tech = nullptr, hW = nullptr, hMtrl = nullptr, hColor = nullptr;
	VkFxHandle hScreen = nullptr, hEnvMips = nullptr, hSheenMips = nullptr, hWorldScale = nullptr;
	VkFxHandle hEnvMode = nullptr, hIrrMode = nullptr, hSceneCopy = nullptr;
	VkFxHandle hShadowS = nullptr, hIrradS = nullptr, hEnvIrrS = nullptr, hEnvGgxS = nullptr, hEnvSheenS = nullptr;
	VkFxHandle hEnvLegacyS = nullptr, hLutS = nullptr, hSceneS = nullptr;
	VkFxHandle hSlot[PBR_SLOT_COUNT] = {};
	std::vector<std::pair<VkFxHandle, VkFxHandle>> shared;   // legacy effect handle, this effect's handle
};

class PbrRender {
public:
	enum Phase { PHASE_ALL, PHASE_OPAQUE, PHASE_TRANSPARENT };

	static void GlobalInit (VkDev *dev);       // scene creation: the lookup texture; ORBITER_PBR_PRECOMPILE checks every variant
	static void GlobalExit ();
	static bool Disabled ();                    // ORBITER_PBR_DISABLE: material configs keep their legacy look

	static void LoadTextures (PbrMaterial &m);  // PBR_TEX files into m.map (sRGB slots decode on the GPU)
	static bool NeedsDecode (const VkTex *t);   // a colour map in a format the sampler doesn't decode (UNORM, not float)
	static PbrVariant *Variant (unsigned features); // created on first use; NULL if it can't compile or the lookup texture failed
	static void BeginVariant (PbrVariant *v, VkTex *legacyEnv); // shared values from the legacy effect, frame values, environment

	static void SetEnvironment (const PbrEnv *env) { curEnv = env; }
	static void PrefilterEnv (VkDev *dev, VkTex *legacyCube, PbrEnv &env);

	static void SetPhase (Phase p) { phase = p; }
	static Phase GetPhase () { return phase; }
	static void SetSceneCopy (VkTex *t, VkSurf *src = NULL); // the copy of src for transmission; resets the refresh state
	static void NoteGlassDrawn (const float rect[4]);  // design-pbr-nested-glass 3: NDC rect of glass drawn since the copy
	static bool WantRefresh (const float rect[4]);     // the copy misses glass drawn over rect
	static void RefreshSceneCopy ();                   // copy src again

private:
	static bool InitIBL (VkDev *dev);

	static VkDev *dev;
	static PbrVariant variants[PBRF_COUNT];
	static const PbrEnv *curEnv;
	static Phase phase;
	static VkTex *sceneCopy;
	static VkSurf *copySrc;
	static float dirty[4][4];
	static int nDirty;
	static int refreshes;
	static VkTex *lut;
	static bool lutOk;
	static ImageProcessing *ipDecode, *ipFilter, *ipLut;
};

#endif // !__PBRRENDER_H
