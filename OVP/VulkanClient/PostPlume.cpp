// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: volumetric plumes, smoke puffs and re-entry plasma (design-volumetric-plumes, Rev 2)
// Kim and Neumann 2001 (opacity maps), Umenhoffer et al. 2006 (spherical billboards), Simons 1972, Pack 1950, Lees 1955

#include "PostPlume.h"
#include "Scene.h"
#include "SceneFx.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "D3D9Surface.h"
#include "IProcess.h"
#include "VPlanet.h"
#include "VVessel.h"
#include "Particle.h"
#include "GfxPool.h"
#include "VolCloudMap.h"
#include "OapiExtension.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

using namespace PlumeMath;

static const VkImageUsageFlags RTUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
static const char *PlumeFile = "Modules/VulkanClient/Plume.glsl";
static const int MAXPL = 32, MAXPZ = 4, PLS = 12, PZS = 10, MAXPUFF = 8192, FLOWN = 128, PUFFROW = 64, MAXINSIDE = 16;
static const int puffRowsOf[3] = { 8, 32, 128 };

PostPlume *PostPlume::inst = nullptr;

struct PostPlume::NoiseBox { std::mutex mx; bool done = false, dropped = false; std::vector<uint8_t> rgba; };

struct PostPlume::ClassCfg {
	struct Rule { int kind, i0, i1, cls; double pe; }; // kind 0 index range, 1 all, 2 main, 3 retro, 4 hover, 5 attitude
	std::vector<Rule> rules;
	double smoke = 1.0;
	int ground = -1; // -1 by class, 0 none, 1 steam, 2 dust
	int puffs = -1;  // -1 streams with the default texture, 0 none, 1 every diffuse stream (design review S5)
};

struct PostPlume::Nozzle {
	THRUSTER_HANDLE th = nullptr;
	double F0 = -1.0, isp0 = -1.0, wsize = -1.0;
	int cls = TEXTURE;
	double re = 1.0, pe = 8e3;
};

struct PostPlume::Plume {
	const vVessel *vv = nullptr;
	OBJHANDLE hV = NULL;
	std::vector<DWORD> members;
	D3DXVECTOR3 o, a, pa, pb;
	double re = 1, L = 1, I = 0, lvl = 0, F0 = 0, isp0 = 0, env = 0, xg = 1e9, smoke = 0, rpx = 0, w = 0, Ad = 0, rate = 0, cr = 1, step = 1, R0 = 1, R1 = 1;
	int cls = 0, slot = -1, gtype = 0;
	Jet jet;
	D3DXVECTOR4 ground = D3DXVECTOR4(0, 0, 0, 0);
	D3DXVECTOR3 Tr = D3DXVECTOR3(1, 1, 1), In = D3DXVECTOR3(0, 0, 0), sun = D3DXVECTOR3(1, 1, 1), amb = D3DXVECTOR3(0, 0, 0), up = D3DXVECTOR3(0, 1, 0);
	float Tf = 1.0f, seed = 0.0f;
	VECTOR3 vel = { 0, 0, 0 };
};

struct PostPlume::Plasma {
	std::vector<vVessel *> members;
	D3DXVECTOR3 c, f, X, Y, col, pa, pb, e0, e1, e2;
	double Rb = 1, I = 0, px = 0, ext = 1, Df = 6, tanmu = 1, lamMin = 0.05, lamH = 1, Tr = 1, flick = 1, wob = 0, texel = 0.1, step = 1;
	int map = -1;
	double vp[2][16];
};

struct PostPlume::GroundPuff { VECTOR3 pos, vel; double size, t0, life; float col[3]; uint32_t seed; };

static uint32_t Hash (uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; } // integer finaliser: lowbias32 (Wellons, hash-prospector, public domain)
static double Rand01 (uint32_t x) { return double(Hash(x) >> 8) / 16777216.0; }

static double VNoise (double t, uint32_t seed) // smooth value noise in 0..1
{
	double i = std::floor(t), f = t - i, u = f * f * (3.0 - 2.0 * f);
	uint32_t k = uint32_t(int64_t(i)) * 0x9E3779B1u + Hash(seed);
	double a = Rand01(k), b = Rand01(k + 0x9E3779B1u);
	return a + (b - a) * u;
}

static D3DXVECTOR3 V3 (const VECTOR3 &v) { return D3DXVECTOR3(float(v.x), float(v.y), float(v.z)); }
static D3DXVECTOR3 V3 (const FVECTOR3 &v) { return D3DXVECTOR3(v.x, v.y, v.z); }
static D3DXVECTOR4 V4 (const D3DXVECTOR3 &v, double w) { return D3DXVECTOR4(v.x, v.y, v.z, float(w)); }
static double Len (const D3DXVECTOR3 &v) { return std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z); }
static double Dot3 (const D3DXVECTOR3 &a, const D3DXVECTOR3 &b) { return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z; }
static D3DXVECTOR3 Unit (const D3DXVECTOR3 &v) { double l = Len(v); return l > 1e-12 ? v * float(1.0 / l) : D3DXVECTOR3(0, 0, 1); }
static float LumF (const D3DXVECTOR3 &c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

PostPlume::PostPlume (oapi::D3D9Client *_gc, Scene *_scn, DWORD w, DWORD h)
	: gc(_gc), scn(_scn), dev(_gc->GetDevice()), W(w), H(h)
{
	nozzles = new std::map<std::pair<OBJHANDLE, DWORD>, Nozzle>;
	classes = new std::map<std::string, ClassCfg>;
	ground = new std::map<OBJHANDLE, std::vector<GroundPuff>>;
	hw = std::max(1u, W / 2);
	hh = std::max(1u, H / 2);
	vPl.assign(MAXPL * PLS, D3DXVECTOR4(0, 0, 0, 0));
	vPz.assign(MAXPZ * PZS, D3DXVECTOR4(0, 0, 0, 0));
	std::string f;
	if (scn->pOffscreenTarget) f += "_LIGHTGLOW "; // LightBlur: HDR, no curve, the plumes bloom
	if (Config->Filmic) f += "_FILMIC ";
	ipi = new ImageProcessing(dev, PlumeFile, "PSPlumeMarch", f.empty() ? NULL : f.c_str());
	for (const char *e : { "PSPlumeBlur", "PSPlumeComposite", "PSAtlasBake", "PSFlowPrep", "PSFlowReduce", "PSFlowBlur" }) ipi->CompileShader(e);
	pPuff = new ShaderClass(dev, PlumeFile, "PuffVS", "PuffPS", "PlumePuff", f.c_str());
	bool tg = true;
	auto make = [&](VkTex *&t, VkSurf *&s, UINT tw, UINT th, VkFormat fmt) { t = new VkTex(dev, tw, th, 1, fmt, RTUsage); s = new VkSurf(t, 0); tg = tg && t->img; };
	make(tVolP, sVolP, hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT);
	make(tVolPD, sVolPD, hw, hh, VK_FORMAT_R32G32_SFLOAT);
	make(tVolP2, sVolP2, hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT);
	for (int i = 0; i < 2; i++) make(tPuff[i], sPuff[i], hw, hh, VK_FORMAT_R16G16B16A16_SFLOAT);
	make(tAtlas, sAtlas, 512, 64, VK_FORMAT_R8G8B8A8_UNORM);
	for (int i = 0; i < 3; i++) {
		tPuffData[i] = new VkTex(dev, 6 * PUFFROW, puffRowsOf[i], 1, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		tg = tg && tPuffData[i]->img;
	}
	flowOK = true;
	auto makeF = [&](VkTex *&t, VkSurf *&s, UINT n, VkFormat fmt) { t = new VkTex(dev, n, n, 1, fmt, RTUsage); s = new VkSurf(t, 0); flowOK = flowOK && t->img; };
	for (int m = 0; m < 2; m++) {
		for (int j = 0; j < 2; j++) makeF(tFlowRaw[m][j], sFlowRaw[m][j], FLOWN, VK_FORMAT_R32_SFLOAT);
		makeF(tFlowP[m], sFlowP[m], FLOWN, VK_FORMAT_R32G32B32A32_SFLOAT);
		makeF(tFlowB[m], sFlowB[m], FLOWN / 2, VK_FORMAT_R16G16B16A16_SFLOAT);
		makeF(tFlowR8[m], sFlowR8[m], 8, VK_FORMAT_R32G32B32A32_SFLOAT);
		makeF(tFlowR[m], sFlowR[m], 1, VK_FORMAT_R32G32B32A32_SFLOAT);
	}
	makeF(tFlowBT, sFlowBT, FLOWN / 2, VK_FORMAT_R16G16B16A16_SFLOAT);
	sFlowDS = new VkSurf(dev, FLOWN, FLOWN, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	if (!sFlowDS->tex->img) { // no D32 attachments: the framework's format
		delete sFlowDS;
		sFlowDS = new VkSurf(dev, FLOWN, FLOWN, gc->GetFramework()->GetDepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	}
	flowOK = flowOK && sFlowDS->tex->img;
	bool sh = ipi->IsOK() && pPuff->GetVSHandle("mVP") && pPuff->GetPSHandle("pCamX");
	ok = tg && sh;
	if (!ok) LogErr("PostPlume: targets or shaders failed, volumetric plumes off (legacy exhausts and particles)");
	if (!flowOK) LogErr("PostPlume: flow map targets failed, the plasma uses the hull ellipsoid");
	hExhaustTex = gc->clbkLoadTexture("Exhaust.dds", 0);
	nbox = std::make_shared<NoiseBox>();

	// [V] per class: core rgb x gain, k_L; diamond rgb x gain, disk factor; soot, scatter, albedo, k_d; tail albedo, tail density
	double kb[3], sb[3];
	Blackbody(2100.0, kb); // [D] kerolox soot
	Blackbody(2600.0, sb); // [D] alumina
	auto P = [&](int c, D3DXVECTOR4 a, D3DXVECTOR4 b, D3DXVECTOR4 d, D3DXVECTOR4 t) { pal[c * 4] = a; pal[c * 4 + 1] = b; pal[c * 4 + 2] = d; pal[c * 4 + 3] = t; };
	P(HYDROLOX, D3DXVECTOR4(0.30f * 0.06f, 0.40f * 0.06f, 1.00f * 0.06f, 0.4f), D3DXVECTOR4(0.8f, 0.68f, 0.52f, 1.875f), D3DXVECTOR4(0, 0, 0, 1.0f), D3DXVECTOR4(0, 0, 0, 0));
	P(METHALOX, D3DXVECTOR4(0.35f * 0.15f, 0.45f * 0.15f, 1.00f * 0.15f, 0.5f), D3DXVECTOR4(0.7f, 0.385f, 0.525f, 2.0f), D3DXVECTOR4(0, 0, 0, 0.9f), D3DXVECTOR4(0, 0, 0, 0));
	P(KEROLOX, D3DXVECTOR4(float(kb[0] * 2.5), float(kb[1] * 2.5), float(kb[2] * 2.5), 0.8f), D3DXVECTOR4(1.0f, 0.8f, 0.5f, 1.0f), D3DXVECTOR4(1.2f, 0.3f, 0.35f, 0.6f), D3DXVECTOR4(0.55f, 0.52f, 0.50f, 2.0f));
	P(HYPERGOLIC, D3DXVECTOR4(0.30f, 0.135f, 0.075f, 0.6f), D3DXVECTOR4(0.3f, 0.18f, 0.12f, 1.0f), D3DXVECTOR4(0.15f, 0.15f, 0.5f, 0.4f), D3DXVECTOR4(0.6f, 0.4f, 0.25f, 0.6f));
	P(SOLID, D3DXVECTOR4(float(sb[0] * 4.0), float(sb[1] * 4.0), float(sb[2] * 4.0), 1.0f), D3DXVECTOR4(0.3f, 0.255f, 0.18f, 1.0f), D3DXVECTOR4(0.1f, 2.5f, 0.9f, 0.3f), D3DXVECTOR4(0.92f, 0.91f, 0.90f, 3.0f));
	P(TEXTURE, D3DXVECTOR4(1.0f, 0.75f, 0.5f, 1.0f), D3DXVECTOR4(0, 0, 0, 0), D3DXVECTOR4(0, 0, 0, 0), D3DXVECTOR4(0, 0, 0, 0));

	LogAlw("PostPlume: march %d, puffs %d, flow %d", ok ? 1 : 0, ok ? 1 : 0, ok && flowOK ? 1 : 0);
	inst = this;
}

PostPlume::~PostPlume ()
{
	if (inst == this) inst = nullptr;
	if (poolId) if (GfxPool *p = GfxPool::Peek()) p->Unregister(poolId);
	if (hExhaustTex) gc->clbkReleaseTexture(hExhaustTex);
	SAFE_DELETE(pPuff);
	SAFE_DELETE(ipi);
	VkSurf **ss[] = { &sVolP, &sVolPD, &sVolP2, &sPuff[0], &sPuff[1], &sAtlas, &sFlowBT, &sFlowDS };
	for (VkSurf **s : ss) SAFE_DELETE(*s);
	VkTex **tt[] = { &tVolP, &tVolPD, &tVolP2, &tPuff[0], &tPuff[1], &tAtlas, &tNoise, &tFlowBT, &tPuffData[0], &tPuffData[1], &tPuffData[2] };
	for (VkTex **t : tt) SAFE_DELETE(*t);
	for (int m = 0; m < 2; m++) {
		for (int j = 0; j < 2; j++) { SAFE_DELETE(sFlowRaw[m][j]); SAFE_DELETE(tFlowRaw[m][j]); }
		SAFE_DELETE(sFlowP[m]); SAFE_DELETE(tFlowP[m]);
		SAFE_DELETE(sFlowB[m]); SAFE_DELETE(tFlowB[m]);
		SAFE_DELETE(sFlowR8[m]); SAFE_DELETE(tFlowR8[m]);
		SAFE_DELETE(sFlowR[m]); SAFE_DELETE(tFlowR[m]);
	}
	delete nozzles;
	delete classes;
	delete ground;
}

// the turbulence noise once, on a pool thread; no claims until it is there
bool PostPlume::NoiseReady ()
{
	if (tNoise) return true;
	GfxPool &pool = GfxPool::Get();
	if (!poolId) poolId = pool.Register();
	std::shared_ptr<NoiseBox> box = nbox;
	if (!noiseAsked) {
		noiseAsked = true;
		GfxPool::Job j;
		j.owner = poolId;
		j.kind = GfxPool::NOISE;
		j.run = [box]() {
			std::vector<uint8_t> s;
			VolCloud::ShapeNoise(32, s);
			std::lock_guard<std::mutex> lk(box->mx);
			box->rgba.swap(s);
			box->done = true;
		};
		j.dropped = [box]() { std::lock_guard<std::mutex> lk(box->mx); box->dropped = true; };
		pool.Submit(std::move(j));
	}
	std::lock_guard<std::mutex> lk(box->mx);
	if (box->dropped) { box->dropped = false; noiseAsked = false; }
	if (box->done && !tNoise) {
		tNoise = new VkTex(dev, VK_FORMAT_R8G8B8A8_UNORM, 32, 32, 32, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		tNoise->Upload(0, 0, box->rgba.data(), box->rgba.size());
		std::vector<uint8_t>().swap(box->rgba);
		if (!tNoise->img) { LogErr("PostPlume: noise texture failed, volumetric plumes off"); SAFE_DELETE(tNoise); ok = false; }
	}
	return tNoise != nullptr;
}

// Config/GC/Plume/<class>.cfg: EXHAUST <i|i-j|*|main|retro|hover|att> <class> [pe=<kPa>], SMOKE <0..2>, GROUND <steam|dust|none>, PUFFS <0|1>; later lines win
const PostPlume::ClassCfg &PostPlume::CfgOf (VESSEL *v)
{
	char cn[256];
	snprintf(cn, sizeof(cn), "%s", v->GetClassName() ? v->GetClassName() : "");
	for (char *c = cn; *c; c++) if (*c == '/' || *c == '\\') *c = '_'; // as MaterialMgr's parse_vessel_classname
	auto it = classes->find(cn);
	if (it != classes->end()) return it->second;
	ClassCfg &c = (*classes)[cn];
	std::string dir = OapiExtension::GetConfigDir();
	for (char &ch : dir) if (ch == '\\') ch = '/';
	if (!dir.empty() && dir.back() != '/') dir += '/';
	std::string path = dir + "GC/Plume/" + cn + ".cfg";
	std::ifstream fs(path);
	if (!fs) return c;
	std::string line;
	while (std::getline(fs, line)) {
		size_t cm = line.find(';');
		if (cm != std::string::npos) line.resize(cm);
		std::istringstream is(line);
		std::string key;
		if (!(is >> key)) continue;
		for (char &ch : key) ch = char(toupper(ch));
		if (key == "EXHAUST") {
			std::string sel, cls, opt;
			if (!(is >> sel >> cls)) continue;
			ClassCfg::Rule r = { 1, 0, 0, ClassFromName(cls.c_str()), -1.0 };
			if (r.cls < 0) { LogErr("PostPlume: %s: unknown class %s", path.c_str(), cls.c_str()); continue; }
			while (is >> opt) if (opt.rfind("pe=", 0) == 0) r.pe = atof(opt.c_str() + 3) * 1e3;
			if (sel == "*") r.kind = 1;
			else if (sel == "main") r.kind = 2;
			else if (sel == "retro") r.kind = 3;
			else if (sel == "hover") r.kind = 4;
			else if (sel == "att") r.kind = 5;
			else {
				r.kind = 0;
				size_t d = sel.find('-');
				r.i0 = atoi(sel.c_str());
				r.i1 = d == std::string::npos ? r.i0 : atoi(sel.c_str() + d + 1);
			}
			c.rules.push_back(r);
		}
		else if (key == "SMOKE") { double s; if (is >> s) c.smoke = std::clamp(s, 0.0, 2.0); }
		else if (key == "GROUND") { std::string g; if (is >> g) c.ground = g == "none" ? 0 : (g == "dust" ? 2 : 1); }
		else if (key == "PUFFS") { int p; if (is >> p) c.puffs = p ? 1 : 0; }
	}
	LogAlw("PostPlume: %s, %d exhaust rules", path.c_str(), int(c.rules.size()));
	return c;
}

static bool InGroup (VESSEL *v, THGROUP_TYPE g, THRUSTER_HANDLE th)
{
	DWORD n = v->GetGroupThrusterCount(g);
	for (DWORD k = 0; k < n; k++) if (v->GetGroupThruster(g, k) == th) return true;
	return false;
}

// exit radius, exit pressure and class of an exhaust, kept while its thruster, F0, Isp0 and width hold (design review N8)
PostPlume::Nozzle &PostPlume::NozzleOf (VESSEL *v, DWORD idx, const EXHAUSTSPEC &es, const ClassCfg &cfg)
{
	Nozzle &n = (*nozzles)[std::make_pair(v->GetHandle(), idx)];
	THRUSTER_HANDLE th = es.th;
	double F0 = th ? v->GetThrusterMax0(th) : 0.0, isp0 = th ? v->GetThrusterIsp0(th) : 0.0;
	if (n.th == th && n.F0 == F0 && n.isp0 == isp0 && n.wsize == es.wsize) return n;
	n.th = th; n.F0 = F0; n.isp0 = isp0; n.wsize = es.wsize;
	double phi = PressureFraction(isp0, th ? v->GetThrusterIsp(th, PSL) : 0.0);
	double ae = F0 * phi / PSL; // [D] the pressure loss of Orbiter's thrust model is A_e p
	n.re = phi > 0.02 ? std::clamp(std::sqrt(ae / PI), 0.3 * es.wsize, 1.5 * es.wsize) : 0.6 * es.wsize;
	n.pe = ExitPressure(phi, F0);
	n.cls = Classify(th != nullptr, F0, isp0);
	for (const ClassCfg::Rule &r : cfg.rules) {
		bool m = false;
		switch (r.kind) {
		case 0: m = int(idx) >= r.i0 && int(idx) <= r.i1; break;
		case 1: m = true; break;
		case 2: m = th && InGroup(v, THGROUP_MAIN, th); break;
		case 3: m = th && InGroup(v, THGROUP_RETRO, th); break;
		case 4: m = th && InGroup(v, THGROUP_HOVER, th); break;
		case 5: for (int g = THGROUP_ATT_PITCHUP; g <= THGROUP_ATT_BACK && th && !m; g++) m = InGroup(v, THGROUP_TYPE(g), th); break;
		}
		if (!m) continue;
		n.cls = r.cls;
		if (r.pe > 0.0) n.pe = r.pe;
	}
	if (n.cls == TEXTURE) n.re = 0.8 * es.wsize; // [V] the legacy flame's half-width
	n.re = std::max(n.re, 0.02);
	return n;
}

// atlas slot of an exhaust or particle texture: LRU of 16 keyed by the handle, the VkTex and its image (design review N9)
int PostPlume::SlotOf (SURFHANDLE h)
{
	if (!h) return -1;
	VkTex *t = SURFACE(h)->GetTexture();
	if (!t || !t->img) return -1;
	int lru = 0;
	for (int i = 0; i < 16; i++) {
		Slot &s = atlasSlot[i];
		if (s.h == h && s.tex == t && s.img == t->img) { s.used = frame; return s.baked ? i : -1; }
		if (atlasSlot[i].used < atlasSlot[lru].used) lru = i;
	}
	if (atlasSlot[lru].used == frame && atlasSlot[lru].h) return -1; // all 16 in use this frame: the palette colour
	Slot &s = atlasSlot[lru];
	s.h = h; s.tex = t; s.img = t->img; s.used = frame; s.baked = false;
	bakeQueue.push_back(lru);
	return -1;
}

void PostPlume::Collect ()
{
	auto c0 = std::chrono::steady_clock::now();
	frame = scn->GetFrameId();
	claimFrame = 0;
	plumes.clear();
	plasma.clear();
	claimed.clear();
	legacy.clear();
	plumeOf.clear();
	reentryW.clear();
	reentryFrame = frame;
	reentryOff = false;
	nPuff = 0;
	nLit = 0;
	for (int i : bakeQueue) atlasSlot[i].h = nullptr; // queued but never baked (no Render): the handle may be gone
	bakeQueue.clear();
	if (frame % 300 == 0) { // deleted vessels: their handle may be reused
		for (auto it = nozzles->begin(); it != nozzles->end();) it = oapiIsVessel(it->first.first) ? std::next(it) : nozzles->erase(it);
		for (auto it = groundAcc.begin(); it != groundAcc.end();) it = oapiIsVessel(it->first) ? std::next(it) : groundAcc.erase(it);
	}
	if (!ok || !scn->pFx || !scn->pFx->DepthOK()) return; // SceneFx draws tDepth every main frame for us
	if (!NoiseReady()) return;
	reentryOff = true; // the plasma replaces the legacy billboard for every vessel, also below its own onset
	time = float(std::fmod(oapiGetSysTime(), 3600.0)); // the wall clock, as the haze
	double vp[16];
	const float *m = (const float *)&scn->Camera.mProjView;
	for (int i = 0; i < 16; i++) vp[i] = m[i];
	FrustumPlanes(vp, planes);
	for (int k = 0; k < 4; k++) { planes[4][k] = vp[k * 4 + 3]; planes[5][k] = k == 3 ? 1e30 : 0.0; } // in front of the eye (w > 0); the pass's near and far limits are not the scene's
	pxs = scn->Camera.mProj._22 * 0.5 * H;
	GatherPlumes();
	UpdateGround();
	GatherPlasma();
	GatherPuffs();
	if ((nLit > 0 || !plasma.empty() || nPuff > 0) && diagN < 3) { diagN++; LogAlw("PostPlume: frame %u: %d lit exhausts, %d plumes, %d plasma, %d puffs", frame, nLit, int(plumes.size()), int(plasma.size()), nPuff); }
	if (plumes.empty() && plasma.empty() && nPuff == 0) return;
	for (size_t i = 0; i < plumes.size(); i++) FillPlume(int(i), plumes[i]);
	for (size_t k = 0; k < plasma.size(); k++) FillPlasma(int(k), plasma[k]);
	ScreenRect();
	claimFrame = frame + 1;
	for (D3D9ParticleStream *s : claimed) s->Claim(frame);
	collectMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
	if (++collectN == 600) LogAlw("PostPlume: Collect %.3f ms mean over 600 frames (now %d plumes, %d plasma, %d puffs)", collectMs / collectN, int(plumes.size()), int(plasma.size()), nPuff);
}

// every lit exhaust in view: LOD crossfade, clustering (design review M4), the 32 nearest by size and level
void PostPlume::GatherPlumes ()
{
	struct Cand { vVessel *vv; VESSEL *v; DWORD idx; EXHAUSTSPEC es; double lvl; Nozzle *nz; D3DXVECTOR3 ref, axis; double rpx, w; const ClassCfg *cfg; };
	std::vector<Cand> cs;
	std::vector<OBJHANDLE> smokers; // vessels with their own diffuse stream: the tail is thinner
	for (DWORD n = 0; n < scn->nstream; n++) {
		D3D9ParticleStream *s = scn->pstream[n];
		if (s && s->Diffuse() && !s->IsReentry() && s->Owner()) smokers.push_back(s->Owner());
	}
	int vid = 0;
	std::vector<OBJHANDLE> vids;
	for (Scene::VOBJREC *pv = scn->vobjFirst; pv; pv = pv->next) {
		vObject *vo = pv->vobj;
		if (!vo || !vo->IsActive() || vo->GetMeshCount() < 1) continue; // as the legacy exhaust loop
		if (oapiGetObjectType(vo->Object()) != OBJTP_VESSEL) continue;
		vVessel *vv = (vVessel *)vo;
		VESSEL *v = vv->GetInterface();
		if (!v) continue;
		DWORD n = v->GetExhaustCount();
		if (!n) continue;
		const ClassCfg &cfg = CfgOf(v);
		const D3DXMATRIX *M = vv->MWorld();
		for (DWORD i = 0; i < n; i++) {
			double lvl = v->GetExhaustLevel(i);
			if (lvl <= 0.0) continue;
			EXHAUSTSPEC es;
			if (!v->GetExhaustSpec(i, &es) || !es.lpos || !es.ldir || es.lsize <= 0.0) continue;
			nLit++;
			Nozzle &nz = NozzleOf(v, i, es, cfg);
			VECTOR3 r = *es.lpos - *es.ldir * es.lofs;
			D3DXVECTOR3 lr = V3(r), ld = V3(-*es.ldir), wr, wd;
			D3DXVec3TransformCoord(&wr, &lr, M); // camera-relative
			D3DXVec3TransformNormal(&wd, &ld, M);
			wd = Unit(wd);
			double dist = std::max(0.1, Len(wr));
			double rpx = nz.re * pxs / dist;
			double w = Smoothstep(1.0, 3.0, rpx); // [V] below 1 px legacy, 1-3 px crossfade, above 3 px volume
			if (w <= 0.0) continue;
			double L = es.lsize * 4.0, rb = std::max(nz.re * 3.0, 0.6 * L); // generous: the vacuum cone, the tail
			double a[3] = { wr.x, wr.y, wr.z }, b[3] = { wr.x + wd.x * L, wr.y + wd.y * L, wr.z + wd.z * L };
			if (!CapsuleVisible(planes, a, b, rb)) continue;
			cs.push_back({ vv, v, i, es, lvl, &nz, wr, wd, rpx, w, &cfg });
			if (diagN < 3 && cs.size() <= 8) LogAlw("PostPlume: exhaust %u of %s: level %.2f, %s, r_e %.2f m, %.1f px, %.1f m away", i, v->GetName(), lvl, ClassName(nz.cls), nz.re, rpx, dist);
		}
	}
	if (cs.empty()) return;

	std::vector<PlumeMath::Nozzle> nzs(cs.size());
	for (size_t i = 0; i < cs.size(); i++) {
		const Cand &c = cs[i];
		OBJHANDLE h = c.v->GetHandle();
		auto it = std::find(vids.begin(), vids.end(), h);
		int id = int(it - vids.begin());
		if (it == vids.end()) vids.push_back(h);
		nzs[i] = { { c.ref.x, c.ref.y, c.ref.z }, { c.axis.x, c.axis.y, c.axis.z }, c.nz->re, c.lvl, c.rpx, c.nz->cls, id };
	}
	(void)vid;
	std::vector<std::vector<int>> groups;
	Cluster(nzs, MAXPL, groups);
	std::sort(groups.begin(), groups.end(), [&](const std::vector<int> &p, const std::vector<int> &q) {
		double a = 0.0, b = 0.0;
		for (int i : p) a = std::max(a, cs[i].rpx * cs[i].lvl);
		for (int i : q) b = std::max(b, cs[i].rpx * cs[i].lvl);
		return a > b;
	});
	if (groups.size() > size_t(MAXPL)) groups.resize(MAXPL); // the rest stays legacy

	VECTOR3 cam = scn->Camera.pos;
	for (const std::vector<int> &g : groups) {
		const Cand *rep = &cs[g[0]];
		for (int i : g) if (cs[i].nz->re * cs[i].lvl > rep->nz->re * rep->lvl) rep = &cs[i];
		Plume P;
		P.vv = rep->vv;
		P.hV = rep->v->GetHandle();
		P.cls = rep->nz->cls;
		P.re = rep->nz->re;
		P.F0 = rep->nz->F0;
		P.isp0 = rep->nz->isp0;
		double ws = 0.0;
		D3DXVECTOR3 o(0, 0, 0), a(0, 0, 0);
		for (int i : g) {
			const Cand &c = cs[i];
			double wl = std::max(c.lvl, 1e-3);
			o += c.ref * float(wl); a += c.axis * float(wl); ws += wl;
			P.lvl = std::max(P.lvl, c.lvl);
			P.rpx = std::max(P.rpx, c.rpx);
			P.w = std::max(P.w, c.w);
			P.members.push_back(c.idx);
		}
		P.o = o * float(1.0 / ws);
		P.a = Unit(a);
		for (int i : g) P.env = std::max(P.env, Len(cs[i].ref - P.o) + cs[i].nz->re - P.re); // the cluster's envelope (design review M4)
		VESSEL *v = rep->v;
		double pa = v->GetAtmPressure();
		P.jet = JetState(rep->nz->pe, pa);
		double watm = P.jet.watm;
		P.L = rep->es.lsize * (0.4 + 0.6 * P.lvl) * (1.0 + 1.5 * (1.0 - watm)); // [V]
		uint32_t seed = Hash(uint32_t(uintptr_t(P.hV) >> 4) * 31u + rep->idx);
		P.seed = float(Rand01(seed) * 17.0);
		double mod = std::clamp(rep->es.modulate, 0.0, 1.0);
		P.I = P.lvl * (1.0 - mod * VNoise(time * 12.0, seed)) * P.w; // smooth flicker, the add-on's modulate (design 5.5)
		P.Ad = P.jet.jet ? Smoothstep(0.1, 0.7, std::fabs(std::log(std::max(P.jet.n, 1e-6)))) * pal[P.cls * 4 + 2].w : 0.0;
		bool own = std::find(smokers.begin(), smokers.end(), P.hV) != smokers.end();
		P.smoke = pal[P.cls * 4 + 3].w > 0.0f ? rep->cfg->smoke * (own ? 0.3 : 1.0) : 0.0;
		if (P.cls == TEXTURE) { P.slot = SlotOf(rep->es.tex ? rep->es.tex : hExhaustTex); P.jet.jet = false; P.jet.Ls = 0.0; P.jet.rj = 1.0; P.jet.xmd = -1.0; }

		// ground under the nozzle (design 5.4)
		OBJHANDLE hP = v->GetSurfaceRef();
		P.gtype = rep->cfg->ground >= 0 ? rep->cfg->ground : (P.cls == TEXTURE ? 2 : 1);
		double hv = v->GetAltitude(ALTMODE_GROUND);
		if (hP && hv < 5e3) {
			VECTOR3 gv, gp;
			v->GetGlobalPos(gv);
			oapiGetGlobalPos(hP, &gp);
			D3DXVECTOR3 up = V3(unit(gv - gp));
			D3DXVECTOR3 g0 = V3(gv - cam) - up * float(hv);
			P.up = up;
			P.ground = D3DXVECTOR4(up.x, up.y, up.z, float(-Dot3(up, g0)));
			double hn = Dot3(up, P.o) + P.ground.w, c = -Dot3(P.a, up);
			if (c > 0.2 && hn > 0.0 && hn / c < 2.5 * P.L) { // [V] 2.5 L: the SRBs reach the pad's trench (design 5.4 had 1.5 L)
				P.xg = hn / c;
				if (pa > 10e3 && hv < 300.0 && P.gtype > 0) P.rate = 6.0 * std::sqrt(std::max(P.F0, 1.0) * P.lvl * double(g.size()) / 1e6); // [V] ground cloud puffs per s
			}
			v->GetGlobalVel(P.vel);
		}

		// light and haze at the vessel, fog to the plume
		const D3D9Sun *sl = rep->vv->GetSunLight();
		float e = scn->fExposure;
		P.Tr = V3(sl->Transmission);
		P.In = V3(sl->Incatter);
		P.sun = V3(sl->Color) * e;
		P.amb = V3(sl->Ambient) * e;
		D3DXVECTOR3 mid = P.o + P.a * float(0.5 * P.L);
		double md = Len(mid);
		P.Tf = md > 1.0 ? scn->pFx->FogAlongRay(cam, _V(mid.x / md, mid.y / md, mid.z / md), md) : 1.0f;

		// bound and step (radii in metres)
		double rmax = P.re + P.env;
		for (int k = 0; k <= 16; k++) rmax = std::max(rmax, P.re * (P.cls == TEXTURE ? 1.15 : PlumeRadius(P.jet, P.L / P.re * k / 16.0)) + P.env);
		P.R0 = P.re + P.env;
		P.R1 = P.cls == TEXTURE ? P.re * 1.15 + P.env : P.re * PlumeRadius(P.jet, P.L / P.re) + P.env;
		double Lb = P.L * 1.2, r = rmax * 1.1;
		if (P.smoke > 0.0 && watm > 0.01) { Lb = 4.0 * P.L; r = std::max(r, (P.re * 1.2 + P.env + 0.094 * 3.0 * P.L) * 2.0); }
		double back = 0.3 * P.re;
		if (watm < 0.99) { Lb = std::max(Lb, 0.6 * P.L); r = std::max(r, 0.8 * P.L); back = 0.3 * P.L; }
		if (P.xg < 1e8) { Lb = std::min(Lb, P.xg + 0.5 * P.re); r = std::max(r, 3.0 * (0.8 * std::max(P.L - P.xg, 0.0) + 2.0 * P.jet.rj * P.re)); }
		P.pa = P.o - P.a * float(back);
		P.pb = P.o + P.a * float(Lb);
		P.cr = r;
		P.step = watm < 0.5 ? std::max(0.1, P.L / 10.0) : std::max(0.6 * P.re, P.L / 40.0);

		int pi = int(plumes.size());
		for (DWORD idx : P.members) {
			std::vector<float> &lw = legacy[P.vv];
			std::vector<int> &po = plumeOf[P.vv];
			DWORD n = v->GetExhaustCount();
			if (lw.size() < n) { lw.resize(n, 1.0f); po.resize(n, -1); }
			lw[idx] = float(1.0 - P.w); // the legacy flame crossfades out (design 5.6)
			po[idx] = pi;
		}
		plumes.push_back(P);
	}
}

// ground clouds where a plume hits the ground in air (design 6.6), sim time
void PostPlume::UpdateGround ()
{
	double simt = oapiGetSimTime(), dt = simt - lastSimt;
	lastSimt = simt;
	OBJHANDLE focus = oapiGetFocusObject();
	if (dt < 0.0 || dt > 1.0 || focus != lastFocus) { ground->clear(); groundAcc.clear(); lastFocus = focus; return; } // a time jump or a new focus
	if (dt <= 0.0) return; // paused
	VECTOR3 cam = scn->Camera.pos;
	for (const Plume &P : plumes) {
		if (P.rate <= 0.0) continue;
		std::vector<GroundPuff> &g = (*ground)[P.hV];
		double &acc = groundAcc[P.hV];
		acc += P.rate * dt;
		D3DXVECTOR3 gpc = P.o + P.a * float(P.xg);
		D3DXVECTOR3 rf = std::fabs(P.up.y) < 0.9f ? D3DXVECTOR3(0, 1, 0) : D3DXVECTOR3(1, 0, 0), e1, e2;
		D3DXVec3Cross(&e1, &P.up, &rf);
		e1 = Unit(e1);
		D3DXVec3Cross(&e2, &P.up, &e1);
		double rjm = std::max(P.jet.rj * P.re, 0.3);
		double spd = std::min(0.15 * P.isp0, 60.0); // [V]
		static const float cols[3][3] = { { 0.62f, 0.55f, 0.45f }, { 0.95f, 0.95f, 0.95f }, { 0.92f, 0.90f, 0.88f } };
		const float *col = P.gtype == 2 ? cols[0] : (P.cls == SOLID ? cols[2] : cols[1]);
		float kcol[3] = { 0.85f, 0.82f, 0.78f };
		if (P.cls == KEROLOX && P.gtype == 1) col = kcol;
		while (acc >= 1.0) {
			acc -= 1.0;
			if (g.size() >= 384) continue;
			uint32_t s = Hash(uint32_t(simt * 1000.0) + uint32_t(g.size()) * 7919u + uint32_t(uintptr_t(P.hV) >> 4));
			double phi = 2.0 * PI * Rand01(s), rr = 2.0 * rjm * Rand01(s + 1);
			D3DXVECTOR3 dir = e1 * float(std::cos(phi)) + e2 * float(std::sin(phi));
			GroundPuff q;
			q.pos = cam + _V(gpc.x + dir.x * rr, gpc.y + dir.y * rr, gpc.z + dir.z * rr) + _V(P.up.x, P.up.y, P.up.z) * (0.5 * rjm);
			q.vel = P.vel + _V(dir.x, dir.y, dir.z) * (spd * (0.5 + 0.5 * Rand01(s + 2)));
			q.size = std::max(0.5 * rjm, 1.0);
			q.t0 = simt;
			q.life = 30.0 * (0.8 + 0.4 * Rand01(s + 3));
			for (int c = 0; c < 3; c++) q.col[c] = col[c];
			q.seed = s;
			g.push_back(q);
		}
		if (acc > 1.0) acc = 1.0;
	}
	for (auto it = ground->begin(); it != ground->end();) {
		std::vector<GroundPuff> &g = it->second;
		OBJHANDLE hV = it->first;
		VESSEL *v = oapiIsVessel(hV) ? oapiGetVesselInterface(hV) : nullptr;
		OBJHANDLE hP = v ? v->GetSurfaceRef() : NULL;
		if (!v || !hP) { it = ground->erase(it); continue; }
		VECTOR3 pp, vg;
		oapiGetGlobalPos(hP, &pp);
		v->GetGlobalPos(vg);
		VECTOR3 up = unit(vg - pp);
		double lng, lat, rad;
		oapiGlobalToEqu(hP, vg, &lng, &lat, &rad);
		VECTOR3 wind = oapiGetWindVector(hP, lng, lat, rad - oapiGetSize(hP), 3); // global, with the planet's motion, as Particle.cpp
		double kh = std::exp(-dt / 4.0), kv = std::exp(-dt / 2.0); // [V] drag toward the wind, buoyancy 3 m/s
		for (size_t i = 0; i < g.size();) {
			GroundPuff &q = g[i];
			if (simt - q.t0 > q.life) { q = g.back(); g.pop_back(); continue; }
			VECTOR3 rel = q.vel - wind;
			double ru = dotp(rel, up);
			VECTOR3 rh = (rel - up * ru) * kh;
			ru = 3.0 + (ru - 3.0) * kv;
			q.vel = wind + rh + up * ru;
			q.pos += q.vel * dt;
			q.size += 3.0 * dt; // [V] growth
			i++;
		}
		if (g.empty()) it = ground->erase(it);
		else ++it;
	}
}

// vessels with a high energy flux (design 7.1; any vessel, as the legacy billboard it replaces, re-entry stream or not); the 2 largest on screen get flow maps
void PostPlume::GatherPlasma ()
{
	std::vector<OBJHANDLE> owners, taken;
	for (DWORD n = 0; n < oapiGetVesselCount(); n++) if (OBJHANDLE h = oapiGetVesselByIndex(n)) owners.push_back(h);
	std::vector<Plasma> cand;
	for (OBJHANDLE h : owners) {
		if (std::find(taken.begin(), taken.end(), h) != taken.end() || !oapiIsVessel(h)) continue;
		VESSEL *v = oapiGetVesselInterface(h);
		vObject *vo = scn->GetVisObject(h);
		if (!v || !vo || !vo->IsActive() || vo->GetMeshCount() < 1 || oapiGetObjectType(h) != OBJTP_VESSEL) continue;
		if (v->GetAtmDensity() <= 0.0) continue;
		double V = v->GetAirspeed(), I = PlasmaIntensity(v->GetDynPressure(), V);
		if (I < 0.005) continue;
		std::vector<OBJHANDLE> rigid = { h }; // docked and attached: one primitive, as SceneFx::FlagPass
		for (size_t k = 0; k < rigid.size() && rigid.size() < 64; k++) {
			VESSEL *w = oapiGetVesselInterface(rigid[k]);
			if (!w) continue;
			auto add = [&](OBJHANDLE x) { if (x && std::find(rigid.begin(), rigid.end(), x) == rigid.end()) rigid.push_back(x); };
			for (UINT i = 0; i < w->DockCount(); i++) add(w->GetDockStatus(w->GetDockHandle(i)));
			for (int p = 0; p < 2; p++) for (DWORD i = 0; i < w->AttachmentCount(p == 1); i++) add(w->GetAttachmentStatus(w->GetAttachmentHandle(p == 1, i)));
		}
		Plasma z;
		for (OBJHANDLE x : rigid) {
			taken.push_back(x);
			vObject *xo = scn->GetVisObject(x);
			if (xo && xo->IsActive() && oapiGetObjectType(x) == OBJTP_VESSEL && xo->GetMeshCount() > 0) z.members.push_back((vVessel *)xo);
		}
		bool first = true;
		for (vVessel *mv : z.members) { // the hull spheres' union, exhausts left out
			const D9BBox &hb = mv->HullBox;
			if (hb.bs.w <= 0.0f) continue;
			D3DXVECTOR3 c, lc(hb.bs.x, hb.bs.y, hb.bs.z);
			D3DXVec3TransformCoord(&c, &lc, mv->MWorld());
			double r = hb.bs.w;
			if (first) { z.c = c; z.Rb = r; first = false; continue; }
			double d = Len(c - z.c);
			if (d + r <= z.Rb) continue;
			double R2 = 0.5 * (z.Rb + d + r);
			if (d > 1e-6) z.c += (c - z.c) * float((R2 - z.Rb) / d);
			z.Rb = R2;
		}
		if (first || z.members.empty()) continue;
		vVessel *vv = (vVessel *)vo;
		VECTOR3 va;
		if (!v->GetAirspeedVector(FRAME_LOCAL, va) || length(va) < 1.0) continue;
		D3DXVECTOR3 fl = V3(-va), f;
		D3DXVec3TransformNormal(&f, &fl, vv->MWorld());
		z.f = Unit(f); // downstream
		double dist = std::max(1.0, Len(z.c));
		z.px = z.Rb * pxs / dist;
		double a[3] = { z.c.x - z.f.x * z.Rb * 1.2, z.c.y - z.f.y * z.Rb * 1.2, z.c.z - z.f.z * z.Rb * 1.2 };
		double b[3] = { z.c.x + z.f.x * z.Rb * 4.0, z.c.y + z.f.y * z.Rb * 4.0, z.c.z + z.f.z * z.Rb * 4.0 };
		if (!CapsuleVisible(planes, a, b, 1.3 * z.Rb) || z.px < 0.25) continue;
		z.I = I;
		OBJHANDLE hA = v->GetAtmRef() ? v->GetAtmRef() : v->GetSurfaceRef();
		const ATMCONST *ac = hA ? oapiGetPlanetAtmConstants(hA) : nullptr;
		double rgb[3] = { 0.85, 0.6, 1.0 }; // [V] not air: CO2 and others
		if (!ac || std::fabs(ac->R - 287.0) <= 0.15 * 287.0) PlasmaColour(V * 1e-3, rgb);
		z.col = D3DXVECTOR3(float(rgb[0]), float(rgb[1]), float(rgb[2]));
		double M = v->GetMachNumber();
		z.tanmu = M > 1.01 ? std::tan(std::asin(1.0 / M)) : 1.0; // [P] Mach angle
		z.Tr = LumF(V3(vv->GetSunLight()->Transmission));
		uint32_t seed = Hash(uint32_t(uintptr_t(h) >> 4));
		z.flick = 1.0 + 0.2 * (VNoise(time * 4.5, seed) * 2.0 - 1.0); // [V] 3-6 Hz, no strobe
		z.wob = VNoise(time * 0.8, seed + 7) * 2.0 - 1.0;
		cand.push_back(z);
	}
	std::sort(cand.begin(), cand.end(), [](const Plasma &p, const Plasma &q) { return p.px > q.px; });
	if (cand.size() > size_t(MAXPZ)) cand.resize(MAXPZ);
	int maps = 0;
	for (Plasma &z : cand) {
		double dist = std::max(1.0, Len(z.c));
		if (flowOK && maps < 2 && z.px >= 48.0) {
			z.map = maps++;
			z.ext = 1.15 * z.Rb;
			z.Df = z.Rb + 5.0;
			z.texel = 2.0 * z.ext / FLOWN;
			double c[3] = { z.c.x, z.c.y, z.c.z }, f[3] = { z.f.x, z.f.y, z.f.z }, nf[3] = { -f[0], -f[1], -f[2] }, X[3], Y[3], Xb[3], Yb[3];
			FlowViewProj(c, f, z.ext, z.Df, 1.0, 2.0 * z.Rb + 10.0, z.vp[0], X, Y);
			FlowViewProj(c, nf, z.ext, z.Df, 1.0, 2.0 * z.Rb + 10.0, z.vp[1], Xb, Yb);
			z.X = D3DXVECTOR3(float(X[0]), float(X[1]), float(X[2]));
			z.Y = D3DXVECTOR3(float(Y[0]), float(Y[1]), float(Y[2]));
		}
		else { // the hull box as an ellipsoid (design 7.5)
			vVessel *mv = z.members[0];
			const D9BBox &hb = mv->HullBox;
			D3DXVECTOR3 lc((hb.min.x + hb.max.x) * 0.5f, (hb.min.y + hb.max.y) * 0.5f, (hb.min.z + hb.max.z) * 0.5f);
			D3DXVECTOR3 A(std::max(0.1f, (hb.max.x - hb.min.x) * 0.55f), std::max(0.1f, (hb.max.y - hb.min.y) * 0.55f), std::max(0.1f, (hb.max.z - hb.min.z) * 0.55f));
			const D3DXMATRIX *M = mv->MWorld();
			D3DXVec3TransformCoord(&z.c, &lc, M);
			z.e0 = Unit(D3DXVECTOR3(M->_11, M->_12, M->_13)) * (1.0f / A.x);
			z.e1 = Unit(D3DXVECTOR3(M->_21, M->_22, M->_23)) * (1.0f / A.y);
			z.e2 = Unit(D3DXVECTOR3(M->_31, M->_32, M->_33)) * (1.0f / A.z);
			z.Rb = Len(A);
			z.Df = z.Rb + 5.0;
			z.ext = z.Rb;
			z.texel = 0.02 * z.Rb;
		}
		double hpx = 2.0 * dist / pxs; // metres per half-res pixel
		z.lamMin = std::max(std::max(0.01 * z.Rb, 1.5 * hpx), 0.5 * z.texel);
		z.lamH = 0.08 * z.Rb; // [V]
		if (z.lamH < 1.5 * hpx) { double inf = 1.5 * hpx; z.I *= (z.lamH / inf) * (z.lamH / inf); z.lamH = inf; } // a glowing point far away (design 7.5)
		z.pa = z.c - z.f * float(1.25 * z.Rb + 3.0 * z.lamH);
		z.pb = z.c + z.f * float(z.Rb + 1.8 * z.Rb);
		z.step = std::max(z.lamH, 0.05 * z.Rb);
		for (vVessel *mv : z.members) reentryW[mv] = 0.0f; // the legacy billboard off (design review M2)
	}
	plasma = cand;
}

static vPlanet *PlanetOf (Scene *scn, OBJHANDLE hP)
{
	if (!hP) return nullptr;
	vObject *o = scn->GetVisObject(hP);
	return (o && oapiGetObjectType(hP) == OBJTP_PLANET) ? (vPlanet *)o : nullptr;
}

// diffuse particle streams and the ground clouds as lit spherical puffs (design 6)
void PostPlume::GatherPuffs ()
{
	struct PT { D3DXVECTOR3 c, sun, amb, ep, Tr, In; float r, sg, dist, Tf, seed, spin, slot; D3DXVECTOR3 alb; };
	std::vector<PT> pt;
	double simt = oapiGetSimTime();
	VECTOR3 cam = scn->Camera.pos;
	float e = scn->fExposure;
	D3DXVECTOR3 toSun = -V3(scn->sunLight.Dir);
	int samples = 0;
	auto light = [&](vPlanet *vp, const VECTOR3 &gpos, D3D9Sun &out, float &tf, float terrain) {
		if (vp && vp->IsActive()) { out = vp->GetObjectAtmoParams(gpos - vp->GlobalPos()); out.Color *= terrain; }
		else { out.Color = 1.0f; out.Ambient = 0.05f; out.Transmission = 1.0f; out.Incatter = 0.0f; }
		VECTOR3 d = gpos - cam;
		double l = length(d);
		tf = l > 1.0 ? scn->pFx->FogAlongRay(cam, d / l, l) : 1.0f;
		samples++;
	};
	std::vector<Puff> pm;
	std::vector<double> tau;
	auto column = [&](size_t i0) { // the column's shadow toward the sun (Kim and Neumann 2001, design 6.3)
		size_t n = pt.size() - i0;
		if (n < 2) return;
		pm.resize(n);
		for (size_t k = 0; k < n; k++) { const PT &p = pt[i0 + k]; pm[k] = { { p.c.x, p.c.y, p.c.z }, p.r, p.sg }; }
		double s[3] = { toSun.x, toSun.y, toSun.z };
		DeepOpacity(pm, s, 48, tau);
		for (size_t k = 0; k < n; k++) pt[i0 + k].sun *= float(std::exp(-tau[k]));
	};

	for (DWORD n = 0; n < scn->nstream; n++) {
		D3D9ParticleStream *s = scn->pstream[n];
		if (!s || !s->Diffuse() || s->IsReentry() || !s->First()) continue; // re-entry streams stay legacy (design review M3; round 2: as soft puffs Atlantis' trail veils the whole hull)
		OBJHANDLE h = s->Owner();
		VESSEL *v = (h && oapiIsVessel(h)) ? oapiGetVesselInterface(h) : nullptr;
		int rule = v ? CfgOf(v).puffs : -1;
		if (!(rule == 1 || (rule < 0 && s->DefaultTexture()))) continue; // add-on textures keep their look unless PUFFS 1 (design review S5)
		float slot = s->DefaultTexture() ? -1.0f : float(SlotOf(s->Texture()));
		vObject *vo = v ? scn->GetVisObject(h) : nullptr;
		float terrain = (vo && oapiGetObjectType(h) == OBJTP_VESSEL) ? ((vVessel *)vo)->terrainSun : 1.0f;
		vPlanet *vp = PlanetOf(scn, v ? v->GetSurfaceRef() : scn->GetCameraProxyBody());
		size_t i0 = pt.size();
		D3D9Sun L;
		float tf = 1.0f;
		VECTOR3 last = { 1e30, 1e30, 1e30 };
		int cnt = 0;
		for (const ParticleSpec *p = s->First(); p; p = p->next, cnt++) {
			if (cnt == 0 || ((cnt % 32 == 0 || length(p->pos - last) > 1000.0) && samples < 96)) { light(vp, p->pos, L, tf, terrain); last = p->pos; } // along the column (design review S7)
			PT q;
			q.c = V3(p->pos - cam);
			q.r = float(std::max(p->size, 0.1));
			double a = s->Alpha(p, simt) * 0.35; // the legacy alpha times a density scale [V]
			q.sg = float(4.0 * a / (4.0 / 3.0 * q.r));
			q.dist = float(Len(q.c));
			q.alb = D3DXVECTOR3(0.85f, 0.85f, 0.85f);
			q.sun = V3(L.Color) * e;
			q.amb = V3(L.Ambient) * e;
			q.Tr = V3(L.Transmission);
			q.In = V3(L.Incatter);
			q.Tf = tf;
			q.seed = float(Rand01(uint32_t(p->texidx) * 131u + uint32_t(uintptr_t(p) >> 4)) * 13.0);
			q.spin = float((simt - p->t0) * 0.15);
			q.slot = slot;
			pt.push_back(q);
		}
		column(i0);
		claimed.push_back(s);
	}

	for (auto &gg : *ground) {
		if (gg.second.empty() || !oapiIsVessel(gg.first)) continue;
		VESSEL *v = oapiGetVesselInterface(gg.first);
		vObject *vo = scn->GetVisObject(gg.first);
		float terrain = vo ? ((vVessel *)vo)->terrainSun : 1.0f;
		vPlanet *vp = PlanetOf(scn, v->GetSurfaceRef());
		VECTOR3 cen = { 0, 0, 0 };
		for (const GroundPuff &q : gg.second) cen += q.pos;
		cen /= double(gg.second.size());
		D3D9Sun L;
		float tf = 1.0f;
		light(vp, cen, L, tf, terrain); // the proxy's light at the cloud's centroid
		size_t i0 = pt.size();
		for (const GroundPuff &g : gg.second) {
			double age = simt - g.t0;
			double a = 0.6 * (1.0 - age / g.life) * Smoothstep(0.0, 1.5, age); // [V]
			if (a <= 0.005) continue;
			PT q;
			q.c = V3(g.pos - cam);
			q.r = float(g.size);
			q.sg = float(3.0 * a / (4.0 / 3.0 * g.size));
			q.dist = float(Len(q.c));
			q.alb = D3DXVECTOR3(g.col[0], g.col[1], g.col[2]);
			q.sun = V3(L.Color) * e;
			q.amb = V3(L.Ambient) * e;
			q.Tr = V3(L.Transmission);
			q.In = V3(L.Incatter);
			q.Tf = tf;
			q.seed = float(Rand01(g.seed) * 13.0);
			q.spin = float(age * 0.1);
			q.slot = -1.0f;
			pt.push_back(q);
		}
		column(i0);
	}
	if (pt.empty()) return;

	// the plumes' own light: the 4 brightest, orange from below at night (design 6.3)
	struct PL { D3DXVECTOR3 m, c; double k, L; };
	std::vector<PL> pls;
	for (const Plume &P : plumes) {
		D3DXVECTOR3 c(pal[P.cls * 4].x, pal[P.cls * 4].y, pal[P.cls * 4].z);
		if (P.cls == TEXTURE) c = D3DXVECTOR3(1.0f, 0.7f, 0.45f);
		float mx = std::max(c.x, std::max(c.y, c.z));
		if (mx <= 0.0f) continue;
		pls.push_back({ P.o + P.a * float(0.5 * P.L), c / mx, P.I * P.L * P.re * (P.cls == HYDROLOX ? 0.3 : 1.0), P.L });
	}
	std::sort(pls.begin(), pls.end(), [](const PL &p, const PL &q) { return p.k > q.k; });
	if (pls.size() > 4) pls.resize(4);
	for (PT &q : pt) {
		D3DXVECTOR3 ep(0, 0, 0);
		for (const PL &l : pls) { D3DXVECTOR3 d = q.c - l.m; ep += l.c * float(2.0 * l.k / (Dot3(d, d) + 0.25 * l.L * l.L)); } // [V]
		q.ep = ep;
	}

	// which side of the biggest plume each puff is on (design 6.2)
	double dMid = 1e30, best = 0.0;
	for (const Plume &P : plumes) if (P.rpx * P.I > best) { best = P.rpx * P.I; dMid = Len(P.o + P.a * float(0.5 * P.L)); }
	if (best <= 0.0 && !plasma.empty()) dMid = Len(plasma[0].c); // no plume: smoke behind the biggest plasma sheath goes behind it

	// at most 16 puffs around the camera, a fill budget of 32 half-res screens (design review S6), 8192 puffs
	std::vector<int> order(pt.size());
	for (size_t i = 0; i < order.size(); i++) order[i] = int(i);
	std::sort(order.begin(), order.end(), [&](int a, int b) { return pt[a].dist < pt[b].dist; });
	std::vector<int> keep;
	int inside = 0;
	double area = 0.0, scr = double(hw) * double(hh), budget = 32.0 * scr;
	for (int i : order) {
		const PT &q = pt[i];
		bool in = q.dist < 1.05f * q.r;
		if (!in) { double c[3] = { q.c.x, q.c.y, q.c.z }; if (!CapsuleVisible(planes, c, c, q.r)) continue; } // off screen: no budget, no place in the 8192
		if (in && ++inside > MAXINSIDE) continue;
		double rr = q.r * pxs * 0.5 / std::max(q.dist, 0.1f);
		area += in ? scr : std::min(PI * rr * rr * 1.3, scr);
		keep.push_back(i);
		if (int(keep.size()) >= MAXPUFF) break;
	}
	if (area > budget) { // a hashed subset, denser, keeps the look and the cost
		double k = budget / area;
		std::vector<int> kk;
		for (int i : keep) {
			PT &q = pt[i];
			bool in = q.dist < 1.05f * q.r;
			if (in || Rand01(Hash(uint32_t(q.seed * 1e5f))) < k) { q.sg *= float(1.0 / std::sqrt(k)); kk.push_back(i); }
		}
		keep.swap(kk);
	}
	std::sort(keep.begin(), keep.end(), [&](int a, int b) { return pt[a].dist > pt[b].dist; }); // back to front
	nPuff = int(keep.size());
	puffData.resize(size_t(nPuff) * 6);
	puffLayer.resize(nPuff);
	for (int j = 0; j < nPuff; j++) {
		const PT &q = pt[keep[j]];
		D3DXVECTOR3 al = q.alb;
		D3DXVECTOR4 *d = &puffData[size_t(j) * 6];
		d[0] = V4(q.c, q.r);
		d[1] = V4(D3DXVECTOR3(q.sun.x * al.x, q.sun.y * al.y, q.sun.z * al.z), q.sg);
		d[2] = V4(D3DXVECTOR3(q.amb.x * al.x, q.amb.y * al.y, q.amb.z * al.z), q.seed);
		d[3] = V4(D3DXVECTOR3(q.ep.x * al.x, q.ep.y * al.y, q.ep.z * al.z), q.spin);
		d[4] = V4(q.Tr, q.Tf);
		d[5] = V4(q.In, q.slot);
		puffLayer[j] = q.dist < dMid ? 1 : 0;
	}
}

// plume i's uniforms (Plume.glsl PlumeAt)
void PostPlume::FillPlume (int i, const Plume &P)
{
	D3DXVECTOR4 *q = &vPl[size_t(i) * PLS];
	const Jet &j = P.jet;
	q[0] = V4(P.o, P.re);
	q[1] = V4(P.a, P.L);
	q[2] = D3DXVECTOR4(float(j.jet ? j.rj : 1.0), float(j.thInf), float(j.jet ? j.Ls : 0.0), float(j.jet ? j.xmd : -1.0));
	q[3] = D3DXVECTOR4(float(P.cls), float(P.slot), float(P.I), float(j.watm));
	q[4] = P.ground;
	q[5] = D3DXVECTOR4(float(P.Ad), float(P.smoke), float(P.env / P.re), float(P.xg));
	q[6] = V4(P.Tr, P.Tf);
	q[7] = V4(P.In, P.seed);
	q[8] = V4(P.pa, P.cr);
	q[9] = V4(P.pb, P.step);
	q[10] = V4(P.sun, 0.0);
	q[11] = V4(P.amb, 0.0);
}

// plasma vessel k's uniforms (Plume.glsl FlowSurf, PlasmaAt, BandAt)
void PostPlume::FillPlasma (int k, const Plasma &z)
{
	D3DXVECTOR4 *q = &vPz[size_t(k) * PZS];
	bool m = z.map >= 0;
	q[0] = V4(z.f, m ? z.map : -1);
	q[1] = V4(m ? z.X : z.e0, z.ext);
	q[2] = V4(m ? z.Y : z.e1, z.Df);
	q[3] = V4(z.c, z.Rb);
	q[4] = V4(z.col, z.I);
	q[5] = D3DXVECTOR4(float(z.Tr), float(z.lamMin), float(z.lamH), 0.5f); // A_n [V] near wake; the far trail stays the legacy default stream (design review N12)
	q[6] = D3DXVECTOR4(0.0f, float(z.tanmu), float(z.wob), float(z.flick));
	q[7] = V4(m ? D3DXVECTOR3(0, 0, 0) : z.e2, z.texel);
	q[8] = V4(z.pa, 1.15 * z.Rb + 3.0 * z.lamH);
	q[9] = V4(z.pb, z.step);
}

// the pixels the passes touch: bounds of every primitive on screen
void PostPlume::ScreenRect ()
{
	const D3DXMATRIX &M = scn->Camera.mProjView;
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	bool full = false;
	auto sphere = [&](const D3DXVECTOR3 &c, double r) {
		double hx = c.x * M._11 + c.y * M._21 + c.z * M._31 + M._41;
		double hy = c.x * M._12 + c.y * M._22 + c.z * M._32 + M._42;
		double hw4 = c.x * M._14 + c.y * M._24 + c.z * M._34 + M._44;
		if (hw4 < r * 1.2 + 0.5) { full = true; return; }
		double sx = (hx / hw4 * 0.5 + 0.5) * W, sy = (0.5 - 0.5 * hy / hw4) * H, rr = r * pxs / hw4 * 1.25 + 6.0;
		x0 = std::min(x0, sx - rr); x1 = std::max(x1, sx + rr);
		y0 = std::min(y0, sy - rr); y1 = std::max(y1, sy + rr);
	};
	auto capsule = [&](const D3DXVECTOR3 &a, const D3DXVECTOR3 &b, double r) {
		int n = std::clamp(int(Len(b - a) / std::max(r, 1e-3)) + 1, 1, 32);
		for (int k = 0; k <= n && !full; k++) sphere(a + (b - a) * (float(k) / n), r * (1.0 + 1.0 / n));
	};
	for (const Plume &P : plumes) capsule(P.pa, P.pb, P.cr);
	for (const Plasma &z : plasma) capsule(z.pa, z.pb, 1.15 * z.Rb + 3.0 * z.lamH);
	for (int j = 0; j < nPuff && !full; j++) { const D3DXVECTOR4 &d = puffData[size_t(j) * 6]; sphere(D3DXVECTOR3(d.x, d.y, d.z), d.w); }
	if (full) { rectF[0] = rectF[1] = 0.0f; rectF[2] = rectF[3] = 1.0f; return; }
	x0 = std::max(0.0, std::floor(x0 / 2.0) * 2.0); y0 = std::max(0.0, std::floor(y0 / 2.0) * 2.0); // whole half-res texels
	x1 = std::min(double(W), std::ceil(x1 / 2.0) * 2.0); y1 = std::min(double(H), std::ceil(y1 / 2.0) * 2.0);
	if (x1 <= x0 || y1 <= y0) { rectF[0] = rectF[1] = 0.0f; rectF[2] = rectF[3] = 0.0f; return; }
	rectF[0] = float(x0 / W); rectF[1] = float(y0 / H); rectF[2] = float((x1 - x0) / W); rectF[3] = float((y1 - y0) / H);
}

void PostPlume::Revoke ()
{
	claimFrame = 0;
	for (D3D9ParticleStream *s : claimed) s->Unclaim();
	claimed.clear();
	reentryW.clear(); // the legacy re-entry billboards back
	reentryOff = false;
}

double PostPlume::LegacyWeight (const vVessel *vv, DWORD idx)
{
	PostPlume *p = inst;
	if (!p || p->claimFrame != p->scn->GetFrameId() + 1 || p->scn->GetRenderPass() != RENDERPASS_MAINSCENE) return 1.0;
	auto it = p->legacy.find(vv);
	if (it == p->legacy.end() || idx >= it->second.size()) return 1.0;
	return it->second[idx];
}

float PostPlume::LegacyReentry (const vVessel *vv)
{
	PostPlume *p = inst;
	if (!p || p->scn->GetRenderPass() != RENDERPASS_MAINSCENE || p->scn->GetFrameId() - p->reentryFrame > 2) return 1.0f; // the opaque pass runs before Collect: last frame's plasma
	if (p->reentryOff) return 0.0f;
	auto it = p->reentryW.find(vv);
	return it == p->reentryW.end() ? 1.0f : it->second;
}

const D3DXMATRIX *PostPlume::FlowVP () { return (inst && inst->flowOn) ? &inst->flowVP : nullptr; }

void PostPlume::HazeShape (const vVessel *vv, DWORD idx, double *r0, double *r1, double *len)
{
	PostPlume *p = inst;
	if (!p || p->claimFrame != p->scn->GetFrameId() + 1) return;
	auto it = p->plumeOf.find(vv);
	if (it == p->plumeOf.end() || idx >= it->second.size() || it->second[idx] < 0) return;
	const Plume &P = p->plumes[it->second[idx]];
	*r0 = P.R0; *r1 = P.R1; *len = P.L;
}

void PostPlume::Render (VkSurf *target)
{
	if (!ok || !target || claimFrame != scn->GetFrameId() + 1) return;
	VkTex *dep = scn->pFx ? scn->pFx->Depth() : nullptr;
	if (!dep) { if (drawN < 3) { drawN++; LogAlw("PostPlume: no tDepth this frame, legacy drawing"); } Revoke(); return; } // the legacy exhausts and particles draw
	VkDev::State st = dev->GetState();
	Bakes();
	if (!plasma.empty()) FlowMaps();
	Puffs(dep);
	bool vol = March(dep);
	Composite(target, dep, vol);
	if (drawN < 3) { drawN++; LogAlw("PostPlume: drawn: %d plumes, %d plasma, %d puffs, rect %.3f %.3f %.3f %.3f, march %d, I0 %.2f, L0 %.1f m", int(plumes.size()), int(plasma.size()), nPuff, rectF[0], rectF[1], rectF[2], rectF[3], vol ? 1 : 0, plumes.empty() ? 0.0 : plumes[0].I, plumes.empty() ? 0.0 : plumes[0].L); }
	gc->PushRenderTarget(target, gc->GetDepthStencil(), RENDERPASS_MAINSCENE); // the main target, viewport and depth back
	gc->PopRenderTargets();
	dev->SetState(st);
}

// exhaust and particle textures into their atlas slots: flame strip, flare, mean colour
void PostPlume::Bakes ()
{
	for (int i : bakeQueue) {
		Slot &s = atlasSlot[i];
		VkTex *t = s.h ? SURFACE(s.h)->GetTexture() : nullptr;
		if (!t || t != s.tex || t->img != s.img || s.baked) continue;
		bool done = true;
		for (int mode = 0; mode < 3; mode++) {
			ipi->Activate("PSAtlasBake");
			float x = float(i * 32 + (mode == 0 ? 0 : 16)), y = mode == 2 ? 48.0f : 0.0f, w = 16.0f, h = mode == 0 ? 64.0f : 16.0f;
			ipi->SetTemplate(w / 512.0f, h / 64.0f, x / 512.0f, y / 64.0f);
			D3DXVECTOR4 b(float(mode), 0.0f, 0.0f, 0.0f);
			ipi->SetFloat("vBake", &b, sizeof(b));
			ipi->SetTextureNative("tSrcP", t, IPF_LINEAR | IPF_CLAMP | IPF_MIPMAP_NATIVE);
			ipi->SetOutputNative(0, sAtlas);
			done = ipi->Execute(true) && done;
		}
		s.baked = done;
	}
	bakeQueue.clear();
}

// each mapped plasma vessel along its airflow: front and back depth, then prepared, reduced and blurred (design 7.3)
void PostPlume::FlowMaps ()
{
	for (const Plasma &z : plasma) {
		if (z.map < 0) continue;
		int m = z.map;
		for (int j = 0; j < 2; j++) {
			for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) flowVP.m[r][c] = float(z.vp[j][r * 4 + c]);
			flowOn = true;
			scn->BeginPass(RENDERPASS_FLOWMAP);
			gc->PushRenderTarget(sFlowRaw[m][j], sFlowDS, RENDERPASS_FLOWMAP);
			dev->Clear(true, true, false, 0, 1.0f, 0);
			dev->SetCullMode(VK_CULL_MODE_NONE); // the back map sees the hull from behind
			for (vVessel *v : z.members) v->Render(dev, false);
			gc->PopRenderTargets();
			scn->PopPass();
			flowOn = false;
		}
		D3DXVECTOR4 tex(1.0f / FLOWN, 1.0f / FLOWN, 1.0f / FLOWN, 1.0f / FLOWN);
		D3DXVECTOR4 fl(1.0f, float(2.0 * z.Rb + 10.0), float(2.0 * z.Df), float(z.texel));
		ipi->Activate("PSFlowPrep");
		ipi->SetFloat("vTex", &tex, sizeof(tex));
		ipi->SetFloat("vFlow", &fl, sizeof(fl));
		ipi->SetTextureNative("tSrcP", tFlowRaw[m][0], IPF_POINT | IPF_CLAMP);
		ipi->SetTextureNative("tSrc2P", tFlowRaw[m][1], IPF_POINT | IPF_CLAMP);
		ipi->SetOutputNative(0, sFlowP[m]);
		if (!ipi->Execute(true)) continue;
		ipi->Activate("PSFlowReduce");
		D3DXVECTOR4 r1(1.0f, 16.0f, 0.0f, 0.0f), r2(2.0f, 8.0f, 0.0f, 0.0f);
		ipi->SetFloat("vFlow2", &r1, sizeof(r1));
		ipi->SetTextureNative("tSrcP", tFlowP[m], IPF_POINT | IPF_CLAMP);
		ipi->SetOutputNative(0, sFlowR8[m]);
		ipi->Execute(true);
		ipi->Activate("PSFlowReduce");
		ipi->SetFloat("vFlow2", &r2, sizeof(r2));
		ipi->SetTextureNative("tSrcP", tFlowR8[m], IPF_POINT | IPF_CLAMP);
		ipi->SetOutputNative(0, sFlowR[m]);
		ipi->Execute(true);
		D3DXVECTOR4 bh(1.0f, 0.0f, 2.0f / FLOWN, 0.0f), bv(2.0f, 0.0f, 0.0f, 2.0f / FLOWN);
		ipi->Activate("PSFlowBlur");
		ipi->SetFloat("vFlow2", &bh, sizeof(bh));
		ipi->SetTextureNative("tSrcP", tFlowP[m], IPF_POINT | IPF_CLAMP);
		ipi->SetTextureNative("tSrc2P", tFlowR[m], IPF_POINT | IPF_CLAMP);
		ipi->SetOutputNative(0, sFlowBT);
		ipi->Execute(true);
		ipi->Activate("PSFlowBlur");
		ipi->SetFloat("vFlow2", &bv, sizeof(bv));
		ipi->SetTextureNative("tSrcP", tFlowBT, IPF_LINEAR | IPF_CLAMP);
		ipi->SetTextureNative("tSrc2P", tFlowR[m], IPF_POINT | IPF_CLAMP);
		ipi->SetOutputNative(0, sFlowB[m]);
		ipi->Execute(true);
	}
}

// the uniforms every march, blur and composite pass shares
void PostPlume::SetCommon (ImageProcessing *ip)
{
	D3DXVECTOR4 cx, cy, cz;
	scn->pFx->CameraRays(cx, cy, cz); // unjittered after the resolve
	D3DXVECTOR4 tex(1.0f / W, 1.0f / H, 1.0f / hw, 1.0f / hh);
	SceneFx *fx = scn->pFx;
	int flags = (fx->FogTex() ? 1 : 0) | (fx->VolumeDist() ? 2 : 0) | (fx->VolumeTex() && !fx->VolumeDist() ? 4 : 0);
	D3DXVECTOR4 fr(time, float(plumes.size()), float(plasma.size()), float(flags));
	D3DXVECTOR3 s = -V3(scn->sunLight.Dir);
	D3DXVECTOR4 sun(s.x, s.y, s.z, 0.05f);
	D3DXVECTOR4 cl(0, 0, 0, 0), cl2(0, 0, 0, 0);
	if (vPlanet *vp = scn->GetCameraProxyVisual()) if (fx->VolumeTex()) {
		D3DXVECTOR3 pc = V3(vp->GlobalPos() - scn->Camera.pos);
		double Rp = vp->GetSize(), base = vp->prm.cloudalt - 1000.0; // SceneFx::VolParams' slab
		cl = V4(pc, Rp + base);
		cl2 = D3DXVECTOR4(float(Rp + base + 3000.0), 0, 0, 0);
	}
	ip->SetFloat("vCamX", &cx, sizeof(cx));
	ip->SetFloat("vCamY", &cy, sizeof(cy));
	ip->SetFloat("vCamZ", &cz, sizeof(cz));
	ip->SetFloat("vTex", &tex, sizeof(tex));
	ip->SetFloat("vFrame", &fr, sizeof(fr));
	ip->SetFloat("vSun", &sun, sizeof(sun));
	ip->SetFloat("vCloud", &cl, sizeof(cl));
	ip->SetFloat("vCloud2", &cl2, sizeof(cl2));
}

// smoke and steam: spherical billboards, behind and in front of the biggest plume, premultiplied over, half res
void PostPlume::Puffs (VkTex *dep)
{
	dev->ColorFill(sPuff[0], NULL, 0);
	dev->ColorFill(sPuff[1], NULL, 0);
	if (nPuff <= 0) return;
	int k = nPuff <= 8 * PUFFROW ? 0 : (nPuff <= 32 * PUFFROW ? 1 : 2);
	VkTex *pd = tPuffData[k];
	size_t need = size_t(6 * PUFFROW) * puffRowsOf[k];
	puffData.resize(need, D3DXVECTOR4(0, 0, 0, 0));
	pd->Upload(0, 0, puffData.data(), need * sizeof(D3DXVECTOR4));
	static std::vector<WORD> idx;
	if (idx.empty()) {
		idx.resize(4096 * 6);
		for (int q = 0; q < 4096; q++) { WORD b = WORD(q * 4); WORD t[6] = { b, WORD(b + 1), WORD(b + 2), b, WORD(b + 2), WORD(b + 3) }; for (int i = 0; i < 6; i++) idx[q * 6 + i] = t[i]; }
	}
	SceneFx *fx = scn->pFx;
	VkTex *vol = fx->VolumeTex(), *vd = fx->VolumeDist(), *fog = fx->FogTex();
	D3DXVECTOR4 cx, cy, cz;
	fx->CameraRays(cx, cy, cz);
	D3DXVECTOR4 tex(1.0f / W, 1.0f / H, 1.0f / hw, 1.0f / hh);
	D3DXVECTOR3 s = -V3(scn->sunLight.Dir);
	D3DXVECTOR4 sun(s.x, s.y, s.z, time);
	D3DXVECTOR3 up(0, 1, 0);
	if (vPlanet *vp = scn->GetCameraProxyVisual()) up = Unit(V3(scn->Camera.pos - vp->GlobalPos()));
	int flags = (fog ? 1 : 0) | (vd ? 2 : 0) | (vol && !vd ? 4 : 0);
	D3DXVECTOR4 upf(up.x, up.y, up.z, float(flags));
	D3DXVECTOR4 cl(0, 0, 0, 0), cl2(0, 0, 0, 0);
	if (vPlanet *vp = scn->GetCameraProxyVisual()) if (vol) {
		D3DXVECTOR3 pc = V3(vp->GlobalPos() - scn->Camera.pos);
		double Rp = vp->GetSize(), base = vp->prm.cloudalt - 1000.0;
		cl = V4(pc, Rp + base);
		cl2 = D3DXVECTOR4(float(Rp + base + 3000.0), 0, 0, 0);
	}
	D3DXMATRIX mvp = scn->Camera.mProjView; // unjittered after the resolve
	std::vector<D3DXVECTOR4> vtx;
	for (int layer = 0; layer < 2; layer++) {
		vtx.clear();
		for (int i = 0; i < nPuff; i++) {
			if (puffLayer[i] != layer) continue;
			float f = float(i);
			vtx.push_back(D3DXVECTOR4(f, -1, -1, 0)); vtx.push_back(D3DXVECTOR4(f, 1, -1, 0));
			vtx.push_back(D3DXVECTOR4(f, 1, 1, 0)); vtx.push_back(D3DXVECTOR4(f, -1, 1, 0));
		}
		if (vtx.empty()) continue;
		gc->PushRenderTarget(sPuff[layer], NULL, RENDERPASS_MAINSCENE);
		VkDev::State ss = dev->GetState();
		ss.blendSeparate = false;
		ss.cull = VK_CULL_MODE_NONE;
		ss.fill = VK_POLYGON_MODE_FILL;
		dev->SetState(ss);
		pPuff->Setup(pVector4Decl, false, 2); // ONE, ONE_MINUS_SRC_ALPHA: premultiplied over
		pPuff->ClearTextures();
		pPuff->SetVSConstants("mVP", &mvp, sizeof(mvp));
		pPuff->SetPSConstants("pCamX", &cx, sizeof(cx));
		pPuff->SetPSConstants("pCamY", &cy, sizeof(cy));
		pPuff->SetPSConstants("pCamZ", &cz, sizeof(cz));
		pPuff->SetPSConstants("pTex", &tex, sizeof(tex));
		pPuff->SetPSConstants("pSun", &sun, sizeof(sun));
		pPuff->SetPSConstants("pUp", &upf, sizeof(upf));
		pPuff->SetPSConstants("pCloud", &cl, sizeof(cl));
		pPuff->SetPSConstants("pCloud2", &cl2, sizeof(cl2));
		pPuff->SetTextureVS("tPuffData", pd, IPF_POINT | IPF_CLAMP, 0);
		pPuff->SetTextureVS("tAtlasP", tAtlas, IPF_POINT | IPF_CLAMP, 0);
		pPuff->SetTexture("tDepthP", dep, IPF_POINT | IPF_CLAMP, 0);
		pPuff->SetTexture("tNoiseP", tNoise, IPF_LINEAR, 0);
		pPuff->SetTexture("tCloudV", vol ? vol : dep, IPF_POINT | IPF_CLAMP, 0);
		pPuff->SetTexture("tCloudD", vd ? vd : dep, IPF_POINT | IPF_CLAMP, 0);
		pPuff->SetTexture("tFogP", fog ? fog : dep, IPF_LINEAR | IPF_CLAMP, 0);
		pPuff->UpdateTextures();
		size_t nq = vtx.size() / 4;
		for (size_t q0 = 0; q0 < nq; q0 += 4096) {
			UINT n = UINT(std::min<size_t>(4096, nq - q0));
			dev->DrawIndexedPrimitiveUP(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, n * 4, n * 6, idx.data(), VK_INDEX_TYPE_UINT16, &vtx[q0 * 4], sizeof(D3DXVECTOR4));
		}
		gc->PopRenderTargets();
	}
}

// plumes and plasma: the march at half res (MRT colour and distance), then a depth-aware 3x3 blur
bool PostPlume::March (VkTex *dep)
{
	dev->ColorFill(sVolP, NULL, 0);
	dev->ColorFill(sVolPD, NULL, 0);
	if ((plumes.empty() && plasma.empty()) || rectF[2] <= 0.0f || rectF[3] <= 0.0f) return false;
	SceneFx *fx = scn->pFx;
	VkTex *vol = fx->VolumeTex(), *vd = fx->VolumeDist(), *fog = fx->FogTex();
	ipi->Activate("PSPlumeMarch");
	ipi->SetTemplate(rectF[2], rectF[3], rectF[0], rectF[1]);
	SetCommon(ipi);
	ipi->SetFloat("vPal", pal, sizeof(pal));
	ipi->SetFloat("vPl", vPl.data(), int(vPl.size() * sizeof(D3DXVECTOR4)));
	ipi->SetFloat("vPz", vPz.data(), int(vPz.size() * sizeof(D3DXVECTOR4)));
	ipi->SetTextureNative("tDepthP", dep, IPF_POINT | IPF_CLAMP);
	ipi->SetTextureNative("tNoiseP", tNoise, IPF_LINEAR);
	ipi->SetTextureNative("tAtlasP", tAtlas, IPF_LINEAR | IPF_CLAMP);
	for (int m = 0; m < 2; m++) {
		ipi->SetTextureNative(m ? "tFlowP1" : "tFlowP0", tFlowP[m], IPF_POINT | IPF_CLAMP);
		ipi->SetTextureNative(m ? "tFlowB1" : "tFlowB0", tFlowB[m], IPF_LINEAR | IPF_CLAMP);
		ipi->SetTextureNative(m ? "tFlowR1" : "tFlowR0", tFlowR[m], IPF_POINT | IPF_CLAMP);
	}
	ipi->SetTextureNative("tCloudV", vol ? vol : dep, IPF_POINT | IPF_CLAMP);
	ipi->SetTextureNative("tCloudD", vd ? vd : dep, IPF_POINT | IPF_CLAMP);
	ipi->SetTextureNative("tFogP", fog ? fog : dep, IPF_LINEAR | IPF_CLAMP);
	ipi->SetOutputNative(0, sVolP);
	ipi->SetOutputNative(1, sVolPD);
	bool marched = ipi->Execute(true);
	ipi->SetOutputNative(1, NULL);
	if (!marched) return false;
	ipi->Activate("PSPlumeBlur");
	ipi->SetTemplate(rectF[2], rectF[3], rectF[0], rectF[1]);
	SetCommon(ipi);
	ipi->SetTextureNative("tVolP", tVolP, IPF_POINT | IPF_CLAMP);
	ipi->SetTextureNative("tVolPD", tVolPD, IPF_POINT | IPF_CLAMP);
	ipi->SetOutputNative(0, sVolP2);
	return ipi->Execute(true);
}

// joint bilateral upsample of the puffs and the march, front puffs over the plumes over the back puffs (design 8)
void PostPlume::Composite (VkSurf *target, VkTex *dep, bool vol)
{
	if ((!vol && nPuff <= 0) || rectF[2] <= 0.0f || rectF[3] <= 0.0f) return;
	ipi->Activate("PSPlumeComposite");
	ipi->SetTemplate(rectF[2], rectF[3], rectF[0], rectF[1]);
	SetCommon(ipi);
	ipi->SetTextureNative("tDepthP", dep, IPF_POINT | IPF_CLAMP);
	ipi->SetTextureNative("tVolP", vol ? tVolP2 : tVolP, IPF_POINT | IPF_CLAMP); // the tap depths come from tDepth, so no tVolPD here
	ipi->SetTextureNative("tPuffBk", tPuff[0], IPF_POINT | IPF_CLAMP);
	ipi->SetTextureNative("tPuffFr", tPuff[1], IPF_POINT | IPF_CLAMP);
	ipi->SetOutputNative(0, target);
	VkDev::State s = dev->GetState();
	s.blendSeparate = false;
	s.blendEq.srcColorBlendFactor = s.blendEq.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	s.blendEq.dstColorBlendFactor = s.blendEq.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	s.blendEq.colorBlendOp = s.blendEq.alphaBlendOp = VK_BLEND_OP_ADD;
	dev->SetState(s);
	ipi->Execute(2, true); // keeps the device's ONE, ONE_MINUS_SRC_ALPHA
}
