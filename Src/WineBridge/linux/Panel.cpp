// WineBridge: meshes a Windows module makes, its HUD meshes, its VESSEL3 2D panels, its airfoils' other forms and nav data. Each call
// reads all its arguments before it calls Orbiter or writes its reply
#include "Orbitersdk.h"
#include "GraphicsAPI.h"
#include "Bridge.h"
#include "WbVessel.h"
#include "Manual.h"
#include <algorithm>
#include <vector>

static VECTOR3 V3 (WbReader &r) { VECTOR3 v; r.v3 (v.data); return v; }
static RECT Rect (WbReader &r) { RECT q; q.left = r.i32 (); q.top = r.i32 (); q.right = r.i32 (); q.bottom = r.i32 (); return q; }

namespace {
	// a group as the helper sends it, its arrays kept here while Orbiter copies them
	struct Group {
		MESHGROUP g {};
		std::vector<NTVERTEX> vtx;
		std::vector<WORD> idx;
		bool Read (WbReader &r)
		{
			g.MtrlIdx = r.u32 (); g.TexIdx = r.u32 (); g.zBias = (WORD)r.u32 ();
			DWORD nv = r.u32 (), ni = r.u32 ();
			if (r.bad || (uint64_t)nv * sizeof (NTVERTEX) + (uint64_t)ni * sizeof (WORD) > WB_MSG_MAX) { r.bad = true; return false; }
			if (r.b ()) { vtx.resize (nv); r.raw (vtx.data (), nv * sizeof (NTVERTEX)); }
			if (r.b ()) { idx.resize (ni); r.raw (idx.data (), ni * sizeof (WORD)); }
			g.nVtx = nv; g.nIdx = ni;
			g.Vtx = vtx.empty () ? nullptr : vtx.data ();
			g.Idx = idx.empty () ? nullptr : idx.data ();
			return !r.bad;
		}
	};
}

bool WbServePanel (uint32_t fn, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	switch (fn) {
	case L_CreateMesh: {
		uint32_t n = r.u32 ();
		if (r.bad || n > 65536) { b.BadArgs (fn); w.u32 (0); return true; }
		std::vector<Group> grp (n);
		for (Group &g : grp) if (!g.Read (r)) break;
		if (r.bad) { b.BadArgs (fn); w.u32 (0); return true; }
		std::vector<MESHGROUP> arr;
		for (Group &g : grp) arr.push_back (g.g);
		w.u32 (b.Id (oapiCreateMesh (n, arr.data ())));
		return true; }
	case L_AddMeshGroup: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		Group g;
		if (!g.Read (r) || !h) { if (r.bad) b.BadArgs (fn); w.u32 (0); return true; }
		w.u32 (oapiAddMeshGroup (h, &g.g));
		return true; }
	case L_AddMeshGroupBlock: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		DWORD grp = r.u32 (), nv = r.u32 (), ni = r.u32 ();
		std::vector<NTVERTEX> vtx;
		std::vector<WORD> idx;
		if (!r.bad && (uint64_t)nv * sizeof (NTVERTEX) + (uint64_t)ni * sizeof (WORD) > WB_MSG_MAX) r.bad = true;
		bool hv = r.b ();
		if (hv && !r.bad) { vtx.resize (nv); r.raw (vtx.data (), nv * sizeof (NTVERTEX)); }
		bool hi = r.b ();
		if (hi && !r.bad) { idx.resize (ni); r.raw (idx.data (), ni * sizeof (WORD)); }
		if (r.bad) { b.BadArgs (fn); w.b (false); return true; }
		w.b (h && oapiAddMeshGroupBlock (h, grp, hv ? vtx.data () : nullptr, nv, hi ? idx.data () : nullptr, ni));
		return true; }
	case L_DeleteMesh: { // not upstream (F2): a mesh only, its id retired
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 (), WK_MESHHANDLE);
		if (h && !r.bad) { oapiDeleteMesh (h); WbMeshGone (h); b.Retire (h); }
		return true; }
	case L_MeshTexCount: { // the entries clbkRender2DPanel reads: hSurf[TexIdx] for the groups it draws with one
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		DWORD n = 0;
		for (DWORD i = 0, k = h && !r.bad ? oapiMeshGroupCount (h) : 0; i < k; i++) {
			MESHGROUP *g = oapiMeshGroup (h, i);
			if (g && !(g->UsrFlag & 2) && g->TexIdx < TEXIDX_MFD0) n = std::max<DWORD> (n, g->TexIdx + 1);
		}
		w.u32 (n);
		return true; }
	case L_RenderHUD: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		bool has = r.b ();
		uint32_t n = r.u32 ();
		if (r.bad || n > 4096) { b.BadArgs (fn); return true; }
		std::vector<SURFHANDLE> tex (n + 1, nullptr); // never an empty array for a module that gave one
		for (uint32_t i = 0; i < n; i++) tex[i] = (SURFHANDLE)b.Ptr (r.u32 ());
		if (h && !r.bad) oapiRenderHUD (h, has ? tex.data () : nullptr);
		return true; }
	case L_GetNavData: {
		NAVHANDLE h = (NAVHANDLE)b.Ptr (r.u32 ());
		NAVDATA d {};
		if (h && !r.bad) oapiGetNavData (h, &d);
		uint32_t handle = 0, dock = 0;
		double x = 0, y = 0;
		switch (d.type) {
		case TRANSMITTER_VOR: handle = b.Id (d.vor.hPlanet); x = d.vor.lng; y = d.vor.lat; break;
		case TRANSMITTER_VTOL: handle = b.Id (d.vtol.hBase); x = d.vtol.npad; break;
		case TRANSMITTER_ILS: handle = b.Id (d.ils.hBase); x = d.ils.appdir; break;
		case TRANSMITTER_IDS: handle = b.Id (d.ids.hVessel); dock = b.Id (d.ids.hDock); break;
		case TRANSMITTER_XPDR: handle = b.Id (d.xpdr.hVessel); break;
		}
		w.u32 (d.type); w.u32 (d.ch); w.f64 (d.power); w.str (d.descr); w.u32 (handle); w.f64 (x); w.f64 (y); w.u32 (dock);
		return true; }
	case L_SetPanelBackground: case L_SetPanelScaling: case L_RegisterPanelArea:
	case L_CreateAirfoil2: case L_CreateAirfoil4: case L_EditAirfoil: case L_GetAirfoilParam:
		break;
	default:
		return false;
	}

	// calls on the module's own vessel; a reply left empty reads as zeros on the helper's side
	uint32_t vid = r.u32 ();
	VESSEL *v = b.Vessel (vid);
	WbVessel *bv = b.Bridged (vid);
	switch (fn) {
	case L_SetPanelBackground: {
		PANELHANDLE hp = (PANELHANDLE)b.Ptr (r.u32 ());
		uint32_t n = r.u32 ();
		if (r.bad || n > 4096) { b.BadArgs (fn); return true; }
		std::vector<SURFHANDLE> surf (n + 1, nullptr);
		for (uint32_t i = 0; i < n; i++) surf[i] = (SURFHANDLE)b.Ptr (r.u32 ());
		MESHHANDLE mesh = (MESHHANDLE)b.Ptr (r.u32 ());
		DWORD width = r.u32 (), height = r.u32 (), base = r.u32 (), scroll = r.u32 ();
		if (bv && !r.bad) w.i32 (bv->SetPanelBackground (hp, n ? surf.data () : nullptr, n, mesh, width, height, base, scroll));
		break; }
	case L_SetPanelScaling: {
		PANELHANDLE hp = (PANELHANDLE)b.Ptr (r.u32 ());
		double d = r.f64 (), e = r.f64 ();
		if (bv && !r.bad) w.i32 (bv->SetPanelScaling (hp, d, e));
		break; }
	case L_RegisterPanelArea: {
		PANELHANDLE hp = (PANELHANDLE)b.Ptr (r.u32 ());
		int id = r.i32 ();
		RECT pos = Rect (r);
		int form = r.i32 ();
		if (form == PA_TexRect) {
			RECT tex = Rect (r);
			int draw = r.i32 (), mouse = r.i32 (), bk = r.i32 ();
			if (bv && !r.bad) w.i32 (static_cast<VESSEL3 *> (bv)->RegisterPanelArea (hp, id, pos, tex, draw, mouse, bk));
		} else if (form == PA_Surface) {
			int draw = r.i32 (), mouse = r.i32 ();
			SURFHANDLE surf = (SURFHANDLE)b.Ptr (r.u32 ());
			uint32_t cx = r.u32 ();
			if (bv && !r.bad) w.i32 (static_cast<VESSEL3 *> (bv)->RegisterPanelArea (hp, id, pos, draw, mouse, surf, bv->PanelContext (cx)));
		} else if (form == PA_TexIdx) {
			int texidx = r.i32 ();
			RECT tex = Rect (r);
			int draw = r.i32 (), mouse = r.i32 (), bk = r.i32 ();
			if (bv && !r.bad) w.i32 (bv->VESSEL4::RegisterPanelArea (hp, id, pos, texidx, tex, draw, mouse, bk));
		} else r.bad = true;
		break; }
	case L_CreateAirfoil2: {
		int align = r.i32 ();
		VECTOR3 ref = V3 (r);
		uint32_t cf = r.u32 ();
		double c = r.f64 (), S = r.f64 (), A = r.f64 ();
		if (bv && !r.bad) w.u32 (b.Id (bv->Airfoil ((AIRFOIL_ORIENTATION)align, ref, cf, 0, c, S, A, A_Old)));
		break; }
	case L_CreateAirfoil4: {
		VECTOR3 ref = V3 (r);
		uint32_t cf = r.u32 (), cx = r.u32 ();
		double c = r.f64 (), S = r.f64 (), A = r.f64 ();
		if (bv && !r.bad) w.u32 (b.Id (bv->Airfoil4 (ref, cf, cx, c, S, A)));
		break; }
	case L_EditAirfoil: { // the function goes to the bridge's record: Orbiter keeps calling the bridge's
		AIRFOILHANDLE h = (AIRFOILHANDLE)b.Ptr (r.u32 ());
		DWORD flag = r.u32 ();
		VECTOR3 ref = V3 (r);
		uint32_t cf = r.u32 ();
		double c = r.f64 (), S = r.f64 (), A = r.f64 ();
		if (!bv || r.bad || !h) break;
		bv->EditAirfoil (h, flag & ~0x02u, ref, nullptr, c, S, A);
		WbVessel::AirfoilContext *a = flag & 0x02 ? bv->AirfoilRecord (h) : nullptr;
		if (a) a->func = cf;
		break; }
	case L_GetAirfoilParam: {
		AIRFOILHANDLE h = (AIRFOILHANDLE)b.Ptr (r.u32 ());
		VECTOR3 ref {};
		AirfoilCoeffFunc cf = nullptr;
		void *cx = nullptr;
		double c = 0, S = 0, A = 0;
		bool ok = v && h && !r.bad && v->GetAirfoilParam (h, &ref, &cf, &cx, &c, &S, &A);
		WbVessel::AirfoilContext *a = ok && bv ? bv->AirfoilRecord (h) : nullptr; // another vessel's functions are Linux code: none
		w.b (ok); w.v3 (ref.data); w.u32 (a ? a->func : 0); w.u32 (a ? a->context : 0); w.f64 (c); w.f64 (S); w.f64 (A);
		break; }
	}
	if (r.bad) b.BadArgs (fn);
	else if (!v || (!bv && fn != L_GetAirfoilParam)) b.NoVessel (fn, vid);
	return true;
}
