// not upstream: collision addon, dmg3 area P: parts tear off, glass breaks, interiors hidden, debris vessels (design-CA-dmg3-P)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
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

Vector Disp (const std::vector<const DentRecord *> &rec, uint16_t g, const Vector &x, const DentSites *sites)
{
	std::vector<const DentParams *> l;
	for (const DentRecord *r : rec) if (Lists (*r, g)) l.push_back (&r->p);
	return DentMath::Fold (l.data (), l.size (), x, true, nullptr, sites); // dmg3 tear: cuts as maps; blast: VCUT with the slot's sites
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

static double Q9 (double v) // round trip through the saved %.9g text
{
	char b[40];
	snprintf (b, sizeof b, "%.9g", v);
	return strtod (b, nullptr);
}

std::vector<DentVtx> CollBreakA::PieceVertices (const std::vector<DentVtx> &rest, uint16_t g, const DentDebrisPose &p, const std::vector<DentRecord> &rec, const DentSites *sites)
{
	std::vector<const DentRecord *> rp;
	for (auto &r : rec) rp.push_back (&r);
	Matrix A = QMat (p.q);
	std::vector<DentVtx> out = rest;
	for (size_t i = 0; i < rest.size (); i++) {
		Vector x = P (rest[i]);
		Vector y = mul (A, x + Disp (rp, g, x, sites)) + p.p, n = mul (A, Vector (rest[i].nx, rest[i].ny, rest[i].nz));
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
	std::map<std::string, uint32_t> clsOf, poseOf;
	sl.cls.assign (ng, 0);
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
		if (sl.usr[g] & 0x2) tier[g] = CBRK_RESTHIDDEN; // dmg3 tear: hidden at rest: never broken or debris
		auto pc = poseOf.emplace (tier[g] == CBRK_INTERIOR ? "I" + k : k, (uint32_t)poseOf.size ());
		sl.cls[g] = pc.first->second;
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
	sl.tier = tier;
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
	std::vector<std::pair<Vector, uint32_t>> fp;
	Vector ofs = sdk.MeshOffset (vh, mesh), x;
	for (uint32_t i = 0, n = sdk.TouchdownCount (vh); i < n; i++) if (sdk.Touchdown (vh, i, x)) fp.push_back ({ x - ofs, CBRK_FN_TD });
	for (uint32_t i = 0, n = sdk.ThrusterCount (vh); i < n; i++) if (sdk.ThrusterPos (vh, sdk.Thruster (vh, i), x)) fp.push_back ({ x - ofs, CBRK_FN_THR });
	for (uint32_t i = 0, n = sdk.DockCount (vh); i < n; i++) { CollPortInfo pi; if (sdk.Dock (vh, i, pi)) fp.push_back ({ pi.pos - ofs, CBRK_FN_DOCK }); }
	Vector lo (1e300, 1e300, 1e300), hi (-1e300, -1e300, -1e300);
	for (auto &pc : sl.piece) {
		Vector cs; double A = 0; std::set<size_t> roots;
		for (uint16_t g : pc.grp) {
			if (gear[g]) pc.functional |= CBRK_FN_GEAR;
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
				for (auto &f : fp) if ((f.first - p).length () <= BRK_FUNC_DIST) pc.functional |= f.second;
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
	if (big >= 0 && !sl.piece[big].grp.empty ()) sl.staticCls = sl.cls[sl.piece[big].grp[0]]; // M2
	for (size_t k = 0; k < sl.piece.size (); k++) {
		CollPieceA &pc = sl.piece[k];
		bool keepAll = true;
		for (uint16_t g : pc.grp) if (!sl.keep.count (g)) keepAll = false;
		pc.fixed = (int)k == big || (pc.functional & ~CBRK_FN_DOCK) || keepAll; // dmg3 tear: the dock pin is decided per hit
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
	if (cfg.blast) BlastHit (h, vh, *sl);
	CollShape *sh = b.sh;
	Vector ofs = sdk.MeshOffset (vh, h.mesh);
	if (!cfg.blast && h.rec >= 0) if (const VesselDamageA *vd = s.Damage (h.id)) if ((size_t)h.rec < vd->d.rec.size ()) { // dmg3 tear: section or tip first; blast decides when on
		const DentParams &src = vd->d.rec[h.rec].p;
		double L = sdk.Size (vh);
		bool tip = TipGate (h, src), sec = !tip && TearGate (h, src, L);
		for (size_t k = (size_t)h.rec + 1; k < vd->d.rec.size () && (tip || sec); k++) // one tear per crush or hinge record: later cuts on the slot came from it
			if ((vd->d.rec[k].p.mode == DENTM_CUT || vd->d.rec[k].p.mode == DENTM_VCUT) && vd->d.rec[k].slot == h.mesh) tip = sec = false;
		if (tip || sec) {
			uint32_t ev = ++events;
			CollCutPlan pl = PlanCut (h.id, *sl, h, src, L, tip, ev);
			if (pl.ok) Section (h.id, vh, h, *sl, pl, ev);
			else if (cfg.logLevel >= 1) Log ("Collision tear '%s' refused: %s", sdk.Name (vh).c_str (), pl.why);
		}
	}
	std::vector<const DentRecord *> rec; // m5: after the tear block appended its cut
	if (const VesselDamageA *vd = s.Damage (h.id)) for (auto &r : vd->d.rec) if (r.slot == h.mesh && r.key == sl->key) rec.push_back (&r);
	std::vector<int> pk;
	const DentSites *hs = s.Sites (h.id, h.mesh);
	double R = std::max (h.R, 1e-3);
	for (size_t k = 0; k < sl->piece.size (); k++) {
		const CollPieceA &p = sl->piece[k];
		if (p.fixed || p.tier == CBRK_RESTHIDDEN || ((p.functional & CBRK_FN_DOCK) && h.eSpec < BRK_TEAR_E)) continue; // dmg3 tear: dock pin released above the tear threshold
		if (cfg.blast && p.tier == CBRK_PART) continue;                       // blast: parts break by bond stress; glass and interior stay
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
			if (p.tier != CBRK_GLASS) u = std::max (u, Disp (rec, g, x, hs).length ());
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
		ps.p = Vector (Q9 (ps.p.x), Q9 (ps.p.y), Q9 (ps.p.z)); // as saved (%.9g), so a reload rebuilds the same bits
		for (double &x : ps.q) x = Q9 (x);
		for (uint16_t g : p.grp) if (!sl.keep.count (g) && !(g < sl.usr.size () && (sl.usr[g] & 0x2))) ps.grp.push_back (g);
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
	d.other = IdName (hit.other);
	sp.cv = cd + ofs;
	sp.mass = Mass (id, sl, pk);
	if (sp.mass < BRK_MIN_MASS || rmax < BRK_DEBRIS_R) return false; // dmg3 tear: small parts are hidden only
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

// dmg3 tear: sections and tips (design-CA-dmg3-tear 2, 3)

bool CollBreakA::TearGate (const CollDamageHit &h, const DentParams &crush, double L)
{
	if (crush.mode != DENTM_CRUSH) return false;
	bool cap = h.Esurplus > 0 || crush.P >= 0.95 * DentMath::DmaxCrush (L);
	return cap && h.vn >= BRK_TEAR_VN && h.eSpec >= BRK_TEAR_E;
}

bool CollBreakA::TipGate (const CollDamageHit &h, const DentParams &hp)
{
	if (hp.mode != DENTM_HINGE || !(h.Mp > 0)) return false;
	return hp.P >= 0.999 * DENT_HINGE_TMAX && h.Esurplus >= 0.951 * h.Mp && h.vn >= BRK_VN_PART;
}

CollAffine CollBreakA::StaticPose (const CollSlotA &sl, const std::vector<CollAffine> &F)
{
	for (size_t g = 0; g < F.size () && g < sl.cls.size (); g++) if (sl.cls[g] == sl.staticCls && sl.tier.size () > g && sl.tier[g] != CBRK_INTERIOR) return F[g];
	return CollAffine ();
}

std::vector<CollAffine> CollBreakA::Poses (uint32_t id, uint32_t mesh, size_t ng)
{
	std::vector<CollAffine> F (ng);
	CollShape *sh = ves[id].sh;
	if (sh) for (size_t g = 0; g < ng; g++) sh->GroupPose (mesh, (uint32_t)g, F[g]);
	return F;
}

CollCutPlan CollBreakA::PlanCut (uint32_t id, const CollSlotA &sl, const CollDamageHit &h, const DentParams &src, double L, bool tip, uint32_t event)
{
	CollCutPlan pl;
	size_t ng = sl.v.size ();
	if (h.grp < 0 || (size_t)h.grp >= ng || sl.cls.size () != ng || sl.tier.size () != ng) { pl.why = "group"; return pl; }
	std::vector<CollAffine> F = Poses (id, h.mesh, ng);
	CollAffine Fsi = CollInverse (StaticPose (sl, F)); // M2: plane in the rest frame of the static class
	auto X = [&] (size_t g, const DentVtx &v) { return CollApply (Fsi, mul (F[g].A, P (v)) + F[g].t); };
	auto Dir = [&] (const Vector &d) { return Unit (CollApplyDir (Fsi, mul (F[h.grp].A, d))); };
	uint32_t hcls = sl.staticCls;
	DentParams sp = src; // the source record in the static rest frame
	sp.c = CollApply (Fsi, mul (F[h.grp].A, src.c) + F[h.grp].t), sp.n = Dir (src.n), sp.t = Dir (src.t);
	Vector tdir = CollApplyDir (Fsi, h.tdir);
	auto live = [&] (size_t g) { return sl.idx[g].size () >= 3 && !sl.v[g].empty () && !sl.keep.count ((uint16_t)g) && !Hidden (id, h.mesh, (uint32_t)g); };
	Vector n, c;
	double cap = BRK_TEAR_DMAX * L, Rcr = std::max (sp.R, 1e-3);
	if (tip) {
		n = Unit (sp.t);
		c = sp.c - sp.t * sp.hd - sp.n * sp.hz;
	} else {
		double vr = h.vn > 0 ? std::min (1.0, h.vt / h.vn) : 0.0;
		Vector td = tdir - sp.n * (tdir & sp.n);
		n = Unit (sp.n + td * (0.3 * vr));
		pl.d = std::min (sp.P + Rcr * (1 + 3 * std::min (1.0, (h.eSpec - BRK_TEAR_E) / BRK_TEAR_E)), cap);
		c = sp.c - n * pl.d;
		for (int it = 0; it < 256; it++) { // snap to a group's rear when the plane would leave a sliver
			bool moved = false;
			for (size_t g = 0; g < ng && !moved; g++) {
				if (!live (g) || sl.cls[g] != hcls) continue;
				double lo = 1e300, hi = -1e300;
				for (auto &v : sl.v[g]) { double t = (X (g, v) - c) & n; lo = std::min (lo, t), hi = std::max (hi, t); }
				if (lo < 0 && hi > 0 && -lo < 0.25 * Rcr && pl.d - lo <= cap) { pl.d -= lo; c = sp.c - n * pl.d; moved = true; }
			}
			if (!moved) break;
		}
	}
	auto classify = [&] (double band, std::vector<uint16_t> &front, std::vector<uint16_t> &strad) {
		front.clear (), strad.clear ();
		for (size_t g = 0; g < ng; g++) {
			if (!live (g)) continue;
			double lo = 1e300, hi = -1e300; Vector cs;
			for (auto &v : sl.v[g]) { Vector x = X (g, v); double t = (x - c) & n; lo = std::min (lo, t), hi = std::max (hi, t); cs += x; }
			cs /= (double)sl.v[g].size ();
			if (lo > band) front.push_back ((uint16_t)g);
			else if (hi > -band && sl.cls[g] == hcls && (sl.tier[g] == CBRK_PART || sl.tier[g] == CBRK_GLASS)) strad.push_back ((uint16_t)g);
			else if (hi > band && ((cs - c) & n) > 0) front.push_back ((uint16_t)g);
		}
	};
	auto extent = [&] (const std::vector<uint16_t> &front, const std::vector<uint16_t> &strad, Vector &ctr, double &R, double &Af, double &edge) {
		std::vector<Vector> pts; Af = 0; edge = 0; size_t ne = 0;
		for (uint16_t g : front) for (auto &v : sl.v[g]) pts.push_back (X (g, v));
		for (uint16_t g : strad) for (auto &v : sl.v[g]) { Vector x = X (g, v); if (((x - c) & n) > 0) pts.push_back (x); }
		auto triA = [&] (uint16_t g, bool half) {
			for (size_t t = 0; t + 2 < sl.idx[g].size (); t += 3) {
				size_t a = sl.idx[g][t], b = sl.idx[g][t + 1], d = sl.idx[g][t + 2];
				if (a >= sl.v[g].size () || b >= sl.v[g].size () || d >= sl.v[g].size ()) continue;
				Vector pa = X (g, sl.v[g][a]), pb = X (g, sl.v[g][b]), pd = X (g, sl.v[g][d]);
				if (half) { edge += (pb - pa).length () + (pd - pb).length () + (pa - pd).length (); ne += 3; }
				if (half && ((((pa + pb + pd) / 3.0) - c) & n) <= 0) continue;
				Af += 0.5 * crossp (pb - pa, pd - pa).length ();
			}
		};
		for (uint16_t g : front) triA (g, false);
		for (uint16_t g : strad) triA (g, true);
		edge = ne ? edge / (double)ne : 0.0;
		ctr = c;
		if (!pts.empty ()) { Vector m; for (auto &x : pts) m += x; m /= (double)pts.size (); ctr = m - n * ((m - c) & n); } // m4: centroid of the front points on the plane
		R = 0;
		for (auto &x : pts) { Vector d = x - ctr; R = std::max (R, (d - n * (d & n)).length ()); }
		R *= 1.05;
	};
	std::vector<uint16_t> front, strad;
	classify (0.0, front, strad);
	Vector ctr; double R, Af, edge;
	extent (front, strad, ctr, R, Af, edge);
	double wc = std::max (std::min (std::max (0.25 * R, 0.15), 0.6), 1.5 * edge), Aj = 0.4 * wc;
	classify (Aj, front, strad);
	extent (front, strad, ctr, R, Af, edge);
	pl.front = front, pl.straddle = strad, pl.area = Af, pl.f = sl.area > 0 ? Af / sl.area : 0.0;
	if (front.empty () && strad.empty ()) { pl.why = "empty"; return pl; }
	if (pl.f < BRK_TEAR_FMIN || pl.f > BRK_TEAR_FMAX) { pl.why = "area"; return pl; }
	double Rmax = tip ? std::min (3.0 * sp.R, 0.45 * L) : BRK_TEAR_RX * L;
	if (!(R > 0) || R > Rmax) { pl.why = "radius"; return pl; }
	DentRecord &r = pl.rec;
	r.slot = h.mesh, r.key = sl.key, r.ngrp = sl.ngrp, r.nvtx = sl.nvtx;
	r.grp = strad;
	std::sort (r.grp.begin (), r.grp.end ());
	DentParams &p = r.p;
	p.mode = DENTM_CUT, p.c = ctr, p.n = n, p.t = DentMath::Tangent (n, tdir, h.vt), p.R = R, p.h = 0, p.T = 0;
	p.P = Aj, p.hd = wc, p.hz = std::min (0.1 * R, 0.4), p.bits = 0;
	const VesselDamageA *vd = s.Damage (id);
	p.seed = Hash (event, vd ? (uint32_t)vd->d.rec.size () : 0u) | 1u;
	DentMath::Quantise (p);
	pl.ok = true;
	return pl;
}

bool CollBreakA::MakeTearSpawn (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const std::vector<uint16_t> &front, const std::vector<uint16_t> &straddle, const DentRecord &cut, uint32_t event, CollSpawnA &sp)
{
	uint32_t mesh = hit.mesh;
	size_t ng = sl.v.size ();
	Vector ofs = sdk.MeshOffset (vh, mesh);
	std::vector<CollAffine> F = Poses (id, mesh, ng);
	CollAffine Fs = StaticPose (sl, F);
	Vector cutC = CollApply (Fs, cut.p.c), cutN = Unit (CollApplyDir (Fs, cut.p.n)); // M2: the cut in the mesh frame
	DentDebris &d = sp.row;
	d.id = ++debrisSeq; d.slot = mesh; d.key = sl.key; d.ngrp = sl.ngrp; d.nvtx = sl.nvtx; d.simt = hit.simt;
	std::vector<uint16_t> in;
	auto ok = [&] (uint16_t g) { return g < ng && !sl.keep.count (g) && !(sl.usr[g] & 0x2) && (g >= sl.tier.size () || sl.tier[g] == CBRK_PART); }; // no glass, interior or rest-hidden groups
	for (uint16_t g : front) if (ok (g)) in.push_back (g);
	for (uint16_t g : straddle) if (ok (g)) in.push_back (g);
	if (in.empty ()) return false;
	Vector cd; double A = 0, Afront = 0;
	for (uint16_t g : in) {
		bool fr = std::find (front.begin (), front.end (), g) != front.end ();
		for (size_t t = 0; t + 2 < sl.idx[g].size (); t += 3) {
			size_t a = sl.idx[g][t], b = sl.idx[g][t + 1], c = sl.idx[g][t + 2];
			if (a >= sl.v[g].size () || b >= sl.v[g].size () || c >= sl.v[g].size ()) continue;
			Vector pa = mul (F[g].A, P (sl.v[g][a])) + F[g].t, pb = mul (F[g].A, P (sl.v[g][b])) + F[g].t, pc = mul (F[g].A, P (sl.v[g][c])) + F[g].t;
			Vector m = (pa + pb + pc) / 3.0;
			if (!fr && ((m - cutC) & cutN) <= 0) continue;
			double ar = 0.5 * crossp (pb - pa, pc - pa).length ();
			cd += m * ar; A += ar;
			Afront += ar;
		}
	}
	if (!(A > 0)) return false;
	cd /= A;
	std::map<std::string, size_t> poseIdx;
	double rmax = 0;
	for (uint16_t g : in) {
		std::string k ((const char *)&F[g], sizeof (CollAffine));
		auto it = poseIdx.find (k);
		if (it == poseIdx.end ()) {
			DentDebrisPose ps;
			MatQ (F[g].A, ps.q);
			ps.p = F[g].t - cd;
			ps.p = Vector (Q9 (ps.p.x), Q9 (ps.p.y), Q9 (ps.p.z));
			for (double &x : ps.q) x = Q9 (x);
			it = poseIdx.emplace (k, d.pose.size ()).first;
			d.pose.push_back (ps);
		}
		d.pose[it->second].grp.push_back (g);
	}
	for (auto &ps : d.pose) std::sort (ps.grp.begin (), ps.grp.end ());
	if (const VesselDamageA *vd = s.Damage (id)) for (auto &r : vd->d.rec) {
		if (r.slot != mesh || r.key != sl.key || (r.p.mode == DENTM_CUT && r.p.seed == cut.p.seed && r.p.R == cut.p.R && r.p.c.x == cut.p.c.x && r.p.c.y == cut.p.c.y && r.p.c.z == cut.p.c.z)) continue;
		bool any = false;
		for (uint16_t g : in) if (Lists (r, g)) any = true;
		if (any) d.rec.push_back (r);
	}
	DentRecord kc = cut;
	kc.p.bits |= DENTC_KEEP;
	d.rec.push_back (kc);
	for (auto &ps : d.pose) for (uint16_t g : ps.grp) for (auto &v : PieceVertices (sl.v[g], g, ps, d.rec, s.Sites (id, mesh))) rmax = std::max (rmax, Vector (v.x, v.y, v.z).length ()); // M1: post-cut extent
	CollH ph = s.VesselHandle (id);
	double M = ph ? sdk.EmptyMass (ph) : 0;
	double m = sl.area > 0 ? M * Afront / sl.area : 0;
	d.mass = Q9 (std::max (1.0, std::min (m, BRK_TEAR_MMAX * M)));
	sp.parent = id; sp.other = hit.other; sp.event = event; sp.mesh = sl.name;
	d.other = IdName (hit.other);
	sp.cv = cd + ofs;
	sp.mass = d.mass;
	Vector n = cutN, t = Unit (CollApplyDir (Fs, cut.p.t)), e = crossp (n, t);
	const Vector dir[4] = { t, t * -1.0, e, e * -1.0 };
	uint32_t hh = Hash (id, event);
	int k = (int)(hh & 3);
	if (hit.vt > 0.2 * hit.vn) { double best = -1e300; for (int i = 0; i < 4; i++) { double x = dir[i] & hit.tdir; if (x > best) best = x, k = i; } }
	Vector u = dir[k];
	double jit = 0.8 + 0.4 * (hh % 1000) / 999.0;
	sp.dv = u * (BRK_TEAR_KICK * hit.vn * jit);
	Vector w = crossp (n, u) * (sp.dv.length () / (2.0 * std::max (rmax, 0.1)));
	double wl = w.length ();
	if (wl > 2 * 3.14159265358979) w *= 2 * 3.14159265358979 / wl;
	sp.dw = w;
	return true;
}

bool CollBreakA::Section (uint32_t id, CollH vh, const CollDamageHit &hit, const CollSlotA &sl, const CollCutPlan &pl, uint32_t event)
{
	VesB &b = ves[id];
	uint32_t mesh = hit.mesh;
	if (!s.AddCut (id, pl.rec, false)) return false;
	tears++;
	bool canDebris = cfg.visuals && s.vis.Mode () != CollVisualA::MODE_OFF;
	if (canDebris && cfgOk < 0) cfgOk = sdk.DebrisClassExists () ? 1 : 0;
	if (canDebris && !cfgOk) { if (!loggedNoCfg) Log ("Collision: Config/Vessels/CollDebris.cfg missing: parts are hidden without debris"); loggedNoCfg = true; canDebris = false; }
	std::string name = "-";
	CollSpawnA sp;
	if (canDebris && MakeTearSpawn (id, vh, hit, sl, pl.front, pl.straddle, pl.rec, event, sp)) {
		sp.row.name = name = NewName (sdk.Name (vh));
		spawn.push_back (sp);
		SyncRows (sp.parent);
	}
	DentTorn t;
	t.kind = (uint8_t)CBRK_SECTION; t.slot = mesh; t.key = sl.key; t.ngrp = sl.ngrp; t.nvtx = sl.nvtx; t.simt = hit.simt; t.debris = name;
	t.grp = pl.front;
	std::sort (t.grp.begin (), t.grp.end ());
	if (name != "-") t.kin = true, t.dv = sp.dv, t.dw = sp.dw, t.mass = sp.mass; // M3: playback uses the live kick
	std::string gl;
	for (uint16_t g : t.grp) gl += (gl.empty () ? "" : ",") + std::to_string (g);
	if (!t.grp.empty ()) {
		b.rows.push_back (t);
		s.AddTorn (id, t); b.adopted++;
		if (b.sh) for (uint16_t g : t.grp) b.sh->SetGroupHidden (mesh, g, true);
	}
	Log ("Collision tear '%s' slot=%u d=%.4g f=%.4g R=%.4g groups=%s debris=%s eSpec=%.4g vn=%.4g", sdk.Name (vh).c_str (), mesh, pl.d, pl.f, pl.rec.p.R, gl.empty () ? "-" : gl.c_str (), name.c_str (), hit.eSpec, hit.vn);
	CollBreakEvent ev;
	ev.id = id; ev.kind = CBRK_SECTION; ev.c = pl.rec.p.c + sdk.MeshOffset (vh, mesh); ev.n = hit.n; ev.r = pl.rec.p.R; ev.playback = false;
	if (s.fx) s.fx->Break (ev);
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
	std::map<size_t, size_t> spIdx;
	for (size_t i = 0; i < pk.size (); i++) {
		if (uf.F (i) != i || sl.piece[pk[i]].tier != CBRK_PART || !canDebris || made >= BRK_PER_EVENT) continue;
		std::vector<int> cl;
		for (size_t j = 0; j < pk.size (); j++) if (uf.F (j) == i) cl.push_back (pk[j]);
		CollSpawnA sp;
		if (!MakeSpawn (id, vh, hit, sl, cl, event, sp)) continue;
		sp.row.name = NewName (sdk.Name (vh));
		nameOf[i] = sp.row.name;
		spIdx[i] = spawn.size ();
		spawn.push_back (sp);
		SyncRows (sp.parent);
		made++;
	}
	for (size_t i = 0; i < pk.size (); i++) {
		const CollPieceA &p = sl.piece[pk[i]];
		DentTorn t;
		t.kind = (uint8_t)p.tier; t.slot = mesh; t.key = sl.key; t.ngrp = sl.ngrp; t.nvtx = sl.nvtx; t.simt = hit.simt;
		auto nm = nameOf.find (uf.F (i));
		t.debris = nm == nameOf.end () ? "-" : nm->second;
		auto si = spIdx.find (uf.F (i));
		if (si != spIdx.end ()) { const CollSpawnA &q = spawn[si->second]; t.kin = true, t.dv = q.dv, t.dw = q.dw, t.mass = q.mass; } // playback uses the live kick
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

bool CollBreakA::BuildMesh (CollH mesh, const DentDebris &d, Geo &geo, uint32_t parent)
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
		std::vector<DentVtx> v = PieceVertices (rest, g, ps, d.rec, s.Sites (parent, d.slot));
		sdk.MeshEdit (mesh, g, 0, v.data (), (uint32_t)v.size ());
		geo.emplace_back (std::move (v), std::vector<uint16_t> (t.idx, t.idx + t.nidx));
	}
	return true;
}

CollSdk::DebrisCaps CollBreakA::Caps (const Geo &geo, double mass, uint32_t *fnv)
{
	CollSdk::DebrisCaps c;
	Vector lo (1e300, 1e300, 1e300), hi (-1e300, -1e300, -1e300), S, cs; double A = 0, sz = 0;
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
			Vector m = pa + pb + pd;
			S += (Vector (pa.x * pa.x + pb.x * pb.x + pd.x * pd.x, pa.y * pa.y + pb.y * pb.y + pd.y * pd.y, pa.z * pa.z + pb.z * pb.z + pd.z * pd.z) + Vector (m.x * m.x, m.y * m.y, m.z * m.z)) * (ar / 12); // second moment: A/12 (sum vi vi^T + s s^T)
			cs += Vector (std::fabs (N.x), std::fabs (N.y), std::fabs (N.z)) * 0.5;
			A += ar;
		}
	}
	if (fnv) *fnv = h;
	if (geo.empty () || !(sz > 0)) sz = 0.5;
	double fl = (0.05 * sz) * (0.05 * sz);
	c.size = std::max (sz, 0.1); c.mass = mass;
	c.pmi = A > 0 ? Vector (S.y + S.z, S.x + S.z, S.x + S.y) / A : Vector (fl, fl, fl);
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

void CollBreakA::Spawn (CollSpawnA &sp, double simt, std::map<uint32_t, CollParentA> &pc)
{
	CollH vh = s.VesselHandle (sp.parent);
	if (!vh) { SyncRows (sp.parent); return; } // every failed spawn drops its queued row
	for (uint32_t i = 0, c = sdk.VesselCount (); i < c; i++) if (CollKey::IEqual (sdk.Name (sdk.Vessel (i)), sp.row.name)) { sp.row.name = NewName (sdk.Name (vh)); break; }
	CollH mesh = sdk.MeshLoad (sp.mesh.c_str ());
	if (!mesh) { Log ("Collision: debris mesh '%s' not loaded, no debris", sp.mesh.c_str ()); SyncRows (sp.parent); return; }
	Geo geo;
	if (!BuildMesh (mesh, sp.row, geo, sp.parent)) { sdk.MeshFree (mesh); Log ("Collision: debris mesh '%s' does not match its slot, no debris", sp.mesh.c_str ()); SyncRows (sp.parent); return; }
	uint32_t fnv = 0;
	CollSdk::DebrisCaps caps = Caps (geo, sp.mass, &fnv);
	CollParentA &P = pc[sp.parent];
	if (!P.read) { // the parent as it was before any kick of this pre-step: every debris starts from it
		sdk.ReadVessel (vh, P.rd, CVR_NOWEIGHT);
		sdk.RelState (vh, P.rd.gref, P.rp, P.rv); // the core's own relative state
		P.M = P.rd.m > 0 ? P.rd.m : sdk.EmptyMass (vh);
		P.read = true;
	}
	const CollVesselRead &rd = P.rd;
	CollStateWrite st;
	st.rbody = rd.gref;
	st.rpos = P.rp + mul (rd.R, sp.cv);
	st.rvel = P.rv + mul (rd.R, crossp (sp.cv, rd.w) + sp.dv);
	st.vrot = rd.w + sp.dw; st.arot = Vector ();
	CollH h = sdk.VesselCreate (sp.row.name.c_str (), BRK_CLASS, st);
	if (!h) { sdk.MeshFree (mesh); Log ("Collision: debris vessel '%s' not created", sp.row.name.c_str ()); SyncRows (sp.parent); return; }
	sdk.DebrisSetup (h, mesh, caps);
	sdk.SetAttitude (h, rd.R);
	sdk.SetSpin (h, rd.w + sp.dw);
	if (sp.blast) { // blast: the parent takes -m dv and the debris spin, summed over this pre-step's debris and written once
		const CollVesselRead &pr = P.rd;
		double M = P.M;
		Vector J = (sp.dv + crossp (sp.cv, rd.w)) * caps.mass;              // vessel frame: the debris keeps the parent's rotation velocity at its centroid
		Vector H = crossp (sp.dv, sp.cv) * caps.mass + Vector (caps.pmi.x * sp.dw.x, caps.pmi.y * sp.dw.y, caps.pmi.z * sp.dw.z) * caps.mass; // Orbiter convention: H = m crossp (v, r) + m pmi w
		P.J += J; P.H += H; P.jf.push_back ({ J, sp.cv });
		Vector dvp = M > 0 ? J * (-1.0 / M) : Vector ();
		Vector dwp (pr.pmi.x > 0 && M > 0 ? -H.x / (M * pr.pmi.x) : 0, pr.pmi.y > 0 && M > 0 ? -H.y / (M * pr.pmi.y) : 0, pr.pmi.z > 0 && M > 0 ? -H.z / (M * pr.pmi.z) : 0);
		if (kicks.size () < 256) kicks.push_back ({ sp.parent, dvp, dwp, M });
	}
	CollDebrisA d;
	d.h = h; d.mesh = mesh; d.parent = sp.parent; d.other = sp.other; d.event = sp.event; d.birth = simt; d.row = sp.row; d.fnv = fnv;
	d.id = FindId (h);
	live.push_back (d);
	AddPairs (d, simt);
	Log ("Collision debris '%s' from '%s' slot=%u mass=%.4g fnv=%08x", d.row.name.c_str (), sdk.Name (vh).c_str (), d.row.slot, sp.mass, fnv);
	SyncRows (sp.parent);
}

void CollBreakA::AddPairs (const CollDebrisA &d, double simt)
{
	if (d.id == ~0u) return;
	auto add = [&] (uint32_t a) { if (a == d.id || !s.VesselHandle (a)) return; pairs.push_back ({ std::min (a, d.id), std::max (a, d.id), simt, d.id, d.restored }); s.noPair[{ std::min (a, d.id), std::max (a, d.id) }] = simt; };
	add (d.parent);
	if (d.other) add (d.other);
	for (auto &o : live) if (o.id != ~0u && o.id != d.id) add (o.id); // debris do not collide with other debris
}

std::string CollBreakA::IdName (uint32_t id)
{
	CollH h = id ? s.VesselHandle (id) : nullptr;
	return h ? sdk.Name (h) : std::string ();
}

uint32_t CollBreakA::NameId (const std::string &name)
{
	if (name.empty ()) return 0;
	uint32_t hv = 0;
	bool hashed = DentMath::NameHash (name, hv);
	for (uint32_t i = 0, c = sdk.VesselCount (); i < c; i++) {
		CollH x = sdk.Vessel (i);
		std::string n = sdk.Name (x);
		if (CollKey::IEqual (n, name) || (hashed && DentMath::Fnv1a (n.data (), n.size ()) == hv)) { uint32_t id = FindId (x); return id == ~0u ? 0 : id; }
	}
	return 0;
}

void CollBreakA::SyncRows (uint32_t parent)
{
	if (!s.VesselHandle (parent)) return;
	std::vector<DentDebris> r;
	for (auto &d : live) if (d.parent == parent) r.push_back (d.row);
	for (auto &x : spawn) if (x.parent == parent) r.push_back (x.row); // queued for the next pre-step: a save in between keeps them
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
		AddPairs (d, sdk.SimTime ());
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
	if (t.kind == CBRK_CELL) { // blast cell debris (recorder only): no hidden groups; playback spawns it with the recorded kick and mass
		CollH vh = s.VesselHandle (id);
		if (!vh || !sdk.Playback (vh) || !t.kin || t.debris == "-" || (t.cells.empty () && t.pieces.empty ()) || !cfg.debrisPlayback || !cfg.visuals || s.vis.Mode () == CollVisualA::MODE_OFF) return;
		const CollSlotA *sl = Slot (id, t.slot);
		if (!sl || !sl->ok || sl->key != t.key) return;
		if (cfgOk < 0) cfgOk = sdk.DebrisClassExists () ? 1 : 0;
		if (!cfgOk) return;
		std::vector<Vector> site;
		if (const DentSites *ds = s.Sites (id, t.slot)) site = ds->s;
		CollBlastBreak bk;
		bk.id = id; bk.slot = t.slot; bk.other = b.haveLast ? b.last.other : 0; bk.simt = sdk.SimTime ();
		bk.cells = t.cells; bk.pieces = t.pieces; bk.mass = t.mass; bk.centroid = t.c; bk.dv = t.dv; bk.dw = t.dw; bk.crushed = t.crushed;
		CollSpawnA sp;
		if (!MakeCellSpawn (bk, *sl, vh, site, StatGroups (*sl), ++events, sp)) return;
		sp.blast = false; // the recording moves the parent
		sp.row.name = NewName (sdk.Name (vh));
		played[{ id, t.debris }] = PlayedA { spawn.size (), {}, true };
		spawn.push_back (sp);
		SyncRows (sp.parent);
		return;
	}
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
	if (t.kind == CBRK_SECTION) { // dmg3 tear: playback rebuilds the section from the T row and its cut record, no gate math
		const VesselDamageA *vd = s.Damage (id);
		const DentRecord *cut = nullptr;
		if (vd) for (auto &r : vd->d.rec) if (r.slot == t.slot && r.p.mode == DENTM_CUT && !(r.p.bits & DENTC_KEEP)) cut = &r;
		if (!cut || !sl || !sl->ok || t.debris == "-" || !cfg.debrisPlayback || !cfg.visuals || s.vis.Mode () == CollVisualA::MODE_OFF) return;
		if (cfgOk < 0) cfgOk = sdk.DebrisClassExists () ? 1 : 0;
		if (!cfgOk) return;
		CollSpawnA sp;
		if (!MakeTearSpawn (id, vh, hit, *sl, t.grp, cut->grp, *cut, ++events, sp)) return;
		if (t.kin) sp.dv = t.dv, sp.dw = t.dw, sp.mass = sp.row.mass = t.mass; // M3: the recorded kick and mass
		sp.row.name = NewName (sdk.Name (vh));
		spawn.push_back (sp);
		SyncRows (sp.parent);
		return;
	}
	for (int k : pk) {
		CollBreakEvent ev;
		ev.id = id; ev.kind = t.kind; ev.c = sl->piece[k].c + sdk.MeshOffset (vh, t.slot); ev.n = hit.n; ev.r = sl->piece[k].r; ev.playback = true;
		if (s.fx) s.fx->Break (ev);
	}
	if (!cfg.debrisPlayback || t.debris == "-" || t.kind != CBRK_PART || pk.empty () || !cfg.visuals || s.vis.Mode () == CollVisualA::MODE_OFF) return;
	if (cfgOk < 0) cfgOk = sdk.DebrisClassExists () ? 1 : 0;
	if (!cfgOk) return;
	auto pl = played.find ({ id, t.debris });
	if (pl != played.end () && (pl->second.cell || pl->second.idx >= spawn.size ())) return; // its cell row spawned it
	std::vector<int> all = pk;
	if (pl != played.end ()) for (int k : pl->second.pk) if (std::find (all.begin (), all.end (), k) == all.end ()) all.push_back (k); // rows of one debris: one spawn
	CollSpawnA sp;
	if (!MakeSpawn (id, vh, hit, *sl, all, ++events, sp) && !(t.kin && !sp.row.pose.empty ())) return; // with its kick recorded the live run made it
	if (t.kin) sp.dv = t.dv, sp.dw = t.dw, sp.mass = sp.row.mass = t.mass; // the recorded kick and mass
	if (pl != played.end ()) { sp.row.name = spawn[pl->second.idx].row.name; spawn[pl->second.idx] = sp; pl->second.pk = all; }
	else {
		sp.row.name = NewName (sdk.Name (vh));
		played[{ id, t.debris }] = PlayedA { spawn.size (), all, false };
		spawn.push_back (sp);
	}
	SyncRows (sp.parent);
}

std::vector<uint16_t> CollBreakA::StatGroups (const CollSlotA &sl)
{
	std::vector<uint16_t> stat;
	for (size_t g = 0; g < sl.v.size () && g < sl.cls.size () && g < sl.tier.size (); g++)
		if (sl.idx[g].size () >= 3 && !sl.keep.count ((uint16_t)g) && !(sl.usr[g] & 0x2) && sl.cls[g] == sl.staticCls && (sl.tier[g] == CBRK_PART || sl.tier[g] == CBRK_GLASS)) stat.push_back ((uint16_t)g);
	return stat;
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
	auto mc = massCut.find (id);
	if (mc != massCut.end ()) { // repair: the cut mass and inertia come back
		if (vh && mc->second.cut > 0) {
			CollVesselRead rd {};
			sdk.ReadVessel (vh, rd, CVR_NOWEIGHT);
			double E = sdk.EmptyMass (vh), Er = E + mc->second.cut, M = rd.m > 0 ? rd.m : E, Mr = M + mc->second.cut;
			sdk.SetEmptyMass (vh, Er);
			sdk.SetPMI (vh, (rd.pmi * M + mc->second.icut) * (1.0 / Mr)); // I = M pmi additively: exact while the fuel is unchanged
			Log ("Collision blast '%s' empty mass restored %.6g kg", sdk.Name (vh).c_str (), Er);
		}
		massCut.erase (mc);
	}
	for (auto bi = blast.begin (); bi != blast.end ();) { if (bi->first.first == id) bi = blast.erase (bi); else ++bi; }
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
	ves.erase (id); massCut.erase (id); massPending.erase (id);
	for (auto it = slots.begin (); it != slots.end ();) { if (it->first.first == id) it = slots.erase (it); else ++it; }
	for (auto bi = blast.begin (); bi != blast.end ();) { if (bi->first.first == id) bi = blast.erase (bi); else ++bi; }
}

void CollBreakA::PairCheck (double simt)
{
	std::vector<uint32_t> stuck;
	for (size_t k = pairs.size (); k-- > 0;) {
		CollPairA p = pairs[k];
		CollH ha = s.VesselHandle (p.a), hb = s.VesselHandle (p.b);
		if (!ha || !hb) { s.noPair.erase ({ p.a, p.b }); pairs.erase (pairs.begin () + (long)k); continue; }
		if (p.sep) continue;
		Vector pa, pb, v; Matrix R;
		sdk.GlobalState (ha, pa, v, R); sdk.GlobalState (hb, pb, v, R);
		auto rad = [&] (uint32_t id, CollH h) { auto it = ves.find (id); Vector c; double r = 0; if (it != ves.end () && it->second.sh) { it->second.sh->Bound (1, c, r); if (r > 0) return r; } return sdk.Size (h); };
		double gap = (pa - pb).length () - rad (p.a, ha) - rad (p.b, hb);
		if (gap > BRK_SEP) { pairs[k].sep = true; continue; } // apart: no stuck check; the filter stays, a slow piece grazing its own wreck costs and adds nothing
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
			uint32_t hv = 0;
			bool hashed = DentMath::NameHash (row.name, hv);
			if (!h && hashed) for (uint32_t i = 0, c = sdk.VesselCount (); i < c && !h; i++) { // a long name saved as its hash: the live debris whose name hashes to it
				CollH x = sdk.Vessel (i);
				std::string n = sdk.Name (x);
				bool ours = claimed.count (n) > 0;
				for (auto &d : live) if (d.h == x) ours = true;
				if (!ours && CollKey::IEqual (sdk.ClassName (x), BRK_CLASS) && DentMath::Fnv1a (n.data (), n.size ()) == hv) h = x, row.name = n;
			}
			if (!h) { // saved between the post-step that queued it and the pre-step that spawns it: spawn it now from the row
				bool queued = false;
				for (auto &x : spawn) if (x.parent == pid && x.row.id == row.id) queued = true;
				if (queued) continue;
				const CollSlotA *sl = vh ? Slot (pid, row.slot) : nullptr;
				if (!sl || !sl->ok || sl->key != row.key || row.pose.empty () || row.pose[0].grp.empty () || row.pose[0].grp[0] >= sl->v.size ()) { Log ("Collision: debris row '%s' without its vessel dropped", row.name.c_str ()); continue; }
				std::vector<CollAffine> F = Poses (pid, row.slot, sl->v.size ());
				CollSpawnA sp;
				sp.parent = pid; sp.other = NameId (row.other); sp.event = ++events; sp.mesh = sl->name; sp.row = row; sp.mass = row.mass;
				if (hashed) sp.row.name = NewName (sdk.Name (vh)); // never a vessel named after the hash
				sp.cv = F[row.pose[0].grp[0]].t - row.pose[0].p + sdk.MeshOffset (vh, row.slot); // the piece centroid as the row placed it
				spawn.push_back (sp);
				Log ("Collision: debris row '%s' without its vessel spawned again", row.name.c_str ());
				continue;
			}
			if (!CollKey::IEqual (sdk.ClassName (h), BRK_CLASS)) continue;
			claimed.insert (row.name);
			const CollSlotA *sl = vh ? Slot (pid, row.slot) : nullptr;
			CollH mesh = sl && sl->ok && sl->key == row.key ? sdk.MeshLoad (sl->name.c_str ()) : nullptr;
			Geo geo;
			if (!mesh || !BuildMesh (mesh, row, geo, pid)) {
				if (mesh) sdk.MeshFree (mesh);
				Log ("Collision: debris '%s' mesh missing or changed, deleted", row.name.c_str ());
				sdk.VesselDelete (h);
				continue;
			}
			std::vector<int> pk;
			for (auto &ps : row.pose) for (uint16_t g : ps.grp) if (g < sl->pieceOf.size () && sl->pieceOf[g] >= 0 && std::find (pk.begin (), pk.end (), sl->pieceOf[g]) == pk.end ()) pk.push_back (sl->pieceOf[g]);
			uint32_t fnv = 0;
			CollSdk::DebrisCaps caps = Caps (geo, row.mass > 0 ? row.mass : Mass (pid, *sl, pk), &fnv); // dmg3 tear: the B row mass
			sdk.DebrisSetup (h, mesh, caps);
			CollDebrisA d;
			d.h = h; d.mesh = mesh; d.parent = pid; d.other = NameId (row.other); d.birth = std::min (row.simt, simt); d.row = row; d.fnv = fnv; d.id = FindId (h); d.restored = true; // birth: sim time restarts on load
			live.push_back (d);
			AddPairs (live.back (), simt); // the filters are not saved: parent, impactor and other debris again
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

void CollBreakA::Post (double simt, double simdt)
{
	if (quiet) return;
	postDt = simdt;
	Boot (simt);
	Adopt ();
	if (!massPending.empty ()) LoadMass ();
	if (cfg.blast) for (auto &kv : blast) { // blast: spin loads only for slots hit within BLAST_LIVE
		CollBlastSlotA &bs = kv.second;
		if (!bs.b || !(simt - bs.lastHit <= BLAST_LIVE) || simt <= bs.lastHit) continue;
		CollH vh = s.VesselHandle (kv.first.first);
		if (!vh || sdk.Playback (vh)) continue;
		BlastStep (kv.first.first, kv.first.second, bs, vh, false);
	}
	BlastRebuild ();
	for (size_t i = freeMesh.size (); i-- > 0;) if (freeMesh[i].dropped) { if (freeMesh[i].mesh) sdk.MeshFree (freeMesh[i].mesh); freeMesh.erase (freeMesh.begin () + (long)i); }
	for (auto &kv : ves) Assert (kv.first, kv.second);
	PairCheck (simt);
	for (size_t i = live.size (); i-- > 0;) if (simt - live[i].birth > cfg.debrisLife) Kill (i, "life");
	while (live.size () > (size_t)std::max (0, cfg.debrisMax)) Kill (0, "cap");
}

void CollBreakA::Boot (double simt)
{
	if (rebuilt) return; // first pass after load, pre-step or post-step: debris rows rebuilt before any new spawn
	rebuilt = true;
	Rebuild (simt);
	Adopt ();
	for (auto &kv : s.Vessels ()) if (!kv.second.d.sites.empty ()) massPending.insert (kv.first);
}

void CollBreakA::PreStep (double simt, double simdt)
{
	if (quiet) return;
	preDt = simdt;
	Boot (simt);
	std::vector<CollSpawnA> sp;
	sp.swap (spawn);
	played.clear ();
	std::map<uint32_t, CollParentA> pc;
	for (auto &x : sp) Spawn (x, simt, pc); // in the pre-step: the core places the new vessel and the parent's write where they were read
	for (auto &kv : pc) { // one write per parent: the sum of its debris' impulses
		CollParentA &P = kv.second;
		CollH vh = s.VesselHandle (kv.first);
		if (!vh || P.jf.empty () || !(P.M > 0)) continue;
		const CollVesselRead &pr = P.rd;
		if (pr.sv) { for (auto &f : P.jf) sdk.AddForce (vh, f.first * (-1.0 / (simdt > 0 ? simdt : 1.0 / 60)), f.second); continue; } // docked stack: the stack takes each impulse over this step
		Vector dvp = P.J * (-1.0 / P.M);
		Vector dwp (pr.pmi.x > 0 ? -P.H.x / (P.M * pr.pmi.x) : 0, pr.pmi.y > 0 ? -P.H.y / (P.M * pr.pmi.y) : 0, pr.pmi.z > 0 ? -P.H.z / (P.M * pr.pmi.z) : 0);
		CollStateWrite ps {};
		ps.rbody = pr.gref;
		ps.rpos = P.rp; ps.rvel = P.rv + mul (pr.R, dvp); ps.vrot = pr.w + dwp;
		ps.arot = Vector (std::atan2 (pr.R (1, 2), pr.R (2, 2)), -std::asin (std::max (-1.0, std::min (1.0, pr.R (0, 2)))), std::atan2 (pr.R (0, 1), pr.R (0, 0))); // inverse of Vessel::SetGlobalOrientation
		sdk.SetState (vh, ps); sdk.SetAttitude (vh, pr.R); sdk.SetSpin (vh, pr.w + dwp);
	}
}

void CollBreakA::End ()
{
	if (!quiet) {
		for (auto &d : live) if (d.mesh) sdk.MeshFree (d.mesh);
		for (auto &f : freeMesh) if (f.mesh) sdk.MeshFree (f.mesh);
	}
	live.clear (); freeMesh.clear (); spawn.clear (); played.clear (); pairs.clear (); ves.clear (); slots.clear (); blast.clear (); massCut.clear (); massPending.clear ();
	rebuilt = false; cfgOk = -1; loggedNoCfg = false;
}

// blast: cells, stress and debris from cells (design-CA-blast 2, 4, 5)

static Vector Orb (const Vector &p) { return Vector (p.y * p.y + p.z * p.z, p.x * p.x + p.z * p.z, p.x * p.x + p.y * p.y); } // point-mass inertia diagonal per kg

double CollBreakA::CutMass (uint32_t id, CollH vh, double m, const Vector &icut)
{
	if (!vh || !(m > 0)) return 0;
	MassCutA &mc = massCut[id];
	CollVesselRead rd {};
	sdk.ReadVessel (vh, rd, CVR_NOWEIGHT);
	double E = sdk.EmptyMass (vh), M = rd.m > 0 ? rd.m : E; // Orbiter's inertia is the total mass times PMI
	if (!(mc.m0 > 0)) mc.m0 = E, mc.pmi0 = rd.pmi;
	if (!(mc.m0 > 0) || !(E > 0) || !(M > 0)) return 0;
	double dm = std::max (0.0, std::min ({ m, BLAST_MASS_CUT * mc.m0 - mc.cut, 0.9 * E })); // the main structure keeps at least a tenth
	if (!(dm > 0)) return 0;
	Vector di = icut * (dm / m);
	Vector p = (rd.pmi * M - di) * (1.0 / (M - dm));
	Vector f (std::max (p.x, 0.1 * mc.pmi0.x), std::max (p.y, 0.1 * mc.pmi0.y), std::max (p.z, 0.1 * mc.pmi0.z));
	if (f.x != p.x || f.y != p.y || f.z != p.z) Log ("Collision blast '%s' PMI floor (%.4g %.4g %.4g) -> (%.4g %.4g %.4g)", sdk.Name (vh).c_str (), p.x, p.y, p.z, f.x, f.y, f.z);
	mc.cut += dm; mc.icut += rd.pmi * M - f * (M - dm); // the inertia really removed: Repair adds it back exactly
	sdk.SetEmptyMass (vh, E - dm); // differences only: the module's own mass changes stay
	sdk.SetPMI (vh, f);
	Log ("Collision blast '%s' empty mass %.6g -> %.6g kg", sdk.Name (vh).c_str (), E, E - dm);
	return dm;
}

void CollBreakA::LoadMass ()
{
	for (auto pi = massPending.begin (); pi != massPending.end ();) { // load: the parent's empty mass without the cells its VCUT records removed
		uint32_t id = *pi;
		const VesselDamageA *vd = s.Damage (id);
		CollH vh = s.VesselHandle (id);
		if (!vd || !vh) { pi = massPending.erase (pi); continue; }
		if (sdk.Playback (vh)) { ++pi; continue; } // retried every post-step until playback ends
		pi = massPending.erase (pi);
		double m = 0;
		Vector ic;
		for (auto &ds : vd->d.sites) {
			const CollSlotA *sl = Slot (id, ds.slot);
			if (!sl || !sl->ok || sl->key != ds.key) continue;
			CollBlastSlotA *bs = BlastSlot (id, ds.slot, vh, *sl);
			if (!bs) continue;
			std::vector<CollAffine> F = Poses (id, ds.slot, sl->v.size ());
			CollAffine Fs = StaticPose (*sl, F);
			Vector ofs = sdk.MeshOffset (vh, ds.slot);
			for (uint32_t c : bs->cut) { const CollBlastChunk &ch = bs->b->chunk[c]; m += ch.mass; ic += Orb (CollApply (Fs, ch.c) + ofs) * ch.mass; }
		}
		CutMass (id, vh, m, ic);
	}
}

double CollBreakA::BlastMass (uint32_t id, uint32_t mesh, CollH vh)
{
	auto it = massCut.find (id);
	double M = it != massCut.end () && it->second.m0 > 0 ? it->second.m0 : sdk.EmptyMass (vh); // before any cut: live and reload build the same chunks
	double A = 0, As = 0;
	for (uint32_t k = 0, n = sdk.MeshCount (vh); k < n; k++) { // the slot's share of the vessel's skin
		const CollSlotA *sl = Slot (id, k);
		if (!sl || !sl->ok) continue;
		double a = 0;
		for (size_t g = 0; g < sl->v.size () && g < sl->cls.size () && g < sl->tier.size (); g++) {
			if (sl->idx[g].size () < 3 || sl->keep.count ((uint16_t)g) || (sl->usr[g] & 0x2)) continue;
			int pc = g < sl->pieceOf.size () ? sl->pieceOf[g] : -1;
			if (!(sl->cls[g] == sl->staticCls && (sl->tier[g] == CBRK_PART || sl->tier[g] == CBRK_GLASS)) && !(pc >= 0 && sl->tier[g] == CBRK_PART && !sl->piece[pc].fixed)) continue;
			for (size_t t = 0; t + 2 < sl->idx[g].size (); t += 3) {
				size_t i = sl->idx[g][t], j = sl->idx[g][t + 1], l = sl->idx[g][t + 2];
				if (i >= sl->v[g].size () || j >= sl->v[g].size () || l >= sl->v[g].size ()) continue;
				Vector x = P (sl->v[g][i]), y = P (sl->v[g][j]), z = P (sl->v[g][l]);
				a += 0.5 * crossp (y - x, z - x).length ();
			}
		}
		A += a;
		if (k == mesh) As = a;
	}
	return A > 0 && As > 0 ? M * As / A : M;
}

CollBlastSlotA *CollBreakA::BlastSlot (uint32_t id, uint32_t mesh, CollH vh, const CollSlotA &sl)
{
	auto key = std::make_pair (id, mesh);
	auto it = blast.find (key);
	if (it != blast.end () && it->second.b && it->second.key == sl.key) return &it->second;
	CollBlastSlotA &bs = blast[key];
	bs = CollBlastSlotA ();
	bs.key = sl.key;
	size_t ng = sl.v.size ();
	std::vector<CollAffine> F = Poses (id, mesh, ng);
	CollAffine Fsi = CollInverse (StaticPose (sl, F));
	std::vector<std::vector<uint32_t>> weld;
	DentMath::WeldMap (sl.v, DENT_WELD, weld);
	CollBlastInput in;
	in.size = sdk.Size (vh); in.mass = BlastMass (id, mesh, vh); in.maxCells = cfg.blastCells;
	for (size_t g = 0; g < ng && g < sl.cls.size () && g < sl.tier.size () && g < weld.size (); g++) {
		if (sl.idx[g].size () < 3 || sl.keep.count ((uint16_t)g) || (sl.usr[g] & 0x2)) continue;
		int pc = g < sl.pieceOf.size () ? sl.pieceOf[g] : -1;
		int piece;
		if (sl.cls[g] == sl.staticCls && (sl.tier[g] == CBRK_PART || sl.tier[g] == CBRK_GLASS)) { piece = -1; bs.groups.push_back ((uint16_t)g); }
		else if (pc >= 0 && sl.tier[g] == CBRK_PART && !sl.piece[pc].fixed) piece = pc;
		else continue;
		for (size_t t = 0; t + 2 < sl.idx[g].size (); t += 3) {
			size_t a = sl.idx[g][t], b = sl.idx[g][t + 1], c = sl.idx[g][t + 2];
			if (a >= sl.v[g].size () || b >= sl.v[g].size () || c >= sl.v[g].size ()) continue;
			for (size_t k : { a, b, c }) { in.v.push_back (P (sl.v[g][k])); in.w.push_back (weld[g][k]); }
			in.piece.push_back (piece);
		}
	}
	const DentSites *ds = s.Sites (id, mesh);
	bool given = ds && ds->key == sl.key && !ds->s.empty ();
	bs.b.reset (new CollBlastA ());
	if (!bs.b->Build (in, given ? &ds->s : nullptr, CollApply (Fsi, Vector () - sdk.MeshOffset (vh, mesh)))) { blast.erase (key); return nullptr; }
	if (!given) { DentSites x; x.slot = mesh; x.key = sl.key; x.s = bs.b->site; s.SetSites (id, x); }
	std::vector<uint32_t> removed, bonds;
	if (const VesselDamageA *vd = s.Damage (id)) for (auto &r : vd->d.rec) // cells cut away by VCUT records
		if (r.slot == mesh && r.key == sl.key && r.p.mode == DENTM_VCUT && !(r.p.bits & DENTC_KEEP) && r.p.P >= 0) { int c = bs.b->ChunkOfCell ((uint32_t)r.p.P); if (c >= 0) removed.push_back ((uint32_t)c); }
	for (size_t c = 0; c < bs.b->chunk.size (); c++) { // animated pieces torn off
		int pc = bs.b->chunk[c].piece;
		if (pc < 0) continue;
		bool all = true;
		for (uint16_t g : sl.piece[pc].grp) if (!sl.keep.count (g) && !(sl.usr[g] & 0x2) && !Hidden (id, mesh, g)) all = false;
		if (all) removed.push_back ((uint32_t)c);
	}
	if (const std::vector<uint32_t> *kb = s.BrokenBonds (id, mesh)) bonds = bs.b->BondsOfPairs (*kb); // K rows hold chunk key pairs
	std::sort (removed.begin (), removed.end ()); removed.erase (std::unique (removed.begin (), removed.end ()), removed.end ());
	for (uint32_t c : removed) bs.cutMass += bs.b->chunk[c].mass;
	bs.cut = removed;
	std::vector<uint32_t> weak;
	if (const std::vector<uint32_t> *wb = s.WeakBonds (id, mesh)) weak = *wb;
	if (!removed.empty () || !bonds.empty () || !weak.empty ()) bs.b->Restore (bonds, removed, weak);
	bs.recorded = bs.b->BrokenPairs ();
	bs.weak = bs.b->WeakPairs ();
	Log ("Collision blast '%s' slot=%u cells=%zu chunks=%zu bonds=%zu t=%.4g restored=%zu/%zu", sdk.Name (vh).c_str (), mesh, bs.b->site.size (), bs.b->chunk.size (), bs.b->bond.size (), bs.b->t, removed.size (), bonds.size ());
	return &bs;
}

void CollBreakA::BlastHit (const CollDamageHit &h, CollH vh, const CollSlotA &sl)
{
	CollBlastSlotA *bs = BlastSlot (h.id, h.mesh, vh, sl);
	if (!bs) return;
	std::vector<CollAffine> F = Poses (h.id, h.mesh, sl.v.size ());
	CollAffine Fsi = CollInverse (StaticPose (sl, F));
	Vector ofs = sdk.MeshOffset (vh, h.mesh);
	Vector cr = CollApply (Fsi, h.c - ofs);
	double depth = h.depth;
	if (!(depth > 0) && h.rec >= 0) if (const VesselDamageA *vd = s.Damage (h.id)) if ((size_t)h.rec < vd->d.rec.size () && vd->d.rec[h.rec].p.mode == DENTM_CRUSH) depth = vd->d.rec[h.rec].p.P;
	double tau = std::max (h.vn > 0 && depth > 0 ? depth / h.vn : 0.0, BLAST_TAU_MIN); // physical contact time: frame rate and warp do not matter
	if (h.Jn > 0) bs->b->Force (cr, CollApplyDir (Fsi, Unit (h.n) * (-h.Jn / tau))); // contact force on this side: inward
	if (h.mat && h.mat->sigma_c > 0) { double k = std::max (0.1, std::min (10.0, h.mat->sigma_c / BLAST_SIGMA_C)); bs->b->Material (BLAST_SIGMA_Y * k, BLAST_SIGMA_U * k); }
	double dmg = cfg.blastESpec > 0 ? std::max (0.0, std::min (1.0, h.eSpec / cfg.blastESpec)) : 0.0;
	if (dmg > 0 && h.R > 0) bs->b->Impact (cr, h.R, dmg);
	if (h.rec >= 0 && h.vn >= BRK_VN_PART && h.grp >= 0 && (size_t)h.grp < F.size ()) if (const VesselDamageA *vd = s.Damage (h.id)) if ((size_t)h.rec < vd->d.rec.size ()) {
		const DentParams &p = vd->d.rec[h.rec].p; // the crush record of this event: skin crushed beyond the part ratio tears off
		if (p.mode == DENTM_CRUSH && p.P > 0 && p.R > 0) bs->b->Crush (CollApply (Fsi, mul (F[h.grp].A, p.c) + F[h.grp].t), CollApplyDir (Fsi, mul (F[h.grp].A, p.n)), p.P, p.R, BRK_RATIO_PART);
	}
	bs->hit = h; bs->haveHit = true; bs->lastHit = h.simt;
	BlastStep (h.id, h.mesh, *bs, vh, true);
	BlastRebuild ();
}

bool CollPieceHeld (const CollPieceA &p, const CollDamageHit &h)
{
	if (h.vn < BRK_VN_PART) return true;                            // parts need the approach speed of the dmg3 part gate
	return (p.functional & CBRK_FN_DOCK) && h.eSpec < BRK_TEAR_E;   // the dock pin holds below the tear threshold
}

void CollBreakA::BlastRebuild ()
{
	std::vector<std::pair<uint32_t, uint32_t>> keys;
	for (auto &kv : blast) if (kv.second.rebuild) keys.push_back (kv.first);
	for (auto &k : keys) {
		CollBlastSlotA &old = blast[k];
		CollDamageHit hit = old.hit;
		bool haveHit = old.haveHit;
		double lastHit = old.lastHit;
		std::set<uint32_t> held = old.held;
		blast.erase (k);
		blastRebuilds++;
		CollH vh = s.VesselHandle (k.first);
		const CollSlotA *sl = vh ? Slot (k.first, k.second) : nullptr;
		if (!sl || !sl->ok) continue;
		if (CollBlastSlotA *bs = BlastSlot (k.first, k.second, vh, *sl)) bs->hit = hit, bs->haveHit = haveHit, bs->lastHit = lastHit, bs->held = held;
	}
}

void CollBreakA::BlastStep (uint32_t id, uint32_t mesh, CollBlastSlotA &bs, CollH vh, bool hitNow)
{
	const CollSlotA *sl = Slot (id, mesh);
	if (!sl || !sl->ok || !bs.b || !bs.b->Ok ()) return;
	auto t0 = std::chrono::steady_clock::now ();
	std::vector<CollAffine> Fp = Poses (id, mesh, sl->v.size ());
	CollAffine Fs = StaticPose (*sl, Fp), Fsi = CollInverse (Fs);
	Vector ofs = sdk.MeshOffset (vh, mesh);
	CollVesselRead rd {};
	sdk.ReadVessel (vh, rd, CVR_NOWEIGHT);
	bs.b->Spin (CollApply (Fsi, Vector () - ofs), CollApplyDir (Fsi, rd.w));
	if (s.Room (id) < bs.b->site.size ()) { // no room for the cell records: the structure holds
		if (!bs.full) Log ("Collision blast '%s' slot=%u: record limit, no more breaks", sdk.Name (vh).c_str (), mesh);
		bs.full = true;
		return;
	}
	std::vector<uint32_t> w0 = bs.b->WeakPairs (), b0 = bs.b->BrokenPairs (); // before the step: held bonds keep this health
	std::vector<CollBlastSplit> sp = bs.b->Step ();
	std::vector<uint8_t> held (sp.size (), 0);
	std::set<uint32_t> hk; // chunk keys of held actors
	for (size_t i = 0; i < sp.size (); i++) {
		bool cells = false, hold = false;
		for (uint32_t c : sp[i].chunks) { const CollBlastChunk &ch = bs.b->chunk[c]; if (ch.cell >= 0) cells = true; else if (bs.haveHit && ch.piece >= 0 && (size_t)ch.piece < sl->piece.size () && CollPieceHeld (sl->piece[ch.piece], bs.hit)) hold = true; }
		if (cells || !hold) { for (uint32_t c : sp[i].chunks) bs.held.erase (bs.b->ChunkKey (c)); continue; } // torn off for good
		held[i] = 1;
		for (uint32_t c : sp[i].chunks) hk.insert (bs.b->ChunkKey (c));
	}
	bool fresh = false;
	for (uint32_t k : hk) if (!bs.held.count (k)) fresh = true;
	bs.held.insert (hk.begin (), hk.end ());
	auto isHeld = [&] (uint32_t p) { return bs.held.count (p / 65536u) || bs.held.count (p % 65536u); };
	std::vector<uint32_t> br = bs.b->BrokenPairs (), nb;
	if (!bs.held.empty ()) { // held pieces stay: their bonds are not stored as broken
		std::map<uint32_t, uint32_t> pre;
		for (size_t k = 0; k + 1 < w0.size (); k += 2) pre[w0[k]] = w0[k + 1];
		for (uint32_t p : br) if (isHeld (p) && !std::binary_search (b0.begin (), b0.end (), p)) { // broke in this step: the pre-step health, at least 1 % of the area
			auto it = pre.find (p);
			uint32_t q = std::max<uint32_t> (it == pre.end () ? 1000000u : it->second, 10000u);
			if (q < 1000000u) bs.heldW[p] = q; else bs.heldW.erase (p);
		}
		br.erase (std::remove_if (br.begin (), br.end (), isHeld), br.end ());
	}
	for (auto it = bs.heldW.begin (); it != bs.heldW.end ();) if (isHeld (it->first)) ++it; else it = bs.heldW.erase (it);
	if (fresh) { // a new held set: the slot is rebuilt from the stored state; the same set again stays split until then
		bs.rebuild = true;
		Log ("Collision blast '%s' slot=%u: %zu piece chunks held (vn=%.4g eSpec=%.4g)", sdk.Name (vh).c_str (), mesh, hk.size (), bs.hit.vn, bs.hit.eSpec);
	}
	std::set_difference (br.begin (), br.end (), bs.recorded.begin (), bs.recorded.end (), std::back_inserter (nb));
	if (!nb.empty ()) { s.AddBrokenBonds (id, mesh, nb); bs.recorded = br; }
	std::vector<uint32_t> wk = bs.b->WeakPairs ();
	if (!bs.heldW.empty ()) { // held bonds as W rows, by pair
		std::map<uint32_t, uint32_t> m;
		for (size_t k = 0; k + 1 < wk.size (); k += 2) m[wk[k]] = wk[k + 1];
		for (auto &x : bs.heldW) m[x.first] = x.second;
		wk.clear ();
		for (auto &x : m) wk.push_back (x.first), wk.push_back (x.second);
	}
	if (wk != bs.weak) { s.SetWeakBonds (id, mesh, wk); bs.weak.swap (wk); } // impact damage that did not break a bond is saved too
	blastSteps++;
	blastMs += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ();
	const CollDamageHit &h = bs.hit;
	for (size_t i = 0; i < sp.size (); i++) {
		if (held[i]) continue;
		const CollBlastSplit &x = sp[i];
		CollBlastBreak bk;
		bk.id = id; bk.slot = mesh; bk.other = h.other; bk.simt = sdk.SimTime ();
		double r = 0;
		for (uint32_t c : x.chunks) {
			const CollBlastChunk &ch = bs.b->chunk[c];
			if (ch.cell >= 0) bk.cells.push_back ((uint32_t)ch.cell); else bk.pieces.push_back ((uint32_t)ch.piece);
			bk.inertia += Orb (CollApply (Fs, ch.c) + ofs) * ch.mass;
			r = std::max (r, (ch.c - x.c).length ());
		}
		std::sort (bk.cells.begin (), bk.cells.end ()); std::sort (bk.pieces.begin (), bk.pieces.end ());
		bk.mass = x.mass; bk.crushed = x.crushed;
		bk.centroid = CollApply (Fs, x.c) + ofs;
		bk.n = Unit (CollApplyDir (Fs, x.n));
		if (hitNow) { // a split under spin loads only gets no kick: Spawn gives it the parent's rotation velocity at its centroid
			bk.dv = bk.n * (BLAST_KICK * std::max (h.vn, 0.0));
			r += 0.5 * std::sqrt (bs.b->Acell);
			Vector dw = bs.haveHit ? crossp (bk.dv, h.c - bk.centroid) / (r * r + 0.01) : Vector ();
			double wl = dw.length ();
			if (wl > 2 * 3.14159265358979) dw *= 2 * 3.14159265358979 / wl;
			bk.dw = dw;
		}
		SpawnCells (bk);
	}
}

bool CollBreakA::MakeCellSpawn (const CollBlastBreak &bk, const CollSlotA &sl, CollH vh, const std::vector<Vector> &site, const std::vector<uint16_t> &stat, uint32_t event, CollSpawnA &sp)
{
	Vector ofs = sdk.MeshOffset (vh, bk.slot);
	std::vector<CollAffine> F = Poses (bk.id, bk.slot, sl.v.size ());
	CollAffine Fs = StaticPose (sl, F);
	Vector cd = bk.centroid - ofs;
	DentDebris &d = sp.row;
	d.id = ++debrisSeq; d.slot = bk.slot; d.key = sl.key; d.ngrp = sl.ngrp; d.nvtx = sl.nvtx; d.simt = bk.simt;
	auto pose = [&] (const CollAffine &X, std::vector<uint16_t> grp) {
		DentDebrisPose ps;
		MatQ (X.A, ps.q);
		ps.p = X.t - cd;
		ps.p = Vector (Q9 (ps.p.x), Q9 (ps.p.y), Q9 (ps.p.z));
		for (double &x : ps.q) x = Q9 (x);
		std::sort (grp.begin (), grp.end ());
		ps.grp = grp;
		if (!ps.grp.empty ()) d.pose.push_back (ps);
	};
	std::vector<uint16_t> sg;
	std::set<uint32_t> kc (bk.cells.begin (), bk.cells.end ());
	if (!bk.cells.empty ()) for (uint16_t g : stat) { // only groups with a triangle in the break's cells: a small piece, not a folded copy of the whole hull
		if (Hidden (bk.id, bk.slot, g) || g >= sl.v.size () || g >= sl.idx.size ()) continue;
		bool any = false;
		for (size_t t = 0; t + 2 < sl.idx[g].size () && !any; t += 3) {
			size_t i = sl.idx[g][t], j = sl.idx[g][t + 1], k = sl.idx[g][t + 2];
			if (i >= sl.v[g].size () || j >= sl.v[g].size () || k >= sl.v[g].size ()) continue;
			Vector x = (P (sl.v[g][i]) + P (sl.v[g][j]) + P (sl.v[g][k])) / 3.0;
			int bi = -1; double bd = 1e300;
			for (size_t c = 0; c < site.size (); c++) { double d = (x - site[c]).length2 (); if (d < bd) bd = d, bi = (int)c; }
			any = bi >= 0 && kc.count ((uint32_t)bi);
		}
		if (any) sg.push_back (g);
	}
	pose (Fs, sg);
	for (uint32_t k : bk.pieces) {
		if (k >= sl.piece.size ()) continue;
		const CollPieceA &p = sl.piece[k];
		std::vector<uint16_t> pg;
		for (uint16_t g : p.grp) if (!sl.keep.count (g) && !(sl.usr[g] & 0x2) && !Hidden (bk.id, bk.slot, g)) pg.push_back (g);
		if (pg.empty ()) continue;
		pose (F[pg[0]], pg);
	}
	if (d.pose.empty ()) return false;
	if (const VesselDamageA *vd = s.Damage (bk.id)) for (auto &r : vd->d.rec) { // the parent's other records
		if (r.slot != bk.slot || r.key != sl.key || r.p.mode == DENTM_VCUT || (bk.crushed && r.p.mode == DENTM_CRUSH)) continue; // a crushed fragment tore off before the crush flattened it
		bool any = false;
		for (auto &ps : d.pose) for (uint16_t g : ps.grp) if (Lists (r, g)) any = true;
		if (any) d.rec.push_back (r);
	}
	double sz = 0; // cell spacing as AddCellCuts: mean nearest-site distance
	for (size_t i = 0; i < site.size (); i++) {
		double b = 0;
		for (size_t j = 0; j < site.size (); j++) if (j != i) { double e = (site[j] - site[i]).length (); if (b == 0 || e < b) b = e; }
		sz += b;
	}
	if (!site.empty ()) sz /= (double)site.size ();
	for (uint32_t c : bk.cells) { // KEEP VCUT: only the break's cells stay
		if (c >= site.size ()) continue;
		DentRecord r {};
		r.slot = bk.slot; r.key = sl.key; r.ngrp = sl.ngrp; r.nvtx = sl.nvtx; r.grp = stat;
		r.p.mode = DENTM_VCUT; r.p.bits = DENTC_KEEP; r.p.P = (double)c; r.p.seed = (uint32_t)site.size (); r.p.c = site[c];
		r.p.n = Vector (0, 0, 1); r.p.t = Vector (1, 0, 0); r.p.R = std::max (0.5 * sz, 1e-3); r.p.h = 0; r.p.T = 0; // R > 0: the row saves and parses
		r.p.hd = std::min (std::max (0.125 * sz, 0.15), 0.6); r.p.hz = std::max (std::min (0.05 * sz, 0.4), 1e-3);
		DentMath::Quantise (r.p);
		d.rec.push_back (r);
	}
	d.mass = Q9 (bk.mass); // the mass that left the parent
	if (!(d.mass >= BRK_MIN_MASS)) return false;
	sp.parent = bk.id; sp.other = bk.other; sp.event = event; sp.mesh = sl.name;
	d.other = IdName (bk.other);
	sp.cv = bk.centroid; sp.dv = bk.dv; sp.dw = bk.dw; sp.mass = d.mass; sp.blast = true;
	return true;
}

void CollBreakA::SpawnCells (const CollBlastBreak &bk)
{
	if (quiet) return;
	CollH vh = s.VesselHandle (bk.id);
	if (!vh) return;
	const CollSlotA *sl = Slot (bk.id, bk.slot);
	if (!sl || !sl->ok) return;
	VesB &b = ves[bk.id];
	uint32_t event = ++events;
	std::vector<Vector> site;
	std::vector<uint16_t> stat;
	auto bi = blast.find ({ bk.id, bk.slot });
	if (bi != blast.end () && bi->second.b) site = bi->second.b->site, stat = bi->second.groups;
	else if (const DentSites *ds = s.Sites (bk.id, bk.slot)) site = ds->s;
	if (stat.empty ()) stat = StatGroups (*sl);
	bool canDebris = cfg.visuals && s.vis.Mode () != CollVisualA::MODE_OFF;
	if (canDebris && cfgOk < 0) cfgOk = sdk.DebrisClassExists () ? 1 : 0;
	if (canDebris && !cfgOk) { if (!loggedNoCfg) Log ("Collision: Config/Vessels/CollDebris.cfg missing: parts are hidden without debris"); loggedNoCfg = true; canDebris = false; }
	size_t queued = 0;
	for (auto &x : spawn) if (x.blast && x.parent == bk.id) queued++;
	CollBlastBreak bm = bk;
	bm.mass = CutMass (bk.id, vh, bk.mass, bk.inertia); // the debris takes the mass the parent lost
	if (bm.mass < bk.mass && bm.mass < BRK_MIN_MASS) Log ("Collision blast '%s' mass cap: %.4g of %.4g kg left, no debris", sdk.Name (vh).c_str (), bm.mass, bk.mass);
	std::string name = "-";
	CollSpawnA sp;
	if (canDebris && queued < (size_t)std::max (BRK_PER_EVENT, cfg.debrisMax) && MakeCellSpawn (bm, *sl, vh, site, stat, event, sp)) { // every Blast break of debris mass flies
		sp.row.name = name = NewName (sdk.Name (vh));
		spawn.push_back (sp);
		SyncRows (sp.parent);
		DentTorn t; // playback: one recorder row per cell debris with its kick, cells and pieces
		t.kind = (uint8_t)CBRK_CELL; t.slot = bk.slot; t.key = sl->key; t.ngrp = sl->ngrp; t.nvtx = sl->nvtx; t.simt = bk.simt; t.debris = name;
		t.kin = true, t.dv = sp.dv, t.dw = sp.dw, t.mass = sp.mass;
		t.cells = bk.cells, t.pieces = bk.pieces, t.c = bk.centroid, t.crushed = bk.crushed;
		s.RecordTorn (bk.id, t);
	}
	if (!bk.cells.empty ()) s.AddCellCuts (bk.id, bk.slot, bk.cells);
	for (uint32_t k : bk.pieces) { // animated pieces: torn rows, existing hide path
		if (k >= sl->piece.size ()) continue;
		DentTorn t;
		t.kind = (uint8_t)CBRK_PART; t.slot = bk.slot; t.key = sl->key; t.ngrp = sl->ngrp; t.nvtx = sl->nvtx; t.simt = bk.simt; t.debris = name;
		for (uint16_t g : sl->piece[k].grp) if (!sl->keep.count (g) && !(sl->usr[g] & 0x2) && !Hidden (bk.id, bk.slot, g)) t.grp.push_back (g);
		if (t.grp.empty ()) continue;
		std::sort (t.grp.begin (), t.grp.end ());
		if (name != "-") t.kin = true, t.dv = bk.dv, t.dw = bk.dw, t.mass = sp.mass;
		b.rows.push_back (t);
		s.AddTorn (bk.id, t); b.adopted++;
		if (b.sh) for (uint16_t g : t.grp) b.sh->SetGroupHidden (bk.slot, g, true);
	}
	blastBreaks++; breaks++;
	std::string cl;
	for (uint32_t c : bk.cells) cl += (cl.empty () ? "" : ",") + std::to_string (c);
	Log ("Collision blast break '%s' slot=%u cells=%s pieces=%zu mass=%.4g debris=%s", sdk.Name (vh).c_str (), bk.slot, cl.empty () ? "-" : cl.c_str (), bk.pieces.size (), bk.mass, name.c_str ());
	CollBreakEvent ev;
	ev.id = bk.id; ev.kind = CBRK_SECTION; ev.c = bk.centroid; ev.n = bk.n.length () > 0 ? bk.n : Unit (bk.dv); ev.r = bi != blast.end () && bi->second.b ? std::sqrt (bi->second.b->Acell) : 0.5; ev.playback = false;
	if (s.fx) s.fx->Break (ev);
}

std::unique_ptr<CollDmgSink> CollMakeBreak (CollSdk &sdk, CollDmgSession &s, const CollCfgValues &cfg)
{
	return std::make_unique<CollBreakA> (sdk, s, cfg);
}
