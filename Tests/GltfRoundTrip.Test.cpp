// custom: the glTF round trip in Src/Orbiter/GltfRoundTrip.cpp on synthetic files (claude/design-gltf-roundtrip.md 7a)
#include <catch2/catch_test_macros.hpp>
#include "GltfRoundTrip.h"
#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// a GLB built field by field
struct Gb {
	std::vector<unsigned char> bin;
	QJsonArray views, accs;

	int Add (const void *p, size_t n, int count, const char *type, int ct)
	{
		while (bin.size() % 4) bin.push_back (0);
		views.append (QJsonObject{ { "buffer", 0 }, { "byteOffset", (qint64)bin.size() }, { "byteLength", (qint64)n } });
		bin.insert (bin.end(), (const unsigned char *)p, (const unsigned char *)p + n);
		accs.append (QJsonObject{ { "bufferView", views.size() - 1 }, { "componentType", ct }, { "count", count }, { "type", type } });
		return accs.size() - 1;
	}
	int F (const std::vector<float> &f, int comps) { return Add (f.data(), f.size() * 4, (int)f.size() / comps, comps == 1 ? "SCALAR" : comps == 2 ? "VEC2" : comps == 3 ? "VEC3" : "VEC4", 5126); }
	int I (const std::vector<uint16_t> &i) { return Add (i.data(), i.size() * 2, (int)i.size(), "SCALAR", 5123); }

	std::string Write (const std::string &name, QJsonObject root)
	{
		while (bin.size() % 4) bin.push_back (0);
		root["asset"] = QJsonObject{ { "version", "2.0" } };
		root["bufferViews"] = views;
		root["accessors"] = accs;
		root["buffers"] = QJsonArray{ QJsonObject{ { "byteLength", (qint64)bin.size() } } };
		QByteArray j = QJsonDocument (root).toJson (QJsonDocument::Compact);
		while (j.size() % 4) j.append (' ');
		fs::path d = fs::temp_directory_path () / "orbiter-gltf-rt-test";
		fs::create_directories (d);
		std::string p = (d / name).string ();
		std::ofstream f (p, std::ios::binary);
		auto u32 = [&](unsigned v) { f.write ((const char *)&v, 4); };
		u32 (0x46546C67u); u32 (2); u32 ((unsigned)(12 + 8 + j.size() + 8 + bin.size()));
		u32 ((unsigned)j.size()); u32 (0x4E4F534Au); f.write (j.data(), j.size());
		u32 ((unsigned)bin.size()); u32 (0x004E4942u); f.write ((const char *)bin.data(), bin.size());
		return p;
	}
};

QJsonObject Tag (QJsonObject o) { return QJsonObject{ { "orbiter", o } }; }

// a triangle mesh in glTF axes: Orbiter (x, y, z) at glTF (x, y, -z), winding swapped as the exporter does
int Tri (Gb &g, QJsonArray &meshes, const float p[9], int mat)
{
	std::vector<float> pos, nrm, uv;
	for (int i = 0; i < 3; i++) { pos.push_back (p[i * 3]); pos.push_back (p[i * 3 + 1]); pos.push_back (-p[i * 3 + 2]); nrm.insert (nrm.end(), { 0, 1, 0 }); uv.insert (uv.end(), { (float)i, 0 }); }
	QJsonObject at{ { "POSITION", g.F (pos, 3) }, { "NORMAL", g.F (nrm, 3) }, { "TEXCOORD_0", g.F (uv, 2) } };
	QJsonObject prim{ { "attributes", at }, { "indices", g.I ({ 0, 2, 1 }) }, { "material", mat } };
	meshes.append (QJsonObject{ { "primitives", QJsonArray{ prim } } });
	return meshes.size() - 1;
}

const float A[9] = { 2, 0, 0, 3, 0, 0, 2, 1, 0 }, B[9] = { -2, 0, 1, -3, 0, 1, -2, 1, 1 };

struct Opt {
	double def = 0, angle = 1.0, fileAngle = 1.0;
	double pivot[3] = { 1, 0, 0 };       // glTF translation of the pivot node
	double staticRot[4] = { 0, 0, 0, 1 }; // what Blender leaves on an animated node
	bool newGroup = false;
};

// one recorded rotation about (1, 0, 0) Orbiter around +y on group 0; group 1 static
std::string Ship (const std::string &name, const Opt &o)
{
	Gb g;
	QJsonArray meshes, nodes, mats, anims;
	QJsonObject vals{ { "base", QJsonArray{ 1, 1, 1, 1 } }, { "metallic", 0 }, { "roughness", 1 }, { "emissive", QJsonArray{ 0, 0, 0 } }, { "alphaMode", "OPAQUE" } };
	mats.append (QJsonObject{ { "name", "hull" }, { "pbrMetallicRoughness", QJsonObject{ { "metallicFactor", 0 } } },
		{ "extras", Tag (QJsonObject{ { "mesh", 0 }, { "mtrl", 0 }, { "tex", -1 }, { "unlit", false }, { "maps", 0 }, { "values", vals } }) } });
	int m0 = Tri (g, meshes, A, 0), m1 = Tri (g, meshes, B, 0);
	nodes.append (QJsonObject{ { "name", "Exterior" }, { "children", QJsonArray{ 1 } } });
	nodes.append (QJsonObject{ { "name", "ship" }, { "children", QJsonArray{ 2, 4 } }, { "extras", Tag (QJsonObject{ { "mesh", 0 } }) } });
	QJsonArray pk{ 3 };
	if (o.newGroup) pk.append (5);
	nodes.append (QJsonObject{ { "name", "pivot 0.0" }, { "children", pk }, { "translation", QJsonArray{ o.pivot[0], o.pivot[1], o.pivot[2] } },
		{ "rotation", QJsonArray{ o.staticRot[0], o.staticRot[1], o.staticRot[2], o.staticRot[3] } }, { "extras", Tag (QJsonObject{ { "anim", 0 }, { "component", 0 } }) } });
	nodes.append (QJsonObject{ { "name", "Wing" }, { "mesh", m0 }, { "translation", QJsonArray{ -o.pivot[0], -o.pivot[1], -o.pivot[2] } },
		{ "extras", Tag (QJsonObject{ { "mesh", 0 }, { "group", 0 }, { "chain", QJsonArray{ QJsonArray{ 0, 0 } } } }) } });
	nodes.append (QJsonObject{ { "name", "Body" }, { "mesh", m1 }, { "extras", Tag (QJsonObject{ { "mesh", 0 }, { "group", 1 }, { "chain", QJsonArray () } }) } });
	if (o.newGroup) nodes.append (QJsonObject{ { "name", "Antenna" }, { "mesh", m1 }, { "translation", QJsonArray{ -o.pivot[0], -o.pivot[1], -o.pivot[2] } } });
	std::vector<float> t, q;
	for (int k = 0; k <= 4; k++) {
		double s = k / 4.0, qq[4];
		RtQuatGltf (RtVec{ 0, 1, 0 }, o.fileAngle * (s - o.def), qq);
		t.push_back ((float)s);
		for (double x : qq) q.push_back ((float)x);
	}
	anims.append (QJsonObject{ { "name", "0 Wing" }, { "samplers", QJsonArray{ QJsonObject{ { "input", g.F (t, 1) }, { "output", g.F (q, 4) } } } },
		{ "channels", QJsonArray{ QJsonObject{ { "sampler", 0 }, { "target", QJsonObject{ { "node", 2 }, { "path", "rotation" } } } } } } });
	QJsonObject comp{ { "state0", 0 }, { "state1", 1 }, { "type", "rotate" }, { "mesh", 0 }, { "groups", QJsonArray{ 0 } },
		{ "ref", QJsonArray{ 1, 0, 0 } }, { "axis", QJsonArray{ 0, 1, 0 } }, { "angle", o.angle } };
	QJsonObject mrec{ { "index", 0 }, { "name", "rt\\ship" }, { "offset", QJsonArray{ 0, 0, 0 } }, { "visibility", 1 }, { "groups", 2 },
		{ "skipped", QJsonArray () }, { "flags", QJsonArray{ 0, 0 } },
		{ "materials", QJsonArray{ QJsonObject{ { "diffuse", QJsonArray{ 1, 1, 1, 1 } }, { "ambient", QJsonArray{ 1, 1, 1, 1 } }, { "specular", QJsonArray{ 0, 0, 0, 1 } }, { "emissive", QJsonArray{ 0, 0, 0, 1 } }, { "power", 0 } } } },
		{ "textures", QJsonArray () } };
	QJsonObject rec{ { "version", 2 }, { "vessel", "RT-1" }, { "class", "RT" }, { "meshes", QJsonArray{ mrec } },
		{ "animations", QJsonArray{ QJsonObject{ { "defstate", o.def }, { "time0", 0 }, { "components", QJsonArray{ comp } } } } } };
	QJsonObject root{ { "scene", 0 }, { "scenes", QJsonArray{ QJsonObject{ { "nodes", QJsonArray{ 0 } }, { "extras", Tag (rec) } } } },
		{ "nodes", nodes }, { "meshes", meshes }, { "materials", mats }, { "animations", anims } };
	return g.Write (name, root);
}

RtFile Load (const std::string &p)
{
	RtFile f;
	std::string err;
	bool ok = RtLoad (p, f, err);
	INFO (err);
	REQUIRE (ok);
	return f;
}

NTVERTEX V (float x, float y, float z, float u) { NTVERTEX v{ x, y, z, 0, 1, 0, u, 0 }; return v; }

} // namespace

TEST_CASE ("glTF round trip: names and records", "[gltfrt]")
{
	CHECK (RtAnimIndex ("11 glider4-1") == 11);
	CHECK (RtAnimIndex ("11") == 11);
	CHECK (RtAnimIndex ("11 glider4-1.001") == 11);
	CHECK (RtAnimIndex ("x11") == -1);
	CHECK (RtAnimIndex ("11x") == -1);
	CHECK (RtSameMeshName ("dg-xr1\\DeltaGlider-XR1", "DG-XR1/deltaglider-xr1"));
	CHECK (!RtSameMeshName ("dg-xr1\\a", "dg-xr1\\b"));
	RtRecord r;
	std::string err;
	CHECK (!RtParseRecord ("{\"orbiter\":{\"version\":1}}", r, err));
	CHECK (err.find ("version 1") != std::string::npos);
	CHECK (!RtParseRecord ("", r, err));
	std::vector<std::string> names;
	CHECK (RtListedMeshes ("{\"scene\":0,\"scenes\":[{\"extras\":{\"orbiter\":{\"version\":2,\"meshes\":[{\"name\":\"a\\\\b\"}]}}}]}", names));
	REQUIRE (names.size() == 1);
	CHECK (names[0] == "a\\b");
}

TEST_CASE ("glTF round trip: an untouched file stays with the program", "[gltfrt]")
{
	Opt o;
	o.staticRot[0] = std::sin (0.7); o.staticRot[3] = std::cos (0.7); // Blender's frame 0 of another action: the default pose comes from the channel
	RtFile f = Load (Ship ("plain.glb", o));
	REQUIRE (f.plans.size() == 1);
	const RtMeshPlan &P = f.plans[0];
	CHECK (P.replaced.empty ());
	CHECK (P.comps.empty ());
	REQUIRE (P.primary.count (0));
	REQUIRE (P.primary.count (1));
	CHECK (P.nadded == 0);
	NTVERTEX a[3] = { V (2, 0, 0, 0), V (3, 0, 0, 1), V (2, 1, 0, 2) };
	WORD idx[3] = { 0, 1, 2 }, rot[3] = { 1, 2, 0 };
	std::vector<const RtOut*> parts{ &P.outs[P.primary.at (0)] };
	CHECK (RtSameGeometry (parts, RtBaseGroup{ a, 3, idx, 3 }));
	CHECK (RtSameGeometry (parts, RtBaseGroup{ a, 3, rot, 3 })); // another start vertex
	NTVERTEX b[3] = { V (2, 0, 0, 0), V (3, 0, 0.001f, 1), V (2, 1, 0, 2) };
	CHECK (!RtSameGeometry (parts, RtBaseGroup{ b, 3, idx, 3 }));
	NTVERTEX c[4] = { V (2, 0, 0, 0), V (3, 0, 0, 1), V (2, 1, 0, 2), V (5, 5, 5, 0) };
	WORD deg[6] = { 0, 1, 2, 3, 3, 3 }, dup[6] = { 0, 1, 2, 1, 2, 0 }, odd[6] = { 0, 1, 2, 0, 1, 3 };
	CHECK (RtSameGeometry (parts, RtBaseGroup{ c, 4, deg, 6 })); // Blender drops a zero-area triangle
	CHECK (RtSameGeometry (parts, RtBaseGroup{ c, 4, dup, 6 })); // and a repeated one
	CHECK (!RtSameGeometry (parts, RtBaseGroup{ c, 4, odd, 6 }));
}

TEST_CASE ("glTF round trip: an edited animation is taken from the file", "[gltfrt]")
{
	SECTION ("angle") {
		Opt o;
		o.fileAngle = 2.0;
		RtFile f = Load (Ship ("angle.glb", o));
		const RtMeshPlan &P = f.plans[0];
		REQUIRE (P.replaced == std::vector<int>{ 0 });
		REQUIRE (P.comps.size() == 1);
		const RtCompSpec &c = P.comps[0];
		CHECK (c.type == RT_ROTATE);
		CHECK (std::fabs (c.state0) < 1e-9);
		CHECK (std::fabs (c.state1 - 1) < 1e-9);
		CHECK (std::fabs (c.axis.y * c.angle - 2.0) < 1e-5);
		CHECK (std::fabs (c.ref.x - 1) < 1e-6);
		CHECK (c.groups == std::vector<int>{ 0 });
		CHECK (c.pivotAnim == 0);
		CHECK (P.fitted == std::vector<int>{ 0 });
	}
	SECTION ("below the default state") {
		Opt o;
		o.def = 1; o.fileAngle = -1.5;
		RtFile f = Load (Ship ("below.glb", o));
		const RtMeshPlan &P = f.plans[0];
		REQUIRE (P.comps.size() == 1);
		const RtCompSpec &c = P.comps[0];
		CHECK (std::fabs (c.state0) < 1e-9);
		CHECK (std::fabs (c.state1 - 1) < 1e-9);
		// Orbiter applies angle (f - 1); at state 0 the file turns -1.5 * (0 - 1) about +y
		CHECK (std::fabs (c.axis.y * c.angle * (0 - 1) - 1.5) < 1e-5);
	}
	SECTION ("hinge moved") {
		Opt o;
		o.pivot[0] = 1.5;
		RtFile f = Load (Ship ("hinge.glb", o));
		const RtMeshPlan &P = f.plans[0];
		REQUIRE (P.comps.size() == 1);
		CHECK (std::fabs (P.comps[0].ref.x - 1.5) < 1e-6);
		CHECK (std::fabs (P.comps[0].axis.y * P.comps[0].angle - 1.0) < 1e-5);
	}
	SECTION ("a new part on the hinge") {
		Opt o;
		o.newGroup = true;
		RtFile f = Load (Ship ("new.glb", o));
		const RtMeshPlan &P = f.plans[0];
		CHECK (P.replaced == std::vector<int>{ 0 });
		CHECK (P.nadded == 1);
		REQUIRE (P.comps.size() == 1);
		CHECK (P.fitted.empty ()); // the pivot itself is untouched: its recorded component as it is
		CHECK (P.comps[0].groups == std::vector<int>{ 0, -1 });
		CHECK (P.comps[0].angle == 1.0);
	}
}

TEST_CASE ("glTF round trip: materials and image hashes", "[gltfrt]")
{
	QJsonObject vals{ { "base", QJsonArray{ 1, 1, 1, 1 } }, { "metallic", 0 }, { "roughness", 0.5 }, { "emissive", QJsonArray{ 0, 0, 0 } }, { "alphaMode", "MASK" }, { "alphaCutoff", 0.5 } };
	QJsonObject tag{ { "orbiter", QJsonObject{ { "mesh", 1 }, { "mtrl", 3 }, { "tex", 2 }, { "unlit", false }, { "maps", 1 }, { "values", vals } } } };
	auto json = [&](QJsonObject m) { m["extras"] = tag; return QJsonDocument (m).toJson (QJsonDocument::Compact).toStdString (); };
	QJsonObject blender{ { "alphaMode", "MASK" }, { "pbrMetallicRoughness", QJsonObject{ { "metallicFactor", 0 }, { "roughnessFactor", 0.50000001 }, { "baseColorTexture", QJsonObject{ { "index", 0 } } } } } };
	RtMatTag t = RtMaterialTag (json (blender));
	CHECK (t.tagged);
	CHECK (t.mesh == 1);
	CHECK (t.mtrl == 3);
	CHECK (t.tex == 2);
	CHECK (!t.edited);
	QJsonObject red = blender;
	QJsonObject pmr = red["pbrMetallicRoughness"].toObject ();
	pmr["baseColorFactor"] = QJsonArray{ 1, 0, 0, 1 };
	red["pbrMetallicRoughness"] = pmr;
	CHECK (RtMaterialTag (json (red)).edited);
	QJsonObject nomap = blender;
	pmr = nomap["pbrMetallicRoughness"].toObject ();
	pmr.remove ("baseColorTexture");
	nomap["pbrMetallicRoughness"] = pmr;
	CHECK (RtMaterialTag (json (nomap)).edited);
	QJsonObject blend = blender;
	blend["alphaMode"] = "BLEND";
	CHECK (!RtMaterialTag (json (blend)).edited); // Blender swaps BLEND and MASK by itself
	QJsonObject opaque = blender;
	opaque.remove ("alphaMode");
	CHECK (RtMaterialTag (json (opaque)).edited);
	CHECK (!RtMaterialTag ("{\"name\":\"x\"}").tagged);

	QImage img (3, 2, QImage::Format_RGBA8888);
	img.fill (QColor (10, 20, 30, 255));
	img.setPixelColor (1, 1, QColor (200, 100, 50, 128));
	QByteArray png;
	QBuffer buf (&png);
	buf.open (QIODevice::WriteOnly);
	REQUIRE (img.save (&buf, "PNG"));
	std::vector<unsigned char> bytes (png.begin(), png.end());
	std::vector<unsigned char> px (3 * 2 * 4);
	for (int y = 0; y < 2; y++) memcpy (&px[y * 12], img.constScanLine (y), 12);
	CHECK (RtImageHash (bytes) == RtPixelHash (3, 2, px.data()));
	CHECK (RtPixelHash (3, 2, px.data()).size() == 16);
	px[0] ^= 1;
	CHECK (RtImageHash (bytes) != RtPixelHash (3, 2, px.data()));
	CHECK (RtImageHash ({ 1, 2, 3 }).empty ());
}
