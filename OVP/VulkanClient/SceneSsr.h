// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space reflections: the colour copy and depth pyramid the shaders trace, and each draw's parameters (design-ssr)

#ifndef __SCENESSR_H
#define __SCENESSR_H

#include "D3D9Util.h"
#include "PostMath.h"
#include <map>
#include <vector>

class Scene;
class ImageProcessing;
class vPlanet;
class ShaderClass;
namespace oapi { class D3D9Client; }

// one draw's parameters, shaders/SsrPrm.glsl (scalar layout, row vectors)
struct SsrPrm {
	float mPrev[16], mSky[16];
	float vHiZ[4], vJit[4], vMode[4], vThick[4], vDecode[4], vSurf[4];
};
static_assert(sizeof(SsrPrm) == 224, "SsrPrm.glsl");

class SceneSsr {
public:
	enum Kind { VESSEL = 0, BASE_SURFACE = 1, BASE_BUILDING = 2 };

	SceneSsr (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~SceneSsr ();
	void Capture (VkSurf *target); // after the TAA resolve, before the late labels: what this frame's glass and next frame's opaque scene trace

	static SceneSsr *Of (Scene *scn); // the scene's reflections when they work, else NULL
	static void PlanetBind (Scene *scn, vPlanet *vp, ShaderClass *sh, bool depthRec); // Surfmgr2: the Earth shader's gSsr and textures, every Render call

	// gSsr and the two textures of the legacy effect (PbrRender copies them) while one vessel or base draws; "off" again after
	class Scope {
	public:
		Scope (Scene *scn, OBJHANDLE body, int kind, bool internal);
		~Scope ();
	private:
		SceneSsr *s = nullptr;
	};

private:
	struct Body { OBJHANDLE h = NULL; PostMath::Mat3 R; PostMath::Vec3 o; };
	struct View {
		bool valid = false, terrain = false;
		DWORD frame = 0;
		PostMath::Vec3 cam, camZ;
		PostMath::Mat4 VP;            // view times the unjittered projection
		float jx = 0.0f, jy = 0.0f;   // the depth's jitter in pixels
		float e = 1.0f;               // exposure of the colour
		double moved = 0.0;           // camera move since the frame before, proxy frame
		OBJHANDLE proxy = NULL;
		std::vector<Body> bodies;     // the proxy first, then the visible vessels
	};
	void Prepare ();                  // the frame's first request: the reset rules
	const Body *NowOf (OBJHANDLE h);
	void Params (OBJHANDLE body, int kind, float skyW, bool planet, SsrPrm &p);
	bool Pass (const char *entry, VkTex *in, VkSurf *out, float level);

	oapi::D3D9Client *gc;
	Scene *scn;
	VkDev *dev;
	DWORD W, H;
	bool ok = false;
	int L = 1;                        // pyramid levels
	ImageProcessing *pIP = nullptr;   // HiZ.glsl: PSHiZ0, PSHiZDown, PSUnfog
	VkTex *tCol = nullptr;            // colour, RGBA16F, full chain
	VkTex *tHiZ = nullptr, *tTmp = nullptr, *tFogTmp = nullptr;
	VkSurf *sCol = nullptr, *sFogTmp = nullptr;
	std::vector<VkSurf*> sHiZ, sTmp;
	std::map<VkTex*, VkSurf*> srcSurf; // the TAA outputs as blit sources
	VkFxHandle hSsr = nullptr, hCol = nullptr, hHiZ = nullptr;

	View last;                        // the latest capture
	DWORD prepFrame = 0;
	bool active = false, prevOk = false;
	double movedNow = 0.0;
	OBJHANDLE lastFocus = NULL, lastTarget = NULL;
	bool lastInternal = false;
	std::vector<Body> now;            // bodies this frame, cached
	DWORD terrainFrame = 0;
	bool terrainRec = false;          // the proxy terrain went into this frame's depth
	bool logged = false;
};

#endif // !__SCENESSR_H
