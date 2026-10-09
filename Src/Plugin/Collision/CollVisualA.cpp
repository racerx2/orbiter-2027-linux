// not upstream: collision addon, E3 client mirrors and the visual sync rule: absolute writes, module-owned groups, confirmed rebuild, sentinels (Design CA E3 3)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "CollShape.h"
#include "CollVisualA.h"

namespace {

enum { ST_NONE, ST_HOLDS, ST_REST, ST_FRESH, ST_FOREIGN };

bool Same (const DentVtx &a, const DentVtx &b) { return std::memcmp (&a, &b, 24) == 0; } // six floats bitwise; tu, tv are never written

bool NoNormal (const DentVtx &v) { return v.nx == 0.0f && v.ny == 0.0f && v.nz == 0.0f; }

bool NaNNormal (const DentVtx &v) { return std::isnan (v.nx) && std::isnan (v.ny) && std::isnan (v.nz); } // the client's renormalisation of a zero normal

bool Holds (const DentVtx &c, const DentVtx &w) { return Same (c, w) || (std::memcmp (&c, &w, 12) == 0 && NoNormal (w) && NaNNormal (c)); } // client value c is what we hold as w

bool IsRest (const DentVtx &c, const DentVtx &r)
{
	if (std::memcmp (&c, &r, 12)) return false;
	if (NoNormal (r) && NaNNormal (c)) return true;
	return std::fabs (c.nx - r.nx) <= 1e-6f && std::fabs (c.ny - r.ny) <= 1e-6f && std::fabs (c.nz - r.nz) <= 1e-6f;
}

bool SameAffine (const CollAffine &X, const CollAffine &Y)
{
	const double a[12] = { X.A.m11 - Y.A.m11, X.A.m12 - Y.A.m12, X.A.m13 - Y.A.m13, X.A.m21 - Y.A.m21, X.A.m22 - Y.A.m22, X.A.m23 - Y.A.m23,
		X.A.m31 - Y.A.m31, X.A.m32 - Y.A.m32, X.A.m33 - Y.A.m33, X.t.x - Y.t.x, X.t.y - Y.t.y, X.t.z - Y.t.z };
	for (double d : a) if (!(std::fabs (d) < 1e-9)) return false;
	return true;
}

// pose class per group from the collider's getters: its part, else the part or earlier group with the same animation transform (dent2 D7)
std::vector<uint32_t> PoseClasses (const CollShape *sh, uint32_t mesh, size_t ng)
{
	std::vector<uint32_t> cls (ng, 0);
	if (!sh) return cls;
	bool cm = sh->CollMesh (mesh);
	uint32_t np = sh->nPart ();
	std::vector<std::pair<uint32_t, CollAffine>> partF, looseF; // class id, transform
	std::vector<CollAffine> F (ng);
	std::vector<int> part (ng, -1);
	for (size_t g = 0; g < ng; g++) {
		sh->GroupPose (mesh, (uint32_t)g, F[g]);
		part[g] = cm ? -1 : sh->PartOf (mesh, (uint32_t)g);
		if (part[g] < 0) continue;
		cls[g] = (uint32_t)part[g];
		bool seen = false;
		for (auto &x : partF) seen = seen || x.first == cls[g];
		if (!seen) partF.push_back ({ cls[g], F[g] });
	}
	for (size_t g = 0; g < ng; g++) {
		if (part[g] >= 0) continue;
		size_t k = 0;
		while (k < partF.size () && !SameAffine (partF[k].second, F[g])) k++;
		if (k < partF.size ()) { cls[g] = partF[k].first; continue; }
		k = 0;
		while (k < looseF.size () && !SameAffine (looseF[k].second, F[g])) k++;
		if (k == looseF.size ()) looseF.push_back ({ np + (uint32_t)k, F[g] });
		cls[g] = looseF[k].first;
	}
	return cls;
}

bool SameRecord (const DentRecord &a, const DentRecord &b) { return std::memcmp (&a.p, &b.p, sizeof (DentParams)) == 0 && a.grp == b.grp; }

void NormF (float &x, float &y, float &z) // the client's float renormalisation (OVP/VulkanClient/Mesh.cpp:722-726)
{
	float l2 = y * y + z * z + x * x;
	if (!(l2 > 0.0f)) return;
	float b = 1.0f / std::sqrt (l2);
	x = x * b, y = y * b, z = z * b;
}

std::vector<uint16_t> Sentinels (const std::vector<uint16_t> &sent, const std::vector<DentVtx> &rp, const std::vector<DentVtx> &pushed)
{
	std::vector<uint16_t> s;
	if (sent.empty ()) return s;
	uint16_t big = sent[0];
	double bd = -1;
	for (uint16_t u : sent) {
		double dx = pushed[u].x - rp[u].x, dy = pushed[u].y - rp[u].y, dz = pushed[u].z - rp[u].z, d = dx * dx + dy * dy + dz * dz;
		if (d > bd) bd = d, big = u;
	}
	for (uint16_t u : { big, sent.front (), sent[sent.size () / 2], sent.back () })
		if (std::find (s.begin (), s.end (), u) == s.end ()) s.push_back (u);
	return s;
}

}

void CollVisualA::SetRecords (uint32_t id, const std::string &name, const CollDmgSlot &s, uint32_t slot, const std::vector<const DentRecord *> &rec)
{
	if (!cfg.visuals || mode == MODE_OFF || !s.rest) return;
	auto vi = ves.find (id);
	if (rec.empty () && (vi == ves.end () || !vi->second.copy.count (slot))) return;
	Ves &v = ves[id];
	v.name = name;
	size_t ng = s.rest->grp.size ();
	std::vector<uint32_t> cls = PoseClasses (host.Shape (id), slot, ng);
	auto it = v.copy.find (slot);
	std::vector<std::vector<DentVtx>> keep; // what the client holds when only the version changed (same rest, same visual)
	if (it != v.copy.end () && (it->second.serial != s.serial || it->second.rest != s.rest)) {
		if (it->second.rest == s.rest) for (auto &G : it->second.g) keep.push_back (std::move (G.pushed));
		v.copy.erase (it);
		it = v.copy.end ();
	}
	if (it == v.copy.end ()) {
		if (rec.empty ()) return;
		DentMeshCopyA c;
		c.rest = s.rest, c.serial = s.serial, c.slot = slot, c.key = s.key, c.ngrp = s.ngrp, c.nvtx = s.nvtx;
		c.rp.resize (ng), c.idx.resize (ng), c.g.resize (ng);
		for (size_t g = 0; g < ng; g++) {
			const CollGroupData &gd = s.rest->grp[g];
			c.rp[g].resize (gd.vtx.size ());
			for (size_t i = 0; i < gd.vtx.size (); i++) {
				DentVtx &d = c.rp[g][i];
				std::memcpy (&d, &gd.vtx[i], sizeof d);
				NormF (d.nx, d.ny, d.nz);
			}
			c.idx[g] = gd.idx;
			c.g[g].pushed = g < keep.size () && keep[g].size () == c.rp[g].size () ? std::move (keep[g]) : c.rp[g];
			c.g[g].big = gd.vtx.size () > 65536;
			if (c.g[g].big && !loggedBig) {
				loggedBig = true;
				char b[256];
				std::snprintf (b, sizeof b, "Collision visual: '%s' mesh=%u grp=%zu has %zu vertices (over 65536), dent not shown", name.c_str (), slot, g, gd.vtx.size ());
				sdk.Log (1, b);
			}
		}
		it = v.copy.emplace (slot, std::move (c)).first;
	}
	DentMeshCopyA &c = it->second;
	if (c.cls != cls || c.weld.size () != ng) { // new copy or new pose classes: weld map and rest sums again, cur from rest
		c.cls = cls;
		c.nweld = DentMath::WeldMap (c.rp, DENT_WELD, c.weld, &c.cls);
		DentMath::FaceNormalSums (c.rp, c.idx, c.weld, c.nweld, c.restSum);
		c.done.clear ();
	}
	size_t k = c.done.size ();
	bool inc = k > 0 && k <= rec.size ();
	for (size_t i = 0; inc && i < k; i++) inc = SameRecord (c.done[i], *rec[i]);
	if (inc && k == rec.size ()) { v.pending = true; return; } // nothing new
	if (inc) { // only new records: positions as a full build gives them (same order), normals from rest again
		for (size_t g = 0; g < ng; g++)
			for (size_t i = 0; i < c.cur[g].size (); i++) c.cur[g][i].nx = c.rp[g][i].nx, c.cur[g][i].ny = c.rp[g][i].ny, c.cur[g][i].nz = c.rp[g][i].nz;
		n.incr++;
	} else {
		c.cur = c.rp;
		c.done.clear ();
		k = 0;
		n.builds++;
	}
	bool anyBig = false;
	for (const auto &G : c.g) anyBig = anyBig || G.big;
	for (size_t r = k; r < rec.size (); r++) {
		const DentRecord &R = *rec[r];
		if (!anyBig) DentMath::Apply (R.p, c.rp, c.cur, R.grp.data (), R.grp.size (), nullptr);
		else { // big groups left out
			std::vector<uint16_t> gl;
			if (R.grp.empty ()) { for (size_t g = 0; g < ng; g++) if (!c.g[g].big) gl.push_back ((uint16_t)g); }
			else for (uint16_t g : R.grp) if (g < ng && !c.g[g].big) gl.push_back (g);
			if (!gl.empty ()) DentMath::Apply (R.p, c.rp, c.cur, gl.data (), gl.size (), nullptr);
		}
		c.done.push_back (R);
	}
	std::vector<uint8_t> touched (c.nweld, 0);
	for (size_t g = 0; g < c.cur.size (); g++)
		for (size_t i = 0; i < c.cur[g].size (); i++)
			if (std::memcmp (&c.cur[g][i], &c.rp[g][i], 12) && c.weld[g][i] < c.nweld) touched[c.weld[g][i]] = 1;
	std::vector<uint8_t> facet; // dmg3: weld ids moved by a crush or hinge record get the crease rule
	if (cfg.dentFacetNormals)
		for (size_t r = 0; r < rec.size (); r++) {
			const DentRecord &R = *rec[r];
			if (R.p.mode < DENTM_CRUSH) continue;
			if (facet.empty ()) facet.assign (c.nweld, 0);
			for (size_t g = 0; g < ng && g < c.rp.size (); g++) {
				if (!R.grp.empty () && !std::binary_search (R.grp.begin (), R.grp.end (), (uint16_t)g)) continue;
				for (size_t i = 0; i < c.rp[g].size () && i < c.weld[g].size (); i++) {
					if (c.weld[g][i] >= c.nweld) continue;
					Vector d = DentMath::Displace (R.p, Vector (c.rp[g][i].x, c.rp[g][i].y, c.rp[g][i].z));
					if (d.x != 0.0 || d.y != 0.0 || d.z != 0.0) facet[c.weld[g][i]] = 1;
				}
			}
		}
	DentMath::Normals (c.rp, c.restSum, c.idx, c.weld, c.nweld, touched, c.cur, facet.empty () ? nullptr : &facet);
	for (size_t g = 0; g < c.cur.size (); g++) {
		c.g[g].edit.clear ();
		if (c.g[g].big) { c.cur[g] = c.rp[g]; continue; } // a welded neighbour may have turned its normals
		for (size_t i = 0; i < c.cur[g].size (); i++) {
			DentVtx &d = c.cur[g][i];
			if (!Same (d, c.rp[g][i])) {
				if (std::memcmp (&d.nx, &c.rp[g][i].nx, 12)) NormF (d.nx, d.ny, d.nz);
				c.g[g].edit.push_back ((uint16_t)i);
			}
		}
	}
	c.norec = rec.empty ();
	v.pending = true;
}

void CollVisualA::DropSlot (uint32_t id, uint32_t slot)
{
	auto it = ves.find (id);
	if (it != ves.end ()) it->second.copy.erase (slot);
}

void CollVisualA::DropVessel (uint32_t id) { ves.erase (id); }

void CollVisualA::Rebuilt (uint32_t id, uint32_t slot, uint32_t serial, bool gccore)
{
	auto it = ves.find (id);
	if (it == ves.end ()) return;
	auto c = it->second.copy.find (slot);
	if (c == it->second.copy.end ()) return;
	c->second.serial = serial;
	if (gccore)
		for (size_t g = 0; g < c->second.g.size (); g++) if (!c->second.g[g].module) c->second.g[g].pushed = c->second.rp[g];
	it->second.pending = true;
}

const DentMeshCopyA *CollVisualA::Copy (uint32_t id, uint32_t slot) const
{
	auto it = ves.find (id);
	if (it == ves.end ()) return nullptr;
	auto c = it->second.copy.find (slot);
	return c == it->second.copy.end () ? nullptr : &c->second;
}

uint32_t CollVisualA::ModuleGroups (uint32_t id) const
{
	uint32_t k = 0;
	auto it = ves.find (id);
	if (it != ves.end ()) for (const auto &c : it->second.copy) for (const auto &g : c.second.g) k += g.module ? 1 : 0;
	return k;
}

void CollVisualA::Pass (int which)
{
	if (!cfg.visuals || mode == MODE_OFF) return;
	for (auto &kv : ves) {
		Ves &v = kv.second;
		if (v.copy.empty ()) continue;
		CollH h = host.Vessel (kv.first);
		if (!h) continue;
		CollH vis = sdk.Visual (h);
		if (!vis) continue;                                // nothing; copies and module flags kept
		if (vis != v.lastVis) {                            // new visual: read before any write
			for (auto &c : v.copy) for (size_t g = 0; g < c.second.g.size (); g++) if (!c.second.g[g].module) c.second.g[g].pushed = c.second.rp[g];
			v.lastVis = vis;
			v.pending = true;
		}
		if (which == PASS_PENDING && !v.pending) continue;
		bool left = false;
		for (auto it = v.copy.begin (); it != v.copy.end ();) {
			DentMeshCopyA &c = it->second;
			if (!SyncCopy (kv.first, h, vis, v, c)) return;
			bool sent = false, dirty = false;
			for (size_t g = 0; g < c.g.size (); g++) {
				if (c.g[g].module || c.g[g].big) continue;
				for (size_t i = 0; i < c.rp[g].size () && !(sent && dirty); i++) {
					sent = sent || !Same (c.g[g].pushed[i], c.rp[g][i]);
					dirty = dirty || !Same (c.cur[g][i], c.g[g].pushed[i]);
				}
			}
			left = left || dirty;
			if (c.norec && !sent && !dirty) it = v.copy.erase (it); // repaired and restored in the client
			else ++it;
		}
		v.pending = left;
	}
}

bool CollVisualA::SyncCopy (uint32_t id, CollH h, CollH vis, Ves &v, DentMeshCopyA &c)
{
	uint32_t serial = 0;
	if (!host.SlotNow (id, c.slot, serial) || serial != c.serial) return true; // E2's next poll handles the change
	if (c.nullWait) { c.nullWait--; return true; }
	CollH dm = sdk.DevMesh (h, vis, c.slot);
	if (!dm) {
		c.nullWait = 63;
		if (!c.loggedNull) {
			c.loggedNull = true;
			char b[256];
			std::snprintf (b, sizeof b, "Collision visual: '%s' mesh=%u has no client mesh, retried every 64 passes", v.name.c_str (), c.slot);
			sdk.Log (1, b);
		}
		return true;
	}
	size_t ng = c.g.size ();
	std::vector<int> state (ng, ST_NONE);
	std::vector<uint8_t> evidence (ng, 0);
	auto skip = [&] (size_t g) {
		c.g[g].skips++;
		if (++c.skips == 600) {
			char b[256];
			std::snprintf (b, sizeof b, "Collision visual: '%s' mesh=%u skipped 600 passes (client mesh differs)", v.name.c_str (), c.slot);
			sdk.Log (1, b);
		}
		return true;
	};
	if (mode == MODE_FULL) {
		for (size_t g = 0; g < ng; g++) {
			DentMeshCopyA::Grp &G = c.g[g];
			if (G.module || G.big) continue;
			std::vector<uint16_t> sent, dirty, all;
			for (size_t i = 0; i < c.rp[g].size (); i++) {
				bool s = !Same (G.pushed[i], c.rp[g][i]), d = !Same (c.cur[g][i], G.pushed[i]);
				if (s) sent.push_back ((uint16_t)i);
				if (d) dirty.push_back ((uint16_t)i);
				if (s || d) all.push_back ((uint16_t)i);
			}
			if (all.empty ()) continue;
			evidence[g] = !sent.empty ();
			std::vector<uint16_t> rd = dirty.empty () ? Sentinels (sent, c.rp[g], G.pushed) : all;
			std::vector<DentVtx> buf (rd.size ());
			int r = sdk.ReadVtx (dm, (uint32_t)g, rd.data (), (uint32_t)rd.size (), buf.data ());
			n.reads++;
			if (r == 1) return skip (g);
			if (r == -1) return false;
			if (r == -2) { mode = MODE_ABSOLUTE; sdk.Log (1, "Collision visual: client cannot read mesh groups, absolute mode"); break; }
			bool holds = true;
			for (size_t k = 0; k < rd.size (); k++) holds = holds && Holds (buf[k], G.pushed[rd[k]]);
			if (dirty.empty () && !holds && rd.size () < all.size ()) { // a sentinel moved: read every sent vertex
				rd = all;
				buf.assign (rd.size (), DentVtx ());
				r = sdk.ReadVtx (dm, (uint32_t)g, rd.data (), (uint32_t)rd.size (), buf.data ());
				n.reads++;
				if (r == 1) return skip (g);
				if (r < 0) return false;
			}
			bool allHolds = true, sentRest = true, otherOk = true, holdOrRest = true;
			for (size_t k = 0; k < rd.size (); k++) {
				uint16_t u = rd[k];
				bool H = Holds (buf[k], G.pushed[u]), R = IsRest (buf[k], c.rp[g][u]);
				bool inSent = !Same (G.pushed[u], c.rp[g][u]);
				allHolds = allHolds && H;
				holdOrRest = holdOrRest && (H || R);
				if (inSent) sentRest = sentRest && R;
				else otherOk = otherOk && (H || R);
			}
			if (allHolds) state[g] = ST_HOLDS;
			else if (!sent.empty () && sentRest && otherOk) state[g] = ST_REST;
			else if (sent.empty () && holdOrRest) state[g] = ST_FRESH;
			else state[g] = ST_FOREIGN;
		}
	}
	if (mode == MODE_FULL) {
		bool anyHolds = false, anyRest = false;
		for (size_t g = 0; g < ng; g++) if (evidence[g]) { anyHolds = anyHolds || state[g] == ST_HOLDS; anyRest = anyRest || state[g] == ST_REST; }
		if (!anyHolds && anyRest) { host.ClientMeshRebuilt (id, c.slot); n.rebuilds++; }
		for (size_t g = 0; g < ng; g++) {
			if (state[g] == ST_REST) c.g[g].pushed = c.rp[g];
			else if (state[g] == ST_FOREIGN) {
				c.g[g].module = true;
				n.module++;
				char b[256];
				std::snprintf (b, sizeof b, "Collision visual: '%s' mesh=%u grp=%zu written by the module, dent not shown", v.name.c_str (), c.slot, g);
				sdk.Log (1, b);
			}
		}
	}
	c.skips = 0;
	for (size_t g = 0; g < ng; g++) {
		DentMeshCopyA::Grp &G = c.g[g];
		if (G.module || G.big) continue;
		std::vector<uint16_t> dirty;
		std::vector<DentVtx> val;
		for (size_t i = 0; i < c.rp[g].size (); i++) if (!Same (c.cur[g][i], G.pushed[i])) { dirty.push_back ((uint16_t)i); val.push_back (c.cur[g][i]); }
		if (dirty.empty ()) continue;
		int r = sdk.WriteVtx (h, dm, (uint32_t)g, dirty.data (), (uint32_t)dirty.size (), val.data ());
		if (r == 1) return skip (g);
		if (r == -1) return false;
		if (r == -2) {
			mode = MODE_OFF;
			if (!warned) { warned = true; sdk.Notification (COLLN_WARNING, "Collision", "client cannot edit meshes, dents not shown"); }
			return false;
		}
		for (uint16_t u : dirty) G.pushed[u] = c.cur[g][u];
		n.writes++;
		n.pushes += dirty.size ();
		if (cfg.cullFix && sdk.ClientCore ()) { // group sphere refresh (3.10)
			float m[16];
			if (sdk.ClientMatrix (COLL_GC_MATRIX_GROUP, h, c.slot, (uint32_t)g, m) == 0) { sdk.SetClientMatrix (COLL_GC_MATRIX_GROUP, h, c.slot, (uint32_t)g, m); n.refresh++; }
		}
	}
	return true;
}
