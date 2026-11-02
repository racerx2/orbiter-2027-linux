// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ray-traced GI's probe volume: schedule, trace and update (design-ddgi 4, 5; Rev 2)
// after Majercik et al. 2019 (DDGI) and 2021 (probe states, relocation, variable hysteresis)

#ifndef __DDGIVOLUME_H
#define __DDGIVOLUME_H

#include "D3D9Util.h"
#include "VkShader.h"
#include "DdgiMath.h"
#include <vector>

class DdgiScene;
class vPlanet;
class Scene;
struct ConstParams;

// a vertex and pixel shader pair outside the effects: its own constants, and set 1 when it traces rays
class DdgiPass {
public:
	DdgiPass (VkDev *dev, const char *file, const char *vs, const char *ps, const char *macros, bool rt);
	~DdgiPass ();
	bool OK () const { return vs && fs; }
	void Set (const char *name, const void *data, UINT bytes);
	void Tex (const char *name, VkTex *t, bool linear, bool mips = false);
	void Draw (UINT verts, VkDescriptorSet set = VK_NULL_HANDLE); // state, targets and viewport are the caller's
private:
	VkConstHandle Find (const char *name) const;
	VkDev *dev;
	bool rt;
	VkShaderEXT vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
	VkConstTable tv, tf;
	VkConstBuffer cb;
	std::vector<VkSamplerSlot> samp;		// not slots: a Qt keyword macro
	std::string name;
};

// DdgiProbe.glsl DdgiPrm (binding 3, scalar layout)
struct DdgiGpuPrm {
	int org[6][4];			// cascade origin, cells from the anchor (xyz)
	int off[6][4];			// the anchor's slot offset per cascade (xyz)
	float ax[4], ay[4], az[4];	// anchor axes, global frame (xyz)
	float cam[4];			// camera, anchor frame (xyz)
	float ctr[4];			// body centre, anchor frame (xyz), radius (w)
	float geo[4];			// anchor from the body centre, global axes (xyz)
	float sun[4];			// to the sun, anchor frame (xyz), sun up (w)
	float rot[3][4];		// this frame's ray rotation (rows)
	float prm[4];			// hysteresis, ray reach [m], distance cap [cells], probes scheduled
	float alb[4];			// terrain albedo pow and scale (NewPlanet's), far ground set (z)
	float lpos[8][4];		// local lights: position, anchor frame
	float ldir[8][4];		// direction (xyz), spot (w)
	float ldif[8][4];		// colour
	float latt[8][4];		// attenuation
	float lprm[8][4];		// range, falloff, theta, phi
	int nl[4];				// lights, probes per ray row, block side of the pass, entries for the state pass
};

// GiPS fields of SSGI.glsl the probe shaders read (SunAt, TerrAmb, MeshAmb)
struct DdgiGiPS {
	D3DXVECTOR4 fade, curve, ambc, misc;
};

class DdgiVolume {
public:
	static const int K = 16;			// probes per row of the ray texture
	static const int NMIN = 256, NMAX = 8192; // probes scheduled per frame

	explicit DdgiVolume (VkDev *dev, const char *ppf);
	~DdgiVolume ();
	bool OK () const { return ok; }

	// schedule, trace and update; outside a rendering pass, after DdgiScene::Build
	bool Update (DdgiScene &scene, Scene *scn, vPlanet *vp, ConstParams *cp, const DdgiGiPS &gps);

	const DdgiGpuPrm &Prm () const { return prm; }
	VkTex *Irr () const { return tIrr; }
	VkTex *Dst () const { return tDst; }
	VkTex *State () const { return tState; }
	bool Valid () const { return valid; }	// the probes hold something

private:
	void Schedule (const DdgiScene &scene);
	void Lights (const DdgiScene &scene, Scene *scn);
	void Timing ();
	void Bind (DdgiPass *p, DdgiScene &scene, ConstParams *cp, vPlanet *vp, const DdgiGiPS &gps);
	void Target (VkSurf *s, UINT w, UINT h);

	VkDev *dev;
	bool ok = false, valid = false;
	DdgiGpuPrm prm = {};
	VkTex *tIrr = nullptr, *tDst = nullptr, *tState = nullptr, *tStateP = nullptr, *tRay = nullptr, *tAlpha = nullptr;
	VkSurf *sIrr = nullptr, *sDst = nullptr, *sState = nullptr, *sStateP = nullptr, *sRay = nullptr, *sAlpha = nullptr;
	VkBuf *sched = nullptr;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorSet set = VK_NULL_HANDLE;
	VkAccelerationStructureKHR setTlas = VK_NULL_HANDLE;
	DdgiPass *pTrace = nullptr, *pAlpha = nullptr, *pState = nullptr, *pIrrU = nullptr, *pDstU = nullptr;
	VkQueryPool timer = VK_NULL_HANDLE;
	double tickNs = 1.0;
	uint64_t uses = 0;
	bool timed[3] = {};
	int budget = 1024;
	float rayMs = 0.25f;
	// schedule
	bool have = false;
	DdgiMath::I3 org[DdgiMath::NCASC];		// cascade origins, absolute cells
	int cursor[DdgiMath::NCASC] = {};
	int cycle[DdgiMath::NCASC] = {};
	std::vector<uint8_t> fresh;				// per probe: starts over at its next update
	std::vector<float> lastSun;				// per probe: the sun at its last update
	std::vector<uint32_t> list;				// this frame's entries
	int nSched = 0, nClear = 0;
	unsigned seed = 0;
	VECTOR3 sunNow = { 0, 0, 0 };
};

#endif // !__DDGIVOLUME_H
