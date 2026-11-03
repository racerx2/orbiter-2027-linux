// WineBridge: the vessel Orbiter sees; its callbacks go to the Windows module's object when that module overrides them
#include "WbVessel.h"
#include "Bridge.h"
#include "Manual.h"
#include "DrawAPI.h"
#if __has_include ("lua_remote.h")
extern "C" {
#include "lua_remote.h"
}
#else
#define lua_isremote(L) false // an Orbiter whose lua.so has no remote states
#endif
#include <cstring>
#include <unistd.h>

using Guard = std::lock_guard<std::recursive_mutex>;

WbVessel::WbVessel (OBJHANDLE hVessel, int fmodel) : VESSEL4 (hVessel, fmodel), m_fmodel (fmodel) {}

WbVessel::WbVessel (OBJHANDLE hVessel, VESSEL *own, const uint32_t *classMask)
	: VESSEL4 (hVessel, own->GetFlightModel ()), m_fmodel (own->GetFlightModel ()), m_on (true), m_own (own), m_cmask (classMask) {}

WbVessel::~WbVessel ()
{
	if (m_own) return; // a Linux vessel's: nothing of its own in the helper
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (m_on && b.Alive ()) {
		WbWriter &w = b.Begin (H_OVCEXIT);
		w.i32 (m_module); w.u32 (m_id);
		b.Call ();
	}
	WbSoundVesselGone (GetHandle ()); // after the module's ovcExit, which may still stop its waves
	if (m_id) b.DropVessel (m_id);
	b.Retire (GetHandle ()); // not upstream (F2): its id names nothing now
	b.Retire (this);
	if (m_module) b.Free (m_module); // the vessel's open of its module
}

// WineModule in the class cfg, found where Orbiter finds it: Config/Vessels, then Config
std::string WbVessel::WineModule ()
{
	const char *cls = GetClassName ();
	if (!cls || !*cls) return "";
	for (const char *dir : {"Vessels/", ""}) {
		std::string f = std::string (dir) + cls + ".cfg";
		FILEHANDLE h = oapiOpenFile (f.c_str (), FILE_IN_ZEROONFAIL, CONFIG);
		if (!h) continue;
		char buf[1024] = "";
		bool ok = oapiReadItem_string (h, "WineModule", buf);
		oapiCloseFile (h, FILE_IN_ZEROONFAIL);
		return ok ? buf : "";
	}
	return "";
}

void WbVessel::Attach ()
{
	const char *cls = GetClassName ();
	std::string dll = WineModule ();
	if (dll.empty ()) {
		oapiWriteLogV ("WineBridge: vessel class %s has no WineModule line in its cfg", cls ? cls : "(none)");
		return;
	}
	Attach (dll);
}

void WbVessel::Attach (const std::string &dll)
{
	std::string path = oapiResolvePath (dll.c_str ()); // '\' separators and case as on disk
	if (path.empty () || path[0] != '/') {
		char cwd[4096];
		if (getcwd (cwd, sizeof cwd)) path = std::string (cwd) + "/" + path;
	}
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	m_module = b.Load (path);
	if (!m_module) return;
	m_id = b.Id (GetHandle ());
	b.AddVessel (m_id, this);
	WbWriter &w = b.Begin (H_OVCINIT);
	w.i32 (m_module); w.u32 (m_id); w.i32 (m_fmodel);
	WbReader r = b.Call ();
	int ok = r.i32 (), version = r.i32 ();
	m_mask[0] = r.u32 (); m_mask[1] = r.u32 ();
	if (r.bad || !ok) {
		b.DropVessel (m_id);
		m_id = 0;
		if (b.Alive ()) oapiWriteLogV ("WineBridge: %s: ovcInit made no vessel for %s", path.c_str (), GetName ());
		return;
	}
	m_on = true;
	m_version = version;
	if (version < 1) this->version = 0; // a plain VESSEL: Orbiter calls none of its callbacks, as on Windows (1 and 2 stay 3: C3)
}

bool WbVessel::Over (int c)
{
	WbBridge &b = WbBridge::Get ();
	if (!m_on || !b.Alive ()) return false;
	if (!m_own) return WbOverridden (m_mask, c);
	if (!WbOverridden (m_cmask, c) || !WbHookProxy (GetHandle ())) return false;
	m_id = b.Id (GetHandle ()); // the vessel's object in the helper, whose class vtable the module patched
	return true;
}

WbWriter &WbVessel::Begin (int c)
{
	WbWriter &w = WbBridge::Get ().Begin (H_CLBK);
	w.u32 (m_id); w.u32 ((uint32_t)c);
	return w;
}

// ---------------------------------------------------------------------
// what the helper's calls need kept

static void AirfoilFunc (VESSEL *, double aoa, double M, double Re, void *context, double *cl, double *cm, double *cd)
{
	const WbVessel::AirfoilContext *a = (const WbVessel::AirfoilContext *)context;
	*cl = *cm = *cd = 0;
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!b.Alive ()) return;
	WbWriter &w = b.Begin (H_AIRFOIL);
	w.u32 (a->func); w.u32 (a->context); w.u32 (a->vessel); w.i32 (a->kind); w.f64 (aoa); w.f64 (M); w.f64 (Re);
	WbReader r = b.Call ();
	double l = r.f64 (), m = r.f64 (), d = r.f64 ();
	if (!r.bad) { *cl = l; *cm = m; *cd = d; }
}

static void AirfoilFunc2 (VESSEL *, VECTOR3 vinf, double aoa, double beta, double gamma, double M, double Re, void *context,
	double *CA, double *CN, double *CY, double *Cl, double *Cm, double *Cn)
{
	const WbVessel::AirfoilContext *a = (const WbVessel::AirfoilContext *)context;
	double *out[6] = {CA, CN, CY, Cl, Cm, Cn};
	for (double *o : out) *o = 0;
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!b.Alive ()) return;
	WbWriter &w = b.Begin (H_AIRFOIL);
	w.u32 (a->func); w.u32 (a->context); w.u32 (a->vessel); w.i32 (A_Ex2); w.v3 (vinf.data);
	w.f64 (aoa); w.f64 (beta); w.f64 (gamma); w.f64 (M); w.f64 (Re);
	WbReader r = b.Call ();
	double k[6];
	for (double &v : k) v = r.f64 ();
	if (!r.bad) for (int i = 0; i < 6; i++) *out[i] = k[i];
}

AIRFOILHANDLE WbVessel::Airfoil (AIRFOIL_ORIENTATION align, const VECTOR3 &ref, uint32_t func, uint32_t context, double c, double S, double A, int kind)
{
	m_airfoils.push_back ({m_id, func, context, kind});
	return CreateAirfoil3 (align, ref, AirfoilFunc, &m_airfoils.back (), c, S, A);
}

AIRFOILHANDLE WbVessel::Airfoil4 (const VECTOR3 &ref, uint32_t func, uint32_t context, double c, double S, double A)
{
	m_airfoils.push_back ({m_id, func, context, A_Ex2});
	return CreateAirfoil4 (ref, AirfoilFunc2, &m_airfoils.back (), c, S, A);
}

WbVessel::AirfoilContext *WbVessel::AirfoilRecord (AIRFOILHANDLE h)
{
	AirfoilCoeffFunc cf = nullptr;
	void *cx = nullptr;
	if (!h || !GetAirfoilParam (h, nullptr, &cf, &cx, nullptr, nullptr, nullptr)) return nullptr;
	auto fn = [] (auto f) { return reinterpret_cast<void (*) ()> (f); }; // any function pointer, compared as one
	if (fn (cf) != fn (AirfoilFunc) && fn (cf) != fn (AirfoilFunc2)) return nullptr;
	for (AirfoilContext &a : m_airfoils) if (&a == cx) return &a;
	return nullptr;
}

void *WbVessel::PanelContext (uint32_t ctx)
{
	if (!ctx) return nullptr;
	for (uint32_t &c : m_panelCtx) if (c == ctx) return &c;
	m_panelCtx.push_back (ctx);
	return &m_panelCtx.back ();
}

uint32_t WbVessel::PanelContextOut (void *context)
{
	for (uint32_t &c : m_panelCtx) if (&c == context) return c;
	return WbBridge::Get ().Id (context);
}

ANIMATIONCOMPONENT_HANDLE WbVessel::Component (UINT anim, double state0, double state1, const Transform *t, ANIMATIONCOMPONENT_HANDLE parent)
{
	if (!t) return AddAnimationComponent (anim, state0, state1, nullptr, parent);
	std::vector<UINT> &grp = m_groups.emplace_back (t->grp);
	if (t->list && grp.empty ()) grp.push_back (0); // an empty list stays a list: no group moves, not the whole mesh
	UINT *g = t->list ? grp.data () : nullptr;
	UINT n = t->ngrp;
	MGROUP_TRANSFORM *tr;
	switch (t->type) {
	case MGROUP_TRANSFORM::ROTATE: tr = &m_rotate.emplace_back (t->mesh, g, n, t->ref, t->axis, t->angle); break;
	case MGROUP_TRANSFORM::TRANSLATE: tr = &m_translate.emplace_back (t->mesh, g, n, t->shift); break;
	case MGROUP_TRANSFORM::SCALE: tr = &m_scale.emplace_back (t->mesh, g, n, t->ref, t->scale); break;
	default: tr = &m_null.emplace_back (t->mesh, g, n); break;
	}
	return AddAnimationComponent (anim, state0, state1, tr, parent);
}

PSTREAM_HANDLE WbVessel::Stream (THRUSTER_HANDLE th, const VECTOR3 &pos, const PARTICLESTREAMSPEC *pss)
{
	if (!pss) return AddExhaustStream (th, pos, nullptr);
	m_streams.push_back (*pss);
	return AddExhaustStream (th, pos, &m_streams.back ());
}

// ---------------------------------------------------------------------
// callbacks: to the Windows module's object if it overrides them, else Orbiter's default

void WbVessel::clbkSetClassCaps (FILEHANDLE cfg)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_SetClassCaps)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkSetClassCaps (cfg) : VESSEL4::clbkSetClassCaps (cfg);
	Begin (C_SetClassCaps).u32 (b.Id (cfg));
	b.Call ();
}

void WbVessel::clbkSaveState (FILEHANDLE scn)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_SaveState)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkSaveState (scn) : VESSEL4::clbkSaveState (scn);
	Begin (C_SaveState).u32 (b.Id (scn));
	b.Call ();
}

void WbVessel::clbkLoadStateEx (FILEHANDLE scn, void *status)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_LoadStateEx)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkLoadStateEx (scn, status) : VESSEL4::clbkLoadStateEx (scn, status);
	WbWriter &w = Begin (C_LoadStateEx);
	w.u32 (b.Id (scn)); WbPutStatus (w, status);
	WbReader r = b.Call ();
	WbGetStatus (r, status); // what the module's lines made of Orbiter's status
}

void WbVessel::clbkSetStateEx (const void *status)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_SetStateEx)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkSetStateEx (status) : VESSEL4::clbkSetStateEx (status);
	WbPutStatus (Begin (C_SetStateEx), status);
	b.Call ();
}

void WbVessel::clbkPostCreation ()
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_PostCreation)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkPostCreation () : VESSEL4::clbkPostCreation ();
	Begin (C_PostCreation);
	b.Call ();
}

void WbVessel::clbkFocusChanged (bool getfocus, OBJHANDLE hNewVessel, OBJHANDLE hOldVessel)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_FocusChanged)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkFocusChanged (getfocus, hNewVessel, hOldVessel) : VESSEL4::clbkFocusChanged (getfocus, hNewVessel, hOldVessel);
	WbWriter &w = Begin (C_FocusChanged);
	w.b (getfocus); w.u32 (b.Id (hNewVessel)); w.u32 (b.Id (hOldVessel));
	b.Call ();
}

void WbVessel::clbkPreStep (double simt, double simdt, double mjd)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (m_on && !m_own) b.Frame (simt);
	if (!Over (C_PreStep)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkPreStep (simt, simdt, mjd) : VESSEL4::clbkPreStep (simt, simdt, mjd);
	WbWriter &w = Begin (C_PreStep);
	w.f64 (simt); w.f64 (simdt); w.f64 (mjd);
	b.Call ();
}

void WbVessel::clbkPostStep (double simt, double simdt, double mjd)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_PostStep)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkPostStep (simt, simdt, mjd) : VESSEL4::clbkPostStep (simt, simdt, mjd);
	WbWriter &w = Begin (C_PostStep);
	w.f64 (simt); w.f64 (simdt); w.f64 (mjd);
	b.Call ();
}

bool WbVessel::clbkPlaybackEvent (double simt, double event_t, const char *event_type, const char *event)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_PlaybackEvent)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkPlaybackEvent (simt, event_t, event_type, event) : VESSEL4::clbkPlaybackEvent (simt, event_t, event_type, event);
	WbWriter &w = Begin (C_PlaybackEvent);
	w.f64 (simt); w.f64 (event_t); w.str (event_type); w.str (event);
	WbReader r = b.Call ();
	bool res = r.b ();
	return !r.bad && res;
}

void WbVessel::clbkRCSMode (int mode)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_RCSMode)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkRCSMode (mode) : VESSEL4::clbkRCSMode (mode);
	Begin (C_RCSMode).i32 (mode);
	b.Call ();
}

void WbVessel::clbkADCtrlMode (DWORD mode)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_ADCtrlMode)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkADCtrlMode (mode) : VESSEL4::clbkADCtrlMode (mode);
	Begin (C_ADCtrlMode).u32 (mode);
	b.Call ();
}

void WbVessel::clbkHUDMode (int mode)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_HUDMode)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkHUDMode (mode) : VESSEL4::clbkHUDMode (mode);
	Begin (C_HUDMode).i32 (mode);
	b.Call ();
}

void WbVessel::clbkMFDMode (int mfd, int mode)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_MFDMode)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkMFDMode (mfd, mode) : VESSEL4::clbkMFDMode (mfd, mode);
	WbWriter &w = Begin (C_MFDMode);
	w.i32 (mfd); w.i32 (mode);
	b.Call ();
}

void WbVessel::clbkNavMode (int mode, bool active)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_NavMode)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkNavMode (mode, active) : VESSEL4::clbkNavMode (mode, active);
	WbWriter &w = Begin (C_NavMode);
	w.i32 (mode); w.b (active);
	b.Call ();
}

void WbVessel::clbkDockEvent (int dock, OBJHANDLE mate)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_DockEvent)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkDockEvent (dock, mate) : VESSEL4::clbkDockEvent (dock, mate);
	WbWriter &w = Begin (C_DockEvent);
	w.i32 (dock); w.u32 (b.Id (mate));
	b.Call ();
}

void WbVessel::clbkAnimate (double simt)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_Animate)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkAnimate (simt) : VESSEL4::clbkAnimate (simt);
	Begin (C_Animate).f64 (simt);
	b.Call ();
}

int WbVessel::clbkConsumeDirectKey (char *kstate)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_ConsumeDirectKey)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkConsumeDirectKey (kstate) : VESSEL4::clbkConsumeDirectKey (kstate);
	Begin (C_ConsumeDirectKey).raw (kstate, 256);
	WbReader r = b.Call ();
	int res = r.i32 ();
	char ks[256];
	r.raw (ks, 256);
	if (r.bad) return 0;
	memcpy (kstate, ks, 256); // the module may have reset keys it used
	return res;
}

int WbVessel::clbkConsumeBufferedKey (DWORD key, bool down, char *kstate)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_ConsumeBufferedKey)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkConsumeBufferedKey (key, down, kstate) : VESSEL4::clbkConsumeBufferedKey (key, down, kstate);
	WbWriter &w = Begin (C_ConsumeBufferedKey);
	w.u32 (key); w.b (down); w.raw (kstate, 256);
	WbReader r = b.Call ();
	int res = r.i32 ();
	return r.bad ? 0 : res;
}

int WbVessel::clbkNavProcess (int mode)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_NavProcess)) return m_own ? static_cast<VESSEL4 *> (m_own)->clbkNavProcess (mode) : VESSEL4::clbkNavProcess (mode);
	Begin (C_NavProcess).i32 (mode);
	WbReader r = b.Call ();
	int res = r.i32 ();
	return r.bad ? mode : res;
}

int WbVessel::clbkGeneric (int msgid, int prm, void *context)
{
	WbBridge &b = WbBridge::Get ();
	bool lua = msgid == VMSG_LUAINTERPRETER || msgid == VMSG_LUAINSTANCE; // context: Linux's state, which the module gets (Lua.cpp)
	if (lua && context && lua_isremote ((lua_State *)context)) { // a helper state a native module passed on: not bridged back
		static bool said;
		if (!said) { said = true; oapiWriteLog ((char *)"WineBridge: a Windows module's interpreter state passed to a Windows vessel by a native module: not bridged"); }
		return 0;
	}
	int res = 0;
	bool err = false;
	{
		Guard g (b.Lock ());
		if (!Over (C_Generic)) return m_own ? static_cast<VESSEL3 *> (m_own)->clbkGeneric (msgid, prm, context) : VESSEL4::clbkGeneric (msgid, prm, context);
		if (lua) WbLuaFlush ();
		WbWriter &w = Begin (C_Generic);
		w.i32 (msgid); w.i32 (prm); w.u32 (lua ? WbLuaStateId ((lua_State *)context) : b.Id (context));
		WbReader r = b.Call ();
		res = r.i32 ();
		err = r.b ();
		if (r.bad) res = 0, err = false;
	}
	if (lua && err) WbLuaRaise ((lua_State *)context); // the module's Lua error, after the lock: it unwinds past this call, as on Windows
	return res;
}

void WbVessel::clbkGetRadiationForce (const VECTOR3 &mflux, VECTOR3 &F, VECTOR3 &pos)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_GetRadiationForce)) return m_own ? static_cast<VESSEL3 *> (m_own)->clbkGetRadiationForce (mflux, F, pos) : VESSEL4::clbkGetRadiationForce (mflux, F, pos);
	WbWriter &w = Begin (C_GetRadiationForce);
	w.v3 (mflux.data); w.v3 (F.data); w.v3 (pos.data);
	WbReader r = b.Call ();
	VECTOR3 f, p;
	r.v3 (f.data); r.v3 (p.data);
	if (!r.bad) { F = f; pos = p; }
}

// ---------------------------------------------------------------------
// visuals, cockpits, panels, VC and HUD

static bool Res (WbReader r) { bool v = r.b (); return !r.bad && v; }

void WbVessel::clbkVisualCreated (VISHANDLE vis, int refcount)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_VisualCreated)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkVisualCreated (vis, refcount) : VESSEL4::clbkVisualCreated (vis, refcount);
	WbWriter &w = Begin (C_VisualCreated);
	w.u32 (b.Id (vis)); w.i32 (refcount);
	b.Call ();
}

void WbVessel::clbkVisualDestroyed (VISHANDLE vis, int refcount)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_VisualDestroyed)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkVisualDestroyed (vis, refcount) : VESSEL4::clbkVisualDestroyed (vis, refcount);
	WbWriter &w = Begin (C_VisualDestroyed);
	w.u32 (b.Id (vis)); w.i32 (refcount);
	b.Call ();
}

bool WbVessel::clbkLoadGenericCockpit ()
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_LoadGenericCockpit)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkLoadGenericCockpit () : VESSEL4::clbkLoadGenericCockpit ();
	Begin (C_LoadGenericCockpit);
	return Res (b.Call ());
}

bool WbVessel::clbkLoadPanel (int id)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_LoadPanel)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkLoadPanel (id) : VESSEL4::clbkLoadPanel (id);
	Begin (C_LoadPanel).i32 (id);
	return Res (b.Call ());
}

bool WbVessel::clbkPanelMouseEvent (int id, int event, int mx, int my)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_PanelMouseEvent2)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkPanelMouseEvent (id, event, mx, my) : VESSEL2::clbkPanelMouseEvent (id, event, mx, my); // VESSEL3's overload hides it
	WbWriter &w = Begin (C_PanelMouseEvent2);
	w.i32 (id); w.i32 (event); w.i32 (mx); w.i32 (my);
	return Res (b.Call ());
}

bool WbVessel::clbkPanelRedrawEvent (int id, int event, SURFHANDLE surf)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_PanelRedrawEvent2)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkPanelRedrawEvent (id, event, surf) : VESSEL2::clbkPanelRedrawEvent (id, event, surf);
	WbWriter &w = Begin (C_PanelRedrawEvent2);
	w.i32 (id); w.i32 (event); w.u32 (b.Id (surf));
	return Res (b.Call ());
}

bool WbVessel::clbkLoadVC (int id)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_LoadVC)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkLoadVC (id) : VESSEL4::clbkLoadVC (id);
	Begin (C_LoadVC).i32 (id);
	return Res (b.Call ());
}

bool WbVessel::clbkVCMouseEvent (int id, int event, VECTOR3 &p)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_VCMouseEvent)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkVCMouseEvent (id, event, p) : VESSEL4::clbkVCMouseEvent (id, event, p);
	WbWriter &w = Begin (C_VCMouseEvent);
	w.i32 (id); w.i32 (event); w.v3 (p.data);
	WbReader r = b.Call ();
	bool res = r.b ();
	VECTOR3 q;
	r.v3 (q.data);
	if (r.bad) return false;
	p = q;
	return res;
}

bool WbVessel::clbkVCRedrawEvent (int id, int event, SURFHANDLE surf)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_VCRedrawEvent)) return m_own ? static_cast<VESSEL2 *> (m_own)->clbkVCRedrawEvent (id, event, surf) : VESSEL4::clbkVCRedrawEvent (id, event, surf);
	WbWriter &w = Begin (C_VCRedrawEvent);
	w.i32 (id); w.i32 (event); w.u32 (b.Id (surf));
	return Res (b.Call ());
}

bool WbVessel::clbkPanelMouseEvent (int id, int event, int mx, int my, void *context)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_PanelMouseEvent3)) return m_own ? static_cast<VESSEL3 *> (m_own)->clbkPanelMouseEvent (id, event, mx, my, context) : VESSEL4::clbkPanelMouseEvent (id, event, mx, my, context);
	WbWriter &w = Begin (C_PanelMouseEvent3);
	w.i32 (id); w.i32 (event); w.i32 (mx); w.i32 (my); w.u32 (PanelContextOut (context));
	return Res (b.Call ());
}

bool WbVessel::clbkPanelRedrawEvent (int id, int event, SURFHANDLE surf, void *context)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_PanelRedrawEvent3)) return m_own ? static_cast<VESSEL3 *> (m_own)->clbkPanelRedrawEvent (id, event, surf, context) : VESSEL4::clbkPanelRedrawEvent (id, event, surf, context);
	WbWriter &w = Begin (C_PanelRedrawEvent3);
	w.i32 (id); w.i32 (event); w.u32 (b.Id (surf)); w.u32 (PanelContextOut (context));
	return Res (b.Call ());
}

bool WbVessel::clbkLoadPanel2D (int id, PANELHANDLE hPanel, DWORD viewW, DWORD viewH)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_LoadPanel2D)) return m_own ? static_cast<VESSEL3 *> (m_own)->clbkLoadPanel2D (id, hPanel, viewW, viewH) : VESSEL4::clbkLoadPanel2D (id, hPanel, viewW, viewH);
	WbWriter &w = Begin (C_LoadPanel2D);
	w.i32 (id); w.u32 (b.Id (hPanel)); w.u32 (viewW); w.u32 (viewH);
	return Res (b.Call ());
}

bool WbVessel::clbkDrawHUD (int mode, const HUDPAINTSPEC *hps, oapi::Sketchpad *skp)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	int c = Over (C_DrawHUD3) ? C_DrawHUD3 : !m_own && m_version < 2 && Over (C_DrawHUD2) ? C_DrawHUD2 : -1; // a VESSEL2 module: its HDC HUD
	if (c < 0) return m_own ? static_cast<VESSEL3 *> (m_own)->clbkDrawHUD (mode, hps, skp) : VESSEL4::clbkDrawHUD (mode, hps, skp);
	WbSkpBegin (skp); // the module draws on Orbiter's sketchpad during the call
	WbWriter &w = Begin (c);
	w.i32 (mode); WbPutHps (w, hps); w.u32 (b.Id (skp ? skp->GetSurface () : nullptr)); w.u32 (b.Id (skp));
	bool res = Res (b.Call ());
	WbSkpEnd (skp);
	return res;
}

void WbVessel::clbkRenderHUD (int mode, const HUDPAINTSPEC *hps, SURFHANDLE hDefaultTex)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!Over (C_RenderHUD)) return m_own ? static_cast<VESSEL3 *> (m_own)->clbkRenderHUD (mode, hps, hDefaultTex) : VESSEL4::clbkRenderHUD (mode, hps, hDefaultTex);
	WbWriter &w = Begin (C_RenderHUD);
	w.i32 (mode); WbPutHps (w, hps); w.u32 (b.Id (hDefaultTex));
	b.Call ();
}
