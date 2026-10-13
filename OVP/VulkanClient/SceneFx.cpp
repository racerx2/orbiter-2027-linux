// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the SceneDepth pass, SSAO, volumetric clouds and the exposure measure (design-ground-graphics 1, 3, 4, 6, 7)

#include "SceneFx.h"
#include "Scene.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "IProcess.h"
#include "VPlanet.h"
#include "VVessel.h"
#include "Cloudmgr2.h"
#include "GfxPool.h"
#include "VolCloudMap.h"
#include <algorithm>
#include <cmath>

static const VkImageUsageFlags RTUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

// background job results, shared with GfxPool's threads
struct SceneFx::CloudBox {
	std::mutex mx;
	bool noiseDone = false, noiseDropped = false;
	std::vector<uint8_t> shape, detail;
	bool mapDone = false, mapDropped = false;
	std::vector<uint8_t> map;
	VolCloud::Frame frame;
	OBJHANDLE obj = NULL;
};

SceneFx::SceneFx (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h)
	: gc(_gc), scn(_scn), dev(_gc->GetDevice()), W(w), H(h)
{
	if (Config->SSAO || Config->VolClouds) InitDepth();
	if (Config->SSAO) InitSSAO();
	if (Config->VolClouds) InitClouds();
	if (Config->AutoExposure) InitExposure();
}

void SceneFx::InitDepth ()
{
	tDepth = new VkTex(dev, W, H, 1, VK_FORMAT_R32_SFLOAT, RTUsage);
	sDepth = new VkSurf(tDepth, 0);
	sDepthDS = new VkSurf(dev, W, H, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	if (!sDepthDS->tex->img) { // no D32 attachments: the framework's format
		delete sDepthDS;
		sDepthDS = new VkSurf(dev, W, H, gc->GetFramework()->GetDepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	}
	pTerrain = new ShaderClass(dev, "Modules/VulkanClient/SceneDepth.glsl", "TerrainDepthVS", "TerrainDepthPS", "TerrainDepth", "");
	depthOK = tDepth->img && sDepthDS->tex->img;
	if (!depthOK) LogErr("SceneFx: SceneDepth targets failed, SSAO and volume clouds off");
}

void SceneFx::InitSSAO ()
{
	for (int i = 0; i < 2; i++) {
		tAO[i] = new VkTex(dev, std::max(1u, W / 2), std::max(1u, H / 2), 1, VK_FORMAT_R8_UNORM, RTUsage);
		sAO[i] = new VkSurf(tAO[i], 0);
	}
	pAO = new ImageProcessing(dev, "Modules/VulkanClient/SSAO.glsl", "PSAO");
	pAO->CompileShader("PSBlur");
	pAO->CompileShader("PSComposite");
	if (!depthOK || !tAO[0]->img || !tAO[1]->img || !pAO->IsOK()) {
		LogErr("SceneFx: SSAO targets or shaders failed, SSAO off");
		SAFE_DELETE(pAO);
	}
}

void SceneFx::InitClouds ()
{
	for (int i = 0; i < 2; i++) {
		tVol[i] = new VkTex(dev, std::max(1u, W / 2), std::max(1u, H / 2), 1, VK_FORMAT_R16G16B16A16_SFLOAT, RTUsage);
		sVol[i] = new VkSurf(tVol[i], 0);
	}
	pVol = new ImageProcessing(dev, "Modules/VulkanClient/VolCloud.glsl", "VolMarch", Config->bAtmoQuality ? NULL : "_PERFORMANCE");
	pVol->CompileShader("VolBlur");
	pVol->CompileShader("VolComposite");
	cbox = std::make_shared<CloudBox>();
	if (!depthOK || !tVol[0]->img || !tVol[1]->img || !pVol->IsOK()) {
		LogErr("SceneFx: volume cloud targets or shaders failed, volume clouds off");
		SAFE_DELETE(pVol);
	}
}

void SceneFx::InitExposure ()
{
	const VkImageUsageFlags blit = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	UINT w = W, h = H;
	bool ok = true;
	for (int i = 0; i < 3; i++) {
		w = std::max(1u, w / 2); h = std::max(1u, h / 2);
		tHalf[i] = new VkTex(dev, w, h, 1, VK_FORMAT_R16G16B16A16_SFLOAT, blit);
		sHalf[i] = new VkSurf(tHalf[i], 0);
		ok = ok && tHalf[i]->img;
	}
	tBlocks = new VkTex(dev, 16, 9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, RTUsage);
	sBlocks = new VkSurf(tBlocks, 0);
	tTotal = new VkTex(dev, 1, 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, RTUsage);
	sTotal = new VkSurf(tTotal, 0);
	pExpo = new ImageProcessing(dev, "Modules/VulkanClient/Exposure.glsl", "PSBlocks");
	pExpo->CompileShader("PSTotal");
	AutoExpo::Params p;
	p.minEV = Config->AutoExpMinEV;
	p.maxEV = Config->AutoExpMaxEV;
	expo.SetParams(p);
	if (!ok || !tBlocks->img || !tTotal->img || !pExpo->IsOK()) {
		LogErr("SceneFx: exposure targets or shaders failed, auto-exposure off");
		SAFE_DELETE(pExpo);
	}
}

SceneFx::~SceneFx ()
{
	if (poolId) if (GfxPool *p = GfxPool::Peek()) p->Unregister(poolId);
	SAFE_DELETE(pVol);
	SAFE_DELETE(tCov);
	SAFE_DELETE(tShape);
	SAFE_DELETE(tDetail);
	for (int i = 0; i < 2; i++) { SAFE_DELETE(sVol[i]); SAFE_DELETE(tVol[i]); }
	SAFE_DELETE(pExpo);
	SAFE_DELETE(sTotal);
	SAFE_DELETE(tTotal);
	SAFE_DELETE(sBlocks);
	SAFE_DELETE(tBlocks);
	for (int i = 0; i < 3; i++) { SAFE_DELETE(sHalf[i]); SAFE_DELETE(tHalf[i]); }
	SAFE_DELETE(pAO);
	SAFE_DELETE(pTerrain);
	for (int i = 0; i < 2; i++) { SAFE_DELETE(sAO[i]); SAFE_DELETE(tAO[i]); }
	SAFE_DELETE(sDepthDS);
	SAFE_DELETE(sDepth);
	SAFE_DELETE(tDepth);
}

// below 20 km over the ground, or a vessel within 2 km
bool SceneFx::WantSSAO (const std::list<vVessel*> &vessels) const
{
	if (!Config->SSAO || !pAO) return false;
	if (scn->GetCameraProxyVisual() && scn->GetCameraElevation() < 20e3) return true; // GetCameraElevation: height over the ground
	for (const vVessel *v : vessels) if (v->CamDist() < 2e3) return true;
	return false;
}

// the camera's proxy has a cloud layer under 20 km and the camera is less than 10-15 km above it
bool SceneFx::CloudsWanted (vPlanet *vp, float *wV) const
{
	*wV = 0.0f;
	if (!pVol || !vp || !vp->CloudMgr2() || !vp->HasAtmosphere() || !vp->prm.bCloud || !vp->GetScatterConst()) return false;
	if (vp->GetShaderID() == PLT_GIANT || vp->prm.cloudalt >= 20e3) return false;
	double x = std::min(1.0, std::max(0.0, (vp->CameraAltitude() - (vp->prm.cloudalt + 10e3)) / 5e3));
	*wV = float(1.0 - x * x * (3.0 - 2.0 * x));
	return *wV > 0.0f;
}

// noise once, the coverage map around the camera when it moves 20 km or every 10 s; uploads the results
void SceneFx::CloudUpdate (vPlanet *vp)
{
	GfxPool &pool = GfxPool::Get();
	if (!poolId) poolId = pool.Register();
	std::shared_ptr<CloudBox> box = cbox;
	if (!noiseAsked) {
		noiseAsked = true;
		GfxPool::Job j;
		j.owner = poolId;
		j.kind = GfxPool::NOISE;
		j.run = [box]() {
			std::vector<uint8_t> s, d;
			VolCloud::ShapeNoise(64, s);
			VolCloud::DetailNoise(32, d);
			std::lock_guard<std::mutex> lk(box->mx);
			box->shape.swap(s);
			box->detail.swap(d);
			box->noiseDone = true;
		};
		j.dropped = [box]() { std::lock_guard<std::mutex> lk(box->mx); box->noiseDropped = true; };
		pool.Submit(std::move(j));
	}
	{
		std::lock_guard<std::mutex> lk(box->mx);
		if (box->noiseDropped) { box->noiseDropped = false; noiseAsked = false; }
		if (box->noiseDone && !tShape) {
			const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			tShape = new VkTex(dev, VK_FORMAT_R8G8B8A8_UNORM, 64, 64, 64, u);
			tShape->Upload(0, 0, box->shape.data(), box->shape.size());
			tDetail = new VkTex(dev, VK_FORMAT_R8G8B8A8_UNORM, 32, 32, 32, u);
			tDetail->Upload(0, 0, box->detail.data(), box->detail.size());
			std::vector<uint8_t>().swap(box->shape);
			std::vector<uint8_t>().swap(box->detail);
		}
		if (box->mapDropped) { box->mapDropped = false; mapAsked = false; }
		if (box->mapDone) {
			box->mapDone = false;
			mapAsked = false;
			if (!tCov) tCov = new VkTex(dev, VolCloud::MAPN, VolCloud::MAPN, 1, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
			tCov->Upload(0, 0, box->map.data(), box->map.size());
			mapFrame = box->frame;
			mapObj = box->obj;
			mapValid = tCov->img != VK_NULL_HANDLE;
		}
	}
	if (!tShape || mapAsked) return;

	OBJHANDLE hP = vp->Object();
	MATRIX3 R;
	VECTOR3 P;
	oapiGetRotationMatrix(hP, &R);
	oapiGetGlobalPos(hP, &P);
	VECTOR3 cl = tmul(R, scn->GetCameraGPos() - P); // planet-local
	double Rp = vp->GetSize();
	double lat = asin(std::max(-1.0, std::min(1.0, cl.y / length(cl)))), lng = atan2(cl.z, cl.x);
	double lngc = VolCloud::CloudLng(lng, vp->prm.cloudrot);
	bool valid = mapValid && mapObj == hP;
	double x = 1e30, y = 1e30, now = oapiGetSysTime();
	if (valid) mapFrame.ToMap(lat, lngc, Rp, &x, &y);
	bool moved = VolCloud::NeedRebuild(x, y, valid);
	if (!moved && now - mapTime < 10.0) return;
	std::vector<VolCloud::Src> srcs;
	vp->CloudMgr2()->VolTiles(lat, lngc, (VolCloud::MAPHALF + 20e3) / Rp, srcs);
	size_t sig = 0;
	for (const VolCloud::Src &s : srcs) sig = sig * 31 + size_t(s.d.get()) + size_t(s.lvl);
	mapTime = now;
	if (srcs.empty()) { mapTime = now - 9.0; return; } // nothing loaded yet: again in a second
	if (!moved && sig == mapSig) return;
	mapSig = sig;
	VolCloud::Frame f;
	f.Set(lat, lngc);
	GfxPool::Job j;
	j.owner = poolId;
	j.kind = GfxPool::CLOUDMAP;
	j.run = [box, srcs, f, Rp, hP]() {
		std::vector<uint8_t> m;
		VolCloud::Fill(f, Rp, srcs, m);
		std::lock_guard<std::mutex> lk(box->mx);
		box->map.swap(m);
		box->frame = f;
		box->obj = hP;
		box->mapDone = true;
	};
	j.dropped = [box]() { std::lock_guard<std::mutex> lk(box->mx); box->mapDropped = true; };
	pool.Submit(std::move(j));
	mapAsked = true;
}

void SceneFx::AfterOpaque (const std::list<vVessel*> &vessels, VkSurf *target)
{
	frame++;
	vPlanet *vp = scn->GetCameraProxyVisual();
	float wV = 0.0f;
	bool clouds = CloudsWanted(vp, &wV);
	if (clouds) CloudUpdate(vp);
	clouds = clouds && tShape && tDetail && tCov && mapValid && mapObj == vp->Object();
	cloudFade = clouds ? wV : 0.0f; // the 2D layer near the camera fades from the next frame on
	bool ssao = WantSSAO(vessels);
	if (!ssao && !clouds) return;
	VkDev::State st = dev->GetState();
	DepthPass(vessels);
	if (ssao) Ssao(target);
	if (clouds) Clouds(target, vp, wV);
	gc->PushRenderTarget(target, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // the main target, viewport and depth back
	gc->PopRenderTargets();
	dev->SetState(st);
}

void SceneFx::DepthPass (const std::list<vVessel*> &vessels)
{
	scn->PushCameraFrustumLimits(std::min(0.5f, scn->GetCameraNearPlane()), 1e6f);
	scn->BeginPass(RENDERPASS_SCENE_DEPTH);
	gc->PushRenderTarget(sDepth, sDepthDS, RENDERPASS_SCENE_DEPTH);
	dev->Clear(true, true, true, 0, 1.0f, 0);
	dev->SetCullMode(VK_CULL_MODE_BACK_BIT);
	D3DXMATRIX mVP = *scn->GetProjectionViewMatrix();
	if (vPlanet *vp = scn->GetCameraProxyVisual()) vp->RenderDepth(dev, pTerrain, mVP);
	for (vVessel *v : vessels) v->Render(dev, false);
	gc->PopRenderTargets();
	scn->PopPass();
	scn->PopCameraFrustumLimits();
}

void SceneFx::Ssao (VkSurf *target)
{
	const D3DXMATRIX *P = scn->GetProjectionMatrix();
	D3DXVECTOR4 proj(1.0f / P->_11, 1.0f / P->_22, 1.0f / float(W), 1.0f / float(H));
	D3DXVECTOR4 prm(Config->SSAORadius, 0.004f, 8.0f, Config->SSAOStrength);
	D3DXVECTOR4 fade(250.0f, 600.0f, float(frame % 64), 12.0f);
	const D3D9Sun *sun = scn->GetSun();
	D3DXVECTOR3 s(-sun->Dir.x, -sun->Dir.y, -sun->Dir.z); // toward the sun
	float up = std::min(1.0f, std::max(sun->Color.x, std::max(sun->Color.y, sun->Color.z)));
	D3DXVECTOR4 vsun(D3DXVec3Dot(&s, scn->GetCameraX()), D3DXVec3Dot(&s, scn->GetCameraY()), D3DXVec3Dot(&s, scn->GetCameraZ()), up);
	D3DXVECTOR2 aot(1.0f / float(tAO[0]->w), 1.0f / float(tAO[0]->h));

	pAO->Activate("PSAO");
	pAO->SetFloat("vProj", &proj, sizeof(proj));
	pAO->SetFloat("vPrm", &prm, sizeof(prm));
	pAO->SetFloat("vFade", &fade, sizeof(fade));
	pAO->SetFloat("vSun", &vsun, sizeof(vsun));
	pAO->SetFloat("vAOTexel", &aot, sizeof(aot));
	pAO->SetTextureNative("tDepth", tDepth, IPF_POINT | IPF_CLAMP);
	pAO->SetOutputNative(0, sAO[0]);
	if (!pAO->Execute(true)) return;

	pAO->Activate("PSBlur");
	for (int pass = 0; pass < 2; pass++) {
		D3DXVECTOR2 dir = pass ? D3DXVECTOR2(0.0f, aot.y) : D3DXVECTOR2(aot.x, 0.0f);
		pAO->SetFloat("vDir", &dir, sizeof(dir));
		pAO->SetTextureNative("tDepth", tDepth, IPF_POINT | IPF_CLAMP);
		pAO->SetTextureNative("tAO", tAO[pass], IPF_POINT | IPF_CLAMP);
		pAO->SetOutputNative(0, sAO[1 - pass]);
		pAO->Execute(true);
	}

	pAO->Activate("PSComposite");
	pAO->SetTextureNative("tDepth", tDepth, IPF_POINT | IPF_CLAMP);
	pAO->SetTextureNative("tAO", tAO[0], IPF_POINT | IPF_CLAMP);
	pAO->SetOutputNative(0, target);
	pAO->Execute(1, true);
}

// march at half resolution, blur, then over the scene
void SceneFx::Clouds (VkSurf *target, vPlanet *vp, float wV)
{
	OBJHANDLE hP = vp->Object();
	MATRIX3 R;
	oapiGetRotationMatrix(hP, &R);
	double rot = vp->prm.cloudrot, ca = cos(rot), sa = sin(rot);
	auto ToGlobal = [&](const double *c) { return mul(R, _V(c[0] * ca + c[2] * sa, c[1], c[2] * ca - c[0] * sa)); }; // cloud frame to global
	auto V4 = [](const VECTOR3 &v, double w) { return D3DXVECTOR4(float(v.x), float(v.y), float(v.z), float(w)); };

	const D3DXMATRIX *P = scn->GetProjectionMatrix();
	const D3DXVECTOR3 *cx = scn->GetCameraX(), *cy = scn->GetCameraY(), *cz = scn->GetCameraZ();
	D3DXVECTOR4 camX(cx->x, cx->y, cx->z, 1.0f / P->_11), camY(cy->x, cy->y, cy->z, 1.0f / P->_22), camZ(cz->x, cz->y, cz->z, 0.0f);
	D3DXVECTOR4 mapE = V4(ToGlobal(mapFrame.E), 1.0 / VolCloud::MAPHALF), mapN = V4(ToGlobal(mapFrame.N), 0.0), mapU = V4(ToGlobal(mapFrame.U), 0.0);
	D3DXVECTOR4 clA = V4(mul(R, _V(ca, 0, -sa)), 0.0), clB = V4(mul(R, _V(0, 1, 0)), 0.0), clC = V4(mul(R, _V(sa, 0, ca)), 0.0);
	double Rp = vp->GetSize(), base = vp->prm.cloudalt - 1000.0;
	D3DXVECTOR4 slab(float(Rp + base), float(Rp + base + 3000.0), float(Rp), float(base));
	float side = float(std::min(1.0, std::max(0.0, (vp->CameraAltitude() - base) / 3000.0)));
	float steps = Config->VolCloudQuality <= 0 ? 32.0f : (Config->VolCloudQuality >= 2 ? 96.0f : 64.0f);
	D3DXVECTOR4 march(steps, float(frame % 64), wV, side);
	D3DXVECTOR4 fade(35e3f, 60e3f, 80e3f, 0.04f);
	D3DXVECTOR4 noise(1.0f / 12000.0f, 1.0f / 1500.0f, 0.35f, 1.0f);
	D3DXVECTOR4 tex(1.0f / float(W), 1.0f / float(H), 1.0f / float(tVol[0]->w), 1.0f / float(tVol[0]->h));

	pVol->Activate("VolMarch");
	pVol->SetStruct("Const", vp->GetScatterConst(), sizeof(ConstParams));
	pVol->SetFloat("vCamX", &camX, sizeof(camX));
	pVol->SetFloat("vCamY", &camY, sizeof(camY));
	pVol->SetFloat("vCamZ", &camZ, sizeof(camZ));
	pVol->SetFloat("vMapE", &mapE, sizeof(mapE));
	pVol->SetFloat("vMapN", &mapN, sizeof(mapN));
	pVol->SetFloat("vMapU", &mapU, sizeof(mapU));
	pVol->SetFloat("vClA", &clA, sizeof(clA));
	pVol->SetFloat("vClB", &clB, sizeof(clB));
	pVol->SetFloat("vClC", &clC, sizeof(clC));
	pVol->SetFloat("vSlab", &slab, sizeof(slab));
	pVol->SetFloat("vMarch", &march, sizeof(march));
	pVol->SetFloat("vFade", &fade, sizeof(fade));
	pVol->SetFloat("vNoise", &noise, sizeof(noise));
	pVol->SetFloat("vTex", &tex, sizeof(tex));
	pVol->SetTextureNative("tSun", vp->GetScatterTable(SUN_COLOR), IPF_LINEAR | IPF_CLAMP);
	pVol->SetTextureNative("tLndRay", vp->GetScatterTable(RAY_LAND), IPF_LINEAR | IPF_CLAMP);
	pVol->SetTextureNative("tLndMie", vp->GetScatterTable(MIE_LAND), IPF_LINEAR | IPF_CLAMP);
	pVol->SetTextureNative("tLndAtn", vp->GetScatterTable(ATN_LAND), IPF_LINEAR | IPF_CLAMP);
	pVol->SetTextureNative("tDepthV", tDepth, IPF_POINT | IPF_CLAMP);
	pVol->SetTextureNative("tCov", tCov, IPF_LINEAR | IPF_CLAMP);
	pVol->SetTextureNative("tShape", tShape, IPF_LINEAR | IPF_WRAP);
	pVol->SetTextureNative("tDetail", tDetail, IPF_LINEAR | IPF_WRAP);
	pVol->SetOutputNative(0, sVol[0]);
	if (!pVol->Execute(true)) return;

	pVol->Activate("VolBlur");
	pVol->SetFloat("vTex", &tex, sizeof(tex));
	pVol->SetTextureNative("tDepthV", tDepth, IPF_POINT | IPF_CLAMP);
	pVol->SetTextureNative("tVol", tVol[0], IPF_POINT | IPF_CLAMP);
	pVol->SetOutputNative(0, sVol[1]);
	pVol->Execute(true);

	pVol->Activate("VolComposite");
	pVol->SetFloat("vTex", &tex, sizeof(tex));
	pVol->SetTextureNative("tDepthV", tDepth, IPF_POINT | IPF_CLAMP);
	pVol->SetTextureNative("tVol", tVol[1], IPF_POINT | IPF_CLAMP);
	pVol->SetOutputNative(0, target);
	pVol->Execute(1, true);
}

float SceneFx::BeginFrame ()
{
	if (!pExpo) return 1.0f;
	OBJHANDLE f = oapiGetFocusObject();
	bool in = oapiCameraInternal();
	if (f != expoFocus || in != expoInternal) {
		expo.Reset();
		expoFocus = f;
		expoInternal = in;
	}
	float m[4];
	uint32_t stamp = 0;
	if (dev->ReadbackGet(m, sizeof(m), &stamp)) {
		vPlanet *vp = scn->GetCameraProxyVisual();
		float top = (vp && vp->CameraInAtmosphere()) ? Config->AutoExpMaxEV : 1.0f; // +1 EV at most out of the atmosphere
		expo.Update(AutoExpo::Sample{ m[0], m[1], m[2] }, stamp, float(oapiGetSysStep()), top);
	}
	expo.Used(scn->GetFrameId());
	return expo.Exposure();
}

// halving blits of the finished scene, 16x9 log luminance blocks, one texel, copied for the CPU
void SceneFx::Measure (VkSurf *target)
{
	if (!pExpo || !target) return;
	VkDev::State st = dev->GetState();
	dev->StretchRect(target, NULL, sHalf[0], NULL, VK_FILTER_LINEAR);
	dev->StretchRect(sHalf[0], NULL, sHalf[1], NULL, VK_FILTER_LINEAR);
	dev->StretchRect(sHalf[1], NULL, sHalf[2], NULL, VK_FILTER_LINEAR);
	D3DXVECTOR4 prm(16.0f, 9.0f, 0.002f, 254.0f / 255.0f);
	pExpo->Activate("PSBlocks");
	pExpo->SetFloat("vPrm", &prm, sizeof(prm));
	pExpo->SetTextureNative("tSrc", tHalf[2], IPF_POINT | IPF_CLAMP);
	pExpo->SetOutputNative(0, sBlocks);
	if (pExpo->Execute(true)) {
		pExpo->Activate("PSTotal");
		pExpo->SetTextureNative("tSrc", tBlocks, IPF_POINT | IPF_CLAMP);
		pExpo->SetOutputNative(0, sTotal);
		if (pExpo->Execute(true)) dev->ReadbackCopy(tTotal, scn->GetFrameId());
	}
	gc->PushRenderTarget(target, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // the main target, viewport and depth back
	gc->PopRenderTargets();
	dev->SetState(st);
}
