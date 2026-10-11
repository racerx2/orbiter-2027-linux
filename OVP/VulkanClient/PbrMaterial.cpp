// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// custom: glTF metal-rough PBR material data and its config text (design-pbr 2, 9)

#include "PbrMaterial.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <vector>

static const char *SlotNames[PBR_SLOT_COUNT] = {
	"BASECOLOR", "METALROUGH", "NORMAL", "OCCLUSION", "EMISSIVE", "SPECULAR", "SPECULARCOLOR",
	"TRANSMISSION", "THICKNESS", "CLEARCOAT", "CLEARCOATROUGH", "CLEARCOATNORMAL",
	"SHEENCOLOR", "SHEENROUGH", "ANISOTROPY", "IRIDESCENCE", "IRIDESCENCETHICK"
};

const char *PbrSlotName (int slot)
{
	return (slot >= 0 && slot < PBR_SLOT_COUNT) ? SlotNames[slot] : "";
}

int PbrSlotByName (const char *name)
{
	for (int i = 0; i < PBR_SLOT_COUNT; i++) if (name && !strcasecmp (name, SlotNames[i])) return i;
	return -1;
}

bool PbrSlotSRGB (int slot)
{
	return slot == PBR_BASECOLOR || slot == PBR_EMISSIVE || slot == PBR_SPECULARCOLOR || slot == PBR_SHEENCOLOR;
}

unsigned PbrMaterial::Features () const
{
	unsigned f = 0;
	if (clearcoat > 0) f |= PBRF_CLEARCOAT;
	if (sheenColor[0] > 0 || sheenColor[1] > 0 || sheenColor[2] > 0) f |= PBRF_SHEEN;
	if (transmission > 0) f |= PBRF_TRANSMISSION;
	if (anisoStrength > 0) f |= PBRF_ANISOTROPY;
	if (iridescence > 0) f |= PBRF_IRIDESCENCE;
	return f;
}

bool PbrMaterial::NeedsTangents () const
{
	return !tex[PBR_NORMAL].empty() || !tex[PBR_CLEARCOATNORMAL].empty() || anisoStrength > 0;
}

void PbrToGPU (const PbrMaterial &m, PbrMtrlGPU &g)
{
	memset (&g, 0, sizeof(g));
	memcpy (g.baseColor, m.baseColor, sizeof(g.baseColor));
	memcpy (g.emissive, m.emissive, sizeof(g.emissive));
	g.emissiveStrength = m.emissiveStrength;
	g.metallic = m.metallic; g.roughness = m.roughness; g.normalScale = m.normalScale; g.occlusion = m.occlusion;
	g.alphaCutoff = m.alphaCutoff; g.alphaMode = m.alphaMode;
	g.ior = m.ior; g.specular = m.specular;
	memcpy (g.specularColor, m.specularColor, sizeof(g.specularColor));
	g.transmission = m.transmission; g.thickness = m.thickness; g.attDistance = m.attDistance;
	memcpy (g.attColor, m.attColor, sizeof(g.attColor));
	g.clearcoat = m.clearcoat; g.clearcoatRough = m.clearcoatRough; g.clearcoatNormalScale = m.clearcoatNormalScale;
	memcpy (g.sheenColor, m.sheenColor, sizeof(g.sheenColor));
	g.sheenRough = m.sheenRough;
	g.anisoStrength = m.anisoStrength; g.anisoRotation = m.anisoRotation;
	g.iridescence = m.iridescence; g.iridIor = m.iridIor; g.iridThickMin = m.iridThickMin; g.iridThickMax = m.iridThickMax;
	for (int i = 0; i < PBR_SLOT_COUNT; i++) if (m.map[i]) g.texMask |= 1 << i;
	if (m.normalGreenUp) g.flags |= PBRM_GREENUP;
	if (m.doubleSided) g.flags |= PBRM_DOUBLESIDED;
}

// ----------------------------------------------------------------------------------------------
// config text

static std::vector<std::string> Tokens (const char *line, std::string *rest2 = NULL)
{
	std::vector<std::string> t;
	std::string s (line);
	size_t sc = s.find (';'), g = s.find ("@gltf:"); // glTF image refs are generated, their paths may hold ';'
	if (sc != std::string::npos && (g == std::string::npos || sc < g)) s.erase (sc);
	size_t i = 0;
	while (i < s.size()) {
		while (i < s.size() && isspace ((unsigned char)s[i])) i++;
		if (i >= s.size()) break;
		if (t.size() == 2 && rest2) { // PBR_TEX <slot> <file>: the file is the rest of the line
			size_t e = s.find_last_not_of (" \t\r\n");
			*rest2 = s.substr (i, e + 1 - i);
		}
		size_t j = i;
		while (j < s.size() && !isspace ((unsigned char)s[j])) j++;
		t.push_back (s.substr (i, j - i));
		i = j;
	}
	return t;
}

bool PbrIsKey (const char *line)
{
	if (!line) return false;
	while (isspace ((unsigned char)*line)) line++;
	if (strncasecmp (line, "PBR", 3)) return false;
	return line[3] == 0 || line[3] == '_' || isspace ((unsigned char)line[3]) || line[3] == ';';
}

static bool Nums (const std::vector<std::string> &t, size_t first, size_t nmin, size_t nmax, float *out)
{
	size_t n = t.size() - first;
	if (n < nmin || n > nmax) return false;
	for (size_t i = 0; i < n; i++) {
		char *end;
		const char *s = t[first + i].c_str();
		float v = strtof (s, &end);
		if (end == s || *end || !std::isfinite (v)) return false;
		out[i] = v;
	}
	return true;
}

bool PbrParseLine (const char *line, PbrMaterial &m, std::string *err)
{
	std::string file;
	std::vector<std::string> t = Tokens (line, &file);
	auto bad = [&](const char *why) { if (err) *err = why; return false; };
	if (t.empty() || !PbrIsKey (t[0].c_str())) return bad ("not a PBR key");
	std::string k = t[0];
	for (auto &c : k) c = (char)toupper ((unsigned char)c);
	float v[5] = { 0, 0, 0, 0, 0 };

	if (k == "PBR") { if (t.size() != 1) return bad ("PBR takes no values"); m.enabled = true; return true; }
	if (k == "PBR_BASECOLOR") { if (!Nums (t, 1, 4, 4, v)) return bad ("PBR_BASECOLOR r g b a"); memcpy (m.baseColor, v, 16); return true; }
	if (k == "PBR_METALLIC") { if (!Nums (t, 1, 1, 1, v)) return bad ("PBR_METALLIC f"); m.metallic = v[0]; return true; }
	if (k == "PBR_ROUGHNESS") { if (!Nums (t, 1, 1, 1, v)) return bad ("PBR_ROUGHNESS f"); m.roughness = v[0]; return true; }
	if (k == "PBR_NORMALSCALE") { if (!Nums (t, 1, 1, 1, v)) return bad ("PBR_NORMALSCALE f"); m.normalScale = v[0]; return true; }
	if (k == "PBR_NORMAL_GREENUP") { if (!Nums (t, 1, 1, 1, v)) return bad ("PBR_NORMAL_GREENUP 0|1"); m.normalGreenUp = v[0] != 0; return true; }
	if (k == "PBR_OCCLUSION") { if (!Nums (t, 1, 1, 1, v)) return bad ("PBR_OCCLUSION f"); m.occlusion = v[0]; return true; }
	if (k == "PBR_EMISSIVE") {
		if (!Nums (t, 1, 3, 4, v)) return bad ("PBR_EMISSIVE r g b [strength]");
		memcpy (m.emissive, v, 12);
		m.emissiveStrength = (t.size() == 5) ? v[3] : 1.0f;
		return true;
	}
	if (k == "PBR_ALPHA") {
		if (t.size() < 2) return bad ("PBR_ALPHA OPAQUE | MASK cutoff | BLEND");
		std::string mode = t[1];
		for (auto &c : mode) c = (char)toupper ((unsigned char)c);
		if (mode == "OPAQUE" && t.size() == 2) { m.alphaMode = PBR_OPAQUE; return true; }
		if (mode == "BLEND" && t.size() == 2) { m.alphaMode = PBR_BLEND; return true; }
		if (mode == "MASK") {
			if (t.size() == 2) { m.alphaMode = PBR_MASK; m.alphaCutoff = 0.5f; return true; }
			if (Nums (t, 2, 1, 1, v)) { m.alphaMode = PBR_MASK; m.alphaCutoff = v[0]; return true; }
		}
		return bad ("PBR_ALPHA OPAQUE | MASK cutoff | BLEND");
	}
	if (k == "PBR_DOUBLESIDED") { if (!Nums (t, 1, 1, 1, v)) return bad ("PBR_DOUBLESIDED 0|1"); m.doubleSided = v[0] != 0; return true; }
	if (k == "PBR_IOR") { if (!Nums (t, 1, 1, 1, v) || v[0] < 1.0f) return bad ("PBR_IOR f (>= 1)"); m.ior = v[0]; return true; }
	if (k == "PBR_SPECULAR") { if (!Nums (t, 1, 1, 1, v)) return bad ("PBR_SPECULAR f"); m.specular = v[0]; return true; }
	if (k == "PBR_SPECULARCOLOR") { if (!Nums (t, 1, 3, 3, v)) return bad ("PBR_SPECULARCOLOR r g b"); memcpy (m.specularColor, v, 12); return true; }
	if (k == "PBR_TRANSMISSION") { if (!Nums (t, 1, 1, 1, v)) return bad ("PBR_TRANSMISSION f"); m.transmission = v[0]; return true; }
	if (k == "PBR_VOLUME") {
		if (!Nums (t, 1, 5, 5, v) || v[1] < 0) return bad ("PBR_VOLUME thickness distance r g b");
		m.thickness = v[0]; m.attDistance = v[1];
		memcpy (m.attColor, v + 2, 12);
		return true;
	}
	if (k == "PBR_CLEARCOAT") {
		if (!Nums (t, 1, 2, 3, v)) return bad ("PBR_CLEARCOAT f roughness [normalscale]");
		m.clearcoat = v[0]; m.clearcoatRough = v[1];
		m.clearcoatNormalScale = (t.size() == 4) ? v[2] : 1.0f;
		return true;
	}
	if (k == "PBR_SHEEN") { if (!Nums (t, 1, 4, 4, v)) return bad ("PBR_SHEEN r g b roughness"); memcpy (m.sheenColor, v, 12); m.sheenRough = v[3]; return true; }
	if (k == "PBR_ANISOTROPY") { if (!Nums (t, 1, 2, 2, v)) return bad ("PBR_ANISOTROPY strength rotation"); m.anisoStrength = v[0]; m.anisoRotation = v[1]; return true; }
	if (k == "PBR_IRIDESCENCE") {
		if (!Nums (t, 1, 4, 4, v) || v[1] < 1.0f) return bad ("PBR_IRIDESCENCE f ior thickmin thickmax");
		m.iridescence = v[0]; m.iridIor = v[1]; m.iridThickMin = v[2]; m.iridThickMax = v[3];
		return true;
	}
	if (k == "PBR_TEX") {
		if (t.size() < 3) return bad ("PBR_TEX SLOT file");
		int s = PbrSlotByName (t[1].c_str());
		if (s < 0) return bad ("PBR_TEX: unknown slot");
		m.tex[s] = file;
		return true;
	}
	return bad ("unknown PBR key");
}

static bool Diff (const float *a, const float *b, int n)
{
	for (int i = 0; i < n; i++) if (a[i] != b[i]) return true;
	return false;
}

void PbrWrite (FILE *f, const PbrMaterial &m)
{
	const PbrMaterial d;
	if (m.enabled) fprintf (f, "PBR\n");
	if (Diff (m.baseColor, d.baseColor, 4)) fprintf (f, "PBR_BASECOLOR %.9g %.9g %.9g %.9g\n", m.baseColor[0], m.baseColor[1], m.baseColor[2], m.baseColor[3]);
	if (m.metallic != d.metallic) fprintf (f, "PBR_METALLIC %.9g\n", m.metallic);
	if (m.roughness != d.roughness) fprintf (f, "PBR_ROUGHNESS %.9g\n", m.roughness);
	if (m.normalScale != d.normalScale) fprintf (f, "PBR_NORMALSCALE %.9g\n", m.normalScale);
	if (m.normalGreenUp != d.normalGreenUp) fprintf (f, "PBR_NORMAL_GREENUP %d\n", m.normalGreenUp ? 1 : 0);
	if (m.occlusion != d.occlusion) fprintf (f, "PBR_OCCLUSION %.9g\n", m.occlusion);
	if (Diff (m.emissive, d.emissive, 3) || m.emissiveStrength != d.emissiveStrength)
		fprintf (f, "PBR_EMISSIVE %.9g %.9g %.9g %.9g\n", m.emissive[0], m.emissive[1], m.emissive[2], m.emissiveStrength);
	if (m.alphaMode == PBR_MASK) fprintf (f, "PBR_ALPHA MASK %.9g\n", m.alphaCutoff);
	else if (m.alphaMode == PBR_BLEND) fprintf (f, "PBR_ALPHA BLEND\n");
	if (m.doubleSided != d.doubleSided) fprintf (f, "PBR_DOUBLESIDED %d\n", m.doubleSided ? 1 : 0);
	if (m.ior != d.ior) fprintf (f, "PBR_IOR %.9g\n", m.ior);
	if (m.specular != d.specular) fprintf (f, "PBR_SPECULAR %.9g\n", m.specular);
	if (Diff (m.specularColor, d.specularColor, 3)) fprintf (f, "PBR_SPECULARCOLOR %.9g %.9g %.9g\n", m.specularColor[0], m.specularColor[1], m.specularColor[2]);
	if (m.transmission != d.transmission) fprintf (f, "PBR_TRANSMISSION %.9g\n", m.transmission);
	if (m.thickness != d.thickness || m.attDistance != d.attDistance || Diff (m.attColor, d.attColor, 3))
		fprintf (f, "PBR_VOLUME %.9g %.9g %.9g %.9g %.9g\n", m.thickness, m.attDistance, m.attColor[0], m.attColor[1], m.attColor[2]);
	if (m.clearcoat != d.clearcoat || m.clearcoatRough != d.clearcoatRough || m.clearcoatNormalScale != d.clearcoatNormalScale)
		fprintf (f, "PBR_CLEARCOAT %.9g %.9g %.9g\n", m.clearcoat, m.clearcoatRough, m.clearcoatNormalScale);
	if (Diff (m.sheenColor, d.sheenColor, 3) || m.sheenRough != d.sheenRough)
		fprintf (f, "PBR_SHEEN %.9g %.9g %.9g %.9g\n", m.sheenColor[0], m.sheenColor[1], m.sheenColor[2], m.sheenRough);
	if (m.anisoStrength != d.anisoStrength || m.anisoRotation != d.anisoRotation) fprintf (f, "PBR_ANISOTROPY %.9g %.9g\n", m.anisoStrength, m.anisoRotation);
	if (m.iridescence != d.iridescence || m.iridIor != d.iridIor || m.iridThickMin != d.iridThickMin || m.iridThickMax != d.iridThickMax)
		fprintf (f, "PBR_IRIDESCENCE %.9g %.9g %.9g %.9g\n", m.iridescence, m.iridIor, m.iridThickMin, m.iridThickMax);
	for (int i = 0; i < PBR_SLOT_COUNT; i++) if (!m.tex[i].empty()) fprintf (f, "PBR_TEX %s %s\n", SlotNames[i], m.tex[i].c_str());
}
