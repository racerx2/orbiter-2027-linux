// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: whole-scene sun shadows, camera-centred cascades in one atlas (design-scene-shadows, Rev 2)

#ifndef __SCENESHADOWS_H
#define __SCENESHADOWS_H

#include "D3D9Util.h"
#include "CsmMath.h"
#include <set>
#include <chrono>

class Scene;
class vPlanet;
class vObject;
class ShaderClass;
namespace oapi { class D3D9Client; }

// the receivers' CsmPrm (SceneShadow.glsl): row-major, scalar layout
struct CsmGpu {
	D3DXMATRIX mC[CsmMath::NT];		// camera-relative world to atlas u, v and depth: the cascades, then the target tiles
	D3DXVECTOR4 vC[CsmMath::NT];	// texel m, depth range m, select radius m, band start m; a target tile: texel, depth range, on, 0
	D3DXVECTOR4 vO[CsmMath::NT];	// world texel offset of the tile (xy), terrain receiver bias m (z)
	D3DXVECTOR4 vL;		// toward the sun, 2 tan of its angular radius
	D3DXVECTOR4 vF;		// fade start, fade end, noise phase, on
};
static_assert(sizeof(CsmGpu) == 608, "CsmGpu must match CsmPrm in SceneShadow.glsl");

class SceneShadows {
public:
	SceneShadows (oapi::D3D9Client *gc, Scene *scn, DWORD w, DWORD h);
	~SceneShadows ();
	void BeforeMain ();		// main pass, before the main target is pushed: the cascades
	void AfterResolve ();	// after the TAA resolve: the noise fixed on the ground
	void BeforeCockpit ();	// after the exterior glass, before the cockpit interior: receivers off

	static SceneShadows *Get () { return inst; }	// the module, while it exists
	static bool Enabled ();							// Config->SceneShadows and the device can: shader macros and branches
	static void Probe (VkDev *dev);					// the sampled D32 check, before any shader compiles

	bool Ok () const { return ok; }
	bool ReceiversOn (const vPlanet *vp) const;		// this pass draws vp with the cascades
	const CsmGpu *Block (bool on) const { return on ? &gpu : &off; }
	VkTex *Atlas () const { return tAtlas; }

	// caster side, while a cascade is drawn
	int Cascade () const { return cur; }
	LPD3DXMATRIX CasterVP () const { return const_cast<D3DXMATRIX *>(&mCaster); }
	bool Cull (const D3DXVECTOR3 &c, float rad) const;	// a caster sphere (camera-relative) reaches this cascade's box and is not below half a texel
	double TerrainCut () const { return cur >= 0 && cur < CsmMath::NC ? CsmMath::TerrainCut[cur] : 0.0; }
	ShaderClass *TerrainShader () const { return pTerrain; }
	ShaderClass *TreeShader () const { return pTrees; }
	const D3DXVECTOR3 &ToSun () const { return toSun; }	// camera-relative world, the frozen light direction

	// a stencil shadow the cascades replace: caster sphere c, rad (camera-relative) at alt over the ground
	bool Covers (const D3DXVECTOR3 &c, float rad, double alt) const;
	bool Covered (const vObject *v) const { return recv && stencilOff.count (v) > 0; }	// this frame's main pass: the cascades draw v's ground shadow

private:
	struct Tile {
		bool valid = false;
		CsmMath::M3 B;		// light frame it was drawn in
		CsmMath::Box box;	// snapped box
		CsmMath::V3 q;		// the camera in that light space when drawn
	};
	bool Gate (vPlanet *vp);
	bool Frame (vPlanet *vp, bool force, int *draw, int &nd);
	struct TgtSet;
	void Target (const CsmMath::M3 &Rp, const CsmMath::V3 &cp, const VECTOR3 &P, const VECTOR3 &C);
	void Seed (TgtSet &s, class vVessel *tv, double prevT, const CsmMath::M3 &Rp, const CsmMath::V3 &cp, const VECTOR3 &P, const VECTOR3 &C);
	bool InTarget (const vObject *v) const;
	void Draw (const int *draw, int ndraw);
	void Fill ();
	void Push ();
	void Stencils ();
	void TimeMark (int k);
	void TimeRead ();

	oapi::D3D9Client *gc;
	Scene *scn;
	VkDev *dev = nullptr;
	DWORD W, H;
	bool ok = false;
	VkTex *tAtlas = nullptr;
	VkSurf *sAtlas = nullptr;
	ShaderClass *pTerrain = nullptr, *pTrees = nullptr;
	VkFxHandle hCsm = nullptr, hCsmMap = nullptr;
	CsmGpu gpu, off;
	D3DXMATRIX mCaster;
	int cur = -1;
	bool active = false, recv = false;	// the cascades this frame; receivers between BeforeMain and BeforeCockpit
	unsigned frame = 0;
	OBJHANDLE hProxy = nullptr;
	vPlanet *vProxy = nullptr;
	bool haveRef = false, alt = false;
	CsmMath::V3 Lref = { 0.0, 0.0, 0.0 };
	CsmMath::M3 Bref;
	Tile tile[CsmMath::NC];
	CsmMath::Xf xf[CsmMath::NC];		// this frame's transforms of each tile
	D3DXVECTOR3 toSun = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
	double sinSun = 1.0;				// sun elevation at the camera's ground point
	float tan2w = 0.0093f;
	std::set<const vObject *> stencilOff;	// vessels this module took the stencil shadow from

	// the target tiles: the camera's target vessel, then the nearest other one within C0's reach, each with those touching it, at their own texel (as the legacy focus map), left out of the cascades
	static const int MaxTgt = 8;
	struct TgtSet {
		bool on = false;
		int n = 0;
		class vVessel *v[MaxTgt] = {};
		CsmMath::Target f = {};
		CsmMath::Xf xf = {};
	};
	TgtSet tg[CsmMath::NTgt];

	// a few timing lines in the log after activation, nothing per frame
	VkQueryPool qp = VK_NULL_HANDLE;
	double tickNs = 0.0;
	unsigned qFrame = 0, nTimed = 0, nTiles = 0, nCpu = 0, reports = 0;
	bool pend[4] = { false, false, false, false }, marked = false;
	double sumCast = 0.0, sumMain = 0.0, sumCpu = 0.0;

	static SceneShadows *inst;
	static bool probeOk;
};

#endif // !__SCENESHADOWS_H
