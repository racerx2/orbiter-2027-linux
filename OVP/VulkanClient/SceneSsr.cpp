// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: SSR capture (colour copy, depth pyramid) and each draw's parameters; hosts trace last frame's capture per body (design-ssr, Valient 2014)

#include "SceneSsr.h"
#include "Scene.h"
#include "SceneFx.h"
#include "PostTaa.h"
#include "SsrMath.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "D3D9Effect.h"
#include "DebugControls.h"
#include "IProcess.h"
#include "VPlanet.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace PostMath;

static const VkImageUsageFlags RTUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

static Mat3 M3 (const MATRIX3 &m) { return { { { m.m11, m.m12, m.m13 }, { m.m21, m.m22, m.m23 }, { m.m31, m.m32, m.m33 } } }; }
static Vec3 V3 (const VECTOR3 &v) { return { v.x, v.y, v.z }; }
static Vec3 V3 (const D3DXVECTOR3 &v) { return { v.x, v.y, v.z }; }

static Mat4 M4 (const D3DXMATRIX &m)
{
	const float *f = &m._11;
	Mat4 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) r.m[i][j] = f[i * 4 + j];
	return r;
}

static void ToF (const Mat4 &m, float *f)
{
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) f[i * 4 + j] = float(m.m[i][j]);
}

static void Set4 (float *v, float x, float y, float z, float w) { v[0] = x; v[1] = y; v[2] = z; v[3] = w; }

SceneSsr::SceneSsr (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h) : gc(_gc), scn(_scn), dev(_gc->GetDevice()), W(w), H(h)
{
	L = SsrMath::HiZLevels(int(W), int(H));
	DWORD a = 1u << (L - 1), Wp = (W + a - 1) / a * a, Hp = (H + a - 1) / a * a; // every level an exact half (design-ssr Rev 2 M2)
	tCol = new VkTex(dev, W, H, 0, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage);
	tCol->autoGenMips = true;
	sCol = new VkSurf(tCol, 0);
	tHiZ = new VkTex(dev, Wp, Hp, L, VK_FORMAT_R32_SFLOAT, RTUsage);
	for (int k = 0; k < L; k++) sHiZ.push_back(new VkSurf(tHiZ, k));
	if (L > 1) {
		tTmp = new VkTex(dev, Wp / 2, Hp / 2, L - 1, VK_FORMAT_R32_SFLOAT, RTUsage); // the IPI cannot draw into the texture it reads
		for (int k = 0; k + 1 < L; k++) sTmp.push_back(new VkSurf(tTmp, k));
	}
	pIP = new ImageProcessing(dev, "Modules/VulkanClient/HiZ.glsl", "PSHiZ0");
	pIP->CompileShader("PSHiZDown");
	pIP->CompileShader("PSUnfog");
	ok = tCol->img && tHiZ->img && (L < 2 || tTmp->img) && pIP->IsOK();
	if (!ok) LogErr("SceneSsr: targets or HiZ.glsl failed, screen-space reflections off");
	else LogAlw("SceneSsr: %ux%u, %d depth levels from %ux%u, %u colour levels", W, H, L, Wp, Hp, tCol->levels);
}

SceneSsr::~SceneSsr ()
{
	SAFE_DELETE(pIP);
	for (auto &p : srcSurf) delete p.second;
	for (VkSurf *s : sHiZ) delete s;
	for (VkSurf *s : sTmp) delete s;
	SAFE_DELETE(sFogTmp);
	SAFE_DELETE(tFogTmp);
	SAFE_DELETE(sCol);
	SAFE_DELETE(tCol);
	SAFE_DELETE(tTmp);
	SAFE_DELETE(tHiZ);
}

SceneSsr *SceneSsr::Of (Scene *scn)
{
	return (scn && scn->pSsr && scn->pSsr->ok) ? scn->pSsr : nullptr;
}

const SceneSsr::Body *SceneSsr::NowOf (OBJHANDLE h)
{
	if (!h) return nullptr;
	for (const Body &b : now) if (b.h == h) return &b;
	Body b;
	b.h = h;
	MATRIX3 R;
	VECTOR3 p;
	oapiGetRotationMatrix(h, &R);
	oapiGetGlobalPos(h, &p);
	b.R = M3(R);
	b.o = V3(p);
	now.push_back(b);
	return &now.back();
}

// the history is dropped before anything traces it: a new proxy, focus, target or camera mode, a frame without a capture, a camera jump
void SceneSsr::Prepare ()
{
	DWORD f = scn->GetFrameId();
	if (prepFrame == f) return;
	prepFrame = f;
	now.clear();
	active = ok;
	if (DebugControls::IsActive()) { // the mesh debugger's camera, wireframe
		DWORD camMode = *(DWORD *)gc->GetConfigParam(CFGPRM_GETCAMERAMODE);
		DWORD flags = *(DWORD *)gc->GetConfigParam(CFGPRM_GETDEBUGFLAGS);
		if (camMode != 0 || (flags & DBG_FLAGS_WIREFRAME)) active = false;
	}
	OBJHANDLE focus = oapiGetFocusObject(), target = oapiCameraTarget();
	bool in = oapiCameraInternal();
	bool reset = !active || !last.valid || last.frame + 1 != f || last.bodies.empty() || focus != lastFocus || target != lastTarget ||
		in != lastInternal || scn->Camera.hObj_proxy != last.proxy;
	lastFocus = focus;
	lastTarget = target;
	lastInternal = in;
	movedNow = 0.0;
	if (!reset) {
		Vec3 cam = V3(scn->Camera.pos);
		const Body &bp = last.bodies[0];
		Body bn = bp;
		if (const Body *q = NowOf(bp.h)) bn = *q;
		Vec3 d = Sub(Mul(Transpose(bn.R), Sub(cam, bn.o)), Mul(Transpose(bp.R), Sub(last.cam, bp.o))); // in the proxy's frame
		movedNow = std::sqrt(Dot(d, d));
		double c = Dot(Unit(V3(scn->Camera.z)), Unit(last.camZ));
		if (SsrMath::JumpTest(movedNow, last.moved, std::acos(std::min(1.0, std::max(-1.0, c))))) reset = true;
	}
	prevOk = !reset;
}

// before this frame's capture: last frame's, through the drawn body's rigid motion; after it (glass): this frame's
void SceneSsr::Params (OBJHANDLE body, int kind, float skyW, bool planet, SsrPrm &p)
{
	memset(&p, 0, sizeof(p));
	Prepare();
	DWORD f = scn->GetFrameId();
	if (!active || !last.valid) return;
	bool current = last.frame == f;
	if (!current && !prevOk) return;
	if (planet && (current || !last.terrain)) return; // the ridges must be in last frame's depth (design-ssr NIT 5)
	Mat4 M = last.VP;
	if (!current) {
		const Body *bp = &last.bodies[0];
		for (const Body &b : last.bodies) if (b.h == body) { bp = &b; break; } // not drawn last frame: as the ground
		Body bn = *bp;
		if (const Body *q = NowOf(bp->h)) bn = *q;
		M = SsrMath::BodyMatrix(bn.R, bn.o, bp->R, bp->o, V3(scn->Camera.pos), last.cam, last.VP);
	}
	ToF(M, p.mPrev);
	ToF(last.VP, p.mSky);
	float pix = 2.0f / (std::max(scn->Camera.mProj._22, 1e-3f) * float(H)); // one pixel's angle
	float e = scn->GetExposure(), es = std::max(last.e, 1e-3f);
	Set4(p.vHiZ, float(W), float(H), float(L - 1), 48.0f);
	Set4(p.vJit, last.jx, last.jy, 1.5f * pix, 0.1f);
	Set4(p.vMode, 1.0f, 0.0f, float(f % 16), skyW); // the mirror ray and the cone's colour level (Uludag 2014): one GGX sample a pixel, with no spatial resolve, left a hit/miss checker under TAA's 10 % blend (r3-ssr2)
	Set4(p.vThick, 0.25f, 0.015f, 0.20f, 0.36f);
	Set4(p.vDecode, e / es, 1.0f / es, float(tCol->levels - 1), 0.06f);
	if (kind == BASE_SURFACE) { // dry (design-ssr Rev 2 M4); a weather hook would set the wetness here
		SsrMath::Wet w = SsrMath::WetModel(0.0);
		Set4(p.vSurf, 1.0f, float(w.alpha), float(w.f0), float(w.albedo));
	}
	else Set4(p.vSurf, float(kind), 0.0f, 0.0f, 1.0f);
}

bool SceneSsr::Pass (const char *entry, VkTex *in, VkSurf *out, float level)
{
	D3DXVECTOR4 src(float(W), float(H), level, 0.0f);
	pIP->Activate(entry);
	pIP->SetFloat("vSrc", &src, sizeof(src));
	pIP->SetTextureNative("tHiZ", in, IPF_POINT | IPF_CLAMP | IPF_MIPMAP_NATIVE);
	pIP->SetOutputNative(0, out);
	return pIP->Execute(true);
}

void SceneSsr::Capture (VkSurf *target)
{
	Prepare();
	DWORD f = scn->GetFrameId();
	last.valid = false;
	if (!active || !target) return;
	SceneFx *fx = scn->pFx;
	VkTex *tD = fx ? fx->Depth() : NULL;
	VkTex *taa = scn->pTaa ? scn->pTaa->Output() : NULL;
	if (!tD || (scn->bJitter && !taa)) return; // no depth this frame, or only a jittered colour
	VkSurf *src = target;
	if (taa) {
		auto it = srcSurf.find(taa);
		if (it == srcSurf.end()) it = srcSurf.emplace(taa, new VkSurf(taa, 0)).first;
		src = it->second;
	}
	VkTex *fog = fx->FogTex();
	VkDev::State st = dev->GetState();
	bool done = true;
	if (fog) { // no double fog: the reflecting pixel gets its own (ue5-integration INT-5)
		if (!tFogTmp) {
			tFogTmp = new VkTex(dev, W, H, 1, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage);
			sFogTmp = new VkSurf(tFogTmp, 0);
		}
		dev->StretchRect(src, NULL, sFogTmp, NULL, VK_FILTER_NEAREST); // resolves MSAA
		D3DXVECTOR4 fs(float(W), float(H), 1.0f / float(std::max(fog->w, 1u)), 1.0f / float(std::max(fog->h, 1u)));
		pIP->Activate("PSUnfog");
		pIP->SetFloat("vSrc", &fs, sizeof(fs));
		pIP->SetTextureNative("tSrc", tFogTmp, IPF_POINT | IPF_CLAMP);
		pIP->SetTextureNative("tFog", fog, IPF_POINT | IPF_CLAMP);
		pIP->SetTextureNative("tDepth", tD, IPF_POINT | IPF_CLAMP); // the composite's weights (HeightFog.glsl PSFogComposite)
		pIP->SetOutputNative(0, sCol);
		done = tFogTmp->img && pIP->Execute(true);
	}
	else dev->StretchRect(src, NULL, sCol, NULL, VK_FILTER_NEAREST); // resolves MSAA

	D3DXVECTOR4 camX, camY, camZ;
	fx->CameraRays(camX, camY, camZ); // the jitter is still on: the rays of tDepth's pixels
	const D3DXVECTOR3 *fz = scn->GetCameraZ();
	D3DXVECTOR4 fwd(fz->x, fz->y, fz->z, 0.0f), s0(float(W), float(H), 0.0f, 0.0f);
	pIP->Activate("PSHiZ0");
	pIP->SetFloat("vCamX", &camX, sizeof(camX));
	pIP->SetFloat("vCamY", &camY, sizeof(camY));
	pIP->SetFloat("vCamZ", &camZ, sizeof(camZ));
	pIP->SetFloat("vFwd", &fwd, sizeof(fwd));
	pIP->SetFloat("vSrc", &s0, sizeof(s0));
	pIP->SetTextureNative("tDepth", tD, IPF_POINT | IPF_CLAMP);
	pIP->SetOutputNative(0, sHiZ[0]);
	done = pIP->Execute(true) && done;
	for (int k = 1; k < L && done; k++) { // level k into the scratch, then copied in
		done = Pass("PSHiZDown", tHiZ, sTmp[k - 1], float(k - 1));
		if (done) dev->StretchRect(sTmp[k - 1], NULL, sHiZ[k], NULL, VK_FILTER_NEAREST);
	}
	dev->PrepareSample(tCol); // the colour levels now, and both read-only before the next draw (no layout change inside a main pass)
	dev->PrepareSample(tHiZ);
	gc->PushRenderTarget(target, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // the main target, viewport and depth back
	gc->PopRenderTargets();
	dev->SetState(st);
	if (!done) {
		if (!logged) LogErr("SceneSsr: a capture pass failed, no reflections this frame");
		logged = true;
		return;
	}

	last.frame = f;
	last.cam = V3(scn->Camera.pos);
	last.camZ = V3(scn->Camera.z);
	D3DXMATRIX P = scn->Camera.mProj;
	P._31 = P._32 = 0.0f; // the colour is unjittered; the depth's jitter is kept apart
	last.VP = Mul(M4(scn->Camera.mView), M4(P));
	last.jx = scn->bJitter ? scn->jitX : 0.0f;
	last.jy = scn->bJitter ? scn->jitY : 0.0f;
	last.e = scn->GetExposure();
	last.moved = movedNow;
	last.proxy = scn->Camera.hObj_proxy;
	last.terrain = terrainRec && terrainFrame == f;
	last.bodies.clear();
	Body pb;
	pb.R = RotY(0.0);
	pb.o = { 0.0, 0.0, 0.0 };
	if (const Body *q = NowOf(last.proxy)) pb = *q;
	last.bodies.push_back(pb);
	for (Scene::VOBJREC *pv = scn->vobjFirst; pv; pv = pv->next) {
		if (pv->type != OBJTP_VESSEL || !pv->vobj->IsActive() || !pv->vobj->IsVisible()) continue;
		if (const Body *q = NowOf(pv->vobj->Object())) last.bodies.push_back(*q);
	}
	last.valid = true;
}

void SceneSsr::PlanetBind (Scene *scn, vPlanet *vp, ShaderClass *sh, bool depthRec)
{
	if (!sh || !sh->GetPSHandle("gSsr")) return; // the Earth shader with _SSR only
	SsrPrm p;
	memset(&p, 0, sizeof(p));
	SceneSsr *s = Of(scn);
	if (s && vp && scn->GetRenderPass() == RENDERPASS_MAINSCENE && vp == scn->GetCameraProxyVisual()) {
		s->terrainFrame = scn->GetFrameId();
		s->terrainRec = depthRec;
		double alt = vp->CameraAltitude();
		if (alt < 30e3) { // from higher up the sky over the water is not on screen
			DWORD pln = *(DWORD *)s->gc->GetConfigParam(CFGPRM_PLANETARIUMFLAG);
			float sky = (pln & PLN_ENABLE) ? 0.0f : 1.0f - float(SsrMath::Smoothstep(3e3, 12e3, alt));
			s->Params(vp->Object(), VESSEL, sky, true, p);
		}
	}
	sh->SetPSConstants("gSsr", &p, sizeof(p)); // every pass: the shader keeps the last values
	sh->SetTexture("tSsrCol", s ? s->tCol : NULL, IPF_LINEAR | IPF_CLAMP);
	sh->SetTexture("tSsrHiZ", s ? s->tHiZ : NULL, IPF_POINT | IPF_CLAMP);
}

SceneSsr::Scope::Scope (Scene *scn, OBJHANDLE body, int kind, bool internal)
{
	SceneSsr *q = Of(scn);
	VkEffect *fx = D3D9Effect::FX;
	if (!q || !fx) return;
	if (!q->hSsr) {
		q->hSsr = fx->GetParameterByName(0, "gSsr");
		q->hCol = fx->GetParameterByName(0, "tSsrCol");
		q->hHiZ = fx->GetParameterByName(0, "tSsrHiZ");
	}
	if (!q->hSsr || !q->hCol || !q->hHiZ) return;
	SsrPrm p;
	memset(&p, 0, sizeof(p));
	if (!internal && body && scn->GetRenderPass() == RENDERPASS_MAINSCENE) { // not the cockpit, env, custom camera, shadow or depth passes
		DWORD pln = *(DWORD *)q->gc->GetConfigParam(CFGPRM_PLANETARIUMFLAG);
		q->Params(body, kind, (pln & PLN_ENABLE) ? 0.0f : 1.0f, false, p);
	}
	fx->SetValue(q->hSsr, &p, sizeof(p));
	fx->SetTexture(q->hCol, q->tCol);
	fx->SetTexture(q->hHiZ, q->tHiZ);
	s = q;
}

SceneSsr::Scope::~Scope ()
{
	if (!s || !D3D9Effect::FX) return;
	SsrPrm z;
	memset(&z, 0, sizeof(z));
	D3D9Effect::FX->SetValue(s->hSsr, &z, sizeof(z));
}
