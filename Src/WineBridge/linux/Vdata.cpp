// WineBridge: a vessel's beacons, exhausts and animations as Orbiter gives them back, its 2006 animation components and scenario helpers
#include "Orbitersdk.h"
#include "Bridge.h"
#include "Manual.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

static VECTOR3 V3 (WbReader &r) { VECTOR3 v; r.v3 (v.data); return v; }
static std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }

namespace {
	// a module's 2006 animation component: Orbiter keeps a pointer to it
	struct Comp { ANIMCOMP ac {}; std::vector<UINT> grp; };
	std::deque<Comp> &g_comps = *new std::deque<Comp>;
	std::unordered_map<uint64_t, Comp *> &g_compOf = *new std::unordered_map<uint64_t, Comp *>; // by (vessel, the helper's id)
	std::deque<std::vector<UINT>> &g_lists = *new std::deque<std::vector<UINT>>; // group lists of transforms a module changed

	bool ReadComp (WbReader &r, Comp &c)
	{
		bool list = r.b ();
		uint32_t n = r.u32 ();
		if (r.bad || n > 4096) { r.bad = true; return false; }
		std::vector<UINT> grp (list ? n : 0);
		for (UINT &g : grp) g = r.u32 ();
		double s0 = r.f64 (), s1 = r.f64 ();
		MESHGROUP_TRANSFORM t {};
		r.raw (&t.P, sizeof t.P);
		t.nmesh = r.i32 (); t.ngrp = r.i32 ();
		int type = r.i32 ();
		if (r.bad) return false;
		c.grp = std::move (grp);
		if (list && c.grp.empty ()) c.grp.push_back (0);
		c.ac.grp = list ? c.grp.data () : nullptr;
		c.ac.ngrp = n;
		c.ac.state0 = s0; c.ac.state1 = s1;
		t.transform = type == MESHGROUP_TRANSFORM::ROTATE ? MESHGROUP_TRANSFORM::ROTATE : type == MESHGROUP_TRANSFORM::SCALE ? MESHGROUP_TRANSFORM::SCALE :
			MESHGROUP_TRANSFORM::TRANSLATE;
		c.ac.trans = t;
		return true;
	}

	// a module changed its transform: Orbiter's copy of it (WbVessel::Component made it) takes the new fields
	void ReadTransform (WbReader &r, MGROUP_TRANSFORM *t)
	{
		int type = r.i32 ();
		UINT mesh = r.u32 ();
		bool list = r.b ();
		uint32_t n = r.u32 ();
		if (r.bad || n > 4096) { r.bad = true; return; }
		std::vector<UINT> grp (list ? n : 0);
		for (UINT &g : grp) g = r.u32 ();
		VECTOR3 a {}, c {};
		double angle = 0;
		if (type == MGROUP_TRANSFORM::ROTATE) { a = V3 (r); c = V3 (r); angle = r.f64 (); }
		else if (type == MGROUP_TRANSFORM::TRANSLATE) a = V3 (r);
		else if (type == MGROUP_TRANSFORM::SCALE) { a = V3 (r); c = V3 (r); }
		if (r.bad || !t || t->Type () != type) return;
		t->mesh = mesh;
		if (list) {
			if (grp.empty ()) grp.push_back (0);
			t->grp = g_lists.emplace_back (std::move (grp)).data ();
		} else t->grp = nullptr;
		t->ngrp = n;
		switch (type) {
		case MGROUP_TRANSFORM::ROTATE: { MGROUP_ROTATE *m = static_cast<MGROUP_ROTATE *> (t); m->ref = a; m->axis = c; m->angle = (float)angle; break; }
		case MGROUP_TRANSFORM::TRANSLATE: static_cast<MGROUP_TRANSLATE *> (t)->shift = a; break;
		case MGROUP_TRANSFORM::SCALE: { MGROUP_SCALE *m = static_cast<MGROUP_SCALE *> (t); m->ref = a; m->scale = c; break; }
		}
	}

	void PutTransform (WbWriter &w, MGROUP_TRANSFORM *t)
	{
		MGROUP_TRANSFORM::TYPE type = t->Type ();
		w.i32 (type); w.u32 (t->mesh);
		w.b (t->grp != nullptr); w.u32 (t->ngrp);
		for (UINT i = 0; t->grp && i < t->ngrp; i++) w.u32 (t->grp[i]);
		switch (type) {
		case MGROUP_TRANSFORM::ROTATE: { MGROUP_ROTATE *m = static_cast<MGROUP_ROTATE *> (t); w.v3 (m->ref.data); w.v3 (m->axis.data); w.f64 (m->angle); break; }
		case MGROUP_TRANSFORM::TRANSLATE: w.v3 (static_cast<MGROUP_TRANSLATE *> (t)->shift.data); break;
		case MGROUP_TRANSFORM::SCALE: { MGROUP_SCALE *m = static_cast<MGROUP_SCALE *> (t); w.v3 (m->ref.data); w.v3 (m->scale.data); break; }
		default: break;
		}
	}

	uint64_t Key (uint32_t vid, uint32_t id) { return (uint64_t)vid << 32 | id; }
}

bool WbServeVdata (uint32_t fn, WbReader &r, WbWriter &w)
{
	if (fn < L_GetBeacon || fn > L_SetAnimStates || fn == L_Watched) return false;
	WbBridge &b = WbBridge::Get ();
	uint32_t vid = r.u32 ();
	VESSEL *v = b.Vessel (vid);
	switch (fn) {
	case L_GetBeacon: {
		DWORD idx = r.u32 ();
		const BEACONLIGHTSPEC *s = v && !r.bad ? v->GetBeacon (idx) : nullptr;
		static const VECTOR3 zero {};
		w.u32 (s ? WbLiveIdOf (s) : 0); w.b (s != nullptr);
		w.u32 (s ? s->shape : 0); w.v3 ((s && s->pos ? *s->pos : zero).data); w.v3 ((s && s->col ? *s->col : zero).data);
		w.f64 (s ? s->size : 0); w.f64 (s ? s->falloff : 0); w.f64 (s ? s->period : 0); w.f64 (s ? s->duration : 0); w.f64 (s ? s->tofs : 0);
		w.b (s && s->active);
		break; }
	case L_DelBeacon: {
		BEACONLIGHTSPEC *s = WbLiveBeacon (r.u32 ());
		w.b (v && s && !r.bad && v->DelBeacon (s));
		break; }
	case L_ClearBeacons:
		if (v && !r.bad) v->ClearBeacons ();
		break;
	case L_GetExhaustValues: {
		UINT idx = r.u32 ();
		double ls = 0, ws = 0;
		VECTOR3 p {}, d {};
		SURFHANDLE tex = nullptr;
		bool ok = v && !r.bad && v->GetExhaustSpec (idx, &ls, &ws, &p, &d, &tex);
		w.b (ok); w.f64 (ls); w.f64 (ws); w.v3 (p.data); w.v3 (d.data); w.u32 (b.Id (tex));
		break; }
	case L_GetExhaustSpec: {
		UINT idx = r.u32 ();
		EXHAUSTSPEC es {};
		bool ok = v && !r.bad && v->GetExhaustSpec (idx, &es);
		w.b (ok); w.u32 (b.Id (es.th)); w.f64 (es.lsize); w.f64 (es.wsize); w.f64 (es.lofs); w.f64 (es.modulate);
		w.u32 (b.Id (es.tex)); w.u32 (es.flags); w.u32 (es.id);
		static const VECTOR3 zero {};
		w.u32 (WbLiveIdOf (es.level)); w.b (es.level != nullptr); w.f64 (es.level ? *es.level : 0);
		w.u32 (WbLiveIdOf (es.lpos)); w.b (es.lpos != nullptr); w.v3 ((es.lpos ? *es.lpos : zero).data);
		w.u32 (WbLiveIdOf (es.ldir)); w.b (es.ldir != nullptr); w.v3 ((es.ldir ? *es.ldir : zero).data);
		break; }
	case L_GetMeshName: {
		UINT idx = r.u32 ();
		const char *s = v && !r.bad ? v->GetMeshName (idx) : nullptr;
		w.b (s != nullptr); w.str (s ? s : "");
		break; }
	case L_GetSupervessel:
		w.u32 (v ? b.Id (v->GetSupervessel ()) : 0);
		break;
	case L_SendBufferedKey: {
		DWORD key = r.u32 ();
		bool down = r.b (), has = r.b ();
		char ks[256] = {};
		if (has) r.raw (ks, sizeof ks);
		w.i32 (v && !r.bad ? v->SendBufferedKey (key, down, has ? ks : nullptr) : 0);
		break; }
	case L_ParseScenarioLine: {
		std::string line = Str (r);
		VESSELSTATUS vs {};
		WbGetVS (r, vs);
		if (v && !r.bad) v->ParseScenarioLine (line.data (), &vs);
		WbPutVS (w, vs);
		break; }
	case L_AddAnimComp: {
		UINT seq = r.u32 ();
		uint32_t id = r.u32 (), n = r.u32 ();
		(void)n;
		Comp &c = g_comps.emplace_back ();
		bool ok = ReadComp (r, c) && v && v->AddAnimComp (seq, &c.ac);
		if (ok) g_compOf[Key (vid, id)] = &c;
		w.b (ok);
		break; }
	case L_GetAnims: {
		ANIMATION *a = nullptr;
		UINT n = v ? v->GetAnimPtr (&a) : 0;
		std::vector<ANIMATIONCOMP *> all;
		for (UINT i = 0; i < n; i++) for (UINT j = 0; j < a[i].ncomp; j++) all.push_back (a[i].comp[j]);
		w.u32 (n); w.u32 ((uint32_t)all.size ());
		for (UINT i = 0, k = 0; i < n; i++) {
			w.f64 (a[i].defstate); w.f64 (a[i].state); w.u32 (a[i].ncomp);
			for (UINT j = 0; j < a[i].ncomp; j++, k++) {
				ANIMATIONCOMP *c = all[k];
				int32_t parent = -1;
				for (size_t q = 0; c->parent && q < all.size (); q++) if (all[q] == c->parent) parent = (int32_t)q;
				w.f64 (c->state0); w.f64 (c->state1); w.u32 (b.Id (c)); w.i32 (parent);
				w.b (c->trans != nullptr);
				if (c->trans) PutTransform (w, c->trans);
			}
		}
		break; }
	case L_GetAnimStates: {
		ANIMATION *a = nullptr;
		UINT n = v ? v->GetAnimPtr (&a) : 0;
		w.u32 (n);
		for (UINT i = 0; i < n; i++) { w.f64 (a[i].defstate); w.f64 (a[i].state); }
		break; }
	case L_SetAnimStates: { // the module wrote into the array: Orbiter's array takes it, as it was the module's on Windows
		uint32_t m = r.u32 ();
		ANIMATION *a = nullptr;
		UINT n = v ? v->GetAnimPtr (&a) : 0;
		for (uint32_t i = 0; i < m && !r.bad; i++) {
			UINT j = r.u32 ();
			double s = r.f64 ();
			if (!r.bad && j < n) a[j].state = s;
		}
		break; }
	}
	if (r.bad) b.BadArgs (fn);
	else if (!v) b.NoVessel (fn, vid);
	return true;
}

// a watched object of a module changed (L_Watched: i32 kind, u32 vessel, u32 id, u32 n, its bytes)
bool WbServeWatched (uint32_t fn, WbReader &r)
{
	if (fn != L_Watched) return false;
	WbBridge &b = WbBridge::Get ();
	int kind = r.i32 ();
	uint32_t vid = r.u32 (), id = r.u32 ();
	r.u32 ();
	if (kind == 0) {
		auto it = g_compOf.find (Key (vid, id));
		Comp tmp;
		ReadComp (r, it != g_compOf.end () ? *it->second : tmp);
	} else {
		ANIMATIONCOMP *c = (ANIMATIONCOMP *)b.Ptr (id);
		ReadTransform (r, c && b.Vessel (vid) ? c->trans : nullptr);
	}
	if (r.bad) b.BadArgs (fn);
	return true;
}
