// WineBridge helper: plugin module objects (ModuleAPI.cpp of the Windows source). A module's object is here; Orbiter calls its
// proxy on the Linux side, which forwards the callbacks the object's class overrides and runs Orbiter's default for the others.
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"
#include "compat2010.h"

using namespace oapi;

namespace {
	struct Rec { uint32_t num; Module *obj; int module; bool old; };
	Rec **g_rec;
	uint32_t g_nrec, g_caprec, g_next;

	Rec *RecOf (const Module *m) { for (uint32_t i = 0; i < g_nrec; i++) if (g_rec[i]->obj == m) return g_rec[i]; return 0; }
	Rec *RecNum (uint32_t num) { for (uint32_t i = 0; i < g_nrec; i++) if (g_rec[i]->num == num) return g_rec[i]; return 0; }
	void Drop (Rec *e)
	{
		for (uint32_t i = 0; i < g_nrec; i++) if (g_rec[i] == e) { g_rec[i] = g_rec[--g_nrec]; break; }
		free (e);
	}

	// a default the module's class calls itself (Module::clbkPreStep from its override): Orbiter's, which calls the opc
	// function of the object's module with Orbiter's arguments, as GetProcAddress (hModule) here
	WbWriter &Default (const Module *m, int c)
	{
		WbWriter &w = wb::Begin (L_ModuleDefault); w.i32 (wb::ModuleNumber (m->GetModule ())); w.i32 (c);
		return w;
	}
}

ModuleNV::ModuleNV (HINSTANCE hDLL)
{
	version = 0;
	hModule = hDLL;
}

double ModuleNV::GetSimTime () const
{
	return oapiGetSimTime ();
}

double ModuleNV::GetSimStep () const
{
	return oapiGetSimStep ();
}

double ModuleNV::GetSimMJD () const
{
	return oapiGetSimMJD ();
}

Module::Module (HINSTANCE hDLL): ModuleNV (hDLL)
{
	version = 1;
}

Module::~Module ()
{
	Rec *e = RecOf (this);
	if (!e) return;
	wb::Begin (L_ModuleGone).u32 (e->num); // its proxy stops forwarding; Orbiter may still hold it
	wb::Call ();
	Drop (e);
}

void Module::clbkSimulationStart (RenderMode mode)
{
	Default (this, O_SimulationStart).i32 (mode);
	wb::Call ();
}

void Module::clbkSimulationEnd ()
{
	Default (this, O_SimulationEnd);
	wb::Call ();
}

void Module::clbkPreStep (double simt, double simdt, double mjd)
{
	WbWriter &w = Default (this, O_PreStep); w.f64 (simt); w.f64 (simdt); w.f64 (mjd);
	wb::Call ();
}

void Module::clbkPostStep (double simt, double simdt, double mjd)
{
	WbWriter &w = Default (this, O_PostStep); w.f64 (simt); w.f64 (simdt); w.f64 (mjd);
	wb::Call ();
}

void Module::clbkFocusChanged (OBJHANDLE new_focus, OBJHANDLE old_focus)
{
	WbWriter &w = Default (this, O_FocusChanged); w.u32 (wb::Id (new_focus)); w.u32 (wb::Id (old_focus));
	wb::Call ();
}

void Module::clbkTimeAccChanged (double new_warp, double old_warp)
{
	WbWriter &w = Default (this, O_TimeAccChanged); w.f64 (new_warp); w.f64 (old_warp);
	wb::Call ();
}

void Module::clbkDeleteVessel (OBJHANDLE hVessel)
{
	Default (this, O_DeleteVessel).u32 (wb::Id (hVessel));
	wb::Call ();
}

void Module::clbkPause (bool pause)
{
	Default (this, O_Pause).b (pause);
	wb::Call ();
}

// Orbiter takes the object a module registers during its load as the module's
void oapiRegisterModule (Module *module)
{
	if (!module) return;
	Rec *e = RecOf (module);
	if (!e) {
		e = (Rec *)calloc (1, sizeof *e);
		e->num = ++g_next;
		e->obj = module;
		if (g_nrec == g_caprec) { g_caprec = g_caprec ? g_caprec * 2 : 8; g_rec = (Rec **)realloc (g_rec, g_caprec * sizeof *g_rec); }
		g_rec[g_nrec++] = e;
	}
	const void *const *vt = *(const void *const *const *)module;
	e->module = wb::AddrModule (vt);
	e->old = wb::CallerApi (vt) < wb::API_2016; // no ProcessMouse and keyboard callbacks in its vtable
	// the callbacks its class overrides: its vtable against Module's own (slot 0 is the destructor)
	static Module base (0);
	const void *const *bt = *(const void *const *const *)&base;
	uint32_t over = 0;
	int n = e->old ? WB_MODULE_2016 : O_COUNT;
	for (int c = 0; c < n; c++) if (wb::Resolve (vt[1 + c]) != bt[1 + c]) over |= 1u << c;
	WbWriter &w = wb::Begin (L_RegisterModule);
	w.u32 (e->num); w.i32 (wb::ModuleNumber (module->GetModule ())); w.u32 (over); w.b (e->old);
	wb::Call ();
}

void wb::ServeModule (WbReader &r, WbWriter &w)
{
	uint32_t num = r.u32 ();
	int c = r.i32 ();
	Rec *e = r.bad ? 0 : RecNum (num);
	if (!e || (e->old && c >= WB_MODULE_2016)) { w.b (false); return; }
	Module *m = e->obj;
	switch (c) {
	case O_SimulationStart: { int mode = r.i32 (); if (!r.bad) m->clbkSimulationStart ((Module::RenderMode)mode); break; }
	case O_SimulationEnd: m->clbkSimulationEnd (); break;
	case O_PreStep: case O_PostStep: case O_TimeJump: {
		double a = r.f64 (), b = r.f64 (), d = r.f64 ();
		if (r.bad) break;
		if (c == O_PreStep) m->clbkPreStep (a, b, d);
		else if (c == O_PostStep) m->clbkPostStep (a, b, d);
		else m->clbkTimeJump (a, b, d);
		break; }
	case O_FocusChanged: { uint32_t a = r.u32 (), b = r.u32 (); if (!r.bad) m->clbkFocusChanged ((OBJHANDLE)wb::Ptr (a), (OBJHANDLE)wb::Ptr (b)); break; }
	case O_TimeAccChanged: { double a = r.f64 (), b = r.f64 (); if (!r.bad) m->clbkTimeAccChanged (a, b); break; }
	case O_NewVessel: case O_DeleteVessel: case O_VesselJump: {
		OBJHANDLE h = (OBJHANDLE)wb::Ptr (r.u32 ());
		if (r.bad) break;
		if (c == O_NewVessel) m->clbkNewVessel (h);
		else if (c == O_DeleteVessel) m->clbkDeleteVessel (h);
		else m->clbkVesselJump (h);
		break; }
	case O_Pause: { bool p = r.b (); if (!r.bad) m->clbkPause (p); break; }
	case O_ProcessMouse: {
		UINT ev = r.u32 (); DWORD st = r.u32 (), x = r.u32 (), y = r.u32 ();
		bool res = !r.bad && m->clbkProcessMouse (ev, st, x, y);
		w.b (true); w.b (res);
		return; }
	case O_ProcessKeyboardImmediate: case O_ProcessKeyboardBuffered: {
		DWORD key = c == O_ProcessKeyboardBuffered ? r.u32 () : 0;
		char ks[256];
		r.raw (ks, 256);
		bool running = r.b ();
		bool res = !r.bad && (c == O_ProcessKeyboardBuffered ? m->clbkProcessKeyboardBuffered (key, ks, running) : m->clbkProcessKeyboardImmediate (ks, running));
		w.b (true); w.b (res); w.raw (ks, 256);
		return; }
	default: break;
	}
	w.b (true); // after the module's code: its calls reuse the buffer
}

// the module's code is gone: its objects are no more called; their proxies go with the module on the Linux side
void wb::ModuleFreed (int module)
{
	for (uint32_t i = 0; i < g_nrec; ) {
		if (g_rec[i]->module == module) Drop (g_rec[i]);
		else i++;
	}
}

// the inline and compiler-made members Orbiter.exe exports (api/inline.def): made here (this file is built without inlining)
__attribute__ ((used)) static void Made (Module *a)
{
	Module c (*a);
	*a = c;
	int v = a->Version ();
	HINSTANCE h = a->GetModule ();
	(void)v; (void)h;
}

// ModuleNV's copy assignment: trivial, so the compiler makes no function of it; this one, exported under its name (api/inline.def)
namespace wb {
	struct NvAssign : ModuleNV {
		NvAssign &Assign (const NvAssign &o);
	};
}

wb::NvAssign &wb::NvAssign::Assign (const NvAssign &o)
{
	version = o.version;
	hModule = o.hModule;
	return *this;
}
