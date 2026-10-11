// not upstream: the glTF mesh reader in Src/Orbiter/GltfLoader.cpp (claude/design-gltf-loader.md 6)
#include <catch2/catch_test_macros.hpp>
#include "GltfLoader.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string Dir ()
{
	fs::path d = fs::temp_directory_path () / "orbiter-gltf-test";
	fs::create_directories (d);
	return d.string ();
}

// a GLB from a JSON text and one BIN buffer
struct Glb {
	std::vector<unsigned char> bin;
	size_t Add (const void *p, size_t n)
	{
		size_t o = bin.size ();
		bin.insert (bin.end (), (const unsigned char *)p, (const unsigned char *)p + n);
		while (bin.size () % 4) bin.push_back (0);
		return o;
	}
	std::string Write (const std::string &name, std::string json)
	{
		while (json.size () % 4) json += ' ';
		std::string p = Dir () + "/" + name;
		std::ofstream f (p, std::ios::binary);
		auto u32 = [&](unsigned v) { f.write ((const char *)&v, 4); };
		u32 (0x46546C67u); u32 (2); u32 ((unsigned)(12 + 8 + json.size () + (bin.empty () ? 0 : 8 + bin.size ())));
		u32 ((unsigned)json.size ()); u32 (0x4E4F534Au); f.write (json.data (), json.size ());
		if (!bin.empty ()) { u32 ((unsigned)bin.size ()); u32 (0x004E4942u); f.write ((const char *)bin.data (), bin.size ()); }
		return p;
	}
};

static const float Tri[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };

// one triangle mesh, its node JSON given; positions at view 0, indices (u8) at view 1
static std::string TriGlb (const std::string &name, const std::string &node, const std::string &material = "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.5,0.5,1]}}", const std::string &ext = "")
{
	Glb g;
	size_t po = g.Add (Tri, sizeof Tri);
	unsigned char idx[3] = { 0, 1, 2 };
	size_t io = g.Add (idx, 3);
	char j[2048];
	snprintf (j, sizeof j,
		"{\"asset\":{\"version\":\"2.0\"},%s\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[%s],"
		"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}],\"materials\":[%s],"
		"\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":3}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5121,\"count\":3,\"type\":\"SCALAR\"}]}",
		ext.c_str (), node.c_str (), material.c_str (), g.bin.size (), po, io);
	return g.Write (name, j);
}

static bool Near (double a, double b) { return std::fabs (a - b) < 1e-5; }

// Orbiter fronts are clockwise in its left-handed frame: (b-a) x (c-a) points along the normal
static bool Front (const GltfGroup &g, size_t t)
{
	const NTVERTEX &a = g.vtx[g.idx[t]], &b = g.vtx[g.idx[t + 1]], &c = g.vtx[g.idx[t + 2]];
	double u[3] = { b.x - a.x, b.y - a.y, b.z - a.z }, v[3] = { c.x - a.x, c.y - a.y, c.z - a.z };
	double n[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
	return n[0] * a.nx + n[1] * a.ny + n[2] * a.nz > 0;
}

static bool Has (const GltfGroup &g, double x, double y, double z)
{
	for (auto &v : g.vtx) if (Near (v.x, x) && Near (v.y, y) && Near (v.z, z)) return true;
	return false;
}

TEST_CASE ("glTF names", "[gltf]")
{
	CHECK (GltfIsName ("Ship\\ship.glb"));
	CHECK (GltfIsName ("a.GLTF"));
	CHECK_FALSE (GltfIsName ("DG\\deltaglider"));
	CHECK_FALSE (GltfIsName ("x.msh"));
	CHECK_FALSE (GltfIsName (".glb"));
	CHECK_FALSE (GltfIsName (NULL));
}

TEST_CASE ("glTF axes, winding and flat normals", "[gltf]")
{
	GltfModel m;
	std::string err;
	REQUIRE (GltfRead (TriGlb ("t1.glb", "{\"mesh\":0,\"translation\":[0,0,5]}"), m, err));
	REQUIRE (m.groups.size () == 1);
	const GltfGroup &g = m.groups[0];
	REQUIRE (g.vtx.size () == 3);
	REQUIRE (g.idx.size () == 3);
	CHECK (Has (g, 0, 0, -5)); // glTF z mirrored
	CHECK (Has (g, 1, 0, -5));
	CHECK (Has (g, 0, 1, -5));
	CHECK (Near (g.vtx[0].nz, -1)); // flat normal of the counterclockwise front (+z) becomes -z
	CHECK (Front (g, 0)); // winding reversed for Orbiter's clockwise fronts
	REQUIRE (m.materials.size () == 1);
	CHECK (m.materials[0].mat.diffuse.r == 0.5f);
	CHECK (m.materials[0].pbr.find ("PBR\n") == 0);
	CHECK (m.materials[0].pbr.find ("PBR_BASECOLOR 0.5 0.5 0.5 1") != std::string::npos);
	CHECK (m.materials[0].pbr.find ("PBR_METALLIC 1") != std::string::npos);
}

TEST_CASE ("glTF negative scale keeps the winding", "[gltf]")
{
	GltfModel m;
	std::string err;
	REQUIRE (GltfRead (TriGlb ("t2.glb", "{\"mesh\":0,\"scale\":[-1,1,1]}"), m, err));
	const GltfGroup &g = m.groups[0];
	CHECK (Has (g, -1, 0, 0));
	CHECK (Near (g.vtx[0].nz, -1));
	CHECK (Front (g, 0)); // a mirrored node's clockwise glTF front stays the front
}

TEST_CASE ("glTF rotation and hierarchy", "[gltf]")
{
	GltfModel m;
	std::string err;
	// parent: 90 degrees about +y; child: translate x 2; vertex (1,0,0) -> (3,0,0) -> glTF (0,0,-3) -> Orbiter (0,0,3)
	const double s = std::sqrt (0.5);
	char nodes[512];
	snprintf (nodes, sizeof nodes, "{\"rotation\":[0,%.9f,0,%.9f],\"children\":[1]},{\"mesh\":0,\"translation\":[2,0,0]}", s, s);
	std::string p = TriGlb ("t3.glb", nodes);
	REQUIRE (GltfRead (p, m, err));
	REQUIRE (m.groups.size () == 1);
	CHECK (Has (m.groups[0], 0, 0, 3));
	CHECK (Front (m.groups[0], 0));
}

TEST_CASE ("glTF strips, fans, sparse and the default material", "[gltf]")
{
	Glb g;
	float pos[12] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0 };
	size_t po = g.Add (pos, sizeof pos);
	unsigned sidx[1] = { 3 };
	float sval[3] = { 2, 2, 0 };
	size_t so = g.Add (sidx, 4), sv = g.Add (sval, 12);
	char j[2048];
	snprintf (j, sizeof j,
		"{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
		"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":5},{\"attributes\":{\"POSITION\":1},\"mode\":6},{\"attributes\":{\"POSITION\":0},\"mode\":1}]}],"
		"\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":4},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":12}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
		"{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":1,\"componentType\":5125},\"values\":{\"bufferView\":2}}}]}",
		g.bin.size (), po, so, sv);
	GltfModel m;
	std::string err;
	REQUIRE (GltfRead (g.Write ("t4.glb", j), m, err));
	REQUIRE (m.groups.size () == 2); // the line primitive is skipped
	CHECK (m.groups[0].idx.size () == 6); // strip of 4: 2 triangles
	CHECK (m.groups[1].idx.size () == 6); // fan of 4: 2 triangles
	bool moved = false;
	for (auto &v : m.groups[1].vtx) if (Near (v.x, 2) && Near (v.y, 2)) moved = true;
	CHECK (moved); // sparse value applied
	REQUIRE (m.materials.size () == 1); // glTF default material, last
	CHECK (m.groups[0].mtrl == 0);
	CHECK (m.materials[0].pbr.find ("PBR_ROUGHNESS 1") != std::string::npos);
	CHECK (m.warnings.size () >= 1);
}

TEST_CASE ("glTF groups split above 65535 vertices", "[gltf]")
{
	Glb g;
	const unsigned n = 70002; // 23334 triangles, no indices
	std::vector<float> pos (n * 3);
	for (unsigned i = 0; i < n; i++) { pos[i * 3] = (float)(i % 3); pos[i * 3 + 1] = (float)(i % 3 == 2); pos[i * 3 + 2] = (float)(i / 3); }
	std::vector<float> nrm (n * 3, 0.0f);
	for (unsigned i = 0; i < n; i++) nrm[i * 3 + 2] = 1.0f;
	size_t po = g.Add (pos.data (), pos.size () * 4), no = g.Add (nrm.data (), nrm.size () * 4);
	char j[2048];
	snprintf (j, sizeof j,
		"{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
		"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1}}]}],"
		"\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%u},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%u}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"}]}",
		g.bin.size (), po, n * 12, no, n * 12, n, n);
	GltfModel m;
	std::string err;
	REQUIRE (GltfRead (g.Write ("t5.glb", j), m, err));
	REQUIRE (m.groups.size () == 2);
	size_t tris = 0;
	for (auto &gr : m.groups) { CHECK (gr.vtx.size () <= 65535); tris += gr.idx.size () / 3; }
	CHECK (tris == n / 3);
}

static std::string B64 (const std::vector<unsigned char> &d)
{
	static const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string s;
	for (size_t i = 0; i < d.size (); i += 3) {
		unsigned v = d[i] << 16 | (i + 1 < d.size () ? d[i + 1] << 8 : 0) | (i + 2 < d.size () ? d[i + 2] : 0);
		s += a[v >> 18 & 63]; s += a[v >> 12 & 63];
		s += i + 1 < d.size () ? a[v >> 6 & 63] : '=';
		s += i + 2 < d.size () ? a[v & 63] : '=';
	}
	return s;
}

TEST_CASE ("glTF data URI buffers and image references", "[gltf]")
{
	Glb g;
	size_t po = g.Add (Tri, sizeof Tri);
	const char img[] = "\x89PNG fake image bytes";
	size_t io = g.Add (img, sizeof img - 1);
	std::string b64 = B64 (g.bin);
	std::string esc; // JSON writers may escape '/'
	for (char c : b64) { if (c == '/') esc += '\\'; esc += c; }
	char j[4096];
	snprintf (j, sizeof j,
		"{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
		"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"
		"\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}},\"normalTexture\":{\"index\":0,\"scale\":0.5},"
		"\"extensions\":{\"KHR_materials_clearcoat\":{\"clearcoatFactor\":1}}}],"
		"\"textures\":[{\"source\":0}],\"images\":[{\"bufferView\":1,\"mimeType\":\"image/png\"}],"
		"\"buffers\":[{\"byteLength\":%zu,\"uri\":\"data:application/octet-stream;base64,%s\"}],"
		"\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]}",
		g.bin.size (), esc.c_str (), po, io, sizeof img - 1);
	std::string p = Dir () + "/t6.gltf";
	{ std::ofstream f (p, std::ios::binary); f << j; }
	GltfModel m;
	std::string err;
	REQUIRE (GltfRead (p, m, err));
	REQUIRE (m.textures.size () == 1);
	CHECK (m.groups[0].tex == 0);
	GltfImageRef r;
	REQUIRE (GltfParseImageRef (m.textures[0].c_str (), r));
	CHECK (r.b64);
	CHECK (r.sliceOff == io);
	std::vector<char> file;
	{ std::ifstream f (r.file, std::ios::binary); file.assign (std::istreambuf_iterator<char> (f), {}); }
	REQUIRE (r.off + r.len <= file.size ());
	std::vector<unsigned char> dec;
	REQUIRE (GltfBase64 (file.data () + r.off, r.len, dec));
	REQUIRE (r.sliceOff + r.sliceLen <= dec.size ());
	CHECK (std::string ((const char *)dec.data () + r.sliceOff, r.sliceLen) == std::string (img, sizeof img - 1));
	const std::string &t = m.materials[0].pbr;
	CHECK (t.find ("PBR_TEX NORMAL @gltf:") != std::string::npos);
	CHECK (t.find ("PBR_NORMALSCALE 0.5") != std::string::npos);
	CHECK (t.find ("PBR_CLEARCOAT 1 0 1") != std::string::npos);
	CHECK (t.find ("PBR_TEX BASECOLOR") == std::string::npos); // the group's texture carries it
}

TEST_CASE ("glTF errors", "[gltf]")
{
	GltfModel m;
	std::string err;
	CHECK_FALSE (GltfRead (TriGlb ("e1.glb", "{\"mesh\":0}", "{}", "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],"), m, err));
	CHECK (err.find ("KHR_draco_mesh_compression") != std::string::npos);
	CHECK (m.groups.empty ());
	CHECK_FALSE (GltfRead (Dir () + "/missing.glb", m, err));
	CHECK_FALSE (GltfRead (TriGlb ("e2.glb", "{\"mesh\":5}"), m, err));
	{
		std::ofstream f (Dir () + "/e3.glb", std::ios::binary);
		f << "glTF\x02";
	}
	CHECK_FALSE (GltfRead (Dir () + "/e3.glb", m, err));
	Glb g; // accessor past its view
	size_t po = g.Add (Tri, sizeof Tri);
	char j[1024];
	snprintf (j, sizeof j, "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
		"\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":36}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}]}", g.bin.size (), po);
	CHECK_FALSE (GltfRead (g.Write ("e4.glb", j), m, err));
	CHECK (err.find ("runs past") != std::string::npos);
}

TEST_CASE ("glTF DGTest model", "[gltf]")
{
	std::string p = std::string (GLTF_TEST_ROOT) + "/Meshes/DGTest/dgTest.glb";
	if (!fs::exists (p)) SKIP ("no Meshes/DGTest/dgTest.glb");
	GltfModel m;
	std::string err;
	REQUIRE (GltfRead (p, m, err));
	CHECK (m.materials.size () == 27);
	CHECK (m.groups.size () == 122);
	CHECK (m.textures.size () == 10);
	float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
	for (auto &g : m.groups)
		for (auto &v : g.vtx) {
			float c[3] = { v.x, v.y, v.z };
			for (int k = 0; k < 3; k++) { mn[k] = std::min (mn[k], c[k]); mx[k] = std::max (mx[k], c[k]); }
		}
	CHECK (mx[2] > 10.0f); // nose forward (+z)
	CHECK (mn[2] < -8.0f);
	CHECK (mn[1] < -2.5f); // wheels at the DG touchdown height
	CHECK (std::fabs (mx[0] + mn[0]) < 0.01f);
	int glass = 0;
	for (auto &mt : m.materials) if (mt.pbr.find ("PBR_TRANSMISSION 1") != std::string::npos) glass++;
	CHECK (glass == 3);
}
