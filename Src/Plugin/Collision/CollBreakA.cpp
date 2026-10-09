// not upstream: collision addon, dmg3 area P: parts tear off, glass breaks, interiors hidden, debris vessels (design-CA-dmg3-P)
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include "CollBreakA.h"
#include "CollDamageA.h"
#include "CollShape.h"
#include "CollStore.h"

namespace {

typedef std::vector<std::pair<std::vector<DentVtx>, std::vector<uint16_t>>> Geo;

struct Uf {
	std::vector<size_t> p;
	explicit Uf (size_t n) : p (n) { for (size_t i = 0; i < n; i++) p[i] = i; }
	size_t F (size_t x) { while (p[x] != x) x = p[x] = p[p[x]]; return x; }
	void U (size_t a, size_t b) { a = F (a), b = F (b); if (a != b) p[std::max (a, b)] = std::min (a, b); }
};

Matrix QMat (const double q[4])
{
	double x = q[0], y = q[1], z = q[2], w = q[3];
	return Matrix (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
		2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
		2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y));
}

void MatQ (const Matrix &m, double q[4]) // Shepperd; scale ignored
{
	double tr = m.m11 + m.m22 + m.m33, S;
	if (tr > 0) { S = std::sqrt (tr + 1) * 2; q[3] = 0.25 * S; q[0] = (m.m32 - m.m23) / S; q[1] = (m.m13 - m.m31) / S; q[2] = (m.m21 - m.m12) / S; }
	else if (m.m11 > m.m22 && m.m11 > m.m33) { S = std::sqrt (1 + m.m11 - m.m22 - m.m33) * 2; q[3] = (m.m32 - m.m23) / S; q[0] = 0.25 * S; q[1] = (m.m12 + m.m21) / S; q[2] = (m.m13 + m.m31) / S; }
	else if (m.m22 > m.m33) { S = std::sqrt (1 + m.m22 - m.m11 - m.m33) * 2; q[3] = (m.m13 - m.m31) / S; q[0] = (m.m12 + m.m21) / S; q[1] = 0.25 * S; q[2] = (m.m23 + m.m32) / S; }
	else { S = std::sqrt (1 + m.m33 - m.m11 - m.m22) * 2; q[3] = (m.m21 - m.m12) / S; q[0] = (m.m13 + m.m31) / S; q[1] = (m.m23 + m.m32) / S; q[2] = 0.25 * S; }
	double l = std::sqrt (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (!(l > 0)) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
	for (int i = 0; i < 4; i++) q[i] /= l;
}

Vector P (const DentVtx &v) { return Vector (v.x, v.y, v.z); }

bool Lists (const DentRecord &r, uint16_t g) { return r.grp.empty () || std::find (r.grp.begin (), r.grp.end (), g) != r.grp.end (); }

Vector Disp (const std::vector<const DentRecord *> &rec, uint16_t g, const Vector &x)
{
	Vector d;
	for (const DentRecord *r : rec) if (Lists (*r, g)) d += DentMath::Field (r, x);
	return d;
}

Vector Unit (const Vector &v) { double l = v.length (); return l > 0 ? v / l : v; }

uint32_t Hash (uint32_t a, uint32_t b) { uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2)); h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; return h; }

void ParseKeep (const std::string &text, std::set<uint16_t> &keep) // ";@KEEPFLAGS 0-27 111" lines
{
	size_t p = 0;
	while (p < text.size ()) {
		size_t e = text.find ('\n', p);
		std::string l = text.substr (p, e == std::string::npos ? std::string::npos : e - p);
		p = e == std::string::npos ? text.size () : e + 1;
		if (l.compare (0, 11, ";@KEEPFLAGS") != 0) continue;
		const char *c = l.c_str () + 11;
		while (*c) {
			char *end; long a = std::strtol (c, &end, 10);
			if (end == c) { c++; continue; }
			long b = a; c = end;
			if (*c == '-') { b = std::strtol (c + 1, &end, 10); if (end == c + 1) b = a; c = end; }
			for (long g = std::max (0L, a); g <= b && g < 65536; g++) keep.insert ((uint16_t)g);
		}
	}
}

}

CollBreakA::~CollBreakA () {}

void CollBreakA::Log (const char *fmt, ...)
{
	char b[512];
	va_list a;
	va_start (a, fmt);
	std::vsnprintf (b, sizeof b, fmt, a);
	va_end (a);
	sdk.Log (1, b);
}

std::vector<DentVtx> CollBreakA::PieceVertices (const std::vector<DentVtx> &rest, uint16_t g, const DentDebrisPose &p, const std::vector<DentRecord> &rec)
{
	std::vector<const DentRecord *> rp;
	for (auto &r : rec) rp.push_back (&r);
	Matrix A = QMat (p.q);
	std::vector<DentVtx> out = rest;
	for (size_t i = 0; i < rest.size (); i++) {
		Vector x = P (rest[i]);
		Vector y = mul (A, x + Disp (rp, g, x)) + p.p, n = mul (A, Vector (rest[i].nx, rest[i].ny, rest[i].nz));
		out[i].x = (float)y.x, out[i].y = (float)y.y, out[i].z = (float)y.z;
		out[i].nx = (float)n.x, out[i].ny = (float)n.y, out[i].nz = (float)n.z;
	}
	return out;
}

// pieces of one slot (1): groups of one pose class and tier, welded and connected

const CollSlotA *CollBreakA::Slot (uint32_t id, uint32_t mesh)
{
	auto key = std::make_pair (id, mesh);
	auto it = slots.find (key);
	if (it != slots.end ()) return &it->second;
	CollSlotA &sl = slots[key];
	CollH vh = s.VesselHandle (id);
	if (!vh || mesh >= sdk.MeshCount (vh)) return &sl;
	CollH tpl = sdk.MeshTemplate (vh, mesh), own = nullptr;
	if (!tpl && !sdk.ProbeSlot (vh, mesh)) return &sl;
	const char *tn = tpl ? sdk.TplName (tpl) : nullptr;
	const char *mn = tn ? tn : sdk.MeshName (vh, mesh);
	sl.name = mn ? mn : "";
	if (!tpl) { if (sl.name.empty ()) return &sl; tpl = own = sdk.MeshLoad (sl.name.c_str ()); }
	if (!tpl) return &sl;
	sl.tpl = tpl;
	uint32_t ng = std::min<uint32_t> (sdk.TplGroups (tpl), 65535);
	sl.v.resize (ng), sl.idx.resize (ng), sl.usr.assign (ng, 0);
	for (uint32_t g = 0; g < ng; g++) {
		CollTplGroup t;
		if (!sdk.TplGroup (tpl, g, t)) continue;
		sl.v[g].resize (t.nvtx);
		if (t.nvtx) std::memcpy (sl.v[g].data (), t.vtx, sizeof (DentVtx) * t.nvtx);
		sl.idx[g].assign (t.idx, t.idx + t.nidx);
		sl.usr[g] = t.usrflag;
		sl.nvtx += t.nvtx;
	}
	if (own) sdk.MeshFree (own);
	sl.ngrp = (uint16_t)ng; sl.key = DentMath::MeshKey (sl.name.c_str ()); sl.ok = true;
	std::string side;
	if (!sl.name.empty () && sdk.ReadText (sdk.Resolve ("Meshes/" + sl.name + ".col"), side)) ParseKeep (side, sl.keep);
	// pose class and tier per group
	CollShape *sh = ves[id].sh;
	std::vector<int> tier (ng, CBRK_PART); std::vector<uint8_t> gear (ng, 0);
	std::vector<uint32_t> cls (ng, 0);
	std::map<std::string, uint32_t> clsOf;
	for (uint32_t g = 0; g < ng; g++) {
		std::string k;
		if (sh) {
			int part = sh->PartOf (mesh, g);
			if (part < 0) {
				tier[g] = CBRK_INTERIOR;
				CollAffine F; sh->GroupPose (mesh, g, F);
				k.assign ((const char *)&F, sizeof F);
			} else {
				const char *m = CollMaterialName (sh->Material (mesh, g));
				if (CollKey::IEqual (m, "glass")) tier[g] = CBRK_GLASS;
				if (CollKey::IEqual (m, "gear")) gear[g] = 1;
				k = "P" + std::to_string (sh->PoseGroup ((uint32_t)part));
			}
		}
		k = std::to_string (tier[g]) + ":" + k;
		auto c = clsOf.emplace (k, (uint32_t)clsOf.size ());
		cls[g] = c.first->second;
	}
	std::vector<std::vector<uint32_t>> weld;
	uint32_t nw = DentMath::WeldMap (sl.v, DENT_WELD, weld, &cls);
	std::vector<size_t> base (ng + 1, 0);
	for (uint32_t g = 0; g < ng; g++) base[g + 1] = base[g] + sl.v[g].size ();
	Uf uf (base[ng]);
	std::vector<uint8_t> used (base[ng], 0);
	for (uint32_t g = 0; g < ng; g++) for (size_t t = 0; t + 2 < sl.idx[g].size (); t += 3) {
		size_t a = sl.idx[g][t], b = sl.idx[g][t + 1], c = sl.idx[g][t + 2];
		if (a >= sl.v[g].size () || b >= sl.v[g].size () || c >= sl.v[g].size ()) continue;
		uf.U (base[g] + a, base[g] + b); uf.U (base[g] + a, base[g] + c);
		used[base[g] + a] = used[base[g] + b] = used[base[g] + c] = 1;
	}
	std::vector<int64_t> first (nw, -1);
	for (uint32_t g = 0; g < ng; g++) for (size_t i = 0; i < sl.v[g].size () && g < weld.size () && i < weld[g].size (); i++) {
		uint32_t w = weld[g][i];
		if (w >= nw) continue;
		if (first[w] < 0) first[w] = (int64_t)(base[g] + i); else uf.U ((size_t)first[w], base[g] + i);
	}
	Uf ug (ng);
	std::map<size_t, uint32_t> rootGrp;
	for (uint32_t g = 0; g < ng; g++) for (size_t i = 0; i < sl.v[g].size (); i++) if (used[base[g] + i]) {
		auto r = rootGrp.emplace (uf.F (base[g] + i), g);
		if (!r.second) ug.U (r.first->second, g);
	}
	sl.pieceOf.assign (ng, -1);
	std::map<size_t, int> pieceOfRoot;
	for (uint32_t g = 0; g < ng; g++) {
		if (sl.idx[g].size () < 3 || sl.v[g].empty ()) continue;
		auto r = pieceOfRoot.emplace (ug.F (g), (int)sl.piece.size ());
		if (r.second) { sl.piece.emplace_back (); sl.piece.back ().tier = tier[g]; }
		sl.piece[r.first->second].grp.push_back ((uint16_t)g);
		sl.pieceOf[g] = r.first->second;
	}
	// functional points in the mesh frame
	std::vector<Vector> fp;
	Vector ofs = sdk.MeshOffset (vh, mesh), x;
	for (uint32_t i = 0, n = sdk.TouchdownCount (vh); i < n; i++) if (sdk.Touchdown (vh, i, x)) fp.push_back (x - ofs);
	for (uint32_t i = 0, n = sdk.ThrusterCount (vh); i < n; i++) if (sdk.ThrusterPos (vh, sdk.Thruster (vh, i), x)) fp.push_back (x - ofs);
	for (uint32_t i = 0, n = sdk.DockCount (vh); i < n; i++) { CollPortInfo pi; if (sdk.Dock (vh, i, pi)) fp.push_back (pi.pos - ofs); }
	Vector lo (1e300, 1e300, 1e300), hi (-1e300, -1e300, -1e300);
	for (auto &pc : sl.piece) {
		Vector cs; double A = 0; std::set<size_t> roots;
		for (uint16_t g : pc.grp) {
			if (gear[g]) pc.functional = true;
			for (size_t t = 0; t + 2 < sl.idx[g].size (); t += 3) {
				size_t a = sl.idx[g][t], b = sl.idx[g][t + 1], c = sl.idx[g][t + 2];
				if (a >= sl.v[g].size () || b >= sl.v[g].size () || c >= sl.v[g].size ()) continue;
				Vector pa = P (sl.v[g][a]), pb = P (sl.v[g][b]), pcc = P (sl.v[g][c]);
				double ar = 0.5 * crossp (pb - pa, pcc - pa).length ();
				A += ar; cs += (pa + pb + pcc) * (ar / 3);
			}
			for (size_t i = 0; i < sl.v[g].size (); i++) if (used[base[g] + i]) {
				roots.insert (uf.F (base[g] + i));
				Vector p = P (sl.v[g][i]);
				lo = Vector (std::min (lo.x, p.x), std::min (lo.y, p.y), std::min (lo.z, p.z));
				hi = Vector (std::max (hi.x, p.x), std::max (hi.y, p.y), std::max (hi.z, p.z));
				for (auto &f : fp) if ((f - p).length () <= BRK_FUNC_DIST) pc.functional = true;
			}
		}
		pc.area = A; pc.comps = (uint32_t)std::max<size_t> (1, roots.size ());
		if (A > 0) pc.c = cs / A;
		else { size_t n = 0; for (uint16_t g : pc.grp) for (auto &v : sl.v[g]) pc.c += P (v), n++; if (n) pc.c /= (double)n; }
		for (uint16_t g : pc.grp) for (auto &v : sl.v[g]) pc.r = std::max (pc.r, (P (v) - pc.c).length ());
		sl.area += A;
	}
	Vector mc = (lo + hi) * 0.5;
	for (uint32_t g = 0; g < ng; g++) for (size_t i = 0; i < sl.v[g].size (); i++) if (used[base[g] + i]) sl.rad = std::max (sl.rad, (P (sl.v[g][i]) - mc).length ());
	int big = -1;
	for (size_t k = 0; k < sl.piece.size (); k++) if (big < 0 || sl.piece[k].area > sl.piece[big].area) big = (int)k;
	for (size_t k = 0; k < sl.piece.size (); k++) {
		CollPieceA &pc = sl.piece[k];
		bool keepAll = true;
		for (uint16_t g : pc.grp) if (!sl.keep.count (g)) keepAll = false;
		pc.fixed = (int)k == big || pc.functional || keepAll;
		if (pc.tier == CBRK_PART && (pc.r > BRK_FIXED_R * sl.rad || pc.area > BRK_FIXED_A * sl.area)) pc.fixed = true;
	}
	return &sl;
}

// decisions (1), live only

void CollBreakA::Hit (const CollDamageHit &h)
{
	if (quiet || !cfg.brk) return;
	maxId = std::max (maxId, h.id);
	VesB &b = ves[h.id];
	if (h.playback) { b.last = h; b.haveLast = true; return; }
	CollH vh = h.h ? h.h : s.VesselHandle (h.id);
	if (!vh || CollKey::IEqual (sdk.ClassName (vh), BRK_CLASS)) return;
	const CollSlotA *sl = Slot (h.id, h.mesh);
	if (!sl || !sl->ok) return;
	CollShape *sh = b.sh;
	Vector ofs = sdk.MeshOffset (vh, h.mesh);
	std::vector<const DentRecord *> rec;
	if (const VesselDamageA *vd = s.Damage (h.id)) for (auto &r : vd->d.rec) if (r.slot == h.mesh && r.key == sl->key) rec.push_back (&r);
	std::vector<int> pk;
	double R = std::max (h.R, 1e-3);
	for (size_t k = 0; k < sl->piece.size (); k++) {
		const CollPieceA &p = sl->piece[k];
		if (p.fixed) continue;
		bool any = false;
		for (uint16_t g : p.grp) if (!sl->keep.count (g) && !Hidden (h.id, h.mesh, g)) any = true;
		if (!any) continue;
		CollAffine F; if (sh) sh->GroupPose (h.mesh, p.grp[0], F);
		Vector cr = tmul (F.A, h.c - ofs - F.t), nr = Unit (tmul (F.A, h.n));
		if ((p.c - cr).length () > R + p.r) continue;
		bool own = std::find (p.grp.begin (), p.grp.end (), (uint16_t)h.grp) != p.grp.end ();
		double lo = 1e300, hi = -1e300, u = 0, kmax = 0; bool inside = true;
		for (uint16_t g : p.grp) for (auto &v : sl->v[g]) {
			Vector x = P (v);
			double t = x & nr; lo = std::min (lo, t), hi = std::max (hi, t);
			double q = (x - cr).length2 () / (R * R);
			if (q > 1) inside = false;
			kmax = std::max (kmax, DentMath::Kernel (q));
			if (p.tier != CBRK_GLASS) u = std::max (u, Disp (rec, g, x).length ());
		}
		if (p.comps > 1 && !inside) continue;
		bool brk = false;
		if (p.tier == CBRK_GLASS) brk = h.vn > DENT_VN_GATE && h.depth * kmax >= std::max (BRK_GLASS_MIN, BRK_GLASS_REL * p.r);
		else {
			if (own && h.Esurplus > 0 && h.mat && h.mat->sigma_c > 0) u += h.Esurplus / (h.mat->sigma_c * std::max (p.area, 0.01));
			double ratio = u / std::max ({ hi - lo, 0.5 * p.r, 0.05 });
			brk = p.tier == CBRK_INTERIOR ? (h.vn >= BRK_VN_INTERIOR && ratio >= BRK_RATIO_INT) : (h.vn >= BRK_VN_PART && ratio >= BRK_RATIO_PART);
		}
		if (brk) pk.push_back ((int)k);
	}
	if (!pk.empty ()) Tear (h.id, vh, h, *sl, pk, ++events, false);
}

bool CollBreakA::MakeSpawn (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<int> &pk, uint32_t event, CollSpawnA &sp)
{
	VesB &b = ves[id];
	uint32_t mesh = hit.mesh;
	Vector ofs = sdk.MeshOffset (vh, mesh);
	DentDebris &d = sp.row;
	d.id = ++debrisSeq; d.slot = mesh; d.key = sl.key; d.ngrp = sl.ngrp; d.nvtx = sl.nvtx; d.simt = hit.simt;
	Vector cd; double A = 0, rmax = 0;
	std::vector<CollAffine> F (pk.size ());
	for (size_t i = 0; i < pk.size (); i++) {
		const CollPieceA &p = sl.piece[pk[i]];
		if (b.sh) b.sh->GroupPose (mesh, p.grp[0], F[i]);
		Vector c = mul (F[i].A, p.c) + F[i].t;
		double w = std::max (p.area, 1e-9);
		cd += c * w; A += w;
	}
	cd /= A;
	for (size_t i = 0; i < pk.size (); i++) {
		const CollPieceA &p = sl.piece[pk[i]];
		DentDebrisPose ps;
		MatQ (F[i].A, ps.q);
		ps.p = F[i].t - cd;
		for (uint16_t g : p.grp) if (!sl.keep.count (g)) ps.grp.push_back (g);
		if (ps.grp.empty ()) continue;
		rmax = std::max (rmax, (mul (F[i].A, p.c) + F[i].t - cd).length () + p.r);
		d.pose.push_back (ps);
	}
	if (d.pose.empty ()) return false;
	if (const VesselDamageA *vd = s.Damage (id)) for (auto &r : vd->d.rec) {
		if (r.slot != mesh || r.key != sl.key) continue;
		bool any = false;
		for (auto &ps : d.pose) for (uint16_t g : ps.grp) if (Lists (r, g)) any = true;
		if (any) d.rec.push_back (r);
	}
	sp.parent = id; sp.other = hit.other; sp.event = event; sp.mesh = sl.name;
	sp.cv = cd + ofs;
	sp.mass = Mass (id, sl, pk);
	if (sp.mass < BRK_MIN_MASS || rmax < BRK_MIN_R) return false;
	Vector n = Unit (hit.n), u = Unit (sp.cv);
	double un = u & n;
	if (un > 0) u = u - n * un;                                      // never toward the impactor (outside along +n)
	if (u.length () < 1e-6) u = crossp (n, std::fabs (n.x) < 0.9 ? Vector (1, 0, 0) : Vector (0, 1, 0));
	u = Unit (u);
	double jit = 0.8 + 0.4 * (Hash (id, (uint32_t)std::max (hit.rec, 0)) % 1000) / 999.0;
	Vector td = hit.tdir - n * (hit.tdir & n);
	sp.dv = u * (BRK_KICK * hit.vn * jit) + td * (0.5 * hit.vt);
	Vector dw = crossp (sp.dv, hit.c - sp.cv) / (rmax * rmax + 0.01);
	double wl = dw.length ();
	if (wl > 2 * 3.14159265358979) dw *= 2 * 3.14159265358979 / wl;
	sp.dw = dw;
	return true;
}

void CollBreakA::Tear (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<int> &pk, uint32_t event, bool playback)
{
	VesB &b = ves[id];
	uint32_t mesh = hit.mesh;
	Vector ofs = sdk.MeshOffset (vh, mesh);
	std::vector<Vector> cw (pk.size ());
	for (size_t i = 0; i < pk.size (); i++) {
		CollAffine F; if (b.sh) b.sh->GroupPose (mesh, sl.piece[pk[i]].grp[0], F);
		cw[i] = mul (F.A, sl.piece[pk[i]].c) + F.t + ofs;
	}
	// parts broken together whose spheres overlap form one debris
	Uf uf (pk.size ());
	for (size_t i = 0; i < pk.size (); i++) for (size_t j = i + 1; j < pk.size (); j++)
		if (sl.piece[pk[i]].tier == CBRK_PART && sl.piece[pk[j]].tier == CBRK_PART && (cw[i] - cw[j]).length () <= sl.piece[pk[i]].r + sl.piece[pk[j]].r) uf.U (i, j);
	bool canDebris = cfg.visuals && s.vis.Mode () != CollVisualA::MODE_OFF;
	if (canDebris && cfgOk < 0) cfgOk = sdk.DebrisClassExists () ? 1 : 0;
	if (canDebris && !cfgOk) { if (!loggedNoCfg) Log ("Collision: Config/Vessels/CollDebris.cfg missing: parts are hidden without debris"); loggedNoCfg = true; canDebris = false; }
	int made = 0;
	std::map<size_t, std::string> nameOf;
	for (size_t i = 0; i < pk.size (); i++) {
		if (uf.F (i) != i || sl.piece[pk[i]].tier != CBRK_PART || !canDebris || made >= BRK_PER_EVENT) continue;
		std::vector<int> cl;
		for (size_t j = 0; j < pk.size (); j++) if (uf.F (j) == i) cl.push_back (pk[j]);
		CollSpawnA sp;
		if (!MakeSpawn (id, vh, hit, sl, cl, event, sp)) continue;
		sp.row.name = NewName (sdk.Name (vh));
		nameOf[i] = sp.row.name;
		spawn.push_back (sp);
		made++;
	}
	for (size_t i = 0; i < pk.size (); i++) {
		const CollPieceA &p = sl.piece[pk[i]];
		DentTorn t;
		t.kind = (uint8_t)p.tier; t.slot = mesh; t.key = sl.key; t.ngrp = sl.ngrp; t.nvtx = sl.nvtx; t.simt = hit.simt;
		auto nm = nameOf.find (uf.F (i));
		t.debris = nm == nameOf.end () ? "-" : nm->second;
		for (uint16_t g : p.grp) if (!sl.keep.count (g) && !Hidden (id, mesh, g)) t.grp.push_back (g);
		if (t.grp.empty ()) continue;
		b.rows.push_back (t);
		if (!playback) { s.AddTorn (id, t); b.adopted++; }
		if (b.sh) for (uint16_t g : t.grp) b.sh->SetGroupHidden (mesh, g, true);
		breaks++;
		std::string gl;
		for (uint16_t g : t.grp) gl += (gl.empty () ? "" : ",") + std::to_string (g);
		Log ("Collision break '%s' kind=%d slot=%u groups=%s debris=%s vn=%.3g", sdk.Name (vh).c_str (), p.tier, mesh, gl.c_str (), t.debris.c_str (), hit.vn);
		CollBreakEvent ev;
		ev.id = id; ev.kind = p.tier; ev.c = cw[i]; ev.n = hit.n; ev.r = p.r; ev.playback = playback;
		if (s.fx) s.fx->Break (ev);
	}
}

std::string CollBreakA::NewName (const std::string &parent)
{
	for (uint32_t n = 1;; n++) {
		std::string nm = parent + "_D" + std::to_string (n);
		bool used = false;
		for (uint32_t i = 0, c = sdk.VesselCount (); i < c && !used; i++) if (CollKey::IEqual (sdk.Name (sdk.Vessel (i)), nm)) used = true;
		for (auto &sp : spawn) if (CollKey::IEqual (sp.row.name, nm)) used = true;
		if (!used) return nm;
	}
}

double CollBreakA::Mass (uint32_t parent, const CollSlotA &sl, const std::vector<int> &pk)
{
	CollH vh = s.VesselHandle (parent);
	double M = vh ? sdk.EmptyMass (vh) : 0, A = 0;
	for (int k : pk) A += sl.piece[k].area;
	double m = sl.area > 0 ? M * A / sl.area : 0;
	return std::max (1.0, std::min (m, BRK_MASS_MAX * M));
}

bool CollBreakA::BuildMesh (CollH mesh, const DentDebris &d, Geo &geo)
{
	std::set<uint16_t> in;
	for (auto &ps : d.pose) in.insert (ps.grp.begin (), ps.grp.end ());
	uint32_t ng = sdk.TplGroups (mesh);
	if (ng != d.ngrp) return false;
	for (uint32_t g = 0; g < ng; g++) if (!in.count ((uint16_t)g)) sdk.MeshEdit (mesh, g, 2, nullptr, 0);
	geo.clear ();
	for (auto &ps : d.pose) for (uint16_t g : ps.grp) {
		CollTplGroup t;
		if (!sdk.TplGroup (mesh, g, t)) return false;
		std::vector<DentVtx> rest (t.nvtx);
		if (t.nvtx) std::memcpy (rest.data (), t.vtx, sizeof (DentVtx) * t.nvtx);
		std::vector<DentVtx> v = PieceVertices (rest, g, ps, d.rec);
		sdk.MeshEdit (mesh, g, 0, v.data (), (uint32_t)v.size ());
		geo.emplace_back (std::move (v), std::vector<uint16_t> (t.idx, t.idx + t.nidx));
	}
	return true;
}

CollSdk::DebrisCaps CollBreakA::Caps (const Geo &geo, double mass, uint32_t *fnv)
{
	CollSdk::DebrisCaps c;
	Vector lo (1e300, 1e300, 1e300), hi (-1e300, -1e300, -1e300), J, cs; double A = 0, sz = 0;
	uint32_t h = 2166136261u;
	for (auto &g : geo) {
		for (auto &v : g.first) {
			Vector p = P (v);
			sz = std::max (sz, p.length ());
			lo = Vector (std::min (lo.x, p.x), std::min (lo.y, p.y), std::min (lo.z, p.z));
			hi = Vector (std::max (hi.x, p.x), std::max (hi.y, p.y), std::max (hi.z, p.z));
			const unsigned char *b = (const unsigned char *)&v;
			for (size_t i = 0; i < 12; i++) h = (h ^ b[i]) * 16777619u;
		}
		for (size_t t = 0; t + 2 < g.second.size (); t += 3) {
			size_t a = g.second[t], b = g.second[t + 1], d = g.second[t + 2];
			if (a >= g.first.size () || b >= g.first.size () || d >= g.first.size ()) continue;
			Vector pa = P (g.first[a]), pb = P (g.first[b]), pd = P (g.first[d]);
			Vector N = crossp (pb - pa, pd - pa) * 0.5;
			double ar = N.length ();
			Vector m = (pa + pb + pd) / 3.0;
			J += Vector (m.y * m.y + m.z * m.z, m.x * m.x + m.z * m.z, m.x * m.x + m.y * m.y) * ar;
			cs += Vector (std::fabs (N.x), std::fabs (N.y), std::fabs (N.z)) * 0.5;
			A += ar;
		}
	}
	if (fnv) *fnv = h;
	if (geo.empty () || !(sz > 0)) sz = 0.5;
	double fl = (0.05 * sz) * (0.05 * sz);
	c.size = std::max (sz, 0.1); c.mass = mass;
	c.pmi = A > 0 ? J / A : Vector (fl, fl, fl);
	c.pmi = Vector (std::max (c.pmi.x, fl), std::max (c.pmi.y, fl), std::max (c.pmi.z, fl));
	c.cs = Vector (std::max (cs.x, 0.01), std::max (cs.y, 0.01), std::max (cs.z, 0.01));
	if (!(lo.x <= hi.x)) lo = Vector (-0.2, -0.2, -0.2), hi = Vector (0.2, 0.2, 0.2);
	double xm = 0.5 * (lo.x + hi.x);
	c.td = { Vector (xm, lo.y, hi.z), Vector (lo.x, lo.y, lo.z), Vector (hi.x, lo.y, lo.z),
		Vector (lo.x, hi.y, lo.z), Vector (hi.x, hi.y, lo.z), Vector (lo.x, hi.y, hi.z), Vector (hi.x, hi.y, hi.z), Vector (xm, hi.y, hi.z) };
	c.tdK = 4 * mass * 9.81 / 0.02;
	c.tdD = 2 * 0.7 * std::sqrt (c.tdK * mass);
	c.mu = 0.5;
	return c;
}

uint32_t CollBreakA::FindId (CollH h)
{
	for (uint32_t i = 0; i <= maxId + 64; i++) if (s.VesselHandle (i) == h) { maxId = std::max (maxId, i); return i; }
	return ~0u;
}

void CollBreakA::Spawn (CollSpawnA &sp, double simt)
{
	CollH vh = s.VesselHandle (sp.parent);
	if (!vh) return;
	for (uint32_t i = 0, c = sdk.VesselCount (); i < c; i++) if (CollKey::IEqual (sdk.Name (sdk.Vessel (i)), sp.row.name)) { sp.row.name = NewName (sdk.Name (vh)); break; }
	CollH mesh = sdk.MeshLoad (sp.mesh.c_str ());
	if (!mesh) { Log ("Collision: debris mesh '%s' not loaded, no debris", sp.mesh.c_str ()); return; }
	Geo geo;
	if (!BuildMesh (mesh, sp.row, geo)) { sdk.MeshFree (mesh); Log ("Collision: debris mesh '%s' does not match its slot, no debris", sp.mesh.c_str ()); return; }
	uint32_t fnv = 0;
	CollSdk::DebrisCaps caps = Caps (geo, sp.mass, &fnv);
	CollVesselRead rd {};
	sdk.ReadVessel (vh, rd, CVR_NOWEIGHT);
	Vector gp, gv; Matrix gR;
	if (rd.gref) sdk.GlobalState (rd.gref, gp, gv, gR);
	CollStateWrite st;
	st.rbody = rd.gref;
	st.rpos = rd.x + mul (rd.R, sp.cv) - gp;
	st.rvel = rd.v + mul (rd.R, crossp (sp.cv, rd.w) + sp.dv) - gv;
	st.vrot = rd.w + sp.dw; st.arot = Vector ();
	CollH h = sdk.VesselCreate (sp.row.name.c_str (), BRK_CLASS, st);
	if (!h) { sdk.MeshFree (mesh); Log ("Collision: debris vessel '%s' not created", sp.row.name.c_str ()); return; }
	sdk.DebrisSetup (h, mesh, caps);
	sdk.SetAttitude (h, rd.R);
	sdk.SetSpin (h, rd.w + sp.dw);
	CollDebrisA d;
	d.h = h; d.mesh = mesh; d.parent = sp.parent; d.other = sp.other; d.event = sp.event; d.birth = simt; d.row = sp.row; d.fnv = fnv;
	d.id = FindId (h);
	live.push_back (d);
	if (d.id != ~0u) {
		auto add = [&] (uint32_t a) { if (a == d.id || !s.VesselHandle (a)) return; pairs.push_back ({ std::min (a, d.id), std::max (a, d.id), simt, d.id }); s.noPair[{ std::min (a, d.id), std::max (a, d.id) }] = simt; };
		add (d.parent);
		if (d.other) add (d.other);
		for (auto &o : live) if (o.event == d.event && o.parent == d.parent && o.id != ~0u && o.id != d.id) add (o.id);
	}
	Log ("Collision debris '%s' from '%s' slot=%u mass=%.4g fnv=%08x", d.row.name.c_str (), sdk.Name (vh).c_str (), d.row.slot, sp.mass, fnv);
	SyncRows (sp.parent);
}

void CollBreakA::SyncRows (uint32_t parent)
{
	if (!s.VesselHandle (parent)) return;
	std::vector<DentDebris> r;
	for (auto &d : live) if (d.parent == parent) r.push_back (d.row);
	const VesselDamageA *vd = s.Damage (parent);
	if (r.empty () && (!vd || vd->d.debris.empty ())) return;
	s.SetDebris (parent, r);
}

void CollBreakA::Kill (size_t i, const char *why)
{
	CollDebrisA d = live[i];
	live.erase (live.begin () + (long)i);
	Log ("Collision debris '%s' deleted: %s", d.row.name.c_str (), why);
	for (size_t k = pairs.size (); k-- > 0;) if (pairs[k].debris == d.id) { s.noPair.erase ({ pairs[k].a, pairs[k].b }); pairs.erase (pairs.begin () + (long)k); }
	freeMesh.push_back ({ d.mesh, d.h, false });
	if (d.h && sdk.IsVessel (d.h)) sdk.VesselDelete (d.h); else freeMesh.back ().dropped = true;
	SyncRows (d.parent);
}

// re-apply (2)

void CollBreakA::Assert (uint32_t id, VesB &b)
{
	if (b.rows.empty () || !cfg.visuals || s.vis.Mode () == CollVisualA::MODE_OFF) return;
	CollH vh = s.VesselHandle (id);
	if (!vh) return;
	CollH vis = sdk.Visual (vh);
	if (!vis) return;
	uint32_t nm = sdk.MeshCount (vh);
	for (auto &t : b.rows) {
		if (t.slot >= nm) continue;
		const CollSlotA *sl = Slot (id, t.slot);
		if (!sl || !sl->ok || sl->key != t.key || sl->ngrp != t.ngrp || sl->nvtx != t.nvtx) continue; // dormant on a mismatch
		CollH dm = sdk.DevMesh (vh, vis, t.slot);
		if (!dm) continue;
		for (uint16_t g : t.grp) sdk.GroupFlag (vh, dm, g, 2, true);
		reasserts++;
	}
}

void CollBreakA::Collider (uint32_t id, VesB &b, bool force)
{
	if (!b.sh) return;
	for (auto &t : b.rows) {
		if (!force && !b.sh->Replaced (t.slot)) continue;
		const CollSlotA *sl = Slot (id, t.slot);
		if (!sl || !sl->ok || sl->key != t.key || sl->ngrp != t.ngrp || sl->nvtx != t.nvtx) continue;
		for (uint16_t g : t.grp) b.sh->SetGroupHidden (t.slot, g, true);
	}
}

void CollBreakA::Shapes (uint32_t id, CollShape *sh)
{
	if (quiet) return;
	maxId = std::max (maxId, id);
	VesB &b = ves[id];
	bool first = !b.seen || b.sh != sh;
	b.sh = sh; b.seen = true;
	if (sh) {
		for (auto it = slots.begin (); it != slots.end ();) {
			if (it->first.first == id && (first || sh->Replaced (it->first.second))) it = slots.erase (it); else ++it;
		}
		bool rep = first;
		for (auto &t : b.rows) if (sh->Replaced (t.slot)) rep = true;
		if (rep) Collider (id, b, true);
	}
	CollH h = s.VesselHandle (id);
	for (auto &d : live) if (d.id == ~0u && h && d.h == h) {
		d.id = id;
		auto add = [&] (uint32_t a) { if (a == id || !s.VesselHandle (a)) return; pairs.push_back ({ std::min (a, id), std::max (a, id), sdk.SimTime (), id }); s.noPair[{ std::min (a, id), std::max (a, id) }] = sdk.SimTime (); };
		add (d.parent);
		if (d.other) add (d.other);
	}
}

void CollBreakA::Adopt ()
{
	for (auto &kv : s.Vessels ()) {
		VesB &b = ves[kv.first];
		const auto &rows = kv.second.d.torn;
		if (rows.size () < b.adopted) b.adopted = rows.size ();
		bool any = false;
		for (size_t i = b.adopted; i < rows.size (); i++) {
			bool dup = false;
			for (auto &t : b.rows) if (t.slot == rows[i].slot && t.grp == rows[i].grp && t.simt == rows[i].simt) dup = true;
			if (!dup) b.rows.push_back (rows[i]), any = true;
		}
		b.adopted = rows.size ();
		if (any) Collider (kv.first, b, true);
	}
}

void CollBreakA::Torn (uint32_t id, const DentTorn &t)
{
	if (quiet) return;
	VesB &b = ves[id];
	for (auto &x : b.rows) if (x.slot == t.slot && x.grp == t.grp && x.simt == t.simt) return;
	b.rows.push_back (t);
	Collider (id, b, true);
	CollH vh = s.VesselHandle (id);
	if (!vh || !sdk.Playback (vh)) return;
	const CollSlotA *sl = Slot (id, t.slot);
	std::vector<int> pk;
	if (sl && sl->ok) for (uint16_t g : t.grp) if (g < sl->pieceOf.size () && sl->pieceOf[g] >= 0 && std::find (pk.begin (), pk.end (), sl->pieceOf[g]) == pk.end ()) pk.push_back (sl->pieceOf[g]);
	CollDamageHit hit;
	if (b.haveLast) hit = b.last; else { hit.vn = 10; hit.n = Vector (0, 0, 1); }
	hit.id = id; hit.mesh = t.slot; hit.simt = sdk.SimTime (); hit.playback = true;
	for (int k : pk) {
		CollBreakEvent ev;
		ev.id = id; ev.kind = t.kind; ev.c = sl->piece[k].c + sdk.MeshOffset (vh, t.slot); ev.n = hit.n; ev.r = sl->piece[k].r; ev.playback = true;
		if (s.fx) s.fx->Break (ev);
	}
	if (!cfg.debrisPlayback || t.debris == "-" || t.kind != CBRK_PART || pk.empty () || !cfg.visuals || s.vis.Mode () == CollVisualA::MODE_OFF) return;
	if (cfgOk < 0) cfgOk = sdk.DebrisClassExists () ? 1 : 0;
	if (!cfgOk) return;
	CollSpawnA sp;
	if (!MakeSpawn (id, vh, hit, *sl, pk, ++events, sp)) return;
	sp.row.name = NewName (sdk.Name (vh));
	spawn.push_back (sp);
}

void CollBreakA::Repair (uint32_t id)
{
	if (quiet) return;
	auto it = ves.find (id);
	if (it == ves.end ()) return;
	VesB &b = it->second;
	CollH vh = s.VesselHandle (id);
	CollH vis = vh ? sdk.Visual (vh) : nullptr;
	for (auto &t : b.rows) {
		const CollSlotA *sl = vh ? Slot (id, t.slot) : nullptr;
		CollH dm = vis && sl && sl->ok ? sdk.DevMesh (vh, vis, t.slot) : nullptr;
		for (uint16_t g : t.grp) {
			if (dm && g < sl->usr.size () && !(sl->usr[g] & 2)) sdk.GroupFlag (vh, dm, g, 2, false);
			if (b.sh) b.sh->SetGroupHidden (t.slot, g, false);
		}
	}
	b.rows.clear ();
	if (const VesselDamageA *vd = s.Damage (id)) b.adopted = vd->d.torn.size ();
}

void CollBreakA::Pass ()
{
	if (quiet) return;
	for (auto &kv : ves) Assert (kv.first, kv.second);
}

bool CollBreakA::Hidden (uint32_t id, uint32_t mesh, uint32_t g) const
{
	auto it = ves.find (id);
	if (it == ves.end ()) return false;
	for (auto &t : it->second.rows) if (t.slot == mesh && std::find (t.grp.begin (), t.grp.end (), (uint16_t)g) != t.grp.end ()) return true;
	return false;
}

void CollBreakA::DropVessel (uint32_t id, CollH h)
{
	for (size_t i = live.size (); i-- > 0;) if (live[i].id == id || (h && live[i].h == h)) {
		uint32_t parent = live[i].parent;
		freeMesh.push_back ({ live[i].mesh, live[i].h, true });
		live.erase (live.begin () + (long)i);
		SyncRows (parent);
	}
	for (auto &f : freeMesh) if (h && f.h == h) f.dropped = true;
	for (size_t k = pairs.size (); k-- > 0;) if (pairs[k].a == id || pairs[k].b == id) { s.noPair.erase ({ pairs[k].a, pairs[k].b }); pairs.erase (pairs.begin () + (long)k); }
	ves.erase (id);
	for (auto it = slots.begin (); it != slots.end ();) { if (it->first.first == id) it = slots.erase (it); else ++it; }
}

void CollBreakA::PairCheck (double simt)
{
	std::vector<uint32_t> stuck;
	for (size_t k = pairs.size (); k-- > 0;) {
		CollPairA p = pairs[k];
		CollH ha = s.VesselHandle (p.a), hb = s.VesselHandle (p.b);
		if (!ha || !hb) { s.noPair.erase ({ p.a, p.b }); pairs.erase (pairs.begin () + (long)k); continue; }
		Vector pa, pb, v; Matrix R;
		sdk.GlobalState (ha, pa, v, R); sdk.GlobalState (hb, pb, v, R);
		auto rad = [&] (uint32_t id, CollH h) { auto it = ves.find (id); Vector c; double r = 0; if (it != ves.end () && it->second.sh) { it->second.sh->Bound (1, c, r); if (r > 0) return r; } return sdk.Size (h); };
		double gap = (pa - pb).length () - rad (p.a, ha) - rad (p.b, hb);
		if (gap > BRK_SEP) { s.noPair.erase ({ p.a, p.b }); pairs.erase (pairs.begin () + (long)k); continue; }
		if (simt - p.t > BRK_STUCK && std::find (stuck.begin (), stuck.end (), p.debris) == stuck.end ()) stuck.push_back (p.debris);
	}
	for (uint32_t id : stuck) for (size_t i = 0; i < live.size (); i++) if (live[i].id == id) { Kill (i, "still overlapping"); break; } // Kill erases pairs: never inside the pair loop
}

void CollBreakA::Rebuild (double simt)
{
	(void)simt;
	std::set<std::string> claimed;
	for (auto &kv : s.Vessels ()) {
		if (kv.second.d.debris.empty ()) continue;
		uint32_t pid = kv.first;
		CollH vh = s.VesselHandle (pid);
		std::vector<DentDebris> rows = kv.second.d.debris;
		for (auto &row : rows) {
			debrisSeq = std::max (debrisSeq, row.id);
			CollH h = nullptr;
			for (uint32_t i = 0, c = sdk.VesselCount (); i < c && !h; i++) { CollH x = sdk.Vessel (i); if (CollKey::IEqual (sdk.Name (x), row.name)) h = x; }
			if (!h) { Log ("Collision: debris row '%s' without its vessel dropped", row.name.c_str ()); continue; }
			if (!CollKey::IEqual (sdk.ClassName (h), BRK_CLASS)) continue;
			claimed.insert (row.name);
			const CollSlotA *sl = vh ? Slot (pid, row.slot) : nullptr;
			CollH mesh = sl && sl->ok && sl->key == row.key ? sdk.MeshLoad (sl->name.c_str ()) : nullptr;
			Geo geo;
			if (!mesh || !BuildMesh (mesh, row, geo)) {
				if (mesh) sdk.MeshFree (mesh);
				Log ("Collision: debris '%s' mesh missing or changed, deleted", row.name.c_str ());
				sdk.VesselDelete (h);
				continue;
			}
			std::vector<int> pk;
			for (auto &ps : row.pose) for (uint16_t g : ps.grp) if (g < sl->pieceOf.size () && sl->pieceOf[g] >= 0 && std::find (pk.begin (), pk.end (), sl->pieceOf[g]) == pk.end ()) pk.push_back (sl->pieceOf[g]);
			uint32_t fnv = 0;
			CollSdk::DebrisCaps caps = Caps (geo, Mass (pid, *sl, pk), &fnv);
			sdk.DebrisSetup (h, mesh, caps);
			CollDebrisA d;
			d.h = h; d.mesh = mesh; d.parent = pid; d.birth = std::min (row.simt, simt); d.row = row; d.fnv = fnv; d.id = FindId (h); // birth: sim time restarts on load
			live.push_back (d);
			Log ("Collision break restored '%s' fnv=%08x", row.name.c_str (), fnv);
		}
		SyncRows (pid);
	}
	for (uint32_t i = sdk.VesselCount (); i-- > 0;) {
		CollH x = sdk.Vessel (i);
		if (!x || !CollKey::IEqual (sdk.ClassName (x), BRK_CLASS) || claimed.count (sdk.Name (x))) continue;
		bool ours = false;
		for (auto &d : live) if (d.h == x) ours = true;
		if (ours) continue;
		Log ("Collision: debris '%s' without rows deleted", sdk.Name (x).c_str ());
		sdk.VesselDelete (x);
	}
}

void CollBreakA::Post (double simt, double)
{
	if (quiet) return;
	if (!rebuilt) { rebuilt = true; Rebuild (simt); }
	Adopt ();
	for (size_t i = freeMesh.size (); i-- > 0;) if (freeMesh[i].dropped) { if (freeMesh[i].mesh) sdk.MeshFree (freeMesh[i].mesh); freeMesh.erase (freeMesh.begin () + (long)i); }
	for (auto &kv : ves) Assert (kv.first, kv.second);
	std::vector<CollSpawnA> sp;
	sp.swap (spawn);
	for (auto &x : sp) Spawn (x, simt);
	PairCheck (simt);
	for (size_t i = live.size (); i-- > 0;) if (simt - live[i].birth > cfg.debrisLife) Kill (i, "life");
	while (live.size () > (size_t)std::max (0, cfg.debrisMax)) Kill (0, "cap");
}

void CollBreakA::End ()
{
	if (!quiet) {
		for (auto &d : live) if (d.mesh) sdk.MeshFree (d.mesh);
		for (auto &f : freeMesh) if (f.mesh) sdk.MeshFree (f.mesh);
	}
	live.clear (); freeMesh.clear (); spawn.clear (); pairs.clear (); ves.clear (); slots.clear ();
	rebuilt = false; cfgOk = -1; loggedNoCfg = false;
}

std::unique_ptr<CollDmgSink> CollMakeBreak (CollSdk &sdk, CollDmgSession &s, const CollCfgValues &cfg)
{
	return std::make_unique<CollBreakA> (sdk, s, cfg);
}
