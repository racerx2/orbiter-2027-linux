// WineBridge: Windows celestial body and atmosphere modules. Orbiter gets a proxy for each object of the helper's (a CELBODY2, an
// ATMOSPHERE): its virtual methods call the object's where its class overrides them, and CELBODY2's own methods the object calls run
// on the proxy here, which holds the body's atmosphere as Orbiter's object of a native module does
#include "Orbitersdk.h"
#include "CelBodyAPI.h"
#include "Bridge.h"
#include "Manual.h"
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using Guard = std::lock_guard<std::recursive_mutex>;

static_assert (sizeof (ATMCONST) == 12 * 8 && sizeof (ATMPARAM) == 3 * 8 && sizeof (ATMOSPHERE::PRM_OUT) == 3 * 8, "doubles only, as in the 32-bit SDK");

static std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }

// the helper the object came from is still the one running
static bool Live (uint64_t session) { WbBridge &b = WbBridge::Get (); return b.Alive () && session == b.Session (); }

#define WB_LEGACY 64 // the 2006 interface's functions bridged at once, of each kind

namespace {
	struct CbAccess : CELBODY2 {
		static void *AtmModule (CELBODY2 *c) { return c->*(&CbAccess::hAtmModule); }
	};

	void PutIn (WbWriter &w, const ATMOSPHERE::PRM_IN *in)
	{
		w.f64 (in->alt); w.f64 (in->lng); w.f64 (in->lat); w.f64 (in->f107bar); w.f64 (in->f107); w.f64 (in->ap); w.u32 (in->flag);
	}
	void GetIn (WbReader &r, ATMOSPHERE::PRM_IN *in)
	{
		in->alt = r.f64 (); in->lng = r.f64 (); in->lat = r.f64 (); in->f107bar = r.f64 (); in->f107 = r.f64 (); in->ap = r.f64 ();
		in->flag = r.u32 ();
	}

	class WbAtmosphere;
	class WbCelbody;
	std::unordered_map<const ATMOSPHERE *, WbAtmosphere *> &g_atms = *new std::unordered_map<const ATMOSPHERE *, WbAtmosphere *>;
	std::unordered_map<const CELBODY *, WbCelbody *> &g_bodies = *new std::unordered_map<const CELBODY *, WbCelbody *>;
	CELBODY2 *g_creating; // the body a Windows atmosphere module's CreateAtmosphere runs for: not in the system yet while it initialises

	bool IsGbody (OBJHANDLE h)
	{
		if (!h) return false;
		DWORD n = oapiGetGbodyCount ();
		for (DWORD i = 0; i < n; i++) if (oapiGetGbodyByIndex ((int)i) == h) return true;
		return false;
	}

	// Orbiter's object for a body the helper names: one of the system's, or the one being given an atmosphere
	CELBODY *BodyObject (OBJHANDLE h)
	{
		if (g_creating && h && g_creating->GetHandle () == h) return g_creating;
		return IsGbody (h) ? oapiGetCelbodyInterface (h) : nullptr;
	}

	// a module's atmosphere object: the virtual methods its class overrides go to it, the others are ATMOSPHERE's
	class WbAtmosphere final : public ATMOSPHERE {
	public:
		WbAtmosphere (CELBODY2 *body, uint32_t obj, uint32_t over, int module, uint64_t session)
		: ATMOSPHERE (body), obj (obj), over (over), module (module), session (session) { g_atms[this] = this; }
		~WbAtmosphere () { g_atms.erase (this); }
		static WbAtmosphere *Of (const ATMOSPHERE *a)
		{
			auto it = a ? g_atms.find (a) : g_atms.end ();
			return it == g_atms.end () ? nullptr : it->second;
		}
		uint32_t obj, over;
		int module;        // the helper's module whose code made the object
		uint64_t session;
		bool gone = false; // that module is freed
		bool Bridged () const { return !gone && Live (session); }

		const char *clbkName () const override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (A_Name)) return ""; // pure in ATMOSPHERE: the module's class has one
			Begin (A_Name);
			WbReader r = b.Call ();
			uint32_t n;
			const char *s = r.str (&n);
			name = s && !r.bad ? std::string (s, n) : std::string ();
			return name.c_str ();
		}
		bool clbkConstants (ATMCONST *c) const override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (A_Constants)) return ATMOSPHERE::clbkConstants (c);
			Begin (A_Constants).raw (c, sizeof *c);
			WbReader r = b.Call ();
			bool ok = r.b ();
			ATMCONST v;
			r.raw (&v, sizeof v);
			if (r.bad) return false;
			*c = v;
			return ok;
		}
		bool clbkParams (const PRM_IN *in, PRM_OUT *out) override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (A_Params)) return ATMOSPHERE::clbkParams (in, out);
			WbWriter &w = Begin (A_Params);
			PutIn (w, in); w.raw (out, sizeof *out);
			WbReader r = b.Call ();
			bool ok = r.b ();
			PRM_OUT v;
			r.raw (&v, sizeof v);
			if (r.bad) return false;
			*out = v;
			return ok;
		}
	private:
		mutable std::string name;
		bool Fw (int c) const { return Bridged () && (over >> c & 1u); }
		WbWriter &Begin (int c) const { WbWriter &w = WbBridge::Get ().Begin (H_CELBODY); w.i32 (K_Atm); w.u32 (obj); w.i32 (c); return w; }
	};

	// the helper's object freed (its module's DeleteAtmosphere, module 0: as Orbiter's delete), then its proxy
	void AtmDrop (WbAtmosphere *a, int module)
	{
		WbBridge &b = WbBridge::Get ();
		if (a->Bridged ()) {
			WbWriter &w = b.Begin (H_CELBODY);
			w.i32 (K_AtmDelete); w.i32 (module); w.u32 (a->obj);
			b.Call ();
		}
		delete a;
	}

	// the proxy of a module's atmosphere object: the one there is, else a new one
	WbAtmosphere *AtmProxy (CELBODY2 *body, uint32_t obj, uint32_t over, int module)
	{
		for (auto &e : g_atms) if (e.second->obj == obj && e.second->Bridged ()) return e.second;
		return new WbAtmosphere (body, obj, over, module, WbBridge::Get ().Session ());
	}

	// a module's object for a body: Orbiter's CELBODY, a CELBODY2 by the object's version
	class WbCelbody final : public CELBODY2 {
	public:
		WbCelbody (OBJHANDLE h, int module, uint64_t session) : CELBODY2 (h), module (module), session (session) { g_bodies[this] = this; }
		~WbCelbody () override { FreeAtm (); g_bodies.erase (this); } // ~CELBODY2 finds no atmosphere left
		static WbCelbody *Of (const CELBODY *c)
		{
			auto it = c ? g_bodies.find (c) : g_bodies.end ();
			return it == g_bodies.end () ? nullptr : it->second;
		}
		uint32_t obj = 0, over = 0;
		int module;
		uint64_t session;
		bool Bridged () const { return obj && Live (session); }
		void Set (uint32_t o, int ver, uint32_t ov) { obj = o; version = (short)(ver < 1 ? 1 : ver); over = ov; }

		bool bEphemeris () const override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (V_bEphemeris)) return CELBODY::bEphemeris ();
			Begin (V_bEphemeris);
			WbReader r = b.Call ();
			bool v = r.b ();
			return v && !r.bad;
		}
		void clbkInit (FILEHANDLE cfg) override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			// a version 2 object's own, or CELBODY2's on it, which loads the atmosphere on this proxy (Q_Init)
			if (!Fw (version >= 2 ? -1 : V_Init)) {
				if (version >= 2) CELBODY2::clbkInit (cfg);
				return;
			}
			Begin (V_Init).u32 (b.Id (cfg));
			b.Call ();
		}
		int clbkEphemeris (double mjd, int req, double *ret) override { return Eph (V_Ephemeris, mjd, req, ret); }
		int clbkFastEphemeris (double simt, int req, double *ret) override { return Eph (V_FastEphemeris, simt, req, ret); }
		bool clbkAtmParam (double alt, ATMPARAM *prm) override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (V_AtmParam)) return CELBODY::clbkAtmParam (alt, prm);
			WbWriter &w = Begin (V_AtmParam);
			w.f64 (alt); w.raw (prm, sizeof *prm);
			WbReader r = b.Call ();
			bool ok = r.b ();
			ATMPARAM p;
			r.raw (&p, sizeof p);
			if (r.bad) return false;
			*prm = p;
			return ok;
		}
		bool LegacyAtmosphereInterface () const override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (V_Legacy)) return CELBODY2::LegacyAtmosphereInterface ();
			Begin (V_Legacy);
			WbReader r = b.Call ();
			bool v = r.b ();
			return v && !r.bad;
		}

		// CELBODY2's own methods, which the module's object calls (L_Celbody)
		void BaseInit (FILEHANDLE cfg) { CELBODY2::clbkInit (cfg); }
		bool FreeAtm ()
		{
			// the module's own atmosphere object (SetAtmosphere, no atmosphere module): freed in the helper, as Orbiter's delete did
			WbAtmosphere *a = hAtmModule ? nullptr : WbAtmosphere::Of (atm);
			if (!a) return FreeAtmosphere ();
			atm = nullptr;
			AtmDrop (a, 0);
			return true;
		}
		void Assign (ATMOSPHERE *a) { atm = a; }
		bool LoadAtm (const char *fname) { return LoadAtmosphereModule (fname); }
		bool FreeAtmModule () { return FreeAtmosphereModule (); }
	private:
		bool Fw (int c) const { return Bridged () && (c < 0 || (over >> c & 1u)); }
		WbWriter &Begin (int c) const { WbWriter &w = WbBridge::Get ().Begin (H_CELBODY); w.i32 (K_Clbk); w.u32 (obj); w.i32 (c); return w; }
		int Eph (int c, double t, int req, double *ret)
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (c)) return c == V_Ephemeris ? CELBODY::clbkEphemeris (t, req, ret) : CELBODY::clbkFastEphemeris (t, req, ret);
			WbWriter &w = Begin (c);
			w.f64 (t); w.i32 (req); w.raw (ret, 12 * sizeof *ret);
			WbReader r = b.Call ();
			int flg = r.i32 ();
			double v[12];
			r.raw (v, sizeof v);
			if (r.bad) return 0;
			memcpy (ret, v, sizeof v);
			return flg;
		}
	};

	// an atmosphere a body's object holds now: one of the system's bodies, or a proxy's (its body may still be initialising)
	bool AtmInUse (const ATMOSPHERE *a)
	{
		if (!a) return false;
		for (auto &e : g_bodies) if (e.second->GetAtmosphere () == a) return true;
		DWORD n = oapiGetGbodyCount ();
		for (DWORD i = 0; i < n; i++) {
			CELBODY *cb = oapiGetCelbodyInterface (oapiGetGbodyByIndex ((int)i));
			if (cb && cb->Version () >= 2 && ((CELBODY2 *)cb)->GetAtmosphere () == a) return true;
		}
		return false;
	}

	// the atmosphere a body's object holds (atm on the wire)
	void PutAtm (WbWriter &w, CELBODY2 *cb)
	{
		WbBridge &b = WbBridge::Get ();
		ATMOSPHERE *a = cb ? cb->GetAtmosphere () : nullptr;
		WbAtmosphere *wa = WbAtmosphere::Of (a);
		void *h = cb ? CbAccess::AtmModule (cb) : nullptr;
		int mod = h ? WbSlotModule (h) : 0;
		w.u32 (wa && wa->Bridged () ? wa->obj : 0); w.u32 (a && !wa ? b.Id (a) : 0); w.i32 (mod); w.u32 (h && !mod ? b.Id (h) : 0);
	}

	// the atmosphere the helper gives a proxy (atmref): its object's proxy, or a native one some body holds
	ATMOSPHERE *AtmFrom (WbCelbody *p, uint32_t obj, uint32_t over, uint32_t lid)
	{
		if (obj) return AtmProxy (p, obj, over, p->module);
		ATMOSPHERE *a = lid ? (ATMOSPHERE *)WbBridge::Get ().Ptr (lid) : nullptr;
		return AtmInUse (a) ? a : nullptr;
	}

	// ---------------------------------------------------------------------
	// the 2006 interface: a function of a pool per kind, bound to the module's function of that name

	struct LegacyBind { int module = 0; uint32_t fn = 0; uint64_t session = 0; std::string name; };
	std::array<std::array<LegacyBind, WB_LEGACY>, G_COUNT> &g_legacy = *new std::array<std::array<LegacyBind, WB_LEGACY>, G_COUNT>;

	const LegacyBind *Bound (int kind, int i)
	{
		const LegacyBind &l = g_legacy[kind][i];
		return l.module && Live (l.session) ? &l : nullptr;
	}

	WbWriter *LegacyBegin (int kind, int i)
	{
		const LegacyBind *l = Bound (kind, i);
		if (!l) return nullptr;
		WbWriter &w = WbBridge::Get ().Begin (H_CELBODY);
		w.i32 (K_Legacy); w.i32 (kind); w.u32 (l->fn);
		return &w;
	}

	int LegacyEph (int kind, int i, double t, double *ret, int &format)
	{
		WbBridge &b = WbBridge::Get ();
		Guard g (b.Lock ());
		WbWriter *w = LegacyBegin (kind, i);
		if (!w) return 0;
		w->f64 (t); w->raw (ret, 12 * sizeof *ret);
		WbReader r = b.Call ();
		int res = r.i32 (), fmt = r.i32 ();
		double v[12];
		r.raw (v, sizeof v);
		if (r.bad) return 0;
		memcpy (ret, v, sizeof v);
		format = fmt;
		return res;
	}

	template <int I> struct LegacyFn {
		static void SetPrecision (double prec)
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			WbWriter *w = LegacyBegin (G_SetPrecision, I);
			if (!w) return;
			w->f64 (prec);
			b.Call ();
		}
		static int Ephemeris (double mjd, double *ret, int &format) { return LegacyEph (G_Ephemeris, I, mjd, ret, format); }
		static int FastEphemeris (double simt, double *ret, int &format) { return LegacyEph (G_FastEphemeris, I, simt, ret, format); }
		static void AtmPrm (double alt, ATMPARAM *prm)
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			WbWriter *w = LegacyBegin (G_AtmPrm, I);
			if (!w) return;
			w->f64 (alt); w->raw (prm, sizeof *prm);
			WbReader r = b.Call ();
			ATMPARAM p;
			r.raw (&p, sizeof p);
			if (!r.bad) *prm = p;
		}
	};
	using LegacyRow = std::array<void *, G_COUNT>;
	template <int I> LegacyRow MakeLegacyRow ()
	{
		using F = LegacyFn<I>;
		return {(void *)&F::SetPrecision, (void *)&F::Ephemeris, (void *)&F::FastEphemeris, (void *)&F::AtmPrm};
	}
	template <size_t... I> std::array<LegacyRow, WB_LEGACY> LegacyRows (std::index_sequence<I...>) { return {MakeLegacyRow<(int)I> ()...}; }
	const std::array<LegacyRow, WB_LEGACY> &g_legacyFn = *new std::array<LegacyRow, WB_LEGACY> (LegacyRows (std::make_index_sequence<WB_LEGACY> ()));
}

// ---------------------------------------------------------------------
// the slots' entry points (Slots.cpp)

CELBODY *WbCbInit (int module, void *hBody)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!b.Alive () || !b.Info (module)) return nullptr;
	WbCelbody *p = new WbCelbody ((OBJHANDLE)hBody, module, b.Session ()); // there for the object's calls during its InitInstance
	WbWriter &w = b.Begin (H_CELBODY);
	w.i32 (K_Init); w.i32 (module); w.u32 (b.Id (hBody)); w.u32 (b.Id (p));
	WbReader r = b.Call ();
	uint32_t obj = r.u32 ();
	int ver = r.i32 ();
	uint32_t over = r.u32 ();
	if (r.bad || !obj) { delete p; return nullptr; }
	p->Set (obj, ver, over);
	return p;
}

void WbCbExit (int module, CELBODY *m)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	WbCelbody *p = WbCelbody::Of (m);
	if (!p) return;
	if (p->Bridged ()) {
		WbWriter &w = b.Begin (H_CELBODY);
		w.i32 (K_Exit); w.i32 (module); w.u32 (p->obj);
		b.Call ();
	}
	delete p;
}

ATMOSPHERE *WbAtmCreate (int module, CELBODY2 *cb)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!cb || !b.Alive () || !b.Info (module)) return nullptr;
	WbCelbody *p = WbCelbody::Of (cb);
	WbWriter &w = b.Begin (H_CELBODY);
	w.i32 (K_AtmCreate); w.i32 (module); w.u32 (p && p->Bridged () ? p->obj : 0); w.u32 (b.Id (cb->GetHandle ()));
	CELBODY2 *outer = g_creating;
	g_creating = cb;
	WbReader r = b.Call ();
	g_creating = outer;
	uint32_t obj = r.u32 (), over = r.u32 ();
	if (r.bad || !obj) return nullptr;
	return AtmProxy (cb, obj, over, module);
}

void WbAtmDelete (int module, ATMOSPHERE *a)
{
	Guard g (WbBridge::Get ().Lock ());
	if (WbAtmosphere *wa = WbAtmosphere::Of (a)) AtmDrop (wa, module);
}

void *WbLegacyProc (int module, const char *name)
{
	static const char *const suffix[G_COUNT] = {"_SetPrecision", "_Ephemeris", "_FastEphemeris", "_AtmPrm"};
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	size_t n = name ? strlen (name) : 0;
	int kind = -1;
	for (int k = 0; k < G_COUNT; k++) {
		size_t s = strlen (suffix[k]);
		if (n > s && !strcmp (name + n - s, suffix[k])) kind = k;
	}
	if (kind < 0 || !b.Alive () || !b.Info (module)) return nullptr;
	int spare = -1;
	for (int i = 0; i < WB_LEGACY; i++) {
		if (!Bound (kind, i)) { if (spare < 0) spare = i; continue; }
		if (g_legacy[kind][i].module == module && g_legacy[kind][i].name == name) return g_legacyFn[i][kind];
	}
	WbWriter &w = b.Begin (H_CELBODY);
	w.i32 (K_LegacyProc); w.i32 (module); w.str (name);
	WbReader r = b.Call ();
	uint32_t fn = r.u32 ();
	if (r.bad || !fn) return nullptr;
	if (spare < 0) {
		static bool said;
		if (!said) oapiWriteLogV ("WineBridge: %s not bridged: %d Windows modules' %s functions are in use", name, WB_LEGACY, suffix[kind] + 1);
		said = true;
		return nullptr;
	}
	g_legacy[kind][spare] = {module, fn, b.Session (), name};
	return g_legacyFn[spare][kind];
}

// the module is freed: its functions are bound no more, its atmosphere objects and body objects are gone
void WbCelbodyFreed (int module)
{
	WbBridge &b = WbBridge::Get ();
	for (auto &k : g_legacy) for (auto &l : k) if (l.module == module) l = LegacyBind ();
	std::vector<WbAtmosphere *> drop;
	for (auto &e : g_atms) {
		WbAtmosphere *a = e.second;
		if (a->module != module || a->session != b.Session () || a->gone) continue;
		a->gone = true;                     // a body still holding it gets ATMOSPHERE's defaults
		if (!AtmInUse (a)) drop.push_back (a);
	}
	for (WbAtmosphere *a : drop) delete a;
	for (auto &e : g_bodies) if (e.second->module == module && e.second->session == b.Session ()) e.second->obj = 0;
}

// ---------------------------------------------------------------------
// the helper's calls (L_Celbody); each reads its arguments before it calls Orbiter or writes its reply

bool WbServeCelbody (uint32_t fn, WbReader &r, WbWriter &w)
{
	if (fn != L_Celbody) return false;
	WbBridge &b = WbBridge::Get ();
	int q = r.i32 ();
	switch (q) {
	case Q_Init: case Q_SetAtm: case Q_FreeAtm: case Q_LoadAtm: case Q_FreeAtmModule: case Q_SyncAtm: {
		WbCelbody *p = WbCelbody::Of ((const CELBODY *)b.Ptr (r.u32 ()));
		FILEHANDLE cfg = nullptr;
		std::string fname;
		uint32_t aobj = 0, aover = 0, alid = 0;
		if (q == Q_Init) cfg = (FILEHANDLE)b.Ptr (r.u32 ());
		if (q == Q_LoadAtm) fname = Str (r);
		if (q == Q_SetAtm || q == Q_SyncAtm) { aobj = r.u32 (); aover = r.u32 (); alid = r.u32 (); }
		if (r.bad) { b.BadArgs (fn); return true; }
		bool ok = false;
		if (p) {
			switch (q) {
			case Q_Init: p->BaseInit (cfg); ok = true; break;
			case Q_SetAtm: p->FreeAtm (); p->Assign (AtmFrom (p, aobj, aover, alid)); ok = true; break; // as CELBODY2::SetAtmosphere
			case Q_FreeAtm: ok = p->FreeAtm (); break;
			case Q_LoadAtm: ok = p->LoadAtm (fname.c_str ()); break;
			case Q_FreeAtmModule: ok = p->FreeAtmModule (); break;
			case Q_SyncAtm: p->Assign (AtmFrom (p, aobj, aover, alid)); break; // the member set: the old one is left
			}
		}
		if (q == Q_SyncAtm) return true;
		w.b (ok);
		PutAtm (w, p);
		return true; }
	case Q_CbInfo: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		if (r.bad) { b.BadArgs (fn); return true; }
		CELBODY *cb = BodyObject (h);
		WbCelbody *p = WbCelbody::Of (cb);
		w.i32 (cb ? cb->Version () : 0); w.u32 (p && p->Bridged () ? p->obj : 0);
		PutAtm (w, cb && cb->Version () >= 2 ? (CELBODY2 *)cb : nullptr);
		return true; }
	case Q_CbClbk: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		int m = r.i32 ();
		CELBODY *cb = r.bad ? nullptr : BodyObject (h);
		switch (m) {
		case V_bEphemeris: {
			bool v = cb && cb->bEphemeris ();
			w.b (v);
			break; }
		case V_Init: {
			FILEHANDLE f = (FILEHANDLE)b.Ptr (r.u32 ());
			if (cb && !r.bad) cb->clbkInit (f);
			break; }
		case V_Ephemeris: case V_FastEphemeris: {
			double t = r.f64 ();
			int req = r.i32 ();
			double v[12];
			r.raw (v, sizeof v);
			int flg = cb && !r.bad ? (m == V_Ephemeris ? cb->clbkEphemeris (t, req, v) : cb->clbkFastEphemeris (t, req, v)) : 0;
			w.i32 (flg); w.raw (v, sizeof v);
			break; }
		case V_AtmParam: {
			double alt = r.f64 ();
			ATMPARAM p;
			r.raw (&p, sizeof p);
			bool ok = cb && !r.bad && cb->clbkAtmParam (alt, &p);
			w.b (ok); w.raw (&p, sizeof p);
			break; }
		case V_Legacy: {
			bool v = cb && cb->Version () >= 2 && ((CELBODY2 *)cb)->LegacyAtmosphereInterface ();
			w.b (v);
			break; }
		default: r.bad = true; break;
		}
		if (r.bad) b.BadArgs (fn);
		return true; }
	case Q_LinuxAtm: {
		ATMOSPHERE *a = (ATMOSPHERE *)b.Ptr (r.u32 ());
		int m = r.i32 ();
		ATMCONST k {};
		ATMOSPHERE::PRM_IN in {};
		ATMOSPHERE::PRM_OUT out {};
		if (m == A_Constants) r.raw (&k, sizeof k);
		if (m == A_Params) { GetIn (r, &in); r.raw (&out, sizeof out); }
		if (r.bad || !AtmInUse (a)) a = nullptr; // a native one some body holds now
		switch (m) {
		case A_Name: { const char *s = a ? a->clbkName () : nullptr; w.str (s); break; }
		case A_Constants: { bool ok = a && a->clbkConstants (&k); w.b (ok); w.raw (&k, sizeof k); break; }
		case A_Params: { bool ok = a && a->clbkParams (&in, &out); w.b (ok); w.raw (&out, sizeof out); break; }
		default: r.bad = true; break;
		}
		if (r.bad) b.BadArgs (fn);
		return true; }
	default:
		b.BadArgs (fn);
		return true;
	}
}
