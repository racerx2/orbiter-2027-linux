// WineBridge helper: a vessel's beacons, exhausts and animations as Orbiter gives them back, and its scenario helpers. Objects of a
// module Orbiter keeps reading (2006 animation components, transforms) are watched: a changed one goes again at the end of the call
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdlib.h>
#include "wb_helper.h"

#define VID (wb::Id (vessel))
static void V3w (WbWriter &w, const VECTOR3 &v) { w.v3 (v.data); }
static VECTOR3 V3r (WbReader &r) { VECTOR3 v; r.v3 (v.data); return v; }

namespace {
	// copies kept for the module: one per vessel and index, as long as the helper runs
	template <class T> struct Kept {
		struct E { uint32_t vid, idx; T *t; };
		E *e = 0;
		uint32_t n = 0, cap = 0;
		T *Get (uint32_t vid, uint32_t idx)
		{
			for (uint32_t i = 0; i < n; i++) if (e[i].vid == vid && e[i].idx == idx) return e[i].t;
			if (n == cap) { cap = cap ? cap * 2 : 16; e = (E *)realloc (e, cap * sizeof (E)); }
			T *t = (T *)calloc (1, sizeof (T));
			e[n++] = {vid, idx, t};
			return t;
		}
	};
	struct BeaconCopy { BEACONLIGHTSPEC b; VECTOR3 pos, col; };
	struct ExhaustCopy { double level; VECTOR3 lpos, ldir; };
	struct NameCopy { char s[256]; };
	Kept<BeaconCopy> g_beacons;
	Kept<ExhaustCopy> g_exhausts;
	Kept<NameCopy> g_names;
}

// ---------------------------------------------------------------------
// beacons: Orbiter's pointer is the module's own spec for a beacon the bridge added, else a copy filled at each call

const BEACONLIGHTSPEC *VESSEL::GetBeacon (DWORD idx) const
{
	uint32_t vid = VID;
	WbWriter &w = wb::Begin (L_GetBeacon); w.u32 (vid); w.u32 (idx);
	WbReader r = wb::Call ();
	uint32_t item = r.u32 ();
	bool has = r.b ();
	DWORD shape = r.u32 ();
	VECTOR3 pos = V3r (r), col = V3r (r);
	double size = r.f64 (), falloff = r.f64 (), period = r.f64 (), duration = r.f64 (), tofs = r.f64 ();
	bool active = r.b ();
	if (r.bad || !has) return 0;
	if (item) if (const void *p = wb::LiveAddr (item)) return (const BEACONLIGHTSPEC *)p;
	BeaconCopy *c = g_beacons.Get (vid, idx);
	c->pos = pos; c->col = col;
	c->b.shape = shape; c->b.pos = &c->pos; c->b.col = &c->col;
	c->b.size = size; c->b.falloff = falloff; c->b.period = period; c->b.duration = duration; c->b.tofs = tofs; c->b.active = active;
	return &c->b;
}

// Orbiter's beacon goes first, then the helper stops reading the module's spec
bool VESSEL::DelBeacon (BEACONLIGHTSPEC *bs)
{
	uint32_t vid = VID, id = wb::LiveFind (vid, WB_LIVE_BEACON, bs);
	if (!id) return false;
	WbWriter &w = wb::Begin (L_DelBeacon); w.u32 (vid); w.u32 (id);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	if (ok) wb::LiveDropTag (vid, wb::TAG_BEACON, id);
	return ok;
}

void VESSEL::ClearBeacons ()
{
	uint32_t vid = VID;
	wb::Begin (L_ClearBeacons).u32 (vid);
	wb::Call ();
	wb::LiveDropTagKind (vid, wb::TAG_BEACON);
}

// ---------------------------------------------------------------------
// exhausts

bool VESSEL::GetExhaustSpec (UINT idx, double *lscale, double *wscale, VECTOR3 *pos, VECTOR3 *dir, SURFHANDLE *tex) const
{
	WbWriter &w = wb::Begin (L_GetExhaustValues); w.u32 (VID); w.u32 (idx);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	double ls = r.f64 (), ws = r.f64 ();
	VECTOR3 p = V3r (r), d = V3r (r);
	uint32_t t = r.u32 ();
	if (!ok || r.bad) return false;
	*lscale = ls; *wscale = ws; *pos = p; *dir = d; *tex = (SURFHANDLE)wb::Ptr (t);
	return true;
}

// a copy of Orbiter's spec: what Orbiter reads from the module's memory is the module's pointer, the rest the helper's copy of the value
bool VESSEL::GetExhaustSpec (UINT idx, EXHAUSTSPEC *spec)
{
	uint32_t vid = VID;
	WbWriter &w = wb::Begin (L_GetExhaustSpec); w.u32 (vid); w.u32 (idx);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	uint32_t th = r.u32 ();
	double lsize = r.f64 (), wsize = r.f64 (), lofs = r.f64 (), modulate = r.f64 ();
	uint32_t tex = r.u32 (), flags = r.u32 (), id = r.u32 ();
	uint32_t item[3];
	bool has[3];
	double level = 0;
	VECTOR3 lpos {}, ldir {};
	item[0] = r.u32 (); has[0] = r.b (); level = r.f64 ();
	item[1] = r.u32 (); has[1] = r.b (); lpos = V3r (r);
	item[2] = r.u32 (); has[2] = r.b (); ldir = V3r (r);
	if (!ok || r.bad || !spec) return false;
	ExhaustCopy *c = g_exhausts.Get (vid, idx);
	c->level = level; c->lpos = lpos; c->ldir = ldir;
	const void *own[3] = {&c->level, &c->lpos, &c->ldir};
	void *p[3];
	for (int k = 0; k < 3; k++) {
		const void *m = item[k] ? wb::LiveAddr (item[k]) : 0;
		p[k] = m ? (void *)m : has[k] ? (void *)own[k] : 0;
	}
	spec->th = (THRUSTER_HANDLE)wb::Ptr (th);
	spec->level = (double *)p[0]; spec->lpos = (VECTOR3 *)p[1]; spec->ldir = (VECTOR3 *)p[2];
	spec->lsize = lsize; spec->wsize = wsize; spec->lofs = lofs; spec->modulate = modulate;
	spec->tex = (SURFHANDLE)wb::Ptr (tex); spec->flags = flags; spec->id = id;
	return true;
}

// ---------------------------------------------------------------------
// names, handles, keys, scenario lines

const char *VESSEL::GetMeshName (UINT idx) const
{
	uint32_t vid = VID;
	WbWriter &w = wb::Begin (L_GetMeshName); w.u32 (vid); w.u32 (idx);
	WbReader r = wb::Call ();
	bool has = r.b ();
	uint32_t n;
	const char *s = r.str (&n);
	if (r.bad || !has) return 0;
	NameCopy *c = g_names.Get (vid, idx);
	if (n > sizeof c->s - 1) n = sizeof c->s - 1;
	memcpy (c->s, s, n);
	c->s[n] = 0;
	return c->s;
}

SUPERVESSELHANDLE VESSEL::GetSupervessel () const
{
	wb::Begin (L_GetSupervessel).u32 (VID);
	WbReader r = wb::Call ();
	return (SUPERVESSELHANDLE)wb::Ptr (r.u32 ());
}

// as Orbiter's: a static 0
double *VESSEL::GetMainThrustModPtr () const
{
	static double dummy = 0.0;
	return &dummy;
}

int VESSEL::SendBufferedKey (DWORD key, bool down, char *kstate)
{
	WbWriter &w = wb::Begin (L_SendBufferedKey); w.u32 (VID); w.u32 (key); w.b (down);
	w.b (kstate != 0);
	if (kstate) w.raw (kstate, 256);
	WbReader r = wb::Call ();
	return r.i32 ();
}

// Orbiter only reads the line
void VESSEL::ParseScenarioLine (char *line, VESSELSTATUS *status) const
{
	WbWriter &w = wb::Begin (L_ParseScenarioLine); w.u32 (VID); w.str (line ? line : "");
	wb::PutVS (w, status);
	WbReader r = wb::Call ();
	VESSELSTATUS vs = *status;
	wb::GetVS (r, &vs);
	if (!r.bad) *status = vs;
}

OBJHANDLE VESSEL::Create (const char *name, const char *classname, const VESSELSTATUS &status)
{
	return oapiCreateVessel (name, classname, status);
}

// ---------------------------------------------------------------------
// watched objects: a 2006 animation component, a module's transform of a component; their state on the wire is the snapshot

namespace {
	enum { W_ANIMCOMP, W_TRANSFORM };
	// last: the object's bytes and its group list as last sent (compared raw; serialised only to send)
	struct Watch { int kind, type; uint32_t vid, id; const void *obj; uint8_t *last; uint32_t n; };
	Watch *g_watch;
	uint32_t g_nwatch, g_capwatch;
	uint8_t g_raw[sizeof (MGROUP_ROTATE) + 4 * 4096 + 64], g_buf[sizeof g_raw + 256];

	uint32_t Raw (const Watch &x)
	{
		const UINT *grp;
		UINT ngrp;
		uint32_t size;
		if (x.kind == W_ANIMCOMP) {
			const ANIMCOMP *c = (const ANIMCOMP *)x.obj;
			size = sizeof *c; grp = c->grp; ngrp = c->ngrp;
		} else {
			const MGROUP_TRANSFORM *t = (const MGROUP_TRANSFORM *)x.obj;
			size = x.type == MGROUP_TRANSFORM::ROTATE ? sizeof (MGROUP_ROTATE) : x.type == MGROUP_TRANSFORM::TRANSLATE ? sizeof (MGROUP_TRANSLATE) :
				x.type == MGROUP_TRANSFORM::SCALE ? sizeof (MGROUP_SCALE) : sizeof (MGROUP_TRANSFORM);
			grp = t->grp; ngrp = t->ngrp;
		}
		if (ngrp > 4096) ngrp = 4096;
		memcpy (g_raw, x.obj, size);
		if (grp) { memcpy (g_raw + size, grp, ngrp * sizeof (UINT)); size += ngrp * sizeof (UINT); }
		return size;
	}
	bool Same (const Watch &x, uint32_t n) { return n == x.n && !memcmp (g_raw, x.last, n); }
	void Keep (Watch &x, uint32_t n)
	{
		x.last = (uint8_t *)realloc (x.last, n ? n : 1);
		memcpy (x.last, g_raw, n);
		x.n = n;
	}

	void PutAnimComp (WbWriter &w, const ANIMCOMP *c)
	{
		w.b (c->grp != 0); w.u32 (c->ngrp);
		for (UINT i = 0; c->grp && i < c->ngrp && i < 4096; i++) w.u32 (c->grp[i]);
		w.f64 (c->state0); w.f64 (c->state1);
		w.raw (&c->trans.P, sizeof c->trans.P); w.i32 (c->trans.nmesh); w.i32 (c->trans.ngrp); w.i32 (c->trans.transform);
	}
	void PutTransform (WbWriter &w, const MGROUP_TRANSFORM *t, int type)
	{
		w.i32 (type); w.u32 (t->mesh);
		w.b (t->grp != 0); w.u32 (t->ngrp);
		for (UINT i = 0; t->grp && i < t->ngrp && i < 4096; i++) w.u32 (t->grp[i]);
		switch (type) {
		case MGROUP_TRANSFORM::ROTATE: { const MGROUP_ROTATE *m = static_cast<const MGROUP_ROTATE *> (t); V3w (w, m->ref); V3w (w, m->axis); w.f64 (m->angle); break; }
		case MGROUP_TRANSFORM::TRANSLATE: V3w (w, static_cast<const MGROUP_TRANSLATE *> (t)->shift); break;
		case MGROUP_TRANSFORM::SCALE: { const MGROUP_SCALE *m = static_cast<const MGROUP_SCALE *> (t); V3w (w, m->ref); V3w (w, m->scale); break; }
		default: break;
		}
	}
	// the object as the wire has it (L_AddAnimComp, L_Watched)
	uint32_t Wire (const Watch &x)
	{
		WbWriter w (g_buf, sizeof g_buf);
		if (x.kind == W_ANIMCOMP) PutAnimComp (w, (const ANIMCOMP *)x.obj);
		else PutTransform (w, (const MGROUP_TRANSFORM *)x.obj, x.type);
		return w.over ? 0 : (uint32_t)(w.p - g_buf);
	}
	Watch &AddWatch (int kind, int type, uint32_t vid, uint32_t id, const void *obj)
	{
		if (g_nwatch == g_capwatch) { g_capwatch = g_capwatch ? g_capwatch * 2 : 32; g_watch = (Watch *)realloc (g_watch, g_capwatch * sizeof (Watch)); }
		Watch &x = g_watch[g_nwatch++];
		x = {kind, type, vid, id, obj, 0, 0};
		Keep (x, Raw (x));
		return x;
	}
}

// Orbiter reads the module's component when the sequence's state changes: its fields and group list go now and whenever they change
bool VESSEL::AddAnimComp (UINT seq, ANIMCOMP *comp)
{
	if (!comp) return false;
	uint32_t vid = VID, id = wb::Id (comp);
	Watch &x = AddWatch (W_ANIMCOMP, 0, vid, id, comp);
	uint32_t n = Wire (x);
	WbWriter &w = wb::Begin (L_AddAnimComp); w.u32 (vid); w.u32 (seq); w.u32 (id); w.u32 (n); w.raw (g_buf, n);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	if (!ok) { free (g_watch[g_nwatch - 1].last); g_nwatch--; } // the one just added
	return ok;
}

void wb::TransformMade (uint32_t vid, const void *comp, void *trans)
{
	AddWatch (W_TRANSFORM, ((MGROUP_TRANSFORM *)trans)->Type (), vid, wb::Id (comp), trans);
}

bool wb::WatchChanged ()
{
	for (uint32_t i = 0; i < g_nwatch; i++) if (!Same (g_watch[i], Raw (g_watch[i]))) return true;
	return AnimsChanged ();
}

void wb::WatchStore ()
{
	for (uint32_t i = 0; i < g_nwatch; i++) {
		Watch &x = g_watch[i];
		uint32_t n = Raw (x);
		if (Same (x, n)) continue;
		Keep (x, n);
		uint32_t m = Wire (x);
		WbWriter &w = wb::Begin (L_Watched); w.i32 (x.kind); w.u32 (x.vid); w.u32 (x.id); w.u32 (m); w.raw (g_buf, m);
		wb::Call ();
	}
	AnimsStore ();
}

// a deleted vessel: its watched objects go (its module may free them)
void wb::WatchGone (uint32_t vid)
{
	uint32_t k = 0;
	for (uint32_t i = 0; i < g_nwatch; i++)
		if (g_watch[i].vid != vid) g_watch[k++] = g_watch[i];
		else free (g_watch[i].last);
	g_nwatch = k;
	AnimsGone (vid);
}

// ---------------------------------------------------------------------
// GetAnimPtr: a copy of Orbiter's animations; Orbiter's states come in before each call from Linux, a state the module writes goes
// into Orbiter's array at the end of the call

namespace {
	struct AnimCopy {
		uint32_t vid, n, ncomp;
		ANIMATION *anim;
		ANIMATIONCOMP *comp;
		ANIMATIONCOMP **lists; // each animation's components, then each component's children
		MGROUP_TRANSFORM **own; // the helper's transforms (a component the bridge didn't make from a module's) and their group lists
		UINT **owngrp;
		uint32_t nown;
		double *seen;           // the states as Orbiter had them
	};
	AnimCopy *g_anims;
	uint32_t g_nanims, g_capanims;

	AnimCopy *AnimsOf (uint32_t vid)
	{
		for (uint32_t i = 0; i < g_nanims; i++) if (g_anims[i].vid == vid) return &g_anims[i];
		return 0;
	}
	void Free (AnimCopy &c)
	{
		for (uint32_t i = 0; i < c.nown; i++) { delete c.own[i]; free (c.owngrp[i]); }
		free (c.anim); free (c.comp); free (c.lists); free (c.own); free (c.owngrp); free (c.seen);
		memset (&c, 0, sizeof c);
	}
	MGROUP_TRANSFORM *ModuleTransform (uint32_t vid, uint32_t comp)
	{
		for (uint32_t i = 0; i < g_nwatch; i++)
			if (g_watch[i].kind == W_TRANSFORM && g_watch[i].vid == vid && g_watch[i].id == comp) return (MGROUP_TRANSFORM *)g_watch[i].obj;
		return 0;
	}
	MGROUP_TRANSFORM *ReadTransform (WbReader &r, UINT *&grp)
	{
		int type = r.i32 ();
		UINT mesh = r.u32 ();
		bool list = r.b ();
		UINT ngrp = r.u32 ();
		grp = 0;
		if (list) {
			grp = (UINT *)malloc ((ngrp ? ngrp : 1) * sizeof (UINT)); // kept with the transform, as the module's list would be
			for (UINT i = 0; i < ngrp && !r.bad; i++) grp[i] = r.u32 ();
		}
		switch (type) {
		case MGROUP_TRANSFORM::ROTATE: { VECTOR3 ref = V3r (r), axis = V3r (r); float a = (float)r.f64 (); return new MGROUP_ROTATE (mesh, grp, ngrp, ref, axis, a); }
		case MGROUP_TRANSFORM::TRANSLATE: { VECTOR3 s = V3r (r); return new MGROUP_TRANSLATE (mesh, grp, ngrp, s); }
		case MGROUP_TRANSFORM::SCALE: { VECTOR3 ref = V3r (r), s = V3r (r); return new MGROUP_SCALE (mesh, grp, ngrp, ref, s); }
		default: return new MGROUP_TRANSFORM (mesh, grp, ngrp);
		}
	}
	// the states only (the shape as at GetAnimPtr)
	void ReadStates (AnimCopy &c, WbReader &r)
	{
		uint32_t n = r.u32 ();
		for (uint32_t i = 0; i < n && !r.bad; i++) {
			double d = r.f64 (), s = r.f64 ();
			if (i < c.n) { c.anim[i].defstate = d; c.anim[i].state = s; c.seen[i] = s; }
		}
	}
}

UINT VESSEL::GetAnimPtr (ANIMATION **anim) const
{
	uint32_t vid = VID;
	wb::Begin (L_GetAnims).u32 (vid);
	WbReader r = wb::Call ();
	uint32_t n = r.u32 (), ncomp = r.u32 ();
	if (r.bad || n > 65536 || ncomp > 65536) { *anim = 0; return 0; }
	AnimCopy *c = AnimsOf (vid);
	if (!c) {
		if (g_nanims == g_capanims) { g_capanims = g_capanims ? g_capanims * 2 : 8; g_anims = (AnimCopy *)realloc (g_anims, g_capanims * sizeof (AnimCopy)); }
		c = &g_anims[g_nanims++];
		memset (c, 0, sizeof *c);
	} else Free (*c);
	c->vid = vid; c->n = n; c->ncomp = ncomp;
	c->anim = (ANIMATION *)calloc (n ? n : 1, sizeof (ANIMATION));
	c->seen = (double *)calloc (n ? n : 1, sizeof (double));
	c->comp = (ANIMATIONCOMP *)calloc (ncomp ? ncomp : 1, sizeof (ANIMATIONCOMP));
	c->lists = (ANIMATIONCOMP **)calloc (2 * ncomp + 1, sizeof (ANIMATIONCOMP *));
	c->own = (MGROUP_TRANSFORM **)calloc (ncomp + 1, sizeof (MGROUP_TRANSFORM *));
	c->owngrp = (UINT **)calloc (ncomp + 1, sizeof (UINT *));
	int32_t *parent = (int32_t *)calloc (ncomp + 1, sizeof (int32_t));
	uint32_t k = 0;
	for (uint32_t i = 0; i < n && !r.bad; i++) { // each animation: its states and its components (indices into all of them)
		c->anim[i].defstate = r.f64 (); c->anim[i].state = r.f64 (); c->seen[i] = c->anim[i].state;
		uint32_t m = r.u32 ();
		if (k + m > ncomp) { r.bad = true; break; }
		c->anim[i].ncomp = m;
		c->anim[i].comp = c->lists + k;
		for (uint32_t j = 0; j < m && !r.bad; j++, k++) {
			c->lists[k] = &c->comp[k];
			ANIMATIONCOMP &a = c->comp[k];
			a.state0 = r.f64 (); a.state1 = r.f64 ();
			uint32_t id = r.u32 ();
			parent[k] = r.i32 ();
			bool has = r.b ();
			MGROUP_TRANSFORM *t = 0;
			if (has) { // Linux's copy; the module's own object when the bridge made the component from it
				UINT *grp;
				MGROUP_TRANSFORM *copy = ReadTransform (r, grp);
				t = ModuleTransform (vid, id);
				if (t) { delete copy; free (grp); }
				else { t = copy; c->own[c->nown] = copy; c->owngrp[c->nown++] = grp; }
			}
			a.trans = t;
		}
	}
	// parents and children, as Orbiter links them
	uint32_t at = ncomp;
	for (uint32_t i = 0; i < k; i++) {
		c->comp[i].parent = parent[i] >= 0 && (uint32_t)parent[i] < k ? &c->comp[parent[i]] : 0;
		c->comp[i].children = c->lists + at;
		for (uint32_t j = 0; j < k; j++) if (parent[j] == (int32_t)i) c->lists[at++] = &c->comp[j];
		c->comp[i].nchildren = (UINT)(c->lists + at - c->comp[i].children);
	}
	free (parent);
	*anim = c->anim;
	return r.bad ? 0 : n;
}

bool wb::AnimsCopied ()
{
	return g_nanims != 0;
}

void wb::AnimsRefresh ()
{
	for (uint32_t i = 0; i < g_nanims; i++) {
		if (!g_anims[i].anim) continue;
		wb::Begin (L_GetAnimStates).u32 (g_anims[i].vid);
		WbReader r = wb::Call ();
		ReadStates (g_anims[i], r);
	}
}

bool wb::AnimsChanged ()
{
	for (uint32_t i = 0; i < g_nanims; i++)
		for (uint32_t j = 0; j < g_anims[i].n; j++) if (g_anims[i].anim[j].state != g_anims[i].seen[j]) return true;
	return false;
}

void wb::AnimsStore ()
{
	for (uint32_t i = 0; i < g_nanims; i++) {
		AnimCopy &c = g_anims[i];
		uint32_t m = 0;
		for (uint32_t j = 0; j < c.n; j++) if (c.anim[j].state != c.seen[j]) m++;
		if (!m) continue;
		WbWriter &w = wb::Begin (L_SetAnimStates); w.u32 (c.vid); w.u32 (m);
		for (uint32_t j = 0; j < c.n; j++) if (c.anim[j].state != c.seen[j]) { w.u32 (j); w.f64 (c.anim[j].state); c.seen[j] = c.anim[j].state; }
		wb::Call ();
	}
}

void wb::AnimsGone (uint32_t vid)
{
	for (uint32_t i = 0; i < g_nanims; i++) if (g_anims[i].vid == vid) { Free (g_anims[i]); g_anims[i] = g_anims[--g_nanims]; return; }
}
