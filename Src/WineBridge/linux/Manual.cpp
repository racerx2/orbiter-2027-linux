// WineBridge: the helper's hand-written calls of step 2 into Orbiter. Each reads all its arguments before it calls Orbiter or writes.
#include "Orbitersdk.h"
#include "Bridge.h"
#include "WbVessel.h"
#include "Manual.h"
#include <array>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using Guard = std::lock_guard<std::recursive_mutex>;

static VECTOR3 V3 (WbReader &r) { VECTOR3 v; r.v3 (v.data); return v; }
static std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }
static bool TooMany (WbReader &r, uint64_t n, uint64_t each) { if (n * each > WB_MSG_MAX) r.bad = true; return r.bad; }

// ---------------------------------------------------------------------
// VESSELSTATUS2

void WbPutStatus (WbWriter &w, const void *p)
{
	WbBridge &b = WbBridge::Get ();
	const VESSELSTATUS2 &s = *(const VESSELSTATUS2 *)p;
	w.u32 (s.version);
	if (s.version != 2) return;
	w.u32 (s.flag);
	w.u32 (b.Id (s.rbody)); w.u32 (b.Id (s.base)); w.i32 (s.port); w.i32 (s.status);
	w.v3 (s.rpos.data); w.v3 (s.rvel.data); w.v3 (s.vrot.data); w.v3 (s.arot.data);
	w.f64 (s.surf_lng); w.f64 (s.surf_lat); w.f64 (s.surf_hdg); w.u32 (s.xpdr);
	uint32_t nf = (s.flag & VS_FUELLIST) && s.fuel ? s.nfuel : 0;
	w.u32 (nf);
	for (uint32_t i = 0; i < nf; i++) { w.u32 (s.fuel[i].idx); w.f64 (s.fuel[i].level); }
	uint32_t nt = (s.flag & VS_THRUSTLIST) && s.thruster ? s.nthruster : 0;
	w.u32 (nt);
	for (uint32_t i = 0; i < nt; i++) { w.u32 (s.thruster[i].idx); w.f64 (s.thruster[i].level); }
	uint32_t nd = (s.flag & VS_DOCKINFOLIST) && s.dockinfo ? s.ndockinfo : 0;
	w.u32 (nd);
	for (uint32_t i = 0; i < nd; i++) { w.u32 (s.dockinfo[i].idx); w.u32 (s.dockinfo[i].ridx); w.u32 (b.Id (s.dockinfo[i].rvessel)); }
}

static bool Same (const VESSELSTATUS2::FUELSPEC &a, const VESSELSTATUS2::FUELSPEC &b) { return a.idx == b.idx && a.level == b.level; }
static bool Same (const VESSELSTATUS2::THRUSTSPEC &a, const VESSELSTATUS2::THRUSTSPEC &b) { return a.idx == b.idx && a.level == b.level; }
static bool Same (const VESSELSTATUS2::DOCKINFOSPEC &a, const VESSELSTATUS2::DOCKINFOSPEC &b) { return a.idx == b.idx && a.ridx == b.ridx && a.rvessel == b.rvessel; }

template <class T> static void SetList (bool used, DWORD &n, T *&list, const std::vector<T> &v)
{
	if (!used) return;
	bool same = v.size () == n && (v.empty () || list);
	for (size_t i = 0; same && i < v.size (); i++) same = Same (list[i], v[i]);
	if (same) return;
	if (n && list) delete []list;
	list = v.empty () ? nullptr : new T[v.size ()];
	for (size_t i = 0; i < v.size (); i++) list[i] = v[i];
	n = (DWORD)v.size ();
}

void WbGetStatus (WbReader &r, void *p)
{
	WbBridge &b = WbBridge::Get ();
	VESSELSTATUS2 &s = *(VESSELSTATUS2 *)p;
	VESSELSTATUS2 t {};
	t.version = r.u32 ();
	if (r.bad) return;
	if (t.version != 2) { s.version = t.version; return; }
	t.flag = r.u32 ();
	t.rbody = (OBJHANDLE)b.Ptr (r.u32 ()); t.base = (OBJHANDLE)b.Ptr (r.u32 ()); t.port = r.i32 (); t.status = r.i32 ();
	t.rpos = V3 (r); t.rvel = V3 (r); t.vrot = V3 (r); t.arot = V3 (r);
	t.surf_lng = r.f64 (); t.surf_lat = r.f64 (); t.surf_hdg = r.f64 (); t.xpdr = r.u32 ();
	std::vector<VESSELSTATUS2::FUELSPEC> f;
	std::vector<VESSELSTATUS2::THRUSTSPEC> th;
	std::vector<VESSELSTATUS2::DOCKINFOSPEC> d;
	uint32_t n = r.u32 ();
	if (!TooMany (r, n, 12)) { f.resize (n); for (auto &e : f) { e.idx = r.u32 (); e.level = r.f64 (); } }
	n = r.u32 ();
	if (!TooMany (r, n, 12)) { th.resize (n); for (auto &e : th) { e.idx = r.u32 (); e.level = r.f64 (); } }
	n = r.u32 ();
	if (!TooMany (r, n, 12)) { d.resize (n); for (auto &e : d) { e.idx = r.u32 (); e.ridx = r.u32 (); e.rvessel = (OBJHANDLE)b.Ptr (r.u32 ()); } }
	if (r.bad) return;
	s.version = t.version; s.flag = t.flag;
	s.rbody = t.rbody; s.base = t.base; s.port = t.port; s.status = t.status;
	s.rpos = t.rpos; s.rvel = t.rvel; s.vrot = t.vrot; s.arot = t.arot;
	s.surf_lng = t.surf_lng; s.surf_lat = t.surf_lat; s.surf_hdg = t.surf_hdg; s.xpdr = t.xpdr;
	SetList ((s.flag & VS_FUELLIST) != 0, s.nfuel, s.fuel, f);
	SetList ((s.flag & VS_THRUSTLIST) != 0, s.nthruster, s.thruster, th);
	SetList ((s.flag & VS_DOCKINFOLIST) != 0, s.ndockinfo, s.dockinfo, d);
}

void WbFreeStatus (void *p)
{
	VESSELSTATUS2 &s = *(VESSELSTATUS2 *)p;
	if (s.version != 2) return;
	delete []s.fuel; delete []s.thruster; delete []s.dockinfo;
	s.fuel = nullptr; s.thruster = nullptr; s.dockinfo = nullptr;
	s.nfuel = s.nthruster = s.ndockinfo = 0;
}

// a VESSELSTATUS2 from the wire, for one call
struct Status {
	VESSELSTATUS2 s {};
	Status (WbReader &r) { WbGetStatus (r, &s); }
	~Status () { WbFreeStatus (&s); }
};

void WbPutVS (WbWriter &w, const VESSELSTATUS &s)
{
	WbBridge &b = WbBridge::Get ();
	w.v3 (s.rpos.data); w.v3 (s.rvel.data); w.v3 (s.vrot.data); w.v3 (s.arot.data);
	w.f64 (s.fuel); w.f64 (s.eng_main); w.f64 (s.eng_hovr);
	w.u32 (b.Id (s.rbody)); w.u32 (b.Id (s.base)); w.i32 (s.port); w.i32 (s.status);
	for (int i = 0; i < 10; i++) w.v3 (s.vdata[i].data);
	for (int i = 0; i < 10; i++) w.f64 (s.fdata[i]);
	for (int i = 0; i < 10; i++) w.u32 (s.flag[i]);
}

void WbGetVS (WbReader &r, VESSELSTATUS &s)
{
	WbBridge &b = WbBridge::Get ();
	s.rpos = V3 (r); s.rvel = V3 (r); s.vrot = V3 (r); s.arot = V3 (r);
	s.fuel = r.f64 (); s.eng_main = r.f64 (); s.eng_hovr = r.f64 ();
	s.rbody = (OBJHANDLE)b.Ptr (r.u32 ()); s.base = (OBJHANDLE)b.Ptr (r.u32 ()); s.port = r.i32 (); s.status = r.i32 ();
	for (int i = 0; i < 10; i++) s.vdata[i] = V3 (r);
	for (int i = 0; i < 10; i++) s.fdata[i] = r.f64 ();
	for (int i = 0; i < 10; i++) s.flag[i] = r.u32 ();
}

// ---------------------------------------------------------------------
// live items: Orbiter reads these copies of the modules' memory every frame; the helper's messages update them

namespace {
	struct Live {
		int kind = 0;
		double d = 0;
		VECTOR3 v {};
		BEACONLIGHTSPEC beacon {};
		VECTOR3 bpos {}, bcol {};
		void *dst = nullptr; // bytes: the member of a proxy they go to (null once the proxy is gone)
		uint32_t n = 0;
	};
	// the state here is never destroyed: Orbiter's fast shutdown calls ExitModule after static objects are gone
	std::deque<Live> &g_live = *new std::deque<Live>;     // Orbiter keeps pointers into these: never moved, kept for the session
	std::vector<Live *> &g_byId = *new std::vector<Live *>; // by the helper's id
	const uint32_t BEACON_BYTES = 4 + 24 + 24 + 40 + 1;

	void SetValue (Live &l, const uint8_t *p, uint32_t n)
	{
		switch (l.kind) {
		case WB_LIVE_DOUBLE: if (n == 8) memcpy (&l.d, p, 8); break;
		case WB_LIVE_VECTOR3: if (n == 24) memcpy (l.v.data, p, 24); break;
		case WB_LIVE_BEACON:
			if (n == BEACON_BYTES) {
				uint32_t shape;
				double d[5];
				memcpy (&shape, p, 4);
				memcpy (l.bpos.data, p + 4, 24);
				memcpy (l.bcol.data, p + 28, 24);
				memcpy (d, p + 52, 40);
				l.beacon.shape = shape;
				l.beacon.size = d[0]; l.beacon.falloff = d[1]; l.beacon.period = d[2]; l.beacon.duration = d[3]; l.beacon.tofs = d[4];
				l.beacon.active = p[92] != 0;
			}
			break;
		case WB_LIVE_BYTES: if (l.dst && n == l.n) memcpy (l.dst, p, n); break;
		case WB_LIVE_STRING: { // the 2010 modules write Orbiter's debug line buffer itself
			char *dbg = oapiDebugString ();
			uint32_t k = n < 255 ? n : 255;
			memcpy (dbg, p, k);
			dbg[k] = 0;
			break; }
		}
	}

	// (u32 id, u32 n, bytes) as the registering call sends it
	Live *ReadLive (WbReader &r, int kind)
	{
		uint32_t id = r.u32 (), n = r.u32 ();
		if (r.bad || !id || id > (1u << 24) || n > 256) { r.bad = true; return nullptr; }
		uint8_t buf[256];
		r.raw (buf, n);
		if (r.bad) return nullptr;
		Live &l = g_live.emplace_back ();
		l.kind = kind;
		if (kind == WB_LIVE_BEACON) { l.beacon.pos = &l.bpos; l.beacon.col = &l.bcol; }
		if (kind != WB_LIVE_STRING) SetValue (l, buf, n); // as Orbiter's: asking for the debug buffer doesn't change it
		if (id >= g_byId.size ()) g_byId.resize (id + 1, nullptr);
		g_byId[id] = &l;
		return &l;
	}
}

// a live item of bytes that go to a proxy's member of n bytes: its id (0: bad arguments)
uint32_t WbLiveBytes (WbReader &r, void *dst, uint32_t n)
{
	uint32_t id = r.u32 (), len = r.u32 ();
	if (r.bad || !id || id > (1u << 24) || len != n || n > 256) { r.bad = true; return 0; }
	uint8_t buf[256];
	r.raw (buf, n);
	if (r.bad) return 0;
	Live &l = g_live.emplace_back ();
	l.kind = WB_LIVE_BYTES;
	l.dst = dst;
	l.n = n;
	SetValue (l, buf, n);
	if (id >= g_byId.size ()) g_byId.resize (id + 1, nullptr);
	g_byId[id] = &l;
	return id;
}

// the item whose value Orbiter reads at p (0: none)
uint32_t WbLiveIdOf (const void *p)
{
	if (!p) return 0;
	for (size_t i = 0; i < g_byId.size (); i++) {
		const Live *l = g_byId[i];
		if (l && (p == &l->d || p == &l->v || p == &l->beacon)) return (uint32_t)i;
	}
	return 0;
}

BEACONLIGHTSPEC *WbLiveBeacon (uint32_t id)
{
	Live *l = id < g_byId.size () ? g_byId[id] : nullptr;
	return l && l->kind == WB_LIVE_BEACON ? &l->beacon : nullptr;
}

// a live item of a double or a VECTOR3 as the registering call sends it: where its value is kept (null: bad arguments)
const void *WbLiveRef (WbReader &r, int kind)
{
	Live *l = ReadLive (r, kind);
	if (!l) return nullptr;
	return kind == WB_LIVE_DOUBLE ? (const void *)&l->d : (const void *)&l->v;
}

// the proxy is gone: its items write nowhere
void WbLiveRelease (uint32_t id)
{
	if (id < g_byId.size () && g_byId[id]) g_byId[id]->dst = nullptr;
}

void WbApplySync (const uint8_t *p, uint32_t n)
{
	while (n >= sizeof (WbSync)) {
		WbSync s;
		memcpy (&s, p, sizeof s);
		p += sizeof s; n -= sizeof s;
		if (s.len > n) return;
		if (s.id < g_byId.size () && g_byId[s.id]) SetValue (*g_byId[s.id], p, s.len);
		p += s.len; n -= s.len;
	}
}

// ---------------------------------------------------------------------
// particle stream specs Orbiter keeps a pointer to

static std::deque<PARTICLESTREAMSPEC> &g_pss = *new std::deque<PARTICLESTREAMSPEC>;

static PARTICLESTREAMSPEC *ReadPSS (WbReader &r)
{
	WbBridge &b = WbBridge::Get ();
	if (!r.b ()) return nullptr;
	PARTICLESTREAMSPEC s {};
	s.flags = r.u32 (); s.srcsize = r.f64 (); s.srcrate = r.f64 (); s.v0 = r.f64 (); s.srcspread = r.f64 ();
	s.lifetime = r.f64 (); s.growthrate = r.f64 (); s.atmslowdown = r.f64 ();
	s.ltype = (PARTICLESTREAMSPEC::LTYPE)r.i32 (); s.levelmap = (PARTICLESTREAMSPEC::LEVELMAP)r.i32 ();
	s.lmin = r.f64 (); s.lmax = r.f64 (); s.atmsmap = (PARTICLESTREAMSPEC::ATMSMAP)r.i32 ();
	s.amin = r.f64 (); s.amax = r.f64 (); s.tex = (SURFHANDLE)b.Ptr (r.u32 ());
	if (r.bad) return nullptr;
	return &g_pss.emplace_back (s);
}

// ---------------------------------------------------------------------
// exhausts: a level, position or direction is none (the thruster's), a constant Orbiter copies, or a live item

static void *ExhaustRef (WbReader &r, int kind, double &d, VECTOR3 &v)
{
	int mode = r.i32 ();
	if (mode == 1) {
		if (kind == WB_LIVE_DOUBLE) { d = r.f64 (); return &d; }
		v = V3 (r); return &v;
	}
	if (mode == 2) {
		Live *l = ReadLive (r, kind);
		if (!l) return nullptr;
		return kind == WB_LIVE_DOUBLE ? (void *)&l->d : (void *)&l->v;
	}
	if (mode != 0) r.bad = true;
	return nullptr;
}

// ---------------------------------------------------------------------
// SetLiftCoeffFunc has no context: a pool of functions, one per module function

namespace {
	const size_t LIFTS = 64;
	uint32_t g_liftFn[LIFTS];
	size_t g_nlift;

	double LiftCall (uint32_t fn, double aoa)
	{
		WbBridge &b = WbBridge::Get ();
		Guard g (b.Lock ());
		if (!b.Alive ()) return 0;
		WbWriter &w = b.Begin (H_LIFT);
		w.u32 (fn); w.f64 (aoa);
		WbReader r = b.Call ();
		double cl = r.f64 ();
		return r.bad ? 0 : cl;
	}
	template <size_t K> double Lift (double aoa) { return LiftCall (g_liftFn[K], aoa); }
	template <size_t... K> constexpr std::array<LiftCoeffFunc, sizeof... (K)> LiftTable (std::index_sequence<K...>) { return {{&Lift<K>...}}; }
	const std::array<LiftCoeffFunc, LIFTS> g_lift = LiftTable (std::make_index_sequence<LIFTS> ());

	LiftCoeffFunc LiftFor (uint32_t fn)
	{
		if (!fn) return nullptr;
		for (size_t i = 0; i < g_nlift; i++) if (g_liftFn[i] == fn) return g_lift[i];
		if (g_nlift == LIFTS) {
			static bool said;
			if (!said) { said = true; oapiWriteLogV ("WineBridge: more than %d lift functions (SetLiftCoeffFunc); the rest get none", (int)LIFTS); }
			return nullptr;
		}
		g_liftFn[g_nlift] = fn;
		return g_lift[g_nlift++];
	}
}

// oapiLoadMeshGlobal's callback has no context either: a pool, one function per module function

namespace {
	const size_t MESHCLBKS = 64;
	uint32_t g_meshFn[MESHCLBKS];
	size_t g_nmesh;

	void MeshLoaded (uint32_t fn, MESHHANDLE h, bool first)
	{
		WbBridge &b = WbBridge::Get ();
		Guard g (b.Lock ());
		if (!b.Alive ()) return;
		WbWriter &w = b.Begin (H_MESHLOADED);
		w.u32 (fn); w.u32 (b.Id (h)); w.b (first);
		b.Call ();
	}
	template <size_t K> void Loaded (MESHHANDLE h, bool first) { MeshLoaded (g_meshFn[K], h, first); }
	template <size_t... K> constexpr std::array<LoadMeshClbkFunc, sizeof... (K)> LoadedTable (std::index_sequence<K...>) { return {{&Loaded<K>...}}; }
	const std::array<LoadMeshClbkFunc, MESHCLBKS> g_loaded = LoadedTable (std::make_index_sequence<MESHCLBKS> ());
}


// ---------------------------------------------------------------------
// input boxes: the module's callback through a context

namespace {
	struct InputBox { uint32_t fn, usr, cancel = 0; }; // cancel: oapiOpenInputBoxEx's
	std::deque<InputBox> &g_boxes = *new std::deque<InputBox>;

	bool InputClbk (void *id, char *str, void *usrdata)
	{
		const InputBox *ib = (const InputBox *)usrdata;
		WbBridge &b = WbBridge::Get ();
		Guard g (b.Lock ());
		if (!b.Alive ()) return false;
		WbWriter &w = b.Begin (H_INPUTBOX);
		w.u32 (ib->fn); w.u32 (b.Id (id)); w.str (str); w.u32 (ib->usr);
		WbReader r = b.Call ();
		bool ok = r.b ();
		return !r.bad && ok;
	}
	bool InputCancel (void *id, char *str, void *usrdata)
	{
		InputBox ib = *(const InputBox *)usrdata;
		ib.fn = ib.cancel;
		return InputClbk (id, str, &ib);
	}
}

// ---------------------------------------------------------------------
// meshes: the helper's copies come back into Orbiter's, when that mesh still exists

namespace {
	std::unordered_set<const void *> &g_globalMeshes = *new std::unordered_set<const void *>; // oapiLoadMeshGlobal and oapiLoadMesh: loaded for the session
	bool g_meshCopies;

	bool MeshAlive (MESHHANDLE h)
	{
		if (!h) return false;
		if (g_globalMeshes.count (h)) return true;
		for (DWORD i = 0; i < oapiGetVesselCount (); i++) { // a vessel's mesh: while the vessel has it
			VESSEL *v = oapiGetVesselInterface (oapiGetVesselByIndex (i));
			if (!v) continue;
			for (UINT m = 0; m < v->GetMeshCount (); m++)
				if (v->GetMeshTemplate (m) == h) return true;
		}
		return false;
	}

	void Once (const char *what)
	{
		static std::unordered_set<std::string> &said = *new std::unordered_set<std::string>;
		if (said.insert (what).second) oapiWriteLogV ("WineBridge: %s", what);
	}
}

bool WbMeshCopies () { return g_meshCopies; }

// ---------------------------------------------------------------------
// the other vessels' callbacks a module calls through oapiGetVesselInterface (as H_CLBK's arguments)

static void ProxyClbk (VESSEL *v, uint32_t vid, uint32_t c, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	VESSEL2 *v2 = v && v->Version () >= 1 ? static_cast<VESSEL2 *> (v) : nullptr;
	VESSEL3 *v3 = v && v->Version () >= 2 ? static_cast<VESSEL3 *> (v) : nullptr;
	VESSEL4 *v4 = v && v->Version () >= 3 ? static_cast<VESSEL4 *> (v) : nullptr;
	switch (c) {
	case C_SetClassCaps: { FILEHANDLE f = (FILEHANDLE)b.Ptr (r.u32 ()); if (v2 && !r.bad) v2->clbkSetClassCaps (f); break; }
	case C_VisualCreated: { VISHANDLE vis = (VISHANDLE)b.Ptr (r.u32 ()); int rc = r.i32 (); if (v2 && !r.bad) v2->clbkVisualCreated (vis, rc); break; }
	case C_VisualDestroyed: { VISHANDLE vis = (VISHANDLE)b.Ptr (r.u32 ()); int rc = r.i32 (); if (v2 && !r.bad) v2->clbkVisualDestroyed (vis, rc); break; }
	case C_LoadPanel: { int id = r.i32 (); if (v2 && !r.bad) w.b (v2->clbkLoadPanel (id)); break; }
	case C_PanelMouseEvent2: {
		int id = r.i32 (), ev = r.i32 (), mx = r.i32 (), my = r.i32 ();
		if (v2 && !r.bad) w.b (v2->clbkPanelMouseEvent (id, ev, mx, my));
		break; }
	case C_PanelRedrawEvent2: {
		int id = r.i32 (), ev = r.i32 (); SURFHANDLE s = (SURFHANDLE)b.Ptr (r.u32 ());
		if (v2 && !r.bad) w.b (v2->clbkPanelRedrawEvent (id, ev, s));
		break; }
	case C_LoadVC: { int id = r.i32 (); if (v2 && !r.bad) w.b (v2->clbkLoadVC (id)); break; }
	case C_VCMouseEvent: {
		int id = r.i32 (), ev = r.i32 (); VECTOR3 p = V3 (r);
		if (v2 && !r.bad) { bool res = v2->clbkVCMouseEvent (id, ev, p); w.b (res); w.v3 (p.data); }
		break; }
	case C_VCRedrawEvent: {
		int id = r.i32 (), ev = r.i32 (); SURFHANDLE s = (SURFHANDLE)b.Ptr (r.u32 ());
		if (v2 && !r.bad) w.b (v2->clbkVCRedrawEvent (id, ev, s));
		break; }
	case C_PanelMouseEvent3: {
		int id = r.i32 (), ev = r.i32 (), mx = r.i32 (), my = r.i32 (); void *cx = b.Ptr (r.u32 ());
		if (v3 && !r.bad) w.b (v3->clbkPanelMouseEvent (id, ev, mx, my, cx));
		break; }
	case C_PanelRedrawEvent3: {
		int id = r.i32 (), ev = r.i32 (); SURFHANDLE s = (SURFHANDLE)b.Ptr (r.u32 ()); void *cx = b.Ptr (r.u32 ());
		if (v3 && !r.bad) w.b (v3->clbkPanelRedrawEvent (id, ev, s, cx));
		break; }
	case C_LoadPanel2D: {
		int id = r.i32 (); PANELHANDLE p = (PANELHANDLE)b.Ptr (r.u32 ()); DWORD vw = r.u32 (), vh = r.u32 ();
		if (v3 && !r.bad) w.b (v3->clbkLoadPanel2D (id, p, vw, vh));
		break; }
	case C_DrawHUD3: { // the sketchpad Orbiter lent the helper for the call this one is inside
		int mode = r.i32 (); HUDPAINTSPEC h; WbGetHps (r, &h); oapi::Sketchpad *s = WbSkpOf (r.u32 ());
		if (v3 && s && !r.bad) w.b (v3->clbkDrawHUD (mode, &h, s));
		break; }
	case C_RenderHUD: {
		int mode = r.i32 (); HUDPAINTSPEC h; WbGetHps (r, &h); SURFHANDLE tex = (SURFHANDLE)b.Ptr (r.u32 ());
		if (v3 && !r.bad) v3->clbkRenderHUD (mode, &h, tex);
		break; }
	case C_NavProcess: { int mode = r.i32 (); if (!r.bad) w.i32 (v4 ? v4->clbkNavProcess (mode) : mode); break; }
	case C_SaveState: { FILEHANDLE f = (FILEHANDLE)b.Ptr (r.u32 ()); if (v2 && !r.bad) v2->clbkSaveState (f); break; }
	case C_LoadStateEx: {
		FILEHANDLE f = (FILEHANDLE)b.Ptr (r.u32 ());
		Status s (r);
		if (v2 && !r.bad) { v2->clbkLoadStateEx (f, &s.s); WbPutStatus (w, &s.s); }
		break; }
	case C_SetStateEx: { Status s (r); if (v2 && !r.bad) v2->clbkSetStateEx (&s.s); break; }
	case C_PostCreation: if (v2) v2->clbkPostCreation (); break;
	case C_FocusChanged: {
		bool g = r.b (); OBJHANDLE n = (OBJHANDLE)b.Ptr (r.u32 ()), o = (OBJHANDLE)b.Ptr (r.u32 ());
		if (v2 && !r.bad) v2->clbkFocusChanged (g, n, o);
		break; }
	case C_PreStep: { double t = r.f64 (), dt = r.f64 (), mjd = r.f64 (); if (v2 && !r.bad) v2->clbkPreStep (t, dt, mjd); break; }
	case C_PostStep: { double t = r.f64 (), dt = r.f64 (), mjd = r.f64 (); if (v2 && !r.bad) v2->clbkPostStep (t, dt, mjd); break; }
	case C_PlaybackEvent: {
		double t = r.f64 (), et = r.f64 ();
		std::string ty = Str (r), ev = Str (r);
		if (v2 && !r.bad) w.b (v2->clbkPlaybackEvent (t, et, ty.c_str (), ev.c_str ()));
		break; }
	case C_RCSMode: { int m = r.i32 (); if (v2 && !r.bad) v2->clbkRCSMode (m); break; }
	case C_ADCtrlMode: { DWORD m = r.u32 (); if (v2 && !r.bad) v2->clbkADCtrlMode (m); break; }
	case C_HUDMode: { int m = r.i32 (); if (v2 && !r.bad) v2->clbkHUDMode (m); break; }
	case C_MFDMode: { int mfd = r.i32 (), m = r.i32 (); if (v2 && !r.bad) v2->clbkMFDMode (mfd, m); break; }
	case C_NavMode: { int m = r.i32 (); bool a = r.b (); if (v2 && !r.bad) v2->clbkNavMode (m, a); break; }
	case C_DockEvent: { int d = r.i32 (); OBJHANDLE m = (OBJHANDLE)b.Ptr (r.u32 ()); if (v2 && !r.bad) v2->clbkDockEvent (d, m); break; }
	case C_Animate: { double t = r.f64 (); if (v2 && !r.bad) v2->clbkAnimate (t); break; }
	case C_ConsumeDirectKey: {
		char ks[256];
		r.raw (ks, 256);
		if (v2 && !r.bad) { int res = v2->clbkConsumeDirectKey (ks); w.i32 (res); w.raw (ks, 256); }
		break; }
	case C_ConsumeBufferedKey: {
		DWORD key = r.u32 (); bool down = r.b ();
		char ks[256];
		r.raw (ks, 256);
		if (v2 && !r.bad) w.i32 (v2->clbkConsumeBufferedKey (key, down, ks));
		break; }
	case C_LoadGenericCockpit: if (v2) w.b (v2->clbkLoadGenericCockpit ()); break;
	case C_Generic: {
		int msg = r.i32 (), prm = r.i32 ();
		uint32_t cx = r.u32 ();
		bool remote = r.b (); // cx: the id of Linux's Lua state a module passed on (VMSG_LUAINTERPRETER, VMSG_LUAINSTANCE)
		bool helper = r.b (); // ... of a helper state (a Windows module's interpreter's)
		if (!v3 || r.bad) break;
		if (msg == VMSG_LUAINTERPRETER || msg == VMSG_LUAINSTANCE) { // a native module calls Linux's Lua: Linux's state, or a remote one
			bool err = false;
			int res = remote ? WbLuaNativeGeneric (v3, msg, prm, cx, &err) : helper ? WbLuaHelperGeneric (v3, msg, prm, cx, &err) : 0;
			w.i32 (res); w.b (err);
			break;
		}
		w.i32 (v3->clbkGeneric (msg, prm, (void *)(uintptr_t)cx)); // the module's own pointer: only its value means something here
		w.b (false);
		break; }
	case C_GetRadiationForce: {
		VECTOR3 mf = V3 (r), F = V3 (r), pos = V3 (r);
		if (v3 && !r.bad) { v3->clbkGetRadiationForce (mf, F, pos); w.v3 (F.data); w.v3 (pos.data); }
		break; }
	default:
		r.bad = true;
		break;
	}
}

// ---------------------------------------------------------------------

bool WbServeManual (uint32_t fn, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	if (fn != L_BaseLoadStateEx && fn != L_BaseSetStateEx && fn != L_PlanetAtmConstants && fn != L_OpenInputBoxEx && (fn < L_GetName || fn > L_LoadMesh)) return false;

	// calls that name no vessel
	switch (fn) {
	case L_MFDButtonLabel: {
		int mfd = r.i32 (), bt = r.i32 ();
		if (!r.bad) w.str (oapiMFDButtonLabel (mfd, bt));
		else b.BadArgs (fn);
		return true; }
	case L_PlanetAtmConstants: {
		static_assert (sizeof (ATMCONST) == 12 * 8, "ATMCONST: doubles only, as in the 32-bit SDK");
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		const ATMCONST *c = !r.bad && h ? oapiGetPlanetAtmConstants (h) : nullptr;
		w.b (c != nullptr);
		if (c) w.raw (c, sizeof *c);
		return true; }
	case L_VesselInfo: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		bool isv = !r.bad && h && oapiIsVessel (h);
		VESSEL *v = isv ? oapiGetVesselInterface (h) : nullptr;
		w.b (v != nullptr); w.i32 (v ? v->GetFlightModel () : 0); w.i32 (v ? v->Version () : 0); w.u32 (v ? WbHookClassOf (h, v) : 0);
		return true; }
	case L_CreateVessel: {
		std::string name = Str (r), cls = Str (r);
		VESSELSTATUS vs {};
		WbGetVS (r, vs);
		if (!r.bad) w.u32 (b.Id (oapiCreateVessel (name.c_str (), cls.c_str (), vs)));
		else b.BadArgs (fn);
		return true; }
	case L_CreateVesselEx: {
		std::string name = Str (r), cls = Str (r);
		Status s (r);
		if (!r.bad) w.u32 (b.Id (oapiCreateVesselEx (name.c_str (), cls.c_str (), &s.s)));
		else b.BadArgs (fn);
		return true; }
	case L_ReadScenarioNextline: {
		FILEHANDLE f = (FILEHANDLE)b.Ptr (r.u32 ());
		char *line = nullptr;
		bool ok = !r.bad && f && oapiReadScenario_nextline (f, line);
		w.b (ok); w.str (ok ? line : nullptr);
		return true; }
	case L_DebugString:
		if (!ReadLive (r, WB_LIVE_STRING)) b.BadArgs (fn);
		return true;
	case L_OpenInputBox: {
		std::string title = Str (r);
		uint32_t f = r.u32 ();
		uint32_t n;
		const char *s = r.str (&n);
		std::vector<char> buf;
		if (s) { buf.assign (s, s + n); buf.push_back (0); }
		int vislen = r.i32 ();
		uint32_t usr = r.u32 ();
		if (r.bad) { b.BadArgs (fn); return true; }
		InputBox &ib = g_boxes.emplace_back (InputBox {f, usr});
		oapiOpenInputBox (title.c_str (), f ? InputClbk : nullptr, s ? buf.data () : nullptr, vislen, &ib);
		return true; }
	case L_OpenInputBoxEx: {
		std::string title = Str (r);
		uint32_t f = r.u32 (), c = r.u32 ();
		uint32_t n;
		const char *s = r.str (&n);
		std::vector<char> buf;
		if (s) { buf.assign (s, s + n); buf.push_back (0); }
		int vislen = r.i32 ();
		uint32_t usr = r.u32 (), flags = r.u32 ();
		if (r.bad) { b.BadArgs (fn); return true; }
		InputBox &ib = g_boxes.emplace_back (InputBox {f, usr, c});
		oapiOpenInputBoxEx (title.c_str (), f ? InputClbk : nullptr, c ? InputCancel : nullptr, s ? buf.data () : nullptr, vislen, &ib, flags);
		return true; }
	case L_MeshGroup: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		DWORD idx = r.u32 ();
		MESHGROUPEX *g = !r.bad && h ? oapiMeshGroupEx (h, idx) : nullptr;
		w.b (g != nullptr);
		if (!g) return true;
		w.u32 (g->MtrlIdx); w.u32 (g->TexIdx); w.u32 (g->UsrFlag); w.u32 (g->zBias); w.u32 (g->Flags); w.u32 (g->TexIdxEx[0]); w.f64 (g->TexMixEx[0]);
		w.u32 (g->nVtx); w.u32 (g->nIdx);
		w.raw (g->Vtx, g->nVtx * sizeof (NTVERTEX));
		w.raw (g->Idx, g->nIdx * sizeof (WORD));
		g_meshCopies = true;
		return true; }
	case L_MeshGroupStore: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		DWORD idx = r.u32 ();
		DWORD mtrl = r.u32 (), tex = r.u32 (), usr = r.u32 (), zb = r.u32 (), fl = r.u32 (), tex1 = r.u32 ();
		float mix = (float)r.f64 ();
		DWORD nv = r.u32 (), ni = r.u32 ();
		std::vector<NTVERTEX> vtx;
		std::vector<WORD> ix;
		bool hv = r.b ();
		if (hv && !TooMany (r, nv, sizeof (NTVERTEX))) { vtx.resize (nv); r.raw (vtx.data (), nv * sizeof (NTVERTEX)); }
		bool hi = r.b ();
		if (hi && !TooMany (r, ni, sizeof (WORD))) { ix.resize (ni); r.raw (ix.data (), ni * sizeof (WORD)); }
		if (r.bad) { b.BadArgs (fn); return true; }
		MESHGROUPEX *g = MeshAlive (h) ? oapiMeshGroupEx (h, idx) : nullptr;
		if (!g) return true; // the mesh is gone: as the module's pointer into it would be
		g->MtrlIdx = mtrl; g->TexIdx = tex; g->UsrFlag = usr; g->zBias = (WORD)zb; g->Flags = (WORD)fl;
		g->TexIdxEx[0] = tex1; g->TexMixEx[0] = mix;
		if (hv) memcpy (g->Vtx, vtx.data (), std::min (nv, g->nVtx) * sizeof (NTVERTEX));
		if (hi) memcpy (g->Idx, ix.data (), std::min (ni, g->nIdx) * sizeof (WORD));
		if (nv > g->nVtx || ni > g->nIdx) Once ("a module made a mesh group larger than Orbiter's arrays; the rest is left out");
		if (nv < g->nVtx) g->nVtx = nv;
		if (ni < g->nIdx) g->nIdx = ni;
		return true; }
	case L_MeshMaterial: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		DWORD idx = r.u32 ();
		MATERIAL *m = !r.bad && h ? oapiMeshMaterial (h, idx) : nullptr;
		w.b (m != nullptr);
		if (m) { w.raw (m, sizeof *m); g_meshCopies = true; }
		return true; }
	case L_MeshMaterialStore: {
		MESHHANDLE h = (MESHHANDLE)b.Ptr (r.u32 ());
		DWORD idx = r.u32 ();
		MATERIAL mat;
		r.raw (&mat, sizeof mat);
		if (r.bad) { b.BadArgs (fn); return true; }
		MATERIAL *m = MeshAlive (h) ? oapiMeshMaterial (h, idx) : nullptr;
		if (m) *m = mat;
		return true; }
	case L_EditMeshGroup: {
		DEVMESHHANDLE h = (DEVMESHHANDLE)b.Ptr (r.u32 ());
		GROUPEDITSPEC ges {};
		DWORD idx = r.u32 ();
		ges.flags = r.u32 (); ges.UsrFlag = r.u32 (); ges.nVtx = r.u32 ();
		std::vector<NTVERTEX> vtx;
		std::vector<WORD> ix;
		bool hv = r.b ();
		if (hv && !TooMany (r, ges.nVtx, sizeof (NTVERTEX))) { vtx.resize (ges.nVtx); r.raw (vtx.data (), ges.nVtx * sizeof (NTVERTEX)); }
		bool hi = r.b ();
		if (hi && !TooMany (r, ges.nVtx, sizeof (WORD))) { ix.resize (ges.nVtx); r.raw (ix.data (), ges.nVtx * sizeof (WORD)); }
		if (r.bad) { b.BadArgs (fn); return true; }
		ges.Vtx = hv ? vtx.data () : nullptr;
		ges.vIdx = hi ? ix.data () : nullptr;
		w.i32 (h ? oapiEditMeshGroup (h, idx, &ges) : -2);
		return true; }
	case L_LoadMeshGlobal:
	case L_LoadMesh: {
		std::string name = Str (r);
		if (r.bad) { b.BadArgs (fn); return true; }
		MESHHANDLE m = fn == L_LoadMeshGlobal ? oapiLoadMeshGlobal (name.c_str ()) : oapiLoadMesh (name.c_str ());
		if (m) g_globalMeshes.insert (m);
		w.u32 (b.Id (m));
		return true; }
	default:
		break;
	}

	// calls on a vessel; a reply left empty reads as zeros on the helper's side
	uint32_t vid = r.u32 ();
	VESSEL *v = b.Vessel (vid);
	WbVessel *bv = b.Bridged (vid);
	bool bridgedOnly = false;
	switch (fn) {
	case L_BaseLoadStateEx: {
		bridgedOnly = true;
		FILEHANDLE scn = (FILEHANDLE)b.Ptr (r.u32 ());
		Status s (r);
		if (bv && !r.bad) { bv->BaseLoadStateEx (scn, &s.s); WbPutStatus (w, &s.s); }
		break; }
	case L_BaseSetStateEx: {
		bridgedOnly = true;
		Status s (r);
		if (bv && !r.bad) bv->BaseSetStateEx (&s.s);
		break; }
	case L_GetName: if (v) w.str (v->GetName ()); break;
	case L_GetClassName: if (v) w.str (v->GetClassName ()); break;
	case L_GetAttachmentId: {
		ATTACHMENTHANDLE a = (ATTACHMENTHANDLE)b.Ptr (r.u32 ());
		if (v && !r.bad) w.str (v->GetAttachmentId (a));
		break; }
	case L_ProxyClbk: {
		uint32_t c = r.u32 ();
		if (!r.bad) ProxyClbk (v, vid, c, r, w);
		break; }
	case L_GetStatus: {
		VESSELSTATUS vs {};
		if (v) { v->GetStatus (vs); WbPutVS (w, vs); }
		break; }
	case L_DefSetState: {
		VESSELSTATUS vs {};
		WbGetVS (r, vs);
		if (v && !r.bad) v->DefSetState (&vs);
		break; }
	case L_GetStatusEx: {
		VESSELSTATUS2 s {};
		s.version = 2;
		s.flag = r.u32 ();
		if (!v || r.bad) break;
		v->GetStatusEx (&s);
		w.u32 (b.Id (s.rbody)); w.u32 (b.Id (s.base)); w.i32 (s.port); w.i32 (s.status);
		w.v3 (s.rpos.data); w.v3 (s.rvel.data); w.v3 (s.vrot.data); w.v3 (s.arot.data);
		w.f64 (s.surf_lng); w.f64 (s.surf_lat); w.f64 (s.surf_hdg); w.u32 (s.xpdr);
		uint32_t nf = (s.flag & VS_FUELLIST) && s.fuel ? s.nfuel : 0;
		w.u32 (nf);
		for (uint32_t i = 0; i < nf; i++) { w.u32 (s.fuel[i].idx); w.f64 (s.fuel[i].level); }
		uint32_t nt = (s.flag & VS_THRUSTLIST) && s.thruster ? s.nthruster : 0;
		w.u32 (nt);
		for (uint32_t i = 0; i < nt; i++) { w.u32 (s.thruster[i].idx); w.f64 (s.thruster[i].level); }
		uint32_t nd = (s.flag & VS_DOCKINFOLIST) && s.dockinfo ? s.ndockinfo : 0;
		w.u32 (nd);
		for (uint32_t i = 0; i < nd; i++) { w.u32 (s.dockinfo[i].idx); w.u32 (s.dockinfo[i].ridx); w.u32 (b.Id (s.dockinfo[i].rvessel)); }
		WbFreeStatus (&s); // Orbiter allocated the lists with new[]
		break; }
	case L_DefSetStateEx: {
		Status s (r);
		if (v && !r.bad) v->DefSetStateEx (&s.s);
		break; }
	case L_ParseScenarioLineEx: {
		std::string line = Str (r);
		Status s (r);
		if (!v || r.bad) break;
		std::vector<char> buf (line.begin (), line.end ());
		buf.push_back (0);
		v->ParseScenarioLineEx (buf.data (), &s.s);
		WbPutStatus (w, &s.s);
		break; }
	case L_AddBeacon: {
		Live *l = ReadLive (r, WB_LIVE_BEACON);
		if (v && l && !r.bad) { v->AddBeacon (&l->beacon); w.b (true); }
		break; }
	case L_AddExhaustSpec: {
		EXHAUSTSPEC es {};
		double d0 = 0;
		VECTOR3 p0 {}, p1 {};
		es.th = (THRUSTER_HANDLE)b.Ptr (r.u32 ());
		es.lsize = r.f64 (); es.wsize = r.f64 (); es.lofs = r.f64 (); es.modulate = r.f64 ();
		es.tex = (SURFHANDLE)b.Ptr (r.u32 ()); es.flags = r.u32 ();
		es.level = (double *)ExhaustRef (r, WB_LIVE_DOUBLE, d0, p0);
		es.lpos = (VECTOR3 *)ExhaustRef (r, WB_LIVE_VECTOR3, d0, p0);
		es.ldir = (VECTOR3 *)ExhaustRef (r, WB_LIVE_VECTOR3, d0, p1);
		if (v && !r.bad) w.u32 (v->AddExhaust (&es));
		break; }
	case L_AddParticleStream: {
		PARTICLESTREAMSPEC *pss = ReadPSS (r);
		VECTOR3 pos = V3 (r), dir = V3 (r);
		Live *l = r.b () ? ReadLive (r, WB_LIVE_DOUBLE) : nullptr;
		if (v && !r.bad) w.u32 (b.Id (v->AddParticleStream (pss, pos, dir, l ? &l->d : nullptr)));
		break; }
	case L_AddReentryStream: {
		PARTICLESTREAMSPEC *pss = ReadPSS (r);
		if (v && !r.bad) w.u32 (b.Id (v->AddReentryStream (pss)));
		break; }
	case L_AddExhaustStream1: {
		THRUSTER_HANDLE th = (THRUSTER_HANDLE)b.Ptr (r.u32 ());
		PARTICLESTREAMSPEC *pss = ReadPSS (r);
		if (v && !r.bad) w.u32 (b.Id (v->AddExhaustStream (th, pss)));
		break; }
	case L_CreateVariableDragElement: {
		Live *l = ReadLive (r, WB_LIVE_DOUBLE);
		double factor = r.f64 ();
		VECTOR3 ref = V3 (r);
		if (v && l && !r.bad) { v->CreateVariableDragElement (&l->d, factor, ref); w.b (true); }
		break; }
	case L_DelExhaust: {
		UINT idx = r.u32 ();
		if (v && !r.bad) w.b (v->DelExhaust (idx));
		break; }
	case L_DelExhaustStream: {
		PSTREAM_HANDLE h = (PSTREAM_HANDLE)b.Ptr (r.u32 ());
		if (v && !r.bad) w.b (v->DelExhaustStream (h));
		break; }
	case L_CreateAirfoil: {
		bridgedOnly = true; // the function's context lives in the bridged vessel
		int align = r.i32 ();
		VECTOR3 ref = V3 (r);
		uint32_t cf = r.u32 ();
		double c = r.f64 (), S = r.f64 (), A = r.f64 ();
		if (bv && !r.bad) bv->Airfoil ((AIRFOIL_ORIENTATION)align, ref, cf, 0, c, S, A, A_Old);
		break; }
	case L_SetLiftCoeffFunc: {
		uint32_t f = r.u32 ();
		if (v && !r.bad) v->SetLiftCoeffFunc (LiftFor (f));
		break; }
	case L_AddPointLight:
	case L_AddSpotLight: {
		VECTOR3 pos = V3 (r), dir {};
		if (fn == L_AddSpotLight) dir = V3 (r);
		double range = r.f64 (), att0 = r.f64 (), att1 = r.f64 (), att2 = r.f64 (), umbra = 0, penumbra = 0;
		if (fn == L_AddSpotLight) { umbra = r.f64 (); penumbra = r.f64 (); }
		COLOUR4 col[3];
		r.raw (col, sizeof col);
		if (!v || r.bad) break;
		LightEmitter *l = fn == L_AddSpotLight ? v->AddSpotLight (pos, dir, range, att0, att1, att2, umbra, penumbra, col[0], col[1], col[2])
			: v->AddPointLight (pos, range, att0, att1, att2, col[0], col[1], col[2]);
		w.u32 (b.Id (l));
		break; }
	default:
		return false;
	}
	if (r.bad) b.BadArgs (fn);
	else if (!v || (bridgedOnly && !bv)) b.NoVessel (fn, vid);
	return true;
}

// with a module's callback, which Orbiter calls through the pool
MESHHANDLE WbLoadMeshGlobal (const char *name, uint32_t fn)
{
	LoadMeshClbkFunc f = nullptr;
	for (size_t i = 0; fn && i < g_nmesh; i++) if (g_meshFn[i] == fn) f = g_loaded[i];
	if (fn && !f && g_nmesh < MESHCLBKS) { g_meshFn[g_nmesh] = fn; f = g_loaded[g_nmesh++]; }
	else if (fn && !f) {
		static bool said;
		if (!said) { said = true; oapiWriteLogV ("WineBridge: more than %d mesh load callbacks (oapiLoadMeshGlobal); the rest get none", (int)MESHCLBKS); }
	}
	MESHHANDLE m = oapiLoadMeshGlobal (name, f);
	if (m) g_globalMeshes.insert (m);
	return m;
}

void WbManualClear ()
{
	g_live.clear ();
	g_byId.clear ();
	g_pss.clear ();
	g_nlift = 0;
	g_boxes.clear ();
	g_globalMeshes.clear ();
	g_meshCopies = false;
}
