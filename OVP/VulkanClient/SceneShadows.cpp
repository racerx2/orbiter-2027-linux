// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: whole-scene sun shadows, camera-centred cascades in one atlas (design-scene-shadows, Rev 2)

#include "SceneShadows.h"
#include "Scene.h"
#include "VPlanet.h"
#include "VVessel.h"
#include "Surfmgr2.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "D3D9Effect.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace CsmMath;

SceneShadows *SceneShadows::inst = nullptr;
bool SceneShadows::probeOk = true;

static V3 Vd (const VECTOR3 &v) { return { v.x, v.y, v.z }; }
static M3 Md (const MATRIX3 &m) { M3 r = { { { m.m11, m.m12, m.m13 }, { m.m21, m.m22, m.m23 }, { m.m31, m.m32, m.m33 } } }; return r; }
static void ToDX (const double m[4][4], D3DXMATRIX &d)
{
	float *f = &d._11;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) f[r * 4 + c] = float(m[r][c]);
}

void SceneShadows::Probe (VkDev *dev)
{
	VkFormatProperties fp;
	vkGetPhysicalDeviceFormatProperties (dev->phys, VK_FORMAT_D32_SFLOAT, &fp);
	const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	probeOk = (fp.optimalTilingFeatures & need) == need;
	if (Config->SceneShadows && !probeOk) LogErr ("SceneShadows: no sampled D32 depth on this device, scene shadows off");
}

bool SceneShadows::Enabled () { return Config->SceneShadows && probeOk; }

SceneShadows::SceneShadows (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h) : gc(_gc), scn(_scn), W(w), H(h)
{
	inst = this;
	memset (&gpu, 0, sizeof gpu);
	memset (&off, 0, sizeof off);
	D3DXMatrixIdentity (&mCaster);
	if (!Enabled ()) return;
	dev = gc->GetDevice ();
	tAtlas = new VkTex (dev, ATLAS, ATLAS, 1, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	if (!tAtlas->img) { LogErr ("SceneShadows: the %d D32 atlas failed, scene shadows off", ATLAS); return; }
	sAtlas = new VkSurf (tAtlas, 0);
	pTerrain = new ShaderClass (dev, "Modules/VulkanClient/CsmCast.glsl", "CsmTerrainVS", "CsmTerrainPS", "CsmTerrain", "");
	pTrees = new ShaderClass (dev, "Modules/VulkanClient/CsmCast.glsl", "CsmTreeVS", "CsmTreePS", "CsmTrees", "");
	hCsm = D3D9Effect::FX->GetParameterByName (0, "gCsm");
	hCsmMap = D3D9Effect::FX->GetParameterByName (0, "gCsmMap");
	ok = hCsm && hCsmMap;
	if (!ok) { LogErr ("SceneShadows: D3D9Client.fx has no gCsm, scene shadows off"); return; }
	D3D9Effect::FX->SetTexture (hCsmMap, tAtlas);
	D3D9Effect::FX->SetValue (hCsm, &off, sizeof (CsmGpu));
	uint32_t nq = 0;
	vkGetPhysicalDeviceQueueFamilyProperties (dev->phys, &nq, NULL);
	std::vector<VkQueueFamilyProperties> qf (nq);
	vkGetPhysicalDeviceQueueFamilyProperties (dev->phys, &nq, qf.data());
	if (dev->queueFamily < nq && qf[dev->queueFamily].timestampValidBits > 0) {
		VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
		qi.queryCount = 12;
		if (vkCreateQueryPool (dev->dev, &qi, NULL, &qp) != VK_SUCCESS) qp = VK_NULL_HANDLE;
		tickNs = dev->props.limits.timestampPeriod;
	}
	LogAlw ("SceneShadows: atlas %d D32, %d cascades of %d texels", ATLAS, NC, N);
}

SceneShadows::~SceneShadows ()
{
	if (inst == this) inst = nullptr;
	if (qp) {
		VkDevice d = dev->dev;
		VkQueryPool q = qp;
		dev->Defer ([d, q]() { vkDestroyQueryPool (d, q, NULL); });
	}
	SAFE_DELETE (pTrees);
	SAFE_DELETE (pTerrain);
	SAFE_DELETE (sAtlas);
	SAFE_DELETE (tAtlas);
}

// the proxy has a tile manager drawn elevated, the camera within 42 km of the ground
bool SceneShadows::Gate (vPlanet *vp)
{
	if (!ok || !vp || !vp->SurfMgr2 ()) return false;
	if (vp->SurfMgr2 ()->ElevMode == eElevMode::Spherical) return false; // last frame's mode
	return scn->GetCameraElevation () < MaxAgl;
}

// this frame's light frame (frozen until the sun turns 1/2048 rad), the tiles to draw, every tile's transform with this frame's camera
bool SceneShadows::Frame (vPlanet *vp, bool force, int *draw, int &nd)
{
	nd = 0;
	OBJHANDLE hP = vp->Object (), hSun = oapiGetGbodyByIndex (0);
	if (!hSun) return false;
	MATRIX3 R;
	VECTOR3 P, S, C = scn->GetCameraGPos ();
	oapiGetRotationMatrix (hP, &R);
	oapiGetGlobalPos (hP, &P);
	oapiGetGlobalPos (hSun, &S);
	V3 ls = Vd (S - C);
	double sd = Len (ls);
	if (sd <= 0.0) return false;
	ls = Mul (ls, 1.0 / sd);
	sinSun = Dot (Unit (Vd (C - P)), ls);
	if (sinSun < MinSunSin) return false;
	M3 Rp = Md (R);
	V3 cp = MulMT (Rp, Vd (C - P)), Lp = MulMT (Rp, ls);
	bool rebase = force || !haveRef || hP != hProxy || Angle (Lp, Lref) > SunTurn;
	if (rebase) {
		Lref = Lp;
		Bref = Basis (Lref, alt);
		haveRef = true;
		hProxy = hP;
	}
	V3 Lg = MulM (Rp, Lref);
	toSun = D3DXVECTOR3 ((float)Lg.x, (float)Lg.y, (float)Lg.z);
	double rs = oapiGetSize (hSun);
	tan2w = (float)(2.0 * rs / std::sqrt (std::max (sd * sd - rs * rs, 1.0)));
	for (int i = 0; i < NC; i++) {
		Tile &T = tile[i];
		if (rebase || !T.valid || Due (i, frame) || MovedOut (i, T.q, MulM (T.B, cp))) {
			T.B = Bref;
			T.q = MulM (Bref, cp);
			T.box = Fit (i, T.q);
			T.valid = true;
			draw[nd++] = i;
		}
		xf[i] = Transform (T.B, Rp, cp, T.box);
	}
	return true;
}

// depth only into the atlas: the whole atlas is the viewport, each tile its scissor and its fold of the clip matrix
void SceneShadows::Draw (const int *draw, int nd)
{
	if (!nd) return;
	VkDev::State st = dev->GetState ();
	VkSurf *c0 = dev->GetRenderTarget (), *d0 = dev->GetDepthStencil ();
	RECT sc0;
	bool sc = dev->GetScissor (&sc0);
	scn->BeginPass (RENDERPASS_SCENESHADOW);
	dev->SetRenderTarget (NULL, sAtlas);
	VkDev::State fs = dev->GetState ();
	fs.fill = VK_POLYGON_MODE_FILL; // the debug wireframe must not reach the casters; st puts it back
	dev->SetState (fs);
	dev->SetDepthFunc (VK_COMPARE_OP_LESS_OR_EQUAL);
	dev->SetDepthBias (0.0f, 1.0f);
	for (int k = 0; k < nd; k++) {
		int i = draw[k];
		cur = i;
		double m[4][4];
		CasterMatrix (i, xf[i], m);
		ToDX (m, mCaster);
		RECT r = { LONG((i & 1) * N), LONG((i >> 1) * N), LONG((i & 1) * N + N), LONG((i >> 1) * N + N) };
		dev->SetScissor (&r);
		dev->Clear (false, true, false, 0, 1.0f, 0);
		dev->SetCullMode (VK_CULL_MODE_NONE);
		vProxy->RenderCasters (dev, *this);
		dev->SetCullMode (VK_CULL_MODE_NONE);
		for (Scene::VOBJREC *pv = scn->vobjFirst; pv; pv = pv->next) {
			if (pv->type != OBJTP_VESSEL || !pv->vobj->IsActive ()) continue;
			vVessel *v = (vVessel *)pv->vobj;
			if (Cull (v->GetBoundingSpherePosDX (), v->GetBoundingSphereRadius ())) v->Render (dev, false);
		}
	}
	cur = -1;
	dev->SetRenderTarget (c0, d0); // resets scissor and viewport, so before the scissor
	dev->SetScissor (sc ? &sc0 : NULL);
	scn->PopPass ();
	dev->SetState (st);
}

void SceneShadows::Fill ()
{
	for (int i = 0; i < NC; i++) {
		double m[4][4], ox, oy, r = Radius[i];
		ReceiverMatrix (i, xf[i], m);
		ToDX (m, gpu.mC[i]);
		gpu.vC[i] = D3DXVECTOR4 ((float)Texel (i), (float)Depth (i), (float)(SelectK * r), (float)(BandK * r));
		WorldTexel (i, tile[i].box, ox, oy);
		gpu.vO[i] = D3DXVECTOR4 ((float)ox, (float)oy, (float)TerrainBias[i], 0.0f);
	}
	gpu.vL = D3DXVECTOR4 (toSun.x, toSun.y, toSun.z, tan2w);
	gpu.vF = D3DXVECTOR4 ((float)FadeStart, (float)FadeEnd, scn->TaaActive () ? float(scn->GetFrameId () % 8) : -1.0f, 1.0f);
}

void SceneShadows::Push ()
{
	if (hCsm) D3D9Effect::FX->SetValue (hCsm, recv ? &gpu : &off, sizeof (CsmGpu));
}

// covered vessels lose their stencil shadow in the main pass; the ones this module turned off get it back when no longer covered
void SceneShadows::Stencils ()
{
	std::set<const vObject *> now;
	for (Scene::VOBJREC *pv = scn->vobjFirst; pv; pv = pv->next) {
		if (pv->type != OBJTP_VESSEL || !pv->vobj->IsActive ()) continue;
		vVessel *v = (vVessel *)pv->vobj;
		bool cov = false;
		if (active) {
			double alt = 0.0;
			if (!oapiGetAltitude (v->Object (), ALTMODE_GROUND, &alt)) alt = 1e9;
			cov = Covers (v->GetBoundingSpherePosDX (), v->GetBoundingSphereRadius (), alt);
		}
		if (cov) {
			v->bStencilShadow = false;
			now.insert (v);
		}
		else if (stencilOff.count (v)) v->bStencilShadow = true;
	}
	stencilOff.swap (now);
}

void SceneShadows::BeforeMain ()
{
	if (!ok) return;
	bool was = active;
	active = recv = false;
	vPlanet *vp = scn->GetCameraProxyVisual ();
	int draw[NC], nd = 0;
	if (Gate (vp) && Frame (vp, !was || vp != vProxy, draw, nd)) {
		vProxy = vp;
		active = recv = true;
		auto t0 = std::chrono::steady_clock::now ();
		TimeMark (0);
		Draw (draw, nd);
		TimeMark (1);
		sumCpu += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ();
		nTiles += nd;
		nCpu++;
		frame++;
		Fill ();
	}
	else {
		for (Tile &T : tile) T.valid = false;
		vProxy = nullptr;
	}
	if (was != active) LogAlw ("SceneShadows: %s", active ? "active" : "inactive (no elevated terrain under 42 km, or the sun below -3 deg)");
	Push ();
	Stencils ();
}

void SceneShadows::AfterResolve ()
{
	if (!recv) return;
	TimeMark (2);
	gpu.vF.z = -1.0f; // glass, exhausts and add-on meshes after the resolve: the noise fixed on the ground, not averaged
	Push ();
}

void SceneShadows::BeforeCockpit ()
{
	if (!recv) return;
	recv = false; // the cockpit interior, the overlay and the next frame's custom cameras and env maps without
	Push ();
}

bool SceneShadows::ReceiversOn (const vPlanet *vp) const
{
	return recv && vp && vp == vProxy && scn->GetRenderPass () == RENDERPASS_MAINSCENE;
}

bool SceneShadows::Cull (const D3DXVECTOR3 &c, float rad) const
{
	if (cur < 0) return false;
	return CastsIn (cur, rad) && CullSphere (cur, xf[cur], V3{ c.x, c.y, c.z }, rad);
}

// the stencil shadow lands at c - L alt / sin e: covered when the cascade that receives it draws the caster
bool SceneShadows::Covers (const D3DXVECTOR3 &c, float rad, double alt) const
{
	if (!active) return false;
	double k = std::max (alt, 0.0) / std::max (sinSun, 0.07);
	if (k > 0.8 * Zext) return false;
	V3 p = { c.x - toSun.x * k, c.y - toSun.y * k, c.z - toSun.z * k };
	return CsmMath::Covers (Len (p), rad);
}

// timestamps: 0 before the casters, 1 after them, 2 at the resolve; read four frames later
void SceneShadows::TimeMark (int k)
{
	if (!qp || reports >= 5 || !dev->InFrame ()) return;
	if (k == 2 && !marked) return;
	VkCommandBuffer cmd = dev->Cmd ();
	dev->EndRendering ();
	unsigned s = qFrame & 3;
	if (k == 0) {
		TimeRead ();
		vkCmdResetQueryPool (cmd, qp, s * 3, 3);
		vkCmdWriteTimestamp (cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, s * 3);
		marked = true;
	}
	else if (k == 1) vkCmdWriteTimestamp (cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, s * 3 + 1);
	else {
		vkCmdWriteTimestamp (cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, s * 3 + 2);
		pend[s] = true;
		marked = false;
		qFrame++;
	}
}

void SceneShadows::TimeRead ()
{
	unsigned s = qFrame & 3;
	if (!pend[s]) return;
	pend[s] = false;
	uint64_t t[3];
	if (vkGetQueryPoolResults (dev->dev, qp, s * 3, 3, sizeof t, t, sizeof (uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
	sumCast += double(t[1] - t[0]) * tickNs * 1e-6;
	sumMain += double(t[2] - t[1]) * tickNs * 1e-6;
	if (++nTimed < 240) return;
	LogAlw ("SceneShadows: casters %.2f ms GPU, %.2f ms CPU, %.2f tiles a frame; opaque scene to the resolve %.2f ms GPU (%u frames)",
		sumCast / nTimed, nCpu ? sumCpu / nCpu : 0.0, nCpu ? double(nTiles) / nCpu : 0.0, sumMain / nTimed, nTimed);
	sumCast = sumMain = sumCpu = 0.0;
	nTimed = nTiles = nCpu = 0;
	reports++;
}
