// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: exported nodes and channels against the client's animation code (design 10)

#include <catch2/catch_test_macros.hpp>
#include "AnimModel.h"
#include "GxEval.h"
#include "OrbiterRef.h"
#include <random>

namespace {

struct Case {
	GxSnapshot snap;
	std::vector<std::vector<std::vector<GxVec3>>> rest; // [mesh][group][vertex]
};

GxVec3 RndVec (std::mt19937 &r, double a)
{
	std::uniform_real_distribution<double> u (-a, a);
	return {u (r), u (r), u (r)};
}

GxVec3 RndUnit (std::mt19937 &r)
{
	GxVec3 v;
	double l;
	do { v = RndVec (r, 1); l = std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z); } while (l < 0.1);
	return {v.x / l, v.y / l, v.z / l};
}

void AddMesh (Case &k, int ngrp, const GxVec3 &ofs, std::mt19937 &r)
{
	GxMesh m;
	m.index = (int)k.snap.meshes.size ();
	m.ofs = ofs;
	m.vis = 1;
	std::vector<std::vector<GxVec3>> rv;
	for (int g = 0; g < ngrp; g++) {
		GxGroup G;
		std::vector<GxVec3> pts;
		for (int i = 0; i < 4; i++) {
			GxVec3 p = RndVec (r, 5);
			GxVertex v{{(float)p.x, (float)p.y, (float)p.z}, {0, 1, 0}, {0, 0}};
			G.vtx.push_back (v);
			pts.push_back ({v.p[0], v.p[1], v.p[2]});
		}
		G.idx = {0, 1, 2, 0, 2, 3};
		m.groups.push_back (G);
		rv.push_back (pts);
	}
	k.snap.meshes.push_back (m);
	k.rest.push_back (rv);
}

int AddAnim (Case &k, double def)
{
	GxAnim a;
	a.defstate = a.state = def;
	k.snap.anims.anims.push_back (a);
	return (int)k.snap.anims.anims.size () - 1;
}

int AddComp (Case &k, int an, GxComp c)
{
	GxAnimSet &s = k.snap.anims;
	c.anim = an;
	c.index = (int)s.anims[an].comps.size ();
	int id = (int)s.comps.size ();
	s.comps.push_back (c);
	s.anims[an].comps.push_back (id);
	if (c.parent >= 0) s.comps[c.parent].children.push_back (id);
	return id;
}

GxComp RndComp (std::mt19937 &r, int mesh, std::vector<int> groups, int parent)
{
	std::uniform_int_distribution<int> type (0, 2);
	std::uniform_real_distribution<double> u (0, 1);
	GxComp c;
	c.mesh = mesh;
	c.groups = groups;
	c.parent = parent;
	double a = u (r), b = u (r);
	if (u (r) < 0.4) { a = 0; b = 1; }
	if (std::fabs (a - b) < 0.05) b = a + 0.3;
	c.state0 = std::min (a, b); c.state1 = std::max (a, b);
	switch (type (r)) {
	case 0: c.type = GxType::Rotate; c.ref = RndVec (r, 4); c.axis = RndUnit (r); c.angle = (u (r) * 2 - 1) * 7.0; break;
	case 1: c.type = GxType::Translate; c.shift = RndVec (r, 2); break;
	default: { c.type = GxType::Scale; c.ref = RndVec (r, 4); double k = 0.4 + u (r) * 1.5; c.scale = {k, k, k}; } break; // uniform: Orbiter keeps anisotropic scale axes fixed under a rotation (design review R15)
	}
	return c;
}

double Lo (const GxAnimSet &s, int a)
{
	double lo = 0;
	for (int c : s.anims[a].comps) lo = std::min (lo, std::min (s.comps[c].state0, s.comps[c].state1));
	return lo;
}

// max distance between the reference after the history and the exported nodes at the final states
double Compare (const Case &k, const std::vector<std::pair<int, double>> &history, bool recover)
{
	RefVessel ref;
	ref.live = k.snap.anims;
	ref.v = k.rest;
	ref.Start ();
	for (auto &h : history) ref.Animate (h.first, h.second);

	GxSnapshot snap = k.snap;
	if (recover) { // the export sees the moved parameters and the states
		snap.anims = ref.live;
		GxRecoverRest (snap.anims);
	}
	for (size_t a = 0; a < snap.anims.anims.size (); a++) snap.anims.anims[a].state = ref.live.anims[a].state;

	GxModel m;
	std::vector<std::string> warn;
	GxBuildModel (snap, {}, m, warn);
	std::map<int, double> times;
	for (size_t a = 0; a < snap.anims.anims.size (); a++) times[(int)a] = ref.live.anims[a].state - Lo (snap.anims, (int)a);
	auto W = EvWorld (m, times);

	double worst = 0;
	int seen = 0;
	for (size_t i = 0; i < m.nodes.size (); i++) {
		const GxNode &n = m.nodes[i];
		if (n.group < 0) continue;
		seen++;
		const GxMesh &M = k.snap.meshes[n.mesh];
		for (size_t v = 0; v < k.rest[n.mesh][n.group].size (); v++) {
			const GxVec3 &p0 = k.rest[n.mesh][n.group][v];
			GxVec3 g = EvApply (W[i], GxToGltf ({p0.x + M.ofs.x, p0.y + M.ofs.y, p0.z + M.ofs.z}));
			const GxVec3 &q = ref.v[n.mesh][n.group][v];
			GxVec3 e = GxToGltf ({q.x + M.ofs.x, q.y + M.ofs.y, q.z + M.ofs.z});
			worst = std::max (worst, std::sqrt ((g.x - e.x) * (g.x - e.x) + (g.y - e.y) * (g.y - e.y) + (g.z - e.z) * (g.z - e.z)));
		}
	}
	REQUIRE (seen > 0);
	return worst;
}

std::vector<std::pair<int, double>> RndHistory (std::mt19937 &r, int nanim, int steps)
{
	std::uniform_int_distribution<int> an (0, nanim - 1);
	std::uniform_real_distribution<double> u (-0.1, 1.1);
	std::vector<std::pair<int, double>> h;
	for (int i = 0; i < steps; i++) h.push_back ({an (r), u (r)});
	return h;
}

// one listing per group: a random hierarchy across animations
Case HierCase (std::mt19937 &r)
{
	Case k;
	AddMesh (k, 8, {0, 0, 0}, r);
	AddMesh (k, 6, {1.5, -0.5, 2.0}, r);
	std::uniform_real_distribution<double> u (0, 1);
	int na = 1 + (int)(u (r) * 4);
	for (int a = 0; a < na; a++) { double d = u (r); AddAnim (k, d < 0.3 ? 0 : d < 0.6 ? 1 : u (r)); }
	std::vector<std::vector<int>> free (2);
	for (int g = 0; g < 8; g++) free[0].push_back (g);
	for (int g = 0; g < 6; g++) free[1].push_back (g);
	std::shuffle (free[0].begin (), free[0].end (), r);
	std::shuffle (free[1].begin (), free[1].end (), r);
	int nc = 3 + (int)(u (r) * 6);
	for (int i = 0; i < nc; i++) {
		int an = (int)(u (r) * na);
		int mesh = u (r) < 0.6 ? 0 : 1;
		std::vector<int> grp;
		int take = (int)(u (r) * 3);
		while (take-- > 0 && !free[mesh].empty ()) { grp.push_back (free[mesh].back ()); free[mesh].pop_back (); }
		int parent = -1;
		if (!k.snap.anims.comps.empty () && u (r) < 0.6) parent = (int)(u (r) * k.snap.anims.comps.size ());
		GxComp c = RndComp (r, u (r) < 0.1 ? -1 : mesh, u (r) < 0.1 ? std::vector<int>{} : grp, parent);
		if (c.mesh < 0) c.groups.clear ();
		AddComp (k, an, c);
	}
	return k;
}

}

TEST_CASE ("Rest pose: every exported vertex where the mesh has it", "[AnimModel]")
{
	std::mt19937 r (7);
	for (int t = 0; t < 50; t++) {
		Case k = HierCase (r);
		CHECK (Compare (k, {}, false) < 1e-5); // float channel values
	}
}

TEST_CASE ("Hierarchies across animations against the client, random histories", "[AnimModel]")
{
	std::mt19937 r (11);
	double worst = 0;
	for (int t = 0; t < 400; t++) {
		Case k = HierCase (r);
		auto h = RndHistory (r, (int)k.snap.anims.anims.size (), 1 + t % 12);
		double e = Compare (k, h, false);
		worst = std::max (worst, e);
		INFO ("case " << t);
		REQUIRE (e < 1e-5);
	}
	WARN ("worst " << worst);
}

TEST_CASE ("Rest parameters recovered from a visual's moved ones", "[AnimModel]")
{
	std::mt19937 r (13);
	for (int t = 0; t < 400; t++) {
		Case k = HierCase (r);
		auto h = RndHistory (r, (int)k.snap.anims.anims.size (), 1 + t % 12);
		RefVessel ref;
		ref.live = k.snap.anims;
		ref.v = k.rest;
		ref.Start ();
		for (auto &s : h) ref.Animate (s.first, s.second);
		GxAnimSet got = ref.live;
		GxRecoverRest (got);
		for (size_t c = 0; c < got.comps.size (); c++) {
			const GxComp &a = got.comps[c], &b = k.snap.anims.comps[c];
			INFO ("case " << t << " comp " << c);
			auto d = [](const GxVec3 &x, const GxVec3 &y) { return std::sqrt ((x.x - y.x) * (x.x - y.x) + (x.y - y.y) * (x.y - y.y) + (x.z - y.z) * (x.z - y.z)); };
			if (a.type == GxType::Rotate) { REQUIRE (d (a.ref, b.ref) < 1e-6); REQUIRE (d (a.axis, b.axis) < 1e-6); }
			if (a.type == GxType::Translate) REQUIRE (d (a.shift, b.shift) < 1e-6);
			if (a.type == GxType::Scale) REQUIRE (d (a.ref, b.ref) < 1e-6);
		}
		REQUIRE (Compare (k, h, true) < 1e-5);
	}
}

TEST_CASE ("One group in two parts of one animation, disjoint ranges (the DG's gear covers)", "[AnimModel]")
{
	std::mt19937 r (17);
	for (int t = 0; t < 300; t++) {
		Case k;
		AddMesh (k, 3, {0.5, 0, -1}, r);
		std::uniform_real_distribution<double> u (0, 1);
		int a = AddAnim (k, t % 2 ? 1.0 : 0.0);
		GxComp c1 = RndComp (r, 0, {0, 1}, -1), c2 = RndComp (r, 0, {1, 2}, -1);
		c1.state0 = 0; c1.state1 = 0.3 + 0.2 * u (r);
		c2.state0 = 0.6 + 0.1 * u (r); c2.state1 = 1;
		AddComp (k, a, c1);
		AddComp (k, a, c2);
		auto h = RndHistory (r, 1, 1 + t % 10);
		INFO ("case " << t);
		REQUIRE (Compare (k, h, false) < 1e-5);
	}
}

TEST_CASE ("Turns over 180 degrees stay on the path between keys", "[AnimModel]")
{
	std::mt19937 r (19);
	Case k;
	AddMesh (k, 1, {0, 0, 0}, r);
	int a = AddAnim (k, 0);
	GxComp c;
	c.type = GxType::Rotate; c.mesh = 0; c.groups = {0}; c.ref = {1, 2, 3}; c.axis = {0, 0.6, 0.8}; c.angle = 7 * 3.14159265358979;
	c.state0 = 0; c.state1 = 1;
	AddComp (k, a, c);
	for (int i = 0; i <= 40; i++) REQUIRE (Compare (k, {{0, i / 40.0}}, false) < 1e-5);
}

TEST_CASE ("Whole-mesh parts and vertex-list parents", "[AnimModel]")
{
	std::mt19937 r (23);
	Case k;
	AddMesh (k, 4, {0, 1, 0}, r);
	int a0 = AddAnim (k, 0), a1 = AddAnim (k, 0.5);
	GxComp list = RndComp (r, -1, {}, -1);
	list.type = GxType::Rotate; list.ref = {0, 0, 1}; list.axis = {1, 0, 0}; list.angle = 1.2;
	int pl = AddComp (k, a0, list);
	GxComp child = RndComp (r, 0, {2, 3}, pl);
	AddComp (k, a1, child);
	AddMesh (k, 3, {2, 0, 1}, r);
	GxComp whole = RndComp (r, 1, {}, -1); // a second mesh: one listing per group, so the client's result has no order to depend on
	whole.allGroups = true; whole.type = GxType::Translate; whole.shift = {0, 0, 3};
	AddComp (k, a1, whole);
	for (int t = 0; t < 50; t++) REQUIRE (Compare (k, RndHistory (r, 2, 1 + t % 8), false) < 1e-5);
}

TEST_CASE ("Channels: keys at the range ends and the part's own ends", "[AnimModel]")
{
	Case k;
	std::mt19937 r (29);
	AddMesh (k, 1, {0, 0, 0}, r);
	int a = AddAnim (k, 1);
	GxComp c;
	c.type = GxType::Translate; c.mesh = 0; c.groups = {0}; c.shift = {0, 0, 2}; c.state0 = 0.25; c.state1 = 0.75;
	AddComp (k, a, c);
	GxModel m;
	std::vector<std::string> w;
	GxBuildModel (k.snap, {}, m, w);
	REQUIRE (m.anims.size () == 1);
	REQUIRE (m.anims[0].channels.size () == 1);
	const GxChannel &ch = m.anims[0].channels[0];
	REQUIRE (ch.times == std::vector<float>{0, 0.25f, 0.75f, 1});
	CHECK (ch.values[2] == 2.0f); // state 0 is 2 m back (Orbiter -z) from the rest at defstate 1: glTF +z
	CHECK (ch.values[11] == 0.0f);
}

TEST_CASE ("Uneven scales: exact at the root, a warning in a hierarchy with a rotation", "[AnimModel]")
{
	std::mt19937 r (31);
	Case k;
	AddMesh (k, 3, {0, 0, 0}, r);
	int a0 = AddAnim (k, 0), a1 = AddAnim (k, 1);
	GxComp s; s.type = GxType::Scale; s.mesh = 0; s.groups = {0}; s.ref = {1, 0, 2}; s.scale = {2, 0.5, 1.5};
	int sc = AddComp (k, a0, s);
	GxComp rt; rt.type = GxType::Rotate; rt.mesh = 0; rt.groups = {1}; rt.ref = {0, 1, 0}; rt.axis = {0, 0, 1}; rt.angle = 2;
	AddComp (k, a1, rt);
	for (int t = 0; t < 40; t++) REQUIRE (Compare (k, RndHistory (r, 2, 1 + t % 6), false) < 1e-5);
	GxModel m;
	std::vector<std::string> w;
	GxBuildModel (k.snap, {}, m, w);
	CHECK (w.empty ());
	GxComp child = rt; child.groups = {2}; child.parent = sc;
	AddComp (k, a1, child);
	w.clear ();
	GxBuildModel (k.snap, {}, m, w);
	REQUIRE (w.size () == 1);
	CHECK (w[0].find ("rotation under an uneven scale") != std::string::npos);
}

TEST_CASE ("Parts listed out of their range order follow the ranges in gradual motion (the DG's radiator)", "[AnimModel]")
{
	std::mt19937 r (37);
	for (int t = 0; t < 200; t++) {
		Case k;
		AddMesh (k, 2, {0, 0.5, 0}, r);
		bool up = t % 2 == 0;
		int a = AddAnim (k, up ? 0.0 : 1.0);
		GxComp late = RndComp (r, 0, {0, 1}, -1), early = RndComp (r, 0, {1}, -1);
		late.state0 = 0.6; late.state1 = 1; early.state0 = 0; early.state1 = 0.4;
		AddComp (k, a, late); // listed first, moves last when rising
		AddComp (k, a, early);
		std::uniform_real_distribution<double> u (0, 1);
		double target = u (r), s = up ? 0.0 : 1.0;
		std::vector<std::pair<int, double>> h;
		for (int i = 0; i < 40; i++) { s += (target - s) * 0.15; h.push_back ({0, s}); }
		h.push_back ({0, target});
		INFO ("case " << t);
		REQUIRE (Compare (k, h, false) < 1e-4);
	}
}
