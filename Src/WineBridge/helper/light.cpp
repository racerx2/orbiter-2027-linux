// WineBridge helper: lights, the classes of LightEmitter.cpp of Orbiter. The helper's object of a light Orbiter has holds the light's
// state, which goes to Orbiter's light after every change; a module before 2016 gets the 2010 layout (compat2010.h)
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdlib.h>
#include <new>
#include "wb_helper.h"
#include "compat2010.h"

#define VID (wb::Id (vessel))
static void V3w (WbWriter &w, const VECTOR3 &v) { w.v3 (v.data); }

namespace {
	// a light's fields in its layout; null where its class has none
	struct View {
		LightEmitter::TYPE *ltype;
		LightEmitter::VISIBILITY *vis;
		OBJHANDLE *hRef;
		bool *active;
		COLOUR4 *diff, *spec, *ambi;
		const double **intens;
		double *lintens;
		const VECTOR3 **pos, **dir;
		VECTOR3 *lpos, *ldir;
		double *range, *att, *umbra, *penumbra;
	};

	// the 2016 layout through member pointers of a class derived from the lights
	struct Acc : SpotLight {
		static View Of (LightEmitter *l)
		{
			View v;
			v.ltype = &(l->*&Acc::ltype); v.vis = &(l->*&Acc::visibility); v.hRef = &(l->*&Acc::hRef); v.active = &(l->*&Acc::active);
			v.diff = &(l->*&Acc::col_diff); v.spec = &(l->*&Acc::col_spec); v.ambi = &(l->*&Acc::col_ambi);
			v.intens = &(l->*&Acc::intens); v.lintens = &(l->*&Acc::lintens);
			v.pos = &(l->*&Acc::pos); v.dir = &(l->*&Acc::dir); v.lpos = &(l->*&Acc::lpos); v.ldir = &(l->*&Acc::ldir);
			bool point = *v.ltype == LT_POINT || *v.ltype == LT_SPOT, spot = *v.ltype == LT_SPOT;
			PointLight *p = static_cast<PointLight *> (l);
			v.range = point ? &(p->*&Acc::range) : 0; v.att = point ? (p->*&Acc::att) : 0;
			SpotLight *s = static_cast<SpotLight *> (l);
			v.umbra = spot ? &(s->*&Acc::umbra) : 0; v.penumbra = spot ? &(s->*&Acc::penumbra) : 0;
			return v;
		}
	};

	View Of2010 (wb::Light2010 *L)
	{
		View v;
		v.ltype = &L->ltype; v.vis = 0; v.hRef = &L->hRef; v.active = &L->active;
		v.diff = &L->col_diff; v.spec = &L->col_spec; v.ambi = &L->col_ambi;
		v.intens = (const double **)&L->intens; v.lintens = &L->lintens;
		v.pos = &L->pos; v.dir = &L->dir; v.lpos = &L->lpos; v.ldir = &L->ldir;
		bool point = L->ltype == LightEmitter::LT_POINT || L->ltype == LightEmitter::LT_SPOT, spot = L->ltype == LightEmitter::LT_SPOT;
		v.range = point ? &L->range : 0; v.att = point ? L->att : 0;
		v.umbra = spot ? &L->umbra : 0; v.penumbra = spot ? &L->penumbra : 0;
		return v;
	}
	static_assert (offsetof (wb::Light2010, col_diff) == 12 && offsetof (wb::Light2010, intens) == 60 && offsetof (wb::Light2010, lpos) == 80 &&
		offsetof (wb::Light2010, range) == 128 && offsetof (wb::Light2010, umbra) == 160 && sizeof (wb::Light2010) == 176, "the 2010 SDK's light layout");

	// the helper's objects of Orbiter's lights: id is Orbiter's light, ref the live items Orbiter reads its position, direction and
	// intensity from (0: its own), last the object's bytes Orbiter has; a mirror stands for a light the bridge didn't make
	struct Ref { const void *p; uint32_t id; };
	struct Rec { void *l; uint32_t vid, id; bool old, mirror; Ref ref[3]; uint8_t last[sizeof (wb::Light2010) + 8]; uint32_t n; };
	Rec *g_recs;
	uint32_t g_nrecs, g_cap;

	Rec *RecOf (const void *l)
	{
		for (uint32_t i = 0; i < g_nrecs; i++) if (g_recs[i].l == l) return &g_recs[i];
		return 0;
	}
	Rec *RecOf (uint32_t vid, uint32_t id, bool old)
	{
		for (uint32_t i = 0; i < g_nrecs; i++) if (g_recs[i].vid == vid && g_recs[i].id == id && g_recs[i].old == old) return &g_recs[i];
		return 0;
	}
	Rec *AddRec (void *l, uint32_t vid, uint32_t id, bool old, bool mirror)
	{
		if (g_nrecs == g_cap) { g_cap = g_cap ? g_cap * 2 : 32; g_recs = (Rec *)realloc (g_recs, g_cap * sizeof (Rec)); }
		Rec &r = g_recs[g_nrecs++];
		r = {l, vid, id, old, mirror, {}, {}, 0};
		return &r;
	}
	void FreeObject (void *l, bool old)
	{
		if (old) free (l);
		else delete (LightEmitter *)l;
	}
	// a record goes with its items; its object too when Orbiter's light is gone
	void DropRec (uint32_t i, bool freeObject)
	{
		Rec &r = g_recs[i];
		for (uint32_t k = 0; k < 3; k++) wb::LiveDropTag (r.vid, wb::TAG_LIGHT, r.id << 2 | k);
		if (freeObject) FreeObject (r.l, r.old);
		g_recs[i] = g_recs[--g_nrecs];
	}

	// the object's bytes, as its layout and class have them
	uint32_t Size (const void *l, bool old)
	{
		LightEmitter::TYPE t = old ? ((const wb::Light2010 *)l)->ltype : *Acc::Of ((LightEmitter *)l).ltype;
		bool point = t == LightEmitter::LT_POINT || t == LightEmitter::LT_SPOT, spot = t == LightEmitter::LT_SPOT;
		if (old) return spot ? sizeof (wb::Light2010) : point ? offsetof (wb::Light2010, umbra) : offsetof (wb::Light2010, range);
		return spot ? sizeof (SpotLight) : point ? sizeof (PointLight) : sizeof (LightEmitter);
	}
	bool Moved (const Rec &r) { uint32_t n = Size (r.l, r.old); return n != r.n || memcmp (r.last, r.l, n); }

	// the layout of a light: its record's, else (an object the module made) the SDK of the code that called
	bool IsOld (const void *l, const void *caller)
	{
		Rec *r = RecOf (l);
		return r ? r->old : wb::AddrModule (caller) && wb::CallerApi (caller) < wb::API_2016;
	}
	View ViewOf (const void *l, bool old) { return old ? Of2010 ((wb::Light2010 *)l) : Acc::Of ((LightEmitter *)l); }

	void LiveArg (WbWriter &w, uint32_t id)
	{
		uint32_t n;
		const uint8_t *v = wb::LiveValue (id, &n);
		w.u32 (id); w.u32 (n); w.raw (v, n);
	}

	// where Orbiter reads a value: 0 its own, 1 the item it reads now, 2 a new item of the module's memory
	void PutRef (WbWriter &w, Rec &r, int k, int kind, const void *p, const void *own)
	{
		if (!p || p == own) {
			if (r.ref[k].id) { wb::LiveDropTag (r.vid, wb::TAG_LIGHT, r.id << 2 | k); r.ref[k] = {0, 0}; }
			w.i32 (0);
			return;
		}
		if (r.ref[k].id && r.ref[k].p == p) { w.i32 (1); return; }
		if (r.ref[k].id) wb::LiveDropTag (r.vid, wb::TAG_LIGHT, r.id << 2 | k);
		uint32_t id = wb::Live (r.vid, kind, p);
		wb::LiveTag (id, wb::TAG_LIGHT, r.id << 2 | k);
		r.ref[k] = {p, id};
		w.i32 (2); LiveArg (w, id);
	}

	// the object's state to Orbiter's light, after every change
	void Send (Rec &r)
	{
		View v = ViewOf (r.l, r.old);
		WbWriter &w = wb::Begin (L_LightState);
		w.u32 (r.vid); w.u32 (r.id);
		w.b (*v.active); w.u32 (wb::Id (*v.hRef)); w.i32 (v.vis ? (int)*v.vis : -1);
		w.raw (v.diff, sizeof (COLOUR4)); w.raw (v.spec, sizeof (COLOUR4)); w.raw (v.ambi, sizeof (COLOUR4));
		V3w (w, *v.lpos); V3w (w, *v.ldir); w.f64 (*v.lintens);
		PutRef (w, r, 0, WB_LIVE_VECTOR3, *v.pos, v.lpos);
		PutRef (w, r, 1, WB_LIVE_VECTOR3, *v.dir, v.ldir);
		PutRef (w, r, 2, WB_LIVE_DOUBLE, *v.intens, v.lintens);
		w.b (v.range != 0);
		if (v.range) { w.f64 (*v.range); w.f64 (v.att[0]); w.f64 (v.att[1]); w.f64 (v.att[2]); }
		w.b (v.umbra != 0);
		if (v.umbra) { w.f64 (*v.umbra); w.f64 (*v.penumbra); }
		wb::Call ();
		r.n = Size (r.l, r.old);
		memcpy (r.last, r.l, r.n);
	}
	void Changed (const void *l) { if (Rec *r = RecOf (l)) Send (*r); }

	// as LightEmitter's constructors, in the layout of the object
	void Init (const View &v, LightEmitter::TYPE type, const COLOUR4 &diffuse, const COLOUR4 &specular, const COLOUR4 &ambient)
	{
		*v.ltype = type;
		if (v.vis) *v.vis = LightEmitter::VIS_EXTERNAL;
		*v.hRef = 0;
		*v.active = false;
		*v.diff = diffuse; *v.spec = specular; *v.ambi = ambient;
		*v.lpos = _V(0,0,0); *v.pos = v.lpos;
		*v.ldir = _V(0,0,1); *v.dir = v.ldir;
		*v.lintens = 1.0; *v.intens = v.lintens;
	}
	const COLOUR4 WHITE = {1, 1, 1, 1};

	// PointLight's and SpotLight's constructors in the 2010 layout (a point light's object has no umbra and penumbra)
	void Fill2010 (wb::Light2010 *L, LightEmitter::TYPE type, OBJHANDLE hObj, const VECTOR3 &pos, const VECTOR3 &dir, double range, double att0,
		double att1, double att2, double umbra, double penumbra, const COLOUR4 &diffuse, const COLOUR4 &specular, const COLOUR4 &ambient)
	{
		L->ltype = type;
		Init (Of2010 (L), type, diffuse, specular, ambient);
		View v = Of2010 (L);
		*v.hRef = hObj; *v.lpos = pos; *v.pos = v.lpos;
		*v.range = range; v.att[0] = att0; v.att[1] = att1; v.att[2] = att2;
		*v.active = true;
		if (type == LightEmitter::LT_SPOT) { *v.ldir = dir; *v.dir = v.ldir; *v.umbra = umbra; *v.penumbra = penumbra; }
	}

	// a point or spot light in the layout of the caller's SDK
	void *Make (bool old, LightEmitter::TYPE type, OBJHANDLE hObj, const VECTOR3 &pos, const VECTOR3 &dir, double range, double att0, double att1,
		double att2, double umbra, double penumbra, const COLOUR4 &diffuse, const COLOUR4 &specular, const COLOUR4 &ambient)
	{
		if (!old) {
			if (type == LightEmitter::LT_SPOT) return new SpotLight (hObj, pos, dir, range, att0, att1, att2, umbra, penumbra, diffuse, specular, ambient);
			return new PointLight (hObj, pos, range, att0, att1, att2, diffuse, specular, ambient);
		}
		wb::Light2010 *L = (wb::Light2010 *)calloc (1, sizeof *L);
		Fill2010 (L, type, hObj, pos, dir, range, att0, att1, att2, umbra, penumbra, diffuse, specular, ambient);
		return L;
	}
}

// ---------------------------------------------------------------------
// LightEmitter, PointLight, SpotLight (an object a module made itself is in its SDK's layout)

LightEmitter::LightEmitter ()
{
	View v = IsOld (this, __builtin_return_address (0)) ? Of2010 ((wb::Light2010 *)this) : Acc::Of (this);
	Init (v, LT_NONE, WHITE, WHITE, WHITE);
}

LightEmitter::LightEmitter (COLOUR4 diffuse, COLOUR4 specular, COLOUR4 ambient)
{
	View v = IsOld (this, __builtin_return_address (0)) ? Of2010 ((wb::Light2010 *)this) : Acc::Of (this);
	Init (v, LT_NONE, diffuse, specular, ambient);
}

void LightEmitter::Activate (bool act)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	if (act != *v.active) { *v.active = act; Changed (this); }
}

bool LightEmitter::IsActive () const
{
	return *ViewOf (this, IsOld (this, __builtin_return_address (0))).active;
}

OBJHANDLE LightEmitter::Attach (OBJHANDLE hObj)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	OBJHANDLE hOld = *v.hRef;
	*v.hRef = hObj;
	Changed (this);
	return hOld;
}

OBJHANDLE LightEmitter::Detach ()
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	OBJHANDLE hOld = *v.hRef;
	*v.hRef = 0;
	Changed (this);
	return hOld;
}

void LightEmitter::SetPosition (const VECTOR3 &p)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	*v.lpos = p; *v.pos = v.lpos;
	Changed (this);
}

void LightEmitter::ShiftExplicitPosition (const VECTOR3 &ofs)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	*v.lpos += ofs;
	Changed (this);
}

void LightEmitter::SetPositionRef (const VECTOR3 *p)
{
	*ViewOf (this, IsOld (this, __builtin_return_address (0))).pos = p;
	Changed (this);
}

const VECTOR3 *LightEmitter::GetPositionRef () const
{
	return *ViewOf (this, IsOld (this, __builtin_return_address (0))).pos;
}

void LightEmitter::SetDirection (const VECTOR3 &d)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	*v.ldir = d; *v.dir = v.ldir;
	Changed (this);
}

VECTOR3 LightEmitter::GetDirection () const
{
	return **ViewOf (this, IsOld (this, __builtin_return_address (0))).dir;
}

void LightEmitter::SetDirectionRef (const VECTOR3 *d)
{
	*ViewOf (this, IsOld (this, __builtin_return_address (0))).dir = d;
	Changed (this);
}

const VECTOR3 *LightEmitter::GetDirectionRef () const
{
	return *ViewOf (this, IsOld (this, __builtin_return_address (0))).dir;
}

void LightEmitter::SetIntensity (double in)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	*v.lintens = in; *v.intens = v.lintens;
	Changed (this);
}

double LightEmitter::GetIntensity () const
{
	return **ViewOf (this, IsOld (this, __builtin_return_address (0))).intens;
}

void LightEmitter::SetIntensityRef (double *pin)
{
	*ViewOf (this, IsOld (this, __builtin_return_address (0))).intens = pin;
	Changed (this);
}

const double *LightEmitter::GetIntensityRef () const
{
	return *ViewOf (this, IsOld (this, __builtin_return_address (0))).intens;
}

PointLight::PointLight (OBJHANDLE hObj, const VECTOR3 &_pos, double _range, double att0, double att1, double att2, COLOUR4 diffuse, COLOUR4 specular, COLOUR4 ambient)
: LightEmitter (diffuse, specular, ambient)
{
	ltype = LT_POINT;
	Attach (hObj);
	SetPosition (_pos);
	range = _range;
	att[0] = att0;
	att[1] = att1;
	att[2] = att2;
	Activate (true);
}

void PointLight::SetRange (double _range)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	if (v.range) *v.range = _range;
	Changed (this);
}

void PointLight::SetAttenuation (double att0, double att1, double att2)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	if (v.att) { v.att[0] = att0; v.att[1] = att1; v.att[2] = att2; }
	Changed (this);
}

SpotLight::SpotLight (OBJHANDLE hObj, const VECTOR3 &_pos, const VECTOR3 &_dir, double _range, double att0, double att1, double att2, double _umbra, double _penumbra, COLOUR4 diffuse, COLOUR4 specular, COLOUR4 ambient)
: PointLight (hObj, _pos, _range, att0, att1, att2, diffuse, specular, ambient)
{
	ltype = LT_SPOT;
	SetDirection (_dir);
	umbra = _umbra;
	penumbra = _penumbra;
}

void SpotLight::SetAperture (double _umbra, double _penumbra)
{
	View v = ViewOf (this, IsOld (this, __builtin_return_address (0)));
	if (v.umbra) { *v.umbra = _umbra; *v.penumbra = _penumbra; }
	Changed (this);
}

// the members the header has inline and the compiler-made ones, exported under their names (api/inline.def); the colourless
// constructors are the 2010 SDK's names (the 2024 header keeps them)
namespace wb {
	struct LightMade {
		LightEmitter::TYPE GetType () const;
		LightEmitter::VISIBILITY GetVisibility () const;
		void SetVisibility (LightEmitter::VISIBILITY vis);
		const COLOUR4 &GetDiffuseColour () const;
		const COLOUR4 &GetSpecularColour () const;
		const COLOUR4 &GetAmbientColour () const;
		VECTOR3 GetPosition () const;
		const OBJHANDLE GetObjectHandle () const;
		double GetRange () const;
		const double *GetAttenuation () const;
		double GetUmbra () const;
		double GetPenumbra () const;
		LightMade *Point (OBJHANDLE hObj, const VECTOR3 &pos, double range, double att0, double att1, double att2);
		LightMade *Spot (OBJHANDLE hObj, const VECTOR3 &pos, const VECTOR3 &dir, double range, double att0, double att1, double att2, double umbra, double penumbra);
		LightMade &AssignLight (const LightMade &o);
		LightMade &AssignPoint (const LightMade &o);
		LightMade &AssignSpot (const LightMade &o);
	};
}

#define LAYOUT IsOld (this, __builtin_return_address (0))
LightEmitter::TYPE wb::LightMade::GetType () const { return *ViewOf (this, LAYOUT).ltype; }
LightEmitter::VISIBILITY wb::LightMade::GetVisibility () const { View v = ViewOf (this, LAYOUT); return v.vis ? *v.vis : LightEmitter::VIS_EXTERNAL; }
void wb::LightMade::SetVisibility (LightEmitter::VISIBILITY vis) { View v = ViewOf (this, LAYOUT); if (v.vis) *v.vis = vis; }
const COLOUR4 &wb::LightMade::GetDiffuseColour () const { return *ViewOf (this, LAYOUT).diff; }
const COLOUR4 &wb::LightMade::GetSpecularColour () const { return *ViewOf (this, LAYOUT).spec; }
const COLOUR4 &wb::LightMade::GetAmbientColour () const { return *ViewOf (this, LAYOUT).ambi; }
VECTOR3 wb::LightMade::GetPosition () const { return **ViewOf (this, LAYOUT).pos; }
const OBJHANDLE wb::LightMade::GetObjectHandle () const { return *ViewOf (this, LAYOUT).hRef; }
double wb::LightMade::GetRange () const { View v = ViewOf (this, LAYOUT); return v.range ? *v.range : 0; }
const double *wb::LightMade::GetAttenuation () const { return ViewOf (this, LAYOUT).att; }
double wb::LightMade::GetUmbra () const { View v = ViewOf (this, LAYOUT); return v.umbra ? *v.umbra : 0; }
double wb::LightMade::GetPenumbra () const { View v = ViewOf (this, LAYOUT); return v.penumbra ? *v.penumbra : 0; }

// as PointLight's and SpotLight's constructors without colours, in the caller's layout (not Orbiter's: it has no light for it)
wb::LightMade *wb::LightMade::Point (OBJHANDLE hObj, const VECTOR3 &pos, double range, double att0, double att1, double att2)
{
	bool old = wb::AddrModule (__builtin_return_address (0)) && wb::CallerApi (__builtin_return_address (0)) < wb::API_2016;
	if (!old) new (this) PointLight (hObj, pos, range, att0, att1, att2, WHITE, WHITE, WHITE);
	else Fill2010 ((wb::Light2010 *)this, LightEmitter::LT_POINT, hObj, pos, pos, range, att0, att1, att2, 0, 0, WHITE, WHITE, WHITE);
	return this;
}

wb::LightMade *wb::LightMade::Spot (OBJHANDLE hObj, const VECTOR3 &pos, const VECTOR3 &dir, double range, double att0, double att1, double att2, double umbra, double penumbra)
{
	bool old = wb::AddrModule (__builtin_return_address (0)) && wb::CallerApi (__builtin_return_address (0)) < wb::API_2016;
	if (!old) new (this) SpotLight (hObj, pos, dir, range, att0, att1, att2, umbra, penumbra, WHITE, WHITE, WHITE);
	else Fill2010 ((wb::Light2010 *)this, LightEmitter::LT_SPOT, hObj, pos, dir, range, att0, att1, att2, umbra, penumbra, WHITE, WHITE, WHITE);
	return this;
}

// the compiler's member-wise assignments, in the target's layout (the pointers copied as they are); Orbiter's light then has its state
static void Assign (void *to, const void *from, bool old, int cls)
{
	if (old) {
		size_t n = cls == 0 ? offsetof (wb::Light2010, range) : cls == 1 ? offsetof (wb::Light2010, umbra) : sizeof (wb::Light2010);
		memmove (to, from, n);
	} else if (cls == 0) *(LightEmitter *)to = *(const LightEmitter *)from;
	else if (cls == 1) *(PointLight *)to = *(const PointLight *)from;
	else *(SpotLight *)to = *(const SpotLight *)from;
	Changed (to);
}
wb::LightMade &wb::LightMade::AssignLight (const LightMade &o) { Assign (this, &o, LAYOUT, 0); return *this; }
wb::LightMade &wb::LightMade::AssignPoint (const LightMade &o) { Assign (this, &o, LAYOUT, 1); return *this; }
wb::LightMade &wb::LightMade::AssignSpot (const LightMade &o) { Assign (this, &o, LAYOUT, 2); return *this; }
#undef LAYOUT

// ---------------------------------------------------------------------
// a vessel's lights

// Orbiter's light is made first; the helper's object, made as Orbiter makes its own, is active as Orbiter's is
static LightEmitter *Added (uint32_t vid, uint32_t id, bool old, LightEmitter::TYPE type, OBJHANDLE hObj, const VECTOR3 &pos, const VECTOR3 &dir,
	double range, double att0, double att1, double att2, double umbra, double penumbra, const COLOUR4 &diffuse, const COLOUR4 &specular, const COLOUR4 &ambient)
{
	void *l = Make (old, type, hObj, pos, dir, range, att0, att1, att2, umbra, penumbra, diffuse, specular, ambient);
	Rec *r = AddRec (l, vid, id, old, false);
	r->n = Size (l, old);
	memcpy (r->last, l, r->n);
	return (LightEmitter *)l;
}

LightEmitter *VESSEL::AddPointLight (const VECTOR3 &pos, double range, double att0, double att1, double att2, COLOUR4 diffuse, COLOUR4 specular, COLOUR4 ambient) const
{
	uint32_t vid = VID;
	WbWriter &w = wb::Begin (L_AddPointLight); w.u32 (vid);
	V3w (w, pos); w.f64 (range); w.f64 (att0); w.f64 (att1); w.f64 (att2);
	w.raw (&diffuse, 16); w.raw (&specular, 16); w.raw (&ambient, 16);
	WbReader r = wb::Call ();
	uint32_t id = r.u32 ();
	if (!id) return 0;
	bool old = wb::CallerApi (__builtin_return_address (0)) < wb::API_2016;
	return Added (vid, id, old, LightEmitter::LT_POINT, (OBJHANDLE)vessel, pos, pos, range, att0, att1, att2, 0, 0, diffuse, specular, ambient);
}

LightEmitter *VESSEL::AddSpotLight (const VECTOR3 &pos, const VECTOR3 &dir, double range, double att0, double att1, double att2, double umbra, double penumbra, COLOUR4 diffuse, COLOUR4 specular, COLOUR4 ambient) const
{
	uint32_t vid = VID;
	WbWriter &w = wb::Begin (L_AddSpotLight); w.u32 (vid);
	V3w (w, pos); V3w (w, dir); w.f64 (range); w.f64 (att0); w.f64 (att1); w.f64 (att2); w.f64 (umbra); w.f64 (penumbra);
	w.raw (&diffuse, 16); w.raw (&specular, 16); w.raw (&ambient, 16);
	WbReader r = wb::Call ();
	uint32_t id = r.u32 ();
	if (!id) return 0;
	bool old = wb::CallerApi (__builtin_return_address (0)) < wb::API_2016;
	return Added (vid, id, old, LightEmitter::LT_SPOT, (OBJHANDLE)vessel, pos, dir, range, att0, att1, att2, umbra, penumbra, diffuse, specular, ambient);
}

// Orbiter's light i: the helper's object of it in the caller's layout; one the bridge didn't make is a mirror filled at each call
const LightEmitter *VESSEL::GetLightEmitter (DWORD i) const
{
	uint32_t vid = VID;
	WbWriter &w = wb::Begin (L_GetLight); w.u32 (vid); w.u32 (i);
	WbReader r = wb::Call ();
	uint32_t id = r.u32 ();
	int type = r.i32 (), vis = r.i32 ();
	uint32_t href = r.u32 ();
	bool active = r.b ();
	COLOUR4 col[3];
	r.raw (col, sizeof col);
	VECTOR3 pos, dir;
	r.v3 (pos.data); r.v3 (dir.data);
	double in = r.f64 (), range = r.f64 (), att[3], umbra, penumbra;
	att[0] = r.f64 (); att[1] = r.f64 (); att[2] = r.f64 ();
	umbra = r.f64 (); penumbra = r.f64 ();
	if (r.bad || !id) return 0;
	bool old = wb::CallerApi (__builtin_return_address (0)) < wb::API_2016;
	Rec *rec = RecOf (vid, id, old);
	if (rec && !rec->mirror) return (const LightEmitter *)rec->l;
	if (!rec) {
		void *l = Make (old, (LightEmitter::TYPE)type, (OBJHANDLE)wb::Ptr (href), pos, dir, range, att[0], att[1], att[2], umbra, penumbra, col[0], col[1], col[2]);
		rec = AddRec (l, vid, id, old, true);
	}
	View v = ViewOf (rec->l, old);
	*v.ltype = (LightEmitter::TYPE)type;
	if (v.vis && vis >= 0) *v.vis = (LightEmitter::VISIBILITY)vis;
	*v.hRef = (OBJHANDLE)wb::Ptr (href); *v.active = active;
	*v.diff = col[0]; *v.spec = col[1]; *v.ambi = col[2];
	*v.lpos = pos; *v.pos = v.lpos; *v.ldir = dir; *v.dir = v.ldir; *v.lintens = in; *v.intens = v.lintens;
	v = ViewOf (rec->l, old);
	if (v.range) { *v.range = range; v.att[0] = att[0]; v.att[1] = att[1]; v.att[2] = att[2]; }
	if (v.umbra) { *v.umbra = umbra; *v.penumbra = penumbra; }
	for (int k = 0; k < 3; k++) if (rec->ref[k].id) { wb::LiveDropTag (vid, wb::TAG_LIGHT, rec->id << 2 | k); rec->ref[k] = {0, 0}; }
	rec->n = Size (rec->l, old);
	memcpy (rec->last, rec->l, rec->n);
	return (const LightEmitter *)rec->l;
}

// Orbiter's light goes first, then the helper's objects of it (the module's pointer is gone as on Windows)
bool VESSEL::DelLightEmitter (LightEmitter *l) const
{
	uint32_t vid = VID;
	Rec *rec = RecOf (l);
	if (!rec || rec->vid != vid) return false;
	uint32_t id = rec->id;
	WbWriter &w = wb::Begin (L_DelLight); w.u32 (vid); w.u32 (id);
	WbReader r = wb::Call ();
	if (!r.b ()) return false;
	for (uint32_t i = g_nrecs; i-- > 0; ) if (g_recs[i].vid == vid && g_recs[i].id == id) DropRec (i, true);
	return true;
}

void VESSEL::ClearLightEmitters () const
{
	uint32_t vid = VID;
	wb::Begin (L_ClearLights).u32 (vid);
	wb::Call ();
	for (uint32_t i = g_nrecs; i-- > 0; ) if (g_recs[i].vid == vid) DropRec (i, true);
}

// a deleted vessel: Orbiter stops reading its module's memory and its lights are gone (the objects stay: the module's ovcExit runs after)
void wb::VesselGone (uint32_t vid)
{
	LiveDrop (vid);
	WatchGone (vid);
	for (uint32_t i = g_nrecs; i-- > 0; ) if (g_recs[i].vid == vid) DropRec (i, false);
}

// a light whose object changed with no setter (an inline one, a member-wise assignment the module compiled in) goes to Orbiter after the call
bool wb::LightsChanged ()
{
	for (uint32_t i = 0; i < g_nrecs; i++) if (Moved (g_recs[i])) return true;
	return false;
}

void wb::LightsStore ()
{
	for (uint32_t i = 0; i < g_nrecs; i++) if (Moved (g_recs[i])) Send (g_recs[i]);
}
