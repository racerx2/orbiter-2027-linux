// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// custom: glTF metal-rough PBR material data and its config text (design-pbr 2, 9); no client dependencies

#ifndef __PBRMATERIAL_H
#define __PBRMATERIAL_H

#include <cstdio>
#include <memory>
#include <string>

class VkTex;

enum PbrSlot {
	PBR_BASECOLOR, PBR_METALROUGH, PBR_NORMAL, PBR_OCCLUSION, PBR_EMISSIVE, PBR_SPECULAR, PBR_SPECULARCOLOR,
	PBR_TRANSMISSION, PBR_THICKNESS, PBR_CLEARCOAT, PBR_CLEARCOATROUGH, PBR_CLEARCOATNORMAL,
	PBR_SHEENCOLOR, PBR_SHEENROUGH, PBR_ANISOTROPY, PBR_IRIDESCENCE, PBR_IRIDESCENCETHICK,
	PBR_SLOT_COUNT
};

enum PbrAlpha { PBR_OPAQUE = 0, PBR_MASK = 1, PBR_BLEND = 2 };

// shader variant bits (PbrMesh.fx macros)
#define PBRF_CLEARCOAT    0x01
#define PBRF_SHEEN        0x02
#define PBRF_TRANSMISSION 0x04
#define PBRF_ANISOTROPY   0x08
#define PBRF_IRIDESCENCE  0x10
#define PBRF_COUNT        32

// PbrMtrlGPU::flags (PbrMesh.fx PBRM_*)
#define PBRM_BASEGAMMA    0x01   // the base colour map is display-encoded in a linear format: decoded in the shader
#define PBRM_GREENUP      0x02   // normal maps are glTF green-up
#define PBRM_DOUBLESIDED  0x04
#define PBRM_FULLYLIT     0x08
#define PBRM_NOCOLOR      0x10
#define PBRM_EMISGAMMA    0x20   // as BASEGAMMA, emissive map
#define PBRM_SPECCOLGAMMA 0x40   // as BASEGAMMA, specular colour map
#define PBRM_SHEENGAMMA   0x80   // as BASEGAMMA, sheen colour map

struct PbrMaterial {
	bool  enabled = false;
	float baseColor[4] = { 1, 1, 1, 1 };
	float metallic = 1, roughness = 1, normalScale = 1, occlusion = 1;
	bool  normalGreenUp = false;
	float emissive[3] = { 0, 0, 0 };
	float emissiveStrength = 1;
	int   alphaMode = PBR_OPAQUE;
	float alphaCutoff = 0.5f;
	bool  doubleSided = false;
	float ior = 1.5f, specular = 1;
	float specularColor[3] = { 1, 1, 1 };
	float transmission = 0;
	float thickness = 0, attDistance = 0;   // attDistance 0 = infinite
	float attColor[3] = { 1, 1, 1 };
	float clearcoat = 0, clearcoatRough = 0, clearcoatNormalScale = 1;
	float sheenColor[3] = { 0, 0, 0 };
	float sheenRough = 0;
	float anisoStrength = 0, anisoRotation = 0;
	float iridescence = 0, iridIor = 1.3f, iridThickMin = 100, iridThickMax = 400;
	std::string tex[PBR_SLOT_COUNT];
	std::shared_ptr<VkTex> map[PBR_SLOT_COUNT];   // loaded by PbrRender

	unsigned Features () const;                    // PBRF_* of the shader variant
	bool Transparent () const { return alphaMode == PBR_BLEND || transmission > 0; }
	bool NeedsTangents () const;
};

// GPU block (PbrMesh.fx struct PbrMtrl, scalar layout)
struct PbrMtrlGPU {
	float baseColor[4];
	float emissive[3]; float emissiveStrength;
	float metallic, roughness, normalScale, occlusion;
	float alphaCutoff; int alphaMode; int texMask; int flags;
	float ior, specular; float specularColor[3];
	float transmission, thickness, attDistance; float attColor[3];
	float clearcoat, clearcoatRough, clearcoatNormalScale;
	float sheenColor[3]; float sheenRough;
	float anisoStrength, anisoRotation;
	float iridescence, iridIor, iridThickMin, iridThickMax;
};
static_assert(sizeof(PbrMtrlGPU) == 40 * 4, "PbrMtrlGPU must match PbrMesh.fx");

void PbrToGPU (const PbrMaterial &m, PbrMtrlGPU &g);   // factors; texMask and flags from the loaded maps, the caller adds group flags

const char *PbrSlotName (int slot);
int PbrSlotByName (const char *name);                   // -1 if unknown
bool PbrSlotSRGB (int slot);

bool PbrIsKey (const char *line);                       // the line starts with the token PBR or PBR_*
bool PbrParseLine (const char *line, PbrMaterial &m, std::string *err = NULL); // one config line; false and err on a bad line
void PbrWrite (FILE *f, const PbrMaterial &m);          // the lines that differ from the defaults, "PBR" first

#endif // !__PBRMATERIAL_H
