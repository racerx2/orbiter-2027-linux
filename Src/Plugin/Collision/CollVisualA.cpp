// not upstream: collision addon, E3 client mirrors and the visual sync rule: absolute writes, module-owned groups, confirmed rebuild, sentinels (Design CA E3 3)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "CollVisualA.h"

namespace {

enum { ST_NONE, ST_HOLDS, ST_REST, ST_FRESH, ST_FOREIGN };

bool Same (const DentVtx &a, const DentVtx &b) { return std::memcmp (&a, &b, 24) == 0; } // six floats bitwise; tu, tv are never written

bool IsRest (const DentVtx &c, const DentVtx &r)
{
	return std::memcmp (&c, &r, 12) == 0 && std::fabs (c.nx - r.nx) <= 1e-6f && std::fabs (c.ny - r.ny) <= 1e-6f && std::fabs (c.nz - r.nz) <= 1e-6f;
}

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
	auto it = v.copy.find (slot);
	if (it != v.copy.end () && (it->second.serial != s.serial || it->second.rest != s.rest)) { v.copy.erase (it); it = v.copy.end (); }
	if (it == v.copy.end ()) {
		if (rec.empty ()) return;
		DentMeshCopyA c;
		c.rest = s.rest, c.serial = s.serial, c.slot = slot, c.key = s.key, c.ngrp = s.ngrp, c.nvtx = s.nvtx;
		size_t ng = s.rest->grp.size ();
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
			c.g[g].pushed = c.rp[g];
		}
		c.nweld = DentMath::WeldMap (c.rp, DENT_WELD, c.weld);
		DentMath::FaceNormalSums (c.rp, c.idx, c.weld, c.nweld, c.restSum);
		it = v.copy.emplace (slot, std::move (c)).first;
	}
	DentMeshCopyA &c = it->second;
	c.cur = c.rp;
	for (const DentRecord *r : rec) DentMath::Apply (r->p, c.rp, c.cur, r->grp.data (), r->grp.size (), nullptr);
	std::vector<uint8_t> touched (c.nweld, 0);
	for (size_t g = 0; g < c.cur.size (); g++)
		for (size_t i = 0; i < c.cur[g].size (); i++)
			if (std::memcmp (&c.cur[g][i], &c.rp[g][i], 12) && c.weld[g][i] < c.nweld) touched[c.weld[g][i]] = 1;
	DentMath::Normals (c.rp, c.restSum, c.idx, c.weld, c.nweld, touched, c.cur);
	for (size_t g = 0; g < c.cur.size (); g++) {
		c.g[g].edit.clear ();
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
				if (c.g[g].module) continue;
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
			if (G.module) continue;
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
			for (size_t k = 0; k < rd.size (); k++) holds = holds && Same (buf[k], G.pushed[rd[k]]);
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
				bool H = Same (buf[k], G.pushed[u]), R = IsRest (buf[k], c.rp[g][u]);
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
		if (G.module) continue;
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
