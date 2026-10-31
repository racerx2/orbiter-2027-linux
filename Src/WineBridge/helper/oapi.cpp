// WineBridge helper: the oapi functions Orbiter.exe answers with its own data (atmosphere, wind, parameters, names, the visual slot,
// zlib, HUD modes, keys, meshes). A value a module gets a pointer to is the helper's copy, kept per object
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdlib.h>
#include <zlib.h>
#include "wb_helper.h"

static VECTOR3 V3r (WbReader &r) { VECTOR3 v; r.v3 (v.data); return v; }

namespace {
	// a copy per (object, key), as long as the helper runs
	struct Slot { uint32_t obj, key; uint8_t *p; uint32_t n; };
	struct Slots {
		Slot *e = 0;
		uint32_t n = 0, cap = 0;
		Slot *Find (uint32_t obj, uint32_t key) { for (uint32_t i = 0; i < n; i++) if (e[i].obj == obj && e[i].key == key) return &e[i]; return 0; }
		Slot *Get (uint32_t obj, uint32_t key, uint32_t size)
		{
			Slot *s = Find (obj, key);
			if (!s) {
				if (n == cap) { cap = cap ? cap * 2 : 16; e = (Slot *)realloc (e, cap * sizeof (Slot)); }
				s = &e[n++];
				*s = {obj, key, 0, 0};
			}
			if (size > s->n) { s->p = (uint8_t *)realloc (s->p, size); s->n = size; } // grows only: a module may keep the pointer
			return s;
		}
	};
	Slots g_strings, g_params, g_visuals;

	// a string the module gets a pointer to (null: none)
	const char *KeepStr (Slots &s, uint32_t obj, uint32_t key, WbReader &r)
	{
		bool has = r.b ();
		uint32_t n;
		const char *str = r.str (&n);
		if (r.bad || !has) return 0;
		Slot *k = s.Get (obj, key, n + 1);
		memcpy (k->p, str, n);
		k->p[n] = 0;
		return (const char *)k->p;
	}
}

void oapiGetAtm (OBJHANDLE hVessel, ATMPARAM *prm, OBJHANDLE *hAtmRef)
{
	wb::Begin (L_GetAtm).u32 (wb::Id (hVessel));
	WbReader r = wb::Call ();
	double T = r.f64 (), p = r.f64 (), rho = r.f64 ();
	uint32_t ref = r.u32 ();
	if (r.bad) return;
	prm->T = T; prm->p = p; prm->rho = rho;
	if (hAtmRef) *hAtmRef = (OBJHANDLE)wb::Ptr (ref);
}

// the 2010 name: the 2024 function without the wind speed
__declspec(dllexport) VECTOR3 oapiGetWindVector (OBJHANDLE hPlanet, double lng, double lat, double alt, int frame)
{
	WbWriter &w = wb::Begin (L_GetWindVector); w.u32 (wb::Id (hPlanet)); w.f64 (lng); w.f64 (lat); w.f64 (alt); w.i32 (frame);
	WbReader r = wb::Call ();
	VECTOR3 v = V3r (r);
	return r.bad ? _V(0,0,0) : v;
}

const char *oapiGetCmdLine ()
{
	wb::Begin (L_GetCmdLine);
	WbReader r = wb::Call ();
	return KeepStr (g_strings, 0, 0, r);
}

// Orbiter.exe's instance is the helper's (the process's image)
HINSTANCE oapiGetOrbiterInstance ()
{
	return GetModuleHandleA (0);
}

const char *oapiGetObjectFileName (OBJHANDLE hObj)
{
	wb::Begin (L_GetObjectFileName).u32 (wb::Id (hObj));
	WbReader r = wb::Call ();
	return KeepStr (g_strings, wb::Id (hObj), 1, r);
}

const char *oapiGetMeshFilename (MESHHANDLE hMesh)
{
	wb::Begin (L_GetMeshFilename).u32 (wb::Id (hMesh));
	WbReader r = wb::Call ();
	return KeepStr (g_strings, wb::Id (hMesh), 2, r);
}

// a copy of Orbiter's value of the type, as large as the type is (Linux knows it); none: null
const void *oapiGetObjectParam (OBJHANDLE hObj, DWORD paramtype)
{
	WbWriter &w = wb::Begin (L_GetObjectParam); w.u32 (wb::Id (hObj)); w.u32 (paramtype);
	WbReader r = wb::Call ();
	uint32_t n = r.u32 ();
	if (r.bad || !n || n > 256) return 0;
	Slot *s = g_params.Get (wb::Id (hObj), paramtype, n);
	r.raw (s->p, n);
	return r.bad ? 0 : s->p;
}

ELEVHANDLE oapiElevationManager (OBJHANDLE hPlanet)
{
	wb::Begin (L_ElevationManager).u32 (wb::Id (hPlanet));
	WbReader r = wb::Call ();
	return (ELEVHANDLE)wb::Ptr (r.u32 ());
}

// zlib here, as Orbiter.exe's
DWORD oapiDeflate (const BYTE *inp, DWORD ninp, BYTE *outp, DWORD noutp)
{
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	deflateInit (&strm, Z_DEFAULT_COMPRESSION);
	strm.avail_in = ninp;
	strm.next_in = (BYTE *)inp;
	strm.avail_out = noutp;
	strm.next_out = outp;
	int ret = deflate (&strm, Z_FINISH);
	DWORD ndata = ret == Z_STREAM_END ? noutp - strm.avail_out : 0;
	deflateEnd (&strm);
	return ndata;
}

DWORD oapiInflate (const BYTE *inp, DWORD ninp, BYTE *outp, DWORD noutp)
{
	uLongf ndata = noutp;
	if (uncompress (outp, &ndata, inp, ninp) != Z_OK) return 0;
	return ndata;
}

// the HUD's parameters: the reference body in orbit mode, the nav receiver in docking mode
int oapiGetHUDMode (HUDPARAM *prm)
{
	wb::Begin (L_GetHUDMode);
	WbReader r = wb::Call ();
	int mode = r.i32 ();
	uint32_t ref = r.u32 (), nav = r.u32 ();
	if (r.bad) return 0;
	if (prm) {
		if (mode == HUD_ORBIT) prm->HUDorbit.hRef = (OBJHANDLE)wb::Ptr (ref);
		else if (mode == HUD_DOCKING) prm->HUDdocking.NavIdx = nav;
	}
	return mode;
}

bool oapiSetHUDMode (int mode, const HUDPARAM *prm)
{
	WbWriter &w = wb::Begin (L_SetHUDMode); w.i32 (mode); w.b (prm != 0);
	w.u32 (prm && mode == HUD_ORBIT ? wb::Id (prm->HUDorbit.hRef) : 0); w.u32 (prm && mode == HUD_DOCKING ? prm->HUDdocking.NavIdx : 0);
	WbReader r = wb::Call ();
	return r.b ();
}

void oapiSetPanelBlink (VECTOR3 v[4])
{
	WbWriter &w = wb::Begin (L_SetPanelBlink);
	w.b (v != 0);
	if (v) for (int i = 0; i < 4; i++) w.v3 (v[i].data);
	wb::Call ();
}

bool oapiSimulateImmediateKey (char kstate[256], bool onRunningOnly)
{
	WbWriter &w = wb::Begin (L_SimulateImmediateKey); w.raw (kstate, 256); w.b (onRunningOnly);
	WbReader r = wb::Call ();
	return r.b ();
}

// ---------------------------------------------------------------------
// the body's visual slot: Orbiter sets it while the visual exists; the helper's slot gets it before each call from Linux

VISHANDLE *oapiObjectVisualPtr (OBJHANDLE hObject)
{
	uint32_t id = wb::Id (hObject);
	WbWriter &w = wb::Begin (L_ObjectVisuals); w.u32 (1); w.u32 (id);
	WbReader r = wb::Call ();
	uint32_t n = r.u32 (), vis = r.u32 ();
	if (r.bad || n != 1) return 0;
	Slot *s = g_visuals.Get (id, 0, sizeof (VISHANDLE));
	*(VISHANDLE *)s->p = (VISHANDLE)wb::Ptr (vis);
	return (VISHANDLE *)s->p;
}

bool wb::VisualsWanted ()
{
	return g_visuals.n != 0;
}

void wb::VisualsRefresh ()
{
	WbWriter &w = wb::Begin (L_ObjectVisuals); w.u32 (g_visuals.n);
	for (uint32_t i = 0; i < g_visuals.n; i++) w.u32 (g_visuals.e[i].obj);
	WbReader r = wb::Call ();
	uint32_t n = r.u32 ();
	for (uint32_t i = 0; i < n && i < g_visuals.n && !r.bad; i++) {
		uint32_t vis = r.u32 ();
		if (!r.bad) *(VISHANDLE *)g_visuals.e[i].p = (VISHANDLE)wb::Ptr (vis);
	}
}

// ---------------------------------------------------------------------
// meshes

// a template mesh: the module's pending copies go first, its copy of the group is read again after
int oapiEditMeshGroup (MESHHANDLE hMesh, DWORD grpidx, GROUPEDITSPEC *ges)
{
	wb::MeshUse (hMesh);
	WbWriter &w = wb::Begin (L_EditMeshGroupT); w.u32 (wb::Id (hMesh)); w.u32 (grpidx);
	w.u32 (ges->flags); w.u32 (ges->UsrFlag); w.u32 (ges->nVtx);
	w.b (ges->Vtx != 0); if (ges->Vtx) w.raw (ges->Vtx, ges->nVtx * sizeof (NTVERTEX));
	w.b (ges->vIdx != 0); if (ges->vIdx) w.raw (ges->vIdx, ges->nVtx * sizeof (WORD));
	WbReader r = wb::Call ();
	int res = r.i32 ();
	if (!r.bad) wb::MeshRefresh (wb::Id (hMesh), grpidx);
	return res;
}

// as Orbiter's: the client's; the arrays the module gave are filled (with its permutations, if given)
int oapiGetMeshGroup (DEVMESHHANDLE hMesh, DWORD grpidx, GROUPREQUESTSPEC *grs)
{
	WbWriter &w = wb::Begin (L_GetMeshGroup); w.u32 (wb::Id (hMesh)); w.u32 (grpidx);
	w.u32 (grs->nVtx); w.b (grs->Vtx != 0); w.b (grs->VtxPerm != 0);
	if (grs->VtxPerm) w.raw (grs->VtxPerm, grs->nVtx * sizeof (WORD));
	w.u32 (grs->nIdx); w.b (grs->Idx != 0); w.b (grs->IdxPerm != 0);
	if (grs->IdxPerm) w.raw (grs->IdxPerm, grs->nIdx * sizeof (WORD));
	WbReader r = wb::Call ();
	int res = r.i32 ();
	DWORD nv = r.u32 (), ni = r.u32 (), mtrl = r.u32 (), tex = r.u32 ();
	if (r.bad) return -1;
	if (grs->Vtx && r.b ()) r.raw (grs->Vtx, nv * sizeof (NTVERTEX));
	if (grs->Idx && r.b ()) r.raw (grs->Idx, ni * sizeof (WORD));
	grs->nVtx = nv; grs->nIdx = ni; grs->MtrlIdx = mtrl; grs->TexIdx = tex;
	return res;
}

// as Orbiter's: a device mesh gives no group data
MESHGROUP *oapiMeshGroup (DEVMESHHANDLE hMesh, DWORD idx)
{
	return 0;
}

int oapiMeshMaterialEx (DEVMESHHANDLE hMesh, DWORD idx, MatProp prp, oapi::FVECTOR4 *out)
{
	WbWriter &w = wb::Begin (L_MeshMaterialEx); w.u32 (wb::Id (hMesh)); w.u32 (idx); w.i32 ((int)prp); w.b (false);
	WbReader r = wb::Call ();
	int res = r.i32 ();
	oapi::FVECTOR4 v;
	r.raw (&v, sizeof v);
	if (!r.bad && out) *out = v;
	return r.bad ? 1 : res;
}

int oapiSetMaterialEx (DEVMESHHANDLE hMesh, DWORD idx, MatProp prp, const oapi::FVECTOR4 *in)
{
	WbWriter &w = wb::Begin (L_MeshMaterialEx); w.u32 (wb::Id (hMesh)); w.u32 (idx); w.i32 ((int)prp); w.b (true);
	w.b (in != 0); if (in) w.raw (in, sizeof *in);
	WbReader r = wb::Call ();
	return r.bad ? 1 : r.i32 ();
}

// the callback is the module's: Orbiter calls Linux's, which calls it (H_MESHLOADED) for this load and later loads of the mesh
const MESHHANDLE oapiLoadMeshGlobal (const char *fname, LoadMeshClbkFunc fClbk)
{
	WbWriter &w = wb::Begin (L_LoadMeshGlobalClbk); w.str (fname); w.u32 (wb::Id ((const void *)fClbk));
	WbReader r = wb::Call ();
	return (MESHHANDLE)wb::Ptr (r.u32 ());
}
