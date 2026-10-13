
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: ground shadows of the volumetric clouds (design-visual-batch 8, rev 2 M3 M4 S8)

#include "SceneFx.h"
#include "Scene.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "IProcess.h"
#include "VPlanet.h"
#include "VolCloudMap.h"
#include <algorithm>
#include <cmath>

struct SceneFx::VolShadowState {
	ImageProcessing *ip = nullptr;         // VolShadow.glsl: PSVolShadow, PSVolShdBlur, PSVolShdComposite
	VkTex *tex[2] = {};                    // half resolution R8: shadow, blurred
	VkSurf *srf[2] = {};
	bool ok = false;
};

void SceneFx::FreeVolShadow (VolShadowState *s)
{
	if (!s) return;
	SAFE_DELETE(s->ip);
	for (int i = 0; i < 2; i++) { SAFE_DELETE(s->srf[i]); SAFE_DELETE(s->tex[i]); }
	delete s;
}

// shadow at half resolution, blur, then black with alpha over the scene, before the volume clouds draw
// targets and shaders, at scene start when VolClouds and VolCloudShadows are on; a failure leaves the volume clouds on
void SceneFx::InitVolShadow ()
{
	if (vsh) return;
	vsh = new VolShadowState();
	const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	bool ok = depthOK;
	for (int i = 0; i < 2; i++) {
		vsh->tex[i] = new VkTex(dev, std::max(1u, W / 2), std::max(1u, H / 2), 1, VK_FORMAT_R8_UNORM, u);
		vsh->srf[i] = new VkSurf(vsh->tex[i], 0);
		ok = ok && vsh->tex[i]->img;
	}
	vsh->ip = new ImageProcessing(dev, "Modules/VulkanClient/VolShadow.glsl", "PSVolShadow", Config->bAtmoQuality ? NULL : "_PERFORMANCE"); // Scatter.glsl's table sizes
	vsh->ip->CompileShader("PSVolShdBlur");
	vsh->ip->CompileShader("PSVolShdComposite");
	vsh->ok = ok && vsh->ip->IsOK();
	if (!vsh->ok) {
		LogErr("SceneFx: volume cloud shadow targets or shaders failed, volume cloud shadows off");
		SAFE_DELETE(vsh->ip);
	}
}

void SceneFx::VolShadow (VkSurf *target, vPlanet *vp, float wV)
{
	volShdFade = 0.0f;
	if (!vsh) InitVolShadow();
	if (!vsh->ok || !DepthValid()) return; // tDepth of this frame only (design-visual-batch S4)

	VolPrm v; // the clouds drawn this frame
	VolParams(vp, v);
	D3DXVECTOR4 march(12.0f, float(frame % 64), wV, 0.0f);
	D3DXVECTOR4 tex(1.0f / float(W), 1.0f / float(H), 1.0f / float(vsh->tex[0]->w), 1.0f / float(vsh->tex[0]->h));
	ImageProcessing *ip = vsh->ip;
	bool drawn = false;

	ip->Activate("PSVolShadow");
	ip->SetStruct("Const", vp->GetScatterConst(), sizeof(ConstParams));
	SetVolParams(ip, v);
	ip->SetFloat("vMarch", &march, sizeof(march));
	ip->SetFloat("vTex", &tex, sizeof(tex));
	ip->SetTextureNative("tDepthV", tDepth, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tCov", tCov, IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tShape", tShape, IPF_LINEAR | IPF_WRAP);
	ip->SetOutputNative(0, vsh->srf[0]);
	if (ip->Execute(true)) {
		ip->Activate("PSVolShdBlur");
		ip->SetFloat("vTex", &tex, sizeof(tex));
		ip->SetTextureNative("tDepthV", tDepth, IPF_POINT | IPF_CLAMP);
		ip->SetTextureNative("tShd", vsh->tex[0], IPF_POINT | IPF_CLAMP);
		ip->SetOutputNative(0, vsh->srf[1]);
		if (ip->Execute(true)) {
			ip->Activate("PSVolShdComposite");
			ip->SetStruct("Const", vp->GetScatterConst(), sizeof(ConstParams));
			ip->SetFloat("vCamX", &v.camX, sizeof(v.camX));
			ip->SetFloat("vCamY", &v.camY, sizeof(v.camY));
			ip->SetFloat("vCamZ", &v.camZ, sizeof(v.camZ));
			ip->SetFloat("vFade", &v.fade, sizeof(v.fade));
			ip->SetFloat("vTex", &tex, sizeof(tex));
			ip->SetTextureNative("tDepthV", tDepth, IPF_POINT | IPF_CLAMP);
			ip->SetTextureNative("tShd", vsh->tex[1], IPF_POINT | IPF_CLAMP);
			ip->SetTextureNative("tLndAtn", vp->GetScatterTable(ATN_LAND), IPF_LINEAR | IPF_CLAMP);
			ip->SetOutputNative(0, target);
			drawn = ip->Execute(1, true);
		}
	}
	volShdFade = drawn ? wV : 0.0f; // the 2D cloud shadow near the camera fades by it from the next frame
}
