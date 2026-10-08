// not upstream: unit tests for Src/Orbiter/CollGeom (Design C D1 9.1)
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include "CollGeom.h"
#include "CollTestMsh.h"

namespace collvm { static bool operator== (const Vector &a, const Vector &b) { return a.x == b.x && a.y == b.y && a.z == b.z; } } // found by ADL from Catch

namespace {

const char *MSH_ISS      = "Src/Vessel/ISS/Meshes/ProjectAlpha_ISS.msh";
const char *MSH_ATLANTIS = "Src/Vessel/Atlantis/Atlantis/Meshes/Atlantis.msh";
const char *MSH_DGNS     = "Src/Vessel/DeltaGlider/Meshes/deltaglider_ns.msh";
const char *MSH_SHUTTLEA = "Src/Vessel/ShuttleA/Meshes/ShuttleA.msh";
const char *MSH_TANK     = "Src/Vessel/Atlantis/Atlantis_Tank/Meshes/Atlantis_tank.msh";
const char *MSH_SRB      = "Src/Vessel/Atlantis/Atlantis_SRB/Meshes/Atlantis_SRB.msh";
const char *MSH_MMU      = "Src/Vessel/MMU/Meshes/mmu.msh";

// splitmix64: same sequence on every platform (no rand (), no std distributions)
struct Rng {
	uint64_t s;
	explicit Rng (uint64_t seed) : s (seed) {}
	uint64_t Next ()
	{
		uint64_t z = (s += 0x9e3779b97f4a7c15ull);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
		return z ^ (z >> 31);
	}
	double U () { return (double)(Next () >> 11) * (1.0 / 9007199254740992.0); }
	double R (double a, double b) { return a + (b - a) * U (); }
	Vector V (double a) { double x = R (-a, a), y = R (-a, a), z = R (-a, a); return Vector (x, y, z); }
	Vector Dir () { for (;;) { Vector v = V (1.0); double l = v.length(); if (l > 0.1 && l <= 1.0) return v / l; } }
};

Matrix AxisRot (const Vector &axis, double ang)
{
	Vector n = axis / axis.length();
	double c = std::cos (ang), s = std::sin (ang), C = 1.0 - c;
	return Matrix (
		c + n.x*n.x*C, n.x*n.y*C - n.z*s, n.x*n.z*C + n.y*s,
		n.y*n.x*C + n.z*s, c + n.y*n.y*C, n.y*n.z*C - n.x*s,
		n.z*n.x*C - n.y*s, n.z*n.y*C + n.x*s, c + n.z*n.z*C);
}

Matrix RandRot (Rng &r) { Vector ax = r.Dir (); return AxisRot (ax, r.R (0.0, Pi)); }

CollAffine RandRigid (Rng &r, double tr)
{
	CollAffine X;
	X.A = RandRot (r);
	X.t = r.V (tr);
	return X;
}

CollAffine RandAffine (Rng &r, double tr)
{
	CollAffine X;
	Matrix S;
	for (int i = 0; i < 9; i++) S.data[i] = (i % 4 == 0) ? r.R (0.5, 2.0) : r.R (-0.3, 0.3);
	X.A = RandRot (r) * S;
	X.t = r.V (tr);
	return X;
}

CollVtx Vtx (const Vector &p) { CollVtx v {}; v.x = (float)p.x; v.y = (float)p.y; v.z = (float)p.z; return v; }
Vector Pos (const CollVtx &v) { return Vector (v.x, v.y, v.z); }

// separate triangles of size s around random centres in a box of half size L
CollGroupData SoupGroup (Rng &r, int ntri, double L, double s, const Vector &ofs)
{
	CollGroupData g;
	for (int i = 0; i < ntri; i++) {
		Vector c = ofs + r.V (L);
		for (int k = 0; k < 3; k++) {
			g.idx.push_back ((uint16_t)g.vtx.size());
			g.vtx.push_back (Vtx (c + r.V (s)));
		}
	}
	return g;
}

// noisy height field of 2*nx*nz triangles sharing vertices
CollGroupData GridGroup (Rng &r, int nx, int nz, double L, double noise, const Vector &ofs)
{
	CollGroupData g;
	for (int j = 0; j <= nz; j++)
		for (int i = 0; i <= nx; i++)
			g.vtx.push_back (Vtx (ofs + Vector (-L + 2*L*i/nx, r.R (-noise, noise), -L + 2*L*j/nz)));
	for (int j = 0; j < nz; j++)
		for (int i = 0; i < nx; i++) {
			uint16_t a = (uint16_t)(j*(nx+1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + nx + 1), d = (uint16_t)(c + 1);
			uint16_t t[6] = { a, c, b, b, c, d };
			g.idx.insert (g.idx.end(), t, t + 6);
		}
	return g;
}

bool BuildGroups (const std::vector<CollGroupData> &grp, CollGeom &G, CollBuildStats *st = nullptr, double tol = COLL_WELD_DEFAULT,
	const std::vector<uint32_t> *scope = nullptr)
{
	std::vector<CollSrcGroup> src;
	for (size_t i = 0; i < grp.size(); i++)
		src.push_back (CollSrcGroup { &grp[i], CollSrc { 0, (uint32_t)i, 0, 0, scope ? (*scope)[i] : 0u } });
	return G.Build (src.data(), src.size(), tol, st);
}

bool LoadStock (const char *rel, CollRestMesh &m)
{
	std::string err;
	bool ok = CollTestLoadMsh (CollTestPath (rel).c_str(), m, &err);
	if (!ok) UNSCOPED_INFO (err);
	return ok;
}

// mesh with all groups in one weld scope
bool BuildStock (const char *rel, CollRestMesh &m, CollGeom &G, CollBuildStats &st)
{
	if (!LoadStock (rel, m)) return false;
	return BuildGroups (m.grp, G, &st);
}

// independent point-segment and point-triangle distances (no shared code with CollGeom.cpp)
double PtSeg (const Vector &p, const Vector &a, const Vector &b)
{
	Vector d = b - a;
	double l2 = d.length2(), t = l2 > 0 ? std::clamp (dotp (p - a, d) / l2, 0.0, 1.0) : 0.0;
	return (p - (a + d*t)).length();
}

double PtTri (const Vector &p, const Vector *t)
{
	double best = std::min (std::min (PtSeg (p, t[0], t[1]), PtSeg (p, t[1], t[2])), PtSeg (p, t[2], t[0]));
	Vector n = crossp (t[1] - t[0], t[2] - t[0]);
	double n2 = n.length2();
	if (n2 > 0) {
		Vector q = p - n * (dotp (p - t[0], n) / n2);
		bool in = true;
		for (int i = 0; i < 3; i++)
			if (dotp (crossp (t[(i+1)%3] - t[i], q - t[i]), n) < 0) in = false;
		if (in) best = std::min (best, (p - q).length());
	}
	return best;
}

// upper bound of the triangle distance: barycentric grid of each side against the other triangle
double DenseMin (const Vector *a, const Vector *b, int N)
{
	double best = 1e300;
	for (int side = 0; side < 2; side++) {
		const Vector *s = side ? b : a, *o = side ? a : b;
		for (int i = 0; i <= N; i++)
			for (int j = 0; i + j <= N; j++) {
				double u = (double)i / N, v = (double)j / N;
				best = std::min (best, PtTri (s[0] + (s[1] - s[0])*u + (s[2] - s[0])*v, o));
			}
	}
	return best;
}

void TriOf (const CollGeom &G, const CollAffine &X, uint32_t t, Vector *v)
{
	for (int k = 0; k < 3; k++) v[k] = CollApply (X, G.vtx[G.tri[t].v[k]]);
}

double BruteDist (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB, const uint8_t *mA = nullptr, const uint8_t *mB = nullptr)
{
	double best = 1e300;
	for (uint32_t i = 0; i < A.tri.size(); i++) {
		if (mA && mA[A.tri[i].src]) continue;
		Vector a[3];
		TriOf (A, XA, i, a);
		for (uint32_t j = 0; j < B.tri.size(); j++) {
			if (mB && mB[B.tri[j].src]) continue;
			Vector b[3], pa, pb;
			TriOf (B, XB, j, b);
			best = std::min (best, CollTriTriDistance (a, b, pa, pb));
		}
	}
	return best;
}

std::vector<std::pair<uint32_t,uint32_t>> BrutePairs (const CollGeom &A, const CollAffine &XA, const CollGeom &B, const CollAffine &XB, double d,
	const uint8_t *mA = nullptr, const uint8_t *mB = nullptr)
{
	std::vector<std::pair<uint32_t,uint32_t>> out;
	for (uint32_t i = 0; i < A.tri.size(); i++) {
		if (mA && mA[A.tri[i].src]) continue;
		Vector a[3];
		TriOf (A, XA, i, a);
		for (uint32_t j = 0; j < B.tri.size(); j++) {
			if (mB && mB[B.tri[j].src]) continue;
			Vector b[3], pa, pb;
			TriOf (B, XB, j, b);
			if (CollTriTriDistance (a, b, pa, pb) < d) out.push_back (std::make_pair (i, j));
		}
	}
	return out;
}

// Moller-Trumbore over all triangles in the ray frame, both faces
bool BruteRay (const CollGeom &G, const CollAffine &X, const Vector &o, const Vector &d, double tmin, double tmax, double &tbest, uint32_t &tri)
{
	tbest = tmax; tri = ~0u;
	for (uint32_t i = 0; i < G.tri.size(); i++) {
		Vector v[3];
		TriOf (G, X, i, v);
		Vector e1 = v[1] - v[0], e2 = v[2] - v[0], p = crossp (d, e2);
		double det = dotp (e1, p);
		if (std::fabs (det) < 1e-300) continue;
		Vector s = o - v[0], q = crossp (s, e1);
		double u = dotp (s, p) / det, w = dotp (d, q) / det, t = dotp (e2, q) / det;
		if (u < 0 || w < 0 || u + w > 1 || t < tmin || t > tbest) continue;
		tbest = t; tri = i;
	}
	return tri != ~0u;
}

// BVH invariants of D1 9.1; returns the maximum depth
int CheckTree (const CollGeom &G)
{
	size_t n = G.tri.size();
	REQUIRE (G.perm.size() == n);
	std::vector<int> seen (n, 0), cover (n, 0), visit (G.node.size(), 0);
	bool pm = true;
	for (uint32_t p : G.perm) { if (p < n) seen[p]++; else pm = false; }
	for (size_t i = 0; i < n; i++) if (seen[i] != 1) pm = false;
	REQUIRE (pm);
	if (!n) { REQUIRE (G.node.empty()); return 0; }
	struct Item { uint32_t id; int depth; };
	std::vector<Item> st { { 0, 0 } };
	int maxd = 0;
	bool ok = true;
	while (!st.empty()) {
		Item it = st.back();
		st.pop_back();
		const CollNode &nd = G.node[it.id];
		visit[it.id]++;
		maxd = std::max (maxd, it.depth);
		if (nd.count) {
			if (nd.count > (uint32_t)COLL_BVH_LEAF || nd.first + nd.count > n) ok = false;
			for (uint32_t i = nd.first; i < nd.first + nd.count && i < n; i++) {
				cover[i]++;
				const CollTri &T = G.tri[G.perm[i]];
				for (int k = 0; k < 3; k++)
					for (int a = 0; a < 3; a++) {
						double x = G.vtx[T.v[k]].data[a];
						if (x < nd.mn[a] || x > nd.mx[a]) ok = false;
					}
			}
		} else {
			for (int c = 0; c < 2; c++) {
				uint32_t ch = nd.first + c;
				if (ch <= it.id || ch >= G.node.size()) { ok = false; continue; }
				const CollNode &cn = G.node[ch];
				for (int a = 0; a < 3; a++)
					if (cn.mn[a] < nd.mn[a] || cn.mx[a] > nd.mx[a]) ok = false;
				st.push_back (Item { ch, it.depth + 1 });
			}
		}
	}
	REQUIRE (ok);
	for (size_t i = 0; i < n; i++) if (cover[i] != 1) ok = false;
	for (size_t i = 0; i < G.node.size(); i++) if (visit[i] != 1) ok = false;
	REQUIRE (ok);
	REQUIRE (maxd <= COLL_BVH_DEPTH);
	return maxd;
}

// every render vertex in one ref list, within tolerance; every render triangle welds to its tri
void CheckRefs (const CollGeom &G, const std::vector<CollGroupData> &grp, double tol)
{
	REQUIRE (G.refOfs.size() == G.vtx.size() + 1);
	REQUIRE (G.refOfs.back() == G.ref.size());
	std::vector<std::vector<uint32_t>> owner (grp.size());
	size_t total = 0;
	for (size_t g = 0; g < grp.size(); g++) { owner[g].assign (grp[g].vtx.size(), ~0u); total += grp[g].vtx.size(); }
	REQUIRE (G.ref.size() == total);
	bool ok = true;
	for (uint32_t w = 0; w < G.vtx.size(); w++) {
		if (G.refOfs[w] >= G.refOfs[w+1]) ok = false;
		for (uint32_t k = G.refOfs[w]; k < G.refOfs[w+1]; k++) {
			const CollRef &r = G.ref[k];
			if (owner[r.src][r.vtx] != ~0u) ok = false;
			owner[r.src][r.vtx] = w;
			if ((Pos (grp[r.src].vtx[r.vtx]) - G.vtx[w]).length() > tol) ok = false;
		}
		const CollRef &r0 = G.ref[G.refOfs[w]];
		if (!(Pos (grp[r0.src].vtx[r0.vtx]) == G.vtx[w])) ok = false;
	}
	REQUIRE (ok);
	for (const CollTri &T : G.tri) {
		const CollGroupData &g = grp[T.src];
		if ((size_t)T.otri * 3 + 2 >= g.idx.size()) { ok = false; continue; }
		for (int k = 0; k < 3; k++) {
			if (g.idx[T.otri*3 + k] != T.ov[k]) ok = false;
			if (owner[T.src][T.ov[k]] != T.v[k]) ok = false;
		}
	}
	REQUIRE (ok);
}

} // namespace

TEST_CASE("CollTestMsh: core parser rules on a synthetic file", "[collgeom][loader]")
{
	std::string text =
		"MSHX1\r\n"
		"groups 4\n"
		"LABEL first\n"
		"FLAG 0x2\n"
		"FLIP\n"
		"GEOM 3 1 ; comment\n"
		"0 0 0 0 1 0 0 0\n"
		"1 0 0 0 1 0 1 0\n"
		"0 0 1 0 1 0 0 1\n"
		"0 1 2\n"
		"MATERIAL 1\n"
		"NONORMAL\n"
		"GEOM 4 2\n"
		"0 0 0 0.5 0.5\n"
		"1 0 0 1 0\n"
		"1 1 0 1 1\n"
		"0 1 0\n"
		"0 1 2\n"
		"0 2 65539\n"
		"GEOM 0 0\n"
		"GEOM 3 1\n"
		"+1.5 -2 3e0\n"
		"4 5 6 0 0 1\n"
		"7 8 9 0 0 1 0.25\n"
		"2 1\n";
	// in memory: no test file is written anywhere (master 8 U7, code review C-A-a 10)
	CollRestMesh m;
	std::string err;
	REQUIRE (CollTestParseMsh (text, "Meshes\\colltestmsh_rules.MSH", m, &err));
	REQUIRE (m.name == "colltestmsh_rules");
	REQUIRE (m.grp.size() == 3);
	REQUIRE (m.nvtx == 10);
	REQUIRE (m.grp[0].usrflag == 2);
	REQUIRE (m.grp[0].idx == std::vector<uint16_t> { 0, 2, 1 });
	REQUIRE (m.grp[0].vtx[1].tu == 1.0f);
	REQUIRE (m.grp[1].usrflag == 0);
	REQUIRE (m.grp[1].vtx[0].tu == 0.5f);
	REQUIRE (m.grp[1].vtx[3].tu == 0.0f);
	REQUIRE (m.grp[1].idx == std::vector<uint16_t> { 0, 1, 2, 0, 2, 3 });
	REQUIRE (std::fabs (m.grp[1].vtx[1].nz - 1.0f) < 1e-6f);
	REQUIRE (m.grp[2].vtx[0].x == 1.5f);
	REQUIRE (m.grp[2].vtx[0].y == -2.0f);
	REQUIRE (m.grp[2].vtx[0].z == 3.0f);
	REQUIRE (m.grp[2].vtx[1].tu == 0.0f);
	REQUIRE (m.grp[2].vtx[2].tu == 0.25f);
	REQUIRE (m.grp[2].vtx[2].nz == 1.0f);
	REQUIRE (m.grp[2].idx == std::vector<uint16_t> { 2, 1, 0 });

	REQUIRE_FALSE (CollTestParseMsh ("MSHX2\nGROUPS 1\n", "bad.msh", m, &err));
	REQUIRE (CollTestParseMsh ("MSHX1\nGROUPS 3\nGEOM 3 1\n0 0 0\n1 0 0\n0 1 0\n0 1 2\nGEOM 3 1\n0 0 0\n", "trunc.msh", m, &err));
	REQUIRE (m.grp.size() == 1);
	REQUIRE_FALSE (CollTestLoadMsh (CollTestPath ("Meshes/no_such_mesh.msh").c_str(), m, &err));
	REQUIRE (err.find ("cannot open") != std::string::npos);
}

TEST_CASE("Loader sanity on stock files", "[collgeom][stock][.slow]")
{
	struct Row { const char *path; size_t ngrp, ntri; } rows[] = {
		{ MSH_ISS, 42, 24309 }, { MSH_ATLANTIS, 59, 13923 }, { MSH_DGNS, 122, 11803 }, { MSH_SHUTTLEA, 67, 7846 } };
	for (const Row &r : rows) {
		CollRestMesh m;
		REQUIRE (LoadStock (r.path, m));
		size_t nt = 0;
		for (const CollGroupData &g : m.grp) nt += g.idx.size() / 3;
		INFO (r.path);
		CHECK (m.grp.size() == r.ngrp);
		CHECK (nt == r.ntri);
	}
}

TEST_CASE("Weld: tolerance, cell boundary, scope, representative", "[collgeom][weld]")
{
	auto two = [](const Vector &a, const Vector &b, double tol, bool sameScope) {
		std::vector<CollGroupData> g (2);
		g[0].vtx = { Vtx (a), Vtx (Vector (1, 0, 0)), Vtx (Vector (0, 1, 0)) };
		g[0].idx = { 0, 1, 2 };
		g[1].vtx = { Vtx (b), Vtx (Vector (1, 0, 1)), Vtx (Vector (0, 1, 1)) };
		g[1].idx = { 0, 1, 2 };
		std::vector<uint32_t> sc = { 0, sameScope ? 0u : 7u };
		CollGeom G;
		REQUIRE (BuildGroups (g, G, nullptr, tol, &sc));
		CheckRefs (G, g, tol);
		return G.tri[0].v[0] == G.tri[1].v[0];
	};
	Vector p (3.25, -1.5, 0.75);
	CHECK (two (p, p + Vector (5e-5, 0, 0), 1e-4, true));
	CHECK (two (p, p + Vector (3e-5, 3e-5, 3e-5), 1e-4, true));
	CHECK_FALSE (two (p, p + Vector (2e-4, 0, 0), 1e-4, true));
	CHECK_FALSE (two (p, p + Vector (0, 0, -1.2e-4), 1e-4, true));
	CHECK_FALSE (two (p, p + Vector (5e-5, 0, 0), 1e-4, false));
	// both sides of a grid line for several cell sizes (cell = 2 tol and floors)
	for (double tol : { 1e-4, 1e-3, 0.01 }) {
		double edge = 2.0 * tol * 1000.0;
		CHECK (two (Vector (edge - 0.3*tol, 1, 1), Vector (edge + 0.3*tol, 1, 1), tol, true));
		CHECK (two (Vector (-edge - 0.45*tol, 1, 1), Vector (-edge + 0.45*tol, 1, 1), tol, true));
		CHECK (two (Vector (1, edge + 0.49*tol, 2), Vector (1, edge - 0.49*tol, 2), tol, true));
	}
	CHECK_FALSE (two (p, p + Vector (1e-5, 0, 0), 0.0, true));
	CHECK (two (p, p, 0.0, true));

	// first vertex seen is the representative; later ones join the lowest welded index within tolerance
	std::vector<CollGroupData> g (1);
	g[0].vtx = { Vtx (Vector (0, 0, 0)), Vtx (Vector (1.6e-4, 0, 0)), Vtx (Vector (0.8e-4, 0, 0)), Vtx (Vector (5, 0, 0)), Vtx (Vector (0, 5, 0)) };
	g[0].idx = { 2, 3, 4, 1, 3, 4 };
	CollGeom G;
	CollBuildStats st;
	REQUIRE (BuildGroups (g, G, &st));
	REQUIRE (G.vtx.size() == 4);
	REQUIRE (G.tri.size() == 2);
	CHECK (G.tri[0].v[0] == 0);
	CHECK (G.tri[1].v[0] == 1);
	CHECK (G.vtx[0] == Pos (g[0].vtx[0]));
	CHECK (G.RestPos (1) == Pos (g[0].vtx[1]));
	CheckRefs (G, g, COLL_WELD_DEFAULT);
	CHECK (G.ref[G.refOfs[0]].vtx == 0);
	CHECK (G.ref[G.refOfs[0] + 1].vtx == 2);
}

TEST_CASE("Weld counts on stock meshes", "[collgeom][weld][stock]")
{
	CollRestMesh m;
	CollGeom G;
	CollBuildStats st;
	REQUIRE (BuildStock (MSH_ATLANTIS, m, G, st));
	CHECK (st.nrender == 15540);
	CHECK (st.nweld == 8545);
	CheckRefs (G, m.grp, COLL_WELD_DEFAULT);
}

TEST_CASE("Weld counts on stock meshes (ISS)", "[collgeom][weld][stock][.slow]")
{
	CollRestMesh m;
	CollGeom G;
	CollBuildStats st;
	REQUIRE (BuildStock (MSH_ISS, m, G, st));
	CHECK (st.nrender == 38624);
	CHECK (st.nweld == 14948);
	CheckRefs (G, m.grp, COLL_WELD_DEFAULT);
}

TEST_CASE("Triangle filters: synthetic", "[collgeom][filters]")
{
	std::vector<CollGroupData> g (2);
	const float nan = std::numeric_limits<float>::quiet_NaN();
	g[0].vtx = { Vtx (Vector (0, 0, 0)), Vtx (Vector (1, 0, 0)), Vtx (Vector (0, 1, 0)), Vtx (Vector (0.5e-4, 0, 0)),
		Vtx (Vector (2e6, 0, 0)), Vtx (Vector (0.5, 0.5e-12, 0)), Vtx (Vector (1, 1, 0)) };
	g[0].vtx[6].y = nan;
	g[0].idx = {
		0, 1, 2,      // kept
		0, 1, 9,      // index out of range
		0, 4, 2,      // vertex beyond 1e6 m
		0, 6, 2,      // NaN vertex
		0, 3, 2,      // 3 welds onto 0
		0, 0, 1,      // repeated index
		0, 1, 5,      // area below 1e-24 m^4
		1, 2, 0,      // same triangle, same winding
		0, 2, 1 };    // double-sided copy
	g[1].vtx = { Vtx (Vector (0, 0, 0)), Vtx (Vector (1, 0, 0)), Vtx (Vector (0, 1, 0)) };
	g[1].idx = { 2, 1, 0 };
	std::vector<uint32_t> sc = { 0, 0 };
	CollGeom G;
	CollBuildStats st;
	REQUIRE (BuildGroups (g, G, &st, COLL_WELD_DEFAULT, &sc));
	CHECK (st.ntriIn == 10);
	CHECK (st.ntri == 1);
	CHECK (st.dropIndex == 1);
	CHECK (st.dropInvalid == 2);
	CHECK (st.dropDegen == 3);
	CHECK (st.dropDup == 3);
	CHECK (G.tri[0].otri == 0);
	CHECK (G.version > 0);
	CHECK (G.vtx.size() == 4);
	CHECK (G.ref.size() == 8);

	// another scope keeps its copy: welds and duplicates stay inside one scope
	sc[1] = 1;
	REQUIRE (BuildGroups (g, G, &st, COLL_WELD_DEFAULT, &sc));
	CHECK (st.ntri == 2);
	CHECK (G.tri[1].src == 1);
	CHECK (G.tri[1].otri == 0);

	// nothing survives: false, empty tree, queries safe
	std::vector<CollGroupData> e (1);
	e[0].vtx = { Vtx (Vector (0, 0, 0)), Vtx (Vector (1, 0, 0)) };
	e[0].idx = { 0, 1, 1 };
	CollGeom E;
	REQUIRE_FALSE (BuildGroups (e, E, &st));
	CHECK (E.node.empty());
	CHECK_FALSE (E.Refit ());
	CollScratch s;
	CollAffine I;
	CollRayHit rh;
	uint32_t ct;
	Vector q;
	CHECK (CollDistance (E, I, G, I, 5.0, nullptr, s) == 5.0);
	CHECK_FALSE (CollRayCast (E, I, Vector (0, 0, 0), Vector (1, 0, 0), 0, 10, rh));
	CHECK (CollClosestPoint (E, Vector (0, 0, 0), 3.0, ct, q) == 3.0);
}

TEST_CASE("Triangle filters: stock degenerates and duplicates", "[collgeom][filters][stock]")
{
	CollRestMesh m;
	CollGeom G;
	CollBuildStats st;
	REQUIRE (BuildStock (MSH_TANK, m, G, st));
	CHECK (st.dropDegen == 50);
	REQUIRE (BuildStock (MSH_SRB, m, G, st));
	CHECK (st.dropDegen == 64);
	REQUIRE (BuildStock (MSH_ATLANTIS, m, G, st));
	CHECK (st.dropDegen == 2);
	CHECK (st.dropIndex + st.dropInvalid == 0);
	REQUIRE (BuildStock (MSH_MMU, m, G, st));
	CHECK (st.dropDup == 2664);
	CHECK (st.dropDegen == 0);
	CheckRefs (G, m.grp, COLL_WELD_DEFAULT);
	CheckTree (G);
}

TEST_CASE("Refs and back map", "[collgeom][refs][stock]")
{
	CollRestMesh m;
	CollGeom G;
	CollBuildStats st;
	REQUIRE (BuildStock (MSH_ATLANTIS, m, G, st));
	CheckRefs (G, m.grp, COLL_WELD_DEFAULT);
	REQUIRE (G.srcTab.size() == m.grp.size());
	for (size_t i = 0; i < m.grp.size(); i++) REQUIRE (G.srcTab[i].grp == i);

	Rng r (11);
	std::vector<CollGroupData> g = { GridGroup (r, 12, 9, 2.0, 0.3, Vector (0, 0, 0)), SoupGroup (r, 50, 2.0, 0.4, Vector (1, 0, 0)) };
	g.push_back (g[0]);
	CollGeom H;
	REQUIRE (BuildGroups (g, H, &st, 1e-3));
	CheckRefs (H, g, 1e-3);
	CHECK (st.dropDup == g[0].idx.size() / 3);
}

TEST_CASE("BVH invariants", "[collgeom][bvh]")
{
	Rng r (5);
	for (int it = 0; it < 6; it++) {
		std::vector<CollGroupData> g = { GridGroup (r, 10 + it, 7, 3.0, 0.5, r.V (1)), SoupGroup (r, 40 + 60*it, 4.0, 0.8, r.V (2)) };
		CollGeom G;
		CollBuildStats st;
		REQUIRE (BuildGroups (g, G, &st));
		CheckTree (G);
		CHECK (st.nnode == G.node.size());
		for (const CollTri &T : G.tri)
			for (int k = 0; k < 3; k++) CHECK ((G.vtx[T.v[k]] - G.bsCentre).length() <= G.bsRadius);
	}
	// equal centroids: every triangle (p, q, -(p+q)) has centroid exactly 0, so the tree halves ranges
	std::vector<CollGroupData> f (1);
	for (int i = 1; i <= 6; i++)
		for (int j = 1; j <= 6; j++) {
			Vector p (i, 1, 0), q (0, j, 1);
			for (const Vector &v : { p, q, -(p + q) }) {
				f[0].idx.push_back ((uint16_t)f[0].vtx.size());
				f[0].vtx.push_back (Vtx (v));
			}
		}
	CollGeom F;
	REQUIRE (BuildGroups (f, F));
	CheckTree (F);
	// stock
	CollRestMesh m;
	CollGeom G;
	CollBuildStats st;
	REQUIRE (BuildStock (MSH_ATLANTIS, m, G, st));
	int depth = CheckTree (G);
	CHECK (depth < 40);
}

// pair class: 0 generic, 1 cross, 2 touch, 3 coplanar, 4 sliver, 5 parallel, 6 shared edge, 7 exact
void TriPair (Rng &r, int cls, Vector *a, Vector *b)
{
	for (int k = 0; k < 3; k++) a[k] = r.V (1.0);
	Vector n = crossp (a[1] - a[0], a[2] - a[0]);
	n = n / n.length();
	double u = r.R (0.1, 0.8), v = r.R (0.05, 0.9 - u);
	Vector x = a[0] + (a[1] - a[0])*u + (a[2] - a[0])*v;
	switch (cls) {
	case 1: {
		Vector d = n * r.R (0.3, 1.0);
		d += r.V (0.5);
		b[0] = x + d * r.R (0.1, 1.0); b[1] = x - d * r.R (0.1, 1.0); b[2] = x + r.V (1.0);
		break; }
	case 2: {
		Vector t1 = crossp (n, a[1] - a[0]); t1 = t1 / t1.length();
		Vector t2 = crossp (n, t1);
		double h1 = r.R (0.05, 1.0), s1 = r.R (-1, 1), h2 = r.R (0.05, 1.0), s2 = r.R (-1, 1);
		b[0] = x; b[1] = x + n * h1 + t1 * s1; b[2] = x + n * h2 + t2 * s2;
		break; }
	case 3: {
		Matrix R = AxisRot (n, r.R (0, Pi2));
		double s1 = r.R (-1.5, 1.5), s2 = r.R (-1.5, 1.5);
		Vector sh = (a[1] - a[0]) * s1 + (a[2] - a[0]) * s2;
		for (int k = 0; k < 3; k++) b[k] = x + mul (R, a[k] - x) * r.R (0.3, 1.2) + sh;
		break; }
	case 4: {
		b[0] = r.V (1.0);
		b[0] += n * r.R (-0.3, 0.3);
		b[1] = r.V (1.0);
		Vector e = b[1] - b[0], p = crossp (e, r.Dir ());
		b[2] = b[0] + e * r.R (0.0, 1.0) + p * (1e-14 / p.length());
		break; }
	case 5: {
		double dz = r.U () < 0.5 ? r.R (1e-6, 1e-3) : r.R (0.01, 0.5);
		double s1 = r.R (-0.5, 0.5), s2 = r.R (-0.5, 0.5);
		Vector sh = (a[1] - a[0]) * s1 + (a[2] - a[0]) * s2;
		for (int k = 0; k < 3; k++) b[k] = a[(k*2)%3] + n * dz + sh * 0.3;
		break; }
	case 6: {
		b[0] = a[1]; b[1] = a[0]; b[2] = a[2] + n * r.R (-1, 1);
		b[2] += r.V (0.5);
		break; }
	case 7: {
		double z = r.R (-1, 1);
		int ax = (int)(r.U () * 3);
		for (int k = 0; k < 6; k++) {
			Vector &v = k < 3 ? a[k] : b[k-3];
			v = r.V (1.0);
			v.data[ax] = z;
		}
		break; }
	default:
		for (int k = 0; k < 3; k++) { b[k] = r.V (1.0); b[k].x += r.R (-2, 2); }
	}
}

void TriTriRandom (int npair, uint64_t seed)
{
	Rng r (seed);
	int fails[6] = {};
	double worst[3] = {};
	for (int i = 0; i < npair; i++) {
		int cls = i % 8;
		Vector a[3], b[3], pa, pb, qa, qb;
		TriPair (r, cls, a, b);
		if (r.U () < 0.1) std::swap (a[0], a[2]);
		double d = CollTriTriDistance (a, b, pa, pb);
		double scale = 3.0;
		if (!(d >= 0.0)) fails[0]++;
		if (std::fabs ((pa - pb).length() - d) > 1e-12) fails[1]++;
		double onA = PtTri (pa, a), onB = PtTri (pb, b);
		worst[0] = std::max (worst[0], std::max (onA, onB));
		if (onA > 1e-12*scale || onB > 1e-12*scale) fails[2]++;
		double dm = DenseMin (a, b, 16);
		worst[1] = std::max (worst[1], d - dm);
		if (d > dm + 1e-9) fails[3]++;
		if ((cls == 1 || cls == 2 || cls == 6) && d > 1e-12) fails[3]++;
		if (cls == 7 && dm == 0.0 && d != 0.0) fails[3]++;
		double d2 = CollTriTriDistance (b, a, qb, qa);
		if (std::fabs (d2 - d) > 1e-12) fails[4]++;
		CollAffine X = RandRigid (r, 50.0);
		Vector xa[3], xb[3];
		for (int k = 0; k < 3; k++) xa[k] = CollApply (X, a[k]), xb[k] = CollApply (X, b[k]);
		double d3 = CollTriTriDistance (xa, xb, qa, qb);
		worst[2] = std::max (worst[2], std::fabs (d3 - d));
		if (std::fabs (d3 - d) > 1e-9) fails[5]++;
	}
	std::printf ("CollTriTriDistance %d pairs: max off-triangle %.3g m, max d - dense min %.3g m, max rigid change %.3g m\n", npair, worst[0], worst[1], worst[2]);
	for (int k = 0; k < 6; k++) { INFO ("check " << k); CHECK (fails[k] == 0); }
}

TEST_CASE("CollTriTriDistance: random pairs", "[collgeom][tritri]")
{
	TriTriRandom (1400, 101);
	Vector a[3] = { Vector (0, 0, 0), Vector (1, 0, 0), Vector (0, 1, 0) }, pa, pb;
	Vector b[3] = { Vector (0.2, 0.2, -1), Vector (0.2, 0.2, 1), Vector (2, 2, 0) };
	CHECK (CollTriTriDistance (a, b, pa, pb) == 0.0);
	Vector c[3] = { Vector (0, 0, 2), Vector (1, 0, 2), Vector (0, 1, 2) };
	CHECK (std::fabs (CollTriTriDistance (a, c, pa, pb) - 2.0) < 1e-15);
	Vector s[3] = { Vector (-1, 0.5, 0.5), Vector (1, 0.5, 0.5), Vector (0, 0.5, 0.5 + 1e-14) };
	CHECK (std::fabs (CollTriTriDistance (a, s, pa, pb) - 0.5) < 1e-12);
	Vector s2[3] = { Vector (-1, 0.25, 0), Vector (1, 0.25, 0), Vector (0, 0.25, 1e-14) };
	CHECK (CollTriTriDistance (a, s2, pa, pb) < 1e-12);
	// exactly coplanar: overlap, containment, shared vertex, collinear edges: 0; disjoint: in-plane gap
	Vector c1[3] = { Vector (0.2, 0.2, 0), Vector (2, 0.2, 0), Vector (0.2, 2, 0) };
	Vector c2[3] = { Vector (0.1, 0.1, 0), Vector (0.3, 0.1, 0), Vector (0.1, 0.3, 0) };
	Vector c3[3] = { Vector (1, 0, 0), Vector (2, 0, 0), Vector (2, -1, 0) };
	Vector c4[3] = { Vector (-1, 0, 0), Vector (2, 0, 0), Vector (0.5, -1, 0) };
	Vector c5[3] = { Vector (1, 1, 0), Vector (2, 1, 0), Vector (1, 2, 0) };
	CHECK (CollTriTriDistance (a, c1, pa, pb) == 0.0);
	CHECK (CollTriTriDistance (a, c2, pa, pb) == 0.0);
	CHECK (CollTriTriDistance (c2, a, pa, pb) == 0.0);
	CHECK (CollTriTriDistance (a, c3, pa, pb) == 0.0);
	CHECK (CollTriTriDistance (a, c4, pa, pb) == 0.0);
	CHECK (std::fabs (CollTriTriDistance (a, c5, pa, pb) - std::sqrt (0.5)) < 1e-15);
}

TEST_CASE("CollTriTriDistance: 10,000 random pairs", "[collgeom][tritri][.slow]")
{
	TriTriRandom (10000, 202);
}

TEST_CASE("Non-finite input is no hit (code review C-A-a 8)", "[collgeom][tritri]")
{
	const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
	Vector far[3] = { Vector (100, 100, 100), Vector (101, 100, 100), Vector (100, 101, 100) };
	Vector near[3] = { Vector (0, 0, 0.5), Vector (1, 0, 0.5), Vector (0, 1, 0.5) };
	Vector pa, pb;
	for (double bad : { nan, inf, -inf }) {
		for (int k = 0; k < 9; k++) {
			Vector a[3] = { Vector (0, 0, 0), Vector (1, 0, 0), Vector (0, 1, 0) };
			a[k / 3].data[k % 3] = bad;
			INFO ("coordinate " << k << " = " << bad);
			CHECK (CollTriTriDistance (a, far, pa, pb) == inf);
			CHECK (CollTriTriDistance (far, a, pa, pb) == inf);
			CHECK (CollTriTriDistance (a, near, pa, pb) == inf);
		}
	}
	// a NaN pose: nothing within any distance, the cap is returned
	CollGroupData g;
	g.vtx = { Vtx (Vector (0, 0, 0)), Vtx (Vector (1, 0, 0)), Vtx (Vector (0, 1, 0)) };
	g.idx = { 0, 1, 2 };
	CollGeom A, B;
	REQUIRE (BuildGroups ({ g }, A));
	REQUIRE (BuildGroups ({ g }, B));
	CollAffine XA, XB;
	XB.t = Vector (1000, 0, 0);
	XB.A.m11 = nan;
	CollScratch s;
	CollHit h;
	CHECK (CollDistance (A, XA, B, XB, 1e9, &h, s) == 1e9);
	CHECK (h.ta == ~0u);
	CHECK_FALSE (CollOverlap (A, XA, B, XB, 0.04, s));
	std::vector<CollHit> out;
	CHECK (CollPairsWithin (A, XA, B, XB, 0.04, out, 100, s) == 0);
}

struct QueryCase { CollGeom A, B; std::vector<CollGroupData> ga, gb; CollAffine XA, XB; };

void MakeCase (Rng &r, QueryCase &q, int ntri, bool affine)
{
	int ns = ntri / 2;
	int nx = std::max (2, (int)std::sqrt ((double)ntri / 4));
	q.ga = { GridGroup (r, nx, nx, 2.0, 0.4, Vector (0, 0, 0)), SoupGroup (r, ns / 2, 2.0, 0.6, Vector (0, 0.3, 0)), SoupGroup (r, ns - ns/2, 2.0, 0.6, Vector (0, -0.3, 0)) };
	q.gb = { SoupGroup (r, ns, 2.0, 0.6, Vector (0, 0, 0)), GridGroup (r, nx, nx, 1.5, 0.4, Vector (0, 0, 0)) };
	REQUIRE (BuildGroups (q.ga, q.A));
	REQUIRE (BuildGroups (q.gb, q.B));
	q.XA = affine ? RandAffine (r, 1.0) : RandRigid (r, 1.0);
	q.XB = affine ? RandAffine (r, 1.0) : RandRigid (r, 1.0);
	Vector dir = r.Dir ();
	q.XB.t = q.XA.t + dir * r.R (0.0, 7.0);
}

void QueriesVsBrute (int ncase, int ntri, uint64_t seed)
{
	Rng r (seed);
	CollScratch s;
	for (int c = 0; c < ncase; c++) {
		QueryCase q;
		MakeCase (r, q, ntri, c % 2 == 1);
		if (c % 3 == 0) { q.XB = CollCompose (CollInverse (q.XA), q.XB); q.XA = CollAffine (); }
		INFO ("case " << c);
		if (c == 1) s.stamp = 0xfffffffeu;
		double bd = BruteDist (q.A, q.XA, q.B, q.XB);
		CollHit h;
		double d = CollDistance (q.A, q.XA, q.B, q.XB, 1e9, &h, s);
		REQUIRE (std::fabs (d - bd) <= 1e-12);
		REQUIRE (h.d == d);
		Vector a[3], b[3], pa, pb;
		TriOf (q.A, q.XA, h.ta, a);
		TriOf (q.B, q.XB, h.tb, b);
		REQUIRE (CollTriTriDistance (a, b, pa, pb) == d);
		double cap = bd * r.R (0.5, 1.5) + 1e-3;
		double dc = CollDistance (q.A, q.XA, q.B, q.XB, cap, &h, s);
		REQUIRE (dc == std::min (bd, cap));
		if (bd >= cap) REQUIRE (h.ta == ~0u);

		double within = bd + r.R (0.0, 0.6);
		std::vector<std::pair<uint32_t,uint32_t>> bp = BrutePairs (q.A, q.XA, q.B, q.XB, within);
		std::vector<CollHit> out;
		size_t n = CollPairsWithin (q.A, q.XA, q.B, q.XB, within, out, 1000000, s);
		REQUIRE (n == out.size());
		std::vector<std::pair<uint32_t,uint32_t>> got;
		for (const CollHit &x : out) got.push_back (std::make_pair (x.ta, x.tb));
		REQUIRE (got == bp);
		if (bp.size() > 3) {
			n = CollPairsWithin (q.A, q.XA, q.B, q.XB, within, out, 3, s);
			REQUIRE (n == 3);
			for (const CollHit &x : out) REQUIRE (std::binary_search (bp.begin(), bp.end(), std::make_pair (x.ta, x.tb)));
		}
		REQUIRE (CollOverlap (q.A, q.XA, q.B, q.XB, within, s) == !bp.empty());
		REQUIRE (CollOverlap (q.A, q.XA, q.B, q.XB, bd * 0.999, s) == false);

		uint8_t mA[3] = { 0, 1, 0 }, mB[2] = { 1, 0 };
		double bm = BruteDist (q.A, q.XA, q.B, q.XB, mA, mB);
		REQUIRE (std::fabs (CollDistance (q.A, q.XA, q.B, q.XB, 1e9, &h, s, mA, mB) - bm) <= 1e-12);
		REQUIRE (mA[q.A.tri[h.ta].src] == 0);
		REQUIRE (mB[q.B.tri[h.tb].src] == 0);
		double wm = bm + 0.4;
		bp = BrutePairs (q.A, q.XA, q.B, q.XB, wm, mA, mB);
		CollPairsWithin (q.A, q.XA, q.B, q.XB, wm, out, 1000000, s, mA, mB);
		got.clear();
		for (const CollHit &x : out) got.push_back (std::make_pair (x.ta, x.tb));
		REQUIRE (got == bp);
	}
}

TEST_CASE("Queries vs brute force", "[collgeom][query]")
{
	QueriesVsBrute (12, 120, 7);
}

TEST_CASE("Queries vs brute force, 500 triangles", "[collgeom][query][.slow]")
{
	QueriesVsBrute (16, 500, 8);
}

TEST_CASE("Ray cast and closest point vs brute force", "[collgeom][ray]")
{
	Rng r (31);
	for (int c = 0; c < 8; c++) {
		std::vector<CollGroupData> g = { GridGroup (r, 9, 9, 2.0, 0.6, Vector (0, 0, 0)), SoupGroup (r, 120, 2.0, 0.5, Vector (0, 0, 0)) };
		CollGeom G;
		REQUIRE (BuildGroups (g, G));
		CollAffine X = (c % 2) ? RandAffine (r, 3.0) : RandRigid (r, 3.0);
		if (c == 0) X = CollAffine ();
		for (int k = 0; k < 60; k++) {
			Vector o = CollApply (X, r.V (3.0)), d = r.Dir ();
			d *= r.R (0.5, 2.0);
			double tmin = r.U () < 0.3 ? r.R (0.0, 1.0) : 0.0;
			double tb;
			uint32_t bt;
			bool bh = BruteRay (G, X, o, d, tmin, 10.0, tb, bt);
			CollRayHit h;
			bool gh = CollRayCast (G, X, o, d, tmin, 10.0, h);
			REQUIRE (gh == bh);
			if (!gh) continue;
			REQUIRE (std::fabs (h.t - tb) <= 1e-9);
			REQUIRE ((h.p - (o + d*h.t)).length() < 1e-12);
			if (c == 0) { REQUIRE (std::fabs (h.t - tb) <= 1e-12); REQUIRE (h.tri == bt); }
		}
		uint8_t mask[2] = { 1, 0 };
		for (int k = 0; k < 20; k++) {
			Vector o = r.V (3.0), d = r.Dir ();
			CollRayHit h;
			if (CollRayCast (G, CollAffine (), o, d, 0, 10, h, mask)) REQUIRE (G.tri[h.tri].src == 1);
		}
		for (int k = 0; k < 60; k++) {
			Vector p = r.V (4.0);
			double bd = 1e300;
			for (uint32_t t = 0; t < G.tri.size(); t++) {
				Vector v[3];
				TriOf (G, CollAffine (), t, v);
				bd = std::min (bd, PtTri (p, v));
			}
			uint32_t t;
			Vector q;
			double d = CollClosestPoint (G, p, 1e9, t, q);
			REQUIRE (std::fabs (d - bd) < 1e-12);
			REQUIRE (std::fabs ((q - p).length() - d) < 1e-12);
			Vector v[3];
			TriOf (G, CollAffine (), t, v);
			REQUIRE (PtTri (q, v) < 1e-12);
			double cap = bd * 0.9;
			REQUIRE (CollClosestPoint (G, p, cap, t, q) == cap);
			REQUIRE (t == ~0u);
		}
	}
}

// leaf node ids and their triangle ranges: what D2 keys GRACE scopes and front caches on
bool SameLeaves (const CollGeom &G, const std::vector<CollNode> &node0, const std::vector<uint32_t> &perm0)
{
	if (G.node.size() != node0.size() || G.perm != perm0) return false;
	for (size_t k = 0; k < node0.size(); k++)
		if (G.node[k].first != node0[k].first || G.node[k].count != node0[k].count) return false;
	return true;
}

TEST_CASE("Refit: boxes only, ids and leaves kept, loose tree reported", "[collgeom][refit]")
{
	Rng r (17);
	CollScratch s;
	for (int c = 0; c < 4; c++) {
		QueryCase q;
		MakeCase (r, q, 150, c % 2 == 1);
		uint32_t ver = q.A.version;
		std::vector<CollTri> tri0 = q.A.tri;
		std::vector<CollNode> node0 = q.A.node;
		std::vector<uint32_t> perm0 = q.A.perm;
		for (Vector &v : q.A.vtx) v += r.V (0.05);
		CHECK_FALSE (q.A.Refit ());
		CheckTree (q.A);
		REQUIRE (SameLeaves (q.A, node0, perm0));
		for (const CollTri &T : q.A.tri)
			for (int k = 0; k < 3; k++) REQUIRE ((q.A.vtx[T.v[k]] - q.A.bsCentre).length() <= q.A.bsRadius);
		REQUIRE (std::fabs (CollDistance (q.A, q.XA, q.B, q.XB, 1e9, nullptr, s) - BruteDist (q.A, q.XA, q.B, q.XB)) <= 1e-12);
		// grown past COLL_REBUILD_GROWTH: reported loose, still never rebuilt
		for (Vector &v : q.A.vtx) v = q.A.bsCentre + (v - q.A.bsCentre) * 2.5;
		REQUIRE (q.A.Refit ());
		CheckTree (q.A);
		REQUIRE (SameLeaves (q.A, node0, perm0));
		REQUIRE (q.A.version == ver);
		REQUIRE (q.A.tri.size() == tri0.size());
		REQUIRE (std::memcmp (q.A.tri.data(), tri0.data(), tri0.size() * sizeof (CollTri)) == 0);
		REQUIRE (std::fabs (CollDistance (q.A, q.XA, q.B, q.XB, 1e9, nullptr, s) - BruteDist (q.A, q.XA, q.B, q.XB)) <= 1e-12);
		std::vector<std::pair<uint32_t,uint32_t>> bp = BrutePairs (q.A, q.XA, q.B, q.XB, 0.5);
		std::vector<CollHit> out;
		CollPairsWithin (q.A, q.XA, q.B, q.XB, 0.5, out, 1000000, s);
		REQUIRE (out.size() == bp.size());
		CHECK (q.A.Refit ());
		REQUIRE (SameLeaves (q.A, node0, perm0));
	}
}

TEST_CASE("Refit: a dent on a flat panel keeps every leaf (code review C-A-a 2)", "[collgeom][refit]")
{
	// axis-aligned 2 m x 2 m panel: zero box volume, so the v2.1 volume rule rebuilt on its first dent
	CollGroupData g;
	const int nx = 20;
	for (int j = 0; j <= nx; j++) for (int i = 0; i <= nx; i++) g.vtx.push_back (Vtx (Vector (i * 0.1, 0, j * 0.1)));
	for (int j = 0; j < nx; j++)
		for (int i = 0; i < nx; i++) {
			uint16_t a = (uint16_t)(j*(nx+1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + nx + 1), d = (uint16_t)(c + 1);
			for (uint16_t k : { a, c, b, b, c, d }) g.idx.push_back (k);
		}
	CollGeom G;
	REQUIRE (BuildGroups ({ g }, G));
	std::vector<CollNode> node0 = G.node;
	std::vector<uint32_t> perm0 = G.perm;
	uint32_t ver = G.version;
	G.vtx[200].y = -0.002;    // 2 mm dent in the middle
	CHECK_FALSE (G.Refit ());
	CHECK (SameLeaves (G, node0, perm0));
	CHECK (G.version == ver);
	CHECK (G.node[0].mn[1] == -0.002);
	CheckTree (G);
	// 0.1 m more: still tight enough, leaves kept
	G.vtx[100].y = 0.1;
	CHECK_FALSE (G.Refit ());
	CHECK (SameLeaves (G, node0, perm0));
	CheckTree (G);
}

TEST_CASE("Ray cast: Atlantis bay, doors at rest", "[collgeom][ray][stock]")
{
	CollRestMesh m;
	CollGeom G;
	CollBuildStats st;
	REQUIRE (BuildStock (MSH_ATLANTIS, m, G, st));
	Rng r (3);
	CollAffine X = RandRigid (r, 20.0);
	for (int frame = 0; frame < 2; frame++) {
		CollAffine P = frame ? X : CollAffine ();
		auto hit = [&](const Vector &o, const Vector &d, int axis) {
			CollRayHit h;
			REQUIRE (CollRayCast (G, P, CollApply (P, o), CollApplyDir (P, d), 0.0, 100.0, h));
			return (o + d * h.t).data[axis];
		};
		for (double z : { -6.0, 0.0, 8.0 }) {
			Vector o (0.0, 1.0, z);
			double xp = hit (o, Vector (1, 0, 0), 0), xm = hit (o, Vector (-1, 0, 0), 0);
			double yf = hit (o, Vector (0, -1, 0), 1), yd = hit (o, Vector (0, 1, 0), 1);
			if (!frame) std::printf ("Atlantis bay z = %4.1f: walls %.3f / %.3f, floor %.3f, inner door %.3f\n", z, xm, xp, yf, yd);
			CHECK (std::fabs (xm + 2.74) <= 0.02);
			CHECK (std::fabs (xp - 2.75) <= 0.02);
			CHECK (std::fabs (yf + 1.88) <= 0.02);
			CHECK (std::fabs (yd - 3.43) <= 0.02);
		}
		// R6: forward bulkhead at x = +-1.5..2 (D1's (1.75, 0) hits a group 30 fitting at z = 9.83)
		for (const Vector &o : { Vector (1.5, 0, 2.7), Vector (2.0, 0, 2.7), Vector (-1.5, 0, 2.7), Vector (1.75, 2.0, 2.7) }) {
			double zf = hit (o, Vector (0, 0, 1), 2);
			if (!frame) std::printf ("Atlantis forward bulkhead from (%.2f, %.1f): z = %.3f\n", o.x, o.y, zf);
			CHECK (std::fabs (zf - 12.10) <= 0.02);
		}
	}
}

// central difference of the pose path, motion bound against samples
void PoseCheck (const CollAffine &P0, const CollAffine &P1, const CollGeom &G, int ntau, int npt, Rng &r)
{
	const Vector &c = G.bsCentre;
	CollAffine E0 = CollPoseAt (P0, P1, c, 0.0), E1 = CollPoseAt (P0, P1, c, 1.0);
	for (int i = 0; i < 9; i++) {
		REQUIRE (std::fabs (E0.A.data[i] - P0.A.data[i]) <= 1e-12);
		REQUIRE (std::fabs (E1.A.data[i] - P1.A.data[i]) <= 1e-12);
	}
	REQUIRE ((E0.t - P0.t).length() <= 1e-12);
	REQUIRE ((E1.t - P1.t).length() <= 1e-12);
	CollAffine N0 = CollPoseAt (P0, P1, c, 1e-12), N1 = CollPoseAt (P0, P1, c, 1.0 - 1e-12);
	for (int k = 0; k < 20; k++) {
		Vector x = G.vtx[G.tri[(size_t)(r.U () * G.tri.size())].v[0]];
		REQUIRE ((CollApply (N0, x) - CollApply (P0, x)).length() < 1e-9);
		REQUIRE ((CollApply (N1, x) - CollApply (P1, x)).length() < 1e-9);
		for (double tau : { 0.0, 1e-3, r.U (), 0.5, 1.0 }) {
			double h = 1e-5;
			Vector fd = (CollApply (CollPoseAt (P0, P1, c, tau + h), x) - CollApply (CollPoseAt (P0, P1, c, tau - h), x)) / (2*h);
			Vector v = CollPoseVel (P0, P1, c, tau, x);
			REQUIRE ((fd - v).length() <= 1e-7);
		}
	}
	double bound = CollPoseMotion (G, P0, P1), mx = 0;
	std::vector<Vector> pts;
	for (int k = 0; k < npt; k++) {
		const CollTri &T = G.tri[(size_t)(r.U () * G.tri.size())];
		double u = r.U (), v = r.U ();
		if (u + v > 1) u = 1 - u, v = 1 - v;
		pts.push_back (k < npt/2 ? G.vtx[T.v[k % 3]] : G.vtx[T.v[0]] + (G.vtx[T.v[1]] - G.vtx[T.v[0]])*u + (G.vtx[T.v[2]] - G.vtx[T.v[0]])*v);
	}
	for (int i = 0; i <= ntau; i++) {
		CollAffine P = CollPoseAt (P0, P1, c, (double)i / ntau);
		for (const Vector &x : pts) mx = std::max (mx, (CollApply (P, x) - CollApply (P0, x)).length());
	}
	REQUIRE (bound >= mx);
	REQUIRE (bound <= 4.0 * mx + 1e-9);
}

void PoseCases (int ntau, int npt, uint64_t seed)
{
	Rng r (seed);
	std::vector<CollGroupData> g = { GridGroup (r, 8, 8, 3.0, 1.0, Vector (5, 1, -2)), SoupGroup (r, 80, 3.0, 0.8, Vector (5, 1, -2)) };
	CollGeom G;
	REQUIRE (BuildGroups (g, G));
	for (int c = 0; c < 10; c++) {
		INFO ("pose case " << c);
		CollAffine P0 = RandRigid (r, 10.0), P1 = RandRigid (r, 10.0);
		Vector ax = r.Dir ();
		if (c == 1) P1.A = AxisRot (ax, r.R (0.01, 0.2)) * P0.A;
		if (c == 2) P1.A = AxisRot (ax, Pi - 1e-6) * P0.A;
		if (c == 3) { P1.A = P0.A; P1.t = P0.t + r.V (0.1); }
		if (c == 4) { P1.A = AxisRot (ax, 1e-9) * P0.A; P1.t = P0.t; }
		if (c == 5) { for (int i = 0; i < 9; i++) P1.A.data[i] += 1e-11 * r.R (-1, 1); }
		REQUIRE (CollIsRigid (P0));
		REQUIRE (CollIsRigid (P1));
		PoseCheck (P0, P1, G, ntau, npt, r);
	}
	for (int c = 0; c < 6; c++) {
		INFO ("affine case " << c);
		CollAffine P0 = c < 2 ? RandRigid (r, 10.0) : RandAffine (r, 10.0), P1 = RandAffine (r, 10.0);
		if (c == 4) { P1 = P0; P1.A = P0.A * 1.001; }
		REQUIRE_FALSE (CollIsRigid (P1));
		PoseCheck (P0, P1, G, ntau, npt, r);
	}
	CollAffine P = RandRigid (r, 10.0);
	REQUIRE (CollPoseMotion (G, P, P) == 0.0);
	Vector v = CollPoseVel (P, P, G.bsCentre, 0.3, G.vtx[0]);
	REQUIRE (v.length() == 0.0);
	CollAffine M;
	M.A = Matrix (1, 0, 0, 0, 1, 0, 0, 0, -1);
	REQUIRE_FALSE (CollIsRigid (M));
	M.A = Matrix (1, 0, 0, 0, 1, 0, 0, 0, 1.0 + 2e-9);
	REQUIRE_FALSE (CollIsRigid (M));
}

TEST_CASE("Pose path, velocity and motion bound", "[collgeom][pose]")
{
	PoseCases (100, 100, 41);
}

TEST_CASE("Pose path: 1,000 tau x 1,000 points", "[collgeom][pose][.slow]")
{
	PoseCases (1000, 1000, 42);
}

TEST_CASE("Budget: build time and memory", "[collgeom][budget][stock][.slow]")
{
	auto bytesPerTri = [](const CollGeom &G) {
		size_t b = G.tri.capacity()*sizeof (CollTri) + G.perm.capacity()*4 + G.node.capacity()*sizeof (CollNode) + G.vtx.capacity()*sizeof (Vector)
			+ G.refOfs.capacity()*4 + G.ref.capacity()*sizeof (CollRef) + G.srcTab.capacity()*sizeof (CollSrc);
		return (double)b / G.tri.size();
	};
	const char *meshes[] = { MSH_ISS, MSH_ATLANTIS, MSH_DGNS, MSH_SHUTTLEA, MSH_TANK, MSH_SRB, MSH_MMU, "Meshes/KLC39B.msh", "Meshes/Vab.msh" };
	double issMs = 0;
	for (const char *p : meshes) {
		CollRestMesh m;
		CollGeom G;
		CollBuildStats st;
		REQUIRE (BuildStock (p, m, G, st));
		double ms = 1e300;
		for (int k = 0; k < 3; k++) { REQUIRE (BuildStock (p, m, G, st)); ms = std::min (ms, st.msWeld + st.msTree); }
		double bpt = bytesPerTri (G);
		std::printf ("%-18s tris %6u -> %6u (degen %u, dup %u), vtx %6u -> %6u, nodes %6u, weld+filters %6.2f ms, tree %6.2f ms, %5.1f B/tri\n",
			m.name.c_str(), st.ntriIn, st.ntri, st.dropDegen, st.dropDup, st.nrender, st.nweld, st.nnode, st.msWeld, st.msTree, bpt);
		CHECK (bpt <= 110.0);
		if (p == MSH_ISS) issMs = ms;
	}
	if (issMs >= 200.0) WARN ("ISS build " << issMs << " ms (soft budget 200 ms)");

	// 9 x ISS in one geometry, add-on size (D1 1.2: <= 0.5 s for 220k triangles)
	CollRestMesh iss;
	REQUIRE (LoadStock (MSH_ISS, iss));
	std::vector<CollGroupData> nine;
	for (int i = 0; i < 9; i++)
		for (const CollGroupData &g : iss.grp) {
			CollGroupData c = g;
			for (CollVtx &v : c.vtx) v.x += (float)(130.0*(i%3)), v.z += (float)(90.0*(i/3));
			nine.push_back (c);
		}
	CollGeom N;
	CollBuildStats st;
	REQUIRE (BuildGroups (nine, N, &st));
	std::printf ("9 x ISS            tris %6u -> %6u, vtx %6u -> %6u, nodes %6u, weld+filters %6.2f ms, tree %6.2f ms, %5.1f B/tri\n",
		st.ntriIn, st.ntri, st.nrender, st.nweld, st.nnode, st.msWeld, st.msTree, bytesPerTri (N));
	CHECK (bytesPerTri (N) <= 110.0);
	if (st.msWeld + st.msTree >= 500.0) WARN ("9 x ISS build " << st.msWeld + st.msTree << " ms (soft budget 500 ms)");
	auto t0 = std::chrono::steady_clock::now();
	N.Refit ();
	std::printf ("9 x ISS refit %.2f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
}
