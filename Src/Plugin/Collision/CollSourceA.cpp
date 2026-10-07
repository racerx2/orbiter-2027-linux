// not upstream: collision addon, E2's per-session vessel sources (design E2 2-9)
#include <algorithm>
#include <cmath>
#include <cstring>
#include "CollSourceA.h"
#include "CollBaseA.h"
#include "DentMath.h"
#include "OrbiterAPI.h"

namespace {
uint64_t HashD (uint64_t h, double d) { return CollFnv (&d, sizeof d, h); }
uint64_t HashV (uint64_t h, const VECTOR3 &v) { h = HashD (h, v.x); h = HashD (h, v.y); return HashD (h, v.z); }

void Fingerprint (const ANIMATIONCOMP *ac, uint64_t &fs, uint64_t &fm)
{
	const MGROUP_TRANSFORM *t = ac->trans;
	fs = CollFnv (nullptr, 0); fm = CollFnv (nullptr, 0);
	if (!t) return;
	int ty = (int)t->Type ();
	fs = CollFnv (&ty, sizeof ty, fs);
	fs = CollFnv (&t->mesh, sizeof t->mesh, fs);
	fs = CollFnv (&t->grp, sizeof t->grp, fs);
	fs = CollFnv (&t->ngrp, sizeof t->ngrp, fs);
	if (t->mesh == LOCALVERTEXLIST) return; // the client rewrites the point list
	if (t->grp && t->ngrp) fs = CollFnv (t->grp, t->ngrp * sizeof (UINT), fs);
	switch (t->Type ()) {
	case MGROUP_TRANSFORM::ROTATE: { auto *r = static_cast<const MGROUP_ROTATE *> (t); fs = CollFnv (&r->angle, sizeof r->angle, fs); fm = HashV (HashV (fm, r->ref), r->axis); break; }
	case MGROUP_TRANSFORM::TRANSLATE: { auto *r = static_cast<const MGROUP_TRANSLATE *> (t); fm = HashV (fm, r->shift); break; }
	case MGROUP_TRANSFORM::SCALE: { auto *r = static_cast<const MGROUP_SCALE *> (t); fs = HashV (fs, r->scale); fm = HashV (fm, r->ref); break; }
	default: break;
	}
}

bool ExcludesAll (const CollSidecar *sc)
{
	if (!sc) return false;
	bool ex = false;
	for (auto &r : sc->rule) {
		if (r.op == CollSideRule::EXCLUDE && r.sel.kind == CollSelector::ALL) ex = true;
		else if (r.op == CollSideRule::INCLUDE) ex = false;
	}
	return ex;
}
} // namespace

CollGeomSession::CollGeomSession (CollSdk &s, const CollCfgValues &c): sdk (s), cfg (c), bases (new CollBaseA) {}
CollGeomSession::~CollGeomSession () = default;

void CollGeomSession::ReadOrbiterCfg ()
{
	std::string text;
	if (sdk.ReadText (sdk.Resolve ("Orbiter.cfg"), text)) orbCfg.SetText (text);
	std::vector<std::string> warn;
	orbCfg.Dirs (dirs, &warn);
	for (auto &w : warn) sdk.Log (1, ("Collision: " + w).c_str ());
}

CollVesselSrc *CollGeomSession::Rec (uint32_t id) const
{
	if (id < vessel.size () && vessel[id]) return vessel[id].get ();
	for (auto &p : pending) if (p->id == id) return p.get ();
	return nullptr;
}

void CollGeomSession::NewVessel (uint32_t id, CollH h)
{
	if (Rec (id)) return;
	auto r = std::make_unique<CollVesselSrc> ();
	r->id = id; r->h = h;
	r->vis = (started && renderMode == 0) ? VIS_HEADLESS : VIS_NOVIS;
	byHandle[h] = id;
	CollVesselSrc &rr = *r;
	pending.push_back (std::move (r));
	FirstSight (rr);
}

void CollGeomSession::DeleteVessel (uint32_t id)
{
	for (size_t i = 0; i < pending.size (); i++)
		if (pending[i]->id == id) { byHandle.erase (pending[i]->h); pending.erase (pending.begin () + i); return; }
	if (id < vessel.size () && vessel[id]) { byHandle.erase (vessel[id]->h); vessel[id].reset (); }
}

void CollGeomSession::Merge ()
{
	for (auto &p : pending) {
		if (vessel.size () <= p->id) vessel.resize (p->id + 1);
		vessel[p->id] = std::move (p);
	}
	pending.clear ();
}

void CollGeomSession::ReadKeys (CollVesselSrc &r)
{
	std::string cls = r.cls.empty () ? r.name : r.cls;
	std::string ck = r.cls.empty () ? "#" + r.name : r.cls; // a vessel without a class name keys on its own name
	auto it = classKeys.find (ck);
	if (it == classKeys.end ()) {
		CollClassKeys k;
		auto readFile = [&](const std::string &path) {
			std::string text;
			if (path.empty () || !sdk.ReadText (sdk.Resolve (path), text)) return false;
			std::string base;
			bool b; double d;
			if (CollItemString (text, "BaseClass", base)) {
				std::string bt;
				if (sdk.ReadText (sdk.Resolve (CollCfgPath (dirs, base, ".cfg")), bt)) {
					if (CollItemBool (bt, "EnableCollider", b)) k.enableCollider = b;
					if (CollItemReal (bt, "DockZoneRadius", d)) k.dockZoneRadius = d;
					if (CollItemReal (bt, "DestroyEnergy", d)) k.destroyEnergy = d;
				}
			}
			if (CollItemBool (text, "EnableCollider", b)) k.enableCollider = b;
			if (CollItemReal (text, "DockZoneRadius", d)) k.dockZoneRadius = d;
			if (CollItemReal (text, "DestroyEnergy", d)) k.destroyEnergy = d;
			return true;
		};
		if (!readFile (CollCfgPath (dirs, "Vessels\\" + cls, ".cfg"))) readFile (CollCfgPath (dirs, cls, ".cfg"));
		for (auto &c : cfg.meshProbeOnce) if (CollLower (c) == CollLower (cls)) k.meshProbe = 1;
		it = classKeys.emplace (ck, k).first;
	}
	r.keys = it->second;
	r.probeOnce = r.keys.meshProbe == 1;
}

void CollGeomSession::FirstSight (CollVesselSrc &r)
{
	r.name = sdk.Name (r.h);
	r.cls = sdk.ClassName (r.h);
	ReadKeys (r);
	r.collider = Active (r);
	if (!r.collider && !r.wantSlots) return;
	PollSlots (r);
	if (!r.collider) return;
	PollAnims (r, true);
	if (sdk.Visual (r.h)) { // the client built the visual first: its constructor stepped from defstate
		const ANIMATION *a = nullptr;
		uint32_t na = sdk.Anims (r.h, &a);
		Rewind (r.anim, a, na, r.present.data (), (uint32_t)r.present.size ());
		r.anim.Step (a, na, r.present.data (), (uint32_t)r.present.size ());
		r.vis = VIS_VIS; r.busy = 2; r.everStepped = true; r.firstVisCheck = true;
	}
}

void CollGeomSession::SimulationStart (int mode, bool renderWindow)
{
	renderMode = renderWindow ? mode : 0;
	started = true;
	int abs = 0;
	if (sdk.CfgInt ("Modules/VulkanClient/VulkanClient.cfg", 0, "AbsoluteAnimations", abs) && abs) {
		absAnim = true;
		sdk.Log (1, "Collision anim: AbsoluteAnimations is on; the replica stays incremental");
	}
	for (auto &p : pending) if (!renderWindow && p->vis == VIS_NOVIS && !p->everStepped) p->vis = VIS_HEADLESS;
	for (uint32_t i = 0, n = sdk.VesselCount (); i < n; i++) {
		CollH h = sdk.Vessel (i);
		if (byHandle.count (h)) continue;
		uint32_t id = idOf ? idOf (h) : ownId++;
		NewVessel (id, h);
		if (!renderWindow) Rec (id)->vis = VIS_HEADLESS;
	}
	Merge ();
	if (cfg.model != 0 || true) bases->Build (sdk, dirs, cfg.model != 0);
}

void CollGeomSession::PollSlots (CollVesselSrc &r)
{
	uint32_t n = sdk.MeshCount (r.h);
	bool countChanged = n != r.lastCount || !r.polled;
	if (r.slot.size () < n) { r.slot.resize (n); r.view.resize (n); r.present.resize (n, 0); }
	for (uint32_t i = n; i < r.slot.size (); i++) if (r.slot[i].kind != SLOT_DEAD) Kill (r, i);
	if (r.slot.size () > n) { r.slot.resize (n); r.view.resize (n); r.present.resize (n); }
	for (uint32_t i = 0; i < n; i++) {
		CollSlotRec &s = r.slot[i];
		CollH tpl = sdk.MeshTemplate (r.h, i);
		uint8_t kind; std::string name;
		if (tpl) {
			kind = SLOT_TPL;
			const char *nm = sdk.TplName (tpl);
			name = nm ? nm : "";
		} else {
			if (r.probeOnce && !countChanged && s.kind == SLOT_NAME) { UpdateInfo (r, i); continue; } // no probe: no dangerous getter, keep ofs and mode
			if (!sdk.ProbeSlot (r.h, i)) { if (s.kind != SLOT_DEAD) Kill (r, i); continue; }
			kind = SLOT_NAME;
			const char *nm = sdk.MeshName (r.h, i); name = nm ? nm : "";
		}
		Vector ofs = sdk.MeshOffset (r.h, i);
		uint16_t mode = sdk.MeshVisMode (r.h, i);
		bool repl = s.kind == SLOT_DEAD || s.kind != kind || s.tpl != tpl || s.name != name;
		s.ofs = ofs; s.mode = mode;
		if (repl) { s.kind = kind; s.tpl = tpl; s.name = name; Replace (r, i); }
		else UpdateInfo (r, i);
	}
	r.lastCount = n;
	r.polled = true;
}

void CollGeomSession::UpdateInfo (CollVesselSrc &r, uint32_t i)
{
	CollSlotRec &s = r.slot[i];
	s.info.ofs = s.ofs;
	s.info.serial = s.serial;
	s.info.present = s.present;
	s.info.collide = s.present && (s.mode & MESHVIS_EXTERNAL) && !ExcludesAll (s.info.side.get ()) && r.keys.enableCollider;
}

void CollGeomSession::Replace (CollVesselSrc &r, uint32_t i)
{
	CollSlotRec &s = r.slot[i];
	s.serial++;
	s.info = CollMeshInfo {};
	if (s.kind == SLOT_TPL) {
		std::string key;
		s.info.rest = cache.ByTemplate (sdk, s.tpl, key);
		s.info.key = key;
		s.present = true;
	} else {
		auto nm = cache.ByName (sdk, dirs, s.name);
		s.info.rest = nm.rest;
		s.info.key = nm.key;
		s.present = nm.present;
	}
	uint32_t ngrp = s.info.rest ? (uint32_t)s.info.rest->grp.size () : 0;
	if (!s.name.empty ()) s.info.side = cache.Sidecar (sdk, dirs, s.name, ngrp);
	if (s.info.side && !s.info.side->mesh.empty ()) s.info.coll = cache.ByName (sdk, dirs, s.info.side->mesh).rest;
	if (!s.info.rest && s.present) s.info.rest = std::make_shared<CollRestMesh> ();
	r.present[i] = s.present ? 1 : 0;
	UpdateInfo (r, i);
	r.anim.OnMeshInsert (i);
	CollSlotView &v = r.view[i];
	v.kind = s.kind; v.present = s.present; v.rest = s.present ? s.info.rest : nullptr; v.name = s.name; v.serial = s.serial;
	v.key = DentMath::MeshKey (s.name.c_str ());
	v.ngrp = v.rest ? (uint16_t)v.rest->grp.size () : 0;
	v.nvtx = v.rest ? v.rest->nvtx : 0;
	r.ev.push_back ({ i, SLOTEV_REPLACED, E2_POLL });
}

void CollGeomSession::Kill (CollVesselSrc &r, uint32_t i)
{
	CollSlotRec &s = r.slot[i];
	r.anim.OnMeshDelete (i);
	r.present[i] = 0;
	s.present = false; s.info.present = s.info.collide = false;
	s.kind = SLOT_DEAD; s.tpl = nullptr; s.name.clear ();
	r.view[i].kind = SLOT_DEAD; r.view[i].present = false; r.view[i].rest = nullptr;
	r.ev.push_back ({ i, SLOTEV_GONE, E2_POLL });
}

void CollGeomSession::PollAnims (CollVesselSrc &r, bool check)
{
	const ANIMATION *a = nullptr;
	uint32_t na = sdk.Anims (r.h, &a);
	r.layoutChanged = false;
	if (na < r.nanimSeen) { r.anim.OnClear (); r.comp.clear (); r.layoutChanged = true; }
	std::unordered_map<const ANIMATIONCOMP *, uint32_t> anOf;
	std::vector<const ANIMATIONCOMP *> cur;
	for (uint32_t an = 0; an < na; an++)
		for (uint32_t c = 0; c < a[an].ncomp; c++) if (a[an].comp[c]) { anOf[a[an].comp[c]] = an; cur.push_back (a[an].comp[c]); }
	// state change of an ancestor in the last two steps: client motion in VIS
	auto ancestorMoved = [&](const ANIMATIONCOMP *ac) {
		for (const ANIMATIONCOMP *p = ac->parent; p; p = p->parent) {
			auto f = anOf.find (p);
			if (f == anOf.end ()) continue;
			uint32_t an = f->second;
			double s = a[an].state;
			if ((an < r.sCur.size () && r.sCur[an] != s) || (an < r.sPrev.size () && an < r.sCur.size () && r.sPrev[an] != r.sCur[an])) return true;
		}
		return false;
	};
	std::unordered_map<const ANIMATIONCOMP *, size_t> recOf;
	std::vector<CollCompRec> keep;
	for (auto &c : r.comp) {
		auto f = anOf.find (c.ac);
		if (f == anOf.end () || c.trans != c.ac->trans || c.s0 != c.ac->state0 || c.s1 != c.ac->state1 || c.parent != c.ac->parent || c.an != f->second) {
			r.anim.OnDel (c.ac); r.layoutChanged = true; continue;
		}
		keep.push_back (c);
	}
	r.comp.swap (keep);
	bool doCheck = check || r.layoutChanged;
	for (size_t k = 0; k < r.comp.size (); k++) {
		CollCompRec &c = r.comp[k];
		recOf[c.ac] = k;
		if (!doCheck) continue;
		uint64_t fs, fm;
		Fingerprint (c.ac, fs, fm);
		if (fs != c.fpStatic) { r.anim.OnDel (c.ac); r.anim.OnAdd (c.ac); c.fpStatic = fs; c.fpMove = fm; continue; }
		if (fm != c.fpMove) {
			bool client = r.vis == VIS_VIS && (r.busy > 0 || ancestorMoved (c.ac));
			if (!client) { r.anim.OnDel (c.ac); r.anim.OnAdd (c.ac); }
			c.fpMove = fm;
		}
	}
	for (const ANIMATIONCOMP *ac : cur) {
		if (recOf.count (ac)) continue;
		CollCompRec c { ac, anOf[ac], ac->trans, ac->state0, ac->state1, ac->parent, 0, 0 };
		Fingerprint (ac, c.fpStatic, c.fpMove);
		r.anim.OnAdd (ac);
		recOf[ac] = r.comp.size ();
		r.comp.push_back (c);
		r.layoutChanged = true;
	}
	r.nanimSeen = na;
}

void CollGeomSession::Rewind (CollAnim &ca, const ANIMATION *a, uint32_t na, const uint8_t *present, uint32_t nmesh)
{
	std::vector<ANIMATION> still (a, a + na), back (a, a + na);
	for (ANIMATION &x : still) { x.ncomp = 0; x.comp = nullptr; }
	ca.Step (still.data (), na, present, nmesh);
	for (ANIMATION &x : back) x.state = x.defstate;
	ca.Step (back.data (), na, present, nmesh);
	ca.OnMeshDelete (COLLANIM_LVL);
}

void CollGeomSession::StepVessel (CollVesselSrc &r, const CollAnim *&src, CollAnim &tmp)
{
	const ANIMATION *a = nullptr;
	uint32_t na = sdk.Anims (r.h, &a);
	const uint8_t *pr = r.present.data (); uint32_t nm = (uint32_t)r.present.size ();
	src = &r.anim;
	if (r.vis == VIS_NOVIS) {
		bool def = true;
		for (uint32_t i = 0; i < na; i++) if (a[i].state != a[i].defstate) def = false;
		if (!def) { tmp = r.anim; tmp.Step (a, na, pr, nm); src = &tmp; }
	} else {
		r.anim.Step (a, na, pr, nm);
		r.everStepped = true;
	}
	r.sPrev2 = r.sPrev; r.sPrev = r.sCur;
	r.sCur.resize (na);
	for (uint32_t i = 0; i < na; i++) r.sCur[i] = a[i].state;
}

void CollGeomSession::Predict (CollVesselSrc &r, const CollAnim &src)
{
	CollVesselGeom &g = r.geom;
	const CollShape *sh = r.shape.get ();
	uint32_t np = sh ? sh->nPart () : 0;
	g.next.assign (np, CollAffine ()); g.nextSc.assign (np, Vector ()); g.nextSr.assign (np, 0);
	g.motionNext.assign (np, 0); g.predErr.assign (np, -1);
	g.animating = false;
	const ANIMATION *a = nullptr;
	uint32_t na = sdk.Anims (r.h, &a);
	bool hist = r.histValid && r.sPrev.size () == na && r.sCur.size () == na && r.dtPrev > 0;
	if (hist) for (uint32_t i = 0; i < na; i++) if (r.sCur[i] != r.sPrev[i]) g.animating = true;
	CollAnim tmp;
	const CollAnim *pa = &src;
	if (g.animating) {
		std::vector<ANIMATION> pred (a, a + na);
		for (uint32_t i = 0; i < na; i++) pred[i].state = r.sCur[i] + (r.sCur[i] - r.sPrev[i]) * r.dtCur / r.dtPrev;
		tmp = src;
		tmp.Step (pred.data (), na, r.present.data (), (uint32_t)r.present.size ());
		pa = &tmp;
	}
	g.nextC = Vector (); g.nextR = 0;
	for (uint32_t k = 0; k < np; k++) {
		const CollPart &p = sh->Part (k);
		if (g.animating) {
			CollAffine F;
			pa->GroupTransform (p.mesh, p.rep, F);
			g.next[k] = CollCompose (CollTranslate (r.slot[p.mesh].ofs), F);
			g.motionNext[k] = CollPoseMotion (p.Geom (), p.pose[1], g.next[k]);
		} else g.next[k] = p.pose[1];
		const CollGeom &G = p.Geom ();
		g.nextSc[k] = CollApply (g.next[k], G.bsCentre);
		double s = 0;
		for (int c = 0; c < 3; c++) {
			Vector col (g.next[k].A.data[c], g.next[k].A.data[3+c], g.next[k].A.data[6+c]);
			s = std::max (s, col.length ());
		}
		g.nextSr[k] = G.bsRadius * s;
		g.nextR = std::max (g.nextR, g.nextSc[k].length () + g.nextSr[k]);
		if (k < r.lastNext.size () && r.lastNextVer[k] == p.version) g.predErr[k] = CollPoseMotion (G, r.lastNext[k], p.pose[1]);
	}
	r.lastNext = g.next;
	r.lastNextVer.resize (np);
	for (uint32_t k = 0; k < np; k++) r.lastNextVer[k] = sh->Part (k).version;
}

void CollGeomSession::ClientCheck (CollVesselSrc &r)
{
	if (!sdk.ClientCore () || !sdk.Visual (r.h)) return;
	for (uint32_t m = 0; m < r.slot.size (); m++) {
		if (!r.present[m]) continue;
		const CollRestMesh *rm = r.slot[m].info.rest.get ();
		if (!rm) continue;
		bool any = false, allId = true;
		for (uint32_t g = 0; g < rm->grp.size (); g++) {
			CollAffine F;
			if (!r.anim.GroupTransform (m, g, F)) continue;
			bool ident = true;
			Matrix I = IMatrix ();
			for (int e = 0; e < 9; e++) if (F.A.data[e] != I.data[e]) ident = false;
			if (F.t.x != 0 || F.t.y != 0 || F.t.z != 0) ident = false;
			if (ident) continue;
			any = true;
			float mm[16], mg[16];
			if (sdk.ClientMatrix (0, r.h, m, ~0u, mm) != 0 || sdk.ClientMatrix (1, r.h, m, g, mg) != 0) { allId = false; break; }
			for (int e = 0; e < 16; e++) if (mg[e] != ((e % 5) == 0 ? 1.0f : 0.0f) || mm[e] != ((e % 5) == 0 ? 1.0f : 0.0f)) allId = false;
			if (!allId) break;
		}
		if (any && allId) r.rebuildQ.push_back ({ m, E2_GCCORE });
	}
}

void CollGeomSession::BeginFrame (double simt, double simdt)
{
	(void)simt;
	inPreStep = true;
	frame++;
	Merge ();
	uint32_t n = sdk.VesselCount ();
	std::vector<CollH> hs (n);
	for (uint32_t i = 0; i < n; i++) hs[i] = sdk.Vessel (i);
	uint32_t rrPick = n ? rr++ % n : 0;
	for (uint32_t i = 0; i < n; i++) {
		auto f = byHandle.find (hs[i]);
		if (f == byHandle.end ()) {
			uint32_t id = idOf ? idOf (hs[i]) : ownId++;
			sdk.Log (1, ("Collision: vessel '" + sdk.Name (hs[i]) + "' first seen in a pre-step").c_str ());
			NewVessel (id, hs[i]);
			Merge ();
			f = byHandle.find (hs[i]);
		}
		CollVesselSrc *r = Rec (f->second);
		if (!r) continue;
		r->geom.id = r->id; r->geom.h = r->h;
		if (!r->collider) {
			if (r->wantSlots) PollSlots (*r);
			r->geom.shape = nullptr;
			continue;
		}
		bool firstVis = false;
		if (r->vis == VIS_NOVIS && sdk.Visual (r->h)) { r->vis = VIS_VIS; r->busy = 2; firstVis = true; }
		for (auto &q : r->rebuildQ) {
			if (q.first >= r->slot.size () || r->slot[q.first].kind == SLOT_DEAD) continue;
			bool dup = false;
			for (auto &e : r->ev) if (e.mesh == q.first && e.what == SLOTEV_REBUILT) dup = true;
			if (dup) continue;
			r->anim.OnMeshInsert (q.first);
			r->slot[q.first].serial++;
			r->view[q.first].serial = r->slot[q.first].serial;
			UpdateInfo (*r, q.first);
			r->ev.push_back ({ q.first, SLOTEV_REBUILT, q.second });
		}
		r->rebuildQ.clear ();
		PollSlots (*r);
		PollAnims (*r, i == rrPick);
		CollAnim tmp;
		const CollAnim *src = nullptr;
		StepVessel (*r, src, tmp);
		if (r->busy > 0) r->busy--;
		if (cfg.clientCheck && (firstVis || r->firstVisCheck || frame % 30 == rrPick % 30)) { r->firstVisCheck = false; ClientCheck (*r); }
		r->dtPrev = r->dtCur; r->dtCur = simdt;
		const ANIMATION *a = nullptr;
		uint32_t na = sdk.Anims (r->h, &a);
		std::vector<CollMeshInfo> info (r->slot.size ());
		for (size_t k = 0; k < r->slot.size (); k++) info[k] = r->slot[k].info;
		if (!r->shape) r->shape = std::make_unique<CollShape> ();
		r->geom.flags = r->shape->Update (info.data (), (uint32_t)info.size (), *src, a, na, tpl);
		r->geom.shape = (r->geom.flags & COLLSH_NONE) ? nullptr : r->shape.get ();
		Predict (*r, *src);
		r->histValid = r->dtCur > 0;
	}
	if (cfg.model != 0) PollAssemblies ();
	if (cfg.model != 0) bases->Poll (sdk, frame);
	inPreStep = false;
}

void CollGeomSession::Deliver (CollShapeSink &sink)
{
	for (auto &p : vessel) {
		if (!p) continue;
		sink.ShapesUpdated (p->id, (p->collider && p->geom.shape) ? p->shape.get () : nullptr, p->ev);
		p->ev.clear ();
	}
}

void CollGeomSession::PostStepPoll ()
{
	for (auto &p : vessel) if (p && p->collider) PollAnims (*p, false);
}

void CollGeomSession::TimeJump ()
{
	for (auto &p : vessel) {
		if (!p) continue;
		if (p->shape) p->shape->MarkJump ();
		p->histValid = false; p->lastNext.clear ();
	}
}

void CollGeomSession::EndSession ()
{
	vessel.clear (); pending.clear (); byHandle.clear ();
	asm_.clear (); port.clear (); att.clear ();
	bases.reset (new CollBaseA);
}

void CollGeomSession::PollAssemblies ()
{
	asm_.clear (); port.clear (); att.clear ();
	std::vector<CollVesselSrc *> v;
	for (auto &p : vessel) if (p) v.push_back (p.get ());
	size_t n = v.size ();
	std::unordered_map<CollH, size_t> idx;
	for (size_t i = 0; i < n; i++) idx[v[i]->h] = i;
	std::vector<size_t> par (n);
	for (size_t i = 0; i < n; i++) par[i] = i;
	std::function<size_t (size_t)> find = [&](size_t x) { while (par[x] != x) x = par[x] = par[par[x]]; return x; };
	auto unite = [&](size_t a, size_t b) { a = find (a); b = find (b); if (a != b) par[std::max (a, b)] = std::min (a, b); };
	std::unordered_map<CollH, size_t> svFirst;
	std::vector<CollH> sv (n, nullptr);
	std::vector<int> parentOf (n, -1);
	for (size_t i = 0; i < n; i++) {
		CollVesselRead rd {};
		sdk.ReadVessel (v[i]->h, rd, CVR_NOWEIGHT);
		sv[i] = rd.sv;
		if (rd.sv) { auto f = svFirst.emplace (rd.sv, i); if (!f.second) unite (i, f.first->second); }
		for (int dir = 0; dir < 2; dir++) {
			bool tp = dir == 1;
			for (uint32_t k = 0, na = sdk.AttachCount (v[i]->h, tp); k < na; k++) {
				CollAttInfo ai {};
				if (!sdk.Attach (v[i]->h, tp, k, ai)) continue;
				CollAttRec ar { v[i]->id, k, tp, ai.pos, ai.dir, ai.rot, {}, ai.mate, -1 };
				memcpy (ar.id, ai.id, 9);
				if (ai.mate) {
					auto f = idx.find (ai.mate);
					if (f != idx.end ()) {
						unite (i, f->second);
						if (tp) parentOf[i] = (int)f->second;
						int cnt = 0;
						for (uint32_t j = 0, nm = sdk.AttachCount (ai.mate, !tp); j < nm; j++) {
							CollAttInfo bi {};
							if (sdk.Attach (ai.mate, !tp, j, bi) && bi.mate == v[i]->h) { ar.mateIndex = (int32_t)j; cnt++; }
						}
						if (cnt > 1) ar.mateIndex = -2;
					}
				}
				att.push_back (ar);
			}
		}
		for (uint32_t k = 0, nd = sdk.DockCount (v[i]->h); k < nd; k++) {
			CollPortInfo pi {};
			if (!sdk.Dock (v[i]->h, k, pi)) continue;
			CollPortRec pr { v[i]->id, k, pi.pos, pi.dir, pi.rot, pi.mate, -1 };
			if (pi.mate) {
				int cnt = 0;
				for (uint32_t j = 0, nm = sdk.DockCount (pi.mate); j < nm; j++) {
					CollPortInfo qi {};
					if (sdk.Dock (pi.mate, j, qi) && qi.mate == v[i]->h) { pr.matePort = (int32_t)j; cnt++; }
				}
				if (cnt > 1) pr.matePort = -2;
			}
			port.push_back (pr);
		}
	}
	std::unordered_map<size_t, size_t> setOf;
	for (size_t i = 0; i < n; i++) {
		size_t root = find (i);
		auto f = setOf.find (root);
		if (f == setOf.end ()) { f = setOf.emplace (root, asm_.size ()).first; asm_.push_back (CollAssembly { {}, 0, nullptr, 0, 0 }); }
		asm_[f->second].member.push_back (v[i]->id);
	}
	for (auto &[root, ai] : setOf) {
		CollAssembly &A = asm_[ai];
		std::sort (A.member.begin (), A.member.end ());
		uint64_t h = CollFnv (A.member.data (), A.member.size () * sizeof (uint32_t));
		A.memberHash = h;
		bool stack = false, mixed = false;
		for (size_t i = 0; i < n; i++) if (find (i) == root && sv[i]) { stack = true; A.sv = sv[i]; if (parentOf[i] >= 0) mixed = true; }
		if (stack) { A.root = v[svFirst[A.sv]]->id; A.flags |= ASM_STACK; }
		else {
			size_t x = root; size_t steps = 0;
			for (size_t i = 0; i < n; i++) if (find (i) == root) { x = i; break; }
			while (parentOf[x] >= 0 && steps++ <= n) x = (size_t)parentOf[x];
			if (steps > n) mixed = true;
			A.root = v[x]->id;
		}
		if (mixed) A.flags |= ASM_MIXED;
		uint64_t rk = A.member.front ();
		auto lh = lastHash.find (rk);
		if (lh == lastHash.end () || lh->second != h) A.flags |= ASM_CHANGED;
		lastHash[rk] = h;
	}
}

const CollVesselGeom *CollGeomSession::Geom (uint32_t id) const
{
	CollVesselSrc *r = Rec (id);
	return r ? &r->geom : nullptr;
}

const std::vector<CollSlotView> &CollGeomSession::Slots (uint32_t id) const
{
	CollVesselSrc *r = Rec (id);
	return r ? r->view : noSlots;
}

bool CollGeomSession::SlotNow (uint32_t id, uint32_t m, uint32_t &serial)
{
	CollVesselSrc *r = Rec (id);
	if (!r || m >= r->slot.size () || r->slot[m].kind == SLOT_DEAD) return false;
	if (sdk.MeshCount (r->h) != r->lastCount) return false;
	const CollSlotRec &s = r->slot[m];
	CollH tpl = sdk.MeshTemplate (r->h, m);
	if (tpl != s.tpl) return false;
	if (s.kind == SLOT_NAME && !r->probeOnce) {
		if (!sdk.ProbeSlot (r->h, m)) return false;
		const char *nm = sdk.MeshName (r->h, m);
		if (s.name != (nm ? nm : "")) return false;
	}
	serial = s.serial;
	return true;
}

void CollGeomSession::ClientMeshRebuilt (uint32_t id, uint32_t mesh, uint8_t src)
{
	CollVesselSrc *r = Rec (id);
	if (!r || mesh >= r->slot.size ()) return;
	if (src == E3_SENTINEL && sdk.ClientCore () && sdk.Visual (r->h) && r->slot[mesh].info.rest) { // confirmation (6.7)
		bool any = false, allMatch = true;
		const CollRestMesh &rm = *r->slot[mesh].info.rest;
		for (uint32_t g = 0; g < rm.grp.size () && allMatch; g++) {
			CollAffine F;
			if (!r->anim.GroupTransform (mesh, g, F)) continue;
			any = true;
			float mg[16];
			if (sdk.ClientMatrix (1, r->h, mesh, g, mg) != 0) { allMatch = false; break; }
			const double e[12] = { F.A.m11, F.A.m21, F.A.m31, F.A.m12, F.A.m22, F.A.m32, F.A.m13, F.A.m23, F.A.m33, F.t.x, F.t.y, F.t.z };
			const int at[12] = { 0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14 };
			for (int k = 0; k < 12; k++) if (std::fabs (mg[at[k]] - e[k]) > 1e-4 * std::max (1.0, std::fabs (e[k]))) allMatch = false;
		}
		if (any && allMatch) {
			char buf[512];
			snprintf (buf, sizeof buf, "Collision anim: '%s' mesh=%u rebuild call not confirmed", r->name.c_str (), mesh);
			sdk.Log (1, buf);
			return;
		}
	}
	for (auto &q : r->rebuildQ) if (q.first == mesh) return;
	r->rebuildQ.push_back ({ mesh, src });
}

void CollGeomSession::WantSlots (uint32_t id, bool on)
{
	if (CollVesselSrc *r = Rec (id)) r->wantSlots = on;
}

bool CollGeomSession::GroupAnimated (uint32_t id, uint32_t mesh, uint32_t grp) const
{
	CollVesselSrc *r = Rec (id);
	if (!r) return false;
	for (auto &c : r->comp) {
		const MGROUP_TRANSFORM *t = c.ac->trans;
		if (!t || t->mesh != mesh) continue;
		if (!t->grp) return true;
		for (UINT k = 0; k < t->ngrp; k++) if (t->grp[k] == grp) return true;
	}
	return false;
}

const CollClassKeys &CollGeomSession::Keys (uint32_t id) const
{
	CollVesselSrc *r = Rec (id);
	return r ? r->keys : noKeys;
}

const CollBaseRec *CollGeomSession::BaseRec (int planet, int base) const { return bases->Rec (planet, base); }
const CollBaseObjView *CollGeomSession::BaseObject (int planet, int base, int obj) const { return bases->Object (planet, base, obj); }
void CollGeomSession::Bases (std::vector<const CollBaseObjView *> &all) const { bases->All (all); }
