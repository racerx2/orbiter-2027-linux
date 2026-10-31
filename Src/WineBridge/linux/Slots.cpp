// WineBridge: the Windows modules Orbiter opens itself (ModuleLoad of a .dll); a slot per module, with the entry points Orbiter asks for
#include "Orbitersdk.h"
#include "Bridge.h"
#include "WbVessel.h"
#include "Manual.h"
#include <array>
#include <cstring>
#include <string>
#include <unistd.h>
#include <utility>

using Guard = std::lock_guard<std::recursive_mutex>;

#define WB_SLOTS 256

// opens: Orbiter's opens of the slot; module: the bridge's module, opened once for the slot; exports: its WbExport bits
struct WbSlot { std::string path; int opens = 0, module = 0; uint32_t exports = 0; };
static WbSlot *const g_slot = new WbSlot[WB_SLOTS]; // never destroyed: modules are freed at exit
static WbSlot *g_loading; // the slot of the module being loaded, during its load

static VESSEL *OvcInit (int s, OBJHANDLE h, int fm)
{
	Guard g (WbBridge::Get ().Lock ());
	WbVessel *v = new WbVessel (h, fm);
	v->Attach (g_slot[s].path);
	return v;
}

template <class F> static void Opc (int s, int e, F args)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!b.Alive () || !b.Info (g_slot[s].module)) return;
	WbWriter &w = b.Begin (H_OPC);
	w.i32 (g_slot[s].module); w.i32 (e);
	args (w);
	b.Call ();
}

template <int S> struct WbSlotFn {
	static int GetModuleVersion () { Guard g (WbBridge::Get ().Lock ()); auto *m = WbBridge::Get ().Info (g_slot[S].module); return m ? m->version : 0; }
	static char *ModuleDate () { Guard g (WbBridge::Get ().Lock ()); auto *m = WbBridge::Get ().Info (g_slot[S].module); return (char *)(m ? m->date.c_str () : ""); }
	static VESSEL *ovcInit (OBJHANDLE h, int fm) { return OvcInit (S, h, fm); }
	static void ovcExit (VESSEL *v) { Guard g (WbBridge::Get ().Lock ()); delete static_cast<WbVessel *> (v); }
	static void opcOpenRenderViewport (void *, DWORD w, DWORD h, BOOL fs) { Opc (S, E_opcOpenRenderViewport, [&] (WbWriter &x) { x.u32 (w); x.u32 (h); x.b (fs); }); }
	static void opcCloseRenderViewport () { Opc (S, E_opcCloseRenderViewport, [] (WbWriter &) {}); }
	static void opcPreStep (double t, double dt, double mjd) {
		{ Guard g (WbBridge::Get ().Lock ()); if (WbBridge::Get ().Alive ()) WbBridge::Get ().Frame (t); }
		Opc (S, E_opcPreStep, [&] (WbWriter &x) { x.f64 (t); x.f64 (dt); x.f64 (mjd); });
	}
	static void opcPostStep (double t, double dt, double mjd) { Opc (S, E_opcPostStep, [&] (WbWriter &x) { x.f64 (t); x.f64 (dt); x.f64 (mjd); }); }
	static void opcFocusChanged (OBJHANDLE n, OBJHANDLE o) { Opc (S, E_opcFocusChanged, [&] (WbWriter &x) { x.u32 (WbBridge::Get ().Id (n)); x.u32 (WbBridge::Get ().Id (o)); }); }
	static void opcTimeAccChanged (double n, double o) { Opc (S, E_opcTimeAccChanged, [&] (WbWriter &x) { x.f64 (n); x.f64 (o); }); }
	static void opcDeleteVessel (OBJHANDLE h) { Opc (S, E_opcDeleteVessel, [&] (WbWriter &x) { x.u32 (WbBridge::Get ().Id (h)); }); }
	static void opcPause (bool p) { Opc (S, E_opcPause, [&] (WbWriter &x) { x.b (p); }); }
	static void opcSaveState (FILEHANDLE f) { Opc (S, E_opcSaveState, [&] (WbWriter &x) { x.u32 (WbBridge::Get ().Id (f)); }); }
	static void opcLoadState (FILEHANDLE f) { Opc (S, E_opcLoadState, [&] (WbWriter &x) { x.u32 (WbBridge::Get ().Id (f)); }); }
	static CELBODY *InitInstance (OBJHANDLE h) { Guard g (WbBridge::Get ().Lock ()); return WbCbInit (g_slot[S].module, h); }
	static void ExitInstance (CELBODY *m) { Guard g (WbBridge::Get ().Lock ()); WbCbExit (g_slot[S].module, m); }
	static ATMOSPHERE *CreateAtmosphere (CELBODY2 *cb) { Guard g (WbBridge::Get ().Lock ()); return WbAtmCreate (g_slot[S].module, cb); }
	static void DeleteAtmosphere (ATMOSPHERE *a) { Guard g (WbBridge::Get ().Lock ()); WbAtmDelete (g_slot[S].module, a); }
	static char *ModelName () { Guard g (WbBridge::Get ().Lock ()); auto *m = WbBridge::Get ().Info (g_slot[S].module); return (char *)(m ? m->model.c_str () : ""); }
	static char *ModelDesc () { Guard g (WbBridge::Get ().Lock ()); auto *m = WbBridge::Get ().Info (g_slot[S].module); return (char *)(m ? m->desc.c_str () : ""); }
};

using Row = std::array<void *, E_COUNT>;
template <int S> static Row SlotRow ()
{
	using F = WbSlotFn<S>;
	return {(void *)&F::GetModuleVersion, (void *)&F::ModuleDate, (void *)&F::ovcInit, (void *)&F::ovcExit,
		(void *)&F::opcOpenRenderViewport, (void *)&F::opcCloseRenderViewport, (void *)&F::opcPreStep, (void *)&F::opcPostStep,
		(void *)&F::opcFocusChanged, (void *)&F::opcTimeAccChanged, (void *)&F::opcDeleteVessel, (void *)&F::opcPause,
		(void *)&F::opcSaveState, (void *)&F::opcLoadState, (void *)&F::InitInstance, (void *)&F::ExitInstance,
		(void *)&F::CreateAtmosphere, (void *)&F::DeleteAtmosphere, (void *)&F::ModelName, (void *)&F::ModelDesc};
}
template <size_t... I> static std::array<Row, WB_SLOTS> SlotRows (std::index_sequence<I...>) { return {SlotRow<(int)I> ()...}; }
static const std::array<Row, WB_SLOTS> &g_fn = *new std::array<Row, WB_SLOTS> (SlotRows (std::make_index_sequence<WB_SLOTS> ()));

static int SlotOf (void *h)
{
	WbSlot *s = (WbSlot *)h;
	return s >= g_slot && s < g_slot + WB_SLOTS && s->opens ? (int)(s - g_slot) : -1;
}

// LoadLibrary: the module at path (Orbiter-relative or absolute), opened once more; null on failure (logged)
DLLCLBK void *wbModuleLoad (const char *path)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	std::string p = path ? path : "";
	if (p.empty ()) return nullptr;
	if (p[0] != '/') {
		char cwd[4096];
		if (getcwd (cwd, sizeof cwd)) p = std::string (cwd) + "/" + p;
	}
	int free = -1;
	for (int s = 0; s < WB_SLOTS; s++) {
		if (g_slot[s].opens && g_slot[s].path == p) { g_slot[s].opens++; return &g_slot[s]; }
		if (!g_slot[s].opens && free < 0) free = s;
	}
	if (free < 0) {
		oapiWriteLogV ("WineBridge: %s not loaded: %d Windows modules are open already", p.c_str (), WB_SLOTS);
		return nullptr;
	}
	g_loading = &g_slot[free]; // its module objects are registered during the load
	int m = b.Load (p);
	g_loading = nullptr;
	if (!m) return nullptr;
	g_slot[free] = WbSlot ();
	g_slot[free].path = p;
	g_slot[free].opens = 1;
	g_slot[free].module = m;
	g_slot[free].exports = b.Info (m) ? b.Info (m)->exports : 0;
	return &g_slot[free];
}

int WbSlotModule (void *h)
{
	int s = SlotOf (h);
	return s >= 0 ? g_slot[s].module : 0;
}

// the slot of the helper's module: during a load, a number the bridge doesn't know yet is the module's being loaded
void *WbSlotFor (int module)
{
	if (!module) return nullptr;
	for (int s = 0; s < WB_SLOTS; s++) if (g_slot[s].opens && g_slot[s].module == module) return &g_slot[s];
	return g_loading && !WbBridge::Get ().Info (module) ? g_loading : nullptr;
}

// GetProcAddress: the slot's function for an entry point the module exports; else null, as dlsym
DLLCLBK void *wbModuleProc (void *h, const char *name)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	int s = SlotOf (h);
	if (s < 0 || !name) return nullptr;
	const WbSlot &slot = g_slot[s];
	bool live = b.Info (slot.module) != nullptr;
	for (int e = 0; e < E_COUNT; e++) {
		if (strcmp (name, WbExportName[e])) continue;
		// Orbiter frees a body's and an atmosphere's objects through the module that made them; the slot does it also for a module
		// without the function (Orbiter's delete) and after the helper stopped
		if (e == E_ExitInstance) return slot.exports >> E_InitInstance & 1u ? g_fn[s][e] : nullptr;
		if (e == E_DeleteAtmosphere) return slot.exports >> E_CreateAtmosphere & 1u ? g_fn[s][e] : nullptr;
		return live && slot.exports >> e & 1u ? g_fn[s][e] : nullptr;
	}
	return live ? WbLegacyProc (slot.module, name) : nullptr; // the 2006 interface's <body>_SetPrecision, _Ephemeris, ...
}

// FreeLibrary: the opens left; the last one frees the module
DLLCLBK int wbModuleFree (void *h)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	int s = SlotOf (h);
	if (s < 0) return 0;
	WbSlot &slot = g_slot[s];
	if (--slot.opens > 0) return slot.opens;
	int m = slot.module;
	slot = WbSlot ();
	b.Free (m);
	return 0;
}
