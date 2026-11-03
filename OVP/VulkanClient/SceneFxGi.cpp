
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space global illumination, one diffuse bounce from the lit opaque scene (design-ssgi, Rev 2)

#include "SsgiMath.h" // first: D3D9Util.h defines PI as a macro
#include "DdgiMath.h"
#include "SceneFx.h"
#include "DdgiScene.h"
#include "DdgiVolume.h"
#include "Scene.h"
#include "AtmoControls.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "DebugControls.h"
#include "IProcess.h"
#include "Mesh.h"
#include "PostMath.h"
#include "VPlanet.h"
#include "VVessel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using namespace PostMath;

static const VkImageUsageFlags GiUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

namespace {

struct GiBody { OBJHANDLE h = NULL; Mat3 R; Vec3 o; };
struct GiView { bool valid = false; Vec3 cam; GiBody pl, ves; Mat4 VP; }; // as PostTaa's: VP is the view times the unjittered projection

Mat3 M3 (const MATRIX3 &m) { return { { { m.m11, m.m12, m.m13 }, { m.m21, m.m22, m.m23 }, { m.m31, m.m32, m.m33 } } }; }
Vec3 V3 (const VECTOR3 &v) { return { v.x, v.y, v.z }; }

Mat4 M4 (const D3DXMATRIX &m)
{
	const float *f = &m._11;
	Mat4 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) r.m[i][j] = f[i * 4 + j];
	return r;
}

struct F16 { float f[16]; };
F16 ToF (const Mat4 &m)
{
	F16 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) r.f[i * 4 + j] = float(m.m[i][j]);
	return r;
}

void Body (OBJHANDLE h, GiBody &b)
{
	b.h = h;
	b.R = RotY(0.0);
	b.o = { 0.0, 0.0, 0.0 };
	if (!h) return;
	MATRIX3 R;
	VECTOR3 p;
	oapiGetRotationMatrix(h, &R);
	oapiGetGlobalPos(h, &p);
	b.R = M3(R);
	b.o = V3(p);
}

}

struct SceneFx::GiState {
	ImageProcessing *ip = nullptr;         // SSGI.glsl: PSGiPrep, PSGiMarch, PSGiBlur, PSGiTemporal, PSGiComposite
	VkTex *tAlb = nullptr, *tNrm = nullptr; // the depth pass MRT, W x H A2B10G10R10
	VkSurf *sAlb = nullptr, *sNrm = nullptr;
	VkTex *tScn = nullptr;                 // the lit opaque scene, W x H RGBA16F (MSAA resolved)
	VkSurf *sScn = nullptr;
	VkTex *tSrc[3] = {};                   // sender radiance (rgb x coverage, coverage): W/2, W/4, W/8 RGBA16F
	VkSurf *sSrc[3] = {};
	VkTex *tGi[2] = {}, *tHist[2] = {};    // march and blur ping-pong; history (irradiance, distance); W/2 RGBA16F
	VkSurf *sGi[2] = {}, *sHist[2] = {};
	bool ok = false;
	bool run = false;                      // this frame's gate, set in DepthPass
	bool bound = false;                    // the MRT is bound (inside DepthPass only)
	DWORD mrtFrame = 0, lastFrame = 0;     // scene frame of the last MRT write, of the last composite
	int cur = 0;                           // tHist[cur] holds the last frame
	float ePrev = 0.0f;
	GiView prev;
	OBJHANDLE lastProxy = NULL, lastFocus = NULL, lastTarget = NULL;
	bool lastInternal = false;
	ConstParams zero;                      // no proxy or no scatter constants: no air, TrGamma 1
	DdgiScene *dScene = nullptr;           // not upstream (design-ddgi): Ray-traced, the probes in place of the march
	DdgiVolume *dVol = nullptr;
	VkTex *tRef = nullptr;                 // the depth pass's fourth target: a vessel's own ambient (rgb), 1 - metal (a), W x H RGBA16F
	VkSurf *sRef = nullptr;
	VkTex *tDepthC = nullptr;              // the cockpit's camera distance, 0 elsewhere, W x H R16F (design-ddgi-cockpit Rev 4)
	VkSurf *sDepthC = nullptr;
	bool rt = false;
	DWORD rtFrame = 0, preFrame = 0;       // scene frames of the last probe update and cockpit pre-pass: the cockpit pass reads both
	DdgiPass *pComp = nullptr, *pCompMS = nullptr; // PSGiCompositeRtL, M: the composite reading its single- or multisampled target in place (design-ddgi-cockpit Rev 4), NULL without local read
	struct Vals { ConstParams *cp = nullptr; vPlanet *vp = nullptr; bool tables = false; D3DXVECTOR4 tex, fade, curve, ambc, misc; } vals; // Gi's, for the composites
};

void SceneFx::FreeGi (GiState *s)
{
	if (!s) return;
	SAFE_DELETE(s->pComp); SAFE_DELETE(s->pCompMS);
	SAFE_DELETE(s->dVol);
	SAFE_DELETE(s->dScene);
	SAFE_DELETE(s->sRef); SAFE_DELETE(s->tRef);
	SAFE_DELETE(s->sDepthC); SAFE_DELETE(s->tDepthC);
	SAFE_DELETE(s->ip);
	SAFE_DELETE(s->sAlb); SAFE_DELETE(s->tAlb);
	SAFE_DELETE(s->sNrm); SAFE_DELETE(s->tNrm);
	SAFE_DELETE(s->sScn); SAFE_DELETE(s->tScn);
	for (int i = 0; i < 3; i++) { SAFE_DELETE(s->sSrc[i]); SAFE_DELETE(s->tSrc[i]); }
	for (int i = 0; i < 2; i++) { SAFE_DELETE(s->sGi[i]); SAFE_DELETE(s->tGi[i]); SAFE_DELETE(s->sHist[i]); SAFE_DELETE(s->tHist[i]); }
	delete s;
}

// targets and shaders at scene start; a failure frees them and leaves gi NULL (the depth-pass shaders' extra outputs then have no attachment and are dropped)
void SceneFx::InitGi ()
{
	if (gi) return;
	gi = new GiState();
	memset(&gi->zero, 0, sizeof(ConstParams));
	gi->zero.TrGamma = 1.0f;
	bool ok = depthOK;
	auto make = [&](VkTex *&t, VkSurf *&s, UINT w, UINT h, VkFormat f) {
		t = new VkTex(dev, std::max(1u, w), std::max(1u, h), 1, f, GiUsage);
		s = new VkSurf(t, 0);
		ok = ok && t->img;
	};
	make(gi->tAlb, gi->sAlb, W, H, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
	make(gi->tNrm, gi->sNrm, W, H, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
	make(gi->tScn, gi->sScn, W, H, VK_FORMAT_R16G16B16A16_SFLOAT);
	for (int i = 0; i < 3; i++) make(gi->tSrc[i], gi->sSrc[i], W >> (i + 1), H >> (i + 1), VK_FORMAT_R16G16B16A16_SFLOAT);
	for (int i = 0; i < 2; i++) {
		make(gi->tGi[i], gi->sGi[i], W / 2, H / 2, VK_FORMAT_R16G16B16A16_SFLOAT);
		make(gi->tHist[i], gi->sHist[i], W / 2, H / 2, VK_FORMAT_R16G16B16A16_SFLOAT);
	}
	std::string ppf = std::string(Config->bAtmoQuality ? "" : "_PERFORMANCE ") + (Config->Filmic ? "_FILMIC " : "") + (Config->PostProcess == PP_DEFAULT ? "_LIGHTGLOW " : "");
	bool rt = Config->RayTracedGI(dev->rayQuery); // not upstream (design-ddgi 0): Ray-traced where the device made it
	std::string ipf = ppf + (rt ? "_DDGI " : "");
	gi->ip = new ImageProcessing(dev, "Modules/VulkanClient/SSGI.glsl", "PSGiPrep", ipf.empty() ? NULL : ipf.c_str());
	for (const char *e : { "PSGiMarch", "PSGiBlur", "PSGiTemporal", "PSGiComposite" }) ok = gi->ip->CompileShader(e) && ok;
	if (rt && ok) {
		bool r = gi->ip->CompileShader("PSGiRtSample") && gi->ip->CompileShader("PSGiCompositeRt");
		gi->tRef = new VkTex(dev, W, H, 1, VK_FORMAT_R16G16B16A16_SFLOAT, GiUsage);
		gi->sRef = new VkSurf(gi->tRef, 0);
		gi->tDepthC = new VkTex(dev, W, H, 1, VK_FORMAT_R16_SFLOAT, GiUsage);
		gi->sDepthC = new VkSurf(gi->tDepthC, 0);
		gi->dScene = new DdgiScene(dev);
		gi->dVol = new DdgiVolume(dev, ppf.c_str());
		if (dev->localRead) { // not upstream (design-ddgi-cockpit Rev 4)
			gi->pComp = new DdgiPass(dev, "Modules/VulkanClient/SSGI.glsl", "GiFull", "PSGiCompositeRtL", ipf.c_str(), false, true);
			if (!gi->pComp->OK()) { LogErr("SceneFx: the in-place GI composite failed, copies instead"); SAFE_DELETE(gi->pComp); }
		}
		if (dev->localReadMS) {
			gi->pCompMS = new DdgiPass(dev, "Modules/VulkanClient/SSGI.glsl", "GiFull", "PSGiCompositeRtM", ipf.c_str(), false, true);
			if (!gi->pCompMS->OK()) { LogErr("SceneFx: the in-place GI composite for multisampled targets failed, copies instead"); SAFE_DELETE(gi->pCompMS); }
		}
		gi->rt = r && gi->tRef->img && gi->tDepthC->img && gi->dScene->OK() && gi->dVol->OK();
		if (!gi->rt) {
			LogErr("SceneFx: ray-traced GI failed, screen-space GI instead");
			SAFE_DELETE(gi->pComp); SAFE_DELETE(gi->pCompMS);
			SAFE_DELETE(gi->dVol);
			SAFE_DELETE(gi->dScene);
			SAFE_DELETE(gi->sRef); SAFE_DELETE(gi->tRef);
			SAFE_DELETE(gi->sDepthC); SAFE_DELETE(gi->tDepthC);
		}
	}
	else if (Config->GIMethod == 1) LogAlw("SceneFx: the device has no ray queries, screen-space GI");
	gi->ok = ok && gi->ip->IsOK();
	if (!gi->ok) {
		LogErr("SceneFx: SSGI targets or shaders failed, SSGI off");
		FreeGi(gi); // the IPI and every target; gi NULL: no GiBind, no pass, no depth pass kept for GI
		gi = nullptr;
	}
}

// DepthPass: 0 after its target push (the gate, the MRT bound before the clear), 1 before the vessels, 2 before its pop (the MRT unbound)
void SceneFx::GiBind (int phase, const std::list<vVessel*> &vessels)
{
	if (!gi || !gi->ok) return;
	if (phase == 0) {
		bool near = scn->GetCameraProxyVisual() && scn->GetCameraElevation() < 20e3; // WantSSAO's test
		for (const vVessel *v : vessels) if (!near && v->CamDist() < 2e3) near = true;
		gi->run = near;
		gi->bound = gi->run; // the MRT only for GI's own frames (no other reader)
		if (!gi->bound) return;
		dev->SetRenderTargetN(1, gi->sAlb);
		dev->SetRenderTargetN(2, gi->sNrm);
		if (gi->rt) dev->SetRenderTargetN(3, gi->sRef); // not upstream (design-ddgi 6)
		D3D9Mesh::SetGiClass(2); // the proxy's bases and buildings
	}
	else if (phase == 1) {
		if (gi->bound) D3D9Mesh::SetGiClass(3);
		if (gi->bound && gi->rt) D3D9Mesh::SetGiRef(true);
	}
	else {
		D3D9Mesh::SetGiClass(-1);
		D3D9Mesh::SetGiRef(false);
		if (!gi->bound) return;
		dev->SetRenderTargetN(3, NULL); // the extra targets are a list ending at the first NULL: the last first
		dev->SetRenderTargetN(2, NULL);
		dev->SetRenderTargetN(1, NULL);
		gi->bound = false;
		gi->mrtFrame = scn->GetFrameId();
	}
}

VkTex *SceneFx::Normal () const { return (gi && gi->ok && gi->mrtFrame == scn->GetFrameId() && DepthValid()) ? gi->tNrm : NULL; }

VkTex *SceneFx::Albedo () const { return (gi && gi->ok && gi->mrtFrame == scn->GetFrameId() && DepthValid()) ? gi->tAlb : NULL; }

// copy, senders at half res and two halvings, march, blur, temporal, composite added into the target (design-ssgi 3)
void SceneFx::Gi (VkSurf *target, vPlanet *vp)
{
	if (!gi || !gi->ok) return;
	DWORD fid = scn->GetFrameId();
	if (!target || !gi->run || gi->mrtFrame != fid || !DepthValid()) { gi->prev.valid = false; return; } // a frame without GI drops the history

	bool dbg = false;
	if (DebugControls::IsActive()) { // the mesh debugger's camera, wireframe
		DWORD camMode = *(DWORD *)gc->GetConfigParam(CFGPRM_GETCAMERAMODE);
		DWORD flags = *(DWORD *)gc->GetConfigParam(CFGPRM_GETDEBUGFLAGS);
		dbg = camMode != 0 || (flags & DBG_FLAGS_WIREFRAME);
	}
	OBJHANDLE proxy = oapiCameraProxyGbody(), focus = oapiGetFocusObject(), tgt = oapiCameraTarget();
	bool in = oapiCameraInternal();
	bool reset = dbg || !gi->prev.valid || gi->lastFrame + 1 != fid || proxy != gi->lastProxy || focus != gi->lastFocus || tgt != gi->lastTarget || in != gi->lastInternal;
	gi->lastProxy = proxy;
	gi->lastFocus = focus;
	gi->lastTarget = tgt;
	gi->lastInternal = in;

	GiView now; // as PostTaa::Capture
	now.cam = V3(scn->GetCameraGPos());
	Body(proxy, now.pl);
	Body((tgt && oapiGetObjectType(tgt) == OBJTP_VESSEL) ? tgt : NULL, now.ves);
	D3DXMATRIX Pm = *scn->GetProjectionMatrix();
	Pm._31 = Pm._32 = 0.0f;
	now.VP = Mul(M4(*scn->GetViewMatrix()), M4(Pm));
	now.valid = true;
	Mat4 mP = Identity4(), mV = Identity4(), mPrev = Identity4();
	if (!reset) {
		mP = Rigid(now.pl.R, now.pl.o, gi->prev.pl.R, gi->prev.pl.o, now.cam, gi->prev.cam);
		bool ves = now.ves.h && now.ves.h == gi->prev.ves.h;
		mV = ves ? Rigid(now.ves.R, now.ves.o, gi->prev.ves.R, gi->prev.ves.o, now.cam, gi->prev.cam) : mP;
		mPrev = gi->prev.VP;
	}

	ConstParams *cp = vp ? vp->GetScatterConst() : NULL;
	bool air = cp && vp->HasAtmosphere() && vp->GetShaderID() != PLT_MOON;
	bool tables = cp != NULL;
	if (!cp) cp = &gi->zero;
	float e = std::max(scn->GetExposure(), 1e-4f);
	float sT = air ? cp->Expo / e : (cp->TrExpo > 0.0f ? cp->TrExpo / e : 1.0f); // terrain: Expo = expoBase e, TrExpo = trBase e (VPlanetAtmo ApplyExposure)
	float sTree = (cp->Expo > 0.0f ? cp->Expo : 1.0f) / e; // TreeMgr's expo
	float tw = 0.0f;
	if (air) if (ScatterParams *sp = vp->GetAtmoParams()) tw = float(sp->tw_bld); // the call UpdateScatter makes each frame
	const D3D9Sun *sun = scn->GetSun();
	VkTex *tF = Flag();
	UINT w2 = gi->tGi[0]->w, h2 = gi->tGi[0]->h;
	int nx = gi->cur ^ 1;

	D3DXVECTOR4 camX, camY, camZ;
	CameraRays(camX, camY, camZ);
	D3DXVECTOR4 tex(1.0f / float(W), 1.0f / float(H), 1.0f / float(w2), 1.0f / float(h2));
	D3DXVECTOR4 rad(25.0f, 0.06f, 4.0f, 0.35f * float(h2)); // the march reaches 35 % of the screen height (r3-gi2)
	D3DXVECTOR4 fade(15e3f, 25e3f, float(frame % 64), air ? 1.0f : 0.0f);
	D3DXVECTOR4 curve(cp->TrGamma > 0.0f ? cp->TrGamma : 1.0f, sT, e, 3.0f);
	D3DXVECTOR4 ambc(sun->Ambient.x, sun->Ambient.y, sun->Ambient.z, tw);
	D3DXVECTOR4 misc(std::min(2.0f, std::max(0.0f, Config->SSGIStrength)), sTree, 0.0f, float(h2)); // z spare
	double pp[4];
	SsgiMath::ProbeParams(Config->EnvMapMode, Config->bIrradiance != 0, scn->GetIrradianceDepthStencil() != NULL, Config->PlanetGlow, pp);
	D3DXVECTOR4 probe((float)pp[0], (float)pp[1], (float)pp[2], (float)pp[3]); // what probe-lit hulls hold already, the march adds only the excess (r3-gi2)
	D3DXVECTOR4 temp(0.08f, 0.25f, reset ? 1.0f : 0.0f, (!reset && gi->ePrev > 0.0f) ? e / gi->ePrev : 1.0f);
	D3DXVECTOR4 dir(0.0f, 0.0f, tF ? 1.0f : 0.0f, 0.0f);
	F16 fP = ToF(mP), fV = ToF(mV), fPrev = ToF(mPrev);

	gi->vals.cp = cp; gi->vals.vp = vp; gi->vals.tables = tables;
	gi->vals.tex = tex; gi->vals.fade = fade; gi->vals.curve = curve; gi->vals.ambc = ambc; gi->vals.misc = misc;
	VkDev::State st = dev->GetState();
	ImageProcessing *ip = gi->ip;
	bool done = false;
	if (!gi->rt || !InPlace(target)) dev->StretchRect(target, NULL, gi->sScn, NULL, VK_FILTER_NEAREST); // resolves MSAA; the ray-traced composite in place needs none
	for (int i = 1; i < 4; i++) ip->SetOutputNative(i, NULL);

	ip->Activate("PSGiPrep");
	ip->SetStruct("Const", cp, sizeof(ConstParams));
	ip->SetFloat("vCamX", &camX, sizeof(camX));
	ip->SetFloat("vCamY", &camY, sizeof(camY));
	ip->SetFloat("vCamZ", &camZ, sizeof(camZ));
	ip->SetFloat("vTex", &tex, sizeof(tex));
	ip->SetFloat("vRad", &rad, sizeof(rad));
	ip->SetFloat("vFade", &fade, sizeof(fade));
	ip->SetFloat("vCurve", &curve, sizeof(curve));
	ip->SetFloat("vAmbC", &ambc, sizeof(ambc));
	ip->SetFloat("vMisc", &misc, sizeof(misc));
	ip->SetFloat("vProbe", &probe, sizeof(probe));
	ip->SetFloat("vTemp", &temp, sizeof(temp));
	ip->SetFloat("vDir", &dir, sizeof(dir));
	ip->SetFloat("mRigP", &fP, sizeof(fP));
	ip->SetFloat("mRigV", &fV, sizeof(fV));
	ip->SetFloat("mPrevVP", &fPrev, sizeof(fPrev));
	if (tables && vp) {
		ip->SetTextureNative("tSun", vp->GetScatterTable(SUN_COLOR), IPF_LINEAR | IPF_CLAMP);
		ip->SetTextureNative("tLndRay", vp->GetScatterTable(RAY_LAND), IPF_LINEAR | IPF_CLAMP);
		ip->SetTextureNative("tLndMie", vp->GetScatterTable(MIE_LAND), IPF_LINEAR | IPF_CLAMP);
		ip->SetTextureNative("tLndAtn", vp->GetScatterTable(ATN_LAND), IPF_LINEAR | IPF_CLAMP);
	}
	ip->SetTextureNative("tDepthG", tDepth, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tNrmG", gi->tNrm, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tAlbG", gi->tAlb, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tScnG", gi->tScn, IPF_POINT | IPF_CLAMP);
	if (gi->rt) done = GiRt(target, vp, cp, { fade, curve, ambc, misc }); // not upstream (design-ddgi 6): the probes in place of the march, blur and history
	if (!gi->rt) ip->SetOutputNative(0, gi->sSrc[0]);
	if (!gi->rt && ip->Execute(true)) {
		dev->StretchRect(gi->sSrc[0], NULL, gi->sSrc[1], NULL, VK_FILTER_LINEAR);
		dev->StretchRect(gi->sSrc[1], NULL, gi->sSrc[2], NULL, VK_FILTER_LINEAR);

		ip->Activate("PSGiMarch");
		ip->SetTextureNative("tSrc0", gi->tSrc[0], IPF_POINT | IPF_CLAMP);
		ip->SetTextureNative("tSrc1", gi->tSrc[1], IPF_LINEAR | IPF_CLAMP);
		ip->SetTextureNative("tSrc2", gi->tSrc[2], IPF_LINEAR | IPF_CLAMP);
		ip->SetOutputNative(0, gi->sGi[0]);
		if (ip->Execute(true)) {
			ip->Activate("PSGiBlur");
			for (int pass = 0; pass < 2; pass++) {
				D3DXVECTOR4 bd = pass ? D3DXVECTOR4(0.0f, tex.w, dir.z, 0.0f) : D3DXVECTOR4(tex.z, 0.0f, dir.z, 0.0f);
				ip->SetFloat("vDir", &bd, sizeof(bd));
				ip->SetTextureNative("tGiG", gi->tGi[pass], IPF_POINT | IPF_CLAMP);
				ip->SetOutputNative(0, gi->sGi[1 - pass]);
				ip->Execute(true);
			}

			ip->Activate("PSGiTemporal");
			ip->SetFloat("vDir", &dir, sizeof(dir));
			ip->SetTextureNative("tGiG", gi->tGi[0], IPF_POINT | IPF_CLAMP);
			ip->SetTextureNative("tHistG", gi->tHist[gi->cur], IPF_LINEAR | IPF_CLAMP);
			ip->SetTextureNative("tFlagG", tF ? tF : tDepth, IPF_POINT | IPF_CLAMP);
			ip->SetOutputNative(0, gi->sHist[nx]);
			if (ip->Execute(true)) {
				ip->Activate("PSGiComposite");
				ip->SetTextureNative("tHistG", gi->tHist[nx], IPF_POINT | IPF_CLAMP);
				ip->SetOutputNative(0, target);
				dev->SetBlendFunc(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD); // Execute keeps the device's function for blendop 2
				done = ip->Execute(2, true);
			}
		}
	}
	gc->PushRenderTarget(target, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // the main target, viewport and depth back
	gc->PopRenderTargets();
	dev->SetState(st);
	if (!done) { gi->prev.valid = false; return; }
	gi->cur = nx;
	gi->prev = now;
	gi->lastFrame = fid;
	gi->ePrev = e;
}

// not upstream (design-ddgi 6): this frame's structures and probe updates, then the signed change at half res and its composite; Gi's values are set on the IPI
bool SceneFx::GiRt (VkSurf *target, vPlanet *vp, ConstParams *cp, const DdgiGiPS &gps)
{
	if (!vp) return false;
	if (!gi->dScene->Build(scn, vp, DdgiScene::RANGE) || !gi->dVol->Update(*gi->dScene, scn, vp, cp, gps)) return false; // nothing traced yet: no change
	gi->rtFrame = scn->GetFrameId();
	ImageProcessing *ip = gi->ip;
	ip->Activate("PSGiRtSample");
	ip->SetStruct("Dg", &gi->dVol->Prm(), sizeof(DdgiGpuPrm));
	ip->SetTextureNative("tRefG", gi->tRef, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tIrrD", gi->dVol->Irr(), IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tDstD", gi->dVol->Dst(), IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tStateD", gi->dVol->State(), IPF_POINT | IPF_CLAMP);
	ip->SetOutputNative(0, gi->sGi[0]);
	if (!ip->Execute(true)) return false;
	D3DXVECTOR4 camX, camY, camZ;
	CameraRays(camX, camY, camZ);
	return GiComposite(target, camX, camY, camZ, D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f), tDepth);
}

// not upstream (design-ddgi-cockpit Rev 4): the composite pass that reads this target in place, NULL: the copy
DdgiPass *SceneFx::InPlacePass (VkSurf *target) const
{
	if (!gi || !target || !target->tex) return NULL;
	if (!(target->tex->usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) return NULL; // not upstream: only a target made for it
	DdgiPass *p = target->tex->samples == VK_SAMPLE_COUNT_1_BIT ? gi->pComp : gi->pCompMS;
	static bool logged;
	if (p && !logged) { logged = true; LogAlw("SceneFx: GI composite reads its target in place (%s)", target->tex->samples == VK_SAMPLE_COUNT_1_BIT ? "single-sampled" : "multisampled"); }
	return p;
}

bool SceneFx::InPlace (VkSurf *target) const { return InPlacePass(target) != NULL; }

// not upstream (design-ddgi 6, design-ddgi-cockpit Rev 4): the half-res change in tGi[0] over the target, in place or from the copy in tScn
bool SceneFx::GiComposite (VkSurf *target, const D3DXVECTOR4 &camX, const D3DXVECTOR4 &camY, const D3DXVECTOR4 &camZ, const D3DXVECTOR4 &dir, VkTex *dist)
{
	const GiState::Vals &v = gi->vals;
	if (!InPlace(target)) {
		ImageProcessing *ip = gi->ip;
		ip->Activate("PSGiCompositeRt");
		ip->SetFloat("vCamX", &camX, sizeof(camX));
		ip->SetFloat("vCamY", &camY, sizeof(camY));
		ip->SetFloat("vCamZ", &camZ, sizeof(camZ));
		ip->SetFloat("vDir", &dir, sizeof(dir));
		ip->SetTextureNative("tDepthG", dist, IPF_POINT | IPF_CLAMP);
		ip->SetTextureNative("tScnG", gi->tScn, IPF_POINT | IPF_CLAMP);
		ip->SetTextureNative("tHistG", gi->tGi[0], IPF_POINT | IPF_CLAMP);
		ip->SetOutputNative(0, target);
		dev->SetBlendFunc(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC1_COLOR, VK_BLEND_OP_ADD); // dst' = src0 + dst * src1: each channel rises or falls
		return ip->Execute(2, true);
	}
	DdgiPass *p = InPlacePass(target);
	D3DXVECTOR4 misc = v.misc;
	misc.z = float(target->tex->samples); // the samples the multisampled read averages
	p->Set("Const", v.cp, sizeof(ConstParams));
	p->Set("vCamX", &camX, sizeof(camX));
	p->Set("vCamY", &camY, sizeof(camY));
	p->Set("vCamZ", &camZ, sizeof(camZ));
	p->Set("vTex", &v.tex, sizeof(v.tex));
	p->Set("vFade", &v.fade, sizeof(v.fade));
	p->Set("vCurve", &v.curve, sizeof(v.curve));
	p->Set("vAmbC", &v.ambc, sizeof(v.ambc));
	p->Set("vMisc", &misc, sizeof(misc));
	p->Set("vDir", &dir, sizeof(dir));
	if (v.tables && v.vp) {
		p->Tex("tSun", v.vp->GetScatterTable(SUN_COLOR), true);
		p->Tex("tLndRay", v.vp->GetScatterTable(RAY_LAND), true);
		p->Tex("tLndMie", v.vp->GetScatterTable(MIE_LAND), true);
		p->Tex("tLndAtn", v.vp->GetScatterTable(ATN_LAND), true);
	}
	p->Tex("tDepthG", dist, false);
	p->Tex("tNrmG", gi->tNrm, false);
	p->Tex("tAlbG", gi->tAlb, false);
	p->Tex("tRefG", gi->tRef, false);
	p->Tex("tHistG", gi->tGi[0], false);
	dev->SetRenderTarget(target, NULL);
	for (int i = 1; i < 4; i++) dev->SetRenderTargetN(i, NULL);
	dev->SetViewport(0.0f, 0.0f, float(target->w), float(target->h));
	VkDev::State s = dev->GetState();
	s.depthTest = s.depthWrite = s.stencil = false;
	s.cull = VK_CULL_MODE_NONE;
	s.fill = VK_POLYGON_MODE_FILL;
	s.writeMask = 0xF;
	dev->SetState(s);
	dev->SetBlend(true);
	dev->SetBlendFunc(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC1_COLOR, VK_BLEND_OP_ADD); // dst' = src0 + dst * src1, per sample
	p->Draw(3);
	dev->SetBlend(false);
	return true;
}

// not upstream (design-ddgi-cockpit Rev 4): the cockpit's distance (its own target), albedo, marked normal and reference, unjittered; tDepth stays the scene's
void SceneFx::CockpitPre ()
{
	if (!gi || !gi->ok || !gi->rt || !gi->run || gi->mrtFrame != scn->GetFrameId() || !DepthValid() || !gi->sDepthC) return;
	vVessel *vf = scn->GetFocusVisual();
	if (!vf || !oapiCameraInternal() || oapiCockpitMode() != COCKPIT_VIRTUAL) return;
	gi->dVol->CutMark(0);
	dev->ColorFill(gi->sDepthC, NULL, 0); // 0: no cockpit
	scn->PushCockpitCamera();
	scn->BeginPass(RENDERPASS_SCENE_DEPTH);
	gc->PushRenderTarget(gi->sDepthC, sDepthDS, RENDERPASS_SCENE_DEPTH);
	dev->SetRenderTargetN(1, gi->sAlb);
	dev->SetRenderTargetN(2, gi->sNrm);
	dev->SetRenderTargetN(3, gi->sRef);
	dev->Clear(false, true, false, 0, 1.0f, 0); // the depth only: the scene's albedo, normal and reference stay where the cockpit leaves them
	dev->SetCullMode(VK_CULL_MODE_BACK_BIT);
	D3D9Mesh::SetGiClass(3);
	D3D9Mesh::SetGiRef(true, true);
	D3D9Mesh::SetGiCockpit(true);
	vf->Render(dev, true);
	D3D9Mesh::SetGiCockpit(false);
	D3D9Mesh::SetGiRef(false);
	D3D9Mesh::SetGiClass(-1);
	dev->SetRenderTargetN(3, NULL);
	dev->SetRenderTargetN(2, NULL);
	dev->SetRenderTargetN(1, NULL);
	gc->PopRenderTargets();
	scn->PopPass();
	scn->PopCockpitCamera();
	gi->dVol->CutMark(1);
	gi->preFrame = scn->GetFrameId();
}

// not upstream (design-ddgi-cockpit 6, Rev 4): the change from the cockpit volume over the lit cockpit, on the pre-pass's pixels; Gi's values stay set
void SceneFx::CockpitGi (VkSurf *target)
{
	if (!gi || !gi->ok || !gi->rt || !target || gi->preFrame != scn->GetFrameId() || gi->rtFrame != scn->GetFrameId()) return;
	if (!gi->dScene->Cockpit().on || !gi->dVol->CockpitOn()) return;
	VkDev::State st = dev->GetState();
	gi->dVol->CutMark(2);
	if (!InPlace(target)) dev->StretchRect(target, NULL, gi->sScn, NULL, VK_FILTER_NEAREST); // the lit cockpit, the composite's display values
	D3DXVECTOR4 camX, camY, camZ;
	CameraRays(camX, camY, camZ); // after the resolve: no jitter, as the pre-pass
	D3DXVECTOR4 dir(0.0f, 0.0f, 0.0f, 1.0f);
	ImageProcessing *ip = gi->ip;
	for (int i = 1; i < 4; i++) ip->SetOutputNative(i, NULL);
	ip->Activate("PSGiRtSample");
	ip->SetFloat("vCamX", &camX, sizeof(camX));
	ip->SetFloat("vCamY", &camY, sizeof(camY));
	ip->SetFloat("vCamZ", &camZ, sizeof(camZ));
	ip->SetFloat("vDir", &dir, sizeof(dir));
	ip->SetStruct("Dg", &gi->dVol->Prm(), sizeof(DdgiGpuPrm));
	ip->SetTextureNative("tDepthG", gi->tDepthC, IPF_POINT | IPF_CLAMP); // the cockpit's distance
	ip->SetTextureNative("tNrmG", gi->tNrm, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tAlbG", gi->tAlb, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tRefG", gi->tRef, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tIrrD", gi->dVol->Irr(), IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tDstD", gi->dVol->Dst(), IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tStateD", gi->dVol->State(), IPF_POINT | IPF_CLAMP);
	ip->SetOutputNative(0, gi->sGi[0]);
	if (ip->Execute(true)) GiComposite(target, camX, camY, camZ, dir, gi->tDepthC);
	gi->dVol->CutMark(3);
	gc->PushRenderTarget(target, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // the main target, viewport and depth back
	gc->PopRenderTargets();
	dev->SetState(st);
}
