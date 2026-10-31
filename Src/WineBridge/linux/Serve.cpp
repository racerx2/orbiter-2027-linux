// WineBridge: the helper's calls into Orbiter. Each reads all its arguments before it calls Orbiter or writes its reply.
#include "Orbitersdk.h"
#include "Bridge.h"
#include "WbVessel.h"
#include "Manual.h"
#include <set>
#include <string>
#include <utility>
#include <vector>

static VECTOR3 V3 (WbReader &r) { VECTOR3 v; r.v3 (v.data); return v; }
static std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }

// a problem with a call is logged once per call id and kind
static void Once (int kind, uint32_t fn, uint32_t vid = 0)
{
	static std::set<std::pair<int, uint32_t>> &said = *new std::set<std::pair<int, uint32_t>>; // never destroyed: calls may come after exit began
	if (!said.insert ({kind, fn}).second) return;
	if (kind == 0) oapiWriteLogV ("WineBridge: call %u from the helper is not known here", fn);
	else if (kind == 1) oapiWriteLogV ("WineBridge: call %u from the helper had bad arguments", fn);
	else oapiWriteLogV ("WineBridge: call %u from the helper names no vessel of this bridge (%u)", fn, vid);
}
static void Unknown (uint32_t fn) { Once (0, fn); }
bool WbServeGen (uint32_t fn, WbReader &r, WbWriter &w); // gen_serve.cpp

void WbBridge::NoVessel (uint32_t fn, uint32_t vid) { Once (2, fn, vid); }
void WbBridge::BadArgs (uint32_t fn) { Once (1, fn); }

void WbServe (uint32_t fn, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	switch (fn) {
	case L_LOG: { std::string s = Str (r); oapiWriteLogV ("WineBridge: %s", s.c_str ()); return; }
	case L_WriteLog: { std::string s = Str (r); oapiWriteLog (s.c_str ()); return; }
	case L_WriteLogError: { std::string f = Str (r), file = Str (r); int line = r.i32 (); std::string s = Str (r); __writeLogError (f.c_str (), file.c_str (), line, "%s", s.c_str ()); return; }
	default:
		break;
	}
	if (fn >= 1000) { if (!WbServeGen (fn, r, w)) Unknown (fn); return; } // gen_serve.cpp
	if (WbServeSound (fn, r, w)) return;  // Sound.cpp
	if (WbServeManual (fn, r, w)) return; // Manual.cpp
	if (WbServeDraw (fn, r, w)) return;   // Draw.cpp
	if (WbServeMfd (fn, r, w)) return;    // Mfd.cpp
	if (WbServeCelbody (fn, r, w)) return; // Celbody.cpp
	if (WbServeExtra (fn, r, w)) return;   // Extra.cpp
	if (WbServePanel (fn, r, w)) return;   // Panel.cpp
	if (WbServeLight (fn, r, w)) return;   // Light.cpp
	if (WbServeWatched (fn, r)) return;    // Vdata.cpp
	if (WbServeVdata (fn, r, w)) return;   // Vdata.cpp
	if (WbServeOapi (fn, r, w)) return;    // Oapi.cpp
	if (WbServeLua (fn, r, w)) return;     // Lua.cpp
	if (WbServeLuaRemote (fn, r, w)) return; // LuaRemote.cpp
	if (fn <= L_LOG || fn >= L_END) { Unknown (fn); return; }

	// the rest act on a vessel; a reply left empty reads as zeros (a null handle) on the helper's side
	uint32_t vid = r.u32 ();
	VESSEL *v = b.Vessel (vid);
	WbVessel *bv = b.Bridged (vid);
	switch (fn) {
	case L_CreateAirfoil3: {
		int align = r.i32 ();
		VECTOR3 ref = V3 (r);
		uint32_t cf = r.u32 (), cx = r.u32 ();
		double c = r.f64 (), S = r.f64 (), A = r.f64 ();
		if (bv && !r.bad) w.u32 (b.Id (bv->Airfoil ((AIRFOIL_ORIENTATION)align, ref, cf, cx, c, S, A)));
		break; }
	case L_AddAnimationComponent: {
		UINT anim = r.u32 ();
		double s0 = r.f64 (), s1 = r.f64 ();
		ANIMATIONCOMPONENT_HANDLE parent = (ANIMATIONCOMPONENT_HANDLE)b.Ptr (r.u32 ());
		bool has = r.b ();
		WbVessel::Transform t;
		if (has) {
			t.type = r.i32 ();
			t.mesh = r.u32 ();
			t.list = r.b ();
			t.ngrp = r.u32 ();
			if (t.list) {
				if (t.ngrp > WB_MSG_MAX / 4) { r.bad = true; break; }
				t.grp.resize (t.ngrp);
				for (uint32_t i = 0; i < t.ngrp; i++) t.grp[i] = r.u32 ();
			}
			switch (t.type) {
			case MGROUP_TRANSFORM::ROTATE: t.ref = V3 (r); t.axis = V3 (r); t.angle = (float)r.f64 (); break;
			case MGROUP_TRANSFORM::TRANSLATE: t.shift = V3 (r); break;
			case MGROUP_TRANSFORM::SCALE: t.ref = V3 (r); t.scale = V3 (r); break;
			default: break;
			}
		}
		if (bv && !r.bad) w.u32 (b.Id (bv->Component (anim, s0, s1, has ? &t : nullptr, parent)));
		break; }
	case L_CreateThrusterGroup: {
		int type = r.i32 (), n = r.i32 ();
		if (n < 0 || (uint32_t)n > WB_MSG_MAX / 4) { r.bad = true; break; }
		std::vector<THRUSTER_HANDLE> th (n);
		for (int i = 0; i < n; i++) th[i] = (THRUSTER_HANDLE)b.Ptr (r.u32 ());
		if (v && !r.bad) w.u32 (b.Id (v->CreateThrusterGroup (n ? th.data () : nullptr, n, (THGROUP_TYPE)type)));
		break; }
	case L_AddExhaustStream2: {
		THRUSTER_HANDLE th = (THRUSTER_HANDLE)b.Ptr (r.u32 ());
		VECTOR3 pos = V3 (r);
		bool has = r.b ();
		PARTICLESTREAMSPEC s {};
		if (has) {
			s.flags = r.u32 (); s.srcsize = r.f64 (); s.srcrate = r.f64 (); s.v0 = r.f64 (); s.srcspread = r.f64 ();
			s.lifetime = r.f64 (); s.growthrate = r.f64 (); s.atmslowdown = r.f64 ();
			s.ltype = (PARTICLESTREAMSPEC::LTYPE)r.i32 (); s.levelmap = (PARTICLESTREAMSPEC::LEVELMAP)r.i32 ();
			s.lmin = r.f64 (); s.lmax = r.f64 (); s.atmsmap = (PARTICLESTREAMSPEC::ATMSMAP)r.i32 ();
			s.amin = r.f64 (); s.amax = r.f64 (); s.tex = (SURFHANDLE)b.Ptr (r.u32 ());
		}
		if (bv && !r.bad) w.u32 (b.Id (bv->Stream (th, pos, has ? &s : nullptr)));
		break; }
	case L_BaseSaveState: {
		FILEHANDLE scn = (FILEHANDLE)b.Ptr (r.u32 ());
		if (bv && !r.bad) bv->BaseSaveState (scn);
		break; }
	case L_BaseLoadGenericCockpit: { if (bv && !r.bad) w.b (bv->BaseLoadGenericCockpit ()); break; }
	case L_BaseGetRadiationForce: {
		VECTOR3 mflux = V3 (r), F {}, pos {};
		if (bv && !r.bad) { bv->BaseGetRadiationForce (mflux, F, pos); w.v3 (F.data); w.v3 (pos.data); }
		break; }
	default:
		Unknown (fn);
		break;
	}
	if (r.bad) Once (1, fn);
	else if (!v || (!bv && (fn == L_CreateAirfoil3 || fn == L_AddAnimationComponent || fn == L_AddExhaustStream2 || fn >= L_BaseSaveState))) Once (2, fn, vid);
}
