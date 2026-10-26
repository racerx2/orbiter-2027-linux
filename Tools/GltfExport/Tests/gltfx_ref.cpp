// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport check tool: the client's animation code on a .glb's own extras and rest vertices; expected positions for Blender
// usage: gltfx_ref <file.glb> <out.json>

#include "OrbiterRef.h"
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

static GxVec3 V3 (const QJsonValue &v) { QJsonArray a = v.toArray (); return {a[0].toDouble (), a[1].toDouble (), a[2].toDouble ()}; }

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "usage: gltfx_ref <file.glb> <out.json>\n"); return 2; }
	QFile f (argv[1]);
	if (!f.open (QIODevice::ReadOnly)) { fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }
	QByteArray b = f.readAll ();
	uint32_t jl, bl;
	memcpy (&jl, b.constData () + 12, 4);
	QJsonObject j = QJsonDocument::fromJson (b.mid (20, jl)).object ();
	memcpy (&bl, b.constData () + 20 + jl, 4);
	const char *bin = b.constData () + 20 + jl + 8;
	QJsonArray acc = j["accessors"].toArray (), views = j["bufferViews"].toArray (), meshes = j["meshes"].toArray (), nodes = j["nodes"].toArray ();
	QJsonObject ex = j["asset"].toObject ()["extras"].toObject ()["orbiter"].toObject ();

	std::map<int, GxVec3> ofs;
	std::map<int, int> ngroups;
	for (auto mv : ex["meshes"].toArray ()) {
		QJsonObject m = mv.toObject ();
		ofs[m["index"].toInt ()] = V3 (m["offset"]);
		ngroups[m["index"].toInt ()] = m["groups"].toInt ();
	}
	int maxMesh = 0;
	for (auto &o : ofs) maxMesh = std::max (maxMesh, o.first);

	RefVessel ref;
	ref.v.resize (maxMesh + 1);
	for (auto &g : ngroups) ref.v[g.first].resize (g.second);
	std::map<std::pair<int, int>, QString> nodeName;
	for (auto nv : nodes) {
		QJsonObject n = nv.toObject ();
		if (!n.contains ("mesh")) continue;
		QJsonObject o = n["extras"].toObject ()["orbiter"].toObject ();
		int mi = o["mesh"].toInt (), g = o["group"].toInt ();
		QJsonObject a = acc[meshes[n["mesh"].toInt ()].toObject ()["primitives"].toArray ()[0].toObject ()["attributes"].toObject ()["POSITION"].toInt ()].toObject ();
		QJsonObject bv = views[a["bufferView"].toInt ()].toObject ();
		const float *p = (const float*)(bin + bv["byteOffset"].toInteger ());
		int cnt = a["count"].toInt ();
		std::vector<GxVec3> pts;
		GxVec3 off = ofs[mi];
		for (int k = 0; k < cnt; k++) pts.push_back ({p[k * 3] - off.x, p[k * 3 + 1] - off.y, -p[k * 3 + 2] - off.z}); // glTF to Orbiter, mesh local
		ref.v[mi][g] = pts;
		nodeName[{mi, g}] = n["name"].toString ();
	}

	std::map<std::pair<int, int>, int> idOf;
	QJsonArray anims = ex["animations"].toArray ();
	for (int a = 0; a < anims.size (); a++) {
		QJsonObject A = anims[a].toObject ();
		GxAnim an;
		an.defstate = A["defstate"].toDouble ();
		an.state = an.defstate;
		QJsonArray comps = A["components"].toArray ();
		for (int c = 0; c < comps.size (); c++) {
			QJsonObject q = comps[c].toObject ();
			GxComp k;
			k.anim = a; k.index = c;
			k.state0 = q["state0"].toDouble (); k.state1 = q["state1"].toDouble ();
			QString t = q["type"].toString ();
			k.type = t == "rotate" ? GxType::Rotate : t == "translate" ? GxType::Translate : t == "scale" ? GxType::Scale : GxType::Null;
			k.mesh = q["mesh"].toInt ();
			if (q["groups"].isString ()) k.allGroups = true;
			else for (auto g : q["groups"].toArray ()) k.groups.push_back (g.toInt ());
			if (q.contains ("ref")) k.ref = V3 (q["ref"]);
			if (q.contains ("axis")) { // unit, as the export makes rotations rigid (a 0.04% long axis skews by mm in Orbiter itself)
				k.axis = V3 (q["axis"]);
				double l = std::sqrt (k.axis.x * k.axis.x + k.axis.y * k.axis.y + k.axis.z * k.axis.z);
				if (l > 0) { k.axis.x /= l; k.axis.y /= l; k.axis.z /= l; }
			}
			if (q.contains ("angle")) k.angle = q["angle"].toDouble ();
			if (q.contains ("shift")) k.shift = V3 (q["shift"]);
			if (q.contains ("scale")) k.scale = V3 (q["scale"]);
			if (q.contains ("parent")) k.parent = -2 - (q["parent"].toArray ()[0].toInt () * 100000 + q["parent"].toArray ()[1].toInt ()); // fixed below
			idOf[{a, c}] = (int)ref.live.comps.size ();
			an.comps.push_back ((int)ref.live.comps.size ());
			ref.live.comps.push_back (k);
		}
		ref.live.anims.push_back (an);
	}
	for (auto &k : ref.live.comps)
		if (k.parent <= -2) {
			int code = -2 - k.parent;
			auto it = idOf.find ({code / 100000, code % 100000});
			k.parent = it == idOf.end () ? -1 : it->second;
		}
	for (size_t c = 0; c < ref.live.comps.size (); c++) if (ref.live.comps[c].parent >= 0) ref.live.comps[ref.live.comps[c].parent].children.push_back ((int)c);

	const RefVessel rest = ref;
	std::map<int, QString> animName;
	for (auto av : j["animations"].toArray ()) {
		QString n = av.toObject ()["name"].toString ();
		animName[n.section (' ', 0, 0).toInt ()] = n;
	}
	QJsonArray checks;
	for (auto &an : animName) {
		int a = an.first;
		double lo = 0, hi = 1;
		for (int c : rest.live.anims[a].comps) { lo = std::min (lo, std::min (rest.live.comps[c].state0, rest.live.comps[c].state1)); hi = std::max (hi, std::max (rest.live.comps[c].state0, rest.live.comps[c].state1)); }
		for (int s = 0; s <= 4; s++) {
			double st = lo + (hi - lo) * s / 4;
			RefVessel r = rest;
			r.Start ();
			r.Animate (a, st);
			QJsonArray moved;
			for (auto &nn : nodeName) {
				int mi = nn.first.first, g = nn.first.second;
				const auto &p0 = rest.v[mi][g], &p1 = r.v[mi][g];
				double d = 0;
				for (size_t k = 0; k < p0.size (); k++) d = std::max (d, std::fabs (p0[k].x - p1[k].x) + std::fabs (p0[k].y - p1[k].y) + std::fabs (p0[k].z - p1[k].z));
				if (d < 1e-6) continue;
				QJsonArray pts;
				GxVec3 off = ofs[mi];
				for (size_t k = 0; k < p1.size () && k < 3; k++) pts.append (QJsonArray{p1[k].x + off.x, p1[k].z + off.z, p1[k].y + off.y}); // Orbiter to Blender
				moved.append (QJsonObject{{"mesh", mi}, {"group", g}, {"name", nn.second}, {"verts", pts}});
			}
			if (moved.isEmpty ()) continue;
			checks.append (QJsonObject{{"anim", a}, {"name", an.second}, {"state", st}, {"time", st - lo}, {"nodes", moved}});
		}
	}
	QFile o (argv[2]);
	if (!o.open (QIODevice::WriteOnly)) return 1;
	o.write (QJsonDocument (QJsonObject{{"checks", checks}}).toJson (QJsonDocument::Indented));
	printf ("%d checks\n", (int)checks.size ());
	return 0;
}
