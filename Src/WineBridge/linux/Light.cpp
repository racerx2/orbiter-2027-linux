// WineBridge: a Windows module's lights. The helper sends a light's state after every change; Orbiter's light takes it as it would
// hold the module's object on Windows (its fields through member pointers of a class derived from the lights)
#include "Orbitersdk.h"
#include "Bridge.h"
#include "Manual.h"

static VECTOR3 V3 (WbReader &r) { VECTOR3 v; r.v3 (v.data); return v; }

namespace {
	struct Acc : SpotLight {
		static void Put (WbWriter &w, const LightEmitter *l, WbBridge &b) // as the helper reads it (L_GetLight)
		{
			LightEmitter *m = const_cast<LightEmitter *> (l);
			LightEmitter::TYPE t = m->*&Acc::ltype;
			w.i32 (t); w.i32 (m->*&Acc::visibility); w.u32 (b.Id (m->*&Acc::hRef)); w.b (m->*&Acc::active);
			w.raw (&(m->*&Acc::col_diff), sizeof (COLOUR4)); w.raw (&(m->*&Acc::col_spec), sizeof (COLOUR4)); w.raw (&(m->*&Acc::col_ambi), sizeof (COLOUR4));
			w.v3 ((m->*&Acc::pos)->data); w.v3 ((m->*&Acc::dir)->data); w.f64 (*(m->*&Acc::intens));
			bool point = t == LT_POINT || t == LT_SPOT, spot = t == LT_SPOT;
			PointLight *p = static_cast<PointLight *> (m);
			SpotLight *s = static_cast<SpotLight *> (m);
			w.f64 (point ? p->*&Acc::range : 0);
			for (int i = 0; i < 3; i++) w.f64 (point ? (p->*&Acc::att)[i] : 0);
			w.f64 (spot ? s->*&Acc::umbra : 0); w.f64 (spot ? s->*&Acc::penumbra : 0);
		}
		struct State {
			bool active;
			OBJHANDLE hRef;
			int vis;
			COLOUR4 col[3];
			VECTOR3 lpos, ldir;
			double lintens;
			int mode[3];
			const void *ref[3];
			bool point, spot;
			double range, att[3], umbra, penumbra;
		};
		static void Set (LightEmitter *l, const State &s) // the attachment first: Activate tells the vessel only of an attached light
		{
			l->*&Acc::hRef = s.hRef;
			if (s.vis >= 0) l->*&Acc::visibility = (VISIBILITY)s.vis;
			l->*&Acc::col_diff = s.col[0]; l->*&Acc::col_spec = s.col[1]; l->*&Acc::col_ambi = s.col[2];
			l->*&Acc::lpos = s.lpos; l->*&Acc::ldir = s.ldir; l->*&Acc::lintens = s.lintens;
			if (s.mode[0] != 1) l->*&Acc::pos = s.mode[0] ? (const VECTOR3 *)s.ref[0] : &(l->*&Acc::lpos);
			if (s.mode[1] != 1) l->*&Acc::dir = s.mode[1] ? (const VECTOR3 *)s.ref[1] : &(l->*&Acc::ldir);
			if (s.mode[2] != 1) l->*&Acc::intens = s.mode[2] ? (const double *)s.ref[2] : &(l->*&Acc::lintens);
			LightEmitter::TYPE t = l->*&Acc::ltype;
			if (s.point && (t == LT_POINT || t == LT_SPOT)) {
				PointLight *p = static_cast<PointLight *> (l);
				p->*&Acc::range = s.range;
				for (int i = 0; i < 3; i++) (p->*&Acc::att)[i] = s.att[i];
			}
			if (s.spot && t == LT_SPOT) {
				SpotLight *p = static_cast<SpotLight *> (l);
				p->*&Acc::umbra = s.umbra; p->*&Acc::penumbra = s.penumbra;
			}
			if (l->IsActive () != s.active) l->Activate (s.active);
		}
	};

	LightEmitter *LightOf (VESSEL *v, const void *l)
	{
		if (!v || !l) return nullptr;
		for (DWORD i = 0; i < v->LightEmitterCount (); i++)
			if (v->GetLightEmitter (i) == l) return const_cast<LightEmitter *> (v->GetLightEmitter (i));
		return nullptr;
	}
}

bool WbServeLight (uint32_t fn, WbReader &r, WbWriter &w)
{
	switch (fn) {
	case L_LightState: case L_GetLight: case L_DelLight: case L_ClearLights: break;
	default: return false;
	}
	WbBridge &b = WbBridge::Get ();
	uint32_t vid = r.u32 ();
	VESSEL *v = b.Vessel (vid);
	switch (fn) {
	case L_LightState: {
		LightEmitter *l = LightOf (v, b.Ptr (r.u32 ()));
		Acc::State s;
		s.active = r.b (); s.hRef = (OBJHANDLE)b.Ptr (r.u32 ()); s.vis = r.i32 ();
		r.raw (s.col, sizeof s.col);
		s.lpos = V3 (r); s.ldir = V3 (r); s.lintens = r.f64 ();
		static const int kind[3] = {WB_LIVE_VECTOR3, WB_LIVE_VECTOR3, WB_LIVE_DOUBLE};
		for (int k = 0; k < 3; k++) {
			s.mode[k] = r.i32 ();
			s.ref[k] = s.mode[k] == 2 ? WbLiveRef (r, kind[k]) : nullptr;
			if (s.mode[k] < 0 || s.mode[k] > 2 || (s.mode[k] == 2 && !s.ref[k])) r.bad = true;
		}
		s.point = r.b ();
		s.range = 0; s.att[0] = s.att[1] = s.att[2] = 0; s.umbra = s.penumbra = 0;
		if (s.point) { s.range = r.f64 (); s.att[0] = r.f64 (); s.att[1] = r.f64 (); s.att[2] = r.f64 (); }
		s.spot = r.b ();
		if (s.spot) { s.umbra = r.f64 (); s.penumbra = r.f64 (); }
		if (l && !r.bad) Acc::Set (l, s);
		break; }
	case L_GetLight: {
		uint32_t i = r.u32 ();
		const LightEmitter *l = v && !r.bad && i < v->LightEmitterCount () ? v->GetLightEmitter (i) : nullptr;
		w.u32 (b.Id (l));
		if (l) Acc::Put (w, l, b);
		break; }
	case L_DelLight: {
		LightEmitter *l = LightOf (v, b.Ptr (r.u32 ()));
		bool ok = false;
		if (l && !r.bad) ok = v->DelLightEmitter (l);
		w.b (ok);
		break; }
	case L_ClearLights:
		if (v && !r.bad) v->ClearLightEmitters ();
		break;
	}
	if (r.bad) b.BadArgs (fn);
	else if (!v) b.NoVessel (fn, vid);
	return true;
}
