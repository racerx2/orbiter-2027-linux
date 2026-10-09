// not upstream: collision addon, E3 session part: events to dents, the only dent re-apply owner, destroyed state, thrust cut, notices, repair, block and recorder (Design CA E3)
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <set>
#include "CollDamageA.h"
#include "CollisionAPI.h"

namespace {

Vector Unit (const Vector &v) { double l = v.length (); return l > 0 ? v / l : v; }

std::vector<uint32_t> Grp32 (const std::vector<uint16_t> &g) { return std::vector<uint32_t> (g.begin (), g.end ()); }

// dmg3: a tangent mapped into a frame, made perpendicular to n again
Vector MapT (const CollAffine &X, const Vector &tw, const Vector &n)
{
	Vector t = CollApplyDir (X, tw);
	t = t - n * dotp (t, n);
	double l = t.length ();
	return l > 0 ? t / l : t;
}

void ApplyRec (CollShape *sh, uint32_t mesh, const DentRecord &r)
{
	std::vector<uint32_t> g = Grp32 (r.grp);
	if (r.p.mode == DENTM_CUT) { sh->ApplyMap (mesh, g.data (), g.size (), DentMath::MapLow, &r); return; } // dmg3 tear
	sh->ApplyDent (mesh, g.data (), g.size (), DentMath::FieldLow, &r); // dmg3: collider without noise and lobes (FieldLow = Field for legacy records)
}

const DentMaterial &MaterialOf (uint16_t id, int cls)
{
	const DentMaterial *m = id ? DentMath::FindMaterial (CollMaterialName (id)) : nullptr;
	return m ? *m : DentMath::DefaultMaterial (cls);
}

bool SameSig (const DentRecord &r, const CollDmgSlot &s) { return s.present && r.key == s.key && r.ngrp == s.ngrp && r.nvtx == s.nvtx; }

uint32_t NoticeFlags (uint32_t xf, bool cut)
{
	uint32_t f = 0;
	if (xf & XDMG_DESTROYED) f |= COLLA_DMG_DESTROYED;
	if (xf & XDMG_MODULEFX) f |= COLLA_DMG_MODULEFX;
	if (xf & XDMG_CATASTROPHIC) f |= COLLA_DMG_CATASTROPHIC;
	if (cut) f |= COLLA_DMG_CUT;
	return f;
}

}

CollDmgSession::CollDmgSession (CollSdk &s, CollDmgHost &h, const CollCfgValues &c) : vis (s, h, c), sdk (s), host (h), cfg (c)
{
	if (c.brk) brk = CollMakeBreak (s, *this, c);
	if (c.fx) fx = CollMakeFx (s, *this, c);
}

bool CollDmgSession::AddCut (uint32_t id, const DentRecord &r, bool playback)
{
	VesselDamageA &v = Get (id);
	size_t pend = laterCuts.count (id) ? laterCuts[id] : 0;
	if (v.d.rec.size () + pend >= DENT_MAX_VESSEL) {
		if (!v.loggedCap) { v.loggedCap = true; Log ("Collision dent: '%s' holds %u records, no tear", v.name.c_str (), DENT_MAX_VESSEL); }
		return false;
	}
	if (inDent) { laterCuts[id]++; later.push_back ([this, id, r, playback] () { ApplyCut (id, r, playback); }); return true; }
	ApplyCut (id, r, playback);
	return true;
}

void CollDmgSession::ApplyCut (uint32_t id, const DentRecord &r, bool playback)
{
	VesselDamageA &v = Get (id);
	if (v.d.rec.size () >= DENT_MAX_VESSEL) return;
	DentRecord x = r;
	std::sort (x.grp.begin (), x.grp.end ());
	x.grp.erase (std::unique (x.grp.begin (), x.grp.end ()), x.grp.end ());
	DentMath::Quantise (x.p);
	v.d.rec.push_back (x);
	v.match.push_back ((int)x.slot);
	CollShape *sh = host.Shape (id);
	if (sh) SyncCollider (v, sh, x.slot, false);
	SyncMirror (v, x.slot);
	n.dents++;
	CollH h = host.Vessel (id);
	if (!playback && rec.active && h && !sdk.Playback (h)) {
		uint32_t k = (uint32_t)(v.d.rec.size () - 1);
		std::vector<std::string> l;
		CollSide::Dent (frameT - rec.t0, Alias (v, h), k, x, l);
		for (auto &s2 : l) Side (s2);
		Side (CollSide::Ext (frameT - rec.t0, Alias (v, h), k, x.p, 0.0, 0.0, 0.0, 0, 0u)); // X hit = 0, before the T rows
	}
}

void CollDmgSession::FlushLater ()
{
	std::vector<std::function<void ()>> q;
	q.swap (later);
	laterCuts.clear ();
	for (auto &f : q) f ();
}

void CollDmgSession::AddTorn (uint32_t id, const DentTorn &t)
{
	if (inDent && !later.empty ()) { later.push_back ([this, id, t] () { AddTorn (id, t); }); return; } // dmg3 tear: T after its cut's X
	VesselDamageA &v = Get (id);
	v.d.torn.push_back (t);
	CollH h = host.Vessel (id);
	if (rec.active && h && !sdk.Playback (h)) { std::vector<std::string> l; CollSide::Torn (sdk.SimTime () - rec.t0, Alias (v, h), t, l); for (auto &x : l) Side (x); }
}

void CollDmgSession::EmitHit (const CollDamageHit &hit)
{
	if (brk) brk->Hit (hit);
	if (fx) fx->Hit (hit);
}

bool CollDmgSession::IsDebris (CollH h)
{
	return h && CollKey::IEqual (sdk.ClassName (h), "CollDebris");
}

void CollDmgSession::SetSites (uint32_t id, const DentSites &s)
{
	auto &v = Get (id).d.sites;
	for (auto &x : v) if (x.slot == s.slot) { x = s; return; }
	v.push_back (s);
}

const DentSites *CollDmgSession::Sites (uint32_t id, uint32_t slot) const
{
	auto it = vessel.find (id);
	if (it == vessel.end ()) return nullptr;
	for (auto &x : it->second.d.sites) if (x.slot == slot) return &x;
	return nullptr;
}

void CollDmgSession::AddCellCuts (uint32_t, uint32_t, const std::vector<uint32_t> &)
{
}

void CollDmgSession::AddBrokenBonds (uint32_t id, uint32_t slot, const std::vector<uint32_t> &bonds)
{
	auto &v = Get (id).d.brokenBonds;
	for (auto &x : v) if (x.first == slot) { for (uint32_t b : bonds) if (std::find (x.second.begin (), x.second.end (), b) == x.second.end ()) x.second.push_back (b); std::sort (x.second.begin (), x.second.end ()); return; }
	std::vector<uint32_t> b = bonds;
	std::sort (b.begin (), b.end ());
	b.erase (std::unique (b.begin (), b.end ()), b.end ());
	v.push_back ({ slot, b });
}

const std::vector<uint32_t> *CollDmgSession::BrokenBonds (uint32_t id, uint32_t slot) const
{
	auto it = vessel.find (id);
	if (it == vessel.end ()) return nullptr;
	for (auto &x : it->second.d.brokenBonds) if (x.first == slot) return &x.second;
	return nullptr;
}

void CollDmgSession::SetDebris (uint32_t id, const std::vector<DentDebris> &d)
{
	Get (id).d.debris = d;
}

void CollDmgSession::Log (const char *fmt, ...)
{
	char b[1024];
	va_list a;
	va_start (a, fmt);
	std::vsnprintf (b, sizeof b, fmt, a);
	va_end (a);
	sdk.Log (1, b);
}

// lifecycle (1.6, 7.6)

bool CollDmgSession::Parse (CollSdk &sdk, CollH scn, CollStoreBlock &pending)
{
	bool ok = CollStore::Parse ([&] (std::string &l) { return sdk.ScnLine (scn, l); }, pending);
	if (!ok) sdk.Log (1, "Collision: scenario block without a COLLA header ignored");
	return ok;
}

void CollDmgSession::Begin (CollStoreBlock &&pending)
{
	blk = std::move (pending);
	begun = true;
	if (!blk.recId.empty ()) play.id = blk.recId, play.active = true;
	if (blk.found)
		Log ("Collision damage block: vessels=%zu bases=%zu unknown=%zu skipped=%d%s%s", blk.vessel.size (), blk.base.size (), blk.unknown.size (), blk.skipped,
			play.active ? " recid=" : "", play.active ? play.id.c_str () : "");
}

void CollDmgSession::End ()
{
	if (rec.file.is_open ()) {
		for (const std::string &l : rec.buf) rec.file << l << '\n';
		rec.buf.clear ();
		rec.file.close ();
	}
	notices.clear ();
	repairs.clear ();
	if (brk) brk->End ();
	if (fx) fx->End ();
}

// matching (2.3, 7.3-7.5)

VesselDamageA *CollDmgSession::Find (uint32_t id)
{
	auto it = vessel.find (id);
	return it == vessel.end () ? nullptr : &it->second;
}

VesselDamageA &CollDmgSession::Get (uint32_t id)
{
	auto it = vessel.find (id);
	if (it != vessel.end ()) return it->second;
	VesselDamageA &v = vessel[id];
	v.id = id;
	CollH h = host.Vessel (id);
	if (h) v.name = sdk.Name (h), v.cls = sdk.ClassName (h);
	return v;
}

void CollDmgSession::MatchAll ()
{
	matched = true;
	std::vector<CollLiveVessel> live;
	std::vector<uint32_t> ids;
	for (uint32_t i = 0, nv = sdk.VesselCount (); i < nv; i++) {
		CollH h = sdk.Vessel (i);
		live.push_back (CollLiveVessel { sdk.Name (h), sdk.ClassName (h) });
		ids.push_back (host.IdOf (h));
	}
	std::vector<int> m = CollStore::MatchVessels (blk.vessel, live);
	size_t nrec = 0, nobj = 0;
	for (size_t s = 0; s < blk.vessel.size (); s++) {
		CollStoreVessel &sv = blk.vessel[s];
		if (sv.skipped) Log ("Collision damage: vessel section '%s' skipped=%d records=%zu", sv.hashed ? CollKey::Hex8 (sv.hName).c_str () : sv.name.c_str (), sv.skipped, sv.d.rec.size ());
		if (m[s] < 0) { dormantVessel.push_back (sv.raw); continue; }
		VesselDamageA &v = Get (ids[m[s]]);
		v.d = sv.d;
		v.match.assign (v.d.rec.size (), -1);
		nrec += v.d.rec.size ();
		host.WantSlots (v.id, true);
		Rematch (v);
		if (!v.d.rec.empty () || v.d.flags || v.d.eabs > 0) Queue (v.id, CDMG_KIND_RESTORED, nullptr);
	}
	std::vector<CollDmgBaseObj> all;
	if (!blk.base.empty ()) host.Bases (all);
	for (const DentBaseText &b : blk.base) {
		std::vector<CollLiveObj> objs;
		std::vector<const CollDmgBaseObj *> view;
		for (const CollDmgBaseObj &o : all)
			if (CollStore::SameBase (b, o.planet, o.base)) objs.push_back (CollLiveObj { o.planet, o.base, o.type, o.obj, o.x, o.z }), view.push_back (&o);
		std::vector<uint8_t> taken (objs.size (), 0);
		DentBaseText dorm = b;
		dorm.obj.clear ();
		for (const DentBaseObjText &o : b.obj) {
			int k = objs.empty () ? -1 : CollStore::MatchObj (o, objs, taken);
			if (k < 0) { dorm.obj.push_back (o); continue; }
			taken[k] = 1;
			const CollDmgBaseObj &lv = *view[k];
			BuildingDamageA &bd = building[{ { lv.planetIdx, lv.baseIdx }, lv.obj }];
			bd.obj = lv, bd.eabs = o.eabs, bd.flags = o.flags;
			nobj++;
		}
		if (!dorm.obj.empty () || !dorm.rec.empty ()) dormantBase.push_back (dorm);
	}
	if (blk.found)
		Log ("Collision damage loaded: vessels=%zu records=%zu bases=%zu objects=%zu dormant vessels=%zu dormant bases=%zu skipped lines=%d",
			vessel.size (), nrec, blk.base.size (), nobj, dormantVessel.size (), dormantBase.size (), blk.skipped);
	blk.vessel.clear ();
	blk.base.clear ();
}

void CollDmgSession::Rematch (VesselDamageA &v, const std::set<uint32_t> &dropped)
{
	uint32_t ns = host.SlotCount (v.id);
	v.nslot = ns;
	std::vector<CollDmgSlot> sl (ns);
	for (uint32_t i = 0; i < ns; i++) host.Slot (v.id, i, sl[i]);
	v.match.resize (v.d.rec.size (), -1);
	std::set<uint32_t> touched (dropped); // a dropped mirror is rebuilt if records still match it
	for (size_t r = 0; r < v.d.rec.size (); r++) {
		DentRecord &R = v.d.rec[r];
		int m = -1;
		if (R.slot < ns && SameSig (R, sl[R.slot])) m = (int)R.slot;
		else for (uint32_t i = 0; i < ns && m < 0; i++) if (SameSig (R, sl[i])) m = (int)i;
		if (m >= 0) R.slot = (uint32_t)m;
		if (v.match[r] == m) continue; // only meshes whose record list changed are re-synced
		if (v.match[r] >= 0) touched.insert ((uint32_t)v.match[r]);
		if (m >= 0) touched.insert ((uint32_t)m);
		v.match[r] = m;
	}
	for (uint32_t m : touched) SyncMirror (v, m);
}

// collider re-apply (2.4): rest plus each matched record once, in record order

void CollDmgSession::SyncCollider (VesselDamageA &v, CollShape *sh, uint32_t mesh, bool force)
{
	std::vector<size_t> &ap = v.applied[mesh];
	if (!sh) { ap.clear (); return; }
	std::vector<size_t> want;
	for (size_t r = 0; r < v.d.rec.size (); r++) if (v.match[r] == (int)mesh) want.push_back (r);
	bool prefix = !force && ap.size () <= want.size () && std::equal (ap.begin (), ap.end (), want.begin ());
	if (!prefix) {
		if (!ap.empty () || !want.empty ()) n.replays++;
		sh->ResetDents (mesh);
		ap.clear ();
	}
	for (size_t k = ap.size (); k < want.size (); k++) {
		ApplyRec (sh, mesh, v.d.rec[want[k]]);
		ap.push_back (want[k]);
	}
}

void CollDmgSession::SyncMirror (VesselDamageA &v, uint32_t mesh)
{
	if (!cfg.visuals) return;
	CollDmgSlot s;
	if (!host.Slot (v.id, mesh, s) || !s.present || !s.rest) { vis.DropSlot (v.id, mesh); return; }
	std::vector<const DentRecord *> rs;
	for (size_t r = 0; r < v.d.rec.size (); r++) if (v.match[r] == (int)mesh) rs.push_back (&v.d.rec[r]);
	vis.SetRecords (v.id, v.name, s, mesh, rs);
}

void CollDmgSession::MarkDirty (uint32_t id, uint32_t mesh, bool replay)
{
	bool &r = dirty[id][mesh];
	r = r || replay;
}

void CollDmgSession::FlushDirty ()
{
	std::map<uint32_t, std::map<uint32_t, bool>> d;
	d.swap (dirty);
	for (auto &kv : d) {
		VesselDamageA *v = Find (kv.first);
		if (!v) continue;
		CollShape *sh = host.Shape (kv.first);
		for (auto &m : kv.second) {
			SyncCollider (*v, sh, m.first, m.second); // without a replay: only records not yet applied
			SyncMirror (*v, m.first);
		}
	}
}

void CollDmgSession::ShapesUpdatedEv (uint32_t id, CollShape *shape, const std::vector<CollDmgSlotEv> &ev)
{
	if (!matched) MatchAll ();
	VesselDamageA *v = Find (id);
	if (!v) { if (brk) brk->Shapes (id, shape); return; }
	bool rematch = false;
	std::set<uint32_t> dropped;
	for (const CollDmgSlotEv &e : ev) {
		if (e.what == CDMG_SLOT_REPLACED || e.what == CDMG_SLOT_GONE) { vis.DropSlot (id, e.mesh); dropped.insert (e.mesh); rematch = true; }
		else if (e.what == CDMG_SLOT_REBUILT) {
			CollDmgSlot s;
			if (host.Slot (id, e.mesh, s)) vis.Rebuilt (id, e.mesh, s.serial, e.src == CDMG_SRC_GCCORE);
			rematch = true;
		}
	}
	uint32_t ns = host.SlotCount (id);
	if (ns > v->nslot) rematch = true; // slots grew (WantSlots polls start one frame late)
	v->nslot = ns;
	if (rematch) Rematch (*v, dropped);
	std::set<uint32_t> meshes;
	for (int m : v->match) if (m >= 0) meshes.insert ((uint32_t)m);
	for (auto &a : v->applied) if (!a.second.empty ()) meshes.insert (a.first);
	for (uint32_t m : meshes) {
		if (shape && shape->Replaced (m)) v->applied[m].clear (); // built, rebuilt or dropped: dents gone
		SyncCollider (*v, shape, m, false);
	}
	if (brk) brk->Shapes (id, shape);
}

// pre-physics (PS2b): repairs, playback, recording link

void CollDmgSession::PrePhysics ()
{
	if (!begun) return;
	if (!matched) MatchAll ();
	double simt = -1;
	for (CollTestRepair &t : blk.testRepair) {
		if (t.done) continue;
		if (simt < 0) simt = sdk.SimTime ();
		if (simt < t.simt) continue;
		t.done = true;
		uint32_t k = 0;
		for (uint32_t i = 0, nv = sdk.VesselCount (); i < nv; i++) {
			CollH h = sdk.Vessel (i);
			if (sdk.Name (h) != t.name) continue;
			if (k++ == t.occ) { RepairVessel (h); break; }
		}
	}
	std::vector<RepairReq> rq;
	rq.swap (repairs);
	for (const RepairReq &r : rq) {
		if (r.building) {
			for (auto it = building.begin (); it != building.end ();) {
				const CollDmgBaseObj &o = it->second.obj;
				if ((o.planet + ":" + o.base) == r.planetBase && (r.obj < 0 || (int)o.obj == r.obj)) {
					Log ("Collision repair building '%s:%s' obj=%u", o.planet.c_str (), o.base.c_str (), o.obj);
					it = building.erase (it);
				} else ++it;
			}
			continue;
		}
		VesselDamageA *v = Find (r.id);
		if (v) DoRepair (*v, false);
	}
	if (rec.active) {
		bool any = false;
		for (uint32_t i = 0, nv = sdk.VesselCount (); i < nv && !any; i++) any = sdk.Recording (sdk.Vessel (i));
		if (!any) {
			if (rec.file.is_open ()) { for (const std::string &l : rec.buf) rec.file << l << '\n'; rec.file.close (); }
			rec.buf.clear ();
			Log ("Collision recorder: link %s ended", rec.id.c_str ());
			rec.active = false;
		}
	}
	if (play.active) Playback (simt < 0 ? sdk.SimTime () : simt);
}

void CollDmgSession::DoRepair (VesselDamageA &v, bool playback)
{
	CollShape *sh = host.Shape (v.id);
	for (auto &a : v.applied) if (sh && !a.second.empty ()) sh->ResetDents (a.first);
	v.applied.clear ();
	std::set<uint32_t> meshes;
	for (int m : v.match) if (m >= 0) meshes.insert ((uint32_t)m);
	auto dv = dirty.find (v.id);
	if (dv != dirty.end ()) { for (auto &m : dv->second) meshes.insert (m.first); dirty.erase (dv); }
	uint32_t was = v.d.flags;
	v.d.rec.clear (), v.match.clear ();
	v.d.eabs = 0;
	v.d.flags = 0;
	v.d.torn.clear (), v.d.debris.clear ();
	if (brk) brk->Repair (v.id);
	if (fx) fx->DropVessel (v.id, host.Vessel (v.id));
	for (uint32_t m : meshes) SyncMirror (v, m); // cur := rp; the pass writes rp to sent vertices
	n.repairs++;
	if (rec.active && !playback) { CollH h = host.Vessel (v.id); if (h) Side (CollSide::Repair (sdk.SimTime () - rec.t0, Alias (v, h))); }
	NoticeA b;
	b.flags = playback ? COLLA_DMG_PLAYBACK : 0;
	Queue (v.id, CDMG_KIND_REPAIRED, &b);
	Log ("Collision repair vessel '%s' flags were 0x%x", v.name.c_str (), was);
	if (!playback) sdk.Notification (COLLN_INFO, "Vessel repaired", v.name.c_str ());
}

// impact events to dents (2)

double CollDmgSession::Threshold (uint32_t id) const
{
	double e = host.DestroyEnergy (id);
	return e >= 0 ? e : cfg.destroyEnergy;
}

void CollDmgSession::Commit (const std::vector<CollImpactEvent> &ev, double simt, const std::vector<CollFxContact> *contacts)
{
	if (!begun) return;
	if (!matched) MatchAll ();
	frameT = simt;
	for (const CollImpactEvent &e : ev) {
		CollH h[2] = { nullptr, nullptr };
		CollDmgBaseObj bo[2];
		bool isV[2], ok[2], pb[2] = { false, false };
		const DentMaterial *mat[2];
		for (int i = 0; i < 2; i++) {
			const CollOwnerRef &o = e.s[i].owner;
			isV[i] = o.planet < 0;
			if (isV[i]) {
				h[i] = host.Vessel (o.vesselId);
				ok[i] = h[i] != nullptr;
				pb[i] = ok[i] && sdk.Playback (h[i]);
				CollShape *sh = ok[i] ? host.Shape (o.vesselId) : nullptr;
				mat[i] = &MaterialOf (sh && e.s[i].mesh >= 0 && e.s[i].grp >= 0 ? sh->Material ((uint32_t)e.s[i].mesh, (uint32_t)e.s[i].grp) : 0, -1);
			} else {
				ok[i] = host.BaseObject (o.planet, o.base, o.obj, bo[i]);
				mat[i] = ok[i] ? &MaterialOf (bo[i].mat, bo[i].cls) : &DentMath::DefaultMaterial (DENTB_BLOCK);
			}
		}
		double E[2], ea[2];
		if (!DentMath::SplitEnergy (e.dKE, e.Wf, e.vn, (e.flags & COLLEV_FIRST) != 0, *mat[0], *mat[1], E, ea) && !loggedNoFirst) {
			loggedNoFirst = true;
			Log ("Collision damage: event energy without FIRST at t=%.17g, counted", e.t);
		}
		for (int i = 0; i < 2; i++) {
			if (!ok[i] || pb[i] || !(ea[i] > 0)) continue;
			int j = 1 - i;
			if (isV[i]) {
				uint32_t id = e.s[i].owner.vesselId;
				VesselDamageA &v = Get (id);
				v.d.eabs = std::min (v.d.eabs + ea[i], DENT_LIM_E);
				if (IsDebris (h[i])) continue; // debris: energy only (L6)
				NoticeA &nt = frameNote[id];
				if (!nt.kind) { nt.id = id; nt.kind = CDMG_KIND_DENT; nt.simt = e.t; nt.mesh = e.s[i].mesh; nt.group = e.s[i].grp; }
				nt.energy += ea[i];
				if (ok[j]) {
					if (isV[j]) nt.hOther = h[j], nt.otherObj = -1;
					else nt.hOther = bo[j].hBase, nt.otherObj = (int32_t)bo[j].obj;
				}
				if (E[i] > 0) Dent (v, h[i], e.s[i], E[i], *mat[i], e.t, nt, &e, isV[j] ? e.s[j].owner.vesselId : 0);
				DestroyedTest (v, h[i], E[i], e.t, 0);
			} else {
				BuildingDamageA &b = building[{ { e.s[i].owner.planet, e.s[i].owner.base }, (uint32_t)e.s[i].owner.obj }];
				bool first = b.eabs == 0 && b.flags == 0;
				b.obj = bo[i];
				b.eabs = std::min (b.eabs + ea[i], DENT_LIM_E);
				double m = DentMath::BuildingMass (bo[i].cls, bo[i].size);
				std::string key = bo[i].planet + ":" + bo[i].base;
				if (cfg.logLevel >= 1) Log ("Collision building t=%.17g '%s' %s #%u E=%.6g eabs=%.6g", e.t, key.c_str (), bo[i].type.c_str (), bo[i].obj, ea[i], b.eabs);
				if (first && cfg.notify >= 2) sdk.Notification (COLLN_INFO, "Building damaged", (key + " " + bo[i].type + " #" + std::to_string (bo[i].obj)).c_str ());
				if (!(b.flags & XDMG_DESTROYED) && m > 0 && b.eabs / m >= cfg.buildingDestroyEnergy) {
					b.flags |= XDMG_DESTROYED;
					Log ("Collision building destroyed '%s' %s #%u eabs=%.6g", key.c_str (), bo[i].type.c_str (), bo[i].obj, b.eabs);
					if (cfg.notify >= 1) sdk.Notification (COLLN_WARNING, "Building destroyed", (key + " " + bo[i].type + " #" + std::to_string (bo[i].obj)).c_str ());
				}
				if (m > 0 && ea[i] / m >= DENT_CATASTROPHIC) b.flags |= XDMG_CATASTROPHIC;
				if (rec.active && isV[j] && ok[j]) Side (CollSide::Building (simt - rec.t0, Alias (Get (e.s[j].owner.vesselId), h[j]), bo[i].obj, b.eabs, b.flags, key));
			}
		}
	}
	FlushDirty ();
	for (auto &kv : frameNote) {
		VesselDamageA *v = Find (kv.first);
		if (!v) continue;
		CollH h = host.Vessel (kv.first);
		if (rec.active && h) Side (CollSide::State (simt - rec.t0, Alias (*v, h), v->d.eabs, v->d.flags));
		notices.push_back (kv.second);
	}
	frameNote.clear ();
	std::vector<NoticeA> later;
	for (auto it = notices.begin (); it != notices.end ();) // DESTROYED after that frame's DENT
		if (it->kind == CDMG_KIND_DESTROYED) { later.push_back (*it); it = notices.erase (it); } else ++it;
	notices.insert (notices.end (), later.begin (), later.end ());
	if (fx && contacts) for (const CollFxContact &c : *contacts) fx->Contact (c);
}

static double Det3 (const Matrix &A) { return A.m11 * (A.m22 * A.m33 - A.m23 * A.m32) - A.m12 * (A.m21 * A.m33 - A.m23 * A.m31) + A.m13 * (A.m21 * A.m32 - A.m22 * A.m31); }

// pose without scale, shear or reflection (dent2 M4)
static bool RigidPose (const CollAffine &X)
{
	if (!(Det3 (X.A) > 0.0)) return false;
	Matrix T = transp (X.A) * X.A;
	const double e[9] = { T.m11 - 1, T.m12, T.m13, T.m21, T.m22 - 1, T.m23, T.m31, T.m32, T.m33 - 1 };
	for (double x : e) if (!(std::fabs (x) < 1e-6)) return false;
	return true;
}

static bool SameAffine (const CollAffine &X, const CollAffine &Y)
{
	const double a[12] = { X.A.m11 - Y.A.m11, X.A.m12 - Y.A.m12, X.A.m13 - Y.A.m13, X.A.m21 - Y.A.m21, X.A.m22 - Y.A.m22, X.A.m23 - Y.A.m23,
		X.A.m31 - Y.A.m31, X.A.m32 - Y.A.m32, X.A.m33 - Y.A.m33, X.t.x - Y.t.x, X.t.y - Y.t.y, X.t.z - Y.t.z };
	for (double d : a) if (!(std::fabs (d) < 1e-9)) return false;
	return true;
}

// parts of mesh whose t1 sphere meets the ball (cw, r), mapped into the frame toT o pose[1]; the hit part always (dent2 D2)
static int ViewNear (const CollShape *sh, uint32_t mesh, uint32_t hit, const CollAffine &toT, const Vector &cw, double r, DentViewData &view, const std::function<bool (uint32_t)> &hidden = nullptr, std::vector<int> *vg = nullptr)
{
	int n = 0;
	for (uint32_t j = 0; j < sh->nPart (); j++) {
		const CollPart &Q = sh->Part (j);
		if (Q.mesh != mesh) continue;
		if (j != hit && (!RigidPose (Q.pose[1]) || (cw - Q.sc[1]).length () >= Q.sr[1] + r)) continue;
		if (j != hit && hidden) { // dmg3: a part whose groups are all hidden by P is gone
			bool all = !Q.Geom ().srcTab.empty ();
			for (const CollSrc &x : Q.Geom ().srcTab) all = all && hidden ((uint32_t)x.grp);
			if (all) continue;
		}
		CollAffine X = CollCompose (toT, Q.pose[1]);
		const CollGeom &G = Q.Geom ();
		uint32_t base = (uint32_t)view.rest.size ();
		for (uint32_t i = 0; i < G.vtx.size (); i++) view.rest.push_back (CollApply (X, G.RestPos (i))), view.cur.push_back (CollApply (X, G.Pos (i)));
		if (vg) for (uint32_t i = 0; i < G.vtx.size (); i++) vg->push_back (G.refOfs[i] < G.refOfs[i+1] && G.ref[G.refOfs[i]].src < G.srcTab.size () ? (int)G.srcTab[G.ref[G.refOfs[i]].src].grp : -1); // m6
		for (const CollTri &tr : G.tri) view.tri.insert (view.tri.end (), { base + tr.v[0], base + tr.v[1], base + tr.v[2] });
		n++;
	}
	return n;
}

// dmg3 tear: view vertices a cut moved take the post-cut position as rest (dents start on the stump face)
static void RebaseCut (const VesselDamageA &v, uint32_t mesh, DentViewData &view, const std::vector<int> &grp)
{
	bool any = false;
	for (size_t k = 0; k < v.d.rec.size () && k < v.match.size (); k++) any = any || (v.match[k] == (int)mesh && v.d.rec[k].p.mode == DENTM_CUT);
	if (!any) return;
	for (size_t i = 0; i < view.rest.size () && i < view.cur.size (); i++) {
		int g = i < grp.size () ? grp[i] : -1;
		std::vector<const DentParams *> op; // m6: records that list the vertex's group
		for (size_t k = 0; k < v.d.rec.size () && k < v.match.size (); k++) {
			const DentRecord &R = v.d.rec[k];
			if (v.match[k] == (int)mesh && (R.grp.empty () || g < 0 || std::find (R.grp.begin (), R.grp.end (), (uint16_t)g) != R.grp.end ())) op.push_back (&R.p);
		}
		bool cut = false;
		DentMath::Fold (op.data (), op.size (), view.rest[i], false, &cut);
		if (cut) view.rest[i] = view.cur[i];
	}
}

// groups without a collider (D7) near (c, r) in the frame toT: rest and rest + matched records on them, for DepthCap only
static void CapView (const CollShape *sh, uint32_t mesh, const CollDmgSlot &slot, const VesselDamageA &v, const CollAffine &toT, const CollAffine &ofs, const Vector &c, double r, DentViewData &out, const std::function<bool (uint32_t)> &hidden = nullptr)
{
	if (!slot.rest || sh->CollMesh (mesh)) return;
	for (uint32_t g = 0; g < slot.rest->grp.size (); g++) {
		CollAffine F;
		if (sh->PartOf (mesh, g) >= 0 || !sh->GroupPose (mesh, g, F) || !RigidPose (F)) continue;
		if (hidden && hidden (g)) continue; // dmg3: hidden by P
		CollAffine X = CollCompose (toT, CollCompose (ofs, F));
		std::vector<const DentRecord *> on;
		for (size_t k = 0; k < v.d.rec.size () && k < v.match.size (); k++) {
			const DentRecord &R = v.d.rec[k];
			if (v.match[k] == (int)mesh && (R.grp.empty () || std::find (R.grp.begin (), R.grp.end (), (uint16_t)g) != R.grp.end ())) on.push_back (&R);
		}
		std::vector<const DentParams *> op;
		bool anyCut = false;
		for (const DentRecord *R : on) op.push_back (&R->p), anyCut = anyCut || R->p.mode == DENTM_CUT;
		for (const CollVtx &x : slot.rest->grp[g].vtx) {
			Vector p (x.x, x.y, x.z), w = CollApply (X, p);
			if (!((w - c).length () < r)) continue;
			bool cut = false;
			Vector q = p;
			if (anyCut) q = p + DentMath::Fold (op.data (), op.size (), p, false, &cut); // dmg3 tear: cuts as maps, not bowls
			else for (const DentRecord *R : on) q += DentMath::DisplaceLow (R->p, p); // m7: the old summation order
			Vector wq = CollApply (X, q);
			out.rest.push_back (cut ? wq : w), out.cur.push_back (wq); // dmg3 tear: stump rest is post-cut
		}
	}
}

void CollDmgSession::Dent (VesselDamageA &v, CollH h, const CollImpactSide &s0, double E, const DentMaterial &mat, double t, NoticeA &note, const CollImpactEvent *ev, uint32_t other)
{
	struct Guard { CollDmgSession *d; bool was; ~Guard () { d->inDent = was; if (!was) d->FlushLater (); } } guard { this, inDent }; // dmg3 tear: cuts wait for the solve
	inDent = true;
	CollShape *sh = host.Shape (v.id);
	if (!sh || s0.mesh < 0 || s0.grp < 0) return;
	CollImpactSide s = s0;
	if (cfg.dentModes) { // dmg3 M8: hits on crushed or folded parts solve in rest space
		std::vector<const DentParams *> mr;
		for (size_t k = 0; k < v.d.rec.size () && k < v.match.size (); k++) {
			const DentRecord &R = v.d.rec[k];
			if (v.match[k] == s.mesh && R.p.mode >= DENTM_CRUSH && (R.grp.empty () || std::binary_search (R.grp.begin (), R.grp.end (), (uint16_t)s.grp))) mr.push_back (&R.p);
		}
		if (!mr.empty ()) DentMath::MapToRest (mr, s.c, s.n);
	}
	uint32_t mesh = (uint32_t)s.mesh, grp = (uint32_t)s.grp;
	auto dv = dirty.find (v.id);
	if (dv != dirty.end ()) {
		auto dm = dv->second.find (mesh);
		if (dm != dv->second.end () && dm->second) { SyncCollider (v, sh, mesh, true); dm->second = false; } // a record grown earlier in this commit: replay before this hit reads the collider
	}
	int part = sh->PartOf (mesh, grp);
	CollDmgSlot slot;
	if (part < 0 || !host.Slot (v.id, mesh, slot) || !slot.present) return;
	if (!slot.rest) for (size_t r = 0; r < v.d.rec.size (); r++) if (v.match[r] < 0 && v.d.rec[r].slot == mesh) return; // the slot's records wait for its rest mesh: energy only; with it a stale record stays dormant
	double size = sdk.Size (h);
	double Rmax = DENT_RMAX_SIZE * size; // not capped by the hit part (dent2 D1)
	CollRayHit hit;
	double rayT = sh->RayRest (mesh, grp, s.c, -s.n, DENT_RAY_TMIN, DENT_LIM_T, hit) ? hit.t : -1.0;
	const CollPart &P = sh->Part ((uint32_t)part);
	const CollGeom &G = P.Geom ();
	if (!(std::fabs (Det3 (P.pose[1].A)) > 1e-12)) return; // degenerate pose: no frame to dent in
	CollAffine Pi = CollInverse (P.pose[1]);
	std::function<bool (uint32_t)> hid = nullptr;
	if (brk) hid = [this, &v, mesh] (uint32_t g) { return brk->Hidden (v.id, mesh, g); };
	DentViewData view;
	std::vector<int> vgrp;
	int nview = ViewNear (sh, mesh, (uint32_t)part, Pi, CollApply (P.pose[1], s.c), Rmax, view, hid, &vgrp);
	RebaseCut (v, mesh, view, vgrp);
	CollAffine ofs = CollCompose (P.pose[1], CollInverse (P.anim[1])); // Translate(mesh offset)
	DentViewData capv; // D7 groups for the depth cap only (fix1)
	CapView (sh, mesh, slot, v, Pi, ofs, s.c, std::max (Rmax, DentMath::LowPolyFloor (view.View (), s.c)), capv, hid);
	DentMeshView cv = capv.View ();
	DentInput in { E, &mat, s.c, s.n, s.a, Rmax, size, rayT, true };
	auto partGroups0 = [] (const CollGeom &g) {
		std::vector<uint16_t> out;
		for (const CollSrc &x : g.srcTab) out.push_back ((uint16_t)x.grp);
		std::sort (out.begin (), out.end ());
		out.erase (std::unique (out.begin (), out.end ()), out.end ());
		return out;
	};
	std::vector<uint8_t> keyed; // merge key: collider groups only, D7 membership follows the animation state
	if (!sh->CollMesh (mesh)) for (uint32_t g = 0; g < slot.ngrp; g++) keyed.push_back (sh->PartOf (mesh, g) >= 0 ? 1 : 0);
	const std::vector<uint8_t> *key = keyed.empty () ? nullptr : &keyed;
	DentSolveX sx;
	in.x = &sx, in.tdir = s.tdir, in.vt = ev ? ev->vt : 0.0, in.salt = (uint32_t)v.d.rec.size ();
	if (cfg.dentNoise) in.modes |= DENTI_NOISE;
	int hingeK = -1; // dmg3: hinge record in coalesce range (grows instead of a new one)
	if (cfg.dentModes) { // dmg3: crush and hinge allowed; a hit in coalesce range of a record reuses its mode, hinge wins
		in.modes |= DENTI_CRUSH | DENTI_HINGE;
		DentRecord pr {};
		pr.p.c = s.c, pr.p.n = Unit (s.n), pr.p.R = 1.0, pr.slot = mesh, pr.key = slot.key, pr.ngrp = slot.ngrp, pr.nvtx = slot.nvtx, pr.grp = partGroups0 (G);
		int kp = DentMath::FindCoalesce (v.d.rec, pr, key, true);
		if (kp >= 0 && v.match[kp] == (int)mesh && v.d.rec[kp].p.mode != DENTM_CUT) in.force = (int)v.d.rec[kp].p.mode; // dmg3 tear: never forces a cut
		pr.p.mode = DENTM_HINGE;
		for (size_t k = 0; k < v.d.rec.size (); k++) {
			const DentRecord &o = v.d.rec[k];
			if (o.p.mode != DENTM_HINGE || v.match[k] != (int)mesh) continue;
			std::vector<DentRecord> one (1, o);
			if (DentMath::FindCoalesce (one, pr, key) == 0) { hingeK = (int)k; in.force = DENTM_HINGE; break; }
		}
	}
	DentParams p;
	int res = DentMath::Solve (in, view.View (), capv.rest.empty () ? nullptr : &cv, p);
	bool hingeOnly = res == DENT_SMALL && sx.hinge && hingeK >= 0; // dmg3 M1: a repeat hit on a fold grows the hinge only
	if (hingeOnly) sx.Esurplus = E, sx.hp.P = 0.0; // all energy to the hinge
	if (res != DENT_OK && !hingeOnly) { // dmg3: energy only; the units still see the hit
		CollDamageHit eh;
		double em = sdk.EmptyMass (h);
		eh.id = v.id, eh.other = other, eh.h = h, eh.mesh = mesh, eh.grp = s.grp, eh.tri = s.tri, eh.rec = -1;
		eh.c = CollApply (P.pose[1], s.c), eh.n = Unit (CollApplyDir (P.pose[1], s.n)), eh.tdir = Unit (CollApplyDir (P.pose[1], s.tdir));
		eh.E = E, eh.eSpec = em > 0 ? E / em : 0.0, eh.Esurplus = 0, eh.R = p.R;
		if (ev) eh.vn = ev->vn, eh.vt = ev->vt, eh.evflags = ev->flags;
		eh.simt = t, eh.mat = &mat;
		EmitHit (eh);
		return;
	}
	DentMath::Quantise (p);
	double placedS = DentMath::VolumeFactor (p, view.View ()); // dmg3: Esurplus = E - sigma_c h S
	auto partGroups = [] (const CollGeom &g) {
		std::vector<uint16_t> out;
		for (const CollSrc &x : g.srcTab) out.push_back ((uint16_t)x.grp);
		std::sort (out.begin (), out.end ());
		out.erase (std::unique (out.begin (), out.end ()), out.end ());
		return out;
	};
	std::vector<DentRecord> nr;
	std::vector<CollAffine> nrF; // animation transform of each record's frame
	DentRecord r {};
	r.p = p, r.slot = mesh, r.key = slot.key, r.ngrp = slot.ngrp, r.nvtx = slot.nvtx, r.grp = partGroups (G), r.flags = 0;
	nr.push_back (r), nrF.push_back (P.anim[1]);
	Vector cw = CollApply (P.pose[1], p.c), nw = Unit (CollApplyDir (P.pose[1], p.n));
	for (uint32_t j = 0; j < sh->nPart (); j++) { // partitions (D4 4.7)
		const CollPart &Q = sh->Part (j);
		if ((int)j == part || Q.mesh != mesh || !RigidPose (Q.pose[1])) continue;
		if ((cw - Q.sc[1]).length () >= Q.sr[1] + std::sqrt (p.R * p.R + p.P * p.P)) continue; // dmg3 M3: crush reach sqrt(R^2 + P^2)
		CollAffine Qi = CollInverse (Q.pose[1]);
		DentRecord q = r;
		q.p.c = CollApply (Qi, cw), q.p.n = Unit (CollApplyDir (Qi, nw));
		if (!DentMath::Legacy (p)) q.p.t = MapT (Qi, CollApplyDir (P.pose[1], p.t), q.p.n);
		DentMath::Quantise (q.p);
		q.grp = partGroups (Q.Geom ());
		nr.push_back (q), nrF.push_back (Q.anim[1]);
	}
	for (uint32_t g = 0; !sh->CollMesh (mesh) && g < slot.ngrp; g++) { // groups without a collider (cabin, pilots, tunnels) follow the record of their transform (dent2 D7); not with a MESH collider
		CollAffine F;
		if (sh->PartOf (mesh, g) >= 0 || !sh->GroupPose (mesh, g, F)) continue;
		size_t i = 0;
		while (i < nr.size () && !SameAffine (nrF[i], F)) i++;
		if (i == nr.size ()) {
			if (!RigidPose (F)) continue;
			CollAffine Gi = CollInverse (CollCompose (ofs, F));
			DentRecord q = r;
			q.p.c = CollApply (Gi, cw), q.p.n = Unit (CollApplyDir (Gi, nw));
			if (!DentMath::Legacy (p)) q.p.t = MapT (Gi, CollApplyDir (P.pose[1], p.t), q.p.n);
			DentMath::Quantise (q.p);
			q.grp.clear ();
			nr.push_back (q), nrF.push_back (F);
		}
		nr[i].grp.push_back ((uint16_t)g);
	}
	for (DentRecord &x : nr) std::sort (x.grp.begin (), x.grp.end ());
	double V = mat.sigma_c > 0 ? E / mat.sigma_c : 0;
	unsigned ncopy = 0; // copies stored or grown besides the hit part's record
	double placed = 0, depth = 0; // dmg3: volume placed by this event [m^3], max displacement added
	int hitRec = -1;
	auto sideExt = [&] (uint32_t k, const DentRecord &o, bool hk) { // dmg3: X right after the record's own D lines; hk marks the event's hit record (M3)
		if (rec.active && !DentMath::Legacy (o.p)) Side (CollSide::Ext (frameT - rec.t0, Alias (v, h), k, o.p, E, ev ? ev->vn : 0.0, ev ? ev->vt : 0.0, hk ? 1 : 0, ev ? ev->flags : 0u));
	};
	int k0 = hingeOnly ? -1 : DentMath::FindCoalesce (v.d.rec, nr[0], key); // decided once on the hit part (dent2 B1, B2)
	if (k0 >= 0 && (v.match[k0] != (int)mesh || nr[0].p.R > v.d.rec[k0].p.R)) k0 = -1;
	double hingeMp = 0, hingeDP = 0; // dmg3 M3: the fold is decided before any X line so the hit's X carries the flag
	size_t hingeK1 = 0;
	auto sameRun = [&] (size_t k, const DentParams &o) { const DentRecord &x = v.d.rec[k]; return x.p.mode == DENTM_HINGE && x.p.R == o.R && x.p.hd == o.hd && x.p.hz == o.hz && x.p.P == o.P && v.match[k] == (int)mesh; };
	if (sx.hinge && hingeK >= 0) {
		const DentParams &o = v.d.rec[hingeK].p;
		hingeMp = DentMath::HingeMp (o, mat);
		hingeDP = hingeMp > 0 ? std::min (o.P + (sx.Esurplus + 2.0 * sx.Mp * sx.hp.P) / (2.0 * hingeMp), DENT_HINGE_TMAX) - o.P : 0.0;
		hingeK1 = (size_t)hingeK;
		while (hingeK1 > 0 && sameRun (hingeK1 - 1, o)) hingeK1--; // dmg3 m1: the run starts at its first copy
	}
	bool hingeGo = sx.hinge && (hingeK >= 0 ? hingeDP > 0 : (k0 >= 0 || hingeOnly ? v.d.rec.size () : std::min<size_t> (DENT_MAX_VESSEL, v.d.rec.size () + nr.size ())) < DENT_MAX_VESSEL);
	if (k0 >= 0) {
		const DentParams o0 = v.d.rec[k0].p;
		DentViewData ov, ox;
		ViewNear (sh, mesh, (uint32_t)part, Pi, CollApply (P.pose[1], o0.c), o0.R, ov);
		CapView (sh, mesh, slot, v, Pi, ofs, o0.c, o0.R, ox);
		DentMeshView xv = ox.View ();
		double dh = 0, dP = 0;
		double S0 = DentMath::VolumeFactor (o0, ov.View ()), q0 = o0.h > 0 ? DentMath::MaxDisplace (o0, ov.View ()) / o0.h : 0.0;
		if (o0.mode == DENTM_CRUSH) { // dmg3: grow P and h together; copies get the same deltas
			double Pn, hn;
			if (DentMath::CoalesceCrush (o0, V, ov.View (), ox.rest.empty () ? nullptr : &xv, size, Pn, hn)) {
				dh = hn - o0.h, dP = Pn - o0.P;
				DentParams g = o0;
				g.P = Pn, g.h = hn;
				placed = hn * DentMath::VolumeFactor (g, ov.View ()) - o0.h * S0;
				depth = DentMath::MaxDisplace (g, ov.View ()) - o0.h * q0;
			}
		} else {
			dh = DentMath::CoalesceDepth (o0, V, ov.View (), ox.rest.empty () ? nullptr : &xv, DentMath::DmaxVessel (o0.T, o0.R, size));
			placed = dh * S0, depth = dh * q0;
		}
		bool grown = false;
		for (const DentRecord &x : nr) {
			if (!(dh > 0) && !(dP > 0)) break;
			int k = DentMath::FindCoalesce (v.d.rec, x, key);
			if (k < 0 || v.match[k] != (int)mesh || x.p.R > v.d.rec[k].p.R) continue; // no partner, or a smaller one: skipped
			DentRecord &o = v.d.rec[k];
			o.p.h = std::min (o.p.h + dh, DENT_LIM_H);
			if (o.p.mode == DENTM_CRUSH) o.p.P = std::min (o.p.P + dP, DENT_LIM_H);
			DentMath::Quantise (o.p);
			n.coalesced++;
			if (grown) ncopy++;
			grown = true;
			if (rec.active) { std::vector<std::string> l; CollSide::Dent (frameT - rec.t0, Alias (v, h), (uint32_t)k, o, l); for (auto &s2 : l) Side (s2); sideExt ((uint32_t)k, o, !hingeGo && k == k0); }
		}
		if (grown) hitRec = k0;
		if (grown) MarkDirty (v.id, mesh, true); // grown records replay the mesh from rest once, at the end of the commit
		p = v.d.rec[k0].p; // the grown record for the note and the log; dh <= 0 (at its cap): energy only, as before
		cw = CollApply (P.pose[1], p.c), nw = Unit (CollApplyDir (P.pose[1], p.n));
	} else if (!hingeOnly) {
		for (DentRecord &x : nr) {
			if (v.d.rec.size () >= DENT_MAX_VESSEL) {
				if (!v.loggedCap) { v.loggedCap = true; Log ("Collision dent: '%s' holds %u records, energy only", v.name.c_str (), DENT_MAX_VESSEL); }
				continue;
			}
			v.d.rec.push_back (x);
			v.match.push_back ((int)mesh);
			MarkDirty (v.id, mesh, false);
			if (!dirty[v.id][mesh]) SyncCollider (v, sh, mesh, false); // new records go on now; after a growth the end-of-commit replay adds them
			if (&x != &nr[0]) ncopy++;
			else hitRec = (int)v.d.rec.size () - 1, placed = p.h * placedS, depth = DentMath::MaxDisplace (p, view.View ());
			n.dents++;
			if (rec.active) { std::vector<std::string> l; CollSide::Dent (frameT - rec.t0, Alias (v, h), (uint32_t)(v.d.rec.size () - 1), x, l); for (auto &s2 : l) Side (s2); sideExt ((uint32_t)(v.d.rec.size () - 1), x, !hingeGo && &x == &nr[0]); }
		}
	}
	double hingeE = 0; // dmg3: energy the fold took
	if (sx.hinge) { // dmg3 S4: plate bowl overflow folds the plate along a line
		if (hingeK >= 0) { // grows the hinge in range and its copies (stored right after it)
			DentParams o = v.d.rec[hingeK].p;
			double Mp = hingeMp, dP = hingeDP;
			if (hingeGo) {
				for (size_t k = hingeK1; k < v.d.rec.size () && sameRun (k, o); k++) {
					DentRecord &x = v.d.rec[k];
					x.p.P = std::min (x.p.P + dP, DENT_HINGE_TMAX);
					DentParams u = x.p;
					u.h = 1.0;
					int qj = x.grp.empty () ? -1 : sh->PartOf (mesh, x.grp[0]);
					DentViewData xvd; // dmg3 m1: h on the copy's own part
					if (qj >= 0 && qj != part) { const CollPart &Q = sh->Part ((uint32_t)qj); ViewNear (sh, mesh, (uint32_t)qj, CollInverse (Q.pose[1]), CollApply (Q.pose[1], x.p.c), Rmax, xvd, hid); }
					x.p.h = std::min (DentMath::MaxDisplace (u, qj >= 0 && qj != part ? xvd.View () : view.View ()), x.p.hz);
					DentMath::Quantise (x.p);
					n.coalesced++;
					if (rec.active) { std::vector<std::string> l; CollSide::Dent (frameT - rec.t0, Alias (v, h), (uint32_t)k, x, l); for (auto &s2 : l) Side (s2); sideExt ((uint32_t)k, x, k == hingeK1); }
				}
				hingeE = 2.0 * Mp * dP, hitRec = (int)hingeK1;
				depth = std::max (depth, DentMath::MaxDisplace (v.d.rec[hingeK1].p, view.View ()));
				MarkDirty (v.id, mesh, true);
			}
		} else if (hingeGo) {
			DentParams hp = sx.hp;
			DentMath::Quantise (hp);
			Vector h0w = CollApply (P.pose[1], hp.c - hp.t * hp.hd - hp.n * hp.hz), aw = Unit (CollApplyDir (P.pose[1], hp.t)), nhw = Unit (CollApplyDir (P.pose[1], hp.n));
			Vector ew = crossp (nhw, aw), chw = CollApply (P.pose[1], hp.c);
			auto push = [&] (DentRecord &x, bool hk) {
				v.d.rec.push_back (x);
				v.match.push_back ((int)mesh);
				n.dents++;
				if (rec.active) { std::vector<std::string> l; CollSide::Dent (frameT - rec.t0, Alias (v, h), (uint32_t)(v.d.rec.size () - 1), x, l); for (auto &s2 : l) Side (s2); sideExt ((uint32_t)(v.d.rec.size () - 1), x, hk); }
			};
			DentRecord x0 = nr[0];
			x0.p = hp;
			push (x0, true);
			hitRec = (int)v.d.rec.size () - 1, depth = std::max (depth, DentMath::MaxDisplace (hp, view.View ()));
			for (uint32_t j = 0; j < sh->nPart () && v.d.rec.size () < DENT_MAX_VESSEL; j++) { // dmg3 M2: every part of the mesh the flap slab reaches (ailerons)
				const CollPart &Q = sh->Part (j);
				if ((int)j == part || Q.mesh != mesh || !RigidPose (Q.pose[1])) continue;
				Vector d = Q.sc[1] - h0w;
				if (!(dotp (d, aw) > -Q.sr[1]) || !(std::fabs (dotp (d, ew)) < 2.0 * hp.R + Q.sr[1])) continue;
				CollAffine Qi = CollInverse (Q.pose[1]);
				DentRecord x = r;
				x.p = hp;
				x.p.c = CollApply (Qi, chw), x.p.n = Unit (CollApplyDir (Qi, nhw)), x.p.t = MapT (Qi, aw, x.p.n);
				DentMath::Quantise (x.p);
				x.grp = partGroups (Q.Geom ());
				push (x, false);
				ncopy++;
			}
			hingeE = 2.0 * sx.Mp * hp.P;
			MarkDirty (v.id, mesh, true);
		}
	}
	MarkDirty (v.id, mesh, false); // the mirror is rebuilt once per slot at the end of the commit
	if (p.h >= note.depth) note.depth = p.h, note.pos = cw, note.nml = nw, note.mesh = (int32_t)mesh, note.group = (int32_t)grp, note.simt = t;
	double m = sdk.EmptyMass (h);
	if (cfg.logLevel >= 1)
		Log ("Collision dent t=%.17g '%s' mesh=%u grp=%u E=%.6g R=%.6g h=%.6g T=%.6g mode=%u P=%.6g parts=%d copies=%u%s eabs=%.6g (%.6g J/kg)", t, v.name.c_str (), mesh, grp, E, p.R, p.h, p.T,
			p.mode, p.P, nview, ncopy, k0 >= 0 ? " grown" : "", v.d.eabs, m > 0 ? v.d.eabs / m : 0.0);
	CollDamageHit dh3; // dmg3 L2: vessel frame
	dh3.id = v.id, dh3.other = other, dh3.h = h, dh3.mesh = mesh, dh3.grp = s.grp, dh3.tri = s.tri, dh3.rec = hitRec;
	dh3.c = cw, dh3.n = nw, dh3.tdir = Unit (CollApplyDir (P.pose[1], s.tdir));
	dh3.E = E, dh3.eSpec = m > 0 ? E / m : 0.0;
	dh3.Esurplus = hitRec < 0 ? E : std::min (E, std::max (0.0, E - mat.sigma_c * std::max (0.0, placed) - hingeE));
	dh3.R = p.R, dh3.depth = std::max (0.0, depth), dh3.mode = hitRec >= 0 && (size_t)hitRec < v.d.rec.size () ? v.d.rec[hitRec].p.mode : p.mode;
	if (ev) dh3.vn = ev->vn, dh3.vt = ev->vt, dh3.evflags = ev->flags, dh3.Jn = ev->Jn;
	dh3.dt = lastPostDt > 0 ? lastPostDt : 1.0 / 60.0; // blast: frame step for force = Jn / dt
	dh3.simt = t, dh3.playback = false, dh3.mat = &mat;
	dh3.Mp = hingeMp > 0 ? hingeMp : sx.Mp; // dmg3 tear: tip tear gate
	EmitHit (dh3);
}

void CollDmgSession::DestroyedTest (VesselDamageA &v, CollH h, double Ei, double t, uint32_t extraFlags)
{
	double me = sdk.EmptyMass (h), mt = sdk.Mass (h), m = me > 0 ? me : mt;
	if (m > 0 && Ei / m >= DENT_CATASTROPHIC) v.d.flags |= XDMG_CATASTROPHIC;
	if (v.d.flags & XDMG_DESTROYED) return;
	if (!DentMath::Destroyed (v.d.eabs, me, mt, Threshold (v.id))) return;
	v.d.flags |= XDMG_DESTROYED;
	NoticeA b;
	b.simt = t, b.flags = extraFlags;
	Queue (v.id, CDMG_KIND_DESTROYED, &b);
	if (fx) fx->Destroyed (v.id);
	Log ("Collision vessel destroyed '%s' eabs=%.6g (%.6g J/kg)", v.name.c_str (), v.d.eabs, m > 0 ? v.d.eabs / m : 0.0);
	if (cfg.notify >= 1) sdk.Notification (COLLN_WARNING, "Vessel destroyed", v.name.c_str ());
}

// notices (6)

void CollDmgSession::Queue (uint32_t id, int kind, const NoticeA *base)
{
	NoticeA nt = base ? *base : NoticeA ();
	nt.id = id, nt.kind = kind;
	notices.push_back (nt);
}

void CollDmgSession::SendNotices ()
{
	std::vector<NoticeA> q;
	q.swap (notices);
	for (const NoticeA &nt : q) {
		CollH h = host.Vessel (nt.id);
		VesselDamageA *v = Find (nt.id);
		if (!h) continue;
		COLLA_DAMAGEINFO info;
		std::memset (&info, 0, sizeof info);
		info.hdr.magic = COLLA_MAGIC, info.hdr.version = COLLA_VERSION, info.hdr.kind = (uint16_t)nt.kind, info.hdr.size = sizeof info;
		info.flags = nt.flags | (v ? NoticeFlags (v->d.flags, v->cut.dummy != nullptr) : 0) | (sdk.Playback (h) ? COLLA_DMG_PLAYBACK : 0); // now, not when the vessel was last played back
		info.hOther = (OBJHANDLE)nt.hOther, info.otherObj = nt.otherObj, info.mesh = nt.mesh, info.group = nt.group;
		info.ndent = v ? (uint32_t)v->d.rec.size () : 0;
		info.simt = nt.simt;
		info.pos = _V (nt.pos.x, nt.pos.y, nt.pos.z), info.nml = _V (nt.nml.x, nt.nml.y, nt.nml.z);
		info.energy = nt.energy, info.energy_total = v ? v->d.eabs : 0, info.depth = nt.depth, info.destroyEnergy = Threshold (nt.id);
		int reply = 0;
		bool sent = sdk.Notify (h, nt.kind, &info, reply);
		n.notices++;
		if (!sent || (nt.kind != CDMG_KIND_DESTROYED && nt.kind != CDMG_KIND_RESTORED)) continue;
		v = Find (nt.id); // module code ran: re-resolve
		if (!v) continue;
		if (reply == CDMG_HANDLED) v->d.flags |= XDMG_MODULEFX;
		else v->d.flags &= ~XDMG_MODULEFX;
	}
}

// thrust cut (5)

bool CollDmgSession::HasTank (CollH h, CollH tk)
{
	for (uint32_t i = 0, k = sdk.TankCount (h); i < k; i++) if (sdk.Tank (h, i) == tk) return true;
	return false;
}

bool CollDmgSession::HasThruster (CollH h, CollH th)
{
	for (uint32_t i = 0, k = sdk.ThrusterCount (h); i < k; i++) if (sdk.Thruster (h, i) == th) return true;
	return false;
}

bool CollDmgSession::AnyVesselHasTank (CollH tk)
{
	for (uint32_t i = 0, nv = sdk.VesselCount (); i < nv; i++) if (HasTank (sdk.Vessel (i), tk)) return true;
	return false;
}

bool CollDmgSession::IsDummy (CollH h, CollH tk) { return tk && HasTank (h, tk) && sdk.TankMaxMass (h, tk) == COLL_DUMMY_MAXMASS; }

void CollDmgSession::KeepLast (CollH h, ThrustCutA &c)
{
	uint32_t nt = sdk.TankCount (h);
	if (!IsDummy (h, c.dummy) || sdk.Tank (h, nt - 1) == c.dummy) return;
	std::vector<CollH> on;
	for (uint32_t i = 0, k = sdk.ThrusterCount (h); i < k; i++) { CollH th = sdk.Thruster (h, i); if (sdk.ThrusterTank (h, th) == c.dummy) on.push_back (th); }
	sdk.DelTank (h, c.dummy);
	c.dummy = sdk.CreateTank (h, COLL_DUMMY_MAXMASS, 0.0);
	for (CollH th : on) sdk.SetTank (h, th, c.dummy);
	c.remakes++;
	n.remakes++;
}

void CollDmgSession::Cut (CollH h, VesselDamageA &v, bool want)
{
	ThrustCutA &c = v.cut;
	if (want) {
		bool fresh = !c.dummy;
		if (!IsDummy (h, c.dummy)) c.dummy = sdk.CreateTank (h, COLL_DUMMY_MAXMASS, 0.0);
		KeepLast (h, c);
		if (sdk.TankMass (h, c.dummy) != 0.0) {
			sdk.SetTankMass (h, c.dummy, 0.0);
			if (!c.loggedRefill) { c.loggedRefill = true; Log ("Collision thrust cut: '%s' dummy tank refilled", v.name.c_str ()); }
		}
		bool relinked = false;
		uint32_t nth = sdk.ThrusterCount (h);
		std::vector<CollH> now;
		for (uint32_t i = 0; i < nth; i++) {
			CollH th = sdk.Thruster (h, i), tk = sdk.ThrusterTank (h, th);
			now.push_back (th);
			if (tk == c.dummy) continue;
			auto w = std::find_if (c.wish.begin (), c.wish.end (), [th] (const std::pair<CollH, CollH> &x) { return x.first == th; });
			if (w == c.wish.end ()) { c.wish.push_back ({ th, tk }); if (!tk) continue; }
			else if (!tk) { w->second = nullptr; continue; }
			else { w->second = tk; relinked = true; c.relinks++; n.relinks++; }
			sdk.SetTank (h, th, c.dummy);
		}
		std::sort (now.begin (), now.end (), std::less<CollH> ());
		c.wish.erase (std::remove_if (c.wish.begin (), c.wish.end (), [&now] (const std::pair<CollH, CollH> &w) { return !std::binary_search (now.begin (), now.end (), w.first, std::less<CollH> ()); }), c.wish.end ()); // deleted thrusters leave the list
		c.run = relinked ? c.run + 1 : 0;
		if (c.run == 60 && !c.loggedRun) { c.loggedRun = true; Log ("Collision thrust cut: '%s' ineffective, the module re-links thrusters every frame", v.name.c_str ()); }
		if (fresh) Log ("Collision thrust cut on: '%s' thrusters=%u tanks=%u", v.name.c_str (), nth, sdk.TankCount (h));
	} else if (c.dummy) {
		for (auto &w : c.wish)
			if (HasThruster (h, w.first) && sdk.ThrusterTank (h, w.first) == c.dummy)
				sdk.SetTank (h, w.first, w.second && AnyVesselHasTank (w.second) ? w.second : nullptr);
		if (IsDummy (h, c.dummy)) sdk.DelTank (h, c.dummy);
		Log ("Collision thrust cut off: '%s' relinks=%u remakes=%u tanks=%u", v.name.c_str (), c.relinks, c.remakes, sdk.TankCount (h));
		c = ThrustCutA ();
	}
}

void CollDmgSession::EndFrame ()
{
	int dm = -2;
	for (auto &kv : vessel) {
		VesselDamageA &v = kv.second;
		if (!(v.d.flags & XDMG_DESTROYED) && !v.cut.dummy) continue;
		CollH h = host.Vessel (kv.first);
		if (!h) continue;
		if (dm == -2) dm = sdk.VesselCount () ? sdk.DamageModel (sdk.Vessel (0)) : 0;
		bool want = (v.d.flags & XDMG_DESTROYED) && !(v.d.flags & XDMG_MODULEFX) && dm != 0 && !sdk.Playback (h) && cfg.thrustCut;
		Cut (h, v, want);
	}
}

void CollDmgSession::PostStep ()
{
	for (auto &kv : vessel) {
		if (!kv.second.cut.dummy) continue;
		CollH h = host.Vessel (kv.first);
		if (h) KeepLast (h, kv.second.cut);
	}
	vis.Pass (CollVisualA::PASS_ALL);
	double t = sdk.SimTime (), dt = lastPostT >= 0 && t > lastPostT ? t - lastPostT : 0.0;
	lastPostT = t;
	if (dt > 0) lastPostDt = dt;
	if (brk) brk->Post (t, dt);
	if (fx) fx->Post (t, dt);
}

void CollDmgSession::KeyPass () { if (cfg.keyPass) vis.Pass (CollVisualA::PASS_PENDING); if (brk) brk->Pass (); }

void CollDmgSession::PausePass () { vis.Pass (CollVisualA::PASS_ALL); if (brk) brk->Pass (); }

void CollDmgSession::OnNewVessel (CollH) {}

void CollDmgSession::OnDeleteVessel (uint32_t id)
{
	vessel.erase (id);
	vis.DropVessel (id);
	frameNote.erase (id);
	dirty.erase (id);
}

// exported API (10)

int CollDmgSession::RepairVessel (CollH h)
{
	if (!h || !sdk.IsVessel (h)) return 0;
	RepairReq r;
	r.id = host.IdOf (h);
	repairs.push_back (r);
	return 1;
}

int CollDmgSession::RepairBuilding (const char *planetBase, int obj)
{
	if (!planetBase) return 0;
	RepairReq r;
	r.building = true, r.planetBase = planetBase, r.obj = obj;
	repairs.push_back (r);
	return 1;
}

int CollDmgSession::GetVesselDamage (CollH h, void *out)
{
	COLLA_DAMAGEINFO *info = (COLLA_DAMAGEINFO *)out;
	if (!h || !info || !sdk.IsVessel (h)) return 0;
	uint32_t id = host.IdOf (h);
	const VesselDamageA *v = Damage (id);
	COLLA_DAMAGEINFO x;
	std::memset (&x, 0, sizeof x);
	size_t sz = std::min<size_t> (info->hdr.size, sizeof x);
	x.hdr.magic = COLLA_MAGIC, x.hdr.version = COLLA_VERSION, x.hdr.kind = COLLA_KIND_STATE, x.hdr.size = (uint32_t)sz;
	x.otherObj = -1, x.mesh = -1, x.group = -1;
	x.destroyEnergy = Threshold (id);
	if (v) {
		x.flags = NoticeFlags (v->d.flags, v->cut.dummy != nullptr) | (sdk.Playback (h) ? COLLA_DMG_PLAYBACK : 0);
		x.ndent = (uint32_t)v->d.rec.size ();
		x.energy_total = v->d.eabs;
		for (size_t r = 0; r < v->d.rec.size (); r++)
			if (v->d.rec[r].p.h > x.depth) {
				x.depth = v->d.rec[r].p.h, x.mesh = (int32_t)v->d.rec[r].slot;
				x.pos = _V (v->d.rec[r].p.c.x, v->d.rec[r].p.c.y, v->d.rec[r].p.c.z), x.nml = _V (v->d.rec[r].p.n.x, v->d.rec[r].p.n.y, v->d.rec[r].p.n.z);
			}
	}
	if (sz < sizeof x.hdr) return 0;
	std::memcpy (info, &x, sz);
	return 1;
}

int CollDmgSession::GetBuildingDamage (const char *planetBase, int obj, double *eabs, uint32_t *flags)
{
	if (!planetBase) return 0;
	for (const auto &kv : building) {
		const CollDmgBaseObj &o = kv.second.obj;
		if ((o.planet + ":" + o.base) == planetBase && (int)o.obj == obj) {
			if (eabs) *eabs = kv.second.eabs;
			if (flags) *flags = kv.second.flags;
			return 1;
		}
	}
	std::vector<CollDmgBaseObj> all;
	host.Bases (all);
	for (const CollDmgBaseObj &o : all)
		if ((o.planet + ":" + o.base) == planetBase && (int)o.obj == obj) {
			if (eabs) *eabs = 0;
			if (flags) *flags = 0;
			return 1;
		}
	return 0;
}

// persistence (7)

void CollDmgSession::StartRecording ()
{
	rec.active = true;
	bool fixed = !cfg.testRecId.empty ();
	if (fixed) rec.id = cfg.testRecId;
	else {
		static uint32_t counter = 0; // per process (8.2)
		std::time_t t = std::time (nullptr);
		char b[64];
		std::tm tm {};
#ifdef _WIN32
		gmtime_s (&tm, &t);
#else
		gmtime_r (&t, &tm);
#endif
		std::snprintf (b, sizeof b, "%04d%02d%02d-%02d%02d%02d-%u", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ++counter);
		rec.id = b;
	}
	rec.t0 = sdk.SimTime ();
	rec.nalias = 0;
	for (auto &kv : vessel) kv.second.alias = ~0u;
	std::error_code ec;
	std::filesystem::create_directories (sideDir, ec);
	rec.file.open (sideDir + "/" + rec.id + ".txt", fixed ? std::ios::trunc : std::ios::app);
	rec.failed = (bool)ec || !rec.file;
	if (rec.failed) Log ("Collision recorder: side file not writable");
	else rec.file << CollSide::Header (rec.id) << '\n';
	Log ("Collision recorder: link %s started t0=%.17g", rec.id.c_str (), rec.t0);
}

uint32_t CollDmgSession::Alias (VesselDamageA &v, CollH h)
{
	if (v.alias != ~0u) return v.alias;
	v.alias = rec.nalias++;
	std::vector<CollLiveVessel> live;
	size_t me = 0;
	for (uint32_t i = 0, nv = sdk.VesselCount (); i < nv; i++) {
		CollH x = sdk.Vessel (i);
		if (x == h) me = live.size ();
		live.push_back (CollLiveVessel { sdk.Name (x), sdk.ClassName (x) });
	}
	Side (CollSide::Vdef (v.alias, CollStore::Occ (live, me, false), v.name, v.cls));
	return v.alias;
}

void CollDmgSession::Side (const std::string &line)
{
	if (!rec.active || rec.failed || !cfg.recorder) return;
	rec.file << line << '\n';
	rec.file.flush ();
	n.side++;
}

void CollDmgSession::Save (CollH scn)
{
	std::vector<std::string> l;
	SaveLines (l);
	for (const std::string &s : l) sdk.ScnWrite (scn, s);
}

void CollDmgSession::SaveLines (std::vector<std::string> &out)
{
	out.push_back ("COLLA " + std::to_string (COLL_STORE_VERSION));
	if (begun && cfg.recorder && !rec.active) {
		bool any = false;
		for (uint32_t i = 0, nv = sdk.VesselCount (); i < nv && !any; i++) any = sdk.Recording (sdk.Vessel (i));
		if (any) { // ToggleRecorder -> SavePlaybackScn -> here
			StartRecording ();
			if (!rec.failed) out.push_back ("RECID " + rec.id + " " + CollSide::Fmt17 (rec.t0)); // no side file: no link
		}
	}
	auto raw = [&] (const std::vector<std::string> &sec) {
		for (size_t i = 0; i < sec.size (); i++) {
			std::string s = (i == 0 || i + 1 == sec.size ()) ? sec[i] : "  " + sec[i];
			if (!CollStore::Line200 (s)) s = sec[i]; // as FormatVessel: a line that cannot take the indent goes without it
			if (CollStore::Line200 (s)) out.push_back (s);
		}
	};
	if (matched) {
		std::vector<CollLiveVessel> live;
		std::vector<CollH> hs;
		for (uint32_t i = 0, nv = sdk.VesselCount (); i < nv; i++) { hs.push_back (sdk.Vessel (i)); live.push_back (CollLiveVessel { sdk.Name (hs[i]), sdk.ClassName (hs[i]) }); }
		for (size_t i = 0; i < hs.size (); i++) {
			const VesselDamageA *v = Damage (host.IdOf (hs[i]));
			if (!v || (v->d.rec.empty () && v->d.eabs == 0 && v->d.flags == 0 && v->d.verbatim.empty ())) continue;
			DentVesselText d = v->d;
			for (size_t r = 0; r < d.rec.size (); r++) {
				CollDmgSlot s;
				uint32_t m = d.rec[r].slot;
				if (r >= v->match.size () || v->match[r] != (int)m) continue; // a waiting record keeps its loaded name
				if (m >= d.slotName.size () && m < 4096) d.slotName.resize (m + 1);
				if (m < d.slotName.size () && host.Slot (v->id, m, s) && s.present) d.slotName[m] = s.name;
			}
			out.push_back (CollKey::VesselLine ("VESSEL", CollStore::Occ (live, i, false), live[i].name, live[i].cls, 0));
			DentMath::FormatVessel (d, "  ", out);
			out.push_back ("END_VESSEL");
			Log ("Collision damage vessel '%s' eabs=%.17g records=%zu flags=0x%x", live[i].name.c_str (), v->d.eabs, v->d.rec.size (), v->d.flags);
		}
		for (const auto &s : dormantVessel) raw (s);
		std::vector<DentBaseText> bt;
		for (const auto &kv : building) {
			const BuildingDamageA &b = kv.second;
			if (b.eabs == 0 && b.flags == 0) continue;
			DentBaseText *t = nullptr;
			for (DentBaseText &x : bt) if (x.planet == b.obj.planet && x.name == b.obj.base) t = &x;
			if (!t) { bt.emplace_back (); t = &bt.back (); t->planet = b.obj.planet, t->name = b.obj.base; }
			t->obj.push_back (DentBaseObjText { b.obj.obj, b.obj.type, b.obj.x, b.obj.z, b.eabs, b.flags });
			Log ("Collision damage base '%s:%s' obj=%u eabs=%.17g flags=0x%x", b.obj.planet.c_str (), b.obj.base.c_str (), b.obj.obj, b.eabs, b.flags);
		}
		for (const DentBaseText &d : dormantBase) {
			DentBaseText *t = nullptr;
			for (DentBaseText &x : bt) if (CollStore::SameBase (d, x.planet, x.name) && d.nameHash == 0) t = &x;
			if (!t) bt.push_back (d);
			else { t->obj.insert (t->obj.end (), d.obj.begin (), d.obj.end ()); t->rec.insert (t->rec.end (), d.rec.begin (), d.rec.end ()); }
		}
		DentMath::FormatBases (bt, out);
	} else { // before the first match: the block as read
		for (const auto &s : blk.vessel) raw (s.raw);
		DentMath::FormatBases (blk.base, out);
	}
	for (const std::string &u : blk.unknown) out.push_back (u);
}

// playback (8.4)

void CollDmgSession::Playback (double simt)
{
	bool any = false;
	std::vector<CollLiveVessel> live;
	std::vector<CollH> hs;
	for (uint32_t i = 0, nv = sdk.VesselCount (); i < nv; i++) {
		hs.push_back (sdk.Vessel (i));
		bool p = sdk.Playback (hs.back ());
		any = any || p;
	}
	if (!any) {
		if (play.read) Log ("Collision playback: link %s ended, %zu events dropped", play.id.c_str (), play.f.ev.size () - play.cursor);
		play.active = false;
		return;
	}
	if (!play.read) {
		play.read = true;
		std::string text;
		if (!sdk.ReadText (sideDir + "/" + play.id + ".txt", text) || !CollSide::Parse (text, play.f)) {
			Log ("Collision playback: side file %s missing", play.id.c_str ());
			sdk.Notification (COLLN_WARNING, "Collision", "no damage record for this playback");
			play.active = false;
			return;
		}
		for (CollH h : hs) live.push_back (CollLiveVessel { sdk.Name (h), sdk.ClassName (h) });
		for (const CollSideAlias &a : play.f.alias) {
			CollStoreVessel sv;
			sv.occ = a.occ, sv.hashed = a.hashed, sv.name = a.name, sv.cls = a.cls, sv.hName = a.hName, sv.hClass = a.hClass;
			std::vector<int> m = CollStore::MatchVessels ({ sv }, live);
			if (m[0] >= 0) play.aliasId[a.alias] = host.IdOf (hs[m[0]]);
		}
		Log ("Collision playback: link %s events=%zu aliases=%zu", play.id.c_str (), play.f.ev.size (), play.f.alias.size ());
	}
	while (play.cursor < play.f.ev.size () && play.f.ev[play.cursor].t <= simt) {
		const CollSideEvent &e = play.f.ev[play.cursor++];
		auto it = play.aliasId.find (e.alias);
		if (it == play.aliasId.end ()) continue;
		CollH h = host.Vessel (it->second);
		if (!h || !sdk.Playback (h)) continue; // live in this session: skipped
		n.playback++;
		if (e.kind == 'B') {
			std::vector<CollDmgBaseObj> all;
			host.Bases (all);
			for (const CollDmgBaseObj &o : all)
				if (o.planet + ":" + o.base == e.base && o.obj == e.obj) {
					BuildingDamageA &b = building[{ { o.planetIdx, o.baseIdx }, o.obj }];
					b.obj = o, b.eabs = e.eabs, b.flags = e.flags;
				}
			continue;
		}
		VesselDamageA &v = Get (it->second);
		if (e.kind == 'R') { DoRepair (v, true); continue; }
		if (e.kind == 'S') {
			bool was = v.d.flags & XDMG_DESTROYED;
			v.d.eabs = e.eabs, v.d.flags = e.flags;
			if (!was && (v.d.flags & XDMG_DESTROYED)) { NoticeA b; b.simt = e.t; b.flags = COLLA_DMG_PLAYBACK; Queue (v.id, CDMG_KIND_DESTROYED, &b); }
			continue;
		}
		if (e.kind == 'T') { // dmg3: torn groups, applied by P
			v.d.torn.push_back (e.torn);
			if (brk) brk->Torn (v.id, e.torn);
			continue;
		}
		if (e.kind == 'X') { // dmg3: extension of the record its D event just stored
			bool firstX = !(play.xT == e.t && play.xA == e.alias); // dmg3 M3: old lines without the flag: the first X of the event is the hit
			play.xT = e.t, play.xA = e.alias;
			if (e.recidx >= v.d.rec.size () || DentMath::ParamsHash (v.d.rec[e.recidx].p) != e.h8) continue;
			DentRecord &r = v.d.rec[e.recidx];
			DentMath::ApplyExt (r.p, e.rec.p);
			int m = e.recidx < v.match.size () ? v.match[e.recidx] : -1;
			if (m >= 0) MarkDirty (v.id, (uint32_t)m, true);
			CollDamageHit hit;
			hit.id = v.id, hit.h = h, hit.mesh = r.slot, hit.rec = (int)e.recidx, hit.c = r.p.c, hit.n = r.p.n;
			CollShape *psh = host.Shape (v.id);
			int pj = psh && !r.grp.empty () ? psh->PartOf (r.slot, r.grp[0]) : -1;
			if (pj >= 0) { const CollPart &Q = psh->Part ((uint32_t)pj); hit.c = CollApply (Q.pose[1], r.p.c), hit.n = Unit (CollApplyDir (Q.pose[1], r.p.n)); hit.grp = r.grp[0]; }
			double em = sdk.EmptyMass (h);
			const DentMaterial &pm = DentMath::DefaultMaterial (-1);
			double placedP = r.p.mode == DENTM_HINGE ? 2.0 * DentMath::HingeMp (r.p, pm) * r.p.P : pm.sigma_c * r.p.h * Pi * r.p.R * r.p.R / 3.0; // stored geometry, disk area
			hit.E = e.E, hit.eSpec = em > 0 ? e.E / em : 0.0, hit.R = r.p.R, hit.depth = r.p.h, hit.mode = r.p.mode, hit.vn = e.vn, hit.vt = e.vt;
			hit.Esurplus = std::min (e.E, std::max (0.0, e.E - placedP));
			hit.simt = e.t, hit.playback = true, hit.mat = &pm, hit.evflags = e.evflags;
			if (e.xhit == 1 || (e.xhit < 0 && firstX)) EmitHit (hit);
			continue;
		}
		if (e.kind != 'D') continue;
		if (e.recidx >= DENT_MAX_VESSEL) { // the live cap (R7)
			if (!v.loggedCap) { v.loggedCap = true; Log ("Collision playback: '%s' dent %u beyond %u records, skipped", v.name.c_str (), (unsigned)e.recidx, DENT_MAX_VESSEL); }
			continue;
		}
		if (e.recidx > v.d.rec.size ()) { // a gap: the saved section was dormant in this session
			if (!play.warned) { play.warned = true; Log ("Collision playback: '%s' dent %u has no earlier records, skipped", v.name.c_str (), (unsigned)e.recidx); }
			continue;
		}
		DentRecord r = e.rec;
		CollDmgSlot s;
		int m = -1;
		if (e.recidx < v.d.rec.size ()) r.key = v.d.rec[e.recidx].key, r.ngrp = v.d.rec[e.recidx].ngrp, r.nvtx = v.d.rec[e.recidx].nvtx;
		else if (host.Slot (v.id, r.slot, s) && s.present) r.key = s.key, r.ngrp = s.ngrp, r.nvtx = s.nvtx; // the payload holds no signature: the recorded slot
		if (r.slot < host.SlotCount (v.id) && host.Slot (v.id, r.slot, s) && SameSig (r, s)) m = (int)r.slot;
		int old = e.recidx < v.match.size () ? v.match[e.recidx] : -1;
		bool replace = e.recidx < v.d.rec.size ();
		if (!replace) v.d.rec.push_back (r), v.match.push_back (m);
		else v.d.rec[e.recidx] = r, v.match[e.recidx] = m;
		if (old >= 0 && old != m) MarkDirty (v.id, (uint32_t)old, false); // the old mesh loses the record
		if (m >= 0) MarkDirty (v.id, (uint32_t)m, replace);
		NoticeA &nt = frameNote[v.id];
		nt.id = v.id, nt.kind = CDMG_KIND_DENT, nt.flags = COLLA_DMG_PLAYBACK, nt.simt = e.t;
		if (r.p.h >= nt.depth) nt.depth = r.p.h, nt.mesh = (int32_t)r.slot, nt.pos = r.p.c, nt.nml = r.p.n;
	}
	FlushDirty ();
	for (auto &kv : frameNote) notices.push_back (kv.second);
	frameNote.clear ();
}

// dialog content and the 2024 report (9.3)

void CollDmgSession::Report (std::vector<std::string> &lines) const
{
	char b[512];
	std::snprintf (b, sizeof b, "Collision damage: model=%d thrustcut=%d visuals=%d notify=%d destroy=%.6g J/kg building=%.6g J/kg", cfg.model, (int)cfg.thrustCut, (int)cfg.visuals,
		cfg.notify, cfg.destroyEnergy, cfg.buildingDestroyEnergy);
	lines.push_back (b);
	for (const auto &kv : vessel) {
		const VesselDamageA &v = kv.second;
		std::snprintf (b, sizeof b, "  vessel '%s' records=%zu eabs=%.6g destroyed=%d modulefx=%d cut=%d relinks=%u remakes=%u modulegroups=%u", v.name.c_str (), v.d.rec.size (), v.d.eabs,
			(v.d.flags & XDMG_DESTROYED) ? 1 : 0, (v.d.flags & XDMG_MODULEFX) ? 1 : 0, v.cut.dummy ? 1 : 0, v.cut.relinks, v.cut.remakes, vis.ModuleGroups (kv.first));
		lines.push_back (b);
	}
	for (const auto &kv : building) {
		const BuildingDamageA &d = kv.second;
		std::snprintf (b, sizeof b, "  building '%s:%s' %s #%u eabs=%.6g destroyed=%d", d.obj.planet.c_str (), d.obj.base.c_str (), d.obj.type.c_str (), d.obj.obj, d.eabs, (d.flags & XDMG_DESTROYED) ? 1 : 0);
		lines.push_back (b);
	}
	std::snprintf (b, sizeof b, "  dormant vessels=%zu bases=%zu; recorder %s%s; playback %s%s; repairs queued=%zu", dormantVessel.size (), dormantBase.size (), rec.active ? "on " : "off",
		rec.active ? rec.id.c_str () : "", play.active ? "on " : "off", play.active ? play.id.c_str () : "", repairs.size ());
	lines.push_back (b);
}

const char *CollUiA::Label () { return "Collision damage"; }

const char *CollUiA::Description () { return "Collision damage: vessels, buildings, repair"; }

void CollUiA::OnCommand (CollSdk &proc, CollDmgSession *s, void *dialog)
{
	if (!s) return;
	if (proc.imgui && dialog) { proc.OpenDialog (dialog); return; }
	std::vector<std::string> l;
	s->Report (l);
	for (const std::string &x : l) s->Sdk ().Log (1, x.c_str ());
	char b[160];
	std::snprintf (b, sizeof b, "Collision: %zu damaged vessels, %zu damaged buildings (report in Orbiter.log)", s->Vessels ().size (), s->Buildings ().size ());
	s->Sdk ().Annotation (b, 8.0);
}
