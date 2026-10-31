// WineBridge: the oapi functions a Windows module asks Orbiter's data of (atmosphere, wind, parameters, names, visuals, HUD modes,
// keys, meshes), each as Orbiter answers it; the helper's interpreter library
#include "Orbitersdk.h"
#include "GraphicsAPI.h"
#include "Bridge.h"
#include "Manual.h"
#include <dlfcn.h>
#include <string>
#include <vector>

static VECTOR3 V3 (WbReader &r) { VECTOR3 v; r.v3 (v.data); return v; }
static std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }

namespace {
	// the size of Orbiter's value of an object parameter (Planet::GetParam's members); 0: not one Orbiter has
	uint32_t ParamSize (DWORD type)
	{
		switch (type) {
		case OBJPRM_PLANET_SURFACEMAXLEVEL: return sizeof (DWORD);
		case OBJPRM_PLANET_SURFACERIPPLE: case OBJPRM_PLANET_HASCLOUDS: case OBJPRM_PLANET_CLOUDMICROTEX: case OBJPRM_PLANET_HASRINGS:
		case OBJPRM_PLANET_CLOUDOVERSATURATE: return sizeof (bool);
		case OBJPRM_PLANET_HAZEEXTENT: case OBJPRM_PLANET_HAZEDENSITY: case OBJPRM_PLANET_HAZESHIFT: case OBJPRM_PLANET_SHADOWCOLOUR:
		case OBJPRM_PLANET_CLOUDALT: case OBJPRM_PLANET_CLOUDROTATION: case OBJPRM_PLANET_CLOUDMICROALTMIN: case OBJPRM_PLANET_CLOUDMICROALTMAX:
		case OBJPRM_PLANET_RINGMINRAD: case OBJPRM_PLANET_RINGMAXRAD: case OBJPRM_PLANET_ATTENUATIONALT: case OBJPRM_PLANET_HORIZONEXCESS:
		case OBJPRM_PLANET_TILEBBEXCESS: case OBJPRM_PLANET_MINELEVATION: case OBJPRM_PLANET_MAXELEVATION: case OBJPRM_PLANET_ELEVRESOLUTION:
			return sizeof (double);
		case OBJPRM_PLANET_CLOUDSHADOWCOL: return sizeof (float);
		case OBJPRM_PLANET_HAZECOLOUR: case OBJPRM_PLANET_ATMTINTCOLOUR: return sizeof (VECTOR3);
		case OBJPRM_PLANET_FOGPARAM: return sizeof (FogParam);
		case OBJPRM_PLANET_TILEENGINE: case OBJPRM_PLANET_CLOUDTILEENGINE: case OBJPRM_PLANET_CLOUDMAXLEVEL: case OBJPRM_PLANET_LABELENGINE:
			return sizeof (int);
		default: return 0;
		}
	}

	void PutStr (WbWriter &w, const char *s) { w.b (s != nullptr); w.str (s ? s : ""); }
}

// the visual handle Orbiter keeps for a body (oapiObjectVisualPtr's slot)
static uint32_t VisualOf (WbBridge &b, OBJHANDLE h)
{
	VISHANDLE *p = h ? oapiObjectVisualPtr (h) : nullptr;
	return b.Id (p ? *p : nullptr);
}

bool WbServeOapi (uint32_t fn, WbReader &r, WbWriter &w)
{
	if (fn != L_LoadInterpreterLib && (fn < L_GetAtm || fn > L_LoadMeshGlobalClbk)) return false;
	WbBridge &b = WbBridge::Get ();
	switch (fn) {
	case L_LoadInterpreterLib: { // as Orbiter.exe's ScriptInterface loads LuaInline: a plugin of Orbiter's, the core's wbPluginLoad
		static void *(*load) (const char *, const char *) = (void *(*) (const char *, const char *))dlsym (RTLD_DEFAULT, "wbPluginLoad");
		void *h = load ? load ("Modules/WineBridge", "LuaInline") : nullptr;
		if (!load) oapiWriteLog ((char *)"WineBridge: this Orbiter has no wbPluginLoad: Windows modules get no interpreter");
		w.i32 (h ? WbSlotModule (h) : 0);
		break; }
	case L_GetAtm: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		ATMPARAM p {};
		OBJHANDLE ref = nullptr;
		if (!r.bad && (!h || oapiIsVessel (h))) oapiGetAtm (h, &p, &ref);
		w.f64 (p.T); w.f64 (p.p); w.f64 (p.rho); w.u32 (b.Id (ref));
		break; }
	case L_GetWindVector: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		double lng = r.f64 (), lat = r.f64 (), alt = r.f64 ();
		int frame = r.i32 ();
		VECTOR3 v = h && !r.bad ? oapiGetWindVector (h, lng, lat, alt, frame) : _V(0,0,0);
		w.v3 (v.data);
		break; }
	case L_GetCmdLine:
		PutStr (w, oapiGetCmdLine ());
		break;
	case L_GetObjectFileName: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		PutStr (w, h && !r.bad ? oapiGetObjectFileName (h) : nullptr);
		break; }
	case L_GetMeshFilename: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		PutStr (w, h && !r.bad ? oapiGetMeshFilename (h) : nullptr);
		break; }
	case L_GetObjectParam: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		DWORD type = r.u32 ();
		const void *p = h && !r.bad ? oapiGetObjectParam (h, type) : nullptr;
		uint32_t n = p ? ParamSize (type) : 0;
		w.u32 (n);
		if (n) w.raw (p, n);
		break; }
	case L_ElevationManager: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		w.u32 (b.Id (h && !r.bad ? oapiElevationManager (h) : nullptr));
		break; }
	case L_GetHUDMode: {
		HUDPARAM p {};
		int mode = oapiGetHUDMode (&p);
		w.i32 (mode); w.u32 (mode == HUD_ORBIT ? b.Id (p.HUDorbit.hRef) : 0); w.u32 (mode == HUD_DOCKING ? p.HUDdocking.NavIdx : 0);
		break; }
	case L_SetHUDMode: {
		int mode = r.i32 ();
		bool has = r.b ();
		OBJHANDLE ref = (OBJHANDLE)b.Ptr (r.u32 ());
		DWORD nav = r.u32 ();
		HUDPARAM p {};
		if (mode == HUD_ORBIT) p.HUDorbit.hRef = ref;
		else if (mode == HUD_DOCKING) p.HUDdocking.NavIdx = nav;
		w.b (!r.bad && oapiSetHUDMode (mode, has ? &p : nullptr));
		break; }
	case L_SetPanelBlink: {
		bool on = r.b ();
		VECTOR3 v[4];
		if (on) for (VECTOR3 &x : v) x = V3 (r);
		if (!r.bad) oapiSetPanelBlink (on ? v : 0);
		break; }
	case L_SimulateImmediateKey: {
		char ks[256];
		r.raw (ks, sizeof ks);
		bool running = r.b ();
		w.b (!r.bad && oapiSimulateImmediateKey (ks, running));
		break; }
	case L_ObjectVisuals: {
		uint32_t n = r.u32 ();
		if (r.bad || n > 65536) { r.bad = true; break; }
		std::vector<uint32_t> ids (n);
		for (uint32_t &id : ids) id = r.u32 ();
		if (r.bad) break;
		w.u32 (n);
		for (uint32_t id : ids) w.u32 (VisualOf (b, (OBJHANDLE)b.Ptr (id)));
		break; }
	case L_EditMeshGroupT: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		GROUPEDITSPEC ges {};
		DWORD idx = r.u32 ();
		ges.flags = r.u32 (); ges.UsrFlag = r.u32 (); ges.nVtx = r.u32 ();
		if (ges.nVtx > WB_MSG_MAX / sizeof (NTVERTEX)) { r.bad = true; break; }
		std::vector<NTVERTEX> vtx;
		std::vector<WORD> ix;
		if (r.b ()) { vtx.resize (ges.nVtx); r.raw (vtx.data (), ges.nVtx * sizeof (NTVERTEX)); }
		if (r.b ()) { ix.resize (ges.nVtx); r.raw (ix.data (), ges.nVtx * sizeof (WORD)); }
		if (r.bad) break;
		ges.Vtx = vtx.empty () ? nullptr : vtx.data ();
		ges.vIdx = ix.empty () ? nullptr : ix.data ();
		w.i32 (h ? oapiEditMeshGroup (h, idx, &ges) : -2);
		break; }
	case L_GetMeshGroup: {
		DEVMESHHANDLE h = (DEVMESHHANDLE)b.Ptr (r.u32 ());
		DWORD idx = r.u32 ();
		GROUPREQUESTSPEC grs {};
		grs.nVtx = r.u32 ();
		bool hv = r.b (), hvp = r.b ();
		if (grs.nVtx > WB_MSG_MAX / sizeof (NTVERTEX)) { r.bad = true; break; }
		std::vector<WORD> vperm (hvp ? grs.nVtx : 0), iperm;
		if (hvp) r.raw (vperm.data (), grs.nVtx * sizeof (WORD));
		grs.nIdx = r.u32 ();
		bool hi = r.b (), hip = r.b ();
		if (grs.nIdx > WB_MSG_MAX / sizeof (WORD)) { r.bad = true; break; }
		if (hip) { iperm.resize (grs.nIdx); r.raw (iperm.data (), grs.nIdx * sizeof (WORD)); }
		if (r.bad) break;
		std::vector<NTVERTEX> vtx (hv ? grs.nVtx : 0);
		std::vector<WORD> ix (hi ? grs.nIdx : 0);
		grs.Vtx = hv ? vtx.data () : nullptr; grs.VtxPerm = hvp ? vperm.data () : nullptr;
		grs.Idx = hi ? ix.data () : nullptr; grs.IdxPerm = hip ? iperm.data () : nullptr;
		int res = h ? oapiGetMeshGroup (h, idx, &grs) : -1;
		DWORD nv = hv ? std::min<DWORD> (grs.nVtx, (DWORD)vtx.size ()) : grs.nVtx, ni = hi ? std::min<DWORD> (grs.nIdx, (DWORD)ix.size ()) : grs.nIdx;
		w.i32 (res); w.u32 (nv); w.u32 (ni); w.u32 (grs.MtrlIdx); w.u32 (grs.TexIdx);
		w.b (hv); if (hv) w.raw (vtx.data (), nv * sizeof (NTVERTEX));
		w.b (hi); if (hi) w.raw (ix.data (), ni * sizeof (WORD));
		break; }
	case L_MeshMaterialEx: {
		DEVMESHHANDLE h = (DEVMESHHANDLE)b.Ptr (r.u32 ());
		DWORD idx = r.u32 ();
		MatProp prp = (MatProp)r.i32 ();
		bool set = r.b ();
		if (set) {
			bool has = r.b ();
			oapi::FVECTOR4 v;
			if (has) r.raw (&v, sizeof v);
			w.i32 (h && !r.bad ? oapiSetMaterialEx (h, idx, prp, has ? &v : nullptr) : 1);
		} else {
			oapi::FVECTOR4 v (0, 0, 0, 0);
			w.i32 (h && !r.bad ? oapiMeshMaterialEx (h, idx, prp, &v) : 1);
			w.raw (&v, sizeof v);
		}
		break; }
	case L_LoadMeshGlobalClbk: {
		std::string name = Str (r);
		uint32_t f = r.u32 ();
		if (r.bad) break;
		w.u32 (b.Id (WbLoadMeshGlobal (name.c_str (), f)));
		break; }
	}
	if (r.bad) b.BadArgs (fn);
	return true;
}
