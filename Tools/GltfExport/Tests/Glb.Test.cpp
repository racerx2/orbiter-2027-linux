// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: a written file read back by the repo's own glTF reader (design 10)

#include <catch2/catch_test_macros.hpp>
#include "DdsDecode.h"
#include "Exporter.h"
#include "GlbWriter.h"
#include "GltfLoader.h"
#include "GltfRoundTrip.h"
#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace {

void WriteBc1 (const fs::path &p, uint16_t c0, uint16_t c1)
{
	std::vector<uint8_t> b (128, 0);
	auto put = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> (8 * i)); };
	memcpy (b.data (), "DDS ", 4);
	put (4, 124); put (8, 0x1007); put (12, 4); put (16, 4); put (76, 32); put (80, 4);
	memcpy (&b[84], "DXT1", 4);
	uint8_t blk[8] = {(uint8_t)(c0 & 255), (uint8_t)(c0 >> 8), (uint8_t)(c1 & 255), (uint8_t)(c1 >> 8), 0, 0, 0, 0};
	b.insert (b.end (), blk, blk + 8);
	fs::create_directories (p.parent_path ());
	std::ofstream (p, std::ios::binary).write ((const char*)b.data (), b.size ());
}

GxGroup Tri (float x0, int mtrl, int tex, uint32_t flag)
{
	GxGroup g;
	g.vtx = {{{x0, 0, 0}, {0, 0, -1}, {0, 0}}, {{x0 + 1, 0, 0}, {0, 0, -1}, {1, 0}}, {{x0, 1, 0.5f}, {0, 0, -2}, {0, 1}}};
	g.idx = {0, 1, 2};
	g.mtrl = mtrl; g.tex = tex; g.usrFlag = flag;
	return g;
}

QJsonObject GlbJson (const std::string &path)
{
	std::ifstream f (path, std::ios::binary);
	std::vector<char> b ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
	REQUIRE (b.size () > 20);
	uint32_t n;
	memcpy (&n, &b[12], 4);
	return QJsonDocument::fromJson (QByteArray (&b[20], (int)n)).object ();
}

QImage GlbImage (const std::string &path, const QJsonObject &j, int texture) // a texture's image, RGBA8888
{
	std::ifstream f (path, std::ios::binary);
	std::vector<char> b ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
	uint32_t n;
	memcpy (&n, &b[12], 4);
	size_t bin = 20 + n + 8;
	int im = j["textures"].toArray ()[texture].toObject ()["source"].toInt ();
	QJsonObject v = j["bufferViews"].toArray ()[j["images"].toArray ()[im].toObject ()["bufferView"].toInt ()].toObject ();
	QImage q;
	q.loadFromData ((const uchar*)&b[bin + v["byteOffset"].toInteger ()], (int)v["byteLength"].toInteger ());
	return q.convertToFormat (QImage::Format_RGBA8888);
}

GxImage Dds (const fs::path &p)
{
	std::ifstream f (p, std::ios::binary);
	std::vector<uint8_t> b ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
	GxImage img;
	std::string err;
	REQUIRE (GxDecodeDds (b.data (), b.size (), img, err));
	return img;
}

}

TEST_CASE ("A synthetic vessel: written, read back by GltfLoader, fields in place", "[Glb]")
{
	fs::path dir = fs::temp_directory_path () / "gltfx_glb_test";
	fs::remove_all (dir);
	WriteBc1 (dir / "Textures" / "T" / "a.dds", 0xF800, 0x001F);
	WriteBc1 (dir / "Textures" / "T" / "a_norm.dds", 0x8410, 0x8410);
	fs::create_directories (dir / "Meshes");
	std::ofstream (dir / "Meshes" / "test.msh") <<
		"MSHX1\nGROUPS 3\nLABEL Wing\nGEOM 3 1\n0 0 0 0 0 -1\n1 0 0 0 0 -1\n0 1 0.5 0 0 -1\n0 1 2\n"
		"LABEL Lamp\nGEOM 3 1\n0 0 0\n1 0 0\n0 1 0\n0 1 2\nLABEL Ghost\nGEOM 3 1\n0 0 0\n1 0 0\n0 1 0\n0 1 2\n"
		"MATERIALS 2\nhull\nlamp\nMATERIAL hull\n1 1 1 1\n1 1 1 1\n0 0 0 1\n0 0 0 1\nMATERIAL lamp\n1 0.5 0 1\n1 1 1 1\n0 0 0 1\n1 1 0 1\n"
		"TEXTURES 1\nT\\a.dds\n";

	GxSnapshot s;
	s.vesselName = "Test-1"; s.className = "Tester";
	s.texDirs = {(dir / "Textures2").string () + "/", (dir / "Textures").string () + "/"};
	GxMesh m;
	m.name = "test"; m.file = (dir / "Meshes" / "test.msh").string (); m.index = 0; m.vis = 1; m.ofs = {1, 2, 3}; m.ntex = 1;
	m.mtrls = {GxMaterial{{1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, 0}, GxMaterial{{1, 0.5f, 0, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {1, 1, 0, 1}, 0}};
	m.groups = {Tri (0, 0, 0, 0), Tri (3, 1, -1, 0x4), Tri (6, 0, -1, 0x2)};
	s.meshes.push_back (m);
	GxMesh vc = m;
	vc.name = ""; vc.file = ""; vc.index = 1; vc.vis = 4; vc.ofs = {0, 0, 0}; vc.groups = {Tri (9, -1, -1, 0)};
	s.meshes.push_back (vc);
	GxAnim a; a.defstate = a.state = 0; a.comps = {0};
	GxComp c; c.type = GxType::Rotate; c.mesh = 0; c.groups = {0}; c.ref = {0, 0, 0}; c.axis = {0, 1, 0}; c.angle = 1.5;
	s.anims.anims.push_back (a);
	s.anims.comps.push_back (c);

	auto resolve = [](const std::string &p) { std::string r = p; for (char &ch : r) if (ch == '\\') ch = '/'; return r; };
	GxResult r = GxExportFile (s, (dir / "Exports" / "Test-1.glb").string (), resolve, 2);
	INFO (r.error);
	REQUIRE (r.ok);
	CHECK (r.parts == 3);
	CHECK (r.anims == 1);
	CHECK (r.images == 2);

	GltfModel g;
	std::string err;
	REQUIRE (GltfRead (r.path, g, err));
	REQUIRE (g.groups.size () == 3); // the hidden group stays out
	const float x0[3] = {0, 3, 9};
	const GxVec3 ofs[3] = {{1, 2, 3}, {1, 2, 3}, {0, 0, 0}};
	for (int k = 0; k < 3; k++) {
		const GltfGroup &G = g.groups[k];
		REQUIRE (G.vtx.size () == 3);
		REQUIRE (G.idx == std::vector<WORD>{0, 1, 2});
		CHECK (std::fabs (G.vtx[0].x - (x0[k] + ofs[k].x)) < 1e-5);
		CHECK (std::fabs (G.vtx[2].z - (0.5 + ofs[k].z)) < 1e-5);
		CHECK (std::fabs (G.vtx[2].tv - 1) < 1e-6);
		CHECK (std::fabs (G.vtx[1].nz + 1) < 1e-6);
	}

	QJsonObject j = GlbJson (r.path);
	CHECK (j["animations"].toArray ().size () == 1);
	CHECK (j["animations"].toArray ()[0].toObject ()["name"].toString () == "0 Wing");
	CHECK (j["extensionsUsed"].toArray ().contains ("KHR_materials_unlit"));
	CHECK (j["images"].toArray ().size () == 2);
	CHECK (!j["asset"].toObject ().contains ("extras")); // Blender drops asset extras: version 2 keeps them on the scene
	QJsonObject ex = j["scenes"].toArray ()[0].toObject ()["extras"].toObject ()["orbiter"].toObject ();
	CHECK (ex["version"].toInt () == 2);
	CHECK (ex["class"].toString () == "Tester");
	CHECK (ex["animations"].toArray ()[0].toObject ()["components"].toArray ()[0].toObject ()["angle"].toDouble () == 1.5);
	CHECK (ex["animations"].toArray ()[0].toObject ()["time0"].toDouble () == 0);
	QJsonObject m0 = ex["meshes"].toArray ()[0].toObject ();
	CHECK (m0["skipped"].toArray () == QJsonArray{2});
	CHECK (m0["flags"].toArray () == QJsonArray{0, 4, 2});
	CHECK (m0["materials"].toArray ().size () == 2);
	QJsonObject t0 = m0["textures"].toArray ()[0].toObject ();
	CHECK (t0["name"].toString () == "T\\a.dds");
	CHECK (t0["hash"].toString ().size () == 16);
	CHECK (!t0["dynamic"].toBool ());
	bool chain = false, meshNode = false;
	for (auto nv : j["nodes"].toArray ()) {
		QJsonObject o = nv.toObject ()["extras"].toObject ()["orbiter"].toObject ();
		if (o.contains ("group") && o["group"].toInt () == 0 && o["mesh"].toInt () == 0) chain = o["chain"].toArray () == QJsonArray{QJsonArray{0, 0}};
		if (o.contains ("mesh") && !o.contains ("group") && o["mesh"].toInt () == 1) meshNode = true;
	}
	CHECK (chain);
	CHECK (meshNode);
	int tagged = 0;
	for (auto mv : j["materials"].toArray ()) {
		QJsonObject o = mv.toObject ()["extras"].toObject ()["orbiter"].toObject ();
		if (o.contains ("values") && o.contains ("maps")) tagged++;
	}
	CHECK (tagged == (int)j["materials"].toArray ().size ());
	for (auto mv : j["materials"].toArray ()) CHECK (!RtMaterialTag (QJsonDocument (mv.toObject ()).toJson (QJsonDocument::Compact).toStdString ()).edited);
	bool normal = false, lampUnlit = false, lampGlow = false;
	for (auto mv : j["materials"].toArray ()) {
		QJsonObject mo = mv.toObject ();
		if (mo.contains ("normalTexture")) normal = true;
		if (mo["name"].toString () == "lamp") {
			lampUnlit = mo["extensions"].toObject ().contains ("KHR_materials_unlit");
			lampGlow = mo["pbrMetallicRoughness"].toObject ()["baseColorFactor"].toArray () == QJsonArray{1, 1, 0, 1} && !mo.contains ("emissiveFactor"); // fully lit: clamp (diffuse + emissive)
		}
	}
	CHECK (normal);
	CHECK (lampUnlit);
	CHECK (lampGlow);
	QJsonArray nodes = j["nodes"].toArray ();
	bool wing = false;
	for (auto nv : nodes) if (nv.toObject ()["name"].toString () == "Wing") wing = true;
	CHECK (wing);
	if (!getenv ("GLTFX_KEEP")) fs::remove_all (dir);
}

TEST_CASE ("Glow as Orbiter draws it", "[Glb]")
{
	fs::path dir = fs::temp_directory_path () / "gltfx_glow_test";
	fs::remove_all (dir);
	WriteBc1 (dir / "Textures" / "T" / "p.dds", 0x8410, 0x8410);
	WriteBc1 (dir / "Textures" / "T" / "q.dds", 0xF800, 0xF800);
	WriteBc1 (dir / "Textures" / "T" / "q_emis.dds", 0x001F, 0x001F);
	fs::create_directories (dir / "Meshes");
	std::ofstream (dir / "Meshes" / "glow.msh") <<
		"MSHX1\nGROUPS 0\nMATERIALS 2\nglow\ndark\nMATERIAL glow\n0.6 0.6 0.6 1\n1 1 1 1\n0 0 0 1\n0.5 0.5 0.5 1\nMATERIAL dark\n1 1 1 1\n1 1 1 1\n0 0 0 1\n0 0 0 1\n"
		"TEXTURES 2\nT\\p.dds\nT\\q.dds\n";

	GxSnapshot s;
	s.vesselName = "Glow-1";
	s.texDirs = {(dir / "Textures").string () + "/"};
	GxMesh m;
	m.name = "glow"; m.file = (dir / "Meshes" / "glow.msh").string (); m.index = 0; m.vis = 1; m.ntex = 2;
	m.mtrls = {GxMaterial{{0.6f, 0.6f, 0.6f, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {0.5f, 0.5f, 0.5f, 1}, 0}, GxMaterial{{1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}, 0}};
	m.groups = {Tri (0, 0, 0, 0), Tri (2, 0, -1, 0), Tri (4, 0, 0, 0x10), Tri (6, 0, 1, 0), Tri (8, 1, 1, 0), Tri (10, 0, 0, 0x4)};
	s.meshes.push_back (m);

	auto resolve = [](const std::string &p) { std::string r = p; for (char &ch : r) if (ch == '\\') ch = '/'; return r; };
	GxResult r = GxExportFile (s, (dir / "Exports" / "Glow-1.glb").string (), resolve, 2);
	INFO (r.error);
	REQUIRE (r.ok);
	CHECK (r.images == 4); // p, q, q_emis and one glow image

	QJsonObject j = GlbJson (r.path);
	QJsonArray mats = j["materials"].toArray (), meshes = j["meshes"].toArray ();
	std::map<int, QJsonObject> mg; // group -> its material
	for (auto nv : j["nodes"].toArray ()) {
		QJsonObject n = nv.toObject (), o = n["extras"].toObject ()["orbiter"].toObject ();
		if (n.contains ("mesh") && o.contains ("group")) mg[o["group"].toInt ()] = mats[meshes[n["mesh"].toInt ()].toObject ()["primitives"].toArray ()[0].toObject ()["material"].toInt ()].toObject ();
	}
	REQUIRE (mg.size () == 6);
	auto emisTex = [](const QJsonObject &mo) { return mo.contains ("emissiveTexture") ? mo["emissiveTexture"].toObject ()["index"].toInt () : -1; };
	auto baseTex = [](const QJsonObject &mo) { QJsonObject p = mo["pbrMetallicRoughness"].toObject (); return p.contains ("baseColorTexture") ? p["baseColorTexture"].toObject ()["index"].toInt () : -1; };
	auto factor = [](const QJsonObject &mo) { QJsonArray a = mo["emissiveFactor"].toArray (); return a.size () == 3 ? a[0].toDouble () : -1.0; };
	auto vals = [](const QJsonObject &mo) { return mo["extras"].toObject ()["orbiter"].toObject (); };
	const double lin = GxSrgbToLinear (0.5);

	// lit, textured: the base image glows by the emissive colour
	CHECK (baseTex (mg[0]) >= 0);
	CHECK (emisTex (mg[0]) == baseTex (mg[0]));
	CHECK (std::fabs (factor (mg[0]) - lin) < 1e-9);
	CHECK (vals (mg[0])["values"].toObject ()["emissive"].toArray () == mg[0]["emissiveFactor"].toArray ());
	CHECK ((vals (mg[0])["maps"].toInt () & 5) == 5);
	// untextured, and alpha-only texture (0x10): a flat glow
	CHECK (emisTex (mg[1]) == -1);
	CHECK (std::fabs (factor (mg[1]) - lin) < 1e-9);
	CHECK (baseTex (mg[2]) >= 0);
	CHECK (emisTex (mg[2]) == -1);
	CHECK (std::fabs (factor (mg[2]) - lin) < 1e-9);
	// an _emis map and an emissive colour: one image, max (emis, base * emissive)
	int g3 = emisTex (mg[3]), g4 = emisTex (mg[4]);
	CHECK (g3 >= 0);
	CHECK (g3 != baseTex (mg[3]));
	CHECK (g3 != g4);
	CHECK (factor (mg[3]) == 1);
	GxImage qb = Dds (dir / "Textures" / "T" / "q.dds"), qe = Dds (dir / "Textures" / "T" / "q_emis.dds");
	QImage gi = GlbImage (r.path, j, g3), ei = GlbImage (r.path, j, g4);
	REQUIRE (gi.width () == qb.w);
	REQUIRE (gi.height () == qb.h);
	int bad = 0;
	for (int y = 0; y < gi.height (); y++)
		for (int x = 0; x < gi.width (); x++) {
			const uchar *o = gi.constScanLine (y) + x * 4;
			const uint8_t *b = &qb.rgba[((size_t)y * qb.w + x) * 4], *e = &qe.rgba[((size_t)y * qe.w + x) * 4];
			for (int c = 0; c < 3; c++) if (std::abs ((int)o[c] - (int)std::lround (std::max (e[c] * 1.0, b[c] * 0.5))) > 1) bad++;
			if (o[3] != 255) bad++;
		}
	CHECK (bad == 0);
	CHECK ((int)gi.constScanLine (0)[2] == 255); // the _emis blue wins in blue, the base's red at half in red
	CHECK (std::abs ((int)gi.constScanLine (0)[0] - 128) <= 1);
	// an _emis map without an emissive colour: the map as it is
	CHECK (factor (mg[4]) == 1);
	CHECK (ei.width () == qe.w);
	CHECK ((int)ei.constScanLine (0)[2] == (int)qe.rgba[2]);
	CHECK ((int)ei.constScanLine (0)[0] == (int)qe.rgba[0]);
	// fully lit: the glow in the base colour
	CHECK (mg[5]["extensions"].toObject ().contains ("KHR_materials_unlit"));
	CHECK (mg[5]["pbrMetallicRoughness"].toObject ()["baseColorFactor"].toArray () == QJsonArray{1, 1, 1, 1});
	CHECK (!mg[5].contains ("emissiveFactor"));
	CHECK (emisTex (mg[5]) == -1);
	CHECK ((vals (mg[5])["maps"].toInt () & 4) == 0);
	for (auto mv : mats) { // the record says what was written: the core reads every material back untouched
		RtMatTag t = RtMaterialTag (QJsonDocument (mv.toObject ()).toJson (QJsonDocument::Compact).toStdString ());
		CHECK (t.tagged);
		CHECK (!t.edited);
	}
	if (!getenv ("GLTFX_KEEP")) fs::remove_all (dir);
}
