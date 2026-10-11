// not upstream: VulkanClient PBR material records and their config text (design-pbr 2, 9)

#include <catch2/catch_test_macros.hpp>
#include "PbrMaterial.h"
#include "VkGltfImage.h"
#include <cstdio>
#include <sstream>
#include <string>

static std::string Written (const PbrMaterial &m)
{
	FILE *f = tmpfile ();
	REQUIRE (f);
	PbrWrite (f, m);
	std::string s;
	rewind (f);
	char buf[512];
	while (fgets (buf, sizeof(buf), f)) s += buf;
	fclose (f);
	return s;
}

static PbrMaterial Parsed (const std::string &text)
{
	PbrMaterial m;
	std::istringstream in (text);
	std::string line;
	while (std::getline (in, line)) {
		std::string err;
		INFO (line);
		REQUIRE (PbrParseLine (line.c_str(), m, &err));
	}
	return m;
}

TEST_CASE ("PBR defaults are the glTF defaults", "[pbr]")
{
	PbrMaterial m;
	CHECK_FALSE (m.enabled);
	CHECK (m.baseColor[3] == 1.0f);
	CHECK (m.metallic == 1.0f);
	CHECK (m.roughness == 1.0f);
	CHECK (m.alphaMode == PBR_OPAQUE);
	CHECK (m.alphaCutoff == 0.5f);
	CHECK (m.ior == 1.5f);
	CHECK (m.iridIor == 1.3f);
	CHECK (m.iridThickMin == 100.0f);
	CHECK (m.iridThickMax == 400.0f);
	CHECK (m.Features () == 0);
	CHECK_FALSE (m.Transparent ());
	CHECK_FALSE (m.NeedsTangents ());
	CHECK (Written (m).empty ());
}

TEST_CASE ("PBR keys are exact tokens", "[pbr]")
{
	CHECK (PbrIsKey ("PBR"));
	CHECK (PbrIsKey ("  pbr_metallic 0"));
	CHECK (PbrIsKey ("PBR ; on"));
	CHECK (PbrIsKey ("PBR;on"));
	CHECK_FALSE (PbrIsKey ("PBRX 1"));
	CHECK_FALSE (PbrIsKey ("DIFFUSE 1 1 1 1"));
	CHECK_FALSE (PbrIsKey (""));
	CHECK_FALSE (PbrIsKey (NULL));
}

TEST_CASE ("PBR lines set every field", "[pbr]")
{
	PbrMaterial m = Parsed (
		"PBR\n"
		"PBR_BASECOLOR 0.1 0.2 0.3 0.4\n"
		"PBR_METALLIC 0.25\n"
		"PBR_ROUGHNESS 0.75\n"
		"PBR_NORMALSCALE 2\n"
		"PBR_NORMAL_GREENUP 1\n"
		"PBR_OCCLUSION 0.5\n"
		"PBR_EMISSIVE 1 0.5 0.25 4\n"
		"PBR_ALPHA MASK 0.3\n"
		"PBR_DOUBLESIDED 1\n"
		"PBR_IOR 1.9\n"
		"PBR_SPECULAR 0.8\n"
		"PBR_SPECULARCOLOR 0.5 1 0.5\n"
		"PBR_TRANSMISSION 0.9\n"
		"PBR_VOLUME 1 1.5 0.4 0.9 0.5\n"
		"PBR_CLEARCOAT 1 0.03 0.5\n"
		"PBR_SHEEN 0.3 0.5 1 0.4\n"
		"PBR_ANISOTROPY 0.9 1.57\n"
		"PBR_IRIDESCENCE 1 1.3 100 600\n"
		"PBR_TEX basecolor Dir\\My File.dds ; comment\n"
		"PBR_TEX CLEARCOATNORMAL cc_n.dds\n");
	CHECK (m.enabled);
	CHECK (m.baseColor[2] == 0.3f);
	CHECK (m.baseColor[3] == 0.4f);
	CHECK (m.metallic == 0.25f);
	CHECK (m.roughness == 0.75f);
	CHECK (m.normalScale == 2.0f);
	CHECK (m.normalGreenUp);
	CHECK (m.occlusion == 0.5f);
	CHECK (m.emissive[1] == 0.5f);
	CHECK (m.emissiveStrength == 4.0f);
	CHECK (m.alphaMode == PBR_MASK);
	CHECK (m.alphaCutoff == 0.3f);
	CHECK (m.doubleSided);
	CHECK (m.ior == 1.9f);
	CHECK (m.specular == 0.8f);
	CHECK (m.specularColor[0] == 0.5f);
	CHECK (m.transmission == 0.9f);
	CHECK (m.thickness == 1.0f);
	CHECK (m.attDistance == 1.5f);
	CHECK (m.attColor[1] == 0.9f);
	CHECK (m.clearcoat == 1.0f);
	CHECK (m.clearcoatRough == 0.03f);
	CHECK (m.clearcoatNormalScale == 0.5f);
	CHECK (m.sheenColor[2] == 1.0f);
	CHECK (m.sheenRough == 0.4f);
	CHECK (m.anisoStrength == 0.9f);
	CHECK (m.anisoRotation == 1.57f);
	CHECK (m.iridescence == 1.0f);
	CHECK (m.iridThickMax == 600.0f);
	CHECK (m.tex[PBR_BASECOLOR] == "Dir\\My File.dds");
	CHECK (m.tex[PBR_CLEARCOATNORMAL] == "cc_n.dds");
	REQUIRE (PbrParseLine ("PBR_TEX NORMAL @gltf:10:20:0:0:0:/home/a;b/x.glb", m));
	CHECK (m.tex[PBR_NORMAL] == "@gltf:10:20:0:0:0:/home/a;b/x.glb");
	CHECK (m.Features () == (PBRF_CLEARCOAT | PBRF_SHEEN | PBRF_TRANSMISSION | PBRF_ANISOTROPY | PBRF_IRIDESCENCE));
	CHECK (m.Transparent ());
	CHECK (m.NeedsTangents ());
}

TEST_CASE ("PBR write and parse round trip", "[pbr]")
{
	PbrMaterial a = Parsed (
		"PBR\n"
		"PBR_BASECOLOR 0.1 0.2 0.3 0.4\n"
		"PBR_METALLIC 0\n"
		"PBR_ROUGHNESS 0.123456789\n"
		"PBR_EMISSIVE 1 1 1 1\n"
		"PBR_EMISSIVE 0 0 0 2\n"
		"PBR_ALPHA BLEND\n"
		"PBR_VOLUME 0 0 0.4 0.9 0.5\n"
		"PBR_CLEARCOAT 0.5 0.1\n"
		"PBR_TEX NORMAL hull_n.dds\n");
	std::string s = Written (a);
	PbrMaterial b = Parsed (s);
	CHECK (Written (b) == s);
	CHECK (b.enabled);
	CHECK (b.roughness == a.roughness);
	CHECK (b.emissiveStrength == 2.0f);
	CHECK (b.alphaMode == PBR_BLEND);
	CHECK (b.attColor[2] == 0.5f);
	CHECK (b.clearcoatNormalScale == 1.0f);
	CHECK (b.tex[PBR_NORMAL] == "hull_n.dds");
	CHECK (s.rfind ("PBR\n", 0) == 0);
}

TEST_CASE ("PBR bad lines are refused", "[pbr]")
{
	const char *bad[] = {
		"PBR 1", "PBR_METALLIC", "PBR_METALLIC x", "PBR_METALLIC 1 2", "PBR_BASECOLOR 1 1 1",
		"PBR_ALPHA", "PBR_ALPHA SOMETIMES", "PBR_ALPHA MASK x", "PBR_ALPHA BLEND 1", "PBR_IOR 0.5",
		"PBR_VOLUME 1 -1 1 1 1", "PBR_IRIDESCENCE 1 0.9 100 400", "PBR_TEX NORMAL", "PBR_TEX NOSUCH f.dds",
		"PBR_UNKNOWN 1", "PBR_ROUGHNESS nan", "DIFFUSE 1 1 1 1"
	};
	for (const char *l : bad) {
		PbrMaterial m;
		std::string err;
		INFO (l);
		CHECK_FALSE (PbrParseLine (l, m, &err));
		CHECK_FALSE (err.empty ());
	}
}

TEST_CASE ("PBR texture slots", "[pbr]")
{
	for (int i = 0; i < PBR_SLOT_COUNT; i++) CHECK (PbrSlotByName (PbrSlotName (i)) == i);
	CHECK (PbrSlotByName ("iridescencethick") == PBR_IRIDESCENCETHICK);
	CHECK (PbrSlotByName ("NOPE") == -1);
	CHECK (PbrSlotByName (NULL) == -1);
	CHECK (std::string (PbrSlotName (PBR_SLOT_COUNT)).empty ());
	CHECK (PbrSlotSRGB (PBR_BASECOLOR));
	CHECK (PbrSlotSRGB (PBR_EMISSIVE));
	CHECK (PbrSlotSRGB (PBR_SPECULARCOLOR));
	CHECK (PbrSlotSRGB (PBR_SHEENCOLOR));
	CHECK_FALSE (PbrSlotSRGB (PBR_NORMAL));
	CHECK_FALSE (PbrSlotSRGB (PBR_METALROUGH));
}

TEST_CASE ("PBR GPU block", "[pbr]")
{
	PbrMaterial m = Parsed ("PBR_BASECOLOR 0.5 0.5 0.5 1\nPBR_NORMAL_GREENUP 1\nPBR_DOUBLESIDED 1\nPBR_ALPHA MASK 0.25\nPBR_IRIDESCENCE 1 1.4 50 500\n");
	PbrMtrlGPU g;
	PbrToGPU (m, g);
	CHECK (g.baseColor[0] == 0.5f);
	CHECK (g.alphaMode == PBR_MASK);
	CHECK (g.alphaCutoff == 0.25f);
	CHECK (g.flags == (PBRM_GREENUP | PBRM_DOUBLESIDED));
	CHECK (g.texMask == 0);
	CHECK (g.iridIor == 1.4f);
	CHECK (g.iridThickMin == 50.0f);
	CHECK (g.iridThickMax == 500.0f);
}

TEST_CASE ("glTF image refs in the client", "[pbr]")
{
	VkGltfRef r;
	REQUIRE (VkParseGltfRef ("@gltf:12:34:1:5:6:/x/a;b c.gltf", r));
	CHECK (r.off == 12);
	CHECK (r.len == 34);
	CHECK (r.b64 == 1);
	CHECK (r.sliceOff == 5);
	CHECK (r.sliceLen == 6);
	CHECK (std::string (r.file) == "/x/a;b c.gltf");
	CHECK_FALSE (VkParseGltfRef ("@gltf:12:34:1:5:6:", r));
	CHECK_FALSE (VkParseGltfRef ("hull.dds", r));
	REQUIRE (VkParseGltfRef ("@gltf:12:34:1:5:6:/x/a;b c.gltf", r));

	// standard, JSON-escaped and url-safe base64 of the same bytes, sliced to "pixels"
	const char *enc[] = { "+//+++9waXhlbHM=", "+\\/\\/+++9waXhlbHM=", "-__---9waXhlbHM=", " +//+ ++9w\naXhlbHM=" };
	for (const char *e : enc) {
		QByteArray b (e);
		REQUIRE (VkGltfImageBytes (r, b));
		CHECK (b == QByteArray ("pixels"));
	}
	QByteArray whole ("+//+++9waXhlbHM=");
	r.sliceOff = 0; r.sliceLen = 0;
	REQUIRE (VkGltfImageBytes (r, whole));
	CHECK (whole.size () == 11);
	QByteArray raw ("0123456789");
	r.b64 = 0; r.sliceOff = 8; r.sliceLen = 3;
	CHECK_FALSE (VkGltfImageBytes (r, raw)); // slice past the range
}
