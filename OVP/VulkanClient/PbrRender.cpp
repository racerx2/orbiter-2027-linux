// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// custom: PBR material path: shader variants, textures, environment prefilters, lookup texture, draw phase (design-pbr 3, 6, 7, 8)

#include "PbrRender.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "D3D9Effect.h"
#include "IProcess.h"
#include "VkTexFile.h"
#include "AABBUtil.h"
#include "Log.h"
#include "SceneShadows.h" // not upstream
#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

using namespace oapi;
extern D3D9Client *g_client;

static const char *PbrFile = "Modules/VulkanClient/PbrMesh.fx";
static const char *IblFile = "Modules/VulkanClient/PbrIBL.glsl";
static const VkImageUsageFlags RTUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
static const VkImageUsageFlags TexUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

VkDev *PbrRender::dev = NULL;
PbrVariant PbrRender::variants[PBRF_COUNT];
const PbrEnv *PbrRender::curEnv = NULL;
PbrRender::Phase PbrRender::phase = PbrRender::PHASE_ALL;
VkTex *PbrRender::sceneCopy = NULL;
VkSurf *PbrRender::copySrc = NULL;
float PbrRender::dirty[4][4] = {};
int PbrRender::nDirty = 0;
int PbrRender::refreshes = 0;
static const int MaxRefresh = 8; // per deferred pass (design-pbr-nested-glass 3)
VkTex *PbrRender::lut = NULL;
bool PbrRender::lutOk = false;
ImageProcessing *PbrRender::ipDecode = NULL;
ImageProcessing *PbrRender::ipFilter = NULL;
ImageProcessing *PbrRender::ipLut = NULL;

static std::map<std::pair<std::string, bool>, std::weak_ptr<VkTex>> texCache;

// values the PBR effect reads from the legacy effect, where the client already sets them
static const char *SharedNames[] = { "kernel", "gLVP", "gVP", "gColor", "gAtmColor", "gRadius", "gSHD", "gCameraPos", "gNorth", "gEast",
	"gSun", "gLights", "gLightsEnabled", "gShadowsEnabled", "gProxySize", "gInvProxySize", "gGlowConst", "gExposure" };

static const char *SlotSamplers[PBR_SLOT_COUNT] = { "BaseS", "MetalRoughS", "NormalS", "OcclusionS", "EmissiveS", "SpecularS", "SpecularColorS",
	"TransmissionS", "ThicknessS", "ClearcoatS", "ClearcoatRoughS", "ClearcoatNormalS", "SheenColorS", "SheenRoughS", "AnisoS",
	"IridescenceS", "IridThickS" };

// ===========================================================================================
//
void PbrEnv::Release ()
{
	SAFE_DELETE(src);
	SAFE_DELETE(ggx);
	SAFE_DELETE(sheen);
	SAFE_DELETE(irr);
	ready = false;
}

// ===========================================================================================
//
bool PbrRender::Disabled ()
{
	const char *e = getenv ("ORBITER_PBR_DISABLE");
	return e && e[0] && strcmp (e, "0");
}

// ===========================================================================================
//
void PbrRender::GlobalInit (VkDev *_dev)
{
	dev = _dev;
	phase = PHASE_ALL;
	curEnv = NULL;
	SetSceneCopy (NULL);

	if (!ipLut) ipLut = new ImageProcessing (dev, IblFile, "PSLut");
	if (!lut) lut = new VkTex (dev, 128, 128, 1, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage);
	lutOk = false;
	if (ipLut->IsOK () && lut->img) {
		VkSurf s (lut, 0);
		ipLut->Activate ("PSLut");
		ipLut->SetInt ("iSamples", 512);
		ipLut->SetOutputNative (0, &s);
		lutOk = ipLut->Execute (false);
		ipLut->SetOutputNative (0, NULL);
	}
	if (!lutOk) {
		LogErr ("PbrRender: the lookup texture (PbrIBL.glsl PSLut) failed, PBR materials are drawn as legacy materials");
		SAFE_DELETE(lut);
	}

	const char *pre = getenv ("ORBITER_PBR_PRECOMPILE");
	if (pre && pre[0] && strcmp (pre, "0")) {
		int failed = 0;
		for (unsigned f = 0; f < PBRF_COUNT; f++) {
			VkMacros m = D3D9Effect::BaseMacros ();
			if (f & PBRF_CLEARCOAT) m.push_back ({ "PBR_CLEARCOAT", "" });
			if (f & PBRF_SHEEN) m.push_back ({ "PBR_SHEEN", "" });
			if (f & PBRF_TRANSMISSION) m.push_back ({ "PBR_TRANSMISSION", "" });
			if (f & PBRF_ANISOTROPY) m.push_back ({ "PBR_ANISOTROPY", "" });
			if (f & PBRF_IRIDESCENCE) m.push_back ({ "PBR_IRIDESCENCE", "" });
			VkEffect *fx = VkEffect::Create (dev, PbrFile, m);
			if (!fx || !fx->CompileCheck ()) { failed++; LogErr ("PBR precompile: variant 0x%X failed", f); }
			delete fx;
		}
		oapiWriteLogV ("PBR precompile: %d variants, %d failed", PBRF_COUNT, failed);
	}
}

// ===========================================================================================
//
void PbrRender::GlobalExit ()
{
	for (auto &v : variants) {
		delete v.fx;
		v = PbrVariant ();
	}
	SAFE_DELETE(ipDecode);
	SAFE_DELETE(ipFilter);
	SAFE_DELETE(ipLut);
	SAFE_DELETE(lut);
	lutOk = false;
	texCache.clear ();
	curEnv = NULL;
	SetSceneCopy (NULL);
	phase = PHASE_ALL;
}

// ===========================================================================================
// design-pbr-nested-glass 3: the copy is taken again when glass drawn since it overlaps the next transmission group
//
void PbrRender::SetSceneCopy (VkTex *t, VkSurf *src)
{
	sceneCopy = t;
	copySrc = t ? src : NULL;
	nDirty = 0;
	refreshes = 0;
}

void PbrRender::NoteGlassDrawn (const float r[4])
{
	if (!sceneCopy) return;
	if (nDirty < 4) { // up to 4 rects; then r joins the one that grows least
		std::copy (r, r + 4, dirty[nDirty++]);
		return;
	}
	int best = 0;
	float grow = 1e30f;
	for (int i = 0; i < 4; i++) {
		const float *d = dirty[i];
		float u = (std::max (d[2], r[2]) - std::min (d[0], r[0])) * (std::max (d[3], r[3]) - std::min (d[1], r[1])) - (d[2] - d[0]) * (d[3] - d[1]);
		if (u < grow) { grow = u; best = i; }
	}
	float *d = dirty[best];
	d[0] = std::min (d[0], r[0]); d[1] = std::min (d[1], r[1]);
	d[2] = std::max (d[2], r[2]); d[3] = std::max (d[3], r[3]);
}

bool PbrRender::WantRefresh (const float r[4])
{
	if (phase != PHASE_TRANSPARENT || !sceneCopy || !copySrc || refreshes >= MaxRefresh) return false;
	for (int i = 0; i < nDirty; i++) {
		const float *d = dirty[i];
		if (r[0] < d[2] && d[0] < r[2] && r[1] < d[3] && d[1] < r[3]) return true;
	}
	return false;
}

void PbrRender::RefreshSceneCopy ()
{
	VkSurf *rt = dev ? dev->GetRenderTarget () : NULL;
	if (!sceneCopy || !rt || rt != copySrc || rt->w != sceneCopy->w || rt->h != sceneCopy->h) {
		refreshes = MaxRefresh; // not the target the copy came from: stop trying this pass
		return;
	}
	VkSurf dst (sceneCopy, 0);
	dev->StretchRect (rt, NULL, &dst, NULL, VK_FILTER_LINEAR); // whole target: the copy matches it again, not only the noted rects
	nDirty = 0;
	refreshes++;
}

// ===========================================================================================
// textures
//
static VkFormat SrgbTwin (VkFormat f)
{
	switch (f) {
	case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
	case VK_FORMAT_BC2_UNORM_BLOCK: return VK_FORMAT_BC2_SRGB_BLOCK;
	case VK_FORMAT_BC3_UNORM_BLOCK: return VK_FORMAT_BC3_SRGB_BLOCK;
	case VK_FORMAT_BC7_UNORM_BLOCK: return VK_FORMAT_BC7_SRGB_BLOCK;
	case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB;
	case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_SRGB;
	default: return VK_FORMAT_UNDEFINED;
	}
}

static bool IsBlockFormat (VkFormat f)
{
	UINT bw = 1;
	VkFormatBlockSize (f, &bw);
	return bw == 4;
}

static bool IsFloatFormat (VkFormat f)
{
	return f == VK_FORMAT_R16_SFLOAT || f == VK_FORMAT_R16G16_SFLOAT || f == VK_FORMAT_R16G16B16A16_SFLOAT || f == VK_FORMAT_R32_SFLOAT ||
		f == VK_FORMAT_R32G32_SFLOAT || f == VK_FORMAT_R32G32B32_SFLOAT || f == VK_FORMAT_R32G32B32A32_SFLOAT;
}

static bool IsHdrFormat (VkFormat f)
{
	return IsFloatFormat (f) || f == VK_FORMAT_BC6H_UFLOAT_BLOCK || f == VK_FORMAT_BC6H_SFLOAT_BLOCK;
}

static std::shared_ptr<VkTex> LoadTex (VkDev *dev, const std::string &name, bool srgb)
{
	std::string full = name; // an "@gltf:" image of a glTF mesh is read from its file as it is (claude/design-gltf-loader.md 4)
	bool gltf = VkIsGltfImage (name.c_str());
	if (!gltf) {
		char path[MAX_PATH];
		if (!g_client->TexturePath (name.c_str(), path)) {
			LogErr ("PBR texture not found: %s", name.c_str());
			return nullptr;
		}
		full = oapiResolvePath (path);
	}
	auto key = std::make_pair (full, srgb);
	auto it = texCache.find (key);
	if (it != texCache.end()) if (auto t = it->second.lock ()) return t;

	VkPixels px;
	if (!(gltf ? VkLoadGltfImage (full.c_str(), px) : VkLoadPixels (full.c_str(), px)) || px.data.empty() || px.depth > 1 || px.layers != 1) {
		LogErr ("PBR texture %s: not a 2D image this client reads", full.c_str());
		return nullptr;
	}
	VkFormat fmt = px.fmt;
	if (srgb) {
		VkFormat tw = SrgbTwin (fmt);
		if (tw == VK_FORMAT_UNDEFINED && !IsBlockFormat (fmt) && !IsFloatFormat (fmt)) {
			VkPixels cv;
			if (!VkConvertPixels (px, cv, VK_FORMAT_B8G8R8A8_UNORM, SWZ_NONE, 0, 0, VKTEX_FROM_FILE)) {
				LogErr ("PBR texture %s: format %d can't be converted", full.c_str(), (int)fmt);
				return nullptr;
			}
			px = std::move (cv);
			tw = VK_FORMAT_B8G8R8A8_SRGB;
		}
		if (tw == VK_FORMAT_UNDEFINED) LogWrn ("PBR texture %s: format %d has no sRGB form, %s", full.c_str(), (int)fmt, IsHdrFormat (fmt) ? "read as linear" : "decoded in the shader");
		else fmt = tw;
	}

	VkTex *t = NULL;
	if (!IsBlockFormat (px.fmt)) { // full chain made on the GPU from level 0 (an _SRGB image is filtered in linear)
		VkPixels l0;
		l0.w = px.w; l0.h = px.h; l0.depth = 1; l0.levels = 1; l0.layers = 1;
		l0.fmt = fmt; l0.swz = px.swz;
		l0.data.assign (1, px.Level (0, 0));
		t = new VkTex (dev, px.w, px.h, 0, fmt, TexUsage);
		if (!t->img) { delete t; return nullptr; }
		if (px.swz != SWZ_NONE) t->SetSwizzle (VkSwizzleMap (px.swz));
		t->autoGenMips = true;
		if (!VkUploadPixels (t, l0)) { delete t; return nullptr; }
		t->mipsDirty = t->levels > 1;
	}
	else {
		if (px.levels == 1 && Config->TextureMips) {
			VkPixels cv;
			if (VkConvertPixels (px, cv, px.fmt, px.swz, 0, 0, 0)) px = std::move (cv);
		}
		px.fmt = fmt;
		t = VkCreateTexture (dev, px, TexUsage);
		if (!t) return nullptr;
	}
	std::shared_ptr<VkTex> sp (t);
	texCache[key] = sp;
	LogAlw ("PBR texture %s loaded (%ux%u, format %d, %u levels)", full.c_str(), t->w, t->h, (int)t->fmt, t->levels);
	return sp;
}

bool PbrRender::NeedsDecode (const VkTex *t)
{
	if (!t) return false;
	VkFormat f = t->fmt;
	if (IsHdrFormat (f)) return false;
	switch (f) {
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK: case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: case VK_FORMAT_BC2_SRGB_BLOCK: case VK_FORMAT_BC3_SRGB_BLOCK:
	case VK_FORMAT_BC7_SRGB_BLOCK: case VK_FORMAT_B8G8R8A8_SRGB: case VK_FORMAT_R8G8B8A8_SRGB:
		return false;
	default:
		return true;
	}
}

void PbrRender::LoadTextures (PbrMaterial &m)
{
	if (!dev) dev = D3D9Effect::pDev;
	for (int s = 0; s < PBR_SLOT_COUNT; s++) {
		m.map[s].reset ();
		if (!m.tex[s].empty()) m.map[s] = LoadTex (dev, m.tex[s], PbrSlotSRGB (s));
	}
}

// ===========================================================================================
// shader variants
//
PbrVariant *PbrRender::Variant (unsigned f)
{
	f &= PBRF_COUNT - 1;
	PbrVariant &v = variants[f];
	if (v.fx) return &v;
	if (v.failed || !lutOk || !D3D9Effect::FX) return NULL;
	if (!dev) dev = D3D9Effect::pDev;

	VkMacros m = D3D9Effect::BaseMacros ();
	if (f & PBRF_CLEARCOAT) m.push_back ({ "PBR_CLEARCOAT", "" });
	if (f & PBRF_SHEEN) m.push_back ({ "PBR_SHEEN", "" });
	if (f & PBRF_TRANSMISSION) m.push_back ({ "PBR_TRANSMISSION", "" });
	if (f & PBRF_ANISOTROPY) m.push_back ({ "PBR_ANISOTROPY", "" });
	if (f & PBRF_IRIDESCENCE) m.push_back ({ "PBR_IRIDESCENCE", "" });

	v.fx = VkEffect::Create (dev, PbrFile, m);
	if (!v.fx) {
		v.failed = true;
		LogErr ("PbrRender: PbrMesh.fx variant 0x%X did not compile, its groups are drawn as legacy groups", f);
		return NULL;
	}
	VkEffect *fx = v.fx;
	v.tech = fx->GetTechniqueByName ("PbrTech");
	v.hW = fx->GetParameterByName (0, "gW");
	v.hMtrl = fx->GetParameterByName (0, "gPbr");
	v.hColor = fx->GetParameterByName (0, "gColor");
	v.hScreen = fx->GetParameterByName (0, "gScreen");
	v.hEnvMips = fx->GetParameterByName (0, "gEnvMips");
	v.hSheenMips = fx->GetParameterByName (0, "gSheenMips");
	v.hWorldScale = fx->GetParameterByName (0, "gWorldScale");
	v.hEnvMode = fx->GetParameterByName (0, "gEnvMode");
	v.hIrrMode = fx->GetParameterByName (0, "gIrrMode");
	v.hSceneCopy = fx->GetParameterByName (0, "gSceneCopy");
	v.hShadowS = fx->GetParameterByName (0, "ShadowS");
	v.hIrradS = fx->GetParameterByName (0, "IrradS");
	v.hEnvIrrS = fx->GetParameterByName (0, "EnvIrrS");
	v.hEnvGgxS = fx->GetParameterByName (0, "EnvGgxS");
	v.hEnvSheenS = fx->GetParameterByName (0, "EnvSheenS");
	v.hEnvLegacyS = fx->GetParameterByName (0, "EnvLegacyS");
	v.hLutS = fx->GetParameterByName (0, "LutS");
	v.hSceneS = fx->GetParameterByName (0, "SceneS");
	for (int s = 0; s < PBR_SLOT_COUNT; s++) v.hSlot[s] = fx->GetParameterByName (0, SlotSamplers[s]);
	for (const char *n : SharedNames) {
		VkFxHandle a = D3D9Effect::FX->GetParameterByName (0, n), b = fx->GetParameterByName (0, n);
		if (a && b) v.shared.push_back ({ a, b });
		else LogErr ("PbrRender: shared value %s missing (%s)", n, a ? "PbrMesh.fx" : "D3D9Client.fx");
	}
	if (SceneShadows::Enabled ()) { // not upstream: the scene shadow block, as the legacy effect has it (design-scene-shadows)
		VkFxHandle a = D3D9Effect::FX->GetParameterByName (0, "gCsm"), b = fx->GetParameterByName (0, "gCsm");
		if (a && b) v.shared.push_back ({ a, b });
	}
	if (Config->SSR) for (const char *n : { "gSsr", "tSsrCol", "tSsrHiZ" }) { // not upstream: screen-space reflections, set per vessel and base on the legacy effect (design-ssr)
		VkFxHandle a = D3D9Effect::FX->GetParameterByName (0, n), b = fx->GetParameterByName (0, n);
		if (!a || !b) { LogErr ("PbrRender: shared value %s missing (%s)", n, a ? "PbrMesh.fx" : "D3D9Client.fx"); continue; }
		if (((VkConstHandle)a)->sampler) v.sharedTex.push_back ({ a, b });
		else v.shared.push_back ({ a, b });
	}
	if (!v.tech || !v.hMtrl || ((VkConstHandle)v.hMtrl)->size != sizeof(PbrMtrlGPU)) {
		LogErr ("PbrRender: PbrMesh.fx does not match PbrMtrlGPU (%u bytes)", v.hMtrl ? ((VkConstHandle)v.hMtrl)->size : 0);
		delete v.fx;
		v.fx = NULL;
		v.failed = true;
		return NULL;
	}
	LogAlw ("PbrRender: variant 0x%X created", f);
	return &v;
}

void PbrRender::BeginVariant (PbrVariant *v, VkTex *legacyEnv)
{
	VkEffect *fx = v->fx;
	std::vector<BYTE> buf;
	for (auto &p : v->shared) {
		UINT n = std::min (((VkConstHandle)p.first)->size, ((VkConstHandle)p.second)->size);
		buf.assign (n, 0);
		D3D9Effect::FX->GetValue (p.first, buf.data(), n);
		fx->SetValue (p.second, buf.data(), n);
	}
	VkTex *irr = D3D9Effect::FX->GetTexture (D3D9Effect::eIrradMap);
	fx->SetTexture (v->hShadowS, D3D9Effect::FX->GetTexture (D3D9Effect::eShadowMap));
	if (SceneShadows *ss = SceneShadows::Get ()) if (ss->Ok ()) fx->SetTexture (fx->GetParameterByName (0, "CsmS"), ss->Atlas ()); // not upstream
	fx->SetTexture (v->hIrradS, irr);

	const PbrEnv *e = (curEnv && curEnv->ready) ? curEnv : NULL;
	int envMode = e ? 2 : (legacyEnv ? 1 : 0);
	int irrMode = e ? 2 : ((irr && Config->bIrradiance && Config->EnvMapMode) ? 1 : 0);
	fx->SetTexture (v->hEnvIrrS, e ? e->irr : NULL);
	fx->SetTexture (v->hEnvGgxS, e ? e->ggx : NULL);
	fx->SetTexture (v->hEnvSheenS, e ? e->sheen : NULL);
	fx->SetTexture (v->hEnvLegacyS, legacyEnv);
	fx->SetTexture (v->hLutS, lut);
	fx->SetFloat (v->hEnvMips, e ? float(e->ggx->levels - 1) : 4.0f);
	fx->SetFloat (v->hSheenMips, e ? float(e->sheen->levels - 1) : 0.0f);
	fx->SetInt (v->hEnvMode, envMode);
	fx->SetInt (v->hIrrMode, irrMode);

	bool sc = (phase == PHASE_TRANSPARENT) && sceneCopy;
	D3DXVECTOR4 scr (1, 1, 1, 1);
	if (sc) scr = D3DXVECTOR4 (float(sceneCopy->w), float(sceneCopy->h), 1.0f / float(sceneCopy->w), 1.0f / float(sceneCopy->h));
	fx->SetTexture (v->hSceneS, sc ? sceneCopy : NULL);
	fx->SetBool (v->hSceneCopy, sc);
	fx->SetVector (v->hScreen, &scr);
	for (auto &p : v->sharedTex) fx->SetTexture (p.second, D3D9Effect::FX->GetTexture (p.first)); // not upstream: screen-space reflections
}

// ===========================================================================================
// environment prefilters (design-pbr 7)
//
bool PbrRender::InitIBL (VkDev *_dev)
{
	if (!dev) dev = _dev;
	if (!ipDecode) ipDecode = new ImageProcessing (dev, IblFile, "PSDecode");
	if (!ipFilter) {
		ipFilter = new ImageProcessing (dev, IblFile, "PSGgx");
		ipFilter->CompileShader ("PSCharlie");
		ipFilter->CompileShader ("PSIrr");
	}
	return ipDecode->IsOK () && ipFilter->IsOK ();
}

static void SetFace (ImageProcessing *ip, int face)
{
	D3DXVECTOR3 dir, up, cp;
	EnvMapDirection (face, &dir, &up);
	D3DXVec3Cross (&cp, &up, &dir);
	D3DXVec3Normalize (&cp, &cp);
	ip->SetFloat ("vDir", &dir, sizeof(D3DXVECTOR3));
	ip->SetFloat ("vUp", &up, sizeof(D3DXVECTOR3));
	ip->SetFloat ("vCp", &cp, sizeof(D3DXVECTOR3));
}

static bool RunFaces (ImageProcessing *ip, VkTex *out, UINT level)
{
	for (int face = 0; face < 6; face++) {
		VkSurf s (out, level, face);
		SetFace (ip, face);
		ip->SetOutputNative (0, &s);
		bool ok = ip->Execute (true);
		ip->SetOutputNative (0, NULL);
		if (!ok) return false;
	}
	return true;
}

void PbrRender::PrefilterEnv (VkDev *_dev, VkTex *legacy, PbrEnv &env)
{
	if (!legacy || !legacy->img) return;
	if (!InitIBL (_dev)) {
		static bool logged = false;
		if (!logged) LogErr ("PbrRender: PbrIBL.glsl did not compile, PBR materials use the legacy env map");
		logged = true;
		return;
	}
	UINT size = std::min (legacy->w, 256u);
	if (!env.src || env.src->w != size) {
		env.Release ();
		UINT full = 1;
		for (UINT d = size; d > 1; d >>= 1) full++;
		env.src = new VkTex (dev, size, size, 0, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage, 6, true);
		env.ggx = new VkTex (dev, size, size, std::min (6u, full), VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage, 6, true);
		env.sheen = new VkTex (dev, 64, 64, 5, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage, 6, true);
		env.irr = new VkTex (dev, 16, 16, 1, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage, 6, true);
		if (!env.src->img || !env.ggx->img || !env.sheen->img || !env.irr->img) {
			LogErr ("PbrRender: environment cubes could not be made");
			env.Release ();
			return;
		}
		env.src->autoGenMips = true;
	}

	// linear copy of the legacy cube, then its mips
	ipDecode->Activate ("PSDecode");
	ipDecode->SetTextureNative ("tGamma", legacy, IPF_LINEAR | IPF_CLAMP);
	if (!RunFaces (ipDecode, env.src, 0)) { env.ready = false; return; }
	env.src->mipsDirty = env.src->levels > 1;

	float srcMips = float(env.src->levels);
	ipFilter->Activate ("PSGgx");
	ipFilter->SetTextureNative ("tSrc", env.src, IPF_LINEAR | IPF_CLAMP | IPF_MIPMAP_NATIVE);
	ipFilter->SetFloat ("fSrcSize", float(env.src->w));
	ipFilter->SetFloat ("fSrcMips", srcMips);
	ipFilter->SetInt ("iSamples", 32);
	for (UINT mip = 0; mip < env.ggx->levels; mip++) {
		ipFilter->SetFloat ("fRough", env.ggx->levels > 1 ? float(mip) / float(env.ggx->levels - 1) : 0.0f);
		if (!RunFaces (ipFilter, env.ggx, mip)) { env.ready = false; return; }
	}

	ipFilter->Activate ("PSCharlie");
	ipFilter->SetTextureNative ("tSrc", env.src, IPF_LINEAR | IPF_CLAMP | IPF_MIPMAP_NATIVE);
	ipFilter->SetFloat ("fSrcSize", float(env.src->w));
	ipFilter->SetFloat ("fSrcMips", srcMips);
	ipFilter->SetInt ("iSamples", 32);
	for (UINT mip = 0; mip < env.sheen->levels; mip++) {
		ipFilter->SetFloat ("fRough", float(mip) / float(env.sheen->levels - 1));
		if (!RunFaces (ipFilter, env.sheen, mip)) { env.ready = false; return; }
	}

	ipFilter->Activate ("PSIrr");
	ipFilter->SetTextureNative ("tSrc", env.src, IPF_LINEAR | IPF_CLAMP | IPF_MIPMAP_NATIVE);
	ipFilter->SetFloat ("fSrcSize", float(env.src->w));
	ipFilter->SetFloat ("fSrcMips", srcMips);
	ipFilter->SetInt ("iSamples", 128);
	if (!RunFaces (ipFilter, env.irr, 0)) { env.ready = false; return; }

	env.ready = true;
}
