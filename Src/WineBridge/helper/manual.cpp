// WineBridge helper: the Orbiter 2010 functions written by hand (api/manual.txt), drawing aside
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "wb_helper.h"
#include "compat2010.h"
#include <stddef.h>

#define VID (wb::Id (vessel))

static void V3w (WbWriter &w, const VECTOR3 &v) { w.v3 (v.data); }

// the helper's own heap for what it allocates for the modules (Orbiter's new and delete)
void *operator new (size_t n) { return malloc (n ? n : 1); }
void *operator new[] (size_t n) { return malloc (n ? n : 1); }
void operator delete (void *p) noexcept { free (p); }
void operator delete[] (void *p) noexcept { free (p); }
void operator delete (void *p, size_t) noexcept { free (p); }
void operator delete[] (void *p, size_t) noexcept { free (p); }

// ---------------------------------------------------------------------
// names Orbiter returns as pointers that stay valid: one copy per distinct text, kept for the session

static const char *Intern (const char *s, uint32_t n)
{
	struct E { E *next; uint32_t n; char s[1]; };
	static E *tab[1024];
	uint32_t h = 2166136261u;
	for (uint32_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
	E **b = &tab[h & 1023];
	for (E *e = *b; e; e = e->next)
		if (e->n == n && !memcmp (e->s, s, n)) return e->s;
	E *e = (E *)malloc (sizeof (E) + n);
	e->next = *b; e->n = n;
	memcpy (e->s, s, n); e->s[n] = 0;
	*b = e;
	return e->s;
}

static const char *StrResult (WbReader &r)
{
	uint32_t n;
	const char *s = r.str (&n);
	return s ? Intern (s, n) : 0;
}

char *VESSEL::GetName () const
{
	WbWriter &w = wb::Begin (L_GetName); w.u32 (VID);
	WbReader r = wb::Call ();
	return (char *)StrResult (r);
}

char *VESSEL::GetClassName () const
{
	WbWriter &w = wb::Begin (L_GetClassName); w.u32 (VID);
	WbReader r = wb::Call ();
	return (char *)StrResult (r);
}

const char *VESSEL::GetAttachmentId (ATTACHMENTHANDLE attachment) const
{
	WbWriter &w = wb::Begin (L_GetAttachmentId); w.u32 (VID); w.u32 (wb::Id (attachment));
	WbReader r = wb::Call ();
	return StrResult (r);
}

const char *oapiMFDButtonLabel (int mfd, int bt)
{
	WbWriter &w = wb::Begin (L_MFDButtonLabel); w.i32 (mfd); w.i32 (bt);
	WbReader r = wb::Call ();
	return StrResult (r);
}

// ---------------------------------------------------------------------
// VESSELSTATUS2 on the wire (vs2 in wb_proto.h)

void wb::PutStatus (WbWriter &w, const void *p)
{
	const VESSELSTATUS2 &s = *(const VESSELSTATUS2 *)p;
	w.u32 (s.version);
	if (s.version != 2) return;
	w.u32 (s.flag);
	w.u32 (Id (s.rbody)); w.u32 (Id (s.base)); w.i32 (s.port); w.i32 (s.status);
	V3w (w, s.rpos); V3w (w, s.rvel); V3w (w, s.vrot); V3w (w, s.arot);
	w.f64 (s.surf_lng); w.f64 (s.surf_lat); w.f64 (s.surf_hdg); w.u32 (s.xpdr);
	uint32_t nf = (s.flag & VS_FUELLIST) && s.fuel ? s.nfuel : 0;
	w.u32 (nf);
	for (uint32_t i = 0; i < nf; i++) { w.u32 (s.fuel[i].idx); w.f64 (s.fuel[i].level); }
	uint32_t nt = (s.flag & VS_THRUSTLIST) && s.thruster ? s.nthruster : 0;
	w.u32 (nt);
	for (uint32_t i = 0; i < nt; i++) { w.u32 (s.thruster[i].idx); w.f64 (s.thruster[i].level); }
	uint32_t nd = (s.flag & VS_DOCKINFOLIST) && s.dockinfo ? s.ndockinfo : 0;
	w.u32 (nd);
	for (uint32_t i = 0; i < nd; i++) { w.u32 (s.dockinfo[i].idx); w.u32 (s.dockinfo[i].ridx); w.u32 (Id (s.dockinfo[i].rvessel)); }
}

// a list read from the wire replaces the struct's when it differs; as Orbiter's scenario parser, the old one is deleted
template <class T> static void SetList (bool used, DWORD &n, T *&list, const T *v, uint32_t nv)
{
	if (!used) return;
	if (nv == n && (!nv || (list && !memcmp (list, v, nv * sizeof (T))))) return;
	if (n && list) delete []list;
	list = nv ? new T[nv] : 0;
	if (nv) memcpy (list, v, nv * sizeof (T));
	n = nv;
}

void wb::GetStatus (WbReader &r, void *p)
{
	VESSELSTATUS2 &s = *(VESSELSTATUS2 *)p;
	VESSELSTATUS2 t;
	memset (&t, 0, sizeof t);
	t.version = r.u32 ();
	if (r.bad) return;
	if (t.version != 2) { s.version = t.version; return; }
	t.flag = r.u32 ();
	t.rbody = (OBJHANDLE)Ptr (r.u32 ()); t.base = (OBJHANDLE)Ptr (r.u32 ()); t.port = r.i32 (); t.status = r.i32 ();
	r.v3 (t.rpos.data); r.v3 (t.rvel.data); r.v3 (t.vrot.data); r.v3 (t.arot.data);
	t.surf_lng = r.f64 (); t.surf_lat = r.f64 (); t.surf_hdg = r.f64 (); t.xpdr = r.u32 ();
	uint32_t n[3];
	VESSELSTATUS2::FUELSPEC *f = 0;
	VESSELSTATUS2::THRUSTSPEC *th = 0;
	VESSELSTATUS2::DOCKINFOSPEC *d = 0;
	n[0] = r.u32 ();
	if (n[0] > WB_MSG_MAX / 12) r.bad = true;
	if (!r.bad && n[0]) { f = (VESSELSTATUS2::FUELSPEC *)calloc (n[0], sizeof *f); for (uint32_t i = 0; i < n[0]; i++) { f[i].idx = r.u32 (); f[i].level = r.f64 (); } }
	n[1] = r.u32 ();
	if (n[1] > WB_MSG_MAX / 12) r.bad = true;
	if (!r.bad && n[1]) { th = (VESSELSTATUS2::THRUSTSPEC *)calloc (n[1], sizeof *th); for (uint32_t i = 0; i < n[1]; i++) { th[i].idx = r.u32 (); th[i].level = r.f64 (); } }
	n[2] = r.u32 ();
	if (n[2] > WB_MSG_MAX / 12) r.bad = true;
	if (!r.bad && n[2]) { d = (VESSELSTATUS2::DOCKINFOSPEC *)calloc (n[2], sizeof *d); for (uint32_t i = 0; i < n[2]; i++) { d[i].idx = r.u32 (); d[i].ridx = r.u32 (); d[i].rvessel = (OBJHANDLE)Ptr (r.u32 ()); } }
	if (!r.bad) {
		s.version = t.version; s.flag = t.flag;
		s.rbody = t.rbody; s.base = t.base; s.port = t.port; s.status = t.status;
		s.rpos = t.rpos; s.rvel = t.rvel; s.vrot = t.vrot; s.arot = t.arot;
		s.surf_lng = t.surf_lng; s.surf_lat = t.surf_lat; s.surf_hdg = t.surf_hdg; s.xpdr = t.xpdr;
		SetList ((s.flag & VS_FUELLIST) != 0, s.nfuel, s.fuel, f, n[0]);
		SetList ((s.flag & VS_THRUSTLIST) != 0, s.nthruster, s.thruster, th, n[1]);
		SetList ((s.flag & VS_DOCKINFOLIST) != 0, s.ndockinfo, s.dockinfo, d, n[2]);
	}
	free (f); free (th); free (d);
}

void wb::FreeStatus (void *p)
{
	VESSELSTATUS2 &s = *(VESSELSTATUS2 *)p;
	if (s.version != 2) return;
	delete []s.fuel; delete []s.thruster; delete []s.dockinfo;
	s.fuel = 0; s.thruster = 0; s.dockinfo = 0;
	s.nfuel = s.nthruster = s.ndockinfo = 0;
}

// ---------------------------------------------------------------------
// other vessels: a module's own object for a bridged vessel, else a Proxy whose callbacks reach the vessel's own in Orbiter: all
// of them, so a module that hooks one (vessel hooks, hooks.cpp) still reaches the vessel's own through the entry it kept

namespace {
	class Proxy final : public VESSEL4 {
	public:
		Proxy (OBJHANDLE h, int fm, int ver) : VESSEL4 (h, fm) { version = (short)ver; } // the vessel's own interface version
		WbWriter &Clbk (int c) { WbWriter &w = wb::Begin (L_ProxyClbk); w.u32 (VID); w.u32 ((uint32_t)c); return w; }
		static bool Res (WbReader r) { bool v = r.b (); return !r.bad && v; }
		void clbkSetClassCaps (FILEHANDLE cfg) { Clbk (C_SetClassCaps).u32 (wb::Id (cfg)); wb::Call (); }
		void clbkSaveState (FILEHANDLE scn) { Clbk (C_SaveState).u32 (wb::Id (scn)); wb::Call (); }
		void clbkLoadStateEx (FILEHANDLE scn, void *status)
		{
			WbWriter &w = Clbk (C_LoadStateEx); w.u32 (wb::Id (scn)); wb::PutStatus (w, status);
			WbReader r = wb::Call (); wb::GetStatus (r, status);
		}
		void clbkSetStateEx (const void *status) { wb::PutStatus (Clbk (C_SetStateEx), status); wb::Call (); }
		void clbkPostCreation () { Clbk (C_PostCreation); wb::Call (); }
		void clbkFocusChanged (bool g, OBJHANDLE n, OBJHANDLE o) { WbWriter &w = Clbk (C_FocusChanged); w.b (g); w.u32 (wb::Id (n)); w.u32 (wb::Id (o)); wb::Call (); }
		void clbkPreStep (double t, double dt, double mjd) { WbWriter &w = Clbk (C_PreStep); w.f64 (t); w.f64 (dt); w.f64 (mjd); wb::Call (); }
		void clbkPostStep (double t, double dt, double mjd) { WbWriter &w = Clbk (C_PostStep); w.f64 (t); w.f64 (dt); w.f64 (mjd); wb::Call (); }
		bool clbkPlaybackEvent (double t, double et, const char *ty, const char *ev)
		{
			WbWriter &w = Clbk (C_PlaybackEvent); w.f64 (t); w.f64 (et); w.str (ty); w.str (ev);
			WbReader r = wb::Call (); return r.b ();
		}
		void clbkRCSMode (int m) { Clbk (C_RCSMode).i32 (m); wb::Call (); }
		void clbkADCtrlMode (DWORD m) { Clbk (C_ADCtrlMode).u32 (m); wb::Call (); }
		void clbkHUDMode (int m) { Clbk (C_HUDMode).i32 (m); wb::Call (); }
		void clbkMFDMode (int mfd, int m) { WbWriter &w = Clbk (C_MFDMode); w.i32 (mfd); w.i32 (m); wb::Call (); }
		void clbkNavMode (int m, bool a) { WbWriter &w = Clbk (C_NavMode); w.i32 (m); w.b (a); wb::Call (); }
		void clbkDockEvent (int d, OBJHANDLE mate) { WbWriter &w = Clbk (C_DockEvent); w.i32 (d); w.u32 (wb::Id (mate)); wb::Call (); }
		void clbkAnimate (double t) { Clbk (C_Animate).f64 (t); wb::Call (); }
		int clbkConsumeDirectKey (char *ks)
		{
			Clbk (C_ConsumeDirectKey).raw (ks, 256);
			WbReader r = wb::Call (); int res = r.i32 (); char k[256]; r.raw (k, 256);
			if (!r.bad) memcpy (ks, k, 256);
			return res;
		}
		int clbkConsumeBufferedKey (DWORD key, bool down, char *ks)
		{
			WbWriter &w = Clbk (C_ConsumeBufferedKey); w.u32 (key); w.b (down); w.raw (ks, 256);
			WbReader r = wb::Call (); return r.i32 ();
		}
		bool clbkLoadGenericCockpit () { Clbk (C_LoadGenericCockpit); WbReader r = wb::Call (); return r.b (); }
		int clbkGeneric (int msgid, int prm, void *context)
		{
			bool lua = msgid == VMSG_LUAINTERPRETER || msgid == VMSG_LUAINSTANCE;
			uint32_t L = lua ? wb::LuaRemoteId (context) : 0;              // a Linux Lua state
			uint32_t hs = lua && context && !L ? wb::LuaServeState (context) : 0; // the helper's (a Windows module's interpreter's)
			WbWriter &w = Clbk (C_Generic); w.i32 (msgid); w.i32 (prm); w.u32 (L ? L : hs ? hs : wb::Id (context)); w.b (L != 0); w.b (hs != 0);
			WbReader r = wb::Call ();
			int res = r.i32 ();
			if (r.b ()) { // the vessel's Lua error: it unwinds the module's frames as on Windows
				if (hs) wb::LuaServeRaise (context);
				wb::LuaRaise ();
			}
			return res;
		}
		void clbkGetRadiationForce (const VECTOR3 &mflux, VECTOR3 &F, VECTOR3 &pos)
		{
			WbWriter &w = Clbk (C_GetRadiationForce); V3w (w, mflux); V3w (w, F); V3w (w, pos);
			WbReader r = wb::Call (); VECTOR3 f, p; r.v3 (f.data); r.v3 (p.data);
			if (!r.bad) { F = f; pos = p; }
		}
		void clbkVisualCreated (VISHANDLE vis, int rc) { WbWriter &w = Clbk (C_VisualCreated); w.u32 (wb::Id (vis)); w.i32 (rc); wb::Call (); }
		void clbkVisualDestroyed (VISHANDLE vis, int rc) { WbWriter &w = Clbk (C_VisualDestroyed); w.u32 (wb::Id (vis)); w.i32 (rc); wb::Call (); }
		void clbkDrawHUD (int, const HUDPAINTSPEC *, HDC) {} // Orbiter 2024 calls no HUD with a device context
		bool clbkLoadPanel (int id) { Clbk (C_LoadPanel).i32 (id); return Res (wb::Call ()); }
		bool clbkPanelMouseEvent (int id, int ev, int mx, int my)
		{
			WbWriter &w = Clbk (C_PanelMouseEvent2); w.i32 (id); w.i32 (ev); w.i32 (mx); w.i32 (my);
			return Res (wb::Call ());
		}
		bool clbkPanelRedrawEvent (int id, int ev, SURFHANDLE s)
		{
			WbWriter &w = Clbk (C_PanelRedrawEvent2); w.i32 (id); w.i32 (ev); w.u32 (wb::Id (s));
			return Res (wb::Call ());
		}
		bool clbkLoadVC (int id) { Clbk (C_LoadVC).i32 (id); return Res (wb::Call ()); }
		bool clbkVCMouseEvent (int id, int ev, VECTOR3 &p)
		{
			WbWriter &w = Clbk (C_VCMouseEvent); w.i32 (id); w.i32 (ev); V3w (w, p);
			WbReader r = wb::Call (); bool res = r.b (); VECTOR3 q; r.v3 (q.data);
			if (r.bad) return false;
			p = q;
			return res;
		}
		bool clbkVCRedrawEvent (int id, int ev, SURFHANDLE s)
		{
			WbWriter &w = Clbk (C_VCRedrawEvent); w.i32 (id); w.i32 (ev); w.u32 (wb::Id (s));
			return Res (wb::Call ());
		}
		bool clbkPanelMouseEvent (int id, int ev, int mx, int my, void *context)
		{
			WbWriter &w = Clbk (C_PanelMouseEvent3); w.i32 (id); w.i32 (ev); w.i32 (mx); w.i32 (my); w.u32 (wb::Id (context));
			return Res (wb::Call ());
		}
		bool clbkPanelRedrawEvent (int id, int ev, SURFHANDLE s, void *context)
		{
			WbWriter &w = Clbk (C_PanelRedrawEvent3); w.i32 (id); w.i32 (ev); w.u32 (wb::Id (s)); w.u32 (wb::Id (context));
			return Res (wb::Call ());
		}
		bool clbkLoadPanel2D (int id, PANELHANDLE hPanel, DWORD vw, DWORD vh)
		{
			WbWriter &w = Clbk (C_LoadPanel2D); w.i32 (id); w.u32 (wb::Id (hPanel)); w.u32 (vw); w.u32 (vh);
			return Res (wb::Call ());
		}
		bool clbkDrawHUD (int mode, const HUDPAINTSPEC *hps, oapi::Sketchpad *skp)
		{
			WbWriter &w = Clbk (C_DrawHUD3); w.i32 (mode); wb::PutHps (w, hps); w.u32 (wb::SketchpadId (skp));
			return Res (wb::Call ());
		}
		void clbkRenderHUD (int mode, const HUDPAINTSPEC *hps, SURFHANDLE tex)
		{
			WbWriter &w = Clbk (C_RenderHUD); w.i32 (mode); wb::PutHps (w, hps); w.u32 (wb::Id (tex));
			wb::Call ();
		}
		int clbkNavProcess (int mode) { Clbk (C_NavProcess).i32 (mode); WbReader r = wb::Call (); int res = r.i32 (); return r.bad ? mode : res; }
	};

	struct ProxyEntry { uint32_t id; Proxy *p; };
	ProxyEntry *g_proxy;
	uint32_t g_nproxy, g_capproxy;
}

void **wb::ProxyVt ()
{
	static Proxy p ((OBJHANDLE)0, 0, 3);
	return *(void ***)static_cast<VESSEL2 *> (&p);
}

uint32_t wb::ProxyIdOf (const void *v)
{
	for (uint32_t i = 0; i < g_nproxy; i++) if ((const void *)static_cast<VESSEL *> (g_proxy[i].p) == v) return g_proxy[i].id;
	return 0;
}

VESSEL *wb::ProxyOf (uint32_t vid)
{
	for (uint32_t i = 0; i < g_nproxy; i++) if (g_proxy[i].id == vid) return g_proxy[i].p;
	return 0;
}

// the Linux vessel is gone: a vessel made later at its address gets an object of its own
void wb::ProxyGone (uint32_t vid)
{
	for (uint32_t i = 0; i < g_nproxy; i++)
		if (g_proxy[i].id == vid) { delete g_proxy[i].p; g_proxy[i] = g_proxy[--g_nproxy]; return; }
}

VESSEL *oapiGetFocusInterface ()
{
	return oapiGetVesselInterface (oapiGetFocusObject ());
}

// a planet's atmospheric constants: Orbiter's never change, so a copy per planet stands for them
const ATMCONST *oapiGetPlanetAtmConstants (OBJHANDLE hPlanet)
{
	static_assert (sizeof (ATMCONST) == 12 * 8, "ATMCONST: doubles only");
	static struct Entry { uint32_t id; ATMCONST *c; } *tab;
	static uint32_t n, cap;
	uint32_t id = wb::Id (hPlanet);
	for (uint32_t i = 0; i < n; i++) if (tab[i].id == id) return tab[i].c;
	wb::Begin (L_PlanetAtmConstants).u32 (id);
	WbReader r = wb::Call ();
	ATMCONST *c = 0;
	if (r.b ()) { c = (ATMCONST *)malloc (sizeof *c); r.raw (c, sizeof *c); }
	if (r.bad) { free (c); return 0; }
	if (n == cap) { cap = cap ? cap * 2 : 16; tab = (Entry *)realloc (tab, cap * sizeof *tab); }
	tab[n++] = {id, c};
	return c;
}

VESSEL *oapiGetVesselInterface (OBJHANDLE hVessel)
{
	uint32_t id = wb::Id (hVessel);
	if (VESSEL *v = wb::Obj (id)) return v;
	if (VESSEL *v = wb::ProxyOf (id)) return v;
	WbWriter &w = wb::Begin (L_VesselInfo); w.u32 (id);
	WbReader r = wb::Call ();
	bool isv = r.b ();
	int fm = r.i32 (), ver = r.i32 ();
	uint32_t cls = r.u32 ();
	if (!isv || r.bad) return 0;
	if (VESSEL *v = wb::Obj (id)) return v; // made by a module meanwhile
	if (g_nproxy == g_capproxy) { g_capproxy = g_capproxy ? g_capproxy * 2 : 64; g_proxy = (ProxyEntry *)realloc (g_proxy, g_capproxy * sizeof *g_proxy); }
	Proxy *p = new Proxy (hVessel, fm, ver);
	if (void **vt = cls ? wb::ProxyClass (cls) : 0) *(void ***)static_cast<VESSEL2 *> (p) = vt; // its class's vtable
	g_proxy[g_nproxy++] = {id, p};
	return p;
}

// ---------------------------------------------------------------------
// vessel states

void wb::PutVS (WbWriter &w, const void *vs)
{
	const VESSELSTATUS &s = *(const VESSELSTATUS *)vs;
	V3w (w, s.rpos); V3w (w, s.rvel); V3w (w, s.vrot); V3w (w, s.arot);
	w.f64 (s.fuel); w.f64 (s.eng_main); w.f64 (s.eng_hovr);
	w.u32 (wb::Id (s.rbody)); w.u32 (wb::Id (s.base)); w.i32 (s.port); w.i32 (s.status);
	for (int i = 0; i < 10; i++) V3w (w, s.vdata[i]);
	for (int i = 0; i < 10; i++) w.f64 (s.fdata[i]);
	for (int i = 0; i < 10; i++) w.u32 (s.flag[i]);
}

void wb::GetVS (WbReader &r, void *vs)
{
	VESSELSTATUS &s = *(VESSELSTATUS *)vs;
	r.v3 (s.rpos.data); r.v3 (s.rvel.data); r.v3 (s.vrot.data); r.v3 (s.arot.data);
	s.fuel = r.f64 (); s.eng_main = r.f64 (); s.eng_hovr = r.f64 ();
	s.rbody = (OBJHANDLE)wb::Ptr (r.u32 ()); s.base = (OBJHANDLE)wb::Ptr (r.u32 ()); s.port = r.i32 (); s.status = r.i32 ();
	for (int i = 0; i < 10; i++) r.v3 (s.vdata[i].data);
	for (int i = 0; i < 10; i++) s.fdata[i] = r.f64 ();
	for (int i = 0; i < 10; i++) s.flag[i] = r.u32 ();
}

void VESSEL::GetStatus (VESSELSTATUS &status) const
{
	WbWriter &w = wb::Begin (L_GetStatus); w.u32 (VID);
	WbReader r = wb::Call ();
	VESSELSTATUS s;
	r.v3 (s.rpos.data); r.v3 (s.rvel.data); r.v3 (s.vrot.data); r.v3 (s.arot.data);
	s.fuel = r.f64 (); s.eng_main = r.f64 (); s.eng_hovr = r.f64 ();
	s.rbody = (OBJHANDLE)wb::Ptr (r.u32 ()); s.base = (OBJHANDLE)wb::Ptr (r.u32 ()); s.port = r.i32 (); s.status = r.i32 ();
	for (int i = 0; i < 10; i++) r.v3 (s.vdata[i].data);
	for (int i = 0; i < 10; i++) s.fdata[i] = r.f64 ();
	for (int i = 0; i < 10; i++) s.flag[i] = r.u32 ();
	if (!r.bad) status = s;
}

void VESSEL::DefSetState (const VESSELSTATUS *status) const
{
	WbWriter &w = wb::Begin (L_DefSetState); w.u32 (VID); wb::PutVS (w, status);
	wb::Call ();
}

OBJHANDLE oapiCreateVessel (const char *name, const char *classname, const VESSELSTATUS &status)
{
	WbWriter &w = wb::Begin (L_CreateVessel); w.str (name); w.str (classname); wb::PutVS (w, &status);
	WbReader r = wb::Call ();
	return (OBJHANDLE)wb::Ptr (r.u32 ());
}

// as Orbiter's GetState2 (Vesselstatus.cpp): the lists the flags ask for, allocated when the caller gave none
void VESSEL::GetStatusEx (void *status) const
{
	VESSELSTATUS2 *vs = (VESSELSTATUS2 *)status;
	if (vs->version != 2) return; // Orbiter knows no other interface version
	WbWriter &w = wb::Begin (L_GetStatusEx); w.u32 (VID); w.u32 (vs->flag);
	WbReader r = wb::Call ();
	VESSELSTATUS2 t;
	memset (&t, 0, sizeof t);
	t.rbody = (OBJHANDLE)wb::Ptr (r.u32 ()); t.base = (OBJHANDLE)wb::Ptr (r.u32 ()); t.port = r.i32 (); t.status = r.i32 ();
	r.v3 (t.rpos.data); r.v3 (t.rvel.data); r.v3 (t.vrot.data); r.v3 (t.arot.data);
	t.surf_lng = r.f64 (); t.surf_lat = r.f64 (); t.surf_hdg = r.f64 (); t.xpdr = r.u32 ();
	if (r.bad) return;
	vs->rbody = t.rbody; vs->base = t.base; vs->port = t.port;
	vs->rpos = t.rpos; vs->rvel = t.rvel; vs->vrot = t.vrot; vs->arot = t.arot;
	vs->surf_lng = t.surf_lng; vs->surf_lat = t.surf_lat; vs->surf_hdg = t.surf_hdg;
	vs->status = t.status;
	DWORD i, n = r.u32 ();
	if (vs->flag & VS_FUELLIST) {
		vs->nfuel = n;
		if (!vs->fuel) vs->fuel = new VESSELSTATUS2::FUELSPEC[n];
	}
	for (i = 0; i < n; i++) {
		DWORD idx = r.u32 (); double lv = r.f64 ();
		if (vs->flag & VS_FUELLIST) { vs->fuel[i].idx = idx; vs->fuel[i].level = lv; }
	}
	n = r.u32 ();
	if (vs->flag & VS_THRUSTLIST) {
		vs->nthruster = n;
		if (!vs->thruster) vs->thruster = new VESSELSTATUS2::THRUSTSPEC[n];
	}
	for (i = 0; i < n; i++) {
		DWORD idx = r.u32 (); double lv = r.f64 ();
		if (vs->flag & VS_THRUSTLIST) { vs->thruster[i].idx = idx; vs->thruster[i].level = lv; }
	}
	n = r.u32 ();
	if (vs->flag & VS_DOCKINFOLIST) {
		vs->ndockinfo = n;
		if (!vs->dockinfo) vs->dockinfo = new VESSELSTATUS2::DOCKINFOSPEC[n];
	}
	for (i = 0; i < n; i++) {
		DWORD idx = r.u32 (), ridx = r.u32 (); OBJHANDLE rv = (OBJHANDLE)wb::Ptr (r.u32 ());
		if (vs->flag & VS_DOCKINFOLIST) { vs->dockinfo[i].idx = idx; vs->dockinfo[i].ridx = ridx; vs->dockinfo[i].rvessel = rv; }
	}
	vs->xpdr = t.xpdr;
}

void VESSEL::DefSetStateEx (const void *status) const
{
	WbWriter &w = wb::Begin (L_DefSetStateEx); w.u32 (VID); wb::PutStatus (w, status);
	wb::Call ();
}

OBJHANDLE oapiCreateVesselEx (const char *name, const char *classname, const void *status)
{
	WbWriter &w = wb::Begin (L_CreateVesselEx); w.str (name); w.str (classname); wb::PutStatus (w, status);
	WbReader r = wb::Call ();
	return (OBJHANDLE)wb::Ptr (r.u32 ());
}

// ---------------------------------------------------------------------
// scenario lines

// Orbiter parses into the module's status struct; here a copy goes to Orbiter's parser and the result comes back into it
void VESSEL::ParseScenarioLineEx (char *line, void *status) const
{
	if (*(DWORD *)status != 2) return; // as Orbiter: an unknown interface version
	WbWriter &w = wb::Begin (L_ParseScenarioLineEx); w.u32 (VID); w.str (line); wb::PutStatus (w, status);
	WbReader r = wb::Call ();
	wb::GetStatus (r, status);
}

// as Orbiter's: the line is in one buffer for all files, valid until the next line is read
bool oapiReadScenario_nextline (FILEHANDLE scn, char *&line)
{
	static char *buf;
	static uint32_t len;
	WbWriter &w = wb::Begin (L_ReadScenarioNextline); w.u32 (wb::Id (scn));
	WbReader r = wb::Call ();
	bool ok = r.b ();
	uint32_t n;
	const char *s = r.str (&n);
	if (!ok || r.bad || !s) return false;
	if (n + 1 > len) { len = n + 256; buf = (char *)realloc (buf, len); }
	memcpy (buf, s, n);
	buf[n] = 0;
	line = buf;
	return true;
}

// ---------------------------------------------------------------------
// memory Orbiter reads every frame

static void LiveArg (WbWriter &w, uint32_t id)
{
	uint32_t n;
	const uint8_t *v = wb::LiveValue (id, &n);
	w.u32 (id); w.u32 (n); w.raw (v, n);
}

// the items of a call Orbiter didn't take are dropped; the others are tagged with what Orbiter made of them
static void Settle (uint32_t vid, const uint32_t *ids, int n, bool ok, int tagkind, uint32_t tag)
{
	for (int i = 0; i < n; i++) if (ids[i]) wb::LiveTag (ids[i], tagkind, ok ? tag : 0xffffffffu);
	if (!ok) wb::LiveDropTag (vid, tagkind, 0xffffffffu);
}

void VESSEL::AddBeacon (BEACONLIGHTSPEC *bs)
{
	uint32_t id = wb::Live (VID, WB_LIVE_BEACON, bs);
	WbWriter &w = wb::Begin (L_AddBeacon); w.u32 (VID); LiveArg (w, id);
	WbReader r = wb::Call ();
	Settle (VID, &id, 1, r.b (), wb::TAG_BEACON, id); // tagged with itself: DelBeacon finds it by the spec
}

// as Orbiter's AddExhaust: a level, position or direction it keeps a pointer to is live; a constant one is copied; none: the thruster's
static uint32_t ExhaustLive (uint32_t vid, int kind, const void *p, bool constant)
{
	return p && !constant ? wb::Live (vid, kind, p) : 0;
}

static void ExhaustRef (WbWriter &w, uint32_t id, const void *p, uint32_t size)
{
	if (id) { w.i32 (2); LiveArg (w, id); }
	else if (p) { w.i32 (1); w.raw (p, size); }
	else w.i32 (0);
}

UINT VESSEL::AddExhaust (EXHAUSTSPEC *spec)
{
	uint32_t vid = VID;
	uint32_t ids[3] = {
		ExhaustLive (vid, WB_LIVE_DOUBLE, spec->level, (spec->flags & EXHAUST_CONSTANTLEVEL) != 0),
		ExhaustLive (vid, WB_LIVE_VECTOR3, spec->lpos, (spec->flags & EXHAUST_CONSTANTPOS) != 0),
		ExhaustLive (vid, WB_LIVE_VECTOR3, spec->ldir, (spec->flags & EXHAUST_CONSTANTDIR) != 0)};
	WbWriter &w = wb::Begin (L_AddExhaustSpec);
	w.u32 (vid); w.u32 (wb::Id (spec->th));
	w.f64 (spec->lsize); w.f64 (spec->wsize); w.f64 (spec->lofs); w.f64 (spec->modulate);
	w.u32 (wb::Id (spec->tex)); w.u32 (spec->flags);
	ExhaustRef (w, ids[0], spec->level, 8);
	ExhaustRef (w, ids[1], spec->lpos, 24);
	ExhaustRef (w, ids[2], spec->ldir, 24);
	WbReader r = wb::Call ();
	UINT ex = r.u32 ();
	Settle (vid, ids, 3, !r.bad, wb::TAG_EXHAUST, ex);
	return ex;
}

bool VESSEL::DelExhaust (UINT idx) const
{
	WbWriter &w = wb::Begin (L_DelExhaust); w.u32 (VID); w.u32 (idx);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	if (ok) wb::LiveDropTag (VID, wb::TAG_EXHAUST, idx);
	return ok;
}

static void PutPSS (WbWriter &w, const PARTICLESTREAMSPEC *pss)
{
	w.b (pss != 0);
	if (!pss) return;
	w.u32 (pss->flags); w.f64 (pss->srcsize); w.f64 (pss->srcrate); w.f64 (pss->v0); w.f64 (pss->srcspread);
	w.f64 (pss->lifetime); w.f64 (pss->growthrate); w.f64 (pss->atmslowdown); w.i32 (pss->ltype); w.i32 (pss->levelmap);
	w.f64 (pss->lmin); w.f64 (pss->lmax); w.i32 (pss->atmsmap); w.f64 (pss->amin); w.f64 (pss->amax); w.u32 (wb::Id (pss->tex));
}

PSTREAM_HANDLE VESSEL::AddParticleStream (PARTICLESTREAMSPEC *pss, const VECTOR3 &pos, const VECTOR3 &dir, double *lvl) const
{
	uint32_t vid = VID;
	uint32_t id = lvl ? wb::Live (vid, WB_LIVE_DOUBLE, lvl) : 0;
	WbWriter &w = wb::Begin (L_AddParticleStream); w.u32 (vid); PutPSS (w, pss); V3w (w, pos); V3w (w, dir);
	w.b (id != 0);
	if (id) LiveArg (w, id);
	WbReader r = wb::Call ();
	uint32_t h = r.u32 ();
	Settle (vid, &id, 1, h != 0, wb::TAG_STREAM, h);
	return (PSTREAM_HANDLE)wb::Ptr (h);
}

PSTREAM_HANDLE VESSEL::AddReentryStream (PARTICLESTREAMSPEC *pss) const
{
	WbWriter &w = wb::Begin (L_AddReentryStream); w.u32 (VID); PutPSS (w, pss);
	WbReader r = wb::Call ();
	return (PSTREAM_HANDLE)wb::Ptr (r.u32 ());
}

PSTREAM_HANDLE VESSEL::AddExhaustStream (THRUSTER_HANDLE th, PARTICLESTREAMSPEC *pss) const
{
	WbWriter &w = wb::Begin (L_AddExhaustStream1); w.u32 (VID); w.u32 (wb::Id (th)); PutPSS (w, pss);
	WbReader r = wb::Call ();
	return (PSTREAM_HANDLE)wb::Ptr (r.u32 ());
}

bool VESSEL::DelExhaustStream (PSTREAM_HANDLE ch) const
{
	WbWriter &w = wb::Begin (L_DelExhaustStream); w.u32 (VID); w.u32 (wb::Id (ch));
	WbReader r = wb::Call ();
	bool ok = r.b ();
	if (ok) wb::LiveDropTag (VID, wb::TAG_STREAM, wb::Id (ch));
	return ok;
}

void VESSEL::CreateVariableDragElement (double *drag, double factor, const VECTOR3 &ref) const
{
	CreateVariableDragElement ((const double *)drag, factor, ref);
}

void VESSEL::CreateVariableDragElement (const double *drag, double factor, const VECTOR3 &ref) const
{
	uint32_t vid = VID;
	uint32_t id = wb::Live (vid, WB_LIVE_DOUBLE, drag);
	WbWriter &w = wb::Begin (L_CreateVariableDragElement); w.u32 (vid); LiveArg (w, id); w.f64 (factor); V3w (w, ref);
	WbReader r = wb::Call ();
	Settle (vid, &id, 1, r.b (), wb::TAG_NONE, 0);
}

// the buffer Orbiter shows on screen; the module writes it, the text crosses whenever it changed
char *oapiDebugString ()
{
	static char buf[256];
	static bool reg;
	if (!reg) {
		reg = true;
		uint32_t id = wb::Live (0, WB_LIVE_STRING, buf, sizeof buf);
		WbWriter &w = wb::Begin (L_DebugString); LiveArg (w, id);
		wb::Call ();
	}
	return buf;
}

// ---------------------------------------------------------------------
// functions Orbiter calls back (H_AIRFOIL, H_LIFT, H_INPUTBOX in main.cpp)

void VESSEL::CreateAirfoil (AIRFOIL_ORIENTATION align, const VECTOR3 &ref, AirfoilCoeffFunc cf, double c, double S, double A) const
{
	WbWriter &w = wb::Begin (L_CreateAirfoil);
	w.u32 (VID); w.i32 (align); V3w (w, ref); w.u32 (wb::Id ((const void *)cf)); w.f64 (c); w.f64 (S); w.f64 (A);
	wb::Call ();
}

void VESSEL::SetLiftCoeffFunc (LiftCoeffFunc lcf) const
{
	WbWriter &w = wb::Begin (L_SetLiftCoeffFunc); w.u32 (VID); w.u32 (wb::Id ((const void *)lcf));
	wb::Call ();
}

void oapiOpenInputBox (char *title, bool (*Clbk)(void*,char*,void*), char *buf, int vislen, void *usrdata)
{
	WbWriter &w = wb::Begin (L_OpenInputBox);
	wb::TextStr (w, title); w.u32 (wb::Id ((const void *)Clbk)); wb::TextStr (w, buf); w.i32 (vislen); w.u32 (wb::Id (usrdata));
	wb::Call ();
}

// both callbacks: H_INPUTBOX calls the one Orbiter calls
void oapiOpenInputBoxEx (const char *title, bool (*Clbk_enter)(void*,char*,void*), bool (*Clbk_cancel)(void*,char*,void*), char *buf, int vislen, void *usrdata, DWORD flags)
{
	WbWriter &w = wb::Begin (L_OpenInputBoxEx);
	wb::TextStr (w, title); w.u32 (wb::Id ((const void *)Clbk_enter)); w.u32 (wb::Id ((const void *)Clbk_cancel)); wb::TextStr (w, buf);
	w.i32 (vislen); w.u32 (wb::Id (usrdata)); w.u32 (flags);
	wb::Call ();
}

// ---------------------------------------------------------------------
// a module's log lines with a format (oapiWriteLogV, oapiWriteLogError): formatted here, written by Orbiter
void oapiWriteLogV (const char *format, ...)
{
	char buf[1024];
	va_list ap;
	va_start (ap, format);
	_vsnprintf (buf, sizeof buf - 1, format ? format : "", ap);
	va_end (ap);
	buf[sizeof buf - 1] = 0;
	wb::TextStr (wb::Begin (L_WriteLog), buf);
	wb::Call ();
}

void __writeLogError (const char *func, const char *file, int line, const char *format, ...)
{
	char buf[1024];
	va_list ap;
	va_start (ap, format);
	_vsnprintf (buf, sizeof buf - 1, format ? format : "", ap);
	va_end (ap);
	buf[sizeof buf - 1] = 0;
	WbWriter &w = wb::Begin (L_WriteLogError);
	w.str (func); w.str (file); w.i32 (line); wb::TextStr (w, buf);
	wb::Call ();
}

// ---------------------------------------------------------------------
// meshes: oapiMeshGroup, oapiMeshGroupEx and oapiMeshMaterial give the module a copy of Orbiter's; a copy it changed goes back
// before the next call naming that mesh, at the end of the call from Orbiter that fetched it, and once a frame

namespace {
	struct Copy {
		uint32_t mesh, idx;
		bool material;
		bool recent;          // fetched during the call from Orbiter now running
		MESHGROUPEX g;        // the module's group (a MESHGROUP is its first part)
		MATERIAL m;
		MESHGROUPEX g0;       // as last read from or sent to Orbiter
		MATERIAL m0;
		NTVERTEX *v0; WORD *i0; // the arrays as last read or sent
		DWORD nv0, ni0;
	};
	Copy **g_copies;
	uint32_t g_ncopies, g_capcopies;
	bool g_recent;

	Copy *Find (uint32_t mesh, uint32_t idx, bool material, bool make)
	{
		for (uint32_t i = 0; i < g_ncopies; i++)
			if (g_copies[i]->mesh == mesh && g_copies[i]->idx == idx && g_copies[i]->material == material) return g_copies[i];
		if (!make) return 0;
		if (g_ncopies == g_capcopies) { g_capcopies = g_capcopies ? g_capcopies * 2 : 32; g_copies = (Copy **)realloc (g_copies, g_capcopies * sizeof (Copy *)); }
		Copy *c = (Copy *)calloc (1, sizeof (Copy));
		c->mesh = mesh; c->idx = idx; c->material = material;
		return g_copies[g_ncopies++] = c;
	}

	bool HeadChanged (const MESHGROUPEX &a, const MESHGROUPEX &b)
	{
		return a.nVtx != b.nVtx || a.nIdx != b.nIdx || a.MtrlIdx != b.MtrlIdx || a.TexIdx != b.TexIdx || a.UsrFlag != b.UsrFlag
			|| a.zBias != b.zBias || a.Flags != b.Flags || a.TexIdxEx[0] != b.TexIdxEx[0] || a.TexMixEx[0] != b.TexMixEx[0];
	}
	bool VtxChanged (const Copy *c) { return c->g.nVtx != c->nv0 || (c->g.nVtx && (!c->g.Vtx || memcmp (c->g.Vtx, c->v0, c->g.nVtx * sizeof (NTVERTEX)))); }
	bool IdxChanged (const Copy *c) { return c->g.nIdx != c->ni0 || (c->g.nIdx && (!c->g.Idx || memcmp (c->g.Idx, c->i0, c->g.nIdx * sizeof (WORD)))); }

	// the arrays as sent or read become the reference the next change is found against
	void Snap (Copy *c)
	{
		MESHGROUPEX &g = c->g;
		if (g.nVtx != c->nv0) { free (c->v0); c->v0 = (NTVERTEX *)malloc ((g.nVtx ? g.nVtx : 1) * sizeof (NTVERTEX)); c->nv0 = g.nVtx; }
		if (g.nIdx != c->ni0) { free (c->i0); c->i0 = (WORD *)malloc ((g.nIdx ? g.nIdx : 1) * sizeof (WORD)); c->ni0 = g.nIdx; }
		if (g.nVtx && g.Vtx) memcpy (c->v0, g.Vtx, g.nVtx * sizeof (NTVERTEX));
		if (g.nIdx && g.Idx) memcpy (c->i0, g.Idx, g.nIdx * sizeof (WORD));
		c->g0 = g;
	}

	void Store (Copy *c)
	{
		if (c->material) {
			if (!memcmp (&c->m, &c->m0, sizeof c->m)) return;
			WbWriter &w = wb::Begin (L_MeshMaterialStore); w.u32 (c->mesh); w.u32 (c->idx); w.raw (&c->m, sizeof c->m);
			wb::Call ();
			c->m0 = c->m;
			return;
		}
		bool vtx = VtxChanged (c), idx = IdxChanged (c);
		if (!vtx && !idx && !HeadChanged (c->g, c->g0)) return;
		const MESHGROUPEX &g = c->g;
		WbWriter &w = wb::Begin (L_MeshGroupStore); w.u32 (c->mesh); w.u32 (c->idx);
		w.u32 (g.MtrlIdx); w.u32 (g.TexIdx); w.u32 (g.UsrFlag); w.u32 (g.zBias); w.u32 (g.Flags); w.u32 (g.TexIdxEx[0]); w.f64 (g.TexMixEx[0]);
		w.u32 (g.nVtx); w.u32 (g.nIdx);
		w.b (vtx && g.Vtx); if (vtx && g.Vtx) w.raw (g.Vtx, g.nVtx * sizeof (NTVERTEX));
		w.b (idx && g.Idx); if (idx && g.Idx) w.raw (g.Idx, g.nIdx * sizeof (WORD));
		wb::Call ();
		Snap (c);
	}

	// Orbiter's group into the copy; arrays the module holds stay where they are while their sizes do
	void ReadGroup (WbReader &r, Copy *c)
	{
		MESHGROUPEX &g = c->g;
		DWORD mtrl = r.u32 (), tex = r.u32 (), usr = r.u32 (), zb = r.u32 (), fl = r.u32 (), tex1 = r.u32 ();
		float mix = (float)r.f64 ();
		DWORD nv = r.u32 (), ni = r.u32 ();
		if (r.bad || (uint64_t)nv * sizeof (NTVERTEX) + (uint64_t)ni * sizeof (WORD) > WB_MSG_MAX) { r.bad = true; return; }
		if (!g.Vtx || nv != g.nVtx) { g.Vtx = (NTVERTEX *)malloc ((nv ? nv : 1) * sizeof (NTVERTEX)); }
		if (!g.Idx || ni != g.nIdx) { g.Idx = (WORD *)malloc ((ni ? ni : 1) * sizeof (WORD)); }
		r.raw (g.Vtx, nv * sizeof (NTVERTEX));
		r.raw (g.Idx, ni * sizeof (WORD));
		g.nVtx = nv; g.nIdx = ni; g.MtrlIdx = mtrl; g.TexIdx = tex; g.UsrFlag = usr; g.zBias = (WORD)zb; g.Flags = (WORD)fl;
		g.TexIdxEx[0] = tex1; g.TexMixEx[0] = mix;
		Snap (c);
	}

	MESHGROUPEX *Group (MESHHANDLE hMesh, DWORD idx)
	{
		wb::MeshUse (hMesh);
		WbWriter &w = wb::Begin (L_MeshGroup); w.u32 (wb::Id (hMesh)); w.u32 (idx);
		WbReader r = wb::Call ();
		if (!r.b ()) return 0; // as Orbiter: none for a bad index
		Copy *c = Find (wb::Id (hMesh), idx, false, true);
		ReadGroup (r, c);
		if (r.bad) return 0;
		c->recent = g_recent = true;
		return &c->g;
	}
}

MESHGROUP *oapiMeshGroup (MESHHANDLE hMesh, DWORD idx)
{
	return (MESHGROUP *)Group (hMesh, idx);
}

MESHGROUPEX *oapiMeshGroupEx (MESHHANDLE hMesh, DWORD idx)
{
	return Group (hMesh, idx);
}

MATERIAL *oapiMeshMaterial (MESHHANDLE hMesh, DWORD idx)
{
	wb::MeshUse (hMesh);
	WbWriter &w = wb::Begin (L_MeshMaterial); w.u32 (wb::Id (hMesh)); w.u32 (idx);
	WbReader r = wb::Call ();
	MATERIAL m;
	bool ok = r.b ();
	r.raw (&m, sizeof m);
	if (!ok || r.bad) return 0;
	Copy *c = Find (wb::Id (hMesh), idx, true, true);
	c->m = c->m0 = m;
	c->recent = g_recent = true;
	return &c->m;
}

int oapiEditMeshGroup (DEVMESHHANDLE hMesh, DWORD grpidx, GROUPEDITSPEC *ges)
{
	WbWriter &w = wb::Begin (L_EditMeshGroup); w.u32 (wb::Id (hMesh)); w.u32 (grpidx);
	w.u32 (ges->flags); w.u32 (ges->UsrFlag); w.u32 (ges->nVtx);
	w.b (ges->Vtx != 0); if (ges->Vtx) w.raw (ges->Vtx, ges->nVtx * sizeof (NTVERTEX));
	w.b (ges->vIdx != 0); if (ges->vIdx) w.raw (ges->vIdx, ges->nVtx * sizeof (WORD));
	WbReader r = wb::Call ();
	return r.i32 ();
}

// meshes a module makes (Orbiter copies the groups' arrays); a group grown with a block is read again into the module's copy
namespace {
	void PutGroup (WbWriter &w, const MESHGROUP &g)
	{
		w.u32 (g.MtrlIdx); w.u32 (g.TexIdx); w.u32 (g.zBias); w.u32 (g.nVtx); w.u32 (g.nIdx);
		w.b (g.Vtx != 0); if (g.Vtx) w.raw (g.Vtx, g.nVtx * sizeof (NTVERTEX));
		w.b (g.Idx != 0); if (g.Idx) w.raw (g.Idx, g.nIdx * sizeof (WORD));
	}
	void Refresh (uint32_t mesh, DWORD idx)
	{
		wb::MeshRefresh (mesh, idx);
	}
}

// the module's copy of a group Orbiter changed is read again
void wb::MeshRefresh (uint32_t mesh, uint32_t idx)
{
	Copy *c = Find (mesh, idx, false, false);
	if (!c) return;
	WbWriter &w = wb::Begin (L_MeshGroup); w.u32 (mesh); w.u32 (idx);
	WbReader r = wb::Call ();
	if (r.b ()) ReadGroup (r, c);
}

MESHHANDLE oapiCreateMesh (DWORD ngrp, MESHGROUP *grp)
{
	WbWriter &w = wb::Begin (L_CreateMesh); w.u32 (grp ? ngrp : 0);
	for (DWORD i = 0; grp && i < ngrp; i++) PutGroup (w, grp[i]);
	WbReader r = wb::Call ();
	return (MESHHANDLE)wb::Ptr (r.u32 ());
}

DWORD oapiAddMeshGroup (MESHHANDLE hMesh, MESHGROUP *grp)
{
	wb::MeshUse (hMesh);
	WbWriter &w = wb::Begin (L_AddMeshGroup); w.u32 (wb::Id (hMesh));
	MESHGROUP none = {};
	PutGroup (w, grp ? *grp : none);
	WbReader r = wb::Call ();
	return r.u32 ();
}

bool oapiAddMeshGroupBlock (MESHHANDLE hMesh, DWORD grpidx, const NTVERTEX *vtx, DWORD nvtx, const WORD *idx, DWORD nidx)
{
	wb::MeshUse (hMesh);
	WbWriter &w = wb::Begin (L_AddMeshGroupBlock); w.u32 (wb::Id (hMesh)); w.u32 (grpidx); w.u32 (nvtx); w.u32 (nidx);
	w.b (vtx != 0); if (vtx) w.raw (vtx, nvtx * sizeof (NTVERTEX));
	w.b (idx != 0); if (idx) w.raw (idx, nidx * sizeof (WORD));
	WbReader r = wb::Call ();
	bool ok = r.b () && !r.bad;
	if (ok) Refresh (wb::Id (hMesh), grpidx);
	return ok;
}

// the module's changes go first; its copies of the mesh go with it
void oapiDeleteMesh (MESHHANDLE hMesh)
{
	wb::MeshUse (hMesh);
	uint32_t id = wb::Id (hMesh), k = 0;
	for (uint32_t i = 0; i < g_ncopies; i++) {
		Copy *c = g_copies[i];
		if (id && c->mesh == id) { free (c->g.Vtx); free (c->g.Idx); free (c->v0); free (c->i0); free (c); }
		else g_copies[k++] = c;
	}
	g_ncopies = k;
	wb::Begin (L_DeleteMesh).u32 (id);
	wb::Call ();
}

// the HUD drawn as a mesh with the module's textures: as many as the mesh's groups use (Linux counts as the client reads them)
void oapiRenderHUD (MESHHANDLE hMesh, SURFHANDLE *hTex)
{
	wb::MeshUse (hMesh);
	uint32_t n = 0;
	if (hTex) {
		wb::Begin (L_MeshTexCount).u32 (wb::Id (hMesh));
		WbReader r = wb::Call ();
		n = r.u32 ();
		if (r.bad || n > 4096) n = 0;
	}
	WbWriter &w = wb::Begin (L_RenderHUD); w.u32 (wb::Id (hMesh)); w.b (hTex != 0); w.u32 (n);
	for (uint32_t i = 0; i < n; i++) w.u32 (wb::Id (hTex[i]));
	wb::Call ();
}

void wb::MeshUse (const void *hMesh)
{
	for (uint32_t i = 0; i < g_ncopies; i++)
		if (!hMesh || g_copies[i]->mesh == Id (hMesh)) Store (g_copies[i]);
}

void wb::MeshFrame ()
{
	MeshUse (0);
}

bool wb::MeshRecent ()
{
	return g_recent;
}

void wb::MeshStoreRecent ()
{
	g_recent = false;
	for (uint32_t i = 0; i < g_ncopies; i++)
		if (g_copies[i]->recent) { g_copies[i]->recent = false; Store (g_copies[i]); }
}

// a transmitter's data; descr points to the helper's copy of its id string, kept per transmitter as Orbiter keeps the string
namespace {
	struct NavDescr { uint32_t nav; char *s; };
	NavDescr *g_navs;
	uint32_t g_nnavs, g_capnavs;

	const char *Descr (uint32_t nav, const char *s, uint32_t n)
	{
		if (!s) return 0;
		uint32_t i = 0;
		while (i < g_nnavs && g_navs[i].nav != nav) i++;
		if (i < g_nnavs && strlen (g_navs[i].s) == n && !memcmp (g_navs[i].s, s, n)) return g_navs[i].s;
		if (i == g_nnavs) {
			if (g_nnavs == g_capnavs) { g_capnavs = g_capnavs ? g_capnavs * 2 : 16; g_navs = (NavDescr *)realloc (g_navs, g_capnavs * sizeof *g_navs); }
			g_navs[g_nnavs++] = {nav, 0};
		}
		free (g_navs[i].s); // another transmitter at a freed one's address: its string was gone with it, as on Windows
		g_navs[i].s = (char *)malloc (n + 1);
		memcpy (g_navs[i].s, s, n);
		g_navs[i].s[n] = 0;
		return g_navs[i].s;
	}
}

int oapiGetNavData (NAVHANDLE hNav, NAVDATA *data)
{
	wb::Begin (L_GetNavData).u32 (wb::Id (hNav));
	WbReader r = wb::Call ();
	DWORD type = r.u32 (), ch = r.u32 ();
	double power = r.f64 ();
	uint32_t n;
	const char *s = r.str (&n);
	uint32_t h = r.u32 ();
	double a = r.f64 (), b = r.f64 ();
	uint32_t dock = r.u32 ();
	if (r.bad || !data) return 0;
	data->type = type; data->ch = ch; data->power = power; data->descr = Descr (wb::Id (hNav), s, n);
	switch (type) {
	case TRANSMITTER_VOR: data->vor.hPlanet = (OBJHANDLE)wb::Ptr (h); data->vor.lng = a; data->vor.lat = b; break;
	case TRANSMITTER_VTOL: data->vtol.hBase = (OBJHANDLE)wb::Ptr (h); data->vtol.npad = (int)a; break;
	case TRANSMITTER_ILS: data->ils.hBase = (OBJHANDLE)wb::Ptr (h); data->ils.appdir = a; break;
	case TRANSMITTER_IDS: data->ids.hVessel = (OBJHANDLE)wb::Ptr (h); data->ids.hDock = (DOCKHANDLE)wb::Ptr (dock); break;
	case TRANSMITTER_XPDR: data->xpdr.hVessel = (OBJHANDLE)wb::Ptr (h); break;
	}
	return 0;
}

// global meshes stay loaded for the session, as Orbiter's
const MESHHANDLE oapiLoadMeshGlobal (const char *fname)
{
	WbWriter &w = wb::Begin (L_LoadMeshGlobal); w.str (fname);
	WbReader r = wb::Call ();
	return (const MESHHANDLE)wb::Ptr (r.u32 ());
}

MESHHANDLE oapiLoadMesh (const char *fname)
{
	WbWriter &w = wb::Begin (L_LoadMesh); w.str (fname);
	WbReader r = wb::Call ();
	return (MESHHANDLE)wb::Ptr (r.u32 ());
}
