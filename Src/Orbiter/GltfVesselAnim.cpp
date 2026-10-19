// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// not upstream: a vessel's animations on meshes from a drop-in file (claude/design-gltf-roundtrip.md 5.4)

#include "GltfVesselAnim.h"
#include "GltfRoundTrip.h"
#include "Mesh.h"
#include "Vessel.h"
#include "Log.h"
#include <algorithm>
#include <cmath>

namespace {

void Free (MGROUP_TRANSFORM *t) // MGROUP_TRANSFORM has no virtual destructor
{
	switch (t->Type ()) {
	case MGROUP_TRANSFORM::ROTATE: delete (MGROUP_ROTATE*)t; break;
	case MGROUP_TRANSFORM::TRANSLATE: delete (MGROUP_TRANSLATE*)t; break;
	case MGROUP_TRANSFORM::SCALE: delete (MGROUP_SCALE*)t; break;
	default: delete t; break;
	}
}

void Unlink (ANIMATIONCOMP *parent, ANIMATIONCOMP *child) // as Vessel::DelAnimationComponent
{
	ANIMATIONCOMP **ch = 0;
	UINT k = 0;
	if (parent->nchildren > 1) {
		ch = new ANIMATIONCOMP*[parent->nchildren - 1]; TRACENEW
		for (UINT j = 0; j < parent->nchildren; j++) if (parent->children[j] != child && k < parent->nchildren - 1) ch[k++] = parent->children[j];
	}
	delete []parent->children;
	parent->children = ch;
	parent->nchildren = k;
}

void Link (ANIMATIONCOMP *parent, ANIMATIONCOMP *child) // as Vessel::AddAnimationComponent
{
	ANIMATIONCOMP **ch = new ANIMATIONCOMP*[parent->nchildren + 1]; TRACENEW
	if (parent->nchildren) memcpy (ch, parent->children, parent->nchildren * sizeof (ANIMATIONCOMP*));
	delete []parent->children;
	parent->children = ch;
	parent->children[parent->nchildren++] = child;
	child->parent = parent;
}

} // namespace

GltfVesselAnim::GltfVesselAnim (Vessel *vessel) : v (vessel)
{
	for (UINT a = 0; a < v->nanim; a++) programCount[a] = (int)v->anim[a].ncomp; // no core components yet
}

GltfVesselAnim::~GltfVesselAnim ()
{
	for (MGROUP_TRANSFORM *t : owned) Free (t);
}

void GltfVesselAnim::Warn (const std::string &w)
{
	if (warned.insert (w).second) LOGOUT_WARN ("%s: %s", v->Name(), w.c_str());
}

UINT *GltfVesselAnim::List (const std::vector<UINT> &g)
{
	lists.push_back (std::make_unique<std::vector<UINT>> (g));
	if (lists.back()->empty()) lists.back()->push_back (0); // a list, not NULL (the whole mesh), with ngrp 0
	return lists.back()->data();
}

MGROUP_TRANSFORM *GltfVesselAnim::Copy (const MGROUP_TRANSFORM *t, UINT *grp, UINT ngrp)
{
	MGROUP_TRANSFORM *c;
	switch (t->Type ()) {
	case MGROUP_TRANSFORM::ROTATE: { const MGROUP_ROTATE *r = (const MGROUP_ROTATE*)t; c = new MGROUP_ROTATE (t->mesh, grp, ngrp, r->ref, r->axis, r->angle); } break;
	case MGROUP_TRANSFORM::TRANSLATE: c = new MGROUP_TRANSLATE (t->mesh, grp, ngrp, ((const MGROUP_TRANSLATE*)t)->shift); break;
	case MGROUP_TRANSFORM::SCALE: { const MGROUP_SCALE *s = (const MGROUP_SCALE*)t; c = new MGROUP_SCALE (t->mesh, grp, ngrp, s->ref, s->scale); } break;
	default: c = new MGROUP_TRANSFORM (t->mesh, grp, ngrp); break;
	}
	owned.push_back (c);
	return c;
}

const std::vector<int> *GltfVesselAnim::Replaced (UINT mesh) const
{
	auto it = slots.find (mesh);
	return it == slots.end() ? nullptr : &it->second.replaced;
}

// a program component on a replaced unit keeps its place, its parameters and its children, and moves only the groups the file doesn't have
void GltfVesselAnim::Shade (ANIMATIONCOMP *ac, UINT slot, int anim, int index)
{
	Slot &s = slots[slot];
	MGROUP_TRANSFORM *t = ac->trans;
	std::vector<UINT> keep;
	if (!t->grp) keep.assign (s.skipped.begin(), s.skipped.end());
	else for (UINT i = 0; i < t->ngrp; i++) if (s.skipped.count ((int)t->grp[i])) keep.push_back (t->grp[i]);
	Shadow sh;
	sh.slot = slot; sh.anim = anim; sh.index = index; sh.original = t;
	ac->trans = Copy (t, List (keep), (UINT)keep.size());
	shadows[ac] = sh;
}

void GltfVesselAnim::AddPending ()
{
	adding = true;
	for (bool progress = true; progress;) {
		progress = false;
		for (auto &kv : slots) {
			Slot &s = kv.second;
			const RtMeshPlan &P = s.src.file->plans[s.src.entry];
			for (size_t i = 0; i < s.made.size(); i++) {
				if (s.made[i]) continue;
				const RtCompSpec &c = P.comps[i];
				if (c.anim < 0 || (UINT)c.anim >= v->nanim) continue;
				ANIMATIONCOMP *parent = nullptr;
				if (c.parent >= 0 && !(parent = s.made[c.parent])) continue;
				ANIMATIONCOMP *ac = v->AddAnimationComponent ((UINT)c.anim, c.state0, c.state1, s.trans[i], parent);
				if (!ac) continue;
				s.made[i] = ac;
				ours[ac] = { kv.first, (int)i };
				progress = true;
			}
		}
	}
	adding = false;
	// program components whose parent became a shadow follow the file's motion
	for (UINT a = 0; a < v->nanim; a++)
		for (UINT j = 0; j < v->anim[a].ncomp; j++) {
			ANIMATIONCOMP *ac = v->anim[a].comp[j];
			if (ac && ac->parent && !ours.count (ac) && !shadows.count (ac) && shadows.count (ac->parent)) Reparent (ac);
		}
}

void GltfVesselAnim::Reparent (ANIMATIONCOMP *ac)
{
	const Shadow &sh = shadows[ac->parent];
	auto si = slots.find (sh.slot);
	ANIMATIONCOMP *np = nullptr;
	if (si != slots.end()) {
		auto it = si->second.pivot.find ({ sh.anim, sh.index });
		if (it != si->second.pivot.end()) np = si->second.made[it->second];
	}
	if (!np) {
		Warn ("a part's parent (animation " + std::to_string (sh.anim) + " part " + std::to_string (sh.index) + ") is not in the glTF file, it keeps the program's motion");
		return;
	}
	moved[ac] = ac->parent;
	Unlink (ac->parent, ac);
	Link (np, ac);
}

void GltfVesselAnim::DropSlot (UINT idx)
{
	auto it = slots.find (idx);
	if (it == slots.end()) return;
	Slot &s = it->second;
	for (auto mv = moved.begin(); mv != moved.end();) { // program children back on their own parents first
		auto o = ours.find (mv->first->parent);
		if (o != ours.end() && o->second.first == idx && shadows.count (mv->second)) {
			Unlink (mv->first->parent, mv->first);
			Link (mv->second, mv->first);
			mv = moved.erase (mv);
		} else ++mv;
	}
	const RtMeshPlan &P = s.src.file->plans[s.src.entry];
	for (size_t i = s.made.size(); i-- > 0;)
		if (s.made[i]) v->DelAnimationComponent ((UINT)P.comps[i].anim, s.made[i]);
	for (auto sh = shadows.begin(); sh != shadows.end();) {
		if (sh->second.slot == idx) { sh->first->trans = sh->second.original; sh = shadows.erase (sh); }
		else ++sh;
	}
	slots.erase (it);
}

void GltfVesselAnim::Report (UINT idx, Slot &s)
{
	if (s.logged) return;
	s.logged = true;
	const RtFile &f = *s.src.file;
	const RtMeshPlan &P = f.plans[s.src.entry];
	std::string r, ft;
	for (int a : P.replaced) r += (r.empty() ? "" : ", ") + std::to_string (a);
	for (int a : P.fitted) ft += (ft.empty() ? "" : ", ") + std::to_string (a);
	LOGOUT ("glTF animations for %s mesh %u (%s): %s from the file%s%s; the rest from the program", v->Name(), idx, f.rec.meshes[s.src.entry].name.c_str(),
		r.c_str(), ft.empty() ? "" : ", fitted: ", ft.c_str());
}

// ----------------------------------------------------------------------------------------------
// hooks

void GltfVesselAnim::InsertedMesh (Vessel *v, UINT idx)
{
	if (idx >= v->nmesh || !v->meshlist[idx]) return;
	const Vessel::MeshList *ml = v->meshlist[idx];
	const char *name = ml->meshname[0] ? ml->meshname : (ml->hMesh ? ((Mesh*)ml->hMesh)->GetName() : nullptr);
	GltfDropSlot s;
	bool have = name && GltfDropInSlot (name, s);
	const RtMeshPlan *P = have ? &s.file->plans[s.entry] : nullptr;
	bool anim = P && !P->replaced.empty();
	GltfVesselAnim *g = v->gltfanim;
	if (g && g->slots.count (idx)) {
		const Slot &old = g->slots[idx];
		if (anim && old.src.file == s.file && old.src.entry == s.entry) return; // the same mesh again: its components stay
		g->DropSlot (idx);
	}
	if (!anim) return;
	if (!g) { g = v->gltfanim = new GltfVesselAnim (v); TRACENEW }
	Slot &sl = g->slots[idx];
	sl.src = s;
	sl.replaced = P->replaced;
	const RtRecMesh &M = s.file->rec.meshes[s.entry];
	sl.skipped.insert (M.skipped.begin(), M.skipped.end());
	for (size_t i = 0; i < P->comps.size(); i++) {
		const RtCompSpec &c = P->comps[i];
		std::vector<UINT> grp;
		for (int x : c.groups) grp.push_back (x >= 0 ? (UINT)x : (UINT)(s.addedBase + (-1 - x)));
		UINT *gp = g->List (grp);
		VECTOR3 ref = _V (c.ref.x, c.ref.y, c.ref.z), axis = _V (c.axis.x, c.axis.y, c.axis.z);
		VECTOR3 shift = _V (c.shift.x, c.shift.y, c.shift.z), scale = _V (c.scale.x, c.scale.y, c.scale.z);
		MGROUP_TRANSFORM *t;
		if (c.type == RT_ROTATE) t = new MGROUP_ROTATE (idx, gp, (UINT)grp.size(), ref, axis, (float)c.angle);
		else if (c.type == RT_SCALE) t = new MGROUP_SCALE (idx, gp, (UINT)grp.size(), ref, scale);
		else t = new MGROUP_TRANSLATE (idx, gp, (UINT)grp.size(), shift);
		g->owned.push_back (t);
		sl.trans.push_back (t);
		if (c.pivotAnim >= 0) sl.pivot[{ c.pivotAnim, c.pivotComp }] = (int)i;
	}
	sl.made.assign (P->comps.size(), nullptr);
	for (int a : sl.replaced) { // program components already on this mesh
		if ((UINT)a >= v->nanim) continue;
		int k = 0;
		for (UINT j = 0; j < v->anim[a].ncomp; j++) {
			ANIMATIONCOMP *ac = v->anim[a].comp[j];
			if (!ac || g->ours.count (ac)) continue;
			int index = k++;
			if (ac->trans && ac->trans->mesh == idx && !g->shadows.count (ac)) g->Shade (ac, idx, a, index);
		}
	}
	g->AddPending ();
}

void GltfVesselAnim::CreatedAnimation (Vessel *v, UINT an)
{
	GltfVesselAnim *g = v->gltfanim;
	if (!g) return;
	g->programCount[an] = 0;
	g->AddPending ();
}

void GltfVesselAnim::AddedComponent (Vessel *v, UINT an, ANIMATIONCOMP *ac)
{
	GltfVesselAnim *g = v->gltfanim;
	if (!g || g->adding || !ac || !ac->trans) return;
	int index = g->programCount[an]++;
	UINT mesh = ac->trans->mesh;
	const std::vector<int> *R = mesh == LOCALVERTEXLIST ? nullptr : g->Replaced (mesh);
	if (R && std::binary_search (R->begin(), R->end(), (int)an)) { g->Shade (ac, mesh, (int)an, index); return; }
	if (ac->parent && g->shadows.count (ac->parent)) g->Reparent (ac);
}

void GltfVesselAnim::DeletingComponent (Vessel *v, ANIMATIONCOMP *ac)
{
	GltfVesselAnim *g = v->gltfanim;
	if (!g) return;
	auto o = g->ours.find (ac);
	if (o != g->ours.end()) {
		auto s = g->slots.find (o->second.first);
		if (s != g->slots.end() && o->second.second < (int)s->second.made.size()) s->second.made[o->second.second] = nullptr;
		g->ours.erase (o);
	}
	g->shadows.erase (ac);
	g->moved.erase (ac);
	for (auto mv = g->moved.begin(); mv != g->moved.end();) mv = (mv->second == ac) ? g->moved.erase (mv) : std::next (mv);
}

void GltfVesselAnim::DeletingMesh (Vessel *v, UINT idx, bool retainAnim)
{
	GltfVesselAnim *g = v->gltfanim;
	if (!g || retainAnim) return; // kept: the same mesh inserted again keeps them, another one drops them (InsertedMesh)
	if (idx == (UINT)-1) g->slots.clear ();
	else g->slots.erase (idx); // Orbiter deletes the slot's components; DeletingComponent forgets them
}

void GltfVesselAnim::ClearingAnimations (Vessel *v)
{
	GltfVesselAnim *g = v->gltfanim;
	if (!g) return;
	for (auto &kv : g->slots) std::fill (kv.second.made.begin(), kv.second.made.end(), nullptr);
	g->ours.clear ();
	g->shadows.clear ();
	g->moved.clear ();
	g->programCount.clear ();
}

void GltfVesselAnim::Check (Vessel *v)
{
	GltfVesselAnim *g = v->gltfanim;
	if (!g) return;
	g->AddPending ();
	for (auto &kv : g->slots) {
		Slot &s = kv.second;
		const RtFile &f = *s.src.file;
		const RtMeshPlan &P = f.plans[s.src.entry];
		g->Report (kv.first, s);
		for (int a : s.replaced) {
			if ((UINT)a >= v->nanim) { g->Warn ("animation " + std::to_string (a) + " from the glTF file: the vessel has no such animation, not used"); continue; }
			if (std::fabs (v->anim[a].defstate - f.rec.anims[a].defstate) > 1e-9)
				g->Warn ("animation " + std::to_string (a) + ": default state " + std::to_string (v->anim[a].defstate) + " in the program, " + std::to_string (f.rec.anims[a].defstate) + " in the glTF file");
			int n = 0;
			for (auto &sh : g->shadows) if (sh.second.slot == kv.first && sh.second.anim == a) n++;
			auto rc = P.recCount.find (a);
			int want = rc == P.recCount.end() ? 0 : rc->second;
			if (n != want) g->Warn ("animation " + std::to_string (a) + " on mesh " + std::to_string (kv.first) + ": the program has " + std::to_string (n) + " parts, the glTF file recorded " + std::to_string (want));
		}
	}
}
