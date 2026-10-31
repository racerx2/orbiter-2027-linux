// WineBridge helper: the Orbiter 2010 API that Windows modules import from Orbiter.exe.
// Signatures come from the 2010 SDK headers, so the exported names are the ones the DLLs ask for.
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdio.h>
#include "wb_helper.h"
#include "compat2010.h"

#define VID (wb::Id (vessel))

static void V3 (WbWriter &w, const VECTOR3 &v) { w.v3 (v.data); }

// ---------------------------------------------------------------------
// constructors: as Orbiter, they only store their arguments; vessel is the Linux side's vessel id

VESSEL::VESSEL (OBJHANDLE hVessel, int fmodel) { vessel = (Vessel *)hVessel; flightmodel = (short)fmodel; version = 0; }
VESSEL2::VESSEL2 (OBJHANDLE hVessel, int fmodel) : VESSEL (hVessel, fmodel) { version = 1; }
VESSEL3::VESSEL3 (OBJHANDLE hVessel, int fmodel) : VESSEL2 (hVessel, fmodel) { version = 2; }
VESSEL4::VESSEL4 (OBJHANDLE hVessel, int fmodel) : VESSEL3 (hVessel, fmodel) { version = 3; }

// ---------------------------------------------------------------------
// VESSEL

AIRFOILHANDLE VESSEL::CreateAirfoil3 (AIRFOIL_ORIENTATION align, const VECTOR3 &ref, AirfoilCoeffFuncEx cf, void *context, double c, double S, double A) const
{
	WbWriter &w = wb::Begin (L_CreateAirfoil3);
	w.u32 (VID); w.i32 (align); V3 (w, ref); w.u32 (wb::Id ((const void *)cf)); w.u32 (wb::Id (context)); w.f64 (c); w.f64 (S); w.f64 (A);
	WbReader r = wb::Call ();
	return (AIRFOILHANDLE)wb::Ptr (r.u32 ());
}

// CreateAirfoil's old kind of function with a handle; CreateAirfoil4's force and moment function (2024)
AIRFOILHANDLE VESSEL::CreateAirfoil2 (AIRFOIL_ORIENTATION align, const VECTOR3 &ref, AirfoilCoeffFunc cf, double c, double S, double A) const
{
	WbWriter &w = wb::Begin (L_CreateAirfoil2);
	w.u32 (VID); w.i32 (align); V3 (w, ref); w.u32 (wb::Id ((const void *)cf)); w.f64 (c); w.f64 (S); w.f64 (A);
	WbReader r = wb::Call ();
	return (AIRFOILHANDLE)wb::Ptr (r.u32 ());
}

AIRFOILHANDLE VESSEL::CreateAirfoil4 (const VECTOR3 &ref, AirfoilCoeffFuncEx2 cf, void *context, double c, double S, double A) const
{
	WbWriter &w = wb::Begin (L_CreateAirfoil4);
	w.u32 (VID); V3 (w, ref); w.u32 (wb::Id ((const void *)cf)); w.u32 (wb::Id (context)); w.f64 (c); w.f64 (S); w.f64 (A);
	WbReader r = wb::Call ();
	return (AIRFOILHANDLE)wb::Ptr (r.u32 ());
}

// flag 0x02 sets the module's function, called as the airfoil's kind
void VESSEL::EditAirfoil (AIRFOILHANDLE hAirfoil, DWORD flag, const VECTOR3 &ref, AirfoilCoeffFunc cf, double c, double S, double A) const
{
	WbWriter &w = wb::Begin (L_EditAirfoil);
	w.u32 (VID); w.u32 (wb::Id (hAirfoil)); w.u32 (flag); V3 (w, ref); w.u32 (wb::Id ((const void *)cf)); w.f64 (c); w.f64 (S); w.f64 (A);
	wb::Call ();
}

bool VESSEL::GetAirfoilParam (AIRFOILHANDLE hAirfoil, VECTOR3 *ref, AirfoilCoeffFunc *cf, void **context, double *c, double *S, double *A) const
{
	WbWriter &w = wb::Begin (L_GetAirfoilParam); w.u32 (VID); w.u32 (wb::Id (hAirfoil));
	WbReader r = wb::Call ();
	bool ok = r.b ();
	VECTOR3 v;
	r.v3 (v.data);
	uint32_t f = r.u32 (), cx = r.u32 ();
	double cc = r.f64 (), ss = r.f64 (), aa = r.f64 ();
	if (!ok || r.bad) return false;
	if (ref) *ref = v;
	if (cf) *cf = (AirfoilCoeffFunc)wb::Ptr (f);
	if (context) *context = wb::Ptr (cx);
	if (c) *c = cc;
	if (S) *S = ss;
	if (A) *A = aa;
	return true;
}

ANIMATIONCOMPONENT_HANDLE VESSEL::AddAnimationComponent (UINT anim, double state0, double state1, MGROUP_TRANSFORM *trans, ANIMATIONCOMPONENT_HANDLE parent) const
{
	bool local = trans && trans->mesh == LOCALVERTEXLIST; // grp points at the module's own vertices, which Orbiter moves
	static bool said;
	if (local && !said) { said = true; wb::Log ("an animation of a local vertex list is not bridged yet; it stays still"); } // before Begin: a call of its own
	WbWriter &w = wb::Begin (L_AddAnimationComponent);
	w.u32 (VID); w.u32 (anim); w.f64 (state0); w.f64 (state1); w.u32 (wb::Id (parent));
	w.b (trans != 0);
	if (local) { // a node only, so its children still attach
		w.i32 (MGROUP_TRANSFORM::NULLTRANSFORM); w.u32 (0); w.b (false); w.u32 (0);
	} else if (trans) {
		MGROUP_TRANSFORM::TYPE t = trans->Type (); // the DLL's own class
		w.i32 (t);
		w.u32 (trans->mesh);
		w.b (trans->grp != 0); w.u32 (trans->ngrp); // a null list moves the whole mesh, whatever ngrp says
		for (UINT i = 0; trans->grp && i < trans->ngrp; i++) w.u32 (trans->grp[i]);
		switch (t) {
		case MGROUP_TRANSFORM::ROTATE: {
			MGROUP_ROTATE *m = static_cast<MGROUP_ROTATE *> (trans);
			V3 (w, m->ref); V3 (w, m->axis); w.f64 (m->angle);
			break; }
		case MGROUP_TRANSFORM::TRANSLATE: {
			MGROUP_TRANSLATE *m = static_cast<MGROUP_TRANSLATE *> (trans);
			V3 (w, m->shift);
			break; }
		case MGROUP_TRANSFORM::SCALE: {
			MGROUP_SCALE *m = static_cast<MGROUP_SCALE *> (trans);
			V3 (w, m->ref); V3 (w, m->scale);
			break; }
		default:
			break;
		}
	}
	WbReader r = wb::Call ();
	ANIMATIONCOMPONENT_HANDLE h = (ANIMATIONCOMPONENT_HANDLE)wb::Ptr (r.u32 ());
	if (h && trans && !local) wb::TransformMade (VID, h, trans);
	return h;
}

THGROUP_HANDLE VESSEL::CreateThrusterGroup (THRUSTER_HANDLE *th, int nth, THGROUP_TYPE thgt) const
{
	WbWriter &w = wb::Begin (L_CreateThrusterGroup);
	w.u32 (VID); w.i32 (thgt); w.i32 (th ? nth : 0);
	for (int i = 0; th && i < nth; i++) w.u32 (wb::Id (th[i]));
	WbReader r = wb::Call ();
	return (THGROUP_HANDLE)wb::Ptr (r.u32 ());
}

PSTREAM_HANDLE VESSEL::AddExhaustStream (THRUSTER_HANDLE th, const VECTOR3 &pos, PARTICLESTREAMSPEC *pss) const
{
	WbWriter &w = wb::Begin (L_AddExhaustStream2);
	w.u32 (VID); w.u32 (wb::Id (th)); V3 (w, pos); w.b (pss != 0);
	if (pss) {
		w.u32 (pss->flags); w.f64 (pss->srcsize); w.f64 (pss->srcrate); w.f64 (pss->v0); w.f64 (pss->srcspread);
		w.f64 (pss->lifetime); w.f64 (pss->growthrate); w.f64 (pss->atmslowdown); w.i32 (pss->ltype); w.i32 (pss->levelmap);
		w.f64 (pss->lmin); w.f64 (pss->lmax); w.i32 (pss->atmsmap); w.f64 (pss->amin); w.f64 (pss->amax); w.u32 (wb::Id (pss->tex));
	}
	WbReader r = wb::Call ();
	return (PSTREAM_HANDLE)wb::Ptr (r.u32 ());
}

// ---------------------------------------------------------------------
// VESSEL2 and VESSEL3 defaults, as Orbiter's: the ones that do something call the Linux side's own default

void VESSEL2::clbkSetClassCaps (FILEHANDLE) {}
void VESSEL2::clbkSaveState (FILEHANDLE scn)
{
	WbWriter &w = wb::Begin (L_BaseSaveState); w.u32 (VID); w.u32 (wb::Id (scn)); wb::Call ();
}
void VESSEL2::clbkLoadStateEx (FILEHANDLE scn, void *status) // Orbiter's parser reads the rest of the vessel's lines into status
{
	WbWriter &w = wb::Begin (L_BaseLoadStateEx); w.u32 (VID); w.u32 (wb::Id (scn)); wb::PutStatus (w, status);
	WbReader r = wb::Call ();
	wb::GetStatus (r, status);
}
void VESSEL2::clbkSetStateEx (const void *status)
{
	WbWriter &w = wb::Begin (L_BaseSetStateEx); w.u32 (VID); wb::PutStatus (w, status); wb::Call ();
}
void VESSEL2::clbkPostCreation () {}
void VESSEL2::clbkFocusChanged (bool, OBJHANDLE, OBJHANDLE) {}
void VESSEL2::clbkPreStep (double, double, double) {}
void VESSEL2::clbkPostStep (double, double, double) {}
bool VESSEL2::clbkPlaybackEvent (double, double, const char *, const char *) { return false; }
void VESSEL2::clbkVisualCreated (VISHANDLE, int) {}
void VESSEL2::clbkVisualDestroyed (VISHANDLE, int) {}
void VESSEL2::clbkDrawHUD (int mode, const HUDPAINTSPEC *hps, HDC hDC) // Orbiter's default HUD, when hDC is the HUD's (hud.cpp)
{
	wb::HudDefault (VID, mode, hps, hDC);
}
void VESSEL2::clbkRCSMode (int) {}
void VESSEL2::clbkADCtrlMode (DWORD) {}
void VESSEL2::clbkHUDMode (int) {}
void VESSEL2::clbkMFDMode (int, int) {}
void VESSEL2::clbkNavMode (int, bool) {}
void VESSEL2::clbkDockEvent (int, OBJHANDLE) {}
void VESSEL2::clbkAnimate (double) {}
int VESSEL2::clbkConsumeDirectKey (char *) { return 0; }
int VESSEL2::clbkConsumeBufferedKey (DWORD, bool, char *) { return 0; }
bool VESSEL2::clbkLoadGenericCockpit ()
{
	WbWriter &w = wb::Begin (L_BaseLoadGenericCockpit); w.u32 (VID);
	WbReader r = wb::Call ();
	return r.b ();
}
bool VESSEL2::clbkLoadPanel (int) { return false; }
bool VESSEL2::clbkPanelMouseEvent (int, int, int, int) { return false; }
bool VESSEL2::clbkPanelRedrawEvent (int, int, SURFHANDLE) { return false; }
bool VESSEL2::clbkLoadVC (int) { return false; }
bool VESSEL2::clbkVCMouseEvent (int, int, VECTOR3 &) { return false; }
bool VESSEL2::clbkVCRedrawEvent (int, int, SURFHANDLE) { return false; }
bool VESSEL3::clbkPanelMouseEvent (int, int, int, int, void *) { return false; }
bool VESSEL3::clbkPanelRedrawEvent (int, int, SURFHANDLE, void *) { return false; }
int VESSEL3::clbkGeneric (int, int, void *) { return 0; }
int VESSEL4::clbkNavProcess (int mode) { return mode; }

// the vessel's own MFD modes (mfd.cpp)
int VESSEL4::RegisterMFDMode (const MFDMODESPECEX &spec)
{
	return wb::RegisterMode (spec.name, spec.key, spec.context, (const void *)spec.msgproc, VID);
}

bool VESSEL4::UnregisterMFDMode (int mode)
{
	return wb::UnregisterMode (mode, VID);
}

int VESSEL3::RegisterPanelMFDGeometry (PANELHANDLE hPanel, int MFD_id, int nmesh, int ngroup)
{
	WbWriter &w = wb::Begin (L_RegisterPanelMFDGeometry);
	w.u32 (VID); w.u32 (wb::Id (hPanel)); w.i32 (MFD_id); w.i32 (nmesh); w.i32 (ngroup);
	WbReader r = wb::Call ();
	return r.i32 ();
}
bool VESSEL3::clbkLoadPanel2D (int, PANELHANDLE, DWORD, DWORD) { return false; }

// 2D panels: Orbiter copies the surfaces; an area's context comes back in the panel callbacks
int VESSEL3::SetPanelBackground (PANELHANDLE hPanel, SURFHANDLE *hSurf, DWORD nsurf, MESHHANDLE hMesh, DWORD width, DWORD height, DWORD baseline, DWORD scrollflag)
{
	wb::MeshUse (hMesh);
	WbWriter &w = wb::Begin (L_SetPanelBackground);
	w.u32 (VID); w.u32 (wb::Id (hPanel)); w.u32 (hSurf ? nsurf : 0);
	for (DWORD i = 0; hSurf && i < nsurf; i++) w.u32 (wb::Id (hSurf[i]));
	w.u32 (wb::Id (hMesh)); w.u32 (width); w.u32 (height); w.u32 (baseline); w.u32 (scrollflag);
	WbReader r = wb::Call ();
	return r.i32 ();
}

int VESSEL3::SetPanelScaling (PANELHANDLE hPanel, double defscale, double extscale)
{
	WbWriter &w = wb::Begin (L_SetPanelScaling); w.u32 (VID); w.u32 (wb::Id (hPanel)); w.f64 (defscale); w.f64 (extscale);
	WbReader r = wb::Call ();
	return r.i32 ();
}

static void Rect (WbWriter &w, const RECT &r) { w.i32 (r.left); w.i32 (r.top); w.i32 (r.right); w.i32 (r.bottom); }

static WbWriter &Area (uint32_t vid, PANELHANDLE hPanel, int id, const RECT &pos, int form)
{
	WbWriter &w = wb::Begin (L_RegisterPanelArea); w.u32 (vid); w.u32 (wb::Id (hPanel)); w.i32 (id); Rect (w, pos); w.i32 (form);
	return w;
}

int VESSEL3::RegisterPanelArea (PANELHANDLE hPanel, int id, const RECT &pos, const RECT &texpos, int draw_event, int mouse_event, int bkmode)
{
	WbWriter &w = Area (VID, hPanel, id, pos, PA_TexRect);
	Rect (w, texpos); w.i32 (draw_event); w.i32 (mouse_event); w.i32 (bkmode);
	WbReader r = wb::Call ();
	return r.i32 ();
}

int VESSEL3::RegisterPanelArea (PANELHANDLE hPanel, int id, const RECT &pos, int draw_event, int mouse_event, SURFHANDLE surf, void *context)
{
	WbWriter &w = Area (VID, hPanel, id, pos, PA_Surface);
	w.i32 (draw_event); w.i32 (mouse_event); w.u32 (wb::Id (surf)); w.u32 (wb::Id (context));
	WbReader r = wb::Call ();
	return r.i32 ();
}

int VESSEL4::RegisterPanelArea (PANELHANDLE hPanel, int id, const RECT &pos, int texidx, const RECT &texpos, int draw_event, int mouse_event, int bkmode)
{
	WbWriter &w = Area (VID, hPanel, id, pos, PA_TexIdx);
	w.i32 (texidx); Rect (w, texpos); w.i32 (draw_event); w.i32 (mouse_event); w.i32 (bkmode);
	WbReader r = wb::Call ();
	return r.i32 ();
}
void wb::PutHps (WbWriter &w, const void *hps)
{
	HUDPAINTSPEC z;
	memset (&z, 0, sizeof z);
	const HUDPAINTSPEC *h = hps ? (const HUDPAINTSPEC *)hps : &z;
	w.i32 (h->W); w.i32 (h->H); w.i32 (h->CX); w.i32 (h->CY); w.f64 (h->Scale); w.i32 (h->Markersize);
}
bool VESSEL3::clbkDrawHUD (int mode, const HUDPAINTSPEC *hps, oapi::Sketchpad *skp) // Orbiter's default HUD, on Orbiter's sketchpad
{
	WbWriter &w = wb::Begin (L_BaseDrawHUD3); w.u32 (VID); w.i32 (mode); wb::PutHps (w, hps); w.u32 (wb::SketchpadId (skp));
	WbReader r = wb::Call ();
	return r.b ();
}
void VESSEL3::clbkRenderHUD (int mode, const HUDPAINTSPEC *hps, SURFHANDLE hDefaultTex)
{
	WbWriter &w = wb::Begin (L_BaseRenderHUD); w.u32 (VID); w.i32 (mode); wb::PutHps (w, hps); w.u32 (wb::Id (hDefaultTex));
	wb::Call ();
}
void VESSEL3::clbkGetRadiationForce (const VECTOR3 &mflux, VECTOR3 &F, VECTOR3 &pos)
{
	WbWriter &w = wb::Begin (L_BaseGetRadiationForce); w.u32 (VID); V3 (w, mflux);
	WbReader r = wb::Call ();
	r.v3 (F.data); r.v3 (pos.data);
}

// ---------------------------------------------------------------------
// oapi functions

// ---------------------------------------------------------------------
// Orbitersdk.lib's DllMain calls these in Orbiter.exe (OrbiterAPI.cpp)

__declspec(dllexport) int Date2Int (char *date)
{
	static const char *mstr[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	char ms[32];
	int day = 0, month, year = 0;
	sscanf (date, "%31s%d%d", ms, &day, &year);
	for (month = 0; month < 12; month++)
		if (!_strnicmp (ms, mstr[month], 3)) break;
	return (year % 100) * 10000 + (month + 1) * 100 + day;
}

// as Orbiter.exe's: the module's ExitModule, or its opcDLLExit
__declspec(dllexport) void ExitLib (HINSTANCE hModule)
{
	typedef void (*DLLEXIT)(HINSTANCE);
	DLLEXIT f = (DLLEXIT)GetProcAddress (hModule, "ExitModule");
	if (!f) f = (DLLEXIT)GetProcAddress (hModule, "opcDLLExit");
	if (f) f (hModule);
}

__declspec(dllexport) void InitLib (HINSTANCE hModule)
{
	char mname[MAX_PATH], *mp = mname;
	GetModuleFileNameA (hModule, mname, sizeof mname);
	for (char *c = mname; *c; c++) if (*c == '\\' || *c == '/') mp = c + 1;
	char *(*mdate)() = (char *(*)())GetProcAddress (hModule, "ModuleDate");
	int (*fversion)() = (int (*)())GetProcAddress (hModule, "GetModuleVersion");
	int api = fversion ? fversion () : 0;
	wb::Log ("Module %s [Build %06d, API %06d] (Windows, through the Wine bridge; %s SDK layouts)", mp, mdate ? Date2Int (mdate ()) : 0, api,
		api < wb::API_2016 ? "2010" : "2016");
	wb::InitLibCalled (hModule);
	typedef void (*DLLINIT)(HINSTANCE);
	DLLINIT f = (DLLINIT)GetProcAddress (hModule, "InitModule");
	if (!f) f = (DLLINIT)GetProcAddress (hModule, "opcDLLInit");
	if (f) f (hModule);
}
