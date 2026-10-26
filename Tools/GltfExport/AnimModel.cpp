// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: Orbiter animations as glTF nodes and channels (claude/design-gltf-export.md 5)

#include "AnimModel.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

static const double kPi = 3.14159265358979323846;

static GxVec3 Sub (const GxVec3 &a, const GxVec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static GxVec3 Add (const GxVec3 &a, const GxVec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static GxVec3 Mul (const GxVec3 &a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static double Dot (const GxVec3 &a, const GxVec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static GxVec3 Cross (const GxVec3 &a, const GxVec3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double Len (const GxVec3 &a) { return std::sqrt (Dot (a, a)); }
static GxVec3 Unit (const GxVec3 &a) { double l = Len (a); return l > 0 ? Mul (a, 1.0 / l) : a; }

GxVec3 GxAff::Point (const GxVec3 &p) const
{
	return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + t[0],
		m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + t[1],
		m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + t[2]};
}

GxVec3 GxAff::Dir (const GxVec3 &d) const
{
	return {m[0][0] * d.x + m[0][1] * d.y + m[0][2] * d.z,
		m[1][0] * d.x + m[1][1] * d.y + m[1][2] * d.z,
		m[2][0] * d.x + m[2][1] * d.y + m[2][2] * d.z};
}

GxAff GxAff::operator* (const GxAff &b) const
{
	GxAff r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j];
		r.t[i] = m[i][0] * b.t[0] + m[i][1] * b.t[1] + m[i][2] * b.t[2] + t[i];
	}
	return r;
}

GxAff GxAff::Inverse () const
{
	const double (*a)[3] = m;
	double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
	GxAff r;
	if (det == 0) return r;
	double id = 1.0 / det;
	r.m[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * id;
	r.m[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id;
	r.m[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id;
	r.m[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * id;
	r.m[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id;
	r.m[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id;
	r.m[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * id;
	r.m[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id;
	r.m[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id;
	for (int i = 0; i < 3; i++) r.t[i] = -(r.m[i][0] * t[0] + r.m[i][1] * t[1] + r.m[i][2] * t[2]);
	return r;
}

double GxFrac (const GxComp &c, double s)
{
	if (c.state1 == c.state0) return 0;
	double v = s; // the client's clamp, also for state0 > state1
	if (v < c.state0) v = c.state0;
	else if (v > c.state1) v = c.state1;
	return (v - c.state0) / (c.state1 - c.state0);
}

bool GxDegenerate (const GxComp &c)
{
	if (c.state0 == c.state1) return true;
	if (c.type == GxType::Rotate && Len (c.axis) == 0) return true;
	return false;
}

// rotation about a unit axis by angle, column vectors (D3DMAT_RotationFromAxis transposed)
static void RotMat (const GxVec3 &a, double angle, double r[3][3])
{
	double w = std::cos (angle * 0.5), s = std::sin (angle * 0.5);
	double x = s * a.x, y = s * a.y, z = s * a.z;
	r[0][0] = 1 - 2 * (y * y + z * z); r[0][1] = 2 * (x * y - w * z);     r[0][2] = 2 * (x * z + w * y);
	r[1][0] = 2 * (x * y + w * z);     r[1][1] = 1 - 2 * (x * x + z * z); r[1][2] = 2 * (y * z - w * x);
	r[2][0] = 2 * (x * z - w * y);     r[2][1] = 2 * (y * z + w * x);     r[2][2] = 1 - 2 * (x * x + y * y);
}

GxAff GxCompTransform (const GxComp &c, double fa, double fb)
{
	GxAff T;
	if (GxDegenerate (c)) return T;
	switch (c.type) {
	case GxType::Rotate: {
		RotMat (Unit (c.axis), c.angle * (fb - fa), T.m);
		GxVec3 r = T.Dir (c.ref);
		T.t[0] = c.ref.x - r.x; T.t[1] = c.ref.y - r.y; T.t[2] = c.ref.z - r.z;
	} break;
	case GxType::Translate:
		T.t[0] = c.shift.x * (fb - fa); T.t[1] = c.shift.y * (fb - fa); T.t[2] = c.shift.z * (fb - fa);
		break;
	case GxType::Scale: {
		const double k[3] = {c.scale.x, c.scale.y, c.scale.z}, p[3] = {c.ref.x, c.ref.y, c.ref.z};
		for (int i = 0; i < 3; i++) {
			double den = fa * (k[i] - 1) + 1;
			T.m[i][i] = den != 0 ? (fb * (k[i] - 1) + 1) / den : 1;
			T.t[i] = p[i] * (1 - T.m[i][i]);
		}
	} break;
	default:
		break;
	}
	return T;
}

static void Ancestors (const GxAnimSet &set, int c, std::vector<int> &out)
{
	out.clear ();
	std::set<int> seen;
	for (int p = set.comps[c].parent; p >= 0 && !seen.count (p); p = set.comps[p].parent) {
		seen.insert (p);
		out.push_back (p);
	}
	std::reverse (out.begin (), out.end ()); // outermost first
}

static double CurFrac (const GxAnimSet &set, const GxComp &c) { return GxFrac (c, set.anims[c.anim].state); }
static double DefFrac (const GxAnimSet &set, const GxComp &c) { return GxFrac (c, set.anims[c.anim].defstate); }

void GxRecoverRest (GxAnimSet &set)
{
	std::vector<int> depth (set.comps.size ()), anc;
	for (size_t c = 0; c < set.comps.size (); c++) { Ancestors (set, (int)c, anc); depth[c] = (int)anc.size (); }
	std::vector<int> order (set.comps.size ());
	for (size_t i = 0; i < order.size (); i++) order[i] = (int)i;
	std::stable_sort (order.begin (), order.end (), [&](int a, int b) { return depth[a] < depth[b]; });
	for (int c : order) {
		if (!depth[c]) continue; // a root is never moved
		Ancestors (set, c, anc);
		GxAff M;
		for (int a : anc) M = M * GxCompTransform (set.comps[a], DefFrac (set, set.comps[a]), CurFrac (set, set.comps[a]));
		GxAff Mi = M.Inverse ();
		GxComp &k = set.comps[c];
		switch (k.type) {
		case GxType::Rotate: k.ref = Mi.Point (k.ref); k.axis = Unit (Mi.Dir (k.axis)); break;
		case GxType::Translate: k.shift = Mi.Dir (k.shift); break;
		case GxType::Scale: k.ref = Mi.Point (k.ref); break;
		default: break;
		}
	}
}

GxVec3 GxToGltf (const GxVec3 &v) { return {v.x, v.y, -v.z}; }

void GxQuatGltf (const GxVec3 &axis, double angle, double q[4])
{
	GxVec3 a = Unit (axis);
	double s = std::sin (angle * 0.5);
	q[0] = -s * a.x; q[1] = -s * a.y; q[2] = s * a.z; q[3] = std::cos (angle * 0.5);
}

static bool Parallel (const GxVec3 &a, const GxVec3 &b)
{
	double la = Len (a), lb = Len (b);
	if (la == 0 || lb == 0) return true;
	return Len (Cross (a, b)) <= 1e-6 * la * lb;
}

static bool Commute (const GxComp &a, const GxComp &b)
{
	if (GxDegenerate (a) || GxDegenerate (b) || a.type == GxType::Null || b.type == GxType::Null) return true;
	if (a.type == GxType::Translate && b.type == GxType::Translate) return true;
	if (a.type == GxType::Rotate && b.type == GxType::Rotate)
		return Parallel (a.axis, b.axis) && Parallel (Sub (b.ref, a.ref), a.axis);
	if (a.type == GxType::Rotate && b.type == GxType::Translate) return Parallel (b.shift, a.axis);
	if (a.type == GxType::Translate && b.type == GxType::Rotate) return Parallel (a.shift, b.axis);
	if (a.type == GxType::Scale && b.type == GxType::Scale) return Len (Sub (a.ref, b.ref)) <= 1e-9;
	return false;
}

static std::string TypeName (GxType t)
{
	switch (t) {
	case GxType::Rotate: return "rotate";
	case GxType::Translate: return "translate";
	case GxType::Scale: return "scale";
	default: return "null";
	}
}

static std::string MeshLabel (const GxMesh &m)
{
	std::string n = m.name;
	size_t p = n.find_last_of ("\\/");
	if (p != std::string::npos) n = n.substr (p + 1);
	return n.empty () ? "Mesh " + std::to_string (m.index) : n;
}

void GxBuildModel (const GxSnapshot &snap, const std::vector<std::vector<std::string>> &labels,
	GxModel &out, std::vector<std::string> &warnings)
{
	out = GxModel ();
	const GxAnimSet &set = snap.anims;
	auto label = [&](int m, int g) -> std::string {
		if (m < 0 || m >= (int)labels.size () || g < 0 || g >= (int)labels[m].size ()) return "";
		return labels[m][g];
	};
	auto meshOf = [&](int idx) -> int { // vessel mesh index to snapshot mesh
		for (size_t i = 0; i < snap.meshes.size (); i++) if (snap.meshes[i].index == idx) return (int)i;
		return -1;
	};

	// animation names: the first labelled group its components move, else its first mesh and its part count
	std::vector<std::string> animName (set.anims.size ());
	for (size_t a = 0; a < set.anims.size (); a++) {
		std::string n, mesh;
		for (int c : set.anims[a].comps) {
			const GxComp &k = set.comps[c];
			int m = meshOf (k.mesh);
			if (m < 0) continue;
			if (mesh.empty ()) mesh = MeshLabel (snap.meshes[m]);
			if (k.allGroups) { n = MeshLabel (snap.meshes[m]); break; }
			for (int g : k.groups) { n = label (m, g); if (!n.empty ()) break; }
			if (!n.empty ()) break;
		}
		size_t np = set.anims[a].comps.size ();
		if (n.empty ()) n = (mesh.empty () ? std::string ("Animation") : mesh) + " (" + std::to_string (np) + (np == 1 ? " part)" : " parts)");
		animName[a] = std::to_string (a) + " " + n;
	}

	std::vector<std::string> longAxis;
	for (const GxComp &k : set.comps) {
		std::string id = "animation " + std::to_string (k.anim) + " part " + std::to_string (k.index);
		if (k.state0 == k.state1) warnings.push_back (id + ": state range empty, not moved");
		else if (k.type == GxType::Rotate && Len (k.axis) == 0) warnings.push_back (id + ": zero rotation axis, not moved");
		else if (k.type == GxType::Rotate && std::fabs (Len (k.axis) - 1) > 1e-3) longAxis.push_back (id);
	}
	if (longAxis.size () == 1) warnings.push_back (longAxis[0] + ": rotation axis not of unit length, exported normalised (Orbiter also scales the part)");
	else if (!longAxis.empty ()) warnings.push_back (std::to_string (longAxis.size ()) + " parts with a rotation axis not of unit length (first " + longAxis[0] + "), exported normalised (Orbiter also scales them)");

	auto aniso = [](const GxComp &k) { return k.type == GxType::Scale && (std::fabs (k.scale.x - k.scale.y) > 1e-9 || std::fabs (k.scale.y - k.scale.z) > 1e-9); };
	for (size_t c = 0; c < set.comps.size (); c++) { // Orbiter keeps an anisotropic scale's axes fixed and a child rotation rigid: a hierarchy can't (code review)
		const GxComp &k = set.comps[c];
		if (GxDegenerate (k) || (k.type != GxType::Rotate && !aniso (k))) continue;
		std::vector<int> up;
		Ancestors (set, (int)c, up);
		for (int a : up) {
			const GxComp &p = set.comps[a];
			if (GxDegenerate (p)) continue;
			if ((k.type == GxType::Rotate && aniso (p)) || (aniso (k) && p.type == GxType::Rotate)) {
				warnings.push_back ("animation " + std::to_string (k.anim) + " part " + std::to_string (k.index) + ": " + (aniso (k) ? "uneven scale under a rotation" : "rotation under an uneven scale") + ", exported as a hierarchy (Orbiter moves it slightly differently)");
				break;
			}
		}
	}

	auto addNode = [&](GxNode n) -> int {
		int id = (int)out.nodes.size ();
		if (n.parent >= 0) out.nodes[n.parent].children.push_back (id);
		else out.roots.push_back (id);
		out.nodes.push_back (std::move (n));
		return id;
	};

	int rootExt = -1, rootCpt = -1;
	std::map<int, std::vector<int>> compNodes; // component -> its node instances

	for (size_t mi = 0; mi < snap.meshes.size (); mi++) {
		const GxMesh &M = snap.meshes[mi];
		bool ext = (M.vis & 1) != 0;
		int &root = ext ? rootExt : rootCpt;
		if (root < 0) { GxNode r; r.name = ext ? "Exterior" : "Cockpit"; root = addNode (r); }
		GxNode mn; mn.name = MeshLabel (M); mn.parent = root; mn.meshOf = (int)mi;
		int meshNode = addNode (mn);

		std::map<std::vector<int>, int> trie; // chain -> node
		int hidden = 0;
		for (size_t g = 0; g < M.groups.size (); g++) {
			const GxGroup &G = M.groups[g];
			if (G.vtx.empty () || G.idx.size () < 3) continue;
			if (G.usrFlag & 0x2) { hidden++; continue; }

			std::vector<int> listings;
			for (size_t c = 0; c < set.comps.size (); c++) {
				const GxComp &k = set.comps[c];
				if (k.mesh != M.index) continue;
				if (k.allGroups || std::find (k.groups.begin (), k.groups.end (), (int)g) != k.groups.end ()) listings.push_back ((int)c);
			}
			// outer first: across animations the higher index; in one animation the part that moves later going away from defstate,
			// then by the side defstate lies on (design 5.3, code review: ranges before list order)
			auto later = [&](const GxComp &k) {
				double def = set.anims[k.anim].defstate, lo = std::min (k.state0, k.state1), hi = std::max (k.state0, k.state1);
				if (lo >= def) return lo - def;
				if (hi <= def) return def - hi;
				return 0.0;
			};
			std::stable_sort (listings.begin (), listings.end (), [&](int a, int b) {
				const GxComp &A = set.comps[a], &B = set.comps[b];
				if (A.anim != B.anim) return A.anim > B.anim;
				double ta = later (A), tb = later (B);
				if (ta != tb) return ta > tb;
				const GxAnim &an = set.anims[A.anim];
				double lo = 0, hi = 1;
				for (int c : an.comps) { lo = std::min (lo, std::min (set.comps[c].state0, set.comps[c].state1)); hi = std::max (hi, std::max (set.comps[c].state0, set.comps[c].state1)); }
				bool rising = an.defstate <= 0.5 * (lo + hi);
				return rising ? A.index > B.index : A.index < B.index;
			});
			std::vector<int> chain, anc;
			std::vector<std::vector<int>> paths;
			for (int c : listings) {
				Ancestors (set, c, anc);
				anc.push_back (c);
				paths.push_back (anc);
				chain.insert (chain.end (), anc.begin (), anc.end ());
			}
			bool ok = true;
			for (size_t i = 0; i < listings.size () && ok; i++)
				for (size_t j = i + 1; j < listings.size () && ok; j++) {
					const GxComp &A = set.comps[listings[i]], &B = set.comps[listings[j]];
					bool overlap = std::max (std::min (A.state0, A.state1), std::min (B.state0, B.state1)) < std::min (std::max (A.state0, A.state1), std::max (B.state0, B.state1));
					if (A.anim == B.anim && !overlap) continue; // one after the other: the order is the one they run in
					for (int x : paths[i]) for (int y : paths[j]) if (x != y && !Commute (set.comps[x], set.comps[y])) ok = false;
				}
			if (!ok) {
				std::string gl = label ((int)mi, (int)g);
				warnings.push_back ("mesh " + std::to_string (M.index) + " group " + std::to_string (g) + (gl.empty () ? "" : " (" + gl + ")") + ": moved by " + std::to_string (listings.size ()) + " parts at once whose order matters; check it in Blender");
			}

			int parent = meshNode;
			GxVec3 ppivot;
			std::vector<int> key;
			for (int c : chain) {
				key.push_back (c);
				auto it = trie.find (key);
				if (it == trie.end ()) {
					const GxComp &k = set.comps[c];
					GxNode n;
					n.comp = c;
					n.parent = parent;
					n.pivot = (k.type == GxType::Rotate || k.type == GxType::Scale) ? Add (k.ref, M.ofs) : ppivot;
					n.t = GxToGltf (Sub (n.pivot, ppivot));
					int m0 = meshOf (k.mesh);
					std::string gl;
					if (m0 >= 0 && !k.allGroups) for (int gg : k.groups) { gl = label (m0, gg); if (!gl.empty ()) break; }
					n.name = "pivot " + std::to_string (k.anim) + "." + std::to_string (k.index) + " " + TypeName (k.type) + (gl.empty () ? "" : " " + gl);
					int id = addNode (n);
					compNodes[c].push_back (id);
					it = trie.emplace (key, id).first;
				}
				parent = it->second;
				ppivot = out.nodes[parent].pivot;
			}
			GxNode gn;
			gn.mesh = (int)mi; gn.group = (int)g; gn.parent = parent;
			gn.pivot = ppivot;
			gn.t = GxToGltf (Mul (ppivot, -1));
			std::string gl = label ((int)mi, (int)g);
			gn.name = gl.empty () ? "Group " + std::to_string (g) : gl;
			addNode (gn);
		}
		if (hidden) warnings.push_back (MeshLabel (M) + ": " + std::to_string (hidden) + " hidden groups (flag 2) left out");
	}

	for (GxNode &n : out.nodes) n.poseT = n.t;

	// channels (design 5.4), and the pose at each animation's state (design-gltf-simpose-glow 1)
	for (size_t a = 0; a < set.anims.size (); a++) {
		const GxAnim &an = set.anims[a];
		double lo = 0, hi = 1;
		for (int c : an.comps) { lo = std::min (lo, std::min (set.comps[c].state0, set.comps[c].state1)); hi = std::max (hi, std::max (set.comps[c].state0, set.comps[c].state1)); }
		GxGltfAnim ga;
		ga.anim = (int)a;
		ga.name = animName[a];
		for (int c : an.comps) {
			const GxComp &k = set.comps[c];
			if (GxDegenerate (k) || k.type == GxType::Null) continue;
			auto it = compNodes.find (c);
			if (it == compNodes.end ()) continue;
			std::vector<double> s = {lo, hi, std::clamp (k.state0, lo, hi), std::clamp (k.state1, lo, hi)};
			std::sort (s.begin (), s.end ());
			s.erase (std::unique (s.begin (), s.end ()), s.end ());
			double fdef = GxFrac (k, an.defstate);
			if (k.type == GxType::Rotate) { // segments over 2 degrees split: Blender's per-component curves then stay within 2e-6 rad of slerp, so an untouched animation reads back equal (design-gltf-roundtrip 1)
				std::vector<double> d;
				for (size_t i = 0; i + 1 < s.size (); i++) {
					d.push_back (s[i]);
					double turn = std::fabs (k.angle * (GxFrac (k, s[i + 1]) - GxFrac (k, s[i])));
					int n = (int)std::ceil (turn / (kPi / 90) - 1e-9);
					for (int j = 1; j < n; j++) d.push_back (s[i] + (s[i + 1] - s[i]) * j / n);
				}
				d.push_back (s.back ());
				s = d;
			}
			for (int node : it->second) {
				GxNode &pn = out.nodes[node];
				double dp = GxFrac (k, an.state) - fdef;
				if (k.type == GxType::Rotate) GxQuatGltf (k.axis, k.angle * dp, pn.poseR);
				else if (k.type == GxType::Translate) pn.poseT = Add (pn.t, GxToGltf (Mul (k.shift, dp)));
				else {
					const double kk[3] = {k.scale.x, k.scale.y, k.scale.z};
					double fs = GxFrac (k, an.state), v[3];
					for (int i = 0; i < 3; i++) { double den = fdef * (kk[i] - 1) + 1; v[i] = den != 0 ? (fs * (kk[i] - 1) + 1) / den : 1; }
					pn.poseS = {v[0], v[1], v[2]};
				}
				GxChannel ch;
				ch.node = node;
				for (double st : s) {
					ch.times.push_back ((float)(st - lo));
					double df = GxFrac (k, st) - fdef;
					if (k.type == GxType::Rotate) {
						ch.path = GxPath::Rotation;
						double q[4];
						GxQuatGltf (k.axis, k.angle * df, q);
						for (double v : q) ch.values.push_back ((float)v);
					} else if (k.type == GxType::Translate) {
						ch.path = GxPath::Translation;
						GxVec3 v = Add (out.nodes[node].t, GxToGltf (Mul (k.shift, df)));
						ch.values.push_back ((float)v.x); ch.values.push_back ((float)v.y); ch.values.push_back ((float)v.z);
					} else {
						ch.path = GxPath::Scale;
						const double kk[3] = {k.scale.x, k.scale.y, k.scale.z};
						double fs = GxFrac (k, st);
						for (int i = 0; i < 3; i++) {
							double den = fdef * (kk[i] - 1) + 1;
							ch.values.push_back ((float)(den != 0 ? (fs * (kk[i] - 1) + 1) / den : 1));
						}
					}
				}
				ga.channels.push_back (std::move (ch));
			}
		}
		if (!ga.channels.empty ()) out.anims.push_back (std::move (ga));
	}
}
