// WineBridge helper: celestial body and atmosphere modules. CELBODY, CELBODY2 and ATMOSPHERE as Orbiter defines them (Celbody.cpp of
// the Windows source). A module's objects are here; Orbiter holds proxies of them, and CELBODY2's own methods run on the proxy,
// which holds the body's atmosphere as Orbiter's object of a native module does
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include "CelBodyAPI.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"

static_assert (sizeof (ATMCONST) == 12 * 8 && sizeof (ATMPARAM) == 3 * 8 && sizeof (ATMOSPHERE::PRM_OUT) == 3 * 8, "doubles only, as on Linux");

namespace {
	// the protected members of an object of any class derived from CELBODY2
	struct CbAccess : CELBODY2 {
		static ATMOSPHERE *&Atm (CELBODY2 *b) { return b->*(&CbAccess::atm); }
		static HINSTANCE &AtmModule (CELBODY2 *b) { return b->*(&CbAccess::hAtmModule); }
	};

	// records that keep their address: the calls they are used in may add others
	template <class T> struct List {
		T **a; int n, cap;
		T *Add (const T &v)
		{
			if (n == cap) { cap = cap ? cap * 2 : 16; a = (T **)realloc (a, cap * sizeof *a); }
			T *p = (T *)malloc (sizeof (T));
			*p = v;
			return a[n++] = p;
		}
		void Drop (T *p) { for (int i = 0; i < n; i++) if (a[i] == p) { a[i] = a[--n]; free (p); return; } }
		template <class F> T *Find (F f) const { for (int i = 0; i < n; i++) if (f (*a[i])) return a[i]; return 0; }
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

	// a native atmosphere, for a module that reads one: its virtual methods are Orbiter's object's
	class AtmLinux final : public ATMOSPHERE {
	public:
		AtmLinux (CELBODY2 *b, uint32_t id) : ATMOSPHERE (b), id (id) { name[0] = 0; }
		void SetBody (CELBODY2 *b) { cbody = b; }
		const char *clbkName () const override
		{
			Begin (A_Name);
			WbReader r = wb::Call ();
			wb::OutStr (r, name, sizeof name);
			return name;
		}
		bool clbkConstants (ATMCONST *c) const override
		{
			Begin (A_Constants).raw (c, sizeof *c);
			WbReader r = wb::Call ();
			bool ok = r.b ();
			ATMCONST v;
			r.raw (&v, sizeof v);
			if (r.bad) return false;
			*c = v;
			return ok;
		}
		bool clbkParams (const PRM_IN *in, PRM_OUT *out) override
		{
			WbWriter &w = Begin (A_Params);
			PutIn (w, in); w.raw (out, sizeof *out);
			WbReader r = wb::Call ();
			bool ok = r.b ();
			PRM_OUT v;
			r.raw (&v, sizeof v);
			if (r.bad) return false;
			*out = v;
			return ok;
		}
		uint32_t id;
	private:
		mutable char name[256];
		WbWriter &Begin (int m) const { WbWriter &w = wb::Begin (L_Celbody); w.i32 (Q_LinuxAtm); w.u32 (id); w.i32 (m); return w; }
	};

	// a native body's object, for a module that asks for it: its virtual methods are Orbiter's object's
	class CbNative final : public CELBODY2 {
	public:
		CbNative (OBJHANDLE h) : CELBODY2 (h) {}
		void SetVersion (int v) { version = (short)v; }
		bool bEphemeris () const override
		{
			Begin (V_bEphemeris);
			WbReader r = wb::Call ();
			bool v = r.b ();
			return v && !r.bad;
		}
		void clbkInit (FILEHANDLE cfg) override
		{
			Begin (V_Init).u32 (wb::Id (cfg));
			wb::Call ();
		}
		int clbkEphemeris (double mjd, int req, double *ret) override { return Eph (V_Ephemeris, mjd, req, ret); }
		int clbkFastEphemeris (double simt, int req, double *ret) override { return Eph (V_FastEphemeris, simt, req, ret); }
		bool clbkAtmParam (double alt, ATMPARAM *prm) override
		{
			WbWriter &w = Begin (V_AtmParam);
			w.f64 (alt); w.raw (prm, sizeof *prm);
			WbReader r = wb::Call ();
			bool ok = r.b ();
			ATMPARAM p;
			r.raw (&p, sizeof p);
			if (r.bad) return false;
			*prm = p;
			return ok;
		}
		bool LegacyAtmosphereInterface () const override
		{
			Begin (V_Legacy);
			WbReader r = wb::Call ();
			bool v = r.b ();
			return v && !r.bad;
		}
	private:
		WbWriter &Begin (int m) const { WbWriter &w = wb::Begin (L_Celbody); w.i32 (Q_CbClbk); w.u32 (wb::Id (hBody)); w.i32 (m); return w; }
		int Eph (int m, double t, int req, double *ret)
		{
			WbWriter &w = Begin (m);
			w.f64 (t); w.i32 (req); w.raw (ret, 12 * sizeof *ret);
			WbReader r = wb::Call ();
			int flg = r.i32 ();
			double v[12];
			r.raw (v, sizeof v);
			if (r.bad) return 0;
			memcpy (ret, v, sizeof v);
			return flg;
		}
	};

	// a module's object for a body; proxy: Orbiter's for it; atm: the atmosphere the proxy holds, as the object's member last was
	struct BodyObj { CELBODY *obj; int module; uint32_t proxy; ATMOSPHERE *atm; };
	struct AtmObj { ATMOSPHERE *obj; int module; };    // a module's atmosphere object, made by that module's code
	struct LinuxAtm { uint32_t id; AtmLinux *obj; };   // the helper's object for a native atmosphere
	struct Native { uint32_t body; CbNative *obj; };   // ... for a native body's object
	List<BodyObj> g_body;
	List<AtmObj> g_atm;
	List<LinuxAtm> g_latm;
	List<Native> g_native;
	struct { int module; uint32_t proxy; } g_init; // the InitInstance running: the object it makes may give itself an atmosphere

	BodyObj *BodyOf (const void *o) { return o ? g_body.Find ([o] (const BodyObj &b) { return b.obj == o; }) : 0; }
	BodyObj *BodyId (uint32_t id) { return id ? g_body.Find ([id] (const BodyObj &b) { return wb::Id (b.obj) == id; }) : 0; }
	AtmObj *AtmOf (const void *o) { return o ? g_atm.Find ([o] (const AtmObj &a) { return a.obj == o; }) : 0; }
	AtmObj *AtmId (uint32_t id) { return id ? g_atm.Find ([id] (const AtmObj &a) { return wb::Id (a.obj) == id; }) : 0; }

	// the object's record; one made during its module's InitInstance is recorded when it first calls Orbiter's side. None: an object
	// of no body's module (a CELBODY2 a module makes for itself), which has no proxy
	BodyObj *Proxied (CELBODY2 *self)
	{
		if (BodyObj *b = BodyOf (self)) return b;
		return g_init.proxy ? g_body.Add ({self, g_init.module, g_init.proxy, 0}) : 0;
	}

	// the vtable slots of the virtual methods (WbCbVirt, WbAtmVirt), and the classes' own vtables (for ATMOSPHERE one of a class of it
	// with the pure clbkName)
	int g_cbSlot[V_COUNT], g_atmSlot[A_COUNT];
	void **g_cbVt, **g_cb2Vt, **g_atmVt;

	uint32_t Mask (const void *obj, void **base, const int *slot, int n)
	{
		void **vt = *(void ***)obj;
		uint32_t m = 0;
		for (int i = 0; i < n; i++) if (wb::Resolve (vt[slot[i]]) != base[slot[i]]) m |= 1u << i;
		return m;
	}
	uint32_t CbMask (CELBODY *o) { return o->Version () >= 2 ? Mask (o, g_cb2Vt, g_cbSlot, V_COUNT) : Mask (o, g_cbVt, g_cbSlot, V_Legacy); }
	uint32_t AtmMask (ATMOSPHERE *a) { return Mask (a, g_atmVt, g_atmSlot, A_COUNT); }

	AtmLinux *LinuxAtmFor (CELBODY2 *body, uint32_t lid)
	{
		LinuxAtm *l = g_latm.Find ([lid] (const LinuxAtm &x) { return x.id == lid; });
		if (!l) l = g_latm.Add ({lid, new AtmLinux (body, lid)});
		l->obj->SetBody (body);
		return l->obj;
	}

	// the atmosphere Orbiter's proxy holds (atm), as the object's member and its module's handle
	void GetAtm (WbReader &r, CELBODY2 *self)
	{
		uint32_t obj = r.u32 (), lid = r.u32 ();
		int mod = r.i32 ();
		uint32_t hid = r.u32 ();
		if (r.bad) return;
		ATMOSPHERE *a = 0;
		if (obj) { AtmObj *o = AtmId (obj); a = o ? o->obj : 0; }
		else if (lid) a = LinuxAtmFor (self, lid);
		CbAccess::Atm (self) = a;
		CbAccess::AtmModule (self) = (HINSTANCE)(mod > 0 ? wb::ModuleHandle (mod) : wb::Ptr (hid));
		if (BodyObj *b = BodyOf (self)) b->atm = a;
	}

	// an atmosphere the object gives Orbiter's proxy (atmref): a module's object (recorded as owner's if new), or a native one's
	void PutAtmRef (WbWriter &w, ATMOSPHERE *a, int owner)
	{
		LinuxAtm *l = a ? g_latm.Find ([a] (const LinuxAtm &x) { return x.obj == a; }) : 0;
		if (a && !l && !AtmOf (a)) g_atm.Add ({a, owner});
		w.u32 (a && !l ? wb::Id (a) : 0); w.u32 (a && !l ? AtmMask (a) : 0); w.u32 (l ? l->id : 0);
	}

	// a module that set its object's atm member itself: Orbiter reads the member, so its proxy holds that one now (the old one left)
	void Sync (uint32_t obj)
	{
		BodyObj *b = BodyId (obj);
		if (!b || b->obj->Version () < 2) return;
		ATMOSPHERE *a = CbAccess::Atm (static_cast<CELBODY2 *> (b->obj));
		if (a == b->atm) return;
		b->atm = a;
		WbWriter &w = wb::Begin (L_Celbody);
		w.i32 (Q_SyncAtm); w.u32 (b->proxy);
		PutAtmRef (w, a, b->module);
		wb::Call ();
	}

	// CELBODY2's own method q, run by Orbiter on the object's proxy: the object's atmosphere is then the proxy's
	bool OnProxy (BodyObj *b, CELBODY2 *self, int q, uint32_t cfg, const char *fname)
	{
		WbWriter &w = wb::Begin (L_Celbody);
		w.i32 (q); w.u32 (b->proxy);
		if (q == Q_Init) w.u32 (cfg);
		if (q == Q_LoadAtm) w.str (fname);
		WbReader r = wb::Call ();
		bool ok = r.b ();
		GetAtm (r, self);
		return ok && !r.bad;
	}

	// what Orbiter deletes itself through CELBODY or ATMOSPHERE, which have no virtual destructor: the memory only, with the module's
	// own operator delete
	void Release (int module, void *p)
	{
		void (*f) (void *) = (void (*) (void *))wb::ModuleDelete (module);
		if (f) { f (p); return; }
		static bool said;
		if (!said) wb::Log ("an object Orbiter frees is kept: its module imports no operator delete or free (a static C runtime)");
		said = true;
	}

	// Orbiter's object for a body: a module's object here, else the helper's for a native one, its version and atmosphere as now
	CELBODY *BodyFor (uint32_t obj, uint32_t body)
	{
		if (obj) { BodyObj *b = BodyId (obj); return b ? b->obj : 0; }
		WbWriter &w = wb::Begin (L_Celbody);
		w.i32 (Q_CbInfo); w.u32 (body);
		WbReader r = wb::Call ();
		int ver = r.i32 ();
		uint32_t mobj = r.u32 ();
		if (r.bad) return 0;
		if (mobj) { BodyObj *b = BodyId (mobj); return b ? b->obj : 0; }
		if (ver < 1) return 0;
		Native *n = g_native.Find ([body] (const Native &x) { return x.body == body; });
		if (!n) n = g_native.Add ({body, new CbNative ((OBJHANDLE)wb::Ptr (body))});
		n->obj->SetVersion (ver);
		GetAtm (r, n->obj);
		return n->obj;
	}
}

// ---------------------------------------------------------------------
// CELBODY

CELBODY::CELBODY ()
{ version = 1; }

bool CELBODY::bEphemeris () const
{ return false; }

void CELBODY::clbkInit (FILEHANDLE cfg)
{}

int CELBODY::clbkEphemeris (double mjd, int req, double *ret)
{ return 0; }

int CELBODY::clbkFastEphemeris (double simt, int req, double *ret)
{ return 0; }

bool CELBODY::clbkAtmParam (double alt, ATMPARAM *prm)
{ return false; }

void CELBODY::Pol2Crt (double *pol, double *crt)
{
	double rad  = pol[2] * AU;
	double cosp = cos (pol[0]), sinp = sin (pol[0]);
	double cost = cos (pol[1]), sint = sin (pol[1]);
	double xz   = rad * cost;
	crt[0] = xz  * cosp;
	crt[2] = xz  * sinp;
	crt[1] = rad * sint;
	double vl = xz  * pol[3];
	double vb = rad * pol[4];
	double vr = pol[5] * AU;
	crt[3] = cosp*cost*vr - cosp*sint*vb - sinp*vl;
	crt[4] = sint*     vr + cost*     vb;
	crt[5] = sinp*cost*vr - sinp*sint*vb + cosp*vl;
}

// ---------------------------------------------------------------------
// CELBODY2: what Orbiter does with the body and its atmosphere is done by Orbiter on the proxy

CELBODY2::CELBODY2 (OBJHANDLE hCBody): CELBODY ()
{
	version++;
	hBody = hCBody;
	atm = NULL;
	hAtmModule = NULL;
}

CELBODY2::~CELBODY2 ()
{
	if (BodyObj *b = BodyOf (this)) { // the atmosphere freed while the object is still there, as Orbiter's FreeAtmosphere here
		OnProxy (b, this, Q_FreeAtm, 0, 0);
		if ((b = BodyOf (this))) g_body.Drop (b);
	}
	atm = NULL;
}

void CELBODY2::clbkInit (FILEHANDLE cfg)
{
	CELBODY::clbkInit (cfg);
	if (BodyObj *b = Proxied (this)) OnProxy (b, this, Q_Init, wb::Id (cfg), 0);
}

OBJHANDLE CELBODY2::GetParent () const
{
	return oapiGetGbodyParent (hBody);
}

OBJHANDLE CELBODY2::GetChild (DWORD idx) const
{
	return oapiGetGbodyChild (hBody, idx);
}

double CELBODY2::SidRotPeriod () const
{
	return oapiGetPlanetPeriod (hBody);
}

void CELBODY2::SetAtmosphere (ATMOSPHERE *a)
{
	BodyObj *b = Proxied (this);
	if (!b) { FreeAtmosphere (); atm = a; return; }
	WbWriter &w = wb::Begin (L_Celbody);
	w.i32 (Q_SetAtm); w.u32 (b->proxy);
	PutAtmRef (w, a, b->module);
	WbReader r = wb::Call ();
	r.b ();
	GetAtm (r, this);
}

bool CELBODY2::FreeAtmosphere ()
{
	BodyObj *b = Proxied (this);
	if (b) return OnProxy (b, this, Q_FreeAtm, 0, 0);
	if (!atm) return false; // an object of no body's: Orbiter's delete of its module's atmosphere object
	if (int m = wb::AddrModule (*(void **)atm)) Release (m, atm);
	atm = NULL;
	return true;
}

bool CELBODY2::LoadAtmosphereModule (const char *fname)
{
	BodyObj *b = Proxied (this);
	return b && OnProxy (b, this, Q_LoadAtm, 0, fname);
}

bool CELBODY2::FreeAtmosphereModule ()
{
	BodyObj *b = Proxied (this);
	return b && OnProxy (b, this, Q_FreeAtmModule, 0, 0);
}

// ---------------------------------------------------------------------
// ATMOSPHERE

ATMOSPHERE::ATMOSPHERE (CELBODY2 *body)
{
	cbody = body;
}

bool ATMOSPHERE::clbkConstants (ATMCONST *atmc) const
{
	atmc->R = 286.91; // Orbiter's Earth defaults
	atmc->gamma = 1.4;
	return false;
}

bool ATMOSPHERE::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm_out)
{
	return false;
}

// ---------------------------------------------------------------------

CELBODY *oapiGetCelbodyInterface (OBJHANDLE hBody)
{
	return BodyFor (0, wb::Id (hBody));
}

namespace {
	struct AtmMade : ATMOSPHERE { AtmMade (CELBODY2 *b) : ATMOSPHERE (b) {} const char *clbkName () const override { return 0; } };
}

bool wb::CelbodyProbe ()
{
	static CELBODY c1;
	static CELBODY2 c2 ((OBJHANDLE)0);
	static AtmMade a (0);
	g_cbVt = *(void ***)&c1;
	g_cb2Vt = *(void ***)&c2;
	g_atmVt = *(void ***)static_cast<ATMOSPHERE *> (&a);
	g_cbSlot[V_bEphemeris] = VcallSlot (&CELBODY::bEphemeris);
	g_cbSlot[V_Init] = VcallSlot (&CELBODY::clbkInit);
	g_cbSlot[V_Ephemeris] = VcallSlot (&CELBODY::clbkEphemeris);
	g_cbSlot[V_FastEphemeris] = VcallSlot (&CELBODY::clbkFastEphemeris);
	g_cbSlot[V_AtmParam] = VcallSlot (&CELBODY::clbkAtmParam);
	g_cbSlot[V_Legacy] = VcallSlot (&CELBODY2::LegacyAtmosphereInterface);
	g_atmSlot[A_Name] = VcallSlot (&ATMOSPHERE::clbkName);
	g_atmSlot[A_Constants] = VcallSlot (&ATMOSPHERE::clbkConstants);
	g_atmSlot[A_Params] = VcallSlot (&ATMOSPHERE::clbkParams);
	// the SDK's order: CELBODY's five, CELBODY2's destructor, LegacyAtmosphereInterface; ATMOSPHERE's three
	static const int cb[V_COUNT] = {0, 1, 2, 3, 4, 6}, at[A_COUNT] = {0, 1, 2};
	return !memcmp (g_cbSlot, cb, sizeof cb) && !memcmp (g_atmSlot, at, sizeof at);
}

// ---------------------------------------------------------------------
// Orbiter's calls (H_CELBODY); each reads its arguments before the module runs and writes its reply after

void wb::ServeCelbody (WbReader &r, WbWriter &w)
{
	int c = r.i32 ();
	switch (c) {
	case K_Init: {
		int mod = r.i32 ();
		uint32_t body = r.u32 (), proxy = r.u32 ();
		void *f = r.bad ? 0 : ModuleExport (mod, E_InitInstance);
		CELBODY *o = 0;
		if (f) {
			g_init.module = mod; g_init.proxy = proxy;
			o = ((CELBODY *(*) (OBJHANDLE))f) ((OBJHANDLE)Ptr (body));
			g_init.module = 0; g_init.proxy = 0;
		}
		for (int i = g_body.n - 1; i >= 0; i--) if (g_body.a[i]->proxy == proxy && g_body.a[i]->obj != o) g_body.Drop (g_body.a[i]);
		if (o && !BodyOf (o)) g_body.Add ({o, mod, proxy, 0});
		if (o) Sync (Id (o));
		w.u32 (Id (o)); w.i32 (o ? o->Version () : 0); w.u32 (o ? CbMask (o) : 0);
		break; }
	case K_Exit: {
		int mod = r.i32 ();
		uint32_t obj = r.u32 ();
		BodyObj *b = r.bad ? 0 : BodyId (obj);
		if (!b) break;
		CELBODY *o = b->obj;
		if (void *f = ModuleExport (mod, E_ExitInstance)) ((void (*) (CELBODY *))f) (o);
		else Release (b->module, o);
		if ((b = BodyOf (o))) g_body.Drop (b); // a version 2 object's destructor dropped it
		break; }
	case K_Clbk: {
		uint32_t obj = r.u32 ();
		int m = r.i32 ();
		BodyObj *b = BodyId (obj);
		CELBODY *o = b ? b->obj : 0;
		switch (m) {
		case V_bEphemeris: {
			bool v = o && o->bEphemeris ();
			Sync (obj);
			w.b (v);
			break; }
		case V_Init: {
			uint32_t cfg = r.u32 ();
			if (o && !r.bad) o->clbkInit ((FILEHANDLE)Ptr (cfg));
			Sync (obj);
			break; }
		case V_Ephemeris: case V_FastEphemeris: {
			double t = r.f64 ();
			int req = r.i32 ();
			double v[12];
			r.raw (v, sizeof v);
			int flg = o && !r.bad ? (m == V_Ephemeris ? o->clbkEphemeris (t, req, v) : o->clbkFastEphemeris (t, req, v)) : 0;
			Sync (obj);
			w.i32 (flg); w.raw (v, sizeof v);
			break; }
		case V_AtmParam: {
			double alt = r.f64 ();
			ATMPARAM p;
			r.raw (&p, sizeof p);
			bool ok = o && !r.bad && o->clbkAtmParam (alt, &p);
			Sync (obj);
			w.b (ok); w.raw (&p, sizeof p);
			break; }
		case V_Legacy: {
			bool v = o && o->Version () >= 2 && static_cast<CELBODY2 *> (o)->LegacyAtmosphereInterface ();
			Sync (obj);
			w.b (v);
			break; }
		}
		break; }
	case K_AtmCreate: {
		int mod = r.i32 ();
		uint32_t obj = r.u32 (), body = r.u32 ();
		void *f = r.bad ? 0 : ModuleExport (mod, E_CreateAtmosphere);
		CELBODY *cb = f ? BodyFor (obj, body) : 0;
		ATMOSPHERE *a = cb && cb->Version () >= 2 ? ((ATMOSPHERE *(*) (CELBODY2 *))f) (static_cast<CELBODY2 *> (cb)) : 0;
		if (a && !AtmOf (a)) g_atm.Add ({a, mod});
		w.u32 (Id (a)); w.u32 (a ? AtmMask (a) : 0);
		break; }
	case K_AtmDelete: {
		int mod = r.i32 ();
		uint32_t obj = r.u32 ();
		AtmObj *o = r.bad ? 0 : AtmId (obj);
		if (!o) break;
		ATMOSPHERE *a = o->obj;
		int owner = o->module;
		g_atm.Drop (o);
		if (void *f = mod ? ModuleExport (mod, E_DeleteAtmosphere) : 0) ((void (*) (ATMOSPHERE *))f) (a);
		else Release (owner, a);
		break; }
	case K_Atm: {
		uint32_t obj = r.u32 ();
		int m = r.i32 ();
		AtmObj *o = AtmId (obj);
		ATMOSPHERE *a = o ? o->obj : 0;
		switch (m) {
		case A_Name: {
			const char *s = a ? a->clbkName () : 0;
			w.str (s);
			break; }
		case A_Constants: {
			ATMCONST k;
			r.raw (&k, sizeof k);
			bool ok = a && !r.bad && a->clbkConstants (&k);
			w.b (ok); w.raw (&k, sizeof k);
			break; }
		case A_Params: {
			ATMOSPHERE::PRM_IN in;
			ATMOSPHERE::PRM_OUT out;
			GetIn (r, &in);
			r.raw (&out, sizeof out);
			bool ok = a && !r.bad && a->clbkParams (&in, &out);
			w.b (ok); w.raw (&out, sizeof out);
			break; }
		}
		break; }
	case K_LegacyProc: {
		int mod = r.i32 ();
		uint32_t n;
		const char *s = r.str (&n);
		char name[512];
		bool fits = s && n < sizeof name && !r.bad;
		if (fits) { memcpy (name, s, n); name[n] = 0; }
		w.u32 (fits ? Id (ModuleExport (mod, name)) : 0);
		break; }
	case K_Legacy: {
		int kind = r.i32 ();
		void *f = Ptr (r.u32 ());
		switch (kind) {
		case G_SetPrecision: {
			double p = r.f64 ();
			if (f && !r.bad) ((void (*) (double))f) (p);
			break; }
		case G_Ephemeris: case G_FastEphemeris: {
			double t = r.f64 (), v[12];
			r.raw (v, sizeof v);
			int fmt = 0, res = f && !r.bad ? ((int (*) (double, double *, int &))f) (t, v, fmt) : 0;
			w.i32 (res); w.i32 (fmt); w.raw (v, sizeof v);
			break; }
		case G_AtmPrm: {
			double alt = r.f64 ();
			ATMPARAM p;
			r.raw (&p, sizeof p);
			if (f && !r.bad) ((void (*) (double, ATMPARAM *))f) (alt, &p);
			w.raw (&p, sizeof p);
			break; }
		}
		break; }
	}
}

// the module is freed: its objects went with it (its ExitInstance and DeleteAtmosphere ran, or Orbiter freed them)
void wb::CelbodyFreed (int module)
{
	for (int i = g_body.n - 1; i >= 0; i--) if (g_body.a[i]->module == module) g_body.Drop (g_body.a[i]);
	for (int i = g_atm.n - 1; i >= 0; i--) if (g_atm.a[i]->module == module) g_atm.Drop (g_atm.a[i]);
}

// ---------------------------------------------------------------------
// the inline and compiler-made members Orbiter.exe exports (api/inline.def): made here (this file is built without inlining)

__attribute__ ((used)) static void Made (CELBODY *a, CELBODY2 *b, AtmMade *c)
{
	CELBODY ca (*a);
	*a = ca;
	CELBODY2 cb (*b);
	*b = cb;
	AtmMade cc (*c);
	*c = cc;
	OBJHANDLE h = b->GetHandle ();
	ATMOSPHERE *x = b->GetAtmosphere ();
	int v = a->Version ();
	(void)h; (void)x; (void)v;
}
