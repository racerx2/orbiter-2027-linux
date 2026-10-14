// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: temporal anti-aliasing of the 3D world (design-visual-batch 3)

#include "PostTaa.h"
#include "Scene.h"
#include "SceneFx.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "DebugControls.h"
#include "IProcess.h"
#include "VPlanet.h"
#include <cmath>

using namespace PostMath;

static const VkImageUsageFlags RTUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

static Mat3 M3 (const MATRIX3 &m) { return { { { m.m11, m.m12, m.m13 }, { m.m21, m.m22, m.m23 }, { m.m31, m.m32, m.m33 } } }; }
static Vec3 V3 (const VECTOR3 &v) { return { v.x, v.y, v.z }; }

static Mat4 M4 (const D3DXMATRIX &m)
{
	const float *f = &m._11;
	Mat4 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) r.m[i][j] = f[i * 4 + j];
	return r;
}

struct F16 { float f[16]; };
static F16 ToF (const Mat4 &m)
{
	F16 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) r.f[i * 4 + j] = float(m.m[i][j]);
	return r;
}

PostTaa::PostTaa (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h) : gc(_gc), scn(_scn), dev(_gc->GetDevice()), W(w), H(h)
{
	tCur = new VkTex(dev, W, H, 1, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage);
	sCur = new VkSurf(tCur, 0);
	for (int i = 0; i < 2; i++) {
		tHist[i] = new VkTex(dev, W, H, 1, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage);
		sHist[i] = new VkSurf(tHist[i], 0);
	}
	pIP = new ImageProcessing(dev, "Modules/VulkanClient/PostTaa.glsl", "PSTaa");
	ok = tCur->img && tHist[0]->img && tHist[1]->img && pIP->IsOK();
	if (!ok) LogErr("PostTaa: targets or shader failed, TAA off");
}

PostTaa::~PostTaa ()
{
	SAFE_DELETE(pIP);
	for (int i = 0; i < 2; i++) { SAFE_DELETE(sHist[i]); SAFE_DELETE(tHist[i]); }
	SAFE_DELETE(sCur);
	SAFE_DELETE(tCur);
}

VkTex *PostTaa::Output () const
{
	return (ok && outFrame && outFrame == scn->GetFrameId()) ? tHist[cur] : NULL;
}

// the history is dropped before the camera moves: a new proxy, focus or target, a camera mode change, or a frame without the resolve; no jitter then
void PostTaa::BeginFrame (bool *jitter, float *pjx, float *pjy)
{
	*jitter = false;
	*pjx = *pjy = 0.0f;
	jx = jy = 0.0f;
	skip = !ok || !scn->pFx || !scn->pFx->DepthOK();
	if (DebugControls::IsActive()) { // the mesh debugger's camera, wireframe
		DWORD camMode = *(DWORD *)gc->GetConfigParam(CFGPRM_GETCAMERAMODE);
		DWORD flags = *(DWORD *)gc->GetConfigParam(CFGPRM_GETDEBUGFLAGS);
		if (camMode != 0 || (flags & DBG_FLAGS_WIREFRAME)) skip = true;
	}
	OBJHANDLE proxy = oapiCameraProxyGbody(), focus = oapiGetFocusObject(), target = oapiCameraTarget();
	bool in = oapiCameraInternal();
	reset = skip || lastFrame + 1 != scn->GetFrameId() || proxy != lastProxy || focus != lastFocus || target != lastTarget || in != lastInternal;
	lastProxy = proxy;
	lastFocus = focus;
	lastTarget = target;
	lastInternal = in;
	if (reset) return;
	double x, y;
	Jitter(seq++, &x, &y);
	jx = float(x);
	jy = float(y);
	*jitter = true;
	*pjx = jx;
	*pjy = jy;
}

void PostTaa::Capture (View &v) const
{
	v.cam = V3(scn->Camera.pos);
	v.pl.h = scn->Camera.hObj_proxy;
	v.pl.R = RotY(0.0);
	v.pl.o = { 0.0, 0.0, 0.0 };
	if (v.pl.h) {
		MATRIX3 R;
		VECTOR3 p;
		oapiGetRotationMatrix(v.pl.h, &R);
		oapiGetGlobalPos(v.pl.h, &p);
		v.pl.R = M3(R);
		v.pl.o = V3(p);
	}
	OBJHANDLE t = oapiCameraTarget();
	v.ves.h = (t && oapiGetObjectType(t) == OBJTP_VESSEL) ? t : NULL;
	v.ves.R = RotY(0.0);
	v.ves.o = { 0.0, 0.0, 0.0 };
	if (v.ves.h) {
		MATRIX3 R;
		VECTOR3 p;
		oapiGetRotationMatrix(v.ves.h, &R);
		oapiGetGlobalPos(v.ves.h, &p);
		v.ves.R = M3(R);
		v.ves.o = V3(p);
	}
	D3DXMATRIX P = scn->Camera.mProj;
	P._31 = P._32 = 0.0f; // the history is unjittered
	v.VP = Mul(M4(scn->Camera.mView), M4(P));
	v.valid = true;
}

// pass A: the opaque scene with last frame's output reprojected, clipped and blended, back into the target
void PostTaa::Resolve (VkSurf *target)
{
	if (skip || !ok || !target) return;
	SceneFx *fx = scn->pFx;
	VkTex *tD = fx ? fx->Depth() : NULL;
	if (!tD) return; // no depth this frame: the next one starts over

	View now;
	Capture(now);
	bool rs = reset || !prev.valid || prev.pl.h != now.pl.h;
	Mat4 mPt = Identity4(), mVs = Identity4(), mSky = Identity4();
	bool ves = !rs && now.ves.h && now.ves.h == prev.ves.h;
	if (!rs) {
		mPt = Mul(Rigid(now.pl.R, now.pl.o, prev.pl.R, prev.pl.o, now.cam, prev.cam), prev.VP);
		mVs = ves ? Mul(Rigid(now.ves.R, now.ves.o, prev.ves.R, prev.ves.o, now.cam, prev.cam), prev.VP) : mPt;
		mSky = prev.VP;
	}
	VkTex *tF = fx->Flag(), *tVD = fx->VolumeDist(), *tV = fx->VolumeTex();
	bool flg = ves && tF, vol = tVD && tV;

	D3DXVECTOR4 camX, camY, camZ;
	fx->CameraRays(camX, camY, camZ);
	D3DXVECTOR4 pl(0.0f, 0.0f, 0.0f, 0.0f), sph(0.0f, 0.0f, 0.0f, 0.0f);
	vPlanet *vp = scn->GetCameraProxyVisual();
	if (vp && now.pl.h && vp->Object() == now.pl.h) { // depth-0 pixels: the cloud shell from below the layer, else the planet
		Vec3 o = Sub(now.cam, now.pl.o);
		double r = vp->GetSize(), ro = std::sqrt(Dot(o, o));
		if (vp->HasAtmosphere() && vp->prm.bCloud && ro < r + vp->prm.cloudalt) r += vp->prm.cloudalt;
		pl = D3DXVECTOR4(float(o.x), float(o.y), float(o.z), float((ro - r) * (ro + r)));
		sph.x = 1.0f;
	}
	D3DXVECTOR4 prm(0.1f, 0.35f, rs ? 1.0f : 0.0f, 1.25f);
	D3DXVECTOR4 tex(1.0f / float(W), 1.0f / float(H), float(W), float(H));
	D3DXVECTOR4 jit(jx / float(W), jy / float(H), flg ? 1.0f : 0.0f, vol ? 1.0f : 0.0f);
	F16 fPt = ToF(mPt), fVs = ToF(mVs), fSky = ToF(mSky);

	VkDev::State st = dev->GetState();
	dev->StretchRect(target, NULL, sCur, NULL, VK_FILTER_NEAREST); // resolves MSAA
	int nx = cur ^ 1;
	pIP->Activate("PSTaa");
	pIP->SetFloat("mPt", &fPt, sizeof(fPt));
	pIP->SetFloat("mVs", &fVs, sizeof(fVs));
	pIP->SetFloat("mSky", &fSky, sizeof(fSky));
	pIP->SetFloat("vCamX", &camX, sizeof(camX));
	pIP->SetFloat("vCamY", &camY, sizeof(camY));
	pIP->SetFloat("vCamZ", &camZ, sizeof(camZ));
	pIP->SetFloat("vPl", &pl, sizeof(pl));
	pIP->SetFloat("vSph", &sph, sizeof(sph));
	pIP->SetFloat("vPrm", &prm, sizeof(prm));
	pIP->SetFloat("vTex", &tex, sizeof(tex));
	pIP->SetFloat("vJit", &jit, sizeof(jit));
	pIP->SetTextureNative("tCur", tCur, IPF_POINT | IPF_CLAMP);
	pIP->SetTextureNative("tHist", tHist[cur], IPF_LINEAR | IPF_CLAMP);
	pIP->SetTextureNative("tDepth", tD, IPF_POINT | IPF_CLAMP);
	pIP->SetTextureNative("tFlag", flg ? tF : tD, IPF_POINT | IPF_CLAMP);
	pIP->SetTextureNative("tVolD", vol ? tVD : tD, IPF_POINT | IPF_CLAMP);
	pIP->SetTextureNative("tVol", vol ? tV : tD, IPF_LINEAR | IPF_CLAMP);
	pIP->SetOutputNative(0, sHist[nx]);
	bool done = pIP->Execute(true);
	if (done) dev->StretchRect(sHist[nx], NULL, target, NULL, VK_FILTER_NEAREST); // into the MSAA target by a draw
	gc->PushRenderTarget(target, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // the main target, viewport and depth back
	gc->PopRenderTargets();
	dev->SetState(st);
	if (!done) return;
	cur = nx;
	prev = now;
	lastFrame = outFrame = scn->GetFrameId();
}
