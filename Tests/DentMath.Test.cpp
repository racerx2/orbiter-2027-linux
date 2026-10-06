// not upstream: unit tests for Src/Orbiter/DentMath (D4 U1-U17; U18 in CollLoop.Test; CR* fixes)
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "DentMath.h"

// measured numbers are printed with DENT_TEST_VERBOSE set
#define NOTE(...) do { if (std::getenv ("DENT_TEST_VERBOSE")) std::printf (__VA_ARGS__); } while (0)

namespace {

// deterministic random numbers (splitmix64)
struct Rng {
	uint64_t s;
	uint64_t Next () { uint64_t z = (s += 0x9e3779b97f4a7c15ULL); z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL; z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL; return z ^ (z >> 31); }
	double U () { return (double)(Next () >> 11) * (1.0 / 9007199254740992.0); }
	double U (double a, double b) { return a + (b - a) * U (); }
	uint32_t I (uint32_t n) { return (uint32_t)(Next () % n); }
};

bool Bits (double a, double b) { return std::memcmp (&a, &b, sizeof (double)) == 0; }
bool SameP (const DentParams &a, const DentParams &b)
{
	return Bits (a.c.x, b.c.x) && Bits (a.c.y, b.c.y) && Bits (a.c.z, b.c.z) && Bits (a.n.x, b.n.x) && Bits (a.n.y, b.n.y) && Bits (a.n.z, b.n.z)
		&& Bits (a.R, b.R) && Bits (a.h, b.h) && Bits (a.T, b.T);
}
bool SameRec (const DentRecord &a, const DentRecord &b)
{
	return SameP (a.p, b.p) && a.slot == b.slot && a.key == b.key && a.ngrp == b.ngrp && a.nvtx == b.nvtx && a.grp == b.grp && a.flags == b.flags;
}
bool SameVtx (const std::vector<std::vector<DentVtx>> &a, const std::vector<std::vector<DentVtx>> &b)
{
	if (a.size () != b.size ()) return false;
	for (size_t g = 0; g < a.size (); g++)
		if (a[g].size () != b[g].size () || (a[g].size () && std::memcmp (a[g].data (), b[g].data (), a[g].size () * sizeof (DentVtx)))) return false;
	return true;
}
bool SameObj (const DentObject &a, const DentObject &b)
{
	return SameVtx (a.rest, b.rest) && SameVtx (a.cur, b.cur) && a.idx == b.idx && a.weld == b.weld && a.nweld == b.nweld;
}
Vector P (const DentVtx &v) { return Vector (v.x, v.y, v.z); }
Vector N (const DentVtx &v) { return Vector (v.nx, v.ny, v.nz); }
double AngleDeg (const Vector &a, const Vector &b)
{
	double c = dotp (a, b) / (a.length () * b.length ());
	return std::acos (std::max (-1.0, std::min (1.0, c))) * 180.0 / Pi;
}
DentParams Params (const Vector &c, const Vector &n, double R, double h, double T)
{
	DentParams p;
	p.c = c, p.n = n, p.R = R, p.h = h, p.T = T;
	return p;
}
DentMaterial Mat (double sigma, double tcap)
{
	return { "test", sigma, tcap, 1.0, 0.3, 1.0, 0.5, false };
}
// a radius as a record stores it
double Q9R (double R)
{
	DentParams p = Params (Vector (0, 0, 0), Vector (0, 0, 1), R, 0.0, 0.0);
	DentMath::Quantise (p);
	return p.R;
}

// fixtures

// one group: grid of nx x ny quads from org with steps ux, uy; faces along ux x uy
void AddGrid (DentObject &o, const Vector &org, const Vector &ux, const Vector &uy, int nx, int ny, const Vector &nm)
{
	std::vector<DentVtx> v;
	std::vector<uint16_t> I;
	for (int j = 0; j <= ny; j++)
		for (int i = 0; i <= nx; i++) {
			Vector p = org + ux * i + uy * j;
			v.push_back ({ (float)p.x, (float)p.y, (float)p.z, (float)nm.x, (float)nm.y, (float)nm.z, (float)i / nx, (float)j / ny });
		}
	for (int j = 0; j < ny; j++)
		for (int i = 0; i < nx; i++) {
			uint16_t a = (uint16_t)(j * (nx + 1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(b + nx + 1), d = (uint16_t)(a + nx + 1);
			I.insert (I.end (), { a, b, c, a, c, d });
		}
	o.rest.push_back (v), o.cur.push_back (v), o.idx.push_back (I);
}

// two floor groups meeting on x = 2 (same normals) and a wall on y = 4 (90 deg edge with the floor)
DentObject Floor3 ()
{
	DentObject o;
	AddGrid (o, Vector (0, 0, 0), Vector (0.25, 0, 0), Vector (0, 0.25, 0), 8, 16, Vector (0, 0, 1));
	AddGrid (o, Vector (2, 0, 0), Vector (0.25, 0, 0), Vector (0, 0.25, 0), 8, 16, Vector (0, 0, 1));
	AddGrid (o, Vector (0, 4, 0), Vector (0.25, 0, 0), Vector (0, 0, 0.25), 16, 8, Vector (0, -1, 0));
	o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
	return o;
}

// 2 m cube, outward faces, one group per face
DentObject Box2 ()
{
	DentObject o;
	AddGrid (o, Vector (-1, -1, 1), Vector (0.5, 0, 0), Vector (0, 0.5, 0), 4, 4, Vector (0, 0, 1));
	AddGrid (o, Vector (-1, -1, -1), Vector (0, 0.5, 0), Vector (0.5, 0, 0), 4, 4, Vector (0, 0, -1));
	AddGrid (o, Vector (1, -1, -1), Vector (0, 0.5, 0), Vector (0, 0, 0.5), 4, 4, Vector (1, 0, 0));
	AddGrid (o, Vector (-1, -1, -1), Vector (0, 0, 0.5), Vector (0, 0.5, 0), 4, 4, Vector (-1, 0, 0));
	AddGrid (o, Vector (-1, 1, -1), Vector (0, 0, 0.5), Vector (0.5, 0, 0), 4, 4, Vector (0, 1, 0));
	AddGrid (o, Vector (-1, -1, -1), Vector (0.5, 0, 0), Vector (0, 0, 0.5), 4, 4, Vector (0, -1, 0));
	o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
	return o;
}

// Brighton Beach BLOCK #1 (POS -60.62 0 -35, ROT 30, SCALE 40 15 30) per ExportGroup, in float
DentObject Brighton ()
{
	const float px = -60.62f, py = 0.0f, pz = -35.0f;
	const float rot = (float)(30.0 * Pi / 180.0);
	const float dx = 0.5f * 40.0f, dy = 15.0f, dz = 0.5f * 30.0f;
	const float s = (float)std::sin ((double)rot), c = (float)std::cos ((double)rot);
	const float db[12] = { dx*c+dz*s+px, -dx*c+dz*s+px, -dx*c-dz*s+px, dx*c-dz*s+px, py, py+dy,
		dx*s-dz*c+pz, -dx*s-dz*c+pz, -dx*s+dz*c+pz, dx*s+dz*c+pz, s, c };
	const uint16_t sidx[12] = { 0,1,2, 2,3,0, 4,5,6, 6,7,4 };
	DentObject o;
	const int xi[2][8] = { { 0, 1, 1, 0, 2, 3, 3, 2 }, { 3, 0, 0, 3, 1, 2, 2, 1 } }; // db index of x per vertex
	const int zi[2][8] = { { 6, 7, 7, 6, 8, 9, 9, 8 }, { 9, 6, 6, 9, 7, 8, 8, 7 } };
	const float tus[3] = { 2.0f, 1.5f, 2.0f }, tvs[3] = { 1.0f, 1.0f, 2.0f };
	for (int g = 0; g < 2; g++) {
		std::vector<DentVtx> v (8);
		for (int i = 0; i < 8; i++) {
			DentVtx &w = v[i];
			w = DentVtx ();
			w.x = db[xi[g][i]];
			w.y = (i == 0 || i == 1 || i == 4 || i == 5) ? db[4] : db[5];
			w.z = db[zi[g][i]];
			float a = g == 0 ? db[10] : db[11], b = g == 0 ? -db[11] : db[10];
			w.nx = i < 4 ? a : -a;
			w.nz = i < 4 ? b : -b;
			w.tu = (i == 0 || i == 3 || i == 4 || i == 7) ? tus[g] : 0.0f;
			w.tv = (i == 0 || i == 1 || i == 4 || i == 5) ? tvs[g] : 0.0f;
		}
		o.rest.push_back (v), o.cur.push_back (v), o.idx.push_back (std::vector<uint16_t> (sidx, sidx + 12));
	}
	std::vector<DentVtx> r (4);
	for (int i = 0; i < 4; i++) {
		r[i] = DentVtx ();
		r[i].x = db[i], r[i].y = db[5], r[i].z = db[6+i], r[i].ny = 1.0f;
		r[i].tu = (i == 0 || i == 3) ? tus[2] : 0.0f;
		r[i].tv = (i == 0 || i == 1) ? tvs[2] : 0.0f;
	}
	o.rest.push_back (r), o.cur.push_back (r), o.idx.push_back (std::vector<uint16_t> (sidx, sidx + 6));
	o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
	return o;
}

// subdivided icosahedron (level 3: 642 vertices), double arrays
DentViewData Icosphere (int level, double r)
{
	const double t = (1.0 + std::sqrt (5.0)) / 2.0;
	std::vector<Vector> v = { {-1,t,0}, {1,t,0}, {-1,-t,0}, {1,-t,0}, {0,-1,t}, {0,1,t}, {0,-1,-t}, {0,1,-t}, {t,0,-1}, {t,0,1}, {-t,0,-1}, {-t,0,1} };
	std::vector<uint32_t> f = { 0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11, 1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
		3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9, 4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1 };
	for (Vector &p : v) p = p / p.length ();
	for (int l = 0; l < level; l++) {
		std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
		auto m = [&] (uint32_t a, uint32_t b) {
			auto k = std::make_pair (std::min (a, b), std::max (a, b));
			auto it = mid.find (k);
			if (it != mid.end ()) return it->second;
			Vector p = (v[a] + v[b]) * 0.5;
			v.push_back (p / p.length ());
			return mid[k] = (uint32_t)v.size () - 1;
		};
		std::vector<uint32_t> g;
		for (size_t i = 0; i < f.size (); i += 3) {
			uint32_t a = f[i], b = f[i+1], c = f[i+2], ab = m (a, b), bc = m (b, c), ca = m (c, a);
			g.insert (g.end (), { a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca });
		}
		f = g;
	}
	DentViewData d;
	for (Vector &p : v) d.rest.push_back (p * r);
	d.cur = d.rest;
	d.tri = f;
	return d;
}

// volume removed by applying p to current positions (double, tetrahedra about p.c, moving tris)
double VolumeChange (const DentMeshView &m, const DentParams &p)
{
	double dv = 0.0;
	for (size_t t = 0; t < m.nt; t++) {
		const uint32_t *v = m.tri + 3 * t;
		Vector a[3], b[3];
		bool moves = false;
		for (int k = 0; k < 3; k++) {
			a[k] = m.cur[v[k]] - p.c;
			if (DentMath::Weight (p, m.rest[v[k]]) != 0.0) moves = true;
			b[k] = a[k] + DentMath::Displace (p, m.rest[v[k]]);
		}
		if (!moves) continue;
		dv += (dotp (a[0], crossp (a[1], a[2])) - dotp (b[0], crossp (b[1], b[2]))) / 6.0;
	}
	return dv;
}

void ApplyView (DentViewData &d, const DentParams &p)
{
	for (size_t i = 0; i < d.rest.size (); i++) d.cur[i] += DentMath::Displace (p, d.rest[i]);
}

// cur = rest + records in order, float cast per record (the rule every DentMath path follows)
bool MatchesReplay (const DentObject &o, const std::vector<DentParams> &rec)
{
	for (size_t g = 0; g < o.rest.size (); g++)
		for (size_t i = 0; i < o.rest[g].size (); i++) {
			DentVtx c = o.rest[g][i];
			for (const DentParams &p : rec) {
				Vector r = P (o.rest[g][i]);
				if (DentMath::Weight (p, r) == 0.0) continue;
				Vector d = DentMath::Displace (p, r);
				c.x = (float)((double)c.x + d.x), c.y = (float)((double)c.y + d.y), c.z = (float)((double)c.z + d.z);
			}
			const DentVtx &u = o.cur[g][i];
			if (std::memcmp (&c.x, &u.x, 3 * sizeof (float))) return false;
		}
	return true;
}

// largest distance between vertices of one weld id (current positions)
double WeldGap (const DentObject &o)
{
	std::vector<Vector> first (o.nweld);
	std::vector<uint8_t> has (o.nweld, 0);
	double gap = 0.0;
	for (size_t g = 0; g < o.rest.size (); g++)
		for (size_t i = 0; i < o.rest[g].size (); i++) {
			uint32_t w = o.weld[g][i];
			if (!has[w]) { has[w] = 1, first[w] = P (o.cur[g][i]); continue; }
			gap = std::max (gap, first[w].dist (P (o.cur[g][i])));
		}
	return gap;
}

size_t NTri (const DentObject &o)
{
	size_t n = 0;
	for (const auto &I : o.idx) n += I.size () / 3;
	return n;
}
size_t NVtx (const DentObject &o)
{
	size_t n = 0;
	for (const auto &v : o.rest) n += v.size ();
	return n;
}

// conformity: vertices strictly inside a live edge (welded rest), length of single-triangle edges
size_t TJunctions (const DentObject &o, double *openLen = nullptr)
{
	std::vector<Vector> Pw (o.nweld);
	std::vector<uint8_t> has (o.nweld, 0);
	for (size_t g = 0; g < o.rest.size (); g++)
		for (size_t i = 0; i < o.rest[g].size (); i++)
			if (!has[o.weld[g][i]]) has[o.weld[g][i]] = 1, Pw[o.weld[g][i]] = P (o.rest[g][i]);
	std::map<std::pair<uint32_t, uint32_t>, int> E;
	for (size_t g = 0; g < o.idx.size (); g++)
		for (size_t j = 0; j + 2 < o.idx[g].size (); j += 3)
			for (int k = 0; k < 3; k++) {
				uint32_t a = o.weld[g][o.idx[g][j+k]], b = o.weld[g][o.idx[g][j+(k+1)%3]];
				E[std::make_pair (std::min (a, b), std::max (a, b))]++;
			}
	size_t bad = 0;
	double open = 0.0;
	for (const auto &e : E) {
		const Vector &pa = Pw[e.first.first], &pb = Pw[e.first.second];
		Vector d = pb - pa;
		double L2 = d.length2 ();
		if (e.second == 1) open += std::sqrt (L2);
		if (L2 == 0.0) continue;
		Vector lo (std::min (pa.x, pb.x) - 1e-4, std::min (pa.y, pb.y) - 1e-4, std::min (pa.z, pb.z) - 1e-4);
		Vector hi (std::max (pa.x, pb.x) + 1e-4, std::max (pa.y, pb.y) + 1e-4, std::max (pa.z, pb.z) + 1e-4);
		for (uint32_t w = 0; w < o.nweld; w++) {
			if (!has[w] || w == e.first.first || w == e.first.second) continue;
			const Vector &p = Pw[w];
			if (p.x < lo.x || p.y < lo.y || p.z < lo.z || p.x > hi.x || p.y > hi.y || p.z > hi.z) continue;
			double t = dotp (p - pa, d) / L2;
			if (t <= 1e-6 || t >= 1.0 - 1e-6) continue;
			if (p.dist (pa + d * t) < 2e-5) bad++;
		}
	}
	if (openLen) *openLen = open;
	return bad;
}

// one building dent as Damage does it (13.4): radius, refinement, slab ray, solve, quantise, apply
struct BDent { int rc = -9, added = 0; DentParams p {}; double V = 0, S = 0, hS = 0, dV = 0; size_t moved = 0; };
BDent BuildingDent (DentObject &o, std::vector<DentParams> &rec, const Vector &c, const Vector &n, double E,
	const DentMaterial &m, double a, double Rmax, double L, std::vector<std::vector<uint8_t>> *dirty = nullptr)
{
	BDent r;
	r.V = E / m.sigma_c;
	double R = DentMath::Radius (E, m.sigma_c, a, Rmax);
	r.added = DentMath::Refine (o, c, R, rec.data (), rec.size (), nullptr);
	DentViewData v;
	DentMath::MakeView (o, v);
	DentMeshView mv = v.View ();
	double t = -1.0;
	DentMath::RayCast (mv, c, -n, DENT_RAY_TMIN, 1e4, t);
	DentInput in = { E, &m, c, n, a, Rmax, L, t, false };
	r.rc = DentMath::Solve (in, mv, r.p);
	if (r.rc != DENT_OK) return r;
	r.S = DentMath::VolumeFactor (r.p, mv);
	r.hS = r.p.h * r.S;
	r.dV = VolumeChange (mv, r.p); // exact solve, before quantisation
	DentMath::Quantise (r.p);
	r.moved = DentMath::Apply (r.p, o.rest, o.cur, nullptr, 0, dirty);
	rec.push_back (r.p);
	return r;
}

// the loop of Vessel::ParseScenarioEx: getline (cbuf, 256)
size_t ReadLines256 (const std::vector<std::string> &lines, std::vector<std::string> &out)
{
	std::string text;
	for (const std::string &l : lines) text += l + "\n";
	std::istringstream is (text);
	char cbuf[256];
	out.clear ();
	while (is.getline (cbuf, 256)) out.push_back (cbuf);
	return out.size ();
}

// vessel lines -> text through the tolerant parser
DentVesselText ParseVessel (const std::vector<std::string> &lines, int *skipped = nullptr)
{
	DentVesselParser vp;
	for (const std::string &l : lines) vp.Line (l.c_str ());
	DentVesselText v;
	vp.Finish (v);
	if (skipped) *skipped = vp.Skipped ();
	return v;
}
std::vector<std::string> Format (const DentVesselText &v)
{
	std::vector<std::string> l;
	DentMath::FormatVessel (v, "  ", l);
	return l;
}
// base section lines (with BEGIN) -> text
std::vector<DentBaseText> ParseBases (const std::vector<std::string> &lines, int *skipped = nullptr)
{
	DentBasesParser bp;
	for (size_t i = 1; i < lines.size () && bp.Line (lines[i].c_str ()); i++) {}
	std::vector<DentBaseText> b;
	bp.Finish (b);
	if (skipped) *skipped = bp.Skipped ();
	return b;
}

// .msh test geometry: GEOM blocks, 3/6/8 numbers per vertex, 3 indices per tri; COLL_TEST_SRC path
bool LoadMsh (const char *rel, DentObject &o)
{
#ifdef COLL_TEST_SRC
	std::string path = std::string (COLL_TEST_SRC) + "/" + rel;
#else
	std::string path = rel;
#endif
	std::ifstream f (path);
	if (!f) return false;
	std::string line;
	while (std::getline (f, line)) {
		std::istringstream h (line);
		std::string kw;
		size_t nv = 0, nt = 0;
		if (!(h >> kw) || kw != "GEOM" || !(h >> nv >> nt)) continue;
		std::vector<DentVtx> v (nv);
		for (size_t i = 0; i < nv && std::getline (f, line); i++) {
			std::istringstream s (line);
			double a[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
			for (int k = 0; k < 8 && (s >> a[k]); k++) {}
			v[i] = { (float)a[0], (float)a[1], (float)a[2], (float)a[3], (float)a[4], (float)a[5], (float)a[6], (float)a[7] };
		}
		std::vector<uint16_t> I;
		for (size_t j = 0; j < nt && std::getline (f, line); j++) {
			std::istringstream s (line);
			unsigned a, b, c;
			if (s >> a >> b >> c) I.insert (I.end (), { (uint16_t)a, (uint16_t)b, (uint16_t)c });
		}
		o.rest.push_back (v), o.cur.push_back (v), o.idx.push_back (I);
	}
	o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
	return !o.rest.empty ();
}

// tris of o whose rest attributes don't come from one same-group tri of r: UV 1e-3, normal 10 deg
size_t AttrMismatch (const DentObject &r, const DentObject &o)
{
	size_t bad = 0;
	for (size_t g = 0; g < o.idx.size (); g++) {
		const std::vector<DentVtx> &RV = r.rest[g], &OV = o.rest[g];
		const std::vector<uint16_t> &RI = r.idx[g], &OI = o.idx[g];
		for (size_t j = 0; j + 2 < OI.size (); j += 3) {
			bool found = false;
			for (size_t i = 0; i + 2 < RI.size () && !found; i += 3) {
				const DentVtx &a = RV[RI[i]], &b = RV[RI[i+1]], &c = RV[RI[i+2]];
				Vector e1 = P (b) - P (a), e2 = P (c) - P (a), nr = crossp (e1, e2);
				double A2 = nr.length2 ();
				if (!(A2 > 0.0)) continue;
				bool all = true;
				for (int k = 0; k < 3 && all; k++) {
					const DentVtx &x = OV[OI[j+k]];
					Vector d = P (x) - P (a);
					double off = std::fabs (dotp (d, nr)) / std::sqrt (A2);
					double u = dotp (crossp (d, e2), nr) / A2, v = dotp (crossp (e1, d), nr) / A2, w = 1.0 - u - v;
					if (off > 1e-3 || u < -1e-4 || v < -1e-4 || w < -1e-4) { all = false; break; }
					double tu = w * a.tu + u * b.tu + v * c.tu, tv = w * a.tv + u * b.tv + v * c.tv;
					double ts = 1.0 + std::max (std::fabs (tu), std::fabs (tv));
					if (std::fabs (tu - x.tu) > 1e-3 * ts || std::fabs (tv - x.tv) > 1e-3 * ts) { all = false; break; }
					Vector ne = N (a) * w + N (b) * u + N (c) * v;
					if (ne.length () > 1e-6 && N (x).length () > 1e-6 && AngleDeg (ne, N (x)) > 10.0) all = false;
				}
				found = all;
			}
			if (!found) bad++;
		}
	}
	return bad;
}

// dented object replayed from rest as Damage loads it: refine per record unless NOREFINE, apply
DentObject Replay (const DentObject &rest, const std::vector<DentRecord> &rec)
{
	DentObject o = rest;
	std::vector<DentParams> prior;
	for (const DentRecord &r : rec) {
		if (!(r.flags & DENTR_NOREFINE)) DentMath::Refine (o, r.p.c, r.p.R, prior.data (), prior.size (), nullptr);
		DentMath::Apply (r.p, o.rest, o.cur, r.grp.data (), r.grp.size (), nullptr);
		prior.push_back (r.p);
	}
	return o;
}

} // namespace

// U1-U7, U10: field, volume, caps, normals

TEST_CASE("U1 kernel values and plane integral", "[dent]")
{
	REQUIRE(DentMath::Kernel (0.0) == 1.0);
	REQUIRE(DentMath::Kernel (1.0) == 0.0);
	REQUIRE(DentMath::Kernel (2.0) == 0.0);
	REQUIRE(DentMath::Kernel (0.25) == 0.5625);
	auto k = [] (double r) { return DentMath::Kernel (r * r); };
	REQUIRE(std::fabs ((k (1e-5) - k (0.0)) / 1e-5) < 1e-4); // zero slope at the centre
	REQUIRE(std::fabs ((k (1.0) - k (1.0 - 1e-5)) / 1e-5) < 1e-4); // and at the rim (C1)
	const double R = 2.0, h = 0.3;
	DentParams p = Params (Vector (0, 0, 0), Vector (0, 0, 1), R, h, 0.0);
	const int n = 2000;
	const double dx = 2.0 * R / n;
	double sum = 0.0;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
			sum -= DentMath::Displace (p, Vector (-R + (i + 0.5) * dx, -R + (j + 0.5) * dx, 0.0)).z;
	double vol = sum * dx * dx, ref = Pi * R * R * h / 3.0;
	NOTE ("U1 plane integral %.9g vs pi R^2 h / 3 = %.9g, rel %.3g\n", vol, ref, vol / ref - 1.0);
	REQUIRE(std::fabs (vol / ref - 1.0) < 0.005);
	// slab: smoothstep from 0.4 T to 0.8 T behind the tangent plane
	DentParams q = Params (Vector (0, 0, 0), Vector (0, 0, 1), 10.0, 1.0, 1.0);
	REQUIRE(DentMath::Weight (q, Vector (0, 0, -0.39)) == DentMath::Kernel (0.39 * 0.39 / 100.0));
	REQUIRE(DentMath::Weight (q, Vector (0, 0, -0.81)) == 0.0);
	REQUIRE(std::fabs (DentMath::Weight (q, Vector (0, 0, -0.6)) - 0.5 * DentMath::Kernel (0.0036)) < 1e-15);
	DentRecord rec {};
	rec.p = p;
	Vector f = DentMath::Field (&rec, Vector (0.5, 0.2, 0.0)), d = DentMath::Displace (p, Vector (0.5, 0.2, 0.0));
	REQUIRE((Bits (f.x, d.x) && Bits (f.y, d.y) && Bits (f.z, d.z)));
}

TEST_CASE("U2 volume on a closed icosphere", "[dent]")
{
	const DentMaterial m = Mat (0.5e6, 10.0);
	const double Vs = 4.0 / 3.0 * Pi;
	for (double frac : { 1e-4, 1e-3, 1e-2, 0.1 }) {
		DentViewData d = Icosphere (3, 1.0);
		REQUIRE(d.rest.size () == 642);
		DentMeshView mv = d.View ();
		double t;
		REQUIRE(DentMath::RayCast (mv, Vector (0.05, 0.03, 5.0), Vector (0, 0, -1), 0.0, 10.0, t));
		Vector c (0.05, 0.03, 5.0 - t), n (0, 0, 1);
		double rayT = -1.0;
		REQUIRE(DentMath::RayCast (mv, c, -n, DENT_RAY_TMIN, 100.0, rayT));
		double V = frac * Vs;
		DentInput in = { V * m.sigma_c, &m, c, n, 0.0, 10.0, 100.0, rayT, false };
		DentParams p {};
		REQUIRE(DentMath::Solve (in, mv, p) == DENT_OK);
		REQUIRE(p.T > 0.0);
		REQUIRE(std::fabs (p.h * DentMath::VolumeFactor (p, mv) / V - 1.0) < 1e-15); // uncapped: h = V / S
		double dV = VolumeChange (mv, p);
		ApplyView (d, p);
		NOTE ("U2 V/Vsphere %g: R %.4f h %.5f T %.3f, swept %.12g target %.12g rel %.2e\n", frac, p.R, p.h, p.T, dV, V, dV / V - 1.0);
		REQUIRE(std::fabs (dV / V - 1.0) < 1e-6);
	}
}

TEST_CASE("U3 welded seams across three groups", "[dent]")
{
	DentObject o = Floor3 ();
	REQUIRE(o.nweld == 3 * 153 - 17 - 17);
	DentParams p = Params (Vector (2.1, 3.3, 0.0), Vector (0, 0, 1), 1.2, 0.15, 0.0);
	size_t moved = DentMath::Apply (p, o.rest, o.cur, nullptr, 0, nullptr);
	REQUIRE(moved > 0);
	std::vector<Vector> first (o.nweld);
	std::vector<int> cnt (o.nweld, 0);
	double gap = 0.0;
	size_t dup = 0;
	for (size_t g = 0; g < 3; g++)
		for (size_t i = 0; i < o.cur[g].size (); i++) {
			uint32_t w = o.weld[g][i];
			if (cnt[w]++ == 0) { first[w] = P (o.cur[g][i]); continue; }
			dup++;
			gap = std::max (gap, first[w].dist (P (o.cur[g][i])));
		}
	NOTE ("U3 %zu duplicates, max gap after the dent %.3g m, vertices moved %zu\n", dup, gap, moved);
	REQUIRE(dup == 17 + 17);
	REQUIRE(gap <= 1e-6);
	REQUIRE(WeldGap (o) == 0.0);
	// a group list moves only those groups, each once whatever the list holds
	DentObject a = Floor3 (), b = Floor3 ();
	const uint16_t once[1] = { 1 }, twice[3] = { 1, 1, 7 };
	DentMath::Apply (p, a.rest, a.cur, once, 1, nullptr);
	DentMath::Apply (p, b.rest, b.cur, twice, 3, nullptr);
	REQUIRE(SameVtx (a.cur, b.cur));
	REQUIRE(SameVtx (std::vector<std::vector<DentVtx>> (1, a.cur[0]), std::vector<std::vector<DentVtx>> (1, a.rest[0])));
}

TEST_CASE("U4 normals", "[dent]")
{
	DentObject o = Floor3 ();
	DentParams p = Params (Vector (2.1, 3.4, 0.0), Vector (0, 0, 1), 1.2, 0.2, 0.0);
	std::vector<Vector> restSum;
	DentMath::FaceNormalSums (o.rest, o.idx, o.weld, o.nweld, restSum);
	std::vector<std::vector<uint8_t>> dirty;
	DentMath::Apply (p, o.rest, o.cur, nullptr, 0, &dirty);
	auto touchedOf = [&] (const DentObject &x) {
		std::vector<uint8_t> t (x.nweld, 0);
		for (size_t g = 0; g < dirty.size (); g++)
			for (size_t i = 0; i < dirty[g].size (); i++) if (dirty[g][i]) t[x.weld[g][i]] = 1;
		return t;
	};
	std::vector<std::vector<DentVtx>> before = o.cur;
	DentMath::Normals (o.rest, restSum, o.idx, o.weld, o.nweld, touchedOf (o), o.cur);
	double unitErr = 0.0, dupAng = 0.0, edgeErr = 0.0, tilt = 0.0;
	size_t changed = 0, far = 0, edges = 0;
	std::vector<std::vector<std::pair<size_t, size_t>>> mem (o.nweld);
	for (size_t g = 0; g < 3; g++)
		for (size_t i = 0; i < o.cur[g].size (); i++) {
			const DentVtx &c = o.cur[g][i], &b = before[g][i];
			mem[o.weld[g][i]].push_back ({ g, i });
			unitErr = std::max (unitErr, std::fabs (N (c).length () - 1.0));
			if (std::memcmp (&c, &b, sizeof (DentVtx))) changed++;
			tilt = std::max (tilt, AngleDeg (N (c), N (o.rest[g][i])));
			if (P (o.rest[g][i]).dist (p.c) > p.R + 0.75) { // beyond the moved ring and its neighbours
				far++;
				REQUIRE(std::memcmp (&c, &b, sizeof (DentVtx)) == 0);
			}
		}
	for (const auto &m : mem)
		for (size_t k = 1; k < m.size (); k++) {
			const DentVtx &r0 = o.rest[m[0].first][m[0].second], &rk = o.rest[m[k].first][m[k].second];
			const DentVtx &c0 = o.cur[m[0].first][m[0].second], &ck = o.cur[m[k].first][m[k].second];
			double ra = AngleDeg (N (r0), N (rk));
			if (ra < 1e-9) dupAng = std::max (dupAng, AngleDeg (N (c0), N (ck)));
			else if (std::fabs (ra - 90.0) < 1e-6) edges++, edgeErr = std::max (edgeErr, std::fabs (AngleDeg (N (c0), N (ck)) - 90.0));
		}
	// the same dent with welds per group only (v1 prototype's per-group update): seam shading differs
	DentObject q = Floor3 ();
	std::vector<std::vector<uint32_t>> wg (3);
	uint32_t nw = 0;
	for (size_t g = 0; g < 3; g++) {
		std::vector<std::vector<uint32_t>> w1;
		uint32_t n1 = DentMath::WeldMap (std::vector<std::vector<DentVtx>> (1, q.rest[g]), DENT_WELD, w1);
		for (uint32_t id : w1[0]) wg[g].push_back (id + nw);
		nw += n1;
	}
	std::vector<Vector> rs;
	DentMath::FaceNormalSums (q.rest, q.idx, wg, nw, rs);
	DentMath::Apply (p, q.rest, q.cur, nullptr, 0, nullptr);
	DentMath::Normals (q.rest, rs, q.idx, wg, nw, std::vector<uint8_t> (nw, 1), q.cur);
	double seamPerGroup = 0.0;
	for (const auto &m : mem)
		for (size_t k = 1; k < m.size (); k++) {
			const DentVtx &r0 = q.rest[m[0].first][m[0].second], &rk = q.rest[m[k].first][m[k].second];
			if (AngleDeg (N (r0), N (rk)) < 1e-9)
				seamPerGroup = std::max (seamPerGroup, AngleDeg (N (q.cur[m[0].first][m[0].second]), N (q.cur[m[k].first][m[k].second])));
		}
	NOTE ("U4 |n|-1 max %.2e, normals changed %zu, untouched checked %zu, duplicates %.4f deg (per-group welds %.3f deg), 90-deg edge pairs %zu err %.2e deg, max tilt %.2f deg\n",
		unitErr, changed, far, dupAng, seamPerGroup, edges, edgeErr, tilt);
	REQUIRE(unitErr < 1e-5);
	REQUIRE(changed > 0);
	REQUIRE(far > 100);
	REQUIRE(dupAng < 0.1);
	REQUIRE(edges > 0);
	REQUIRE(edgeErr < 0.1);
	REQUIRE(tilt > 1.0);
	// faces as at rest: the rest normal bitwise
	DentObject z = Floor3 ();
	DentMath::Normals (z.rest, restSum, z.idx, z.weld, z.nweld, std::vector<uint8_t> (z.nweld, 1), z.cur);
	REQUIRE(SameVtx (z.cur, z.rest));
}

TEST_CASE("U5 50 identical dents stay within D_max", "[dent]")
{
	const DentMaterial m = Mat (0.5e6, 0.05);
	DentViewData d = Icosphere (3, 1.0);
	Vector c (0.05, 0.03, 0.0), n (0, 0, 1);
	double t;
	REQUIRE(DentMath::RayCast (d.View (), Vector (c.x, c.y, 5.0), -n, 0.0, 10.0, t));
	c.z = 5.0 - t;
	double rayT;
	REQUIRE(DentMath::RayCast (d.View (), c, -n, DENT_RAY_TMIN, 100.0, rayT));
	DentInput in = { 0.002 * m.sigma_c, &m, c, n, 0.0, 10.0, 100.0, rayT, false };
	int ok = 0, small = 0;
	double worst = 0.0, dmax = 0.0;
	for (int k = 0; k < 50; k++) {
		DentParams p {};
		int rc = DentMath::Solve (in, d.View (), p);
		dmax = DentMath::Dmax (m.t_cap, p.T, p.R, in.L);
		if (rc == DENT_OK) ok++, ApplyView (d, p);
		else if (rc == DENT_SMALL) small++;
		for (size_t i = 0; i < d.rest.size (); i++) worst = std::max (worst, dotp (d.rest[i] - d.cur[i], n));
		REQUIRE(worst <= dmax + 1e-9);
	}
	NOTE ("U5 50 dents: %d recorded, %d below 2 mm, max inward %.12f m, D_max %.3f m\n", ok, small, worst, dmax);
	REQUIRE(ok > 1);
	REQUIRE(small > 0);
	REQUIRE(worst > 0.9 * dmax);
}

TEST_CASE("U6 plate vs crush", "[dent]")
{
	// two skins 2 cm apart: plate mode, both skins move by the kernel alone
	DentObject pl;
	AddGrid (pl, Vector (-1, -1, 0), Vector (0.1, 0, 0), Vector (0, 0.1, 0), 20, 20, Vector (0, 0, 1));
	AddGrid (pl, Vector (-1, -1, -0.02), Vector (0, 0.1, 0), Vector (0.1, 0, 0), 20, 20, Vector (0, 0, -1));
	DentViewData v;
	DentMath::MakeView (pl, v);
	Vector c (0.03, -0.02, 0.0), n (0, 0, 1);
	double rayT = -1.0;
	REQUIRE(DentMath::RayCast (v.View (), c, -n, DENT_RAY_TMIN, 10.0, rayT));
	REQUIRE(std::fabs (rayT - 0.02) < 1e-6);
	const DentMaterial &al = DentMath::DefaultMaterial (-1);
	DentInput in = { 2.0e4, &al, c, n, 0.0, 5.0, 10.0, rayT, false };
	DentParams p {};
	REQUIRE(DentMath::Solve (in, v.View (), p) == DENT_OK);
	REQUIRE(p.T == 0.0);
	DentMath::Apply (p, pl.rest, pl.cur, nullptr, 0, nullptr);
	double diff = 0.0, back = 0.0;
	for (int j = 0; j <= 20; j++)
		for (int i = 0; i <= 20; i++) {
			const DentVtx &f = pl.cur[0][j*21+i], &b = pl.cur[1][i*21+j];
			REQUIRE(f.x == b.x);
			REQUIRE(f.y == b.y);
			double df = (double)f.z - 0.0, dbk = (double)b.z - (double)pl.rest[1][i*21+j].z;
			diff = std::max (diff, std::fabs (df - dbk));
			back = std::max (back, -dbk);
		}
	double bound = 2.0 * (0.02 / p.R) * (0.02 / p.R) * p.h + 1e-6;
	NOTE ("U6 plate: R %.3f h %.4f, back skin max %.4f m, |front - back| max %.3g m (kernel bound %.3g)\n", p.R, p.h, back, diff, bound);
	REQUIRE(back > 0.9 * p.h);
	REQUIRE(diff <= bound);
	// 2 m box hit on +z with R 2.5: crush mode, the far wall stays exactly
	DentObject bx = Box2 ();
	DentViewData bv;
	DentMath::MakeView (bx, bv);
	Vector cb (0.1, 0.05, 1.0);
	REQUIRE(DentMath::RayCast (bv.View (), cb, -n, DENT_RAY_TMIN, 10.0, rayT));
	REQUIRE(std::fabs (rayT - 2.0) < 1e-9);
	DentInput ib = { 5.0e4, &al, cb, n, 2.5, 10.0, 10.0, rayT, false };
	REQUIRE(DentMath::Solve (ib, bv.View (), p) == DENT_OK);
	REQUIRE(p.R == 2.5);
	REQUIRE(p.T == rayT);
	DentMath::Apply (p, bx.rest, bx.cur, nullptr, 0, nullptr);
	double farMove = 0.0, nearMove = 0.0;
	for (size_t g = 0; g < 6; g++)
		for (size_t i = 0; i < bx.rest[g].size (); i++) {
			double mv = P (bx.cur[g][i]).dist (P (bx.rest[g][i]));
			if (bx.rest[g][i].z <= -0.6f) farMove = std::max (farMove, mv);
			else nearMove = std::max (nearMove, mv);
		}
	NOTE ("U6 crush: R %.2f T %.2f h %.4f, near side max %.4f m, far wall max %.3g m\n", p.R, p.T, p.h, nearMove, farMove);
	REQUIRE(farMove < 1e-12);
	REQUIRE(nearMove > 0.0);
}

TEST_CASE("U7 vessel radius floor on a 2-triangle quad", "[dent]")
{
	DentViewData d;
	d.rest = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 } };
	d.cur = d.rest;
	d.tri = { 0, 1, 2, 0, 2, 3 };
	const DentMaterial &al = DentMath::DefaultMaterial (-1);
	Vector c (0.3, -0.2, 0.0), n (0, 0, 1);
	DentInput in = { 1.0, &al, c, n, 0.01, 10.0, 10.0, -1.0, true };
	DentParams p {};
	int rc = DentMath::Solve (in, d.View (), p);
	REQUIRE((rc == DENT_OK || rc == DENT_SMALL));
	double floorR = DentMath::LowPolyFloor (d.View (), c);
	REQUIRE(p.R == floorR);
	const double wmin = (1.0 - DENT_FLOOR * DENT_FLOOR) * (1.0 - DENT_FLOOR * DENT_FLOOR);
	int k = 0;
	double lo = 1.0;
	for (const Vector &v : d.rest) {
		double w = DentMath::Weight (p, v);
		lo = std::min (lo, w);
		if (w >= wmin - 1e-12) k++;
	}
	NOTE ("U7 floor R %.4f, %d vertices with weight >= %.5f (lowest %.5f), result %d\n", p.R, k, wmin, lo, rc);
	REQUIRE(k >= 4);
	// the floor wins over Rmax for vessels (4.2 step 3): a dent, never energy only
	in.Rmax = 1.0;
	rc = DentMath::Solve (in, d.View (), p);
	REQUIRE((rc == DENT_OK || rc == DENT_SMALL));
	REQUIRE(floorR > in.Rmax);
	REQUIRE(p.R == floorR);
	in.vessel = false;
	REQUIRE(DentMath::Solve (in, d.View (), p) != DENT_FLOOR_CAP);
	REQUIRE(p.R == DentMath::Radius (in.E, al.sigma_c, in.a, in.Rmax));
}

TEST_CASE("CR4 vessel floor above R_max still dents (ShuttlePB)", "[dent]")
{
	DentObject o;
	if (!LoadMsh ("Meshes/ShuttlePB.msh", o)) SKIP ("Meshes/ShuttlePB.msh not found");
	DentViewData vd;
	DentMath::MakeView (o, vd);
	DentMeshView mv = vd.View ();
	const double Rmax = 0.5 * 3.5; // Size 3.5
	const DentMaterial &al = DentMath::DefaultMaterial (-1);
	int n = 0, above = 0, dents = 0;
	for (size_t t = 0; t < mv.nt; t++) {
		Vector a = vd.rest[mv.tri[3*t]], b = vd.rest[mv.tri[3*t+1]], c = vd.rest[mv.tri[3*t+2]];
		Vector nm = crossp (b - a, c - a);
		if (!(nm.length () > 1e-9)) continue;
		for (int k = 0; k < 6; k++) {
			double u = (k + 0.5) / 6.0, w = (5 - k + 0.25) / 7.0;
			if (u + w > 1.0) w = 1.0 - u;
			Vector q = a + (b - a) * u + (c - a) * w;
			double f = DentMath::LowPolyFloor (mv, q);
			DentInput in = { 5.0e3, &al, q, nm / nm.length (), 0.05, Rmax, 3.5, -1.0, true };
			DentParams p {};
			int rc = DentMath::Solve (in, mv, p);
			n++;
			if (f > Rmax) above++;
			REQUIRE(rc != DENT_FLOOR_CAP);
			if (rc == DENT_OK || rc == DENT_SMALL) {
				dents++;
				REQUIRE(p.R >= f);
			}
		}
	}
	NOTE ("CR4 ShuttlePB: %d sample points, floor above R_max at %d, recorded or small %d\n", n, above, dents);
	REQUIRE(above > 0);
}

TEST_CASE("U10 double-sided plate", "[dent]")
{
	DentViewData one, rev, same;
	one.rest = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 } };
	one.cur = one.rest;
	one.tri = { 0, 1, 2, 0, 2, 3 };
	rev = one, same = one;
	for (int i = 0; i < 4; i++) rev.rest.push_back (one.rest[i]), same.rest.push_back (one.rest[i]);
	rev.cur = rev.rest, same.cur = same.rest;
	for (uint32_t i : { 4u, 6u, 5u, 4u, 7u, 6u }) rev.tri.push_back (i);   // back side, reversed winding
	for (uint32_t i : { 4u, 5u, 6u, 4u, 6u, 7u }) same.tri.push_back (i);  // duplicate in another group, same winding
	Vector n = Vector (0.3, -0.1, 1.0).unit ();
	DentParams p = Params (Vector (0.1, 0.2, 0.0), n, 2.0, 0.1, 0.0);
	double s1 = DentMath::VolumeFactor (p, one.View ()), s2 = DentMath::VolumeFactor (p, rev.View ()), s3 = DentMath::VolumeFactor (p, same.View ());
	NOTE ("U10 S one-sided %.15g, double-sided %.15g, duplicated %.15g\n", s1, s2, s3);
	REQUIRE(s1 > 0.0);
	REQUIRE(Bits (s1, s2));
	REQUIRE(Bits (s1, s3));
}

// U11: refinement

TEST_CASE("U11 refinement on BLOCK walls, roof edge, second dent, random welded mesh", "[dent]")
{
	const DentMaterial &rc = *DentMath::FindMaterial ("building_rc");
	const double rb = 0.5 * std::sqrt (40.0 * 40.0 + 15.0 * 15.0 + 30.0 * 30.0);
	DentObject b0 = Brighton ();
	double open0;
	REQUIRE(NTri (b0) == 10);
	REQUIRE(NVtx (b0) == 20);
	REQUIRE(TJunctions (b0, &open0) == 0);
	const DentVtx *v0 = b0.rest[0].data ();
	Vector n = N (v0[0]).unit ();
	Vector c = (P (v0[0]) + P (v0[1]) + P (v0[2]) + P (v0[3])) / 4.0;
	c.y = 4.0;
	const double e = 0.3 * std::pow (0.1, 0.25);
	const double EB = 0.5 * 11000.0 * 10.0 * 10.0 * (1.0 - e * e) * (1.0 - 0.01) * 0.5 / 0.9;  // DG 10 m/s, wall share (2.4)
	const double ep = 0.3 * std::pow (0.2, 0.25);
	const double EP = 0.5 * 1250.0 * 5.0 * 5.0 * (1.0 - ep * ep) * (1.0 - 0.04) * 0.5 / 0.9;  // ShuttlePB 5 m/s
	auto check = [&] (const DentObject &o, const std::vector<DentParams> &rec, const BDent &d, const char *what) {
		double open;
		size_t tj = TJunctions (o, &open);
		double gap = WeldGap (o);
		NOTE ("U11 %s: R %.3f m, +%d triangles (total %zu, %zu vertices), h %.4f m, T %.2f, swept %.6f / target %.6f m^3 (rel %.2e), "
			"T-junctions %zu, weld gap %.1e, open edge length %.6f (rest %.6f)\n",
			what, d.p.R, d.added, NTri (o), NVtx (o), d.p.h, d.p.T, d.dV, d.V, d.dV / d.V - 1.0, tj, gap, open, open0);
		REQUIRE(d.rc == DENT_OK);
		REQUIRE(d.added >= 0);
		REQUIRE(d.added <= (int)DENT_REFINE_NEW);
		REQUIRE(tj == 0);
		REQUIRE(gap == 0.0);
		REQUIRE(std::fabs (open - open0) < 1e-4);
		REQUIRE(std::fabs (d.dV / d.V - 1.0) < 1e-9);
		REQUIRE(MatchesReplay (o, rec));
	};
	// mid wall, DG 10 m/s, then an overlapping ShuttlePB dent
	DentObject b = b0;
	std::vector<DentParams> rec;
	std::vector<std::vector<uint8_t>> dirty;
	BDent d1 = BuildingDent (b, rec, c, n, EB, rc, 0.3, 0.5 * rb, rb, &dirty);
	check (b, rec, d1, "mid wall, DG 10 m/s");
	REQUIRE(std::fabs (d1.p.R - 1.327) < 1e-3);
	REQUIRE(std::fabs (d1.p.h - 0.398) < 2e-3);
	{
		std::vector<Vector> rs;
		DentMath::FaceNormalSums (b.rest, b.idx, b.weld, b.nweld, rs);
		std::vector<uint8_t> tch (b.nweld, 0);
		for (size_t g = 0; g < dirty.size (); g++)
			for (size_t i = 0; i < dirty[g].size (); i++) if (dirty[g][i]) tch[b.weld[g][i]] = 1;
		DentMath::Normals (b.rest, rs, b.idx, b.weld, b.nweld, tch, b.cur);
		double tilt = 0.0, unit = 0.0;
		size_t far10 = 0;
		for (size_t g = 0; g < b.cur.size (); g++)
			for (size_t i = 0; i < b.cur[g].size (); i++) {
				tilt = std::max (tilt, AngleDeg (N (b.cur[g][i]), N (b.rest[g][i])));
				unit = std::max (unit, std::fabs (N (b.cur[g][i]).length () - 1.0));
				if (P (b.cur[g][i]).dist (P (b.rest[g][i])) > 0.1) far10++;
			}
		NOTE ("U11 mid wall normals: max tilt %.1f deg, |n|-1 %.1e, vertices moved > 10 cm %zu\n", tilt, unit, far10);
		REQUIRE(unit < 1e-5);
	}
	BDent d2 = BuildingDent (b, rec, c + Vector (0.0, -0.5, 0.0), n, EP, rc, 0.1, 0.5 * rb, rb);
	check (b, rec, d2, "overlapping ShuttlePB 5 m/s");
	// roof edge (crosses wall, side and roof groups)
	DentObject br = b0;
	std::vector<DentParams> rr;
	Vector c2 = P (v0[2]) + (P (v0[3]) - P (v0[2])) * (0.3 / 40.0);
	c2.y = 14.5;
	BDent d3 = BuildingDent (br, rr, c2, n, EB, rc, 0.3, 0.5 * rb, rb);
	check (br, rr, d3, "roof edge, DG 10 m/s");
	// small dent on a fresh wall
	DentObject bp = b0;
	std::vector<DentParams> rp;
	BDent d4 = BuildingDent (bp, rp, c, n, EP, rc, 0.1, 0.5 * rb, rb);
	check (bp, rp, d4, "ShuttlePB 5 m/s");
	// tjunc.py: two overlapping refinements without dents
	for (int k = 0; k < 2; k++) {
		DentObject bt = b0;
		Vector ct = k == 0 ? c : c2;
		int a1 = DentMath::Refine (bt, ct, 1.327, nullptr, 0, nullptr);
		int a2 = DentMath::Refine (bt, ct + Vector (0.0, -2.0, 0.0), 1.0, nullptr, 0, nullptr);
		double open;
		size_t tj = TJunctions (bt, &open);
		NOTE ("U11 two refinements (%s): +%d +%d, live triangles %zu, T-junctions %zu, open edge length %.6f\n", k ? "roof edge" : "mid wall", a1, a2, NTri (bt), tj, open);
		REQUIRE(a1 > 0);
		REQUIRE(a2 > 0);
		REQUIRE(tj == 0);
		REQUIRE(std::fabs (open - open0) < 1e-4);
	}
	// same result twice, bitwise
	{
		DentObject x = b0, y = b0;
		std::vector<DentParams> rx, ry;
		BuildingDent (x, rx, c, n, EB, rc, 0.3, 0.5 * rb, rb);
		BuildingDent (x, rx, c + Vector (0.0, -0.5, 0.0), n, EP, rc, 0.1, 0.5 * rb, rb);
		BuildingDent (y, ry, c, n, EB, rc, 0.3, 0.5 * rb, rb);
		BuildingDent (y, ry, c + Vector (0.0, -0.5, 0.0), n, EP, rc, 0.1, 0.5 * rb, rb);
		REQUIRE(SameObj (x, y));
		REQUIRE(SameVtx (std::vector<std::vector<DentVtx>> (x.rest), b.rest));
	}
}

TEST_CASE("U11 refinement on a random welded mesh, determinism and caps", "[dent]")
{
	// 25 x 10 quads (500 triangles) with jitter, three groups with duplicated seam vertices
	auto build = [] () {
		DentObject o;
		auto pos = [] (int i, int j) {
			Rng r { (uint64_t)(i * 131 + j * 7919 + 17) };
			double jx = r.U () - 0.5, jy = r.U () - 0.5, jz = r.U () - 0.5;
			return Vector (0.4 * i + (i % 25 ? 0.08 * jx : 0.0), 0.4 * j + (j % 10 ? 0.08 * jy : 0.0), 0.05 * jz);
		};
		const int cut[4] = { 0, 8, 17, 25 };
		for (int g = 0; g < 3; g++) {
			int nx = cut[g+1] - cut[g];
			std::vector<DentVtx> v;
			std::vector<uint16_t> I;
			for (int j = 0; j <= 10; j++)
				for (int i = 0; i <= nx; i++) {
					Vector p = pos (cut[g] + i, j);
					v.push_back ({ (float)p.x, (float)p.y, (float)p.z, 0.0f, 0.0f, 1.0f, (float)i, (float)j });
				}
			for (int j = 0; j < 10; j++)
				for (int i = 0; i < nx; i++) {
					uint16_t a = (uint16_t)(j * (nx + 1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(b + nx + 1), d = (uint16_t)(a + nx + 1);
					if ((i + j) % 2) I.insert (I.end (), { a, b, c, a, c, d });
					else I.insert (I.end (), { a, b, d, b, c, d });
				}
			o.rest.push_back (v), o.cur.push_back (v), o.idx.push_back (I);
		}
		o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
		return o;
	};
	const DentMaterial m = Mat (0.5e6, 3.0);
	auto run = [&] (DentObject &o, std::vector<DentParams> &rec, bool checks) {
		Rng r { 2026 };
		int ok = 0;
		for (int k = 0; k < 6; k++) {
			const uint32_t nx[3] = { 8, 9, 8 };
			uint32_t g = r.I (3), i = 3 + r.I (nx[g] - 5), j = 3 + r.I (5);
			Vector c = P (o.rest[g][j * (nx[g] + 1) + i]); // interior: the open boundary never moves
			double R = r.U (0.3, 0.8);
			double E = R * R * R * Pi * DENT_KAPPA / 3.0 * m.sigma_c;
			BDent d = BuildingDent (o, rec, c, Vector (0, 0, 1), E, m, 0.05, 1.0, 10.0);
			if (!checks) continue;
			double open;
			size_t tj = TJunctions (o, &open);
			bool capped = !(std::fabs (d.hS / d.V - 1.0) < 1e-12);
			NOTE ("U11 random mesh dent %d: group %u, R %.3f +%d triangles (total %zu), result %d, h %.4f%s, swept/shown rel %.2e, T-junctions %zu\n",
				k, g, d.p.R, d.added, NTri (o), d.rc, d.p.h, capped ? " (capped)" : "", d.rc == DENT_OK ? d.dV / d.hS - 1.0 : 0.0, tj);
			REQUIRE((d.rc == DENT_OK || d.rc == DENT_SMALL));
			REQUIRE(d.added >= 0);
			REQUIRE(tj == 0);
			REQUIRE(WeldGap (o) == 0.0);
			REQUIRE(MatchesReplay (o, rec));
			if (d.rc != DENT_OK) continue; // fully capped by earlier dents
			ok++;
			REQUIRE(std::fabs (d.dV / d.hS - 1.0) < 1e-9);
			if (!capped) REQUIRE(std::fabs (d.dV / d.V - 1.0) < 1e-9);
		}
		if (checks) REQUIRE(ok >= 4);
	};
	DentObject a = build (), b = build ();
	REQUIRE(NTri (a) == 500);
	REQUIRE(a.nweld == 26 * 11);
	std::vector<DentParams> ra, rb;
	run (a, ra, true);
	run (b, rb, false);
	REQUIRE(SameObj (a, b));
	// caps: no vertex room in any group
	DentObject f = build (), f0 = f;
	const uint32_t none[3] = { 0, 0, 0 };
	REQUIRE(DentMath::Refine (f, Vector (5.0, 2.0, 0.0), 0.5, nullptr, 0, none) == -1);
	REQUIRE(SameObj (f, f0));
	const uint32_t room[3] = { 0, 65535, 0 };
	int ok = DentMath::Refine (f, P (f.rest[1][5 * 10 + 4]), 0.4, nullptr, 0, room);
	REQUIRE(ok > 0);
	REQUIRE(f.rest[0].size () == f0.rest[0].size ()); // the room limits refinement to group 1 here
	// 4,096 new triangles per dent: ten stacked plates need more
	auto stack = [] (int layers) {
		DentObject o;
		for (int l = 0; l < layers; l++) AddGrid (o, Vector (-20, -20, -0.05 * l), Vector (40, 0, 0), Vector (0, 40, 0), 1, 1, Vector (0, 0, 1));
		o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
		return o;
	};
	DentObject s2 = stack (2), s10 = stack (10), s10c = s10;
	int add2 = DentMath::Refine (s2, Vector (0.3, 0.2, 0.0), 1.0, nullptr, 0, nullptr);
	int add10 = DentMath::Refine (s10, Vector (0.3, 0.2, 0.0), 1.0, nullptr, 0, nullptr);
	NOTE ("U11 caps: two plates +%d, ten plates %d (cap %u)\n", add2, add10, DENT_REFINE_NEW);
	REQUIRE(add2 > 0);
	REQUIRE(add2 <= (int)DENT_REFINE_NEW);
	REQUIRE(add10 == -1);
	REQUIRE(SameObj (s10, s10c));
	// 65,536 triangles per object
	DentObject big;
	AddGrid (big, Vector (-45, -45, 0), Vector (0.5, 0, 0), Vector (0, 0.5, 0), 181, 181, Vector (0, 0, 1));
	big.nweld = DentMath::WeldMap (big.rest, DENT_WELD, big.weld);
	REQUIRE(NTri (big) == 65522);
	DentObject big0 = big;
	REQUIRE(DentMath::Refine (big, Vector (0.1, 0.2, 0.0), 1.0, nullptr, 0, nullptr) == -1);
	REQUIRE(SameObj (big, big0));
	DentObject small;
	AddGrid (small, Vector (-5, -5, 0), Vector (0.5, 0, 0), Vector (0, 0.5, 0), 20, 20, Vector (0, 0, 1));
	small.nweld = DentMath::WeldMap (small.rest, DENT_WELD, small.weld);
	REQUIRE(DentMath::Refine (small, Vector (0.1, 0.2, 0.0), 1.0, nullptr, 0, nullptr) > 14);
}

// U8, U12, U13, U17: records, text, keys

TEST_CASE("U8 record format round trip and quantisation", "[dent]")
{
	// the example lines of D4 9.2 and 9.4
	{
		DentVesselParser vp;
		REQUIRE(vp.Line ("  XDMG 1 15470.3 1"));
		REQUIRE(vp.Line ("  XDMGM 0 0 999a2640 7 140 ShuttlePB"));
		REQUIRE(vp.Line ("  XDMGD 0 0 -0.3 2.4 0 0 1 1.23104 0.0411352 0 *"));
		REQUIRE_FALSE(vp.Line ("  STATUS Landed Earth"));
		DentVesselText v;
		vp.Finish (v);
		REQUIRE(vp.Skipped () == 0);
		REQUIRE(v.rec.size () == 1);
		REQUIRE(v.rec[0].key == DentMath::MeshKey ("ShuttlePB"));
		REQUIRE((v.rec[0].ngrp == 7 && v.rec[0].nvtx == 140 && v.rec[0].slot == 0 && v.rec[0].grp.empty ()));
		REQUIRE((v.eabs == 15470.3 && v.flags == 1 && v.slotName.size () == 1 && v.slotName[0] == "ShuttlePB"));
		std::vector<std::string> l;
		DentMath::FormatVessel (v, "  ", l);
		REQUIRE(l == std::vector<std::string> ({ "  XDMG 1 15470.3 1", "  XDMGM 0 0 999a2640 7 140 ShuttlePB", "  XDMGD 0 0 -0.3 2.4 0 0 1 1.23104 0.0411352 0 *" }));
		const std::vector<std::string> bl = { "BEGIN_XDMG_BASES", "BASE Moon:Brighton Beach", "OBJ 1 BLOCK -60.6 -35 293900 0",
			"ODENT 1 -53.12 4 -47.99 0.5 0 -0.866025404 1.32716 0.398231 30", "END_BASE", "END_XDMG_BASES" };
		DentBasesParser bp;
		for (size_t i = 1; i + 1 < bl.size (); i++) REQUIRE(bp.Line (bl[i].c_str ()));
		REQUIRE_FALSE(bp.Line (bl.back ().c_str ()));
		std::vector<DentBaseText> bt;
		bp.Finish (bt);
		REQUIRE(bp.Skipped () == 0);
		REQUIRE((bt.size () == 1 && bt[0].planet == "Moon" && bt[0].name == "Brighton Beach" && bt[0].obj.size () == 1 && bt[0].rec.size () == 1));
		std::vector<std::string> l2;
		DentMath::FormatBases (bt, l2);
		REQUIRE(l2 == bl);
	}
	Rng r { 8 };
	DentObject geo = Floor3 ();
	DentVesselText v;
	v.eabs = 15470.3, v.flags = 5;
	v.slotName = { "ShuttlePB", "", "DG\\DeltaGlider" };
	for (int i = 0; i < 40; i++) {
		DentRecord d {};
		d.slot = r.I (3), d.key = DentMath::MeshKey (v.slotName[d.slot].c_str ()), d.ngrp = 3, d.nvtx = 500;
		d.p = Params (Vector (r.U (0, 4), r.U (0, 4), r.U (-0.1, 0.1)), Vector (r.U (-0.5, 0.5), r.U (-0.5, 0.5), 1.0).unit (), r.U (0.2, 1.5), r.U (0, 0.3), r.I (2) ? 0.0 : r.U (0.5, 3));
		for (uint32_t k = r.I (4); k > 0; k--) d.grp.push_back ((uint16_t)(k - 1));
		DentParams q = d.p;
		DentMath::Quantise (d.p);
		DentParams q2 = d.p;
		DentMath::Quantise (q2);
		REQUIRE(SameP (d.p, q2)); // idempotent
		REQUIRE(std::fabs (d.p.R / q.R - 1.0) < 1e-8);
		v.rec.push_back (d);
	}
	std::vector<std::string> lines;
	DentMath::FormatVessel (v, "  ", lines);
	DentVesselParser vp;
	for (const std::string &l : lines) REQUIRE(vp.Line (l.c_str ()));
	DentVesselText w;
	vp.Finish (w);
	REQUIRE(vp.Skipped () == 0);
	REQUIRE(w.rec.size () == v.rec.size ());
	for (size_t i = 0; i < v.rec.size (); i++) REQUIRE(SameRec (v.rec[i], w.rec[i]));
	REQUIRE((Bits (w.eabs, v.eabs) && w.flags == v.flags));
	// geometry from the parsed records equals the live geometry bitwise
	DentObject g1 = geo, g2 = geo;
	for (size_t i = 0; i < v.rec.size (); i++) {
		DentMath::Apply (v.rec[i].p, g1.rest, g1.cur, v.rec[i].grp.data (), v.rec[i].grp.size (), nullptr);
		DentMath::Apply (w.rec[i].p, g2.rest, g2.cur, w.rec[i].grp.data (), w.rec[i].grp.size (), nullptr);
	}
	REQUIRE(SameVtx (g1.cur, g2.cur));
	REQUIRE_FALSE(SameVtx (g1.cur, g1.rest));
	// recorder payloads
	for (const DentRecord &d : v.rec) {
		std::vector<std::string> pl;
		DentMath::FormatDentEvent (d, pl);
		REQUIRE(pl.size () == 1);
		DentRecord e {};
		REQUIRE(DentMath::ParseDentEvent (pl[0].c_str (), e));
		REQUIRE((SameP (e.p, d.p) && e.slot == d.slot && e.grp == d.grp));
		std::string bpl = DentMath::FormatBaseDentEvent (3, 1, 17, d.p);
		int pa = 0, pb = 0, po = 0;
		DentParams bpp {};
		REQUIRE(DentMath::ParseBaseDentEvent (bpl.c_str (), pa, pb, po, bpp));
		REQUIRE((pa == 3 && pb == 1 && po == 17 && SameP (bpp, d.p)));
	}
	double ea;
	uint32_t fl;
	REQUIRE(DentMath::ParseStateEvent (DentMath::FormatStateEvent (293900.123456, 5).c_str (), ea, fl));
	REQUIRE((ea == 293900.123 && fl == 5)); // %.9g
	REQUIRE_FALSE(DentMath::ParseStateEvent ("nan 1", ea, fl));
	DentRecord bad {};
	REQUIRE_FALSE(DentMath::ParseDentEvent ("0 1 2 3 0 0 1 0.5 0.1", bad));
}

TEST_CASE("U12 line writers: long names, 60 groups, 512 records, extreme values", "[dent]")
{
	// extremes inside the DENT_LIM_* limits; tiny exponents give the longest numbers (16 characters)
	Rng r { 12 };
	const double ext[] = { 1e-300, -1e-300, DENT_LIM_POS, -9.87654321e6, -1.23456789e-300, 0.0, -0.0, 3.14159265358979, -987654.321123 };
	const double pos[] = { 1e-300, DENT_LIM_R, 1.23456789e-300, 9.87654321e3, 0.5 };
	const double nng[] = { 0.0, 1e-300, DENT_LIM_H, 1.23456789e-300, 7.5 };
	auto pick = [&] (const double *a, size_t n) { return a[r.I ((uint32_t)n)]; };
	DentVesselText v;
	v.eabs = DENT_LIM_E, v.flags = 7;
	for (int s = 0; s < 4; s++) {
		std::string nm = "Mesh";
		while (nm.size () < 255) nm += (r.I (5) == 0) ? std::string ("\xc3\xa9") : std::string (1, "abcXYZ _-.\\/"[r.I (12)]);
		nm.resize (255);
		v.slotName.push_back (nm);
	}
	for (int i = 0; i < 512; i++) {
		DentRecord d {};
		d.slot = (uint32_t)(i % 4), d.key = DentMath::MeshKey (v.slotName[d.slot].c_str ()), d.ngrp = (uint16_t)(60 + d.slot), d.nvtx = 4000000000u;
		d.p = Params (Vector (pick (ext, 9), pick (ext, 9), pick (ext, 9)), Vector (r.U (-1, 1), r.U (-1, 1), r.U (0.2, 1)).unit (), pick (pos, 5), pick (nng, 5), pick (nng, 5));
		DentMath::Quantise (d.p);
		if (i % 3 == 0)
			while (d.grp.size () < 60) {
				uint16_t g = (uint16_t)(65535 - r.I (40000));
				if (std::find (d.grp.begin (), d.grp.end (), g) == d.grp.end ()) d.grp.push_back (g);
			}
		else if (i % 3 == 2) d.grp = { (uint16_t)r.I (65536), 65535 };
		v.rec.push_back (d);
	}
	std::vector<std::string> lines, back;
	DentMath::FormatVessel (v, "  ", lines);
	size_t longest = 0;
	for (const std::string &l : lines) longest = std::max (longest, l.size ());
	REQUIRE(longest <= 200);
	const size_t longestVessel = longest;
	REQUIRE(ReadLines256 (lines, back) == lines.size ());
	DentVesselParser vp;
	for (const std::string &l : back) REQUIRE(vp.Line (l.c_str ()));
	DentVesselText w;
	vp.Finish (w);
	REQUIRE(vp.Skipped () == 0);
	REQUIRE(w.rec.size () == 512);
	for (size_t i = 0; i < 512; i++) REQUIRE(SameRec (v.rec[i], w.rec[i]));
	REQUIRE((Bits (w.eabs, v.eabs) && w.flags == v.flags));
	for (size_t s = 0; s < 4; s++) { // names cut to the line, never inside a UTF-8 sequence
		REQUIRE(v.slotName[s].compare (0, w.slotName[s].size (), w.slotName[s]) == 0);
		REQUIRE((unsigned char)w.slotName[s].back () != 0xc3);
	}
	// building section with a base name that does not fit, 64 objects, extreme records
	std::vector<DentBaseText> bases (2);
	bases[0].planet = "Moon", bases[0].name = std::string (300, 'B');
	bases[1].planet = "Earth", bases[1].name = "Cape Canaveral";
	for (int i = 0; i < 64; i++) {
		bases[0].obj.push_back ({ (uint32_t)i, "HANGAR2", pick (ext, 9) / 7.0, -35.04, pick (pos, 5), (uint32_t)i });
		DentRecord d {};
		d.slot = (uint32_t)i;
		d.p = Params (Vector (pick (ext, 9), pick (ext, 9), pick (ext, 9)), Vector (0, 1, 0), pick (pos, 5), pick (nng, 5), pick (nng, 5));
		DentMath::Quantise (d.p);
		bases[i % 2].rec.push_back (d);
	}
	std::vector<std::string> bl;
	DentMath::FormatBases (bases, bl);
	longest = 0;
	for (const std::string &l : bl) longest = std::max (longest, l.size ());
	REQUIRE(longest <= 200);
	REQUIRE(ReadLines256 (bl, back) == bl.size ());
	DentBasesParser bp;
	for (size_t i = 1; i + 1 < back.size (); i++) REQUIRE(bp.Line (back[i].c_str ()));
	REQUIRE_FALSE(bp.Line (back.back ().c_str ()));
	REQUIRE_FALSE(bp.Line ("OBJ 1 BLOCK 0 0 0 0")); // after END_XDMG_BASES
	std::vector<DentBaseText> bt;
	bp.Finish (bt);
	REQUIRE(bp.Skipped () == 0);
	REQUIRE(bt.size () == 2);
	REQUIRE((bt[0].name.empty () && bt[0].nameHash == DentMath::Fnv1a (bases[0].name.data (), bases[0].name.size ())));
	REQUIRE(bt[1].name == "Cape Canaveral");
	REQUIRE((bt[0].obj.size () == 64 && bt[0].obj[5].type == "HANGAR2" && bt[0].obj[5].z == -35.0));
	for (int b = 0; b < 2; b++) {
		REQUIRE(bt[b].rec.size () == bases[b].rec.size ());
		for (size_t i = 0; i < bt[b].rec.size (); i++) REQUIRE(SameRec (bt[b].rec[i], bases[b].rec[i]));
	}
	// recorder payloads: groups split, each at most 180
	size_t lp = 0, np = 0;
	for (const DentRecord &d : v.rec) {
		std::vector<std::string> pl;
		DentMath::FormatDentEvent (d, pl);
		std::vector<uint16_t> all;
		for (const std::string &s : pl) {
			lp = std::max (lp, s.size ());
			DentRecord e {};
			REQUIRE(DentMath::ParseDentEvent (s.c_str (), e));
			REQUIRE(SameP (e.p, d.p));
			all.insert (all.end (), e.grp.begin (), e.grp.end ());
		}
		np += pl.size ();
		REQUIRE(all == d.grp);
	}
	NOTE ("U12 vessel: %zu lines for 512 records, longest %zu; bases: %zu lines, longest %zu; DENT payloads %zu, longest %zu\n", lines.size (), longestVessel, bl.size (), longest, np, lp);
	REQUIRE(lp <= 180);
	// values beyond the limits: records are not written, eabs is clamped, every line still <= 200
	DentVesselText x;
	x.eabs = 1e300;
	x.rec.push_back (v.rec[1]);
	for (double big : { 1e300, -1.23456789e300, 2e7 }) {
		DentRecord d = v.rec[1];
		d.p.c.y = big;
		x.rec.push_back (d);
		d = v.rec[1], d.p.R = big;
		x.rec.push_back (d);
		d = v.rec[1], d.p.h = big;
		x.rec.push_back (d);
	}
	std::vector<std::string> xl = Format (x);
	REQUIRE(xl.size () == 3);
	REQUIRE(xl[0] == "  XDMG 1 1e+30 0");
	for (const std::string &l : xl) REQUIRE(l.size () <= 200);
	REQUIRE(ParseVessel (xl).rec.size () == 1);
}

TEST_CASE("U13 tolerant parser fuzz", "[dent]")
{
	// crafted bad lines: each skipped and counted once
	{
		DentVesselParser vp;
		const char *ok[] = { "XDMG 1 100 0", "XDMGM 0 2 0000abcd 4 300", "XDMGD 0 1 2 3 0 0 1 0.5 0.1 0 *",
			"\t xdmgd\t0 1 2 3 0 0 2 0.5 0.1 0 1,2\r\n", "XDMGD 5 1 2 3 0 0 1 0.5 0.1 0 3", "XDMGM 5 1 ffffffff 2 9 late mesh" };
		const char *bad[] = { "XDMGD 0 nan 2 3 0 0 1 0.5 0.1 0 *", "XDMGD 0 1 2 3 0 0 1 0.5", "XDMGD 0 1 2 3 0 0 1 -0.5 0.1 0 *",
			"XDMGD 0 1 2 3 0 0 5 0.5 0.1 0 *", "XDMGD 0 1 2 3 0 0 1 0.5 0.1 0 2,70000", "XDMGD 9 1 2 3 0 0 1 0.5 0.1 0 *",
			"XDMGM x 2 0000abcd 4 300", "XDMGD 0 1 2 3 0 0 1 0.5 -0.1 0 *", "XDMGD 0 1 2 3 0 0 1 0.5 0.1 -1 *", "XDMGD 0 1 2 3 0 0 1 0.5 0.1 0 1,,2",
			"XDMGM 0 7 00000001 1 1", "XDMG 1 inf 0", "XDMGD 0 1 2 3 0 0 1 1e999 0.1 0 *" };
		for (const char *l : ok) REQUIRE(vp.Line (l));
		for (const char *l : bad) REQUIRE(vp.Line (l));
		REQUIRE_FALSE(vp.Line ("XDMGX 1 2 3"));
		REQUIRE_FALSE(vp.Line ("XDM 1"));
		REQUIRE_FALSE(vp.Line (""));
		DentVesselText v;
		vp.Finish (v);
		NOTE ("U13 crafted: %zu records, %d skipped of %zu bad lines\n", v.rec.size (), vp.Skipped (), sizeof (bad) / sizeof (bad[0]));
		REQUIRE(vp.Skipped () == (int)(sizeof (bad) / sizeof (bad[0])));
		REQUIRE(v.rec.size () == 3);
		REQUIRE((v.rec[1].p.n.z == 1.0 && v.rec[1].grp == std::vector<uint16_t> ({ 1, 2 }))); // normalised, tabs and CRLF
		REQUIRE((v.rec[2].slot == 1 && v.rec[2].key == 0xffffffffu)); // XDMGD before its XDMGM
		REQUIRE((v.eabs == 100.0 && v.slotName.size () == 2 && v.slotName[1] == "late mesh"));
	}
	// unknown section verbatim (with lines before 1st XDMG), lines > 200 dropped; XDMG 1 starts v1
	{
		DentVesselParser vp;
		std::string longl = "XDMGD 0 " + std::string (300, '7');
		REQUIRE(vp.Line ("  XDMGM 0 0 0000abcd 4 300 a b"));
		REQUIRE(vp.Line ("  XDMG 2 99 0 future fields"));
		REQUIRE(vp.Line ("  XDMGD 0 whatever comes in v2"));
		REQUIRE(vp.Line (longl.c_str ()));
		REQUIRE(vp.Line ("  XDMG 1 5 0"));
		DentVesselText v;
		vp.Finish (v);
		REQUIRE(v.rec.empty ());
		REQUIRE(v.verbatim == std::vector<std::string> ({ "XDMGM 0 0 0000abcd 4 300 a b", "XDMG 2 99 0 future fields", "XDMGD 0 whatever comes in v2" }));
		REQUIRE(v.eabs == 5.0);
		REQUIRE(vp.Skipped () == 1);
		std::vector<std::string> l;
		DentMath::FormatVessel (v, "  ", l);
		REQUIRE(l.size () == 4);
		REQUIRE(l.back () == "  XDMG 1 5 0");
		for (const std::string &s : l) REQUIRE((s.size () <= 200 && s.find ("7777") == std::string::npos));
	}
	// random mutations of valid blocks
	Rng r { 13 };
	const char *junk[] = { "nan", "inf", "-1", "abc", "1e999", "0x10", "99999999999", "*", ",", "-0", "+3", "65536", "1,", "" };
	std::vector<std::string> base;
	{
		DentVesselText v;
		v.eabs = 12.5, v.flags = 1, v.slotName = { "m0", "m1" };
		for (int i = 0; i < 6; i++) {
			DentRecord d {};
			d.slot = (uint32_t)(i % 2), d.key = 77u + d.slot, d.ngrp = 9, d.nvtx = 99;
			d.p = Params (Vector (i, -i, 0.5), Vector (0, 0, 1), 0.7, 0.05, i % 2 ? 0.0 : 2.0);
			if (i % 2) for (uint16_t g = 0; g < 50; g++) d.grp.push_back ((uint16_t)(g * 977));
			v.rec.push_back (d);
		}
		DentMath::FormatVessel (v, "  ", base);
	}
	size_t lines = 0, skipped = 0, kept = 0;
	for (int it = 0; it < 400; it++) {
		DentVesselParser vp;
		for (const std::string &l0 : base) {
			std::string l = l0;
			switch (r.I (9)) {
			case 0: l.resize (r.I ((uint32_t)l.size () + 1)); break;
			case 1: { size_t p = l.find (' ', 2 + r.I ((uint32_t)l.size ())); if (p != std::string::npos) l.insert (p + 1, std::string (junk[r.I (14)]) + " "); } break;
			case 2: for (char &c : l) if (c == ' ' && r.I (3) == 0) c = '\t'; break;
			case 3: l += "\r"; break;
			case 4: for (char &c : l) if (c >= 'A' && c <= 'Z' && r.I (2)) c = (char)(c - 'A' + 'a'); break;
			case 5: { size_t p = r.I ((uint32_t)l.size ()); l.replace (p, std::min<size_t> (3, l.size () - p), junk[r.I (14)]); } break;
			case 6: if (l.find ("XDMG ") != std::string::npos) l = "  XDMG " + std::to_string (r.I (4)) + " 1 0"; break;
			case 7: l += std::string (r.I (300), r.I (2) ? ' ' : 'x'); break;
			default: break;
			}
			vp.Line (l.c_str ());
			lines++;
		}
		DentVesselText v;
		vp.Finish (v);
		skipped += (size_t)vp.Skipped ();
		std::vector<std::string> out;
		DentMath::FormatVessel (v, "  ", out);
		kept += v.rec.size ();
		for (const std::string &s : out) REQUIRE(s.size () <= 200);
		for (const DentRecord &d : v.rec) REQUIRE((d.p.R > 0.0 && d.p.h >= 0.0 && d.p.T >= 0.0 && std::fabs (d.p.n.length () - 1.0) < 1e-6));
		// what was kept survives a save and load unchanged (record count, numbers, groups, sections)
		int sk = -1;
		DentVesselText w = ParseVessel (out, &sk);
		REQUIRE(sk == 0);
		REQUIRE(w.rec.size () == v.rec.size ());
		for (size_t i = 0; i < v.rec.size (); i++) REQUIRE(SameRec (w.rec[i], v.rec[i]));
		REQUIRE((Bits (w.eabs, v.eabs) && w.flags == v.flags && w.verbatim == v.verbatim));
	}
	// base section fuzz
	std::vector<std::string> bl;
	{
		std::vector<DentBaseText> b (1);
		b[0].planet = "Moon", b[0].name = "Brighton Beach";
		b[0].obj.push_back ({ 1, "BLOCK", -60.62, -35.0, 293900.0, 0 });
		for (int i = 0; i < 4; i++) {
			DentRecord d {};
			d.slot = 1;
			d.p = Params (Vector (-53.12, 4.0 + i, -47.99), Vector (0.5, 0, -0.866025404), 1.32716, 0.398231, 30.0);
			b[0].rec.push_back (d);
		}
		DentMath::FormatBases (b, bl);
	}
	size_t bskip = 0;
	for (int it = 0; it < 400; it++) {
		DentBasesParser bp;
		for (size_t i = 1; i < bl.size (); i++) {
			std::string l = bl[i];
			if (r.I (3) == 0) l.resize (r.I ((uint32_t)l.size () + 1));
			else if (r.I (3) == 0) { size_t p = r.I ((uint32_t)l.size ()); l.replace (p, std::min<size_t> (2, l.size () - p), junk[r.I (14)]); }
			if (!bp.Line (l.c_str ())) break;
		}
		std::vector<DentBaseText> out;
		bp.Finish (out);
		bskip += (size_t)bp.Skipped ();
		std::vector<std::string> back;
		DentMath::FormatBases (out, back);
		for (const std::string &s : back) REQUIRE(s.size () <= 200);
	}
	NOTE ("U13 fuzz: %zu vessel lines, %zu skipped, %zu records kept; base section skips %zu\n", lines, skipped, kept, bskip);
	REQUIRE(skipped > 0);
	REQUIRE(kept > 0);
	REQUIRE(bskip > 0);
}

TEST_CASE("U17 key hash", "[dent]")
{
	REQUIRE(DentMath::Fnv1a ("", 0) == 0x811c9dc5u);
	REQUIRE(DentMath::Fnv1a ("a", 1) == 0xe40c292cu);
	REQUIRE(DentMath::Fnv1a ("foobar", 6) == 0xbf9cf968u);
	REQUIRE(DentMath::MeshKey ("ShuttlePB") == 0x999a2640u); // D4 9.2 example line
	REQUIRE(DentMath::MeshKey ("DG\\DeltaGlider") == DentMath::MeshKey ("dg/deltaglider"));
	REQUIRE(DentMath::MeshKey ("DG\\DeltaGlider") == DentMath::Fnv1a ("dg/deltaglider", 14));
	REQUIRE(DentMath::MeshKey ("DG\\DeltaGlider") != DentMath::MeshKey ("dg/deltaglider2"));
	REQUIRE(DentMath::MeshKey (nullptr) == DentMath::Fnv1a ("#", 1));
	REQUIRE(DentMath::MeshKey ("") == DentMath::Fnv1a ("#", 1));
}

// U9, U14, U15, U16: energy and coalescing

TEST_CASE("U9 energy split, plastic factor, per-side v_el", "[dent]")
{
	const DentMaterial &al = *DentMath::FindMaterial ("AL_Structure"), &rc = *DentMath::FindMaterial ("building_rc"), &gear = *DentMath::FindMaterial ("gear");
	REQUIRE(DentMath::FindMaterial ("unobtainium") == nullptr);
	REQUIRE(DentMath::FindMaterial ("al_structure_x") == nullptr);
	REQUIRE((al.sigma_c == 0.5e6 && rc.sigma_c == 0.4e6 && gear.v_el == 3.0 && DentMath::FindMaterial ("glass")->host));
	REQUIRE(std::string (DentMath::DefaultMaterial (-1).id) == "al_structure");
	REQUIRE(std::string (DentMath::DefaultMaterial (DENTB_BLOCK).id) == "building_rc");
	REQUIRE(std::string (DentMath::DefaultMaterial (DENTB_HANGAR3).id) == "steel_clad");
	REQUIRE(std::string (DentMath::DefaultMaterial (DENTB_TANK).id) == "steel_structure");
	REQUIRE(std::string (DentMath::DefaultMaterial (DENTB_MESH).id) == "steel_structure");
	REQUIRE(DentMath::PlasticFactor (1.0, 1.0) == 0.0);
	REQUIRE(DentMath::PlasticFactor (0.5, 1.0) == 0.0);
	REQUIRE(DentMath::PlasticFactor (2.0, 1.0) == 0.75);
	REQUIRE(DentMath::PlasticFactor (0.0, 1.0) == 0.0);
	const double dKE = 12345.6, Wf = 345.6, vn = 2.5;
	double E[2], ea[2];
	REQUIRE(DentMath::SplitEnergy (dKE, Wf, vn, true, al, rc, E, ea));
	const double En = dKE - Wf, wA = rc.sigma_c / (al.sigma_c + rc.sigma_c), r = 1.0 / vn, f = 1.0 - r * r;
	REQUIRE(E[0] == En * wA * f);
	REQUIRE(E[1] == En * (1.0 - wA) * f);
	REQUIRE(ea[0] == E[0] + 0.25 * Wf);
	REQUIRE(ea[1] == E[1] + 0.25 * Wf);
	// gear side: vn below its v_el gives nothing, the hull side keeps its share (no transfer)
	REQUIRE(DentMath::SplitEnergy (dKE, Wf, vn, true, gear, al, E, ea));
	REQUIRE((E[0] == 0.0 && ea[0] == 0.0));
	REQUIRE(E[1] == En * 0.5 * f);
	REQUIRE(DentMath::SplitEnergy (dKE, Wf, 3.5, true, gear, al, E, ea));
	REQUIRE(E[0] == En * 0.5 * (1.0 - (3.0 / 3.5) * (3.0 / 3.5)));
	// examples of 2.4 (D3 e(v): e0 0.3, vy 1)
	auto ev = [] (double v) { double e = 0.3 * std::min (1.0, std::pow (1.0 / v, 0.25)); return v > 50.0 ? e * std::max (0.0, (300.0 - v) / 250.0) : e; };
	auto closing = [&] (double m1, double m2, double v) { double me = m2 > 0 ? m1 * m2 / (m1 + m2) : m1, e = ev (v); return 0.5 * me * v * v * (1 - e * e); };
	REQUIRE(DentMath::SplitEnergy (closing (1250, 1250, 10), 0, 10, true, al, al, E, ea));
	NOTE ("U9 ShuttlePB pair 10 m/s: %.4g J each (2.4: 1.50e4)\n", E[0]);
	REQUIRE(std::fabs (E[0] / 1.50e4 - 1.0) < 0.005);
	REQUIRE(DentMath::SplitEnergy (closing (11000, 0, 10), 0, 10, true, al, rc, E, ea));
	NOTE ("U9 DG into BLOCK 10 m/s: DG %.4g J, BLOCK %.4g J (2.4: 2.35e5, 2.94e5)\n", E[0], E[1]);
	REQUIRE(std::fabs (E[0] / 2.35e5 - 1.0) < 0.005);
	REQUIRE(std::fabs (E[1] / 2.94e5 - 1.0) < 0.005);
	// destroyed threshold (5.1) and building mass
	REQUIRE(DentMath::Destroyed (500.0 * DENT_DESTROY_ENERGY, 500.0, 1250.0, DENT_DESTROY_ENERGY));
	REQUIRE_FALSE(DentMath::Destroyed (499.0 * DENT_DESTROY_ENERGY, 500.0, 1250.0, DENT_DESTROY_ENERGY));
	REQUIRE(DentMath::Destroyed (1250.0 * DENT_DESTROY_ENERGY, 0.0, 1250.0, DENT_DESTROY_ENERGY));
	REQUIRE_FALSE(DentMath::Destroyed (1e9, 0.0, 0.0, DENT_DESTROY_ENERGY));
	REQUIRE(DentMath::BuildingMass (DENTB_BLOCK, Vector (40, 15, 30)) == 5.4e6);
	REQUIRE(std::fabs (DentMath::BuildingMass (DENTB_TANK, Vector (10, 20, 10)) - 150.0 * 2000.0 * 0.785) < 1e-6);
	REQUIRE(DentMath::BuildingMass (DENTB_MESH, Vector (10, 10, 10)) == 100.0 * 1000.0 * 0.5);
	REQUIRE(DentMath::BuildingMass (99, Vector (10, 10, 10)) == 0.0);
	// head-on equal pairs reach the default threshold at 56.9 m/s (ShuttlePB, empty) and 89.8 m/s (DG)
	auto vdestroy = [&] (double m, double mempty) {
		double lo = 1.0, hi = 300.0;
		for (int i = 0; i < 100; i++) {
			double v = 0.5 * (lo + hi);
			DentMath::SplitEnergy (closing (m, m, v), 0, v, true, al, al, E, ea);
			(DentMath::Destroyed (ea[0], mempty, m, DENT_DESTROY_ENERGY) ? hi : lo) = v;
		}
		return hi;
	};
	double vpb = vdestroy (1250, 500), vdg = vdestroy (11000, 11000);
	NOTE ("U9 destroyed head-on at %.1f m/s (ShuttlePB) and %.1f m/s (DG)\n", vpb, vdg);
	REQUIRE(std::fabs (vpb - 56.9) < 0.1);
	REQUIRE(std::fabs (vdg - 89.8) < 0.1);
}

TEST_CASE("U14 event rule (Y3')", "[dent]")
{
	const DentMaterial &al = DentMath::DefaultMaterial (-1);
	double E[2], ea[2];
	// no FIRST point: D3 fills dKE = Wf = 0
	REQUIRE(DentMath::SplitEnergy (0.0, 0.0, 3.0, false, al, al, E, ea));
	REQUIRE((E[0] == 0.0 && E[1] == 0.0 && ea[0] == 0.0 && ea[1] == 0.0));
	// FIRST at 0.99 m/s: gate
	REQUIRE(DentMath::SplitEnergy (5000.0, 100.0, 0.99, true, al, al, E, ea));
	REQUIRE((E[0] == 0.0 && ea[0] == 0.0 && E[1] == 0.0 && ea[1] == 0.0));
	// FIRST at exactly 1 m/s on hull material: nothing (strict gate, plastic factor 0)
	REQUIRE(DentMath::SplitEnergy (5000.0, 100.0, 1.0, true, al, al, E, ea));
	REQUIRE((E[0] == 0.0 && ea[0] == 0.0));
	REQUIRE(DentMath::PlasticFactor (1.0, al.v_el) == 0.0);
	// FIRST at 1.5 m/s: counted by 2.1; a RESTING-kind FIRST event is the same call (kind is no damage)
	REQUIRE(DentMath::SplitEnergy (5000.0, 100.0, 1.5, true, al, al, E, ea));
	const double f = 1.0 - (1.0 / 1.5) * (1.0 / 1.5);
	REQUIRE(E[0] == 4900.0 * 0.5 * f);
	REQUIRE(ea[0] == E[0] + 25.0);
	// RESTING kind without FIRST: 0 J from D3, nothing here
	REQUIRE(DentMath::SplitEnergy (0.0, 0.0, 1.5, false, al, al, E, ea));
	REQUIRE((E[0] == 0.0 && ea[0] == 0.0));
	// a non-FIRST event that carries energy is a D3 bug: false (Damage asserts in debug)
	REQUIRE_FALSE(DentMath::SplitEnergy (10.0, 0.0, 1.5, false, al, al, E, ea));
	REQUIRE_FALSE(DentMath::SplitEnergy (0.0, 2.0, 0.5, false, al, al, E, ea));
}

TEST_CASE("U15 energy frame invariance", "[dent]")
{
	// box A on body B and roof K, partner offsets 0, 3e4 m/s: SplitEnergy keeps frame invariance
	const double mA = 1250.0, mB = 24500.0;
	const Vector vA (0.3, -2.1, 0.7), vB (2.5, -0.2, 0.3), vK (0.0, 0.0, 0.0);
	const Vector n1 = Vector (1.0, 0.05, 0.1).unit (), n2 = Vector (0.0, 1.0, 0.0); // B pushes A along n1 (from B to A), K below A
	auto run = [&] (const Vector &off, double &naive) {
		Vector a = vA + off, b = vB + off, k = vK + off;
		double dKE = 0.0, Wf = 0.0, Jsum = 0.0, Jv = 0.0;
		naive = 0.0;
		struct C { Vector *x, *y; double mx, my; Vector n; } cs[2] = { { &a, &k, mA, 0.0, n2 }, { &a, &b, mA, mB, n1 } };
		int fired = 0;
		for (const C &c : cs) {
			Vector vr = *c.x - *c.y;
			double vn = dotp (vr, c.n);
			if (vn >= 0.0) continue;
			double me = c.my > 0.0 ? c.mx * c.my / (c.mx + c.my) : c.mx;
			double e = 0.3 * std::min (1.0, std::pow (1.0 / -vn, 0.25));
			double Jn = -(1.0 + e) * vn * me, vpost = vn + Jn / me;
			Vector vt = vr - c.n * vn;
			double vtl = vt.length (), Jt = std::min (0.5 * Jn, vtl * me); // Coulomb disk, mu 0.5
			Vector tdir = vtl > 0.0 ? vt / vtl : Vector ();
			double vtpost = vtl - Jt / me;
			double ke0 = 0.5 * c.mx * c.x->length2 () + (c.my > 0.0 ? 0.5 * c.my * c.y->length2 () : 0.0);
			*c.x += (c.n * Jn - tdir * Jt) / c.mx;
			if (c.my > 0.0) *c.y -= (c.n * Jn - tdir * Jt) / c.my;
			double ke1 = 0.5 * c.mx * c.x->length2 () + (c.my > 0.0 ? 0.5 * c.my * c.y->length2 () : 0.0);
			naive += ke0 - ke1;
			double wn = -0.5 * Jn * (vn + vpost), wt = 0.5 * Jt * (vtl + vtpost);
			dKE += wn + wt, Wf += wt;
			Jsum += Jn, Jv += Jn * -vn;
			fired++;
		}
		REQUIRE(fired == 2);
		double E[2], ea[2];
		REQUIRE(DentMath::SplitEnergy (dKE, Wf, Jv / Jsum, true, DentMath::DefaultMaterial (-1), DentMath::DefaultMaterial (DENTB_BLOCK), E, ea));
		return std::vector<double> { dKE, Wf, E[0], E[1], ea[0], ea[1] };
	};
	double n0, n1v;
	std::vector<double> r0 = run (Vector (), n0);
	std::vector<double> r1 = run (Vector (2.0e4, 2.0e4, 1.5e4) * (3.0e4 / std::sqrt (2.0e4 * 2.0e4 * 2 + 1.5e4 * 1.5e4)), n1v);
	double worst = 0.0;
	for (size_t i = 0; i < r0.size (); i++) worst = std::max (worst, std::fabs (r1[i] - r0[i]) / std::fabs (r0[i]));
	NOTE ("U15 dKE %.6f J, E %.6f / %.6f J; offset 3e4 m/s: max rel diff %.2e; naive body-velocity KE change %.6g J vs %.6g J\n", r0[0], r0[2], r0[3], worst, n0, n1v);
	REQUIRE(r0[2] > 0.0);
	REQUIRE(worst < 1e-9);
	REQUIRE(std::fabs (n1v - n0) > 1e3 * std::fabs (n0)); // absolute velocities are not frame invariant with a kinematic partner
}

TEST_CASE("U16 coalescing", "[dent]")
{
	DentObject o = Floor3 ();
	const DentMaterial &al = DentMath::DefaultMaterial (-1);
	Vector c1 (2.1, 3.3, 0.0), n (0, 0, 1);
	auto view = [] (const DentObject &x) { DentViewData d; DentMath::MakeView (x, d); return d; };
	DentViewData v0 = view (o);
	DentInput in = { 5.0e4, &al, c1, n, 0.05, 2.0, 8.0, -1.0, false };
	DentRecord r1 {};
	r1.slot = 2, r1.key = DentMath::MeshKey ("floor3"), r1.ngrp = 3, r1.nvtx = (uint32_t)NVtx (o);
	REQUIRE(DentMath::Solve (in, v0.View (), r1.p) == DENT_OK);
	DentMath::Quantise (r1.p);
	std::vector<DentRecord> rec { r1 };
	DentMath::Apply (r1.p, o.rest, o.cur, nullptr, 0, nullptr);
	const double V1 = r1.p.h * DentMath::VolumeFactor (r1.p, v0.View ());
	// a second impact near the first: grows the first record's depth on its own kernel
	DentRecord r2 = r1;
	r2.p.c = c1 + Vector (0.3 * r1.p.R, 0.1, 0.0);
	REQUIRE(DentMath::FindCoalesce (rec, r2) == 0);
	const double V2 = 2.0e4 / al.sigma_c;
	DentViewData v1 = view (o);
	double dmax = DentMath::Dmax (al.t_cap, rec[0].p.T, rec[0].p.R, in.L);
	double dh = DentMath::CoalesceDepth (rec[0].p, V2, v1.View (), dmax);
	REQUIRE(dh > 0.0);
	rec[0].p.h += dh;
	DentMath::Quantise (rec[0].p);
	REQUIRE(rec.size () == 1);
	// live geometry is replayed from rest
	DentObject live = Floor3 ();
	for (const DentRecord &r : rec) DentMath::Apply (r.p, live.rest, live.cur, r.grp.data (), r.grp.size (), nullptr);
	DentViewData lv = view (Floor3 ());
	double vol = VolumeChange (lv.View (), rec[0].p);
	NOTE ("U16 h %.6f + %.6f, swept %.9f m^3, V1 + V2 %.9f m^3 (rel %.2e)\n", r1.p.h, dh, vol, V1 + V2, vol / (V1 + V2) - 1.0);
	REQUIRE(std::fabs (vol / (V1 + V2) - 1.0) < 1e-6);
	// save, load, replay: bitwise the live geometry
	DentVesselText t;
	t.rec = rec;
	std::vector<std::string> lines;
	DentMath::FormatVessel (t, "  ", lines);
	DentVesselParser vp;
	for (const std::string &l : lines) vp.Line (l.c_str ());
	DentVesselText u;
	vp.Finish (u);
	REQUIRE(u.rec.size () == 1);
	DentObject re = Floor3 ();
	for (const DentRecord &r : u.rec) DentMath::Apply (r.p, re.rest, re.cur, r.grp.data (), r.grp.size (), nullptr);
	REQUIRE(SameVtx (re.cur, live.cur));
	REQUIRE(SameRec (u.rec[0], rec[0]));
	REQUIRE(DentMath::FindCoalesce (u.rec, r2) == DentMath::FindCoalesce (rec, r2)); // a later hit coalesces the same way after a reload
	// no coalescing: too far, too steep, other target or partition; the nearest of two candidates wins
	DentRecord x = r2;
	x.p.c = c1 + Vector (0.51 * r1.p.R, 0.0, 0.0);
	REQUIRE(DentMath::FindCoalesce (rec, x) == -1);
	x = r2, x.p.n = Vector (std::sin (31.0 * Pi / 180.0), 0.0, std::cos (31.0 * Pi / 180.0));
	REQUIRE(DentMath::FindCoalesce (rec, x) == -1);
	x = r2, x.p.n = Vector (std::sin (29.0 * Pi / 180.0), 0.0, std::cos (29.0 * Pi / 180.0));
	REQUIRE(DentMath::FindCoalesce (rec, x) == 0);
	x = r2, x.slot = 1;
	REQUIRE(DentMath::FindCoalesce (rec, x) == -1);
	x = r2, x.key ^= 1;
	REQUIRE(DentMath::FindCoalesce (rec, x) == -1);
	x = r2, x.grp = { 0, 1 };
	REQUIRE(DentMath::FindCoalesce (rec, x) == -1);
	std::vector<DentRecord> two { rec[0], rec[0] };
	two[1].p.c = r2.p.c + Vector (0.01, 0.0, 0.0);
	REQUIRE(DentMath::FindCoalesce (two, r2) == 1);
}

// code review C-A-c regressions: CR<n> = finding n

TEST_CASE("CR1 refinement keeps hard edges and UV seams inside a group", "[dent]")
{
	// one group: floor quad (normal +z, u 0..1), wall quad (normal -y, u 5..6), own vertices at y = 0
	DentObject o;
	std::vector<DentVtx> v = {
		{ 0, 0, 0, 0, 0, 1, 0, 0 }, { 4, 0, 0, 0, 0, 1, 1, 0 }, { 4, 4, 0, 0, 0, 1, 1, 1 }, { 0, 4, 0, 0, 0, 1, 0, 1 },
		{ 0, 0, 0, 0, -1, 0, 5, 0 }, { 4, 0, 0, 0, -1, 0, 6, 0 }, { 4, 0, 4, 0, -1, 0, 6, 1 }, { 0, 0, 4, 0, -1, 0, 5, 1 } };
	o.rest.push_back (v), o.cur.push_back (v);
	o.idx.push_back ({ 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7 });
	o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
	DentObject o0 = o;
	int a = DentMath::Refine (o, Vector (2, 0, 0), 4.0, nullptr, 0, nullptr);
	size_t wallBad = 0, corners = 0;
	for (size_t j = 0; j < o.idx[0].size (); j += 3) {
		bool wall = false;
		for (int k = 0; k < 3; k++) if (o.rest[0][o.idx[0][j+k]].z > 0.5f) wall = true;
		if (!wall) continue;
		for (int k = 0; k < 3; k++, corners++) {
			const DentVtx &w = o.rest[0][o.idx[0][j+k]];
			if (w.ny != -1.0f || w.tu < 5.0f) wallBad++;
		}
	}
	size_t bad = AttrMismatch (o0, o);
	NOTE ("CR1 hard edge in one group: +%d triangles, wall corners %zu with floor attributes %zu, triangles off their face %zu\n", a, corners, wallBad, bad);
	REQUIRE(a > 0);
	REQUIRE(wallBad == 0);
	REQUIRE(bad == 0);
	REQUIRE(TJunctions (o) == 0);
	REQUIRE(WeldGap (o) == 0.0);
}

TEST_CASE("CR1 VAB facade dent: refined attributes stay on their own faces", "[dent]")
{
	DentObject o;
	if (!LoadMsh ("Meshes/Vab.msh", o)) SKIP ("Meshes/Vab.msh not found");
	REQUIRE(o.rest.size () == 142);
	// the first facade triangle (horizontal normal) whose longest edge is 70-80 m (D4 4.9: 77 m)
	size_t bg = 0, bj = 0;
	double best = 0.0;
	Vector lo (1e9, 1e9, 1e9), hi (-1e9, -1e9, -1e9);
	for (size_t g = 0; g < o.rest.size (); g++) {
		for (const DentVtx &x : o.rest[g])
			lo = Vector (std::min (lo.x, (double)x.x), std::min (lo.y, (double)x.y), std::min (lo.z, (double)x.z)),
			hi = Vector (std::max (hi.x, (double)x.x), std::max (hi.y, (double)x.y), std::max (hi.z, (double)x.z));
		for (size_t j = 0; j + 2 < o.idx[g].size () && best == 0.0; j += 3) {
			double l = 0.0;
			for (int k = 0; k < 3; k++) l = std::max (l, P (o.rest[g][o.idx[g][j+k]]).dist (P (o.rest[g][o.idx[g][j+(k+1)%3]])));
			if (l > 70.0 && l < 80.0 && std::fabs (o.rest[g][o.idx[g][j]].ny) < 0.1f) best = l, bg = g, bj = j;
		}
	}
	const DentVtx &a = o.rest[bg][o.idx[bg][bj]], &b = o.rest[bg][o.idx[bg][bj+1]], &c = o.rest[bg][o.idx[bg][bj+2]];
	Vector cen = (P (a) + P (b) + P (c)) / 3.0, n = N (a).unit ();
	const double rb = 0.5 * (hi - lo).length ();
	DentObject o0 = o;
	std::vector<DentParams> rec;
	BDent d = BuildingDent (o, rec, cen, n, 2.94e5, DentMath::DefaultMaterial (DENTB_MESH), 0.3, 0.5 * rb, rb);
	size_t bad = AttrMismatch (o0, o);
	NOTE ("CR1 VAB: longest edge %.1f m (group %zu), R %.3f m, +%d triangles, h %.4f m, triangles off their face %zu\n", best, bg, d.p.R, d.added, d.p.h, bad);
	REQUIRE(best > 70.0);
	REQUIRE(d.rc == DENT_OK);
	REQUIRE(d.added > 0);
	REQUIRE(bad == 0);
	REQUIRE(MatchesReplay (o, rec));
}

TEST_CASE("CR2 live and replayed c, R refine alike (Refine quantises)", "[dent]")
{
	// lone triangle whose bounding sphere just touches the ball: R and its record straddle threshold
	DentObject o;
	std::vector<DentVtx> v = { { 10, 0, 0, 0, 0, 1, 0, 0 }, { 14, 0, 0, 0, 0, 1, 1, 0 }, { 12, 3, 0, 0, 0, 1, 0, 1 } };
	o.rest.push_back (v), o.cur.push_back (v), o.idx.push_back ({ 0, 1, 2 });
	o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
	const Vector p0 (10, 0, 0), p1 (14, 0, 0), p2 (12, 3, 0), cen = (p0 + p1 + p2) / 3.0;
	const double rad = std::max (std::max ((cen - p0).length (), (cen - p1).length ()), (cen - p2).length ());
	const Vector cl (0.1234567891234, 0.000123456789123, 0.0); // live centre, more digits than a record keeps
	DentParams q0 = Params (cl, Vector (0, 0, 1), 1.0, 0.0, 0.0);
	DentMath::Quantise (q0);
	const double dist = (cen - q0.c).length ();
	const double need = dist - rad;
	int border = 0;
	for (int k = -20000; k < 20000 && border < 8; k++) {
		DentParams q = q0;
		q.R = need * (1.0 + k * 1e-13);
		const double R = q.R;
		DentMath::Quantise (q);
		if ((dist <= R + rad) == (dist <= q.R + rad)) continue;
		border++;
		DentObject live = o, live2 = o, rep = o;
		int a1 = DentMath::Refine (live, q0.c, R, nullptr, 0, nullptr);
		int a2 = DentMath::Refine (live2, cl, R, nullptr, 0, nullptr);
		int a3 = DentMath::Refine (rep, q.c, q.R, nullptr, 0, nullptr);
		REQUIRE(a1 == a3);
		REQUIRE(a2 == a3);
		REQUIRE(SameObj (live, rep));
		REQUIRE(SameObj (live2, rep));
	}
	NOTE ("CR2 borderline radii checked: %d\n", border);
	REQUIRE(border > 0);
}

TEST_CASE("CR2 building dents replay from saved ODENT lines bitwise", "[dent]")
{
	const DentMaterial &rc = *DentMath::FindMaterial ("building_rc");
	const double rb = 0.5 * std::sqrt (40.0 * 40.0 + 15.0 * 15.0 + 30.0 * 30.0);
	DentObject b0 = Brighton (), live = b0;
	const DentVtx *v0 = b0.rest[0].data ();
	Vector n = N (v0[0]).unit ();
	Vector c = (P (v0[0]) + P (v0[1]) + P (v0[2]) + P (v0[3])) / 4.0;
	c.y = 4.0;
	Vector c2 = P (v0[2]) + (P (v0[3]) - P (v0[2])) * (0.3 / 40.0);
	c2.y = 14.5;
	std::vector<DentParams> rec;
	REQUIRE(BuildingDent (live, rec, c, n, 2.94e5, rc, 0.3, 0.5 * rb, rb).rc == DENT_OK);
	REQUIRE(BuildingDent (live, rec, c + Vector (0.0, -0.5, 0.0), n, 8.0e3, rc, 0.1, 0.5 * rb, rb).rc == DENT_OK);
	REQUIRE(BuildingDent (live, rec, c2, n, 2.94e5, rc, 0.3, 0.5 * rb, rb).rc == DENT_OK);
	std::vector<DentBaseText> bt (1);
	bt[0].planet = "Moon", bt[0].name = "Brighton Beach";
	bt[0].obj.push_back ({ 1, "BLOCK", -60.62, -35.0, 1.0e5, 0 });
	for (const DentParams &p : rec) {
		DentRecord r {};
		r.slot = 1, r.p = p;
		bt[0].rec.push_back (r);
	}
	std::vector<std::string> lines;
	DentMath::FormatBases (bt, lines);
	int sk = -1;
	std::vector<DentBaseText> back = ParseBases (lines, &sk);
	REQUIRE((sk == 0 && back.size () == 1 && back[0].rec.size () == rec.size ()));
	DentObject re = Replay (b0, back[0].rec);
	NOTE ("CR2 Brighton: %zu records, %zu triangles live, replay equal %d\n", rec.size (), NTri (live), (int)SameObj (re, live));
	REQUIRE(SameObj (re, live));
}

TEST_CASE("CR2 a refinement cap hit is saved with the record (NOREFINE) and replayed as such", "[dent]")
{
	DentObject g0;
	AddGrid (g0, Vector (-4, -4, 0), Vector (1, 0, 0), Vector (0, 1, 0), 8, 8, Vector (0, 0, 1));
	g0.nweld = DentMath::WeldMap (g0.rest, DENT_WELD, g0.weld);
	const DentMaterial &rc = *DentMath::FindMaterial ("building_rc");
	const Vector c (0.3, 0.2, 0.0), n (0, 0, 1);
	const double E = 2.0e4, Rmax = 5.0, L = 10.0;
	// live (4.9 item 5): merged group full, so no refinement; floor fits R_max: coarse dent with floor
	DentObject live = g0;
	const uint32_t full[1] = { 0 };
	REQUIRE(DentMath::Refine (live, c, DentMath::Radius (E, rc.sigma_c, 0.1, Rmax), nullptr, 0, full) == -1);
	DentViewData v;
	DentMath::MakeView (live, v);
	const double fl = DentMath::LowPolyFloor (v.View (), c);
	REQUIRE(fl <= Rmax);
	DentInput in = { E, &rc, c, n, 0.1, Rmax, L, -1.0, true };
	DentRecord r {};
	r.slot = 0;
	REQUIRE(DentMath::Solve (in, v.View (), r.p) == DENT_OK);
	DentMath::Quantise (r.p);
	REQUIRE(r.p.R == Q9R (fl));
	r.flags = DENTR_NOREFINE;
	DentMath::Apply (r.p, live.rest, live.cur, nullptr, 0, nullptr);
	// save and load: the flag is the last ODENT field
	std::vector<DentBaseText> bt (1);
	bt[0].planet = "Moon", bt[0].name = "Grid";
	bt[0].rec.push_back (r);
	std::vector<std::string> lines;
	DentMath::FormatBases (bt, lines);
	REQUIRE(lines[2].compare (lines[2].size () - 2, 2, " 1") == 0);
	int sk = -1;
	std::vector<DentBaseText> back = ParseBases (lines, &sk);
	REQUIRE((sk == 0 && back[0].rec.size () == 1 && SameRec (back[0].rec[0], r)));
	REQUIRE(SameObj (Replay (g0, back[0].rec), live));
	// replay without the flag refines with the floor radius: different geometry
	std::vector<DentRecord> noflag = back[0].rec;
	noflag[0].flags = 0;
	REQUIRE_FALSE(SameObj (Replay (g0, noflag), live));
	// lines without the field read as before (flags 0); a bad field skips the line
	std::string old = lines[2].substr (0, lines[2].size () - 2);
	back = ParseBases ({ "BEGIN_XDMG_BASES", "BASE Moon:Grid", old, lines[2] + "x", "END_BASE", "END_XDMG_BASES" }, &sk);
	REQUIRE((sk == 1 && back[0].rec.size () == 1 && back[0].rec[0].flags == 0));
	// the recorder event carries it too
	int pa, pb, po;
	uint32_t f = 0;
	DentParams q {};
	std::string ev = DentMath::FormatBaseDentEvent (2, 3, 4, r.p, r.flags);
	REQUIRE(DentMath::ParseBaseDentEvent (ev.c_str (), pa, pb, po, q, f));
	REQUIRE((pa == 2 && pb == 3 && po == 4 && SameP (q, r.p) && f == DENTR_NOREFINE));
	REQUIRE(DentMath::ParseBaseDentEvent (DentMath::FormatBaseDentEvent (2, 3, 4, r.p).c_str (), pa, pb, po, q, f));
	REQUIRE(f == 0);
}

TEST_CASE("CR3 numbers beyond the record limits never reach geometry", "[dent]")
{
	int sk = -1;
	DentVesselText v = ParseVessel ({ "XDMG 1 0 0", "XDMGM 0 0 00000001 1 4",
		"XDMGD 0 0 0 0 0 0 1 1e300 1e300 0 *", "XDMGD 0 0 0 0 0 0 1 1e20 1e20 0 *", "XDMGD 0 2e7 0 0 0 0 1 1 0.1 0 *",
		"XDMGD 0 0 0 0 0 0 1 1 0.1 2e6 *", "XDMGD 0 0 0 0 0 0 1 1 1001 0 *", "XDMGD 0 0 0 0 0 0 1 10001 0.1 0 *",
		"XDMGD 0 -1e7 1e7 0 0 0 1 1e4 1e3 1e6 *" }, &sk);
	REQUIRE(sk == 6);
	REQUIRE(v.rec.size () == 1); // the line at the limits
	v = ParseVessel ({ "XDMG 1 1e31 0" }, &sk);
	REQUIRE((sk == 1 && v.eabs == 0.0));
	std::vector<DentBaseText> b = ParseBases ({ "BEGIN_XDMG_BASES", "BASE Moon:X", "ODENT 0 0 0 0 0 1 0 1e30 1e30 0", "OBJ 1 BLOCK 1e8 0 0 0",
		"OBJ 2 BLOCK 0 0 1e31 0", "OBJ 3 BLOCK 0 0 5 0", "END_BASE", "END_XDMG_BASES" }, &sk);
	REQUIRE((sk == 3 && b[0].rec.empty () && b[0].obj.size () == 1));
	DentRecord e {};
	REQUIRE_FALSE(DentMath::ParseDentEvent ("0 0 0 0 0 0 1 1e300 1 0 *", e));
	int pa, pb, po;
	DentParams q {};
	REQUIRE_FALSE(DentMath::ParseBaseDentEvent ("1:2:3 0 0 0 0 1 0 1 1e300 1", pa, pb, po, q));
	REQUIRE_FALSE(DentMath::ParseBaseDentEvent ("-1:2:3 0 0 0 0 1 0 1 0.1 1", pa, pb, po, q));
	double ea;
	uint32_t fl;
	REQUIRE_FALSE(DentMath::ParseStateEvent ("1e31 0", ea, fl));
	// a field beyond float range is not stored (never inf in the geometry)
	DentObject o;
	AddGrid (o, Vector (-1, -1, 0), Vector (1, 0, 0), Vector (0, 1, 0), 2, 2, Vector (0, 0, 1));
	DentMath::Apply (Params (Vector (0, 0, 0), Vector (0, 0, 1), 1e300, 1e300, 0.0), o.rest, o.cur, nullptr, 0, nullptr);
	for (const DentVtx &x : o.cur[0]) REQUIRE((std::isfinite (x.x) && std::isfinite (x.y) && std::isfinite (x.z)));
	// Solve gives no record beyond the limits (vessel floor 1e5 m away); coalescing keeps h within them
	DentViewData d;
	d.rest = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 } };
	d.cur = d.rest;
	d.tri = { 0, 1, 2, 0, 2, 3 };
	DentParams p {};
	DentInput in = { 1.0e3, &DentMath::DefaultMaterial (-1), Vector (1e5, 0, 0), Vector (0, 0, 1), 0.01, 1.0, 10.0, -1.0, true };
	REQUIRE(DentMath::Solve (in, d.View (), p) == DENT_NOSURFACE);
	DentParams old = Params (Vector (0, 0, 0), Vector (0, 0, 1), 2.0, DENT_LIM_H - 1e-3, 0.0);
	double dh = DentMath::CoalesceDepth (old, 1e9, d.View (), 1e9);
	REQUIRE((dh > 0.0 && old.h + dh <= DENT_LIM_H + 1e-9));
	REQUIRE(DentMath::SlabT (2.0 * DENT_LIM_T, 1.0) == 0.0);
}

TEST_CASE("CR5 split group lists carry a continuation marker; equal records stay apart", "[dent]")
{
	// two partition records with the same numbers (a door at identity pose) and disjoint groups
	DentRecord a {};
	a.slot = 0, a.key = 7, a.ngrp = 30, a.nvtx = 900;
	a.p = Params (Vector (1, 2, 3), Vector (0, 1, 0), 1.2, 0.1, 0.0);
	a.grp = { 1, 2 };
	DentRecord b = a;
	b.grp = { 5 };
	DentVesselText v;
	v.rec = { a, b }, v.eabs = 100;
	int sk = -1;
	DentVesselText w = ParseVessel (Format (v), &sk);
	REQUIRE((sk == 0 && w.rec.size () == 2 && SameRec (w.rec[0], a) && SameRec (w.rec[1], b)));
	DentRecord hit = b;
	hit.p.c = Vector (1.1, 2, 3);
	REQUIRE(DentMath::FindCoalesce (v.rec, hit) == 1);
	REQUIRE(DentMath::FindCoalesce (w.rec, hit) == 1);
	// long lists: all lines but a record's last end in ','; two equal long records come back as two
	DentRecord l = a;
	l.grp.clear ();
	for (uint16_t g = 0; g < 120; g++) l.grp.push_back ((uint16_t)(60000 + g));
	v.rec = { l, l, b };
	std::vector<std::string> lines = Format (v);
	size_t marked = 0;
	for (const std::string &s : lines) {
		REQUIRE(s.size () <= 200);
		if (s.compare (0, 7, "  XDMGD") == 0 && s.back () == ',') marked++;
	}
	w = ParseVessel (lines, &sk);
	REQUIRE((sk == 0 && w.rec.size () == 3 && SameRec (w.rec[0], l) && SameRec (w.rec[1], l) && SameRec (w.rec[2], b)));
	REQUIRE(marked >= 4);
	// a dangling marker at the block end, or a next line not continuing it: that record is skipped
	w = ParseVessel ({ "XDMG 1 0 0", "XDMGM 0 0 00000007 30 900", "XDMGD 0 1 2 3 0 1 0 1.2 0.1 0 1,2," }, &sk);
	REQUIRE((w.rec.empty () && sk == 1));
	w = ParseVessel ({ "XDMG 1 0 0", "XDMGM 0 0 00000007 30 900", "XDMGD 0 1 2 3 0 1 0 1.2 0.1 0 1,2,", "XDMGD 0 1 2 3 0 1 0 1.3 0.1 0 5" }, &sk);
	REQUIRE((w.rec.size () == 1 && w.rec[0].grp == std::vector<uint16_t> ({ 5 }) && sk == 1));
	w = ParseVessel ({ "XDMG 1 0 0", "XDMGD 0 1 2 3 0 1 0 1.2 0.1 0 1,2,", "XDMGM 0 0 00000007 30 900", "XDMGD 0 1 2 3 0 1 0 1.2 0.1 0 5" }, &sk);
	REQUIRE((w.rec.size () == 1 && sk == 1));
	// a repeated index at a split point stays one record, so it is applied once as live
	w = ParseVessel ({ "XDMG 1 0 0", "XDMGM 0 0 00000007 30 900", "XDMGD 0 1 2 3 0 1 0 1.2 0.1 0 1,2,", "XDMGD 0 1 2 3 0 1 0 1.2 0.1 0 2,5" }, &sk);
	REQUIRE((sk == 0 && w.rec.size () == 1 && w.rec[0].grp == std::vector<uint16_t> ({ 1, 2, 2, 5 })));
	// recorder payloads carry the same marker; playback joins them
	std::vector<std::string> pl;
	DentMath::FormatDentEvent (l, pl);
	REQUIRE(pl.size () > 1);
	std::vector<uint16_t> joined;
	for (size_t i = 0; i < pl.size (); i++) {
		DentRecord e {};
		bool more = false;
		REQUIRE(pl[i].size () <= 180);
		REQUIRE(DentMath::ParseDentEvent (pl[i].c_str (), e, more));
		REQUIRE(more == (i + 1 < pl.size ()));
		REQUIRE(SameP (e.p, l.p));
		joined.insert (joined.end (), e.grp.begin (), e.grp.end ());
	}
	REQUIRE(joined == l.grp);
	DentRecord e {};
	bool more = true;
	REQUIRE((DentMath::ParseDentEvent ("0 1 2 3 0 1 0 1.2 0.1 0 1,2", e, more) && !more));
	REQUIRE_FALSE(DentMath::ParseDentEvent ("0 1 2 3 0 1 0 1.2 0.1 0 *,", e, more));
	REQUIRE_FALSE(DentMath::ParseDentEvent ("0 1 2 3 0 1 0 1.2 0.1 0 ,", e, more));
}

TEST_CASE("CR6 groups sharing one merged group share one vertex budget", "[dent]")
{
	DentObject g;
	for (int s = 0; s < 2; s++) AddGrid (g, Vector (s * 4 - 4, -4, 0), Vector (2, 0, 0), Vector (0, 2, 0), 2, 4, Vector (0, 0, 1));
	g.nweld = DentMath::WeldMap (g.rest, DENT_WELD, g.weld);
	const Vector c (0.1, 0.05, 0.0);
	const uint32_t shared[1] = { 50 }, both[2] = { 0, 0 }, apart[2] = { 50, 50 };
	bool over = false;
	int ok = 0, capped = 0;
	for (double R : { 2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 16.0 }) {
		DentObject sh = g, pg = g;
		int a = DentMath::Refine (sh, c, R, nullptr, 0, shared, both, nullptr);
		int b = DentMath::Refine (pg, c, R, nullptr, 0, apart);
		NOTE ("CR6 R %.0f: shared room 50: %+d (vertices +%zu); per-group rooms 50 + 50: %+d (vertices +%zu)\n", R, a, NVtx (sh) - NVtx (g), b, NVtx (pg) - NVtx (g));
		if (a < 0) {
			capped++;
			REQUIRE(SameObj (sh, g));
		} else {
			ok += a > 0;
			REQUIRE(NVtx (sh) - NVtx (g) <= 50);
		}
		if (b > 0 && NVtx (pg) - NVtx (g) > 50) over = true;
	}
	REQUIRE(over);
	REQUIRE(ok > 0);
	REQUIRE(capped > 0);
	// enough room: the same result as without a budget; slot ids index vtxRoom as given
	const uint32_t big[8] = { 0, 0, 0, 0, 0, 0, 0, 100000 }, seven[2] = { 7, 7 };
	DentObject x = g, y = g;
	REQUIRE(DentMath::Refine (x, c, 10.0, nullptr, 0, big, seven, nullptr) > 0);
	REQUIRE(DentMath::Refine (y, c, 10.0, nullptr, 0, nullptr) > 0);
	REQUIRE(SameObj (x, y));
}

TEST_CASE("CR7 depth cap holds when no vertex is above q 0.05 (U5 row)", "[dent]")
{
	// a large hit face with every vertex outside the ball, and a small front-facing detail at the rim
	DentViewData d;
	d.rest = { { -50, -50, 0 }, { 50, -50, 0 }, { 0, 60, 0 }, { 0.93, 0, 0.0 }, { 0.98, 0.01, 0.0 }, { 0.98, -0.01, 0.0 } };
	d.cur = d.rest;
	d.tri = { 0, 1, 2, 3, 5, 4 };
	const DentMaterial m = Mat (0.4e6, 3.0);
	DentInput in = { 2.94e5, &m, Vector (0, 0, 0), Vector (0, 0, 1), 1.0, 1.0, 50.0, -1.0, false };
	DentParams p {};
	int rc = DentMath::Solve (in, d.View (), p);
	double qmax = 0.0, dmax = DentMath::Dmax (m.t_cap, p.T, p.R, in.L);
	for (const Vector &x : d.rest) qmax = std::max (qmax, DentMath::Weight (p, x));
	NOTE ("CR7 result %d, R %.3f, h %.6g, max q %.4f, deepest %.6g m (D_max %.3g)\n", rc, p.R, p.h, qmax, p.h * qmax, dmax);
	REQUIRE((qmax > 0.0 && qmax <= DENT_CAP_Q));
	REQUIRE(p.h * qmax <= dmax + 1e-12);
}

TEST_CASE("CR8 coincident faces on the weld grid count once in S", "[dent]")
{
	DentViewData d;
	d.rest = { { -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 }, { -1, -1, 2e-6 }, { 1, -1, 2e-6 }, { 1, 1, 2e-6 }, { -1, 1, 2e-6 } };
	d.cur = d.rest;
	d.tri = { 0, 1, 2, 0, 2, 3 };
	DentParams p = Params (Vector (0.1, 0.2, 0.0), Vector (0, 0, 1), 1.5, 0.0, 0.0);
	double s1 = DentMath::VolumeFactor (p, d.View ());
	d.tri.insert (d.tri.end (), { 4, 5, 6, 4, 6, 7 }); // a decal 2 micrometres above
	double s2 = DentMath::VolumeFactor (p, d.View ());
	d.tri.resize (6);
	d.tri.insert (d.tri.end (), { 4, 6, 5, 4, 7, 6 }); // its back side
	double s3 = DentMath::VolumeFactor (p, d.View ());
	NOTE ("CR8 S single %.12f, with an offset duplicate %.12f, with an offset back side %.12f\n", s1, s2, s3);
	REQUIRE(std::fabs (s2 / s1 - 1.0) < 1e-9);
	REQUIRE(Bits (s3, s1));
}

TEST_CASE("CR9 tabs in kept unknown-version lines survive", "[dent]")
{
	int sk = -1;
	DentVesselText v = ParseVessel ({ "XDMG\t2\t99 0", "XDMGD\t0 future\tfields" }, &sk);
	REQUIRE(v.verbatim == std::vector<std::string> ({ "XDMG\t2\t99 0", "XDMGD\t0 future\tfields" }));
	std::vector<std::string> l = Format (v);
	REQUIRE(l == std::vector<std::string> ({ "  XDMG\t2\t99 0", "  XDMGD\t0 future\tfields" }));
	DentVesselText w = ParseVessel (l, &sk);
	REQUIRE((sk == 0 && w.verbatim == v.verbatim));
	// mesh names keep tabs; other control characters never reach a line
	DentVesselText m;
	DentRecord r {};
	r.p = Params (Vector (0, 0, 0), Vector (0, 0, 1), 1.0, 0.1, 0.0);
	m.rec.push_back (r);
	m.slotName = { std::string ("a\tb\x01" "c") };
	l = Format (m);
	REQUIRE(l[1] == "  XDMGM 0 0 00000000 0 0 a\tb_c");
	REQUIRE(ParseVessel (l).slotName[0] == "a\tb_c");
}

TEST_CASE("CR10 BASEH keeps planet names with spaces", "[dent]")
{
	std::vector<DentBaseText> b (1);
	b[0].planet = "Planet X", b[0].name = std::string (250, 'n');
	DentRecord r {};
	r.p = Params (Vector (1, 2, 3), Vector (0, 1, 0), 1.0, 0.1, 0.0);
	b[0].rec.push_back (r);
	std::vector<std::string> l;
	DentMath::FormatBases (b, l);
	const uint32_t h = DentMath::Fnv1a (b[0].name.data (), b[0].name.size ());
	char hs[16];
	std::snprintf (hs, sizeof (hs), "%08x", h);
	REQUIRE(l[1] == std::string ("BASEH ") + hs + " Planet X");
	int sk = -1;
	std::vector<DentBaseText> o = ParseBases (l, &sk);
	REQUIRE((sk == 0 && o.size () == 1 && o[0].planet == "Planet X" && o[0].nameHash == h && o[0].name.empty () && o[0].rec.size () == 1));
	o = ParseBases ({ "BEGIN_XDMG_BASES", "BASEH 1234abcd", "BASEH zz Moon", "END_XDMG_BASES" }, &sk);
	REQUIRE((sk == 2 && o.empty ()));
}

TEST_CASE("CR11 one section per version: unknown versions kept, version 1 parsed", "[dent]")
{
	int sk = -1;
	// XDMG -1 is a version, not "none yet"
	DentVesselText v = ParseVessel ({ "XDMG -1 5 0", "XDMGM 0 0 00000001 1 4", "XDMGD 0 0 0 0 0 0 1 1 0.1 0 *" }, &sk);
	REQUIRE((v.rec.empty () && v.verbatim.size () == 3 && sk == 0));
	// kept future lines next to new version-1 damage: both survive a save and load
	DentVesselText m;
	m.verbatim = { "XDMG 2 99 0", "XDMGM 0 0 00000001 1 4 new" };
	DentRecord r {};
	r.slot = 0, r.key = 5, r.ngrp = 1, r.nvtx = 4;
	r.p = Params (Vector (1, 2, 3), Vector (0, 1, 0), 1.0, 0.1, 0.0);
	m.rec.push_back (r), m.eabs = 10, m.flags = 1;
	DentVesselText w = ParseVessel (Format (m), &sk);
	REQUIRE((sk == 0 && w.verbatim == m.verbatim && w.rec.size () == 1 && SameRec (w.rec[0], r) && w.eabs == 10.0 && w.flags == 1));
	// XDMG 1 before XDMG 2: the version-2 lines stay verbatim; a reload of the reload is the same
	const std::vector<std::string> mixed = { "XDMG 1 3 0", "XDMGM 0 0 00000001 1 4", "XDMGD 0 0 0 0 0 0 1 1 0.1 0 *", "XDMG 2 7 0", "XDMGM 0 9 00000002 2 8", "XDMGD 0 v2 data" };
	w = ParseVessel (mixed, &sk);
	REQUIRE((sk == 0 && w.rec.size () == 1 && w.rec[0].key == 1 && w.eabs == 3.0));
	REQUIRE(w.verbatim == std::vector<std::string> ({ "XDMG 2 7 0", "XDMGM 0 9 00000002 2 8", "XDMGD 0 v2 data" }));
	DentVesselText w2 = ParseVessel (Format (w), &sk);
	REQUIRE((sk == 0 && w2.verbatim == w.verbatim && w2.rec.size () == 1 && SameRec (w2.rec[0], w.rec[0]) && w2.eabs == w.eabs));
	// an unknown section with an XDMG line over 200 chars is dropped whole, never left headerless
	w = ParseVessel ({ "XDMGD 0 a", "XDMG 3 " + std::string (250, '1'), "XDMGD 0 b", "XDMG 1 1 0" }, &sk);
	REQUIRE((w.verbatim.empty () && sk == 3 && w.eabs == 1.0));
	// a second XDMG 1 line is skipped
	w = ParseVessel ({ "XDMG 1 1 0", "XDMG 1 2 0" }, &sk);
	REQUIRE((sk == 1 && w.eabs == 1.0));
}

TEST_CASE("CR12 refinement reports split ids; normals per dent equal normals once after replay", "[dent]")
{
	DentObject g0;
	AddGrid (g0, Vector (-4, -4, 0), Vector (1, 0, 0), Vector (0, 1, 0), 8, 8, Vector (0, 0, 1));
	for (DentVtx &v : g0.rest[0]) v.z = 0.3f * v.x * v.y / 16.0f; // curved: face-normal sums of a vertex are not parallel
	g0.cur = g0.rest;
	g0.nweld = DentMath::WeldMap (g0.rest, DENT_WELD, g0.weld);
	{
		DentObject a = g0;
		std::vector<Vector> s0, s1;
		DentMath::FaceNormalSums (a.rest, a.idx, a.weld, a.nweld, s0);
		std::vector<uint8_t> split;
		REQUIRE(DentMath::Refine (a, Vector (1, 1, 0), 3.0, nullptr, 0, nullptr, nullptr, &split) > 0);
		REQUIRE(split.size () == a.nweld);
		DentMath::FaceNormalSums (a.rest, a.idx, a.weld, a.nweld, s1);
		size_t changed = 0;
		for (uint32_t w = 0; w < g0.nweld; w++)
			if (!(Bits (s0[w].x, s1[w].x) && Bits (s0[w].y, s1[w].y) && Bits (s0[w].z, s1[w].z))) {
				changed++;
				REQUIRE(split[w]);
			}
		for (uint32_t w = g0.nweld; w < a.nweld; w++) REQUIRE(split[w]);
		NOTE ("CR12 one refinement: %zu of %u old ids changed their rest face-normal sum, all reported\n", changed, g0.nweld);
		REQUIRE(changed > 0);
	}
	std::vector<DentParams> rec = { Params (Vector (-1.5, -1.5, 0.042), Vector (0, 0, 1), 1.5, 0.1, 0.0), Params (Vector (0.1, 0.1, 0.0), Vector (0, 0, 1), 0.3, 0.03, 0.0) };
	for (DentParams &p : rec) DentMath::Quantise (p);
	auto mark = [] (const DentObject &o, const std::vector<std::vector<uint8_t>> &dirty, std::vector<uint8_t> &t) {
		t.resize (o.nweld, 0);
		for (size_t g = 0; g < dirty.size (); g++)
			for (size_t i = 0; i < dirty[g].size (); i++) if (dirty[g][i]) t[o.weld[g][i]] = 1;
	};
	// live: per dent refine, rest sums, apply, normals over the moved (and split) ids
	auto live = [&] (bool withSplit) {
		DentObject o = g0;
		std::vector<DentParams> prior;
		for (const DentParams &p : rec) {
			std::vector<uint8_t> split, t;
			DentMath::Refine (o, p.c, p.R, prior.data (), prior.size (), nullptr, nullptr, &split);
			std::vector<Vector> rs;
			DentMath::FaceNormalSums (o.rest, o.idx, o.weld, o.nweld, rs);
			std::vector<std::vector<uint8_t>> dirty;
			DentMath::Apply (p, o.rest, o.cur, nullptr, 0, &dirty);
			if (withSplit) t = split;
			mark (o, dirty, t);
			DentMath::Normals (o.rest, rs, o.idx, o.weld, o.nweld, t, o.cur);
			prior.push_back (p);
		}
		return o;
	};
	// replay: every record, then one normals pass over all moved and split ids
	DentObject re = g0;
	std::vector<DentParams> prior;
	std::vector<uint8_t> all;
	std::vector<std::vector<uint8_t>> dirty;
	for (const DentParams &p : rec) {
		std::vector<uint8_t> split;
		DentMath::Refine (re, p.c, p.R, prior.data (), prior.size (), nullptr, nullptr, &split);
		all.resize (re.nweld, 0);
		for (size_t w = 0; w < split.size (); w++) if (split[w]) all[w] = 1;
		DentMath::Apply (p, re.rest, re.cur, nullptr, 0, &dirty);
		prior.push_back (p);
	}
	mark (re, dirty, all);
	std::vector<Vector> rs;
	DentMath::FaceNormalSums (re.rest, re.idx, re.weld, re.nweld, rs);
	DentMath::Normals (re.rest, rs, re.idx, re.weld, re.nweld, all, re.cur);
	DentObject a = live (true), b = live (false);
	NOTE ("CR12 normals: live with split ids equal to replay %d, without %d\n", (int)SameObj (a, re), (int)SameObj (b, re));
	REQUIRE(SameObj (a, re));
	REQUIRE_FALSE(SameObj (b, re));
}

TEST_CASE("CR13 distances use DentMath's own products (no contraction from Vecmat.cpp)", "[dent]")
{
	// dx*dx + dy*dy + dz*dz rounded term by term; volatile keeps this test's own build from fusing it
	auto ref2 = [] (const Vector &a, const Vector &b) {
		volatile double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
		volatile double x2 = dx * dx, y2 = dy * dy, z2 = dz * dz;
		volatile double s = x2 + y2;
		s = s + z2;
		return (double)s;
	};
	Rng r { 1313 };
	int fusedDiffers = 0;
	for (int it = 0; it < 400; it++) {
		DentViewData d;
		for (int k = 0; k < 4; k++) d.rest.push_back (Vector (r.U (-3, 3), r.U (-3, 3), r.U (-3, 3)));
		d.cur = d.rest;
		d.tri = { 0, 1, 2, 0, 2, 3 };
		Vector c (r.U (-1, 1), r.U (-1, 1), r.U (-1, 1));
		double far = 0.0;
		for (const Vector &p : d.rest) {
			far = std::max (far, std::sqrt (ref2 (p, c)));
			double dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
			if (std::fma (dz, dz, std::fma (dy, dy, dx * dx)) != ref2 (p, c)) fusedDiffers++;
		}
		REQUIRE(DentMath::LowPolyFloor (d.View (), c) == far / DENT_FLOOR);
		// coalescing distance: exactly at 0.5 R_r it is not "< 0.5 R_r"
		DentRecord o {}, q {};
		o.p = Params (d.rest[0], Vector (0, 0, 1), 2.0 * std::sqrt (ref2 (c, d.rest[0])), 0.1, 0.0);
		q.p = Params (c, Vector (0, 0, 1), 1.0, 0.1, 0.0);
		std::vector<DentRecord> one (1, o);
		REQUIRE(DentMath::FindCoalesce (one, q) == -1);
		one[0].p.R = std::nextafter (one[0].p.R, 1e300);
		REQUIRE(DentMath::FindCoalesce (one, q) == 0);
	}
	NOTE ("CR13 fused sums differ in %d of 1600 distances\n", fusedDiffers);
	REQUIRE(fusedDiffers > 0);
}

TEST_CASE("CR14 integer conversions stay in range: tiny weld tolerance, edges beyond 1e9 m", "[dent]")
{
	DentObject o = Floor3 ();
	std::vector<std::vector<uint32_t>> w;
	REQUIRE(DentMath::WeldMap (o.rest, 1e-300, w) == o.nweld); // tolerance clamped to 1e-9: bitwise duplicates still weld
	REQUIRE(w == o.weld);
	DentObject g;
	std::vector<DentVtx> v = { { -5e9f, -5e9f, 0, 0, 0, 1, 0, 0 }, { 5e9f, -5e9f, 0, 0, 0, 1, 1, 0 }, { 0, 5e9f, 0, 0, 0, 1, 0, 1 } };
	g.rest.push_back (v), g.cur.push_back (v), g.idx.push_back ({ 0, 1, 2 });
	g.nweld = DentMath::WeldMap (g.rest, DENT_WELD, g.weld);
	DentObject g0 = g;
	REQUIRE(DentMath::Refine (g, Vector (0, 0, 0), 1.0, nullptr, 0, nullptr) == 0); // never split, no int64 edge key overflow
	REQUIRE(SameObj (g, g0));
}

TEST_CASE("CR16 ray cast misses and tmin; antiparallel normal rotation", "[dent]")
{
	DentViewData d = Icosphere (2, 1.0);
	double t = -7.0;
	REQUIRE_FALSE(DentMath::RayCast (d.View (), Vector (5, 5, 5), Vector (1, 0, 0), 0.0, 100.0, t));
	REQUIRE(t == -7.0);
	REQUIRE(DentMath::RayCast (d.View (), Vector (0, 0, 5), Vector (0, 0, -1), 0.0, 100.0, t));
	const double t0 = t;
	REQUIRE(std::fabs (t0 - 4.0) < 0.05);
	REQUIRE(DentMath::RayCast (d.View (), Vector (0, 0, 5), Vector (0, 0, -1), t0 + 0.01, 100.0, t));
	REQUIRE(std::fabs (t - 6.0) < 0.05); // tmin skips the near side
	REQUIRE_FALSE(DentMath::RayCast (d.View (), Vector (0, 0, 5), Vector (0, 0, -1), 0.0, t0 - 0.01, t));
	// a face turned over: the minimal rotation is a half turn about an axis normal to the rest sum
	DentObject o;
	AddGrid (o, Vector (0, 0, 0), Vector (1, 0, 0), Vector (0, 1, 0), 1, 1, Vector (0, 0.6, 0.8));
	o.nweld = DentMath::WeldMap (o.rest, DENT_WELD, o.weld);
	std::vector<Vector> rs;
	DentMath::FaceNormalSums (o.rest, o.idx, o.weld, o.nweld, rs);
	for (DentVtx &c : o.cur[0]) c.x = 1.0f - c.x; // mirrored: the faces now point to -z
	DentMath::Normals (o.rest, rs, o.idx, o.weld, o.nweld, std::vector<uint8_t> (o.nweld, 1), o.cur);
	for (const DentVtx &c : o.cur[0]) REQUIRE((N (c) - Vector (0, 0.6, -0.8)).length () < 1e-6);
}

TEST_CASE("U13 parser fuzz, long (sanitizer runs)", "[.fuzz]")
{
	static const char *tmpl[] = {
		"XDMG 1 15470.3 1", "XDMGM 0 0 999a2640 7 140 ShuttlePB", "XDMGD 0 0 -0.3 2.4 0 0 1 1.23104 0.0411352 0 *",
		"XDMGD 0 1 2 3 0 0 1 0.5 0.1 0 1,2,3", "XDMGM 1 3 ffffffff 65535 4000000000 a b c", "XDMG 2 99 0", "XDMGD 1 1e300 -1e-300 0 0.6 0 0.8 1e300 1e300 1e300 65535",
		"xdmgd\t0\t1 2 3 0 0 1 0.5 0.1 0 7,\r", "XDMG -1 0 0", "XDMGD 0 4.94065646e-324 0 0 0 0 1 1 1 0 *" };
	static const char *btmpl[] = {
		"BASE Moon:Brighton Beach", "BASEH 1234abcd Earth", "OBJ 1 BLOCK -60.6 -35 293900 0", "ODENT 1 -53.12 4 -47.99 0.5 0 -0.866025404 1.32716 0.398231 30",
		"END_BASE", "BASE :x", "BASE Mars:", "OBJ 99999999999 X 0 0 0 0", "ODENT 4294967295 0 0 0 0 1 0 1 1 1 1", "END_XDMG_BASES" };
	static const char *junk[] = { "nan", "inf", "-1", "abc", "1e999", "0x10", "99999999999", "*", ",", "-0", "+3", "65536", "1,", "", ":", "\t", "-nan", "1e-400", "+", "++1", "0.", ".5", "1e+", "e5" };
	Rng r { 777 };
	auto mutate = [&] (std::string l) {
		for (int n = 1 + (int)r.I (4); n > 0; n--) {
			switch (r.I (8)) {
			case 0: l.resize (r.I ((uint32_t)l.size () + 1)); break;
			case 1: l.insert (r.I ((uint32_t)l.size () + 1), std::string (junk[r.I (24)]) + (r.I (2) ? " " : "")); break;
			case 2: if (!l.empty ()) l.erase (r.I ((uint32_t)l.size ()), 1 + r.I (4)); break;
			case 3: if (!l.empty ()) l[r.I ((uint32_t)l.size ())] = (char)(1 + r.I (255)); break;
			case 4: l += std::string (r.I (300), " x\t,"[r.I (4)]); break;
			case 5: { std::string s; for (uint32_t i = r.I (40); i > 0; i--) s += (char)(1 + r.I (255)); l = (r.I (2) ? "XDMGD " : "XDMGM ") + s; } break;
			case 6: l.insert (r.I ((uint32_t)l.size () + 1), std::to_string ((int64_t)r.Next ())); break;
			default: break;
			}
		}
		return l;
	};
	long recs = 0, changed = 0, longLines = 0, outside = 0;
	for (int it = 0; it < 100000; it++) {
		std::vector<std::string> in;
		for (int i = 1 + (int)r.I (12); i > 0; i--) in.push_back (r.I (3) ? mutate (tmpl[r.I (10)]) : std::string (tmpl[r.I (10)]));
		DentVesselText v = ParseVessel (in);
		std::vector<std::string> out = Format (v);
		for (const std::string &s : out) if (s.size () > 200) longLines++;
		for (const DentRecord &d : v.rec) {
			DentParams q = d.p;
			DentMath::Quantise (q);
			if (!SameP (q, d.p) || !(d.p.R > 0.0 && d.p.R <= DENT_LIM_R && d.p.h <= DENT_LIM_H && d.p.T <= DENT_LIM_T && std::fabs (d.p.c.x) <= DENT_LIM_POS)) outside++;
		}
		recs += (long)v.rec.size ();
		int sk = -1;
		DentVesselText w = ParseVessel (out, &sk);
		bool same = sk == 0 && w.rec.size () == v.rec.size () && w.verbatim == v.verbatim && Bits (w.eabs, v.eabs) && w.flags == v.flags;
		for (size_t i = 0; same && i < v.rec.size (); i++) same = SameRec (w.rec[i], v.rec[i]);
		if (!same) changed++;
		std::vector<std::string> bin (1, "BEGIN_XDMG_BASES");
		for (int i = 1 + (int)r.I (12); i > 0; i--) bin.push_back (r.I (3) ? mutate (btmpl[r.I (10)]) : std::string (btmpl[r.I (10)]));
		std::vector<std::string> bout;
		DentMath::FormatBases (ParseBases (bin), bout);
		for (const std::string &s : bout) if (s.size () > 200) longLines++;
		DentRecord e {};
		bool more;
		DentMath::ParseDentEvent (mutate (tmpl[2] + 6).c_str (), e, more);
		int pa, pb, po;
		uint32_t f;
		DentParams p {};
		DentMath::ParseBaseDentEvent (mutate ("3:1:17 0 0 0 0 1 0 1 1 1").c_str (), pa, pb, po, p, f);
		double ea;
		DentMath::ParseStateEvent (mutate ("123.5 7").c_str (), ea, f);
	}
	NOTE ("U13 long fuzz: %ld records kept, %ld round-trip changes, %ld lines over 200, %ld records outside the limits or not quantised\n", recs, changed, longLines, outside);
	REQUIRE(recs > 0);
	REQUIRE(changed == 0);
	REQUIRE(longLines == 0);
	REQUIRE(outside == 0);
}
