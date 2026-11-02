// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ray-traced GI's probe volume (design-ddgi 4, 5; Rev 2)

#include "DdgiMath.h" // first: the client util header defines PI as a macro
#include "DdgiVolume.h"
#include "DdgiScene.h"
#include "D3D9Config.h"
#include "Scene.h"
#include "VPlanet.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

using namespace DdgiMath;

static const VkImageUsageFlags ProbeUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
static const uint32_t E_RESET = 0x10000u, E_FAST = 0x20000u, E_RECHECK = 0x40000u, E_CLEAR = 0x80000u; // DDGI.glsl's DG_ flags
static const float HYSTERESIS = 0.97f;	// at rest (Majercik 2019)
static const float DIST_CAP = 4.0f;		// stored distances [cells]

// ---------------------------------------------------------------------------------------------
// DdgiPass

DdgiPass::DdgiPass (VkDev *_dev, const char *file, const char *vsEntry, const char *psEntry, const char *macros, bool _rt)
	: dev(_dev), rt(_rt), cb(_dev), name(psEntry)
{
	std::string src;
	if (!VkLoadShaderSource(file, src)) { LogErr("DdgiPass: %s not read", file); return; }
	VkMacros m;
	std::istringstream ts(macros ? macros : "");
	std::string tok;
	while (ts >> tok) m.push_back({ tok, "" });
	std::vector<uint32_t> sv, sf;
	if (!VkCompileGLSL(file, src, vsEntry, VK_SHADER_STAGE_VERTEX_BIT, m, sv, &tv) || !VkCompileGLSL(file, src, psEntry, VK_SHADER_STAGE_FRAGMENT_BIT, m, sf, &tf)) {
		LogErr("DdgiPass: %s (%s, %s) failed to compile", file, vsEntry, psEntry);
		return;
	}
	vs = VkCreateShaderObject(dev, sv, VK_SHADER_STAGE_VERTEX_BIT, rt);
	fs = VkCreateShaderObject(dev, sf, VK_SHADER_STAGE_FRAGMENT_BIT, rt);
	cb.SetTable(&tv);
	cb.SetTable(&tf);
	for (VkConstTable *t : { &tf, &tv })
		for (auto &e : t->entries) {
			if (!e.sampler) continue;
			bool have = false;
			for (auto &s : samp) if (s.binding == e.binding) have = true;
			if (!have) samp.push_back({ e.binding, e.view });
		}
}

DdgiPass::~DdgiPass ()
{
	if (dev->GetConstantSource() == &cb) dev->SetConstantSource(NULL, NULL);
	VkDevice d = dev->dev;
	for (VkShaderEXT s : { vs, fs }) {
		if (!s) continue;
		dev->ForgetShader(s);
		dev->Defer([d, s]() { vkx.DestroyShaderEXT(d, s, NULL); });
	}
}

VkConstHandle DdgiPass::Find (const char *n) const
{
	VkConstHandle h = tf.GetConstantByName(n);
	return h ? h : tv.GetConstantByName(n);
}

void DdgiPass::Set (const char *n, const void *data, UINT bytes)
{
	if (VkConstHandle h = Find(n)) if (!h->sampler) cb.SetValue(h, data, bytes); // a value this pair doesn't read is left out
}

void DdgiPass::Tex (const char *n, VkTex *t, bool linear, bool mips)
{
	VkConstHandle h = Find(n);
	if (!h || !h->sampler) return;
	VkFilter f = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	VkSamplerDesc sd = { f, f, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, 0.0f, !mips };
	cb.SetTexture(h->binding, t, sd);
}

void DdgiPass::Draw (UINT verts, VkDescriptorSet set)
{
	if (!OK() || !verts) return;
	dev->BindShaders(vs, fs);
	dev->SetVertexDecl(NULL);
	dev->SetConstantSource(&cb, &samp);
	if (rt) dev->BindRtSet(set);
	dev->DrawPrimitive(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, verts);
	if (rt) dev->BindRtSet(VK_NULL_HANDLE);
}

// ---------------------------------------------------------------------------------------------
// DdgiVolume

DdgiVolume::DdgiVolume (VkDev *_dev, const char *ppf) : dev(_dev)
{
	if (!dev->rayQuery) return;
	int iw, ih, dw, dh;
	AtlasSize(IRR, &iw, &ih);
	AtlasSize(DST, &dw, &dh);
	if (UINT(std::max(dw, dh)) > dev->props.limits.maxImageDimension2D) {
		LogErr("DdgiVolume: the distance atlas (%d x %d) is over the device's %u texels", dw, dh, dev->props.limits.maxImageDimension2D);
		return;
	}
	bool good = true;
	auto make = [&](VkTex *&t, VkSurf *&s, UINT w, UINT h, VkFormat f) {
		t = new VkTex(dev, w, h, 1, f, ProbeUsage);
		s = new VkSurf(t, 0);
		good = good && t->img;
	};
	make(tIrr, sIrr, iw, ih, VK_FORMAT_R16G16B16A16_SFLOAT);
	make(tDst, sDst, dw, dh, VK_FORMAT_R16G16_SFLOAT);
	make(tState, sState, ROW, NPROBE / ROW, VK_FORMAT_R16G16B16A16_SFLOAT);
	make(tStateP, sStateP, ROW, NPROBE / ROW, VK_FORMAT_R16G16B16A16_SFLOAT);
	make(tRay, sRay, K * RAYS, NMAX / K, VK_FORMAT_R16G16B16A16_SFLOAT);
	make(tAlpha, sAlpha, K, NMAX / K, VK_FORMAT_R16_SFLOAT);
	sched = new VkBuf(dev, VkDeviceSize(NMAX + NPROBE) * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
	good = good && sched->buf;

	VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 4 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 } };
	VkDescriptorPoolCreateInfo pi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	pi.maxSets = 4;
	pi.poolSizeCount = 2;
	pi.pPoolSizes = ps;
	VKCHECK(vkCreateDescriptorPool(dev->dev, &pi, NULL, &pool));
	if (pool) {
		VkDescriptorSetLayout l = dev->RtSetLayout();
		VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		ai.descriptorPool = pool;
		ai.descriptorSetCount = 1;
		ai.pSetLayouts = &l;
		VKCHECK(vkAllocateDescriptorSets(dev->dev, &ai, &set));
	}

	UINT nq = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(dev->phys, &nq, NULL);
	std::vector<VkQueueFamilyProperties> qf(nq);
	vkGetPhysicalDeviceQueueFamilyProperties(dev->phys, &nq, qf.data());
	if (dev->queueFamily < nq && qf[dev->queueFamily].timestampValidBits >= 32) {
		VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
		qi.queryCount = 6;
		VKCHECK(vkCreateQueryPool(dev->dev, &qi, NULL, &timer));
		tickNs = dev->props.limits.timestampPeriod;
	}
	else LogAlw("DdgiVolume: no timestamps on this queue, a fixed probe budget");

	std::string m = std::string(ppf ? ppf : "") + " _DDGI";
	const char *f = "Modules/VulkanClient/DDGI.glsl";
	pTrace = new DdgiPass(dev, f, "DgFull", "DgTrace", m.c_str(), true);
	pAlpha = new DdgiPass(dev, f, "DgFull", "DgAlpha", m.c_str(), true);
	pState = new DdgiPass(dev, f, "DgQuads", "DgState", m.c_str(), true);
	pIrrU = new DdgiPass(dev, f, "DgQuads", "DgIrr", m.c_str(), true);
	pDstU = new DdgiPass(dev, f, "DgQuads", "DgDst", m.c_str(), true);

	fresh.assign(NPROBE, 1);
	lastSun.assign(NPROBE * 3, 0.0f);
	rayMs = Config->GIRayMs;
	ok = good && set && pTrace->OK() && pAlpha->OK() && pState->OK() && pIrrU->OK() && pDstU->OK();
	if (!ok) LogErr("DdgiVolume: probe targets or shaders failed");
}

DdgiVolume::~DdgiVolume ()
{
	delete pTrace; delete pAlpha; delete pState; delete pIrrU; delete pDstU;
	for (VkSurf *s : { sIrr, sDst, sState, sStateP, sRay, sAlpha }) delete s;
	for (VkTex *t : { tIrr, tDst, tState, tStateP, tRay, tAlpha }) delete t;
	delete sched;
	VkDevice d = dev->dev;
	VkDescriptorPool p = pool;
	VkQueryPool q = timer;
	dev->Defer([d, p, q]() {
		if (p) vkDestroyDescriptorPool(d, p, NULL);
		if (q) vkDestroyQueryPool(d, q, NULL);
	});
}

// the GPU time of the use three back: the next budget
void DdgiVolume::Timing ()
{
	int r = int(uses % 3);
	if (!timer || !timed[r]) return;
	uint64_t t[4] = {};
	VkResult res = vkGetQueryPoolResults(dev->dev, timer, r * 2, 2, sizeof(t), t, 2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
	if (res != VK_SUCCESS || !t[1] || !t[3] || t[2] < t[0]) return;
	double ms = double(t[2] - t[0]) * tickNs * 1e-6;
	if (uses % 1000 == 3) LogAlw("DdgiVolume: %d probes in %.3f ms (target %.2f ms)", budget, ms, rayMs);
	budget = Budget(budget, ms, rayMs, NMIN, NMAX);
	timed[r] = false;
}

// new probes first, then each cascade's turn by its weight; probes that came in and wait are cleared so nothing reads their old cell
void DdgiVolume::Schedule (const DdgiScene &scene)
{
	const DdgiAnchor &a = scene.Anchor();
	V3 cam = { a.cam.x + a.k[0] * 1024.0, a.cam.y + a.k[1] * 1024.0, a.cam.z + a.k[2] * 1024.0 }; // from the anchor's first point
	double ground = a.ground + a.k[1] * 1024.0;
	for (int c = 0; c < NCASC; c++) {
		I3 o = Origin(c, cam, ground);
		if (have && (o.x != org[c].x || o.y != org[c].y || o.z != org[c].z))
			for (int s = 0; s < NPC; s++) {
				I3 n = CellOf(s, o), p = CellOf(s, org[c]);
				if (n.x != p.x || n.y != p.y || n.z != p.z) fresh[c * NPC + s] = 1;
			}
		org[c] = o;
		int sc = 1024 / int(Spacing(c)); // cells per anchor step
		I3 ak = { a.k[0] * sc, a.k[1] * sc, a.k[2] * sc };
		prm.org[c][0] = o.x - ak.x; prm.org[c][1] = o.y - ak.y; prm.org[c][2] = o.z - ak.z; prm.org[c][3] = 0;
		prm.off[c][0] = ((ak.x % GX) + GX) % GX; prm.off[c][1] = ((ak.y % GY) + GY) % GY; prm.off[c][2] = ((ak.z % GZ) + GZ) % GZ; prm.off[c][3] = 0;
	}
	have = true;

	list.clear();
	std::vector<uint32_t> clears;
	float cs = std::cos(2.0f * float(CPI) / 180.0f);
	auto take = [&](int p, uint32_t flags) {
		float *ls = &lastSun[p * 3];
		float d = ls[0] * float(sunNow.x) + ls[1] * float(sunNow.y) + ls[2] * float(sunNow.z);
		if (d < cs) flags |= E_FAST; // the sun moved since its last update (time warp)
		ls[0] = float(sunNow.x); ls[1] = float(sunNow.y); ls[2] = float(sunNow.z);
		list.push_back(uint32_t(p) | flags);
	};
	for (int p = 0; p < NPROBE; p++) {
		if (!fresh[p]) continue;
		if ((int)list.size() < budget) { take(p, E_RESET); fresh[p] = 0; }
		else if (fresh[p] == 1) { clears.push_back(uint32_t(p) | E_CLEAR); fresh[p] = 2; }
	}
	int left = budget - (int)list.size(), wsum = 0;
	for (int c = 0; c < NCASC; c++) wsum += Weight(c);
	for (int c = 0; c < NCASC && left > 0; c++) {
		int n = std::min(NPC, std::max(1, left * Weight(c) / wsum));
		for (int k = 0; k < n; k++) {
			int s = cursor[c];
			if (++cursor[c] >= NPC) { cursor[c] = 0; cycle[c]++; }
			int p = c * NPC + s;
			if (fresh[p]) continue;
			take(p, (cycle[c] % 8 == 0) ? E_RECHECK : 0u);
		}
	}
	nSched = (int)list.size();
	nClear = (int)clears.size();
	list.insert(list.end(), clears.begin(), clears.end());
}

// the local lights nearest the camera, anchor frame
void DdgiVolume::Lights (const DdgiScene &scene, Scene *scn)
{
	const DdgiAnchor &a = scene.Anchor();
	const D3D9Light *L = scn->GetLights();
	int n = L ? int(std::min<DWORD>(scn->GetLightCount(), MAX_SCENE_LIGHTS)) : 0;
	std::vector<std::pair<float, int>> by;
	D3DXVECTOR3 c(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < n; i++) {
		float il = L[i].GetIlluminance(c, 500.0f);
		if (il > 0.005f) by.push_back({ -il, i });
	}
	std::sort(by.begin(), by.end());
	int m = std::min<int>(8, (int)by.size());
	auto toA = [&](const D3DXVECTOR3 &v, bool pos, float *out) {
		VECTOR3 g = { v.x, v.y, v.z };
		out[0] = float(dotp(a.ax, g) + (pos ? a.cam.x : 0.0));
		out[1] = float(dotp(a.ay, g) + (pos ? a.cam.y : 0.0));
		out[2] = float(dotp(a.az, g) + (pos ? a.cam.z : 0.0));
	};
	for (int k = 0; k < m; k++) {
		const D3D9Light &l = L[by[k].second];
		toA(l.Position, true, prm.lpos[k]);
		toA(l.Direction, false, prm.ldir[k]);
		prm.ldir[k][3] = l.Type == 1 ? 1.0f : 0.0f;
		prm.ldif[k][0] = l.Diffuse.r; prm.ldif[k][1] = l.Diffuse.g; prm.ldif[k][2] = l.Diffuse.b;
		prm.latt[k][0] = l.Attenuation.x; prm.latt[k][1] = l.Attenuation.y; prm.latt[k][2] = l.Attenuation.z;
		memcpy(prm.lprm[k], &l.Param, sizeof(prm.lprm[k]));
	}
	prm.nl[0] = m;
}

void DdgiVolume::Target (VkSurf *s, UINT w, UINT h)
{
	dev->SetRenderTarget(s, NULL);
	for (int i = 1; i < 4; i++) dev->SetRenderTargetN(i, NULL);
	dev->SetViewport(0.0f, 0.0f, float(w), float(h));
}

void DdgiVolume::Bind (DdgiPass *p, DdgiScene &scene, ConstParams *cp, vPlanet *vp, const DdgiGiPS &gps)
{
	p->Set("Dg", &prm, sizeof(prm));
	p->Set("Const", cp, sizeof(ConstParams));
	p->Set("vFade", &gps.fade, sizeof(gps.fade));
	p->Set("vCurve", &gps.curve, sizeof(gps.curve));
	p->Set("vAmbC", &gps.ambc, sizeof(gps.ambc));
	p->Set("vMisc", &gps.misc, sizeof(gps.misc));
	if (vp && vp->GetScatterConst()) {
		p->Tex("tSun", vp->GetScatterTable(SUN_COLOR), true);
		p->Tex("tLndRay", vp->GetScatterTable(RAY_LAND), true);
		p->Tex("tLndMie", vp->GetScatterTable(MIE_LAND), true);
		p->Tex("tLndAtn", vp->GetScatterTable(ATN_LAND), true);
	}
	p->Tex("tIrrD", tIrr, false);
	p->Tex("tDstD", tDst, false);
	p->Tex("tStateD", tState, false);
	p->Tex("tAlbTab", scene.Table(), false);
	p->Tex("tAlbAtl", scene.Atlas(), true);
	p->Tex("tRayD", tRay, false);
	p->Tex("tAlphaD", tAlpha, false);
	p->Tex("tStateP", tStateP, false);
}

bool DdgiVolume::Update (DdgiScene &scene, Scene *scn, vPlanet *vp, ConstParams *cp, const DdgiGiPS &gps)
{
	if (!ok || !scene.Instances()) return false;
	const DdgiAnchor &a = scene.Anchor();
	if (a.reset || !have) { // a new frame: every probe starts over, nothing is read from the old ones
		dev->ColorFill(sIrr, NULL, 0);
		dev->ColorFill(sDst, NULL, 0);
		dev->ColorFill(sState, NULL, 0);
		std::fill(fresh.begin(), fresh.end(), 2);
		have = false;
	}
	sunNow = a.sun;
	Timing();
	Schedule(scene);
	Lights(scene, scn);

	const VECTOR3 *ax[3] = { &a.ax, &a.ay, &a.az };
	float (*dst[3])[4] = { &prm.ax, &prm.ay, &prm.az };
	for (int i = 0; i < 3; i++) { (*dst[i])[0] = float(ax[i]->x); (*dst[i])[1] = float(ax[i]->y); (*dst[i])[2] = float(ax[i]->z); (*dst[i])[3] = 0.0f; }
	prm.cam[0] = float(a.cam.x); prm.cam[1] = float(a.cam.y); prm.cam[2] = float(a.cam.z); prm.cam[3] = 0.0f;
	prm.ctr[0] = float(a.ctr.x); prm.ctr[1] = float(a.ctr.y); prm.ctr[2] = float(a.ctr.z); prm.ctr[3] = float(a.radius);
	prm.geo[0] = float(a.geo.x); prm.geo[1] = float(a.geo.y); prm.geo[2] = float(a.geo.z); prm.geo[3] = 0.0f;
	prm.sun[0] = float(a.sun.x); prm.sun[1] = float(a.sun.y); prm.sun[2] = float(a.sun.z); prm.sun[3] = 1.0f;
	double R[9];
	Rotation(++seed, R);
	for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) prm.rot[r][c] = float(R[r * 3 + c]); prm.rot[r][3] = 0.0f; }
	prm.prm[0] = HYSTERESIS; prm.prm[1] = float(DdgiScene::RANGE); prm.prm[2] = DIST_CAP; prm.prm[3] = float(nSched);
	prm.alb[0] = scene.AlbGamma(); prm.alb[1] = scene.AlbScale(); prm.alb[2] = 1.0f; prm.alb[3] = 0.0f;
	prm.nl[1] = K;
	prm.nl[3] = nSched + nClear;

	sched->Upload(list.data(), list.size() * sizeof(uint32_t));
	if (setTlas != scene.Tlas()) { // the structure and the buffers stay; written once
		VkWriteDescriptorSetAccelerationStructureKHR as = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		VkAccelerationStructureKHR t = scene.Tlas();
		as.accelerationStructureCount = 1;
		as.pAccelerationStructures = &t;
		VkDescriptorBufferInfo bi[2] = { { scene.Rows()->buf, 0, VK_WHOLE_SIZE }, { sched->buf, 0, VK_WHOLE_SIZE } };
		VkWriteDescriptorSet w[3] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET } };
		for (int i = 0; i < 3; i++) { w[i].dstSet = set; w[i].dstBinding = i; w[i].descriptorCount = 1; }
		w[0].pNext = &as;
		w[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		w[1].descriptorType = w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w[1].pBufferInfo = &bi[0];
		w[2].pBufferInfo = &bi[1];
		vkUpdateDescriptorSets(dev->dev, 3, w, 0, NULL);
		setTlas = t;
	}

	dev->EndRendering();
	VkCommandBuffer cmd = dev->Cmd();
	int slot = int(uses % 3);
	if (timer) {
		vkCmdResetQueryPool(cmd, timer, slot * 2, 2);
		vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timer, slot * 2);
	}

	VkDev::State st = dev->GetState();
	st.depthTest = st.depthWrite = st.blend = st.stencil = false;
	st.cull = VK_CULL_MODE_NONE;
	st.fill = VK_POLYGON_MODE_FILL;
	st.writeMask = 0xF;
	dev->SetState(st);
	UINT rows = UINT((nSched + K - 1) / K);

	if (nSched) {
		Bind(pTrace, scene, cp, vp, gps);
		Target(sRay, K * RAYS, rows);
		pTrace->Draw(3, set);

		Bind(pAlpha, scene, cp, vp, gps);
		Target(sAlpha, K, rows);
		pAlpha->Draw(3, set);
	}

	dev->CopySurface(sState, NULL, sStateP, NULL);
	prm.nl[2] = 1;
	Bind(pState, scene, cp, vp, gps);
	Target(sState, ROW, NPROBE / ROW);
	pState->Draw(UINT(nSched + nClear) * 6, set);

	if (nSched) {
		dev->SetBlend(true);
		dev->SetBlendFunc(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD);
		int w, h;
		prm.nl[2] = IRR;
		AtlasSize(IRR, &w, &h);
		Bind(pIrrU, scene, cp, vp, gps);
		Target(sIrr, w, h);
		pIrrU->Draw(UINT(nSched) * 6, set);

		prm.nl[2] = DST;
		AtlasSize(DST, &w, &h);
		Bind(pDstU, scene, cp, vp, gps);
		Target(sDst, w, h);
		pDstU->Draw(UINT(nSched) * 6, set);
		dev->SetBlend(false);
	}

	dev->EndRendering();
	if (timer) {
		vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timer, slot * 2 + 1);
		timed[slot] = true;
	}
	uses++;
	valid = true;
	return true;
}
