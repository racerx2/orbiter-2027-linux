
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space global illumination, one diffuse bounce from the lit opaque scene (design-ssgi, Rev 2)

#include "SsgiMath.h" // first: D3D9Util.h defines PI as a macro
#include "SceneFx.h"
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
};

void SceneFx::FreeGi (GiState *s)
{
	if (!s) return;
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
	gi->ip = new ImageProcessing(dev, "Modules/VulkanClient/SSGI.glsl", "PSGiPrep", ppf.empty() ? NULL : ppf.c_str());
	for (const char *e : { "PSGiMarch", "PSGiBlur", "PSGiTemporal", "PSGiComposite" }) ok = gi->ip->CompileShader(e) && ok;
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
		D3D9Mesh::SetGiClass(2); // the proxy's bases and buildings
	}
	else if (phase == 1) {
		if (gi->bound) D3D9Mesh::SetGiClass(3);
	}
	else {
		D3D9Mesh::SetGiClass(-1);
		if (!gi->bound) return;
		dev->SetRenderTargetN(2, NULL); // the extra targets are a list ending at the first NULL: the last first
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

	VkDev::State st = dev->GetState();
	ImageProcessing *ip = gi->ip;
	bool done = false;
	dev->StretchRect(target, NULL, gi->sScn, NULL, VK_FILTER_NEAREST); // resolves MSAA
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
	ip->SetOutputNative(0, gi->sSrc[0]);
	if (ip->Execute(true)) {
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
