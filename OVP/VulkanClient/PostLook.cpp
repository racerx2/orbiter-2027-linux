// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: heat haze, light shafts, lens flare, sharpening and vignette (design-visual-batch 4-7)

#include "PostLook.h"
#include "Scene.h"
#include "SceneFx.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "IProcess.h"
#include "VPlanet.h"
#include "VVessel.h"
#include "Cloudmgr2.h"
#include "LookMath.h"
#include "PostPlume.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

static const VkImageUsageFlags RTUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
static const char *LookFile = "Modules/VulkanClient/PostLook.glsl";

PostLook::PostLook (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h)
	: gc(_gc), scn(_scn), dev(_gc->GetDevice()), W(w), H(h)
{
	tsc = new TerrainSunCache;
	if (Config->HeatHaze) {
		pHaze = new ImageProcessing(dev, LookFile, "PSHaze");
		if (!pHaze->IsOK()) { LogErr("PostLook: PSHaze failed, heat haze off"); SAFE_DELETE(pHaze); }
	}
	if (Config->LightShafts) {
		bool ok = true;
		for (int i = 0; i < 2; i++) {
			tShaft[i] = new VkTex(dev, std::max(1u, W / 2), std::max(1u, H / 2), 1, VK_FORMAT_R16_SFLOAT, RTUsage);
			sShaft[i] = new VkSurf(tShaft[i], 0);
			ok = ok && tShaft[i]->img;
		}
		pShaft = new ImageProcessing(dev, LookFile, "PSShaftMask", "_SHAFTS");
		pShaft->CompileShader("PSShaftBlur");
		if (!ok || !pShaft->IsOK()) { LogErr("PostLook: shaft targets or shaders failed, light shafts off"); SAFE_DELETE(pShaft); }
	}
	std::string f;
	if (pShaft) f += "_SHAFTS ";
	if (Config->LensFlare) f += "_FLARE ";
	if (Config->Sharpen) f += "_SHARPEN ";
	if (Config->Vignette) f += "_VIGNETTE ";
	if (!f.empty()) {
		pFinal = new ImageProcessing(dev, LookFile, "PSFinal", f.c_str());
		if (!pFinal->IsOK()) { LogErr("PostLook: PSFinal failed, shafts, flare, sharpening and vignette off"); SAFE_DELETE(pFinal); }
	}
	LogAlw("PostLook: haze %d, final [%s]", pHaze ? 1 : 0, pFinal ? f.c_str() : "");
}

PostLook::~PostLook ()
{
	SAFE_DELETE(pFinal);
	SAFE_DELETE(pShaft);
	SAFE_DELETE(pHaze);
	for (int i = 0; i < 2; i++) {
		SAFE_DELETE(sShaft[i]); SAFE_DELETE(tShaft[i]);
		SAFE_DELETE(sCopy[i]); SAFE_DELETE(tCopy[i]);
	}
	delete tsc;
}

VkTex *PostLook::FxDepth () const { return scn->pFx ? scn->pFx->Depth() : nullptr; }

VkTex *PostLook::FxVolume () const { return scn->pFx ? scn->pFx->VolumeTex() : nullptr; }

VkTex *PostLook::Copy (VkSurf *src, int slot)
{
	VkTex *&t = tCopy[slot];
	if (!t || t->w != src->w || t->h != src->h || t->fmt != src->tex->fmt) {
		SAFE_DELETE(sCopy[slot]);
		SAFE_DELETE(t);
		t = new VkTex(dev, src->w, src->h, 1, src->tex->fmt, RTUsage);
		sCopy[slot] = new VkSurf(t, 0);
		if (!t->img) {
			LogErr("PostLook: copy target failed");
			SAFE_DELETE(sCopy[slot]);
			SAFE_DELETE(t);
			return nullptr;
		}
	}
	dev->StretchRect(src, NULL, sCopy[slot], NULL, VK_FILTER_NEAREST); // resolves MSAA
	return t;
}

// exhausts of the visible vessels as screen segments, nearest first
int PostLook::Exhausts (D3DXVECTOR4 *seg, int rect[4])
{
	struct Cand { double dist; D3DXVECTOR4 a, b, c; double x0, y0, x1, y1; };
	std::vector<Cand> list;
	double vp[16];
	const float *m = (const float *)&scn->Camera.mProjView; // unjittered: the jitter is off after the resolve
	for (int i = 0; i < 16; i++) vp[i] = m[i];
	const double pxs = scn->Camera.mProj._22 * 0.5 * H; // pixels per metre at 1 m
	for (Scene::VOBJREC *pv = scn->vobjFirst; pv; pv = pv->next) {
		vObject *vo = pv->vobj;
		if (!vo || !vo->IsActive() || !vo->IsVisible() || vo->GetMeshCount() < 1) continue;
		if (oapiGetObjectType(vo->Object()) != OBJTP_VESSEL) continue;
		vVessel *vv = (vVessel *)vo;
		VESSEL *v = vv->GetInterface();
		if (!v) continue;
		const D3DXMATRIX *M = vv->MWorld();
		DWORD n = v->GetExhaustCount();
		for (DWORD i = 0; i < n; i++) {
			double lvl = v->GetExhaustLevel(i);
			if (lvl <= 0.02) continue;
			EXHAUSTSPEC es;
			if (!v->GetExhaustSpec(i, &es) || !es.lpos || !es.ldir) continue;
			double hwA = 1.2 * es.wsize, hwB = 2.0 * es.wsize, hl = es.lsize * lvl; // the plume's half-width at the nozzle and the end, its length, m
			PostPlume::HazeShape(vv, i, &hwA, &hwB, &hl); // the volumetric plume's when it draws this exhaust (design-volumetric-plumes 10)
			VECTOR3 ref = *es.lpos - *es.ldir * es.lofs, end = ref - *es.ldir * hl;
			D3DXVECTOR3 la(float(ref.x), float(ref.y), float(ref.z)), lb(float(end.x), float(end.y), float(end.z)), wa, wb;
			D3DXVec3TransformCoord(&wa, &la, M); // camera-relative
			D3DXVec3TransformCoord(&wb, &lb, M);
			const double a[3] = { wa.x, wa.y, wa.z }, b[3] = { wb.x, wb.y, wb.z };
			LookMath::Seg s;
			if (!LookMath::ProjectSegment(vp, a, b, W, H, 0.05, s)) continue;
			double w0 = std::min(0.25 * H, (hwA + (hwB - hwA) * s.t0) * pxs / s.w0);
			double w1 = std::min(0.25 * H, (hwA + (hwB - hwA) * s.t1) * pxs / s.w1);
			if (std::max(w0, w1) < 1.5) continue; // too far to show
			double ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2], ll = ex * ex + ey * ey + ez * ez;
			double k = ll > 1e-12 ? std::clamp(-(a[0] * ex + a[1] * ey + a[2] * ez) / ll, 0.0, 1.0) : 0.0;
			double dmin = std::sqrt(std::pow(a[0] + ex * k, 2) + std::pow(a[1] + ey * k, 2) + std::pow(a[2] + ez * k, 2));
			double fade = LookMath::Smoothstep(1.0, 4.0, dmin / std::max(0.05, es.wsize)) * std::min(1.0, std::max(w0, w1) / 30.0); // camera in the plume; small plumes
			double str = lvl * fade;
			if (str < 0.005) continue;
			double dn = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
			double r = std::max(w0, w1) + 6.0;
			Cand c;
			c.dist = dn;
			c.a = D3DXVECTOR4(float(s.x0), float(s.y0), float(s.x1), float(s.y1));
			c.b = D3DXVECTOR4(float(w0), float(w1), float(str), float(dn));
			c.c = D3DXVECTOR4(float(s.t0), float(s.t1), 0.0f, 0.0f);
			c.x0 = std::min(s.x0, s.x1) - r; c.x1 = std::max(s.x0, s.x1) + r;
			c.y0 = std::min(s.y0, s.y1) - r; c.y1 = std::max(s.y0, s.y1) + r;
			list.push_back(c);
		}
	}
	std::sort(list.begin(), list.end(), [](const Cand &p, const Cand &q) { return p.dist < q.dist; });
	if (list.size() > size_t(MAXSEG)) list.resize(MAXSEG);
	double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
	for (size_t i = 0; i < list.size(); i++) {
		seg[3 * i] = list[i].a; seg[3 * i + 1] = list[i].b; seg[3 * i + 2] = list[i].c;
		x0 = std::min(x0, list[i].x0); y0 = std::min(y0, list[i].y0);
		x1 = std::max(x1, list[i].x1); y1 = std::max(y1, list[i].y1);
	}
	rect[0] = std::max(0, int(std::floor(x0))); rect[1] = std::max(0, int(std::floor(y0)));
	rect[2] = std::min(int(W), int(std::ceil(x1))); rect[3] = std::min(int(H), int(std::ceil(y1)));
	if (list.empty() || rect[2] <= rect[0] || rect[3] <= rect[1]) return 0;
	return int(list.size());
}

void PostLook::Haze (VkSurf *target)
{
	if (!pHaze || !target) return;
	if (scn->CameraInSpace() >= 0.9f) return; // no air
	D3DXVECTOR4 seg[3 * MAXSEG] = {};
	int rect[4];
	int n = Exhausts(seg, rect);
	if (n <= 0) return;
	VkDev::State st = dev->GetState();
	if (VkTex *cp = Copy(target, 0)) {
		VkTex *dep = FxDepth();
		float time = float(std::fmod(oapiGetSysTime(), 3600.0));
		D3DXVECTOR4 tex(1.0f / float(W), 1.0f / float(H), float(W) / float(H), time);
		D3DXVECTOR4 rc(float(rect[0]) / float(W), float(rect[1]) / float(H), float(rect[2] - rect[0]) / float(W), float(rect[3] - rect[1]) / float(H));
		D3DXVECTOR4 hz(float(n), time, dep ? 1.0f : 0.0f, 0.0f);
		pHaze->Activate("PSHaze");
		pHaze->SetTemplate(rc.z, rc.w, rc.x, rc.y); // the segments' bounds only
		pHaze->SetFloat("vTex", &tex, sizeof(tex));
		pHaze->SetFloat("vRect", &rc, sizeof(rc));
		pHaze->SetFloat("vHaze", &hz, sizeof(hz));
		pHaze->SetFloat("vSeg", seg, sizeof(seg));
		pHaze->SetTextureNative("tScene", cp, IPF_LINEAR | IPF_CLAMP);
		pHaze->SetTextureNative("tDepthL", dep ? dep : cp, IPF_POINT | IPF_CLAMP);
		pHaze->SetOutputNative(0, target);
		pHaze->Execute(true);
	}
	gc->PushRenderTarget(target, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // the main target, viewport and depth back
	gc->PopRenderTargets();
	dev->SetState(st);
}

void PostLook::Final (VkSurf *bb)
{
	if (!pFinal || !bb) return;
	if (scn->pOffscreenTarget && !(scn->pLightBlur && scn->pLightBlur->IsOK())) return; // nothing has reached the backbuffer

	VECTOR3 gsun;
	oapiGetGlobalPos(oapiGetObjectByIndex(0), &gsun);
	const VECTOR3 cam = scn->Camera.pos;
	const VECTOR3 usun = unit(gsun - cam);
	oapi::FVECTOR2 pt;
	bool front = scn->WorldToScreenSpace2(usun * 1e5, &pt, NULL, 1e9f); // unjittered, as the glare; false behind the camera
	double su = (pt.x - 0.5) / double(W), sv = (pt.y - 0.5) / double(H);
	float cis = scn->CameraInSpace();
	bool panels = oapiCameraInternal() && oapiCockpitMode() == COCKPIT_PANELS; // the 2D panel may hide the sun: no ghosts or rays from it

	vPlanet *vn = scn->Camera.vNear, *vpx = scn->Camera.vProxy;
	FVECTOR4 clr(1.0f, 1.0f, 1.0f, 1.0f);
	if (vn && vn->IsActive()) clr = vn->SunLightColor(vn->CameraPos(), 2.0); // as the glare
	float mx = clr.MaxRGB();
	D3DXVECTOR4 col(1.0f, 1.0f, 1.0f, 0.0f);
	if (mx > 1e-4f) col = D3DXVECTOR4(clr.r / mx, clr.g / mx, clr.b / mx, 0.0f);

	float shafts = 0.0f;
	if (pShaft && front && !panels && cis < 0.9f && mx > 1e-3f) {
		double dc = std::hypot(pt.x - 0.5 * W, pt.y - 0.5 * H) / double(W);
		double hor = 1.0;
		if (vpx) { // the sun over the local horizon, dip included
			VECTOR3 P;
			oapiGetGlobalPos(vpx->Object(), &P);
			VECTOR3 up = cam - P;
			double r = length(up), R = vpx->GetSize();
			up /= r;
			double el = asin(std::clamp(dotp(up, usun), -1.0, 1.0)), dip = acos(std::min(1.0, R / r));
			hor = LookMath::Smoothstep(-0.01, 0.03, el + dip);
		}
		shafts = float(0.35 * (1.0 - cis) * (1.0 - LookMath::Smoothstep(1.0, 1.5, dc)) * hor);
	}

	float flare = 0.0f;
	if (Config->LensFlare && front && !panels && mx > 1e-3f) {
		double edge = LookMath::EdgeFade(2.0 * su - 1.0, 1.0 - 2.0 * sv);
		if (edge > 0.0) {
			double ter = (vn && vn->IsActive()) ? vn->TerrainSunVisibility(cam, *tsc) : 1.0;
			double cld = (vpx && vpx->CloudMgr2()) ? CloudSunAlpha(vpx->CloudMgr2(), scn->dwFrameId) : 0.0;
			double e = std::clamp(double(scn->fExposure), 0.25, 2.0);
			flare = float(0.25 * edge * ter * (1.0 - cld) * mx * e);
		}
	}

	if (shafts < 1e-3f && flare < 1e-3f && !Config->Sharpen && !Config->Vignette) return; // nothing this frame

	VkDev::State st = dev->GetState();
	if (VkTex *cp = Copy(bb, 1)) {
		VkTex *dep = FxDepth(), *vol = FxVolume();
		VkTex *gbuf = scn->ptgBuffer[GBUF_DEPTH], *vis = scn->pLocalResults;
		float time = float(std::fmod(oapiGetSysTime(), 3600.0));
		D3DXVECTOR4 tex(1.0f / float(W), 1.0f / float(H), float(W) / float(H), time);
		D3DXVECTOR4 sun(float(su), float(sv), shafts, flare);
		D3DXVECTOR4 flags(dep ? 1.0f : 0.0f, gbuf ? 1.0f : 0.0f, vol ? 1.0f : 0.0f, vis ? 1.0f : 0.0f);
		D3DXVECTOR4 sharp(scn->TaaActive() ? 0.18f : 0.12f, 0.0f, 0.0f, 0.0f); // more after TAA's softening

		bool rays = false;
		if (shafts >= 1e-3f) {
			pShaft->Activate("PSShaftMask");
			pShaft->SetFloat("vTex", &tex, sizeof(tex));
			pShaft->SetFloat("vSun", &sun, sizeof(sun));
			pShaft->SetFloat("vFlags", &flags, sizeof(flags));
			pShaft->SetTextureNative("tScene", cp, IPF_LINEAR | IPF_CLAMP);
			pShaft->SetTextureNative("tDepthL", dep ? dep : cp, IPF_POINT | IPF_CLAMP);
			pShaft->SetTextureNative("tGbuf", gbuf ? gbuf : cp, IPF_POINT | IPF_CLAMP);
			pShaft->SetOutputNative(0, sShaft[0]);
			if (pShaft->Execute(true)) {
				pShaft->Activate("PSShaftBlur");
				pShaft->SetFloat("vTex", &tex, sizeof(tex));
				pShaft->SetFloat("vSun", &sun, sizeof(sun));
				pShaft->SetTextureNative("tShaft", tShaft[0], IPF_LINEAR | IPF_CLAMP);
				pShaft->SetOutputNative(0, sShaft[1]);
				rays = pShaft->Execute(true);
			}
		}
		if (!rays) sun.z = 0.0f;

		pFinal->Activate("PSFinal");
		pFinal->SetFloat("vTex", &tex, sizeof(tex));
		pFinal->SetFloat("vSun", &sun, sizeof(sun));
		pFinal->SetFloat("vSunCol", &col, sizeof(col));
		pFinal->SetFloat("vFlags", &flags, sizeof(flags));
		pFinal->SetFloat("vSharp", &sharp, sizeof(sharp));
		pFinal->SetTextureNative("tScene", cp, IPF_POINT | IPF_CLAMP);
		if (pShaft) pFinal->SetTextureNative("tShaft", rays ? tShaft[1] : cp, IPF_LINEAR | IPF_CLAMP); // only the variants that sample them
		if (Config->LensFlare) {
			pFinal->SetTextureNative("tDepthL", dep ? dep : cp, IPF_POINT | IPF_CLAMP);
			pFinal->SetTextureNative("tVis", vis ? vis : cp, IPF_POINT | IPF_CLAMP);
			pFinal->SetTextureNative("tVolL", vol ? vol : cp, IPF_LINEAR | IPF_CLAMP);
		}
		pFinal->SetOutputNative(0, bb);
		pFinal->Execute(true);
	}
	gc->PushRenderTarget(bb, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // viewport and target back
	gc->PopRenderTargets();
	dev->SetState(st);
}
