// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: files from the exporter read back by the core's round trip (claude/design-gltf-roundtrip.md), against the client's animation code

#include <catch2/catch_test_macros.hpp>
#include "AnimModel.h"
#include "GlbWriter.h"
#include "GltfRoundTrip.h"
#include "GxEval.h"
#include "OrbiterRef.h"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

namespace fs = std::filesystem;

namespace {

struct Case { GxSnapshot snap; };

EvMat EvIdent () { EvMat m{}; for (int i = 0; i < 4; i++) m.m[i][i] = 1; return m; }

EvMat EvInverse (const EvMat &a) // affine
{
	const double (*m)[4] = a.m;
	double d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	EvMat r = EvIdent ();
	r.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / d; r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / d; r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / d;
	r.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / d; r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / d; r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / d;
	r.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / d; r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / d; r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / d;
	for (int i = 0; i < 3; i++) r.m[i][3] = -(r.m[i][0] * m[0][3] + r.m[i][1] * m[1][3] + r.m[i][2] * m[2][3]);
	return r;
}

GxVec3 RndVec (std::mt19937 &r, double a) { std::uniform_real_distribution<double> u (-a, a); return {u (r), u (r), u (r)}; }
GxVec3 RndUnit (std::mt19937 &r)
{
	GxVec3 v; double l;
	do { v = RndVec (r, 1); l = std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z); } while (l < 0.1);
	return {v.x / l, v.y / l, v.z / l};
}

void AddMesh (Case &k, int ngrp, const GxVec3 &ofs, int vis, std::mt19937 &r)
{
	GxMesh m;
	m.index = (int)k.snap.meshes.size ();
	m.name = "rt\\mesh" + std::to_string (m.index);
	m.ofs = ofs;
	m.vis = vis;
	std::uniform_real_distribution<double> u (0, 1);
	for (int g = 0; g < ngrp; g++) {
		GxGroup G;
		GxVec3 c = RndVec (r, 4);
		for (int i = 0; i < 4; i++) {
			GxVec3 p = RndVec (r, 1);
			G.vtx.push_back (GxVertex{{(float)(c.x + p.x), (float)(c.y + p.y), (float)(c.z + p.z)}, {0, 1, 0}, {(float)u (r), (float)u (r)}});
		}
		G.idx = {0, 1, 2, 0, 2, 3};
		m.groups.push_back (G);
	}
	k.snap.meshes.push_back (m);
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

// two meshes, three animations; a group is listed by one component (a second listing makes Orbiter's result depend on list order), parents anywhere earlier
Case Random (unsigned seed)
{
	std::mt19937 r (seed);
	std::uniform_real_distribution<double> u (0, 1);
	Case k;
	AddMesh (k, 6, RndVec (r, 2), 1, r);
	AddMesh (k, 4, {0, 0, 0}, 4, r);
	const double defs[3] = {0, 1, 0.5};
	std::mt19937 rs (seed * 31 + 7); // the states the sim had: the file's static pose (design-gltf-simpose-glow 1)
	std::uniform_real_distribution<double> us (-0.1, 1.1);
	for (int a = 0; a < 3; a++) {
		GxAnim A;
		A.defstate = defs[(seed + a) % 3];
		A.state = (seed + a) % 4 ? us (rs) : A.defstate;
		k.snap.anims.anims.push_back (A);
	}
	std::vector<std::vector<int>> free (2);
	for (int m = 0; m < 2; m++) for (int g = 0; g < (int)k.snap.meshes[m].groups.size (); g++) free[m].push_back (g);
	for (int a = 0; a < 3; a++) {
		for (int n = 0; n < 3; n++) {
			int m = u (r) < 0.7 ? 0 : 1;
			if (free[m].empty ()) continue;
			std::shuffle (free[m].begin (), free[m].end (), r);
			int ng = std::min ((int)free[m].size (), 1 + (int)(u (r) * 2));
			GxComp c;
			c.mesh = m;
			c.groups.assign (free[m].begin (), free[m].begin () + ng);
			free[m].erase (free[m].begin (), free[m].begin () + ng);
			if (!k.snap.anims.comps.empty () && u (r) < 0.4) c.parent = (int)(u (r) * k.snap.anims.comps.size ());
			double s0 = u (r), s1 = u (r);
			if (u (r) < 0.4) { s0 = 0; s1 = 1; }
			if (std::fabs (s0 - s1) < 0.1) s1 = std::min (1.0, s0 + 0.3), s0 = s1 - 0.3;
			c.state0 = std::min (s0, s1); c.state1 = std::max (s0, s1);
			double t = u (r);
			if (t < 0.6) { c.type = GxType::Rotate; c.ref = RndVec (r, 3); c.axis = RndUnit (r); c.angle = (u (r) * 2 - 1) * 4.0; }
			else if (t < 0.85) { c.type = GxType::Translate; c.shift = RndVec (r, 1.5); }
			else { c.type = GxType::Scale; c.ref = RndVec (r, 3); double s = 0.5 + u (r); c.scale = {s, s, s}; }
			AddComp (k, a, c);
		}
	}
	return k;
}

std::vector<uint8_t> Export (const Case &k)
{
	GxModel m;
	std::vector<std::string> warn;
	GxBuildModel (k.snap, {}, m, warn);
	GxGlbInput in;
	in.snap = &k.snap;
	in.model = &m;
	std::vector<uint8_t> out;
	std::string err;
	REQUIRE (GxWriteGlb (in, out, err));
	return out;
}

struct Parts { QJsonObject json; std::vector<uint8_t> bin; };

uint32_t U32 (const uint8_t *p) { uint32_t v; memcpy (&v, p, 4); return v; }

Parts Split (const std::vector<uint8_t> &b)
{
	Parts p;
	uint32_t jl = U32 (&b[12]);
	p.json = QJsonDocument::fromJson (QByteArray ((const char *)&b[20], (int)jl)).object ();
	size_t o = 20 + jl;
	if (o + 8 <= b.size ()) p.bin.assign (b.begin () + o + 8, b.begin () + o + 8 + U32 (&b[o]));
	return p;
}

std::vector<uint8_t> Join (Parts p)
{
	while (p.bin.size () % 4) p.bin.push_back (0);
	QJsonArray bufs{QJsonObject{{"byteLength", (qint64)p.bin.size ()}}};
	p.json["buffers"] = bufs;
	QByteArray j = QJsonDocument (p.json).toJson (QJsonDocument::Compact);
	while (j.size () % 4) j.append (' ');
	std::vector<uint8_t> out;
	auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) out.push_back ((uint8_t)(v >> (8 * i))); };
	u32 (0x46546C67); u32 (2); u32 ((uint32_t)(12 + 8 + j.size () + 8 + p.bin.size ()));
	u32 ((uint32_t)j.size ()); u32 (0x4E4F534A);
	out.insert (out.end (), j.begin (), j.end ());
	u32 ((uint32_t)p.bin.size ()); u32 (0x004E4942);
	out.insert (out.end (), p.bin.begin (), p.bin.end ());
	return out;
}

std::vector<float> Floats (const Parts &p, int acc)
{
	QJsonObject a = p.json["accessors"].toArray ()[acc].toObject ();
	QJsonObject v = p.json["bufferViews"].toArray ()[a["bufferView"].toInt ()].toObject ();
	int n = a["count"].toInt () * (a["type"].toString () == "VEC4" ? 4 : a["type"].toString () == "VEC3" ? 3 : 1);
	std::vector<float> f (n);
	memcpy (f.data (), &p.bin[v["byteOffset"].toInt () + a["byteOffset"].toInt (0)], n * 4);
	return f;
}

int AddFloats (Parts &p, const std::vector<float> &f, const char *type)
{
	while (p.bin.size () % 4) p.bin.push_back (0);
	QJsonArray views = p.json["bufferViews"].toArray (), accs = p.json["accessors"].toArray ();
	views.append (QJsonObject{{"buffer", 0}, {"byteOffset", (qint64)p.bin.size ()}, {"byteLength", (qint64)f.size () * 4}});
	const uint8_t *b = (const uint8_t *)f.data ();
	p.bin.insert (p.bin.end (), b, b + f.size () * 4);
	int comps = !strcmp (type, "VEC4") ? 4 : !strcmp (type, "VEC3") ? 3 : 1;
	QJsonObject a{{"bufferView", views.size () - 1}, {"componentType", 5126}, {"count", (int)(f.size () / comps)}, {"type", type}};
	if (comps == 1) { a["min"] = QJsonArray{*std::min_element (f.begin (), f.end ())}; a["max"] = QJsonArray{*std::max_element (f.begin (), f.end ())}; }
	accs.append (a);
	p.json["bufferViews"] = views;
	p.json["accessors"] = accs;
	return accs.size () - 1;
}

// what Blender writes: every channel sampled once per frame (24 per second), each component interpolated on its own, quaternions normalised
void Blenderise (Parts &p)
{
	QJsonArray anims = p.json["animations"].toArray ();
	for (int ai = 0; ai < anims.size (); ai++) {
		QJsonObject a = anims[ai].toObject ();
		QJsonArray samplers = a["samplers"].toArray ();
		for (int si = 0; si < samplers.size (); si++) {
			QJsonObject s = samplers[si].toObject ();
			std::vector<float> in = Floats (p, s["input"].toInt ()), out = Floats (p, s["output"].toInt ());
			int c = (int)(out.size () / in.size ());
			std::vector<float> t2, o2;
			int f1 = (int)std::floor (in.back () * 24 + 1e-6);
			for (int f = 0; f <= f1; f++) {
				double t = f / 24.0;
				size_t k = std::upper_bound (in.begin (), in.end (), (float)t) - in.begin ();
				size_t i0 = k ? k - 1 : 0, i1 = std::min (k, in.size () - 1);
				double w = (i1 == i0) ? 0 : (t - in[i0]) / (in[i1] - in[i0]);
				double v[4], l = 0;
				for (int j = 0; j < c; j++) { v[j] = out[i0 * c + j] + (out[i1 * c + j] - out[i0 * c + j]) * w; l += v[j] * v[j]; }
				if (c == 4) for (int j = 0; j < 4; j++) v[j] /= std::sqrt (l);
				t2.push_back ((float)t);
				for (int j = 0; j < c; j++) o2.push_back ((float)v[j]);
			}
			s["input"] = AddFloats (p, t2, "SCALAR");
			s["output"] = AddFloats (p, o2, c == 4 ? "VEC4" : "VEC3");
			samplers[si] = s;
		}
		a["samplers"] = samplers;
		anims[ai] = a;
	}
	p.json["animations"] = anims;
}

std::string Save (const std::string &name, const std::vector<uint8_t> &b)
{
	fs::path d = fs::temp_directory_path () / "gltfx_rt_test";
	fs::create_directories (d);
	std::string p = (d / name).string ();
	std::ofstream (p, std::ios::binary).write ((const char *)b.data (), b.size ());
	return p;
}

RtFile Load (const std::vector<uint8_t> &b, const std::string &name)
{
	RtFile f;
	std::string err;
	bool ok = RtLoad (Save (name, b), f, err);
	INFO (err);
	REQUIRE (ok);
	return f;
}

// the file's motion: world(states) world(default)^-1 on each output's default vertices, Orbiter frame, mesh local
std::map<std::pair<int, int>, std::vector<GxVec3>> FileVerts (const RtFile &f, const std::vector<double> &states)
{
	std::vector<double> def;
	for (const RtRecAnim &a : f.rec.anims) def.push_back (a.defstate);
	auto world = [&](int node, const std::vector<double> &st) {
		std::vector<int> up;
		for (int n = node; n >= 0; n = f.scene.nodes[n].parent) up.push_back (n);
		EvMat w = EvIdent ();
		for (auto it = up.rbegin (); it != up.rend (); ++it) {
			std::vector<double> m;
			RtLocalAt (f, *it, st, m);
			EvMat l{};
			for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) l.m[rr][c] = m[c * 4 + rr];
			w = EvMul (w, l);
		}
		return w;
	};
	std::map<std::pair<int, int>, std::vector<GxVec3>> out;
	for (size_t e = 0; e < f.plans.size (); e++) {
		const RtVec &o = f.rec.meshes[e].ofs;
		for (const RtOut &x : f.plans[e].outs) {
			if (x.group < 0) continue;
			EvMat a = world (x.node, states), b = world (x.node, def);
			EvMat m = EvMul (a, EvInverse (b));
			std::vector<GxVec3> v;
			for (const NTVERTEX &p : x.vtx) {
				GxVec3 g = EvApply (m, {p.x + o.x, p.y + o.y, -(p.z + o.z)});
				v.push_back ({g.x - o.x, g.y - o.y, -g.z - o.z});
			}
			out[{f.rec.meshes[e].index, x.group}] = v;
		}
	}
	return out;
}

// the vessel after the round trip: the program's components outside replaced units, then the file's
GxAnimSet Effective (const Case &k, const RtFile &f)
{
	GxAnimSet s;
	for (const GxAnim &a : k.snap.anims.anims) { GxAnim b; b.defstate = b.state = a.defstate; s.anims.push_back (b); }
	auto replaced = [&](int a, int mesh) {
		for (size_t e = 0; e < f.rec.meshes.size (); e++)
			if (f.rec.meshes[e].index == mesh) return std::binary_search (f.plans[e].replaced.begin (), f.plans[e].replaced.end (), a);
		return false;
	};
	std::map<int, int> id;
	for (size_t c = 0; c < k.snap.anims.comps.size (); c++) {
		const GxComp &x = k.snap.anims.comps[c];
		if (replaced (x.anim, x.mesh)) continue;
		GxComp y = x;
		y.children.clear ();
		if (x.parent >= 0) { REQUIRE (id.count (x.parent)); y.parent = id[x.parent]; }
		id[(int)c] = (int)s.comps.size ();
		s.comps.push_back (y);
	}
	for (size_t e = 0; e < f.plans.size (); e++) {
		int base = (int)s.comps.size ();
		for (const RtCompSpec &c : f.plans[e].comps) {
			GxComp y;
			y.anim = c.anim;
			y.mesh = f.rec.meshes[e].index;
			for (int g : c.groups) { REQUIRE (g >= 0); y.groups.push_back (g); }
			y.state0 = c.state0; y.state1 = c.state1;
			y.type = c.type == RT_ROTATE ? GxType::Rotate : c.type == RT_TRANSLATE ? GxType::Translate : GxType::Scale;
			y.ref = {c.ref.x, c.ref.y, c.ref.z}; y.axis = {c.axis.x, c.axis.y, c.axis.z}; y.angle = c.angle;
			y.shift = {c.shift.x, c.shift.y, c.shift.z}; y.scale = {c.scale.x, c.scale.y, c.scale.z};
			y.parent = c.parent >= 0 ? base + c.parent : -1;
			s.comps.push_back (y);
		}
	}
	for (size_t c = 0; c < s.comps.size (); c++) {
		s.anims[s.comps[c].anim].comps.push_back ((int)c);
		if (s.comps[c].parent >= 0) s.comps[s.comps[c].parent].children.push_back ((int)c);
	}
	return s;
}

// one animation at a time from the default pose: the vessel against the file
double Worst (const Case &k, const RtFile &f, unsigned seed, bool frames = false)
{
	std::mt19937 r (seed);
	std::uniform_real_distribution<double> u (-0.1, 1.1);
	std::uniform_int_distribution<int> fr (-2, 26);
	GxAnimSet live = Effective (k, f);
	double worst = 0;
	for (size_t a = 0; a < live.anims.size (); a++)
		for (int n = 0; n < 12; n++) {
			double s = n == 0 ? 0 : n == 1 ? 1 : n == 2 ? 0.5 : frames ? fr (r) / 24.0 : u (r); // through Blender the file is exact at its frames only
			RefVessel ref;
			ref.live = live;
			ref.v.assign (k.snap.meshes.size (), {});
			for (size_t e = 0; e < f.plans.size (); e++) {
				int mi = f.rec.meshes[e].index;
				ref.v[mi].assign (k.snap.meshes[mi].groups.size (), {});
				for (const RtOut &x : f.plans[e].outs) if (x.group >= 0) for (const NTVERTEX &p : x.vtx) ref.v[mi][x.group].push_back ({p.x, p.y, p.z});
			}
			ref.Start ();
			ref.Animate ((int)a, s);
			std::vector<double> st;
			for (const GxAnim &x : live.anims) st.push_back (x.defstate);
			st[a] = s;
			auto fv = FileVerts (f, st);
			for (auto &kv : fv) {
				const auto &q = ref.v[kv.first.first][kv.first.second];
				for (size_t i = 0; i < kv.second.size (); i++) {
					double dx = kv.second[i].x - q[i].x, dy = kv.second[i].y - q[i].y, dz = kv.second[i].z - q[i].z;
					worst = std::max (worst, std::sqrt (dx * dx + dy * dy + dz * dz));
				}
			}
		}
	return worst;
}

std::vector<RtBaseGroup> Base (const GxMesh &m, std::vector<std::vector<NTVERTEX>> &keep)
{
	std::vector<RtBaseGroup> b;
	keep.clear ();
	for (const GxGroup &G : m.groups) {
		std::vector<NTVERTEX> v;
		for (const GxVertex &x : G.vtx) v.push_back (NTVERTEX{x.p[0], x.p[1], x.p[2], x.n[0], x.n[1], x.n[2], x.uv[0], x.uv[1]});
		keep.push_back (v);
	}
	for (size_t g = 0; g < m.groups.size (); g++) b.push_back (RtBaseGroup{keep[g].data (), (DWORD)keep[g].size (), m.groups[g].idx.data (), (DWORD)m.groups[g].idx.size ()});
	return b;
}

void CheckUntouched (const Case &k, const RtFile &f)
{
	for (size_t e = 0; e < f.plans.size (); e++) {
		INFO ("mesh " << e);
		CHECK (f.plans[e].replaced.empty ());
		CHECK (f.plans[e].comps.empty ());
		std::vector<std::vector<NTVERTEX>> keep;
		auto base = Base (k.snap.meshes[f.rec.meshes[e].index], keep);
		for (size_t g = 0; g < base.size (); g++) {
			auto it = f.plans[e].primary.find ((int)g);
			REQUIRE (it != f.plans[e].primary.end ());
			std::vector<const RtOut *> parts{&f.plans[e].outs[it->second]};
			CHECK (RtSameGeometry (parts, base[g]));
		}
	}
}

// the file's node TRS on its vertices against the client's Animate with every animation at its state (design-gltf-simpose-glow 1)
double StaticWorst (const Case &k, int &posed)
{
	Parts p = Split (Export (k));
	QJsonArray nodes = p.json["nodes"].toArray (), meshes = p.json["meshes"].toArray ();
	std::vector<int> parent (nodes.size (), -1);
	for (int i = 0; i < nodes.size (); i++) for (auto c : nodes[i].toObject ()["children"].toArray ()) parent[c.toInt ()] = i;
	auto vec = [](const QJsonObject &n, const char *key, std::vector<double> d) {
		QJsonArray a = n[key].toArray ();
		for (int i = 0; i < a.size () && i < (int)d.size (); i++) d[i] = a[i].toDouble ();
		return d;
	};
	std::vector<EvMat> local (nodes.size ());
	for (int i = 0; i < nodes.size (); i++) {
		QJsonObject n = nodes[i].toObject ();
		if (n.contains ("rotation") || n.contains ("scale")) posed++;
		std::vector<double> t = vec (n, "translation", {0, 0, 0}), r = vec (n, "rotation", {0, 0, 0, 1}), s = vec (n, "scale", {1, 1, 1});
		local[i] = EvTRS (t.data (), r.data (), s.data ());
	}
	RefVessel ref;
	ref.live = k.snap.anims;
	ref.v.resize (k.snap.meshes.size ());
	for (size_t m = 0; m < k.snap.meshes.size (); m++)
		for (const GxGroup &G : k.snap.meshes[m].groups) {
			std::vector<GxVec3> v;
			for (const GxVertex &x : G.vtx) v.push_back ({x.p[0], x.p[1], x.p[2]});
			ref.v[m].push_back (v);
		}
	ref.Start ();
	for (size_t a = 0; a < k.snap.anims.anims.size (); a++) ref.Animate ((int)a, k.snap.anims.anims[a].state);
	double worst = 0;
	for (int i = 0; i < nodes.size (); i++) {
		QJsonObject n = nodes[i].toObject (), o = n["extras"].toObject ()["orbiter"].toObject ();
		if (!n.contains ("mesh") || !o.contains ("group")) continue;
		int mi = o["mesh"].toInt (), g = o["group"].toInt ();
		std::vector<float> pos = Floats (p, meshes[n["mesh"].toInt ()].toObject ()["primitives"].toArray ()[0].toObject ()["attributes"].toObject ()["POSITION"].toInt ());
		EvMat w = local[i];
		for (int q = parent[i]; q >= 0; q = parent[q]) w = EvMul (local[q], w);
		const GxVec3 &of = k.snap.meshes[mi].ofs;
		const auto &q = ref.v[mi][g];
		REQUIRE (pos.size () == q.size () * 3);
		for (size_t v = 0; v < q.size (); v++) {
			GxVec3 gl = EvApply (w, {pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]});
			double dx = gl.x - of.x - q[v].x, dy = gl.y - of.y - q[v].y, dz = -gl.z - of.z - q[v].z;
			worst = std::max (worst, std::sqrt (dx * dx + dy * dy + dz * dz));
		}
	}
	return worst;
}

} // namespace

TEST_CASE ("An untouched export reads back unchanged", "[RoundTrip]")
{
	for (unsigned seed = 1; seed <= 30; seed++) {
		INFO ("seed " << seed);
		Case k = Random (seed);
		RtFile f = Load (Export (k), "plain.glb");
		CHECK (f.rec.anims.size () == 3);
		CheckUntouched (k, f);
		CHECK (Worst (k, f, seed) < 2e-4);
	}
}

TEST_CASE ("An untouched export through Blender's sampling reads back unchanged", "[RoundTrip]")
{
	for (unsigned seed = 1; seed <= 30; seed++) {
		INFO ("seed " << seed);
		Case k = Random (seed);
		Parts p = Split (Export (k));
		Blenderise (p);
		RtFile f = Load (Join (p), "sampled.glb");
		CheckUntouched (k, f);
	}
}

TEST_CASE ("An edited animation is taken from the file and moves as the file says", "[RoundTrip]")
{
	int replaced = 0, fitted = 0;
	for (unsigned seed = 1; seed <= 40; seed++) {
		INFO ("seed " << seed);
		Case k = Random (seed);
		Case e = k;
		std::mt19937 r (seed * 7 + 1);
		int c = (int)(r () % e.snap.anims.comps.size ());
		GxComp &x = e.snap.anims.comps[c];
		switch (seed % 4) {
		case 0: if (x.type == GxType::Rotate) x.angle *= 1.25; else if (x.type == GxType::Translate) x.shift.x += 0.4; else x.scale = {x.scale.x * 1.2, x.scale.y * 1.2, x.scale.z * 1.2}; break;
		case 1: x.ref.y += 0.3; x.ref.z -= 0.2; break;
		case 2: x.state1 = x.state0 + (x.state1 - x.state0) * 0.6; break;
		default: if (x.type == GxType::Rotate) x.axis = {x.axis.y, x.axis.z, x.axis.x}; else x.shift = {x.shift.z, x.shift.x, x.shift.y}; break;
		}
		bool moves = (seed % 4 == 1) ? x.type != GxType::Translate : (seed % 4 == 3) ? x.type != GxType::Scale : true;
		Parts motion = Split (Export (e)), record = Split (Export (k));
		QJsonArray sc = motion.json["scenes"].toArray ();
		QJsonObject s0 = sc[0].toObject ();
		s0["extras"] = record.json["scenes"].toArray ()[0].toObject ()["extras"];
		sc[0] = s0;
		motion.json["scenes"] = sc;
		if (seed % 2) Blenderise (motion);
		RtFile f = Load (Join (motion), "edited.glb");
		int m = -1;
		for (size_t i = 0; i < f.rec.meshes.size (); i++) if (f.rec.meshes[i].index == x.mesh) m = (int)i;
		REQUIRE (m >= 0);
		if (moves) {
			CHECK (std::binary_search (f.plans[m].replaced.begin (), f.plans[m].replaced.end (), x.anim));
			replaced++;
		}
		for (auto &P : f.plans) fitted += (int)P.fitted.size ();
		CHECK (Worst (k, f, seed, seed % 2) < 2e-4);
	}
	CHECK (replaced > 20);
	CHECK (fitted > 10);
}

TEST_CASE ("The file's static pose is the vessel at its states", "[RoundTrip]")
{
	int posed = 0;
	for (unsigned seed = 1; seed <= 30; seed++) {
		INFO ("seed " << seed);
		Case k = Random (seed);
		CHECK (StaticWorst (k, posed) < 1e-4);
	}
	CHECK (posed > 30);
}
