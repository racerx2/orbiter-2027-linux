// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: snapshot, nodes and images to GLB bytes (claude/design-gltf-export.md 3, 6, 7)

#include "GlbWriter.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>

double GxSrgbToLinear (double c)
{
	c = std::clamp (c, 0.0, 1.0);
	return c <= 0.04045 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
}

double GxRoughness (const float sp[4], float power)
{
	if (std::max ({sp[0], sp[1], sp[2]}) <= 0) return 1.0;
	double p = std::max (0.0f, power);
	return std::sqrt (std::sqrt (2.0 / (p + 2.0))); // Blinn-Phong exponent to GGX alpha, then perceptual roughness
}

namespace {

struct Bin {
	std::vector<uint8_t> data;
	QJsonArray views, accessors;

	int View (const void *p, size_t n, int target)
	{
		while (data.size () % 4) data.push_back (0);
		QJsonObject v;
		v["buffer"] = 0;
		v["byteOffset"] = (qint64)data.size ();
		v["byteLength"] = (qint64)n;
		if (target) v["target"] = target;
		const uint8_t *b = (const uint8_t*)p;
		data.insert (data.end (), b, b + n);
		views.append (v);
		return views.size () - 1;
	}

	int Floats (const std::vector<float> &f, int comps, const char *type, int target, bool minmax)
	{
		QJsonObject a;
		a["bufferView"] = View (f.data (), f.size () * sizeof (float), target);
		a["componentType"] = 5126;
		a["count"] = (qint64)(f.size () / comps);
		a["type"] = type;
		if (minmax) {
			QJsonArray mn, mx;
			for (int c = 0; c < comps; c++) {
				float lo = f[c], hi = f[c];
				for (size_t i = c; i < f.size (); i += comps) { lo = std::min (lo, f[i]); hi = std::max (hi, f[i]); }
				mn.append (lo); mx.append (hi);
			}
			a["min"] = mn; a["max"] = mx;
		}
		accessors.append (a);
		return accessors.size () - 1;
	}

	int Shorts (const std::vector<uint16_t> &s, int target)
	{
		QJsonObject a;
		a["bufferView"] = View (s.data (), s.size () * 2, target);
		a["componentType"] = 5123;
		a["count"] = (qint64)s.size ();
		a["type"] = "SCALAR";
		accessors.append (a);
		return accessors.size () - 1;
	}
};

QJsonArray V3 (const GxVec3 &v) { return QJsonArray{v.x, v.y, v.z}; }
QJsonArray C4 (const float c[4]) { return QJsonArray{c[0], c[1], c[2], c[3]}; }

const char *TypeName (GxType t)
{
	switch (t) {
	case GxType::Rotate: return "rotate";
	case GxType::Translate: return "translate";
	case GxType::Scale: return "scale";
	default: return "null";
	}
}

QJsonArray C (const float c[4]) { return QJsonArray{c[0], c[1], c[2], c[3]}; }

bool Skipped (const GxGroup &G) { return G.vtx.empty () || G.idx.size () < 3 || (G.usrFlag & 0x2); } // as GxBuildModel leaves them out

// scene extras, version 2 (claude/design-gltf-roundtrip.md 1)
QJsonObject Extras (const GxGlbInput &in)
{
	const GxSnapshot &s = *in.snap;
	QJsonObject o;
	o["version"] = 2;
	o["vessel"] = QString::fromStdString (s.vesselName);
	o["class"] = QString::fromStdString (s.className);
	o["secondsPerState"] = 1;
	QJsonArray meshes;
	for (size_t mi = 0; mi < s.meshes.size (); mi++) {
		const GxMesh &m = s.meshes[mi];
		QJsonObject j;
		j["index"] = m.index;
		j["name"] = QString::fromStdString (m.name);
		j["offset"] = V3 (m.ofs);
		j["visibility"] = m.vis;
		j["groups"] = (int)m.groups.size ();
		QJsonArray sk, fl;
		for (size_t g = 0; g < m.groups.size (); g++) {
			if (Skipped (m.groups[g])) sk.append ((int)g);
			fl.append ((double)m.groups[g].usrFlag);
		}
		j["skipped"] = sk;
		j["flags"] = fl;
		QJsonArray mt;
		for (const GxMaterial &x : m.mtrls)
			mt.append (QJsonObject{{"diffuse", C (x.diffuse)}, {"ambient", C (x.ambient)}, {"specular", C (x.specular)}, {"emissive", C (x.emissive)}, {"power", x.power}});
		j["materials"] = mt;
		QJsonArray tx;
		for (int t = 0; t < std::max (0, m.ntex); t++) {
			QJsonObject e;
			e["name"] = QString::fromStdString (mi < in.texNames.size () && t < (int)in.texNames[mi].size () ? in.texNames[mi][t] : std::string ("0"));
			e["dynamic"] = mi < in.texDynamic.size () && t < (int)in.texDynamic[mi].size () && in.texDynamic[mi][t];
			e["hash"] = QString::fromStdString (mi < in.texHash.size () && t < (int)in.texHash[mi].size () ? in.texHash[mi][t] : std::string ());
			tx.append (e);
		}
		j["textures"] = tx;
		meshes.append (j);
	}
	o["meshes"] = meshes;
	QJsonArray anims;
	for (const GxAnim &a : s.anims.anims) {
		QJsonObject j;
		j["defstate"] = a.defstate;
		j["state"] = a.state;
		double lo = 0;
		for (int c : a.comps) lo = std::min (lo, std::min (s.anims.comps[c].state0, s.anims.comps[c].state1));
		j["time0"] = lo; // GxBuildModel's channel times are state - lo
		QJsonArray comps;
		for (int c : a.comps) {
			const GxComp &k = s.anims.comps[c];
			QJsonObject q;
			q["state0"] = k.state0; q["state1"] = k.state1;
			q["type"] = TypeName (k.type);
			q["mesh"] = k.mesh;
			if (k.allGroups) q["groups"] = "all";
			else { QJsonArray g; for (int x : k.groups) g.append (x); q["groups"] = g; }
			if (k.type == GxType::Rotate) { q["ref"] = V3 (k.ref); q["axis"] = V3 (k.axis); q["angle"] = k.angle; }
			if (k.type == GxType::Translate) q["shift"] = V3 (k.shift);
			if (k.type == GxType::Scale) { q["ref"] = V3 (k.ref); q["scale"] = V3 (k.scale); }
			if (k.parent >= 0) q["parent"] = QJsonArray{s.anims.comps[k.parent].anim, s.anims.comps[k.parent].index};
			comps.append (q);
		}
		j["components"] = comps;
		anims.append (j);
	}
	o["animations"] = anims;
	return o;
}

}

std::string GxPixelHash (int w, int h, const uint8_t *rgba)
{
	unsigned long long x = 1469598103934665603ull;
	auto feed = [&](const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) { x ^= p[i]; x *= 1099511628211ull; } };
	uint8_t hdr[8];
	for (int i = 0; i < 4; i++) { hdr[i] = (uint8_t)((unsigned)w >> (8 * i)); hdr[4 + i] = (uint8_t)((unsigned)h >> (8 * i)); }
	feed (hdr, 8);
	feed (rgba, (size_t)w * h * 4);
	char b[24];
	snprintf (b, sizeof b, "%016llx", x);
	return b;
}

bool GxWriteGlb (const GxGlbInput &in, std::vector<uint8_t> &out, std::string &err)
{
	const GxSnapshot &snap = *in.snap;
	const GxModel &model = *in.model;
	Bin bin;
	QJsonArray images, textures, materials, meshes, nodes, anims;
	bool unlit = false;

	for (const GxPng &p : in.images) {
		QJsonObject im;
		im["bufferView"] = bin.View (p.png.data (), p.png.size (), 0);
		im["mimeType"] = "image/png";
		images.append (im);
		QJsonObject tx;
		tx["source"] = images.size () - 1;
		tx["sampler"] = 0;
		textures.append (tx);
	}

	std::map<std::tuple<int, int, int, bool, bool>, int> mtrlOf;
	auto material = [&](int mi, const GxGroup &G) -> int {
		bool noLight = (G.usrFlag & 0x4) != 0, noColor = (G.usrFlag & 0x10) != 0; // 0x10: the shaders take the texture colour as 1
		auto key = std::make_tuple (mi, G.mtrl, G.tex, noLight, noColor);
		auto it = mtrlOf.find (key);
		if (it != mtrlOf.end ()) return it->second;
		static const GxMaterial def = {{1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, 10};
		const GxMesh &M = snap.meshes[mi];
		const GxMaterial &mt = (G.mtrl >= 0 && G.mtrl < (int)M.mtrls.size ()) ? M.mtrls[G.mtrl] : def;
		GxTexSet ts;
		if (G.tex >= 0 && mi < (int)in.tex.size () && G.tex < (int)in.tex[mi].size ()) ts = in.tex[mi][G.tex];
		QJsonObject m, pbr;
		std::string name;
		if (G.mtrl >= 0 && mi < (int)in.mtrlNames.size () && G.mtrl < (int)in.mtrlNames[mi].size ()) name = in.mtrlNames[mi][G.mtrl];
		if (name.empty ()) name = G.mtrl >= 0 ? "Material " + std::to_string (G.mtrl) : "Default";
		m["name"] = QString::fromStdString (name);
		bool glow = mt.emissive[0] > 0 || mt.emissive[1] > 0 || mt.emissive[2] > 0;
		double d[3];
		for (int c = 0; c < 3; c++) d[c] = noLight ? std::clamp (mt.diffuse[c] + mt.emissive[c], 0.0f, 1.0f) : mt.diffuse[c]; // fully lit: tex * clamp (diffuse + emissive)
		QJsonArray base{GxSrgbToLinear (d[0]), GxSrgbToLinear (d[1]), GxSrgbToLinear (d[2]), std::clamp (mt.diffuse[3], 0.0f, 1.0f)};
		double metal = ts.mr >= 0 && ts.metal ? 1.0 : 0.0; // a channel without its map is 255: the factor gives the value
		double rough = ts.mr >= 0 && ts.rough ? 1.0 : GxRoughness (mt.specular, mt.power);
		pbr["baseColorFactor"] = base;
		pbr["metallicFactor"] = metal;
		pbr["roughnessFactor"] = rough;
		if (ts.base >= 0) pbr["baseColorTexture"] = QJsonObject{{"index", ts.base}};
		if (ts.mr >= 0) pbr["metallicRoughnessTexture"] = QJsonObject{{"index", ts.mr}};
		m["pbrMetallicRoughness"] = pbr;
		// Orbiter lights the texture with the emissive colour; an _emis map is max'd in (design-gltf-simpose-glow 2)
		QJsonArray elin{GxSrgbToLinear (mt.emissive[0]), GxSrgbToLinear (mt.emissive[1]), GxSrgbToLinear (mt.emissive[2])};
		auto gi = in.glow.find (std::make_tuple (mi, G.mtrl, G.tex));
		if (!noLight) { // fully lit: in the base colour
			if (!noColor && gi != in.glow.end ()) {
				m["emissiveTexture"] = QJsonObject{{"index", gi->second}};
				m["emissiveFactor"] = QJsonArray{1, 1, 1};
			} else if (ts.emissive >= 0) {
				m["emissiveTexture"] = QJsonObject{{"index", ts.emissive}};
				m["emissiveFactor"] = QJsonArray{1, 1, 1};
			} else if (glow && ts.base >= 0 && !noColor) {
				m["emissiveTexture"] = QJsonObject{{"index", ts.base}};
				m["emissiveFactor"] = elin;
			} else if (glow)
				m["emissiveFactor"] = elin;
		}
		if (ts.normal >= 0) m["normalTexture"] = QJsonObject{{"index", ts.normal}};
		GxAlpha a = mt.diffuse[3] < 1 ? GxAlpha::Blend : ts.base >= 0 ? in.images[ts.base].alpha : GxAlpha::Opaque;
		if (a == GxAlpha::Blend) m["alphaMode"] = "BLEND";
		else if (a == GxAlpha::Mask) { m["alphaMode"] = "MASK"; m["alphaCutoff"] = 0.5; }
		if (noLight) { m["extensions"] = QJsonObject{{"KHR_materials_unlit", QJsonObject ()}}; unlit = true; }
		// what was written, so the core can tell an untouched material after Blender (design-gltf-roundtrip 3.2)
		QJsonObject vals{{"base", base}, {"metallic", metal}, {"roughness", rough}, {"alphaMode", a == GxAlpha::Blend ? "BLEND" : a == GxAlpha::Mask ? "MASK" : "OPAQUE"}};
		vals["emissive"] = m.contains ("emissiveFactor") ? m["emissiveFactor"].toArray () : QJsonArray{0, 0, 0};
		if (a == GxAlpha::Mask) vals["alphaCutoff"] = 0.5;
		int maps = (ts.base >= 0 ? 1 : 0) | (ts.normal >= 0 ? 2 : 0) | (m.contains ("emissiveTexture") ? 4 : 0) | (ts.mr >= 0 ? 8 : 0); // as written: the core counts the file's maps
		m["extras"] = QJsonObject{{"orbiter", QJsonObject{{"mesh", snap.meshes[mi].index}, {"mtrl", G.mtrl}, {"tex", G.tex}, {"unlit", noLight}, {"maps", maps}, {"values", vals}}}};
		materials.append (m);
		int id = materials.size () - 1;
		mtrlOf[key] = id;
		return id;
	};

	for (size_t i = 0; i < model.nodes.size (); i++) {
		const GxNode &n = model.nodes[i];
		QJsonObject j;
		j["name"] = QString::fromStdString (n.name);
		if (n.poseT.x != 0 || n.poseT.y != 0 || n.poseT.z != 0) j["translation"] = V3 (n.poseT); // the pose the sim had (design-gltf-simpose-glow 1)
		if (n.poseR[0] != 0 || n.poseR[1] != 0 || n.poseR[2] != 0 || n.poseR[3] != 1) j["rotation"] = QJsonArray{n.poseR[0], n.poseR[1], n.poseR[2], n.poseR[3]};
		if (n.poseS.x != 1 || n.poseS.y != 1 || n.poseS.z != 1) j["scale"] = V3 (n.poseS);
		if (!n.children.empty ()) { QJsonArray c; for (int x : n.children) c.append (x); j["children"] = c; }
		if (n.group >= 0) {
			QJsonArray chain; // the pivots above the group, outer first
			std::vector<int> up;
			for (int p = n.parent; p >= 0; p = model.nodes[p].parent) if (model.nodes[p].comp >= 0) up.push_back (model.nodes[p].comp);
			for (auto it = up.rbegin (); it != up.rend (); ++it) chain.append (QJsonArray{snap.anims.comps[*it].anim, snap.anims.comps[*it].index});
			j["extras"] = QJsonObject{{"orbiter", QJsonObject{{"mesh", snap.meshes[n.mesh].index}, {"group", n.group}, {"chain", chain}}}};
		} else if (n.comp >= 0) j["extras"] = QJsonObject{{"orbiter", QJsonObject{{"anim", snap.anims.comps[n.comp].anim}, {"component", snap.anims.comps[n.comp].index}}}};
		else if (n.meshOf >= 0) j["extras"] = QJsonObject{{"orbiter", QJsonObject{{"mesh", snap.meshes[n.meshOf].index}}}};
		if (n.group >= 0) {
			const GxMesh &M = snap.meshes[n.mesh];
			const GxGroup &G = M.groups[n.group];
			std::vector<float> pos, nrm, uv;
			pos.reserve (G.vtx.size () * 3); nrm.reserve (G.vtx.size () * 3); uv.reserve (G.vtx.size () * 2);
			for (const GxVertex &v : G.vtx) {
				pos.push_back ((float)(v.p[0] + M.ofs.x)); pos.push_back ((float)(v.p[1] + M.ofs.y)); pos.push_back ((float)-(v.p[2] + M.ofs.z));
				double nx = v.n[0], ny = v.n[1], nz = -v.n[2], l = std::sqrt (nx * nx + ny * ny + nz * nz);
				if (l > 1e-12) { nx /= l; ny /= l; nz /= l; } else { nx = 0; ny = 1; nz = 0; }
				nrm.push_back ((float)nx); nrm.push_back ((float)ny); nrm.push_back ((float)nz);
				uv.push_back (v.uv[0]); uv.push_back (v.uv[1]);
			}
			std::vector<uint16_t> idx;
			idx.reserve (G.idx.size ());
			for (size_t t = 0; t + 2 < G.idx.size (); t += 3) { idx.push_back (G.idx[t]); idx.push_back (G.idx[t + 2]); idx.push_back (G.idx[t + 1]); } // winding reversed by the mirror
			QJsonObject attr;
			attr["POSITION"] = bin.Floats (pos, 3, "VEC3", 34962, true);
			attr["NORMAL"] = bin.Floats (nrm, 3, "VEC3", 34962, false);
			attr["TEXCOORD_0"] = bin.Floats (uv, 2, "VEC2", 34962, false);
			QJsonObject prim;
			prim["attributes"] = attr;
			prim["indices"] = bin.Shorts (idx, 34963);
			prim["material"] = material (n.mesh, G);
			QJsonObject mesh;
			mesh["name"] = QString::fromStdString (n.name);
			mesh["primitives"] = QJsonArray{prim};
			meshes.append (mesh);
			j["mesh"] = meshes.size () - 1;
		}
		nodes.append (j);
	}

	for (const GxGltfAnim &a : model.anims) {
		QJsonArray samplers, channels;
		for (const GxChannel &c : a.channels) {
			QJsonObject s;
			s["input"] = bin.Floats (c.times, 1, "SCALAR", 0, true);
			s["output"] = bin.Floats (c.values, c.path == GxPath::Rotation ? 4 : 3, c.path == GxPath::Rotation ? "VEC4" : "VEC3", 0, false);
			s["interpolation"] = "LINEAR";
			samplers.append (s);
			QJsonObject t;
			t["node"] = c.node;
			t["path"] = c.path == GxPath::Rotation ? "rotation" : c.path == GxPath::Translation ? "translation" : "scale";
			channels.append (QJsonObject{{"sampler", samplers.size () - 1}, {"target", t}});
		}
		QJsonObject j;
		j["name"] = QString::fromStdString (a.name);
		j["samplers"] = samplers;
		j["channels"] = channels;
		anims.append (j);
	}

	QJsonObject root;
	QJsonObject asset;
	asset["version"] = "2.0";
	asset["generator"] = QString::fromStdString (in.generator);
	root["asset"] = asset;
	QJsonArray sroots;
	for (int r : model.roots) sroots.append (r);
	root["scene"] = 0;
	root["scenes"] = QJsonArray{QJsonObject{{"name", QString::fromStdString (snap.vesselName)}, {"nodes", sroots}, {"extras", QJsonObject{{"orbiter", Extras (in)}}}}}; // Blender keeps scene extras, drops asset extras
	root["nodes"] = nodes;
	if (!meshes.isEmpty ()) root["meshes"] = meshes;
	if (!materials.isEmpty ()) root["materials"] = materials;
	if (!images.isEmpty ()) {
		root["images"] = images;
		root["textures"] = textures;
		root["samplers"] = QJsonArray{QJsonObject{{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 10497}, {"wrapT", 10497}}};
	}
	if (!anims.isEmpty ()) root["animations"] = anims;
	if (unlit) root["extensionsUsed"] = QJsonArray{"KHR_materials_unlit"};
	while (bin.data.size () % 4) bin.data.push_back (0);
	if (!bin.data.empty ()) {
		root["bufferViews"] = bin.views;
		root["accessors"] = bin.accessors;
		root["buffers"] = QJsonArray{QJsonObject{{"byteLength", (qint64)bin.data.size ()}}};
	}

	QByteArray json = QJsonDocument (root).toJson (QJsonDocument::Compact);
	while (json.size () % 4) json.append (' ');
	uint64_t total = 12 + 8 + (uint64_t)json.size () + (bin.data.empty () ? 0 : 8 + bin.data.size ());
	if (total > 0xffffffffull) { err = "the file would be over 4 GB"; return false; }
	out.clear ();
	out.reserve ((size_t)total);
	auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) out.push_back ((uint8_t)(v >> (8 * i))); };
	u32 (0x46546C67); u32 (2); u32 ((uint32_t)total);
	u32 ((uint32_t)json.size ()); u32 (0x4E4F534A);
	out.insert (out.end (), json.begin (), json.end ());
	if (!bin.data.empty ()) {
		u32 ((uint32_t)bin.data.size ()); u32 (0x004E4942);
		out.insert (out.end (), bin.data.begin (), bin.data.end ());
	}
	return true;
}
