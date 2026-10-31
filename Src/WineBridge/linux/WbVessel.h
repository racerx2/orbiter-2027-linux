// WineBridge: the vessel Orbiter sees; its callbacks go to the Windows module's object when that module overrides them. Also,
// for a Linux vessel whose class a Windows module hooked, the object Orbiter calls its callbacks on (Hooks.cpp): the hooked
// ones go to the vessel's object in the helper, the others to the vessel's own
#pragma once
#include "Orbitersdk.h"
#include "../proto/wb_proto.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class WbVessel final : public VESSEL4 {
public:
	WbVessel (OBJHANDLE hVessel, int fmodel);
	WbVessel (OBJHANDLE hVessel, VESSEL *own, const uint32_t *classMask); // a Linux vessel's, with its class's hooked callbacks
	~WbVessel ();
	void Attach (); // the Windows module named by the class cfg; on failure this stays a vessel with no module
	void Attach (const std::string &dll); // that Windows module (opened for this vessel, freed with it)
	void SetMask (uint32_t m0, uint32_t m1) { m_mask[0] = m0; m_mask[1] = m1; } // its class's vtable changed (H_HOOKS)

	// the helper's calls that need storage kept as long as the vessel
	struct AirfoilContext { uint32_t vessel, func, context; int kind; }; // kind: WbAirfoilKind, the module function's
	AIRFOILHANDLE Airfoil (AIRFOIL_ORIENTATION align, const VECTOR3 &ref, uint32_t func, uint32_t context, double c, double S, double A, int kind = A_Ex);
	AIRFOILHANDLE Airfoil4 (const VECTOR3 &ref, uint32_t func, uint32_t context, double c, double S, double A); // force and moment
	AirfoilContext *AirfoilRecord (AIRFOILHANDLE h); // the record of one of this vessel's airfoils made by the bridge; null if not
	void *PanelContext (uint32_t ctx);       // what Orbiter keeps as a panel area's context for the module's context ctx
	uint32_t PanelContextOut (void *context); // ... and the module's context for Orbiter's, or Orbiter's pointer as an id
	struct Transform {
		int type = MGROUP_TRANSFORM::NULLTRANSFORM;
		UINT mesh = 0;
		bool list = false; // the module's grp wasn't null
		UINT ngrp = 0;
		std::vector<UINT> grp;
		VECTOR3 ref {}, axis {}, shift {}, scale {};
		float angle = 0;
	};
	ANIMATIONCOMPONENT_HANDLE Component (UINT anim, double state0, double state1, const Transform *t, ANIMATIONCOMPONENT_HANDLE parent);
	PSTREAM_HANDLE Stream (THRUSTER_HANDLE th, const VECTOR3 &pos, const PARTICLESTREAMSPEC *pss);

	// Orbiter's own defaults, for a Windows module calling its base class
	void BaseSaveState (FILEHANDLE scn) { VESSEL4::clbkSaveState (scn); }
	void BaseLoadStateEx (FILEHANDLE scn, void *status) { VESSEL4::clbkLoadStateEx (scn, status); }
	void BaseSetStateEx (const void *status) { VESSEL4::clbkSetStateEx (status); }
	bool BaseLoadGenericCockpit () { return VESSEL4::clbkLoadGenericCockpit (); }
	void BaseGetRadiationForce (const VECTOR3 &mflux, VECTOR3 &F, VECTOR3 &pos) { VESSEL4::clbkGetRadiationForce (mflux, F, pos); }
	bool BaseDrawHUD3 (int mode, const HUDPAINTSPEC *hps, oapi::Sketchpad *skp) { return VESSEL4::clbkDrawHUD (mode, hps, skp); }
	void BaseRenderHUD (int mode, const HUDPAINTSPEC *hps, SURFHANDLE tex) { VESSEL4::clbkRenderHUD (mode, hps, tex); }

	using VESSEL4::clbkDrawHUD; // the QPainter one stays Orbiter's: Orbiter calls it for no VESSEL3
	void clbkVisualCreated (VISHANDLE vis, int refcount) override;
	void clbkVisualDestroyed (VISHANDLE vis, int refcount) override;
	bool clbkLoadGenericCockpit () override;
	bool clbkLoadPanel (int id) override;
	bool clbkPanelMouseEvent (int id, int event, int mx, int my) override;
	bool clbkPanelRedrawEvent (int id, int event, SURFHANDLE surf) override;
	bool clbkLoadVC (int id) override;
	bool clbkVCMouseEvent (int id, int event, VECTOR3 &p) override;
	bool clbkVCRedrawEvent (int id, int event, SURFHANDLE surf) override;
	bool clbkPanelMouseEvent (int id, int event, int mx, int my, void *context) override;
	bool clbkPanelRedrawEvent (int id, int event, SURFHANDLE surf, void *context) override;
	bool clbkLoadPanel2D (int id, PANELHANDLE hPanel, DWORD viewW, DWORD viewH) override;
	bool clbkDrawHUD (int mode, const HUDPAINTSPEC *hps, oapi::Sketchpad *skp) override;
	void clbkRenderHUD (int mode, const HUDPAINTSPEC *hps, SURFHANDLE hDefaultTex) override;

	void clbkSetClassCaps (FILEHANDLE cfg) override;
	void clbkSaveState (FILEHANDLE scn) override;
	void clbkLoadStateEx (FILEHANDLE scn, void *status) override;
	void clbkSetStateEx (const void *status) override;
	void clbkPostCreation () override;
	void clbkFocusChanged (bool getfocus, OBJHANDLE hNewVessel, OBJHANDLE hOldVessel) override;
	void clbkPreStep (double simt, double simdt, double mjd) override;
	void clbkPostStep (double simt, double simdt, double mjd) override;
	bool clbkPlaybackEvent (double simt, double event_t, const char *event_type, const char *event) override;
	void clbkRCSMode (int mode) override;
	void clbkADCtrlMode (DWORD mode) override;
	void clbkHUDMode (int mode) override;
	void clbkMFDMode (int mfd, int mode) override;
	void clbkNavMode (int mode, bool active) override;
	void clbkDockEvent (int dock, OBJHANDLE mate) override;
	void clbkAnimate (double simt) override;
	int clbkConsumeDirectKey (char *kstate) override;
	int clbkConsumeBufferedKey (DWORD key, bool down, char *kstate) override;
	int clbkGeneric (int msgid, int prm, void *context) override;
	int clbkNavProcess (int mode) override;
	void clbkGetRadiationForce (const VECTOR3 &mflux, VECTOR3 &F, VECTOR3 &pos) override;

private:
	bool Over (int c);
	WbWriter &Begin (int c);
	std::string WineModule ();

	int m_fmodel;
	uint32_t m_id = 0; // the vessel's id on the channel
	int m_module = 0;
	bool m_on = false;
	int m_version = 3; // the module object's interface version
	uint32_t m_mask[2] = {0, 0};
	VESSEL *m_own = nullptr;            // a Linux vessel's own object (hooks); null for a bridged vessel
	const uint32_t *m_cmask = nullptr;  // ... its class's hooked callbacks
	std::deque<AirfoilContext> m_airfoils;
	std::deque<uint32_t> m_panelCtx; // the module's panel area contexts, one each; Orbiter keeps their addresses
	std::deque<std::vector<UINT>> m_groups;
	std::deque<MGROUP_TRANSFORM> m_null;
	std::deque<MGROUP_ROTATE> m_rotate;
	std::deque<MGROUP_TRANSLATE> m_translate;
	std::deque<MGROUP_SCALE> m_scale;
	std::deque<PARTICLESTREAMSPEC> m_streams;
};
