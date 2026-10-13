// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: height and valley fog (design-height-fog, Rev 2): fog maps baked on GfxPool, a half-res march, blur and composite

#include "SceneFx.h"
#include "Scene.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "IProcess.h"
#include "VPlanet.h"
#include "Surfmgr2.h"
#include "Cloudmgr2.h"
#include "GfxPool.h"
#include "FogMath.h"
#include "VolCloudMap.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>

template<> int TileManager2<SurfTile>::ElevGrids (double lat0, double lat1, double lng0, double lng1, int lvl, int maxn, bool heights, const std::vector<unsigned> &have, std::vector<Horizon::Grid> &out, std::vector<unsigned> &keys); // Surfmgr2.cpp

namespace {
const double HALF[2] = { 25e3, 150e3 };   // near, far: half the map side, m
const double MARGIN[2] = { 16e3, 30e3 };  // grids beyond the map for the shadow sweep
const int LVL[2] = { 9, 7 };              // internal grid level asked first (153 m, 610 m nodes); one coarser above MAXGRIDS tiles, so the far map is mostly level 6 (1.2 km) at mid latitudes
const int MAXGRIDS = 25;
const double SIGDT[2] = { 1.0, 2.0 };     // grid signature checks, s

inline double FogSmooth (double a, double b, double x) { double t = std::min(1.0, std::max(0.0, (x - a) / (b - a))); return t * t * (3.0 - 2.0 * t); }
inline D3DXVECTOR4 V4 (const VECTOR3 &v, double w) { return D3DXVECTOR4(float(v.x), float(v.y), float(v.z), float(w)); }
inline VECTOR3 V3 (const double *a) { return _V(a[0], a[1], a[2]); }
}

struct SceneFx::FogState {
	ImageProcessing *ip = nullptr;         // HeightFog.glsl: PSFogMarch, PSFogBlur, PSFogComposite
	VkTex *tMap[2][2] = {};                // [near, far][slot], RGBA16F F V Hs m
	VkTex *tCld = nullptr;                 // cloud coverage around the far map's centre, R8, cloud frame
	VkTex *tFH[2] = {}, *tFD = nullptr;    // half res: march and blur (premultiplied), fog distance (R32F)
	VkSurf *sFH[2] = {}, *sFD = nullptr;
	bool ok = false;
	int poolId = 0;

	struct Box {                           // job results, shared with GfxPool's threads
		std::mutex mx;
		std::shared_ptr<const FogMath::Map> map[2];
		OBJHANDLE obj[2] = {};
		bool dropped[2] = {};
		double ms[2] = {};
		std::vector<uint8_t> cld;
		VolCloud::Frame cldFrame;
		OBJHANDLE cldObj = NULL;
		float covMean = -1.0f;
		bool cldDone = false;
	};
	std::shared_ptr<Box> box = std::make_shared<Box>();

	struct Cascade {
		std::shared_ptr<const FogMath::Map> cur, prev; // CPU copies of what tMap holds
		int slot = 0;                      // tMap slot of cur; prev sits in the other
		double fade0 = -1e9;               // system time of the last swap
		bool asked = false;
		OBJHANDLE obj = NULL;
		double sys = -1e9, retry = -1e9;   // request time; no new request before retry
		double backoff = 1.0;              // retry delay after a refused bake, doubled up to 30 s (review SHOULD 1)
		double at[2] = { 0.0, 0.0 };       // lat, lng of the last request
		double sun[3] = { 0, 0, 0 };       // planet-local sun at the request
		size_t sig = 0;
		double sigCheck = -1e9;
		int builds = 0;
	} cas[2];                              // 0 near, 1 far
	std::unordered_map<unsigned, std::pair<std::shared_ptr<const Horizon::Grid>, double>> grids; // by tile horGen, with the last use

	bool cldValid = false;                 // tCld holds this planet's coverage
	VolCloud::Frame cldFrame;
	OBJHANDLE cldObj = NULL;
	float covMean = -1.0f;

	FogMath::Params prm;                   // this frame's
	float wF = 0.0f, sunT = 1.0f;
	double ramp0 = -1.0;                   // system time of the first far map
	DWORD drawFrame = 0;
	bool drawn = false;
	double lamS = 0.0, lamSim = -1e30;     // subsolar longitude and its sim time: the sense of rotation
	int sense = 1;
	OBJHANDLE planet = NULL;
	MATRIX3 R;                             // planet rotation, global from local
	VECTOR3 P = _V(0, 0, 0);               // planet global position
	double size = 0.0, drift = 0.0, hc = 0.0;
	double rIn = 0.0, rOut = 0.0;          // this frame's fog shell radii (vShell.xy)
	double gridUse = -1e9;                 // system time Grids last ran
	int refused = 0;                       // grids the last Grids call refused
	VECTOR3 camL = _V(0, 0, 0), sunL = _V(0, 1, 0); // planet-local camera and unit sun
	float wPrev[2] = { 0.0f, 0.0f };

	~FogState ();
	bool Field (const double p[3], float f[4], float *ef) const;
	bool Update (SceneFx &fx, vPlanet *vp);
	void Collect ();
	void Requests (vPlanet *vp, double lat, double lng);
	int Grids (vPlanet *vp, double lat, double lng, int c, bool heights, std::vector<std::shared_ptr<const Horizon::Grid>> *out, std::vector<unsigned> &keys);
	void Submit (vPlanet *vp, int c, double lat, double lng);
	void Draw (SceneFx &fx, VkSurf *target, vPlanet *vp, float wV);
	double Transmit (const VECTOR3 &oL, const VECTOR3 &dL, double dist, int steps) const;
};

SceneFx::FogState::~FogState ()
{
	if (poolId) if (GfxPool *p = GfxPool::Peek()) p->Unregister(poolId);
	SAFE_DELETE(ip);
	for (int c = 0; c < 2; c++) for (int k = 0; k < 2; k++) SAFE_DELETE(tMap[c][k]);
	SAFE_DELETE(tCld);
	for (int i = 0; i < 2; i++) { SAFE_DELETE(sFH[i]); SAFE_DELETE(tFH[i]); }
	SAFE_DELETE(sFD);
	SAFE_DELETE(tFD);
}

void SceneFx::FreeFog (FogState *s)
{
	delete s;
}

// targets, map textures and the shaders, at scene start when HeightFog is on; a failure leaves everything else as it was
void SceneFx::InitFog ()
{
	if (fog) return;
	fog = new FogState();
	FogState &s = *fog;
	const VkImageUsageFlags rt = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	const VkImageUsageFlags up = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	UINT w = std::max(1u, W / 2), h = std::max(1u, H / 2);
	bool ok = depthOK;
	for (int i = 0; i < 2; i++) {
		s.tFH[i] = new VkTex(dev, w, h, 1, VK_FORMAT_R16G16B16A16_SFLOAT, rt);
		s.sFH[i] = new VkSurf(s.tFH[i], 0);
		ok = ok && s.tFH[i]->img;
	}
	s.tFD = new VkTex(dev, w, h, 1, VK_FORMAT_R32_SFLOAT, rt);
	s.sFD = new VkSurf(s.tFD, 0);
	ok = ok && s.tFD->img;
	for (int c = 0; c < 2; c++)
		for (int k = 0; k < 2; k++) {
			s.tMap[c][k] = new VkTex(dev, FogMath::MAPN, FogMath::MAPN, 1, VK_FORMAT_R16G16B16A16_SFLOAT, up);
			ok = ok && s.tMap[c][k]->img;
		}
	s.tCld = new VkTex(dev, VolCloud::MAPN, VolCloud::MAPN, 1, VK_FORMAT_R8_UNORM, up);
	ok = ok && s.tCld->img;
	std::string ppf = std::string(Config->bAtmoQuality ? "" : "_PERFORMANCE ") + (Config->Filmic ? "_FILMIC " : ""); // Scatter.glsl's table sizes, HDR()
	s.ip = new ImageProcessing(dev, "Modules/VulkanClient/HeightFog.glsl", "PSFogMarch", ppf.empty() ? NULL : ppf.c_str());
	s.ip->CompileShader("PSFogBlur");
	s.ip->CompileShader("PSFogComposite");
	s.ok = ok && s.ip->IsOK();
	if (!s.ok) { LogErr("SceneFx: height fog targets or shaders failed, height fog off"); FreeFog(fog); fog = nullptr; } // nothing kept (review NIT 1)
}

// HeightFog.glsl's Fields on the CPU copies, planet-local
bool SceneFx::FogState::Field (const double p[3], float f[4], float *ef) const
{
	const Cascade &Fc = cas[1], &Nc = cas[0];
	double r, rq;
	if (!Fc.cur || Fc.obj != planet || !Fc.cur->Sample(p, f, &r)) return false;
	*ef = float(1.0 - FogSmooth(0.85, 1.0, r));
	float q[4];
	if (wPrev[1] > 0.0f && Fc.prev && Fc.prev->Sample(p, q, &rq) && rq < 1.0)
		for (int c = 0; c < 4; c++) f[c] += (q[c] - f[c]) * wPrev[1];
	float g[4];
	if (Nc.cur && Nc.obj == planet && Nc.cur->Sample(p, g, &r)) {
		float wn = float(1.0 - FogSmooth(0.80, 0.95, r));
		if (wn > 0.0f) {
			if (wPrev[0] > 0.0f && Nc.prev && Nc.prev->Sample(p, q, &rq) && rq < 1.0)
				for (int c = 0; c < 4; c++) g[c] += (q[c] - g[c]) * wPrev[0];
			wn *= std::min(1.0f, std::max(0.0f, 1.0f + g[3])); // uncovered near texels give way to the far map (review MUST 1)
			for (int c = 0; c < 4; c++) f[c] += (g[c] - f[c]) * wn;
		}
	}
	return true;
}

// transmittance of this frame's fog from a planet-local point along a unit direction
double SceneFx::FogState::Transmit (const VECTOR3 &oL, const VECTOR3 &dL, double dist, int steps) const
{
	double b = dotp(oL, dL), oo = dotp(oL, oL), ho = b * b - (oo - rOut * rOut); // the stretch inside the fog shell, as HeightFog.glsl Shell() (review SHOULD 2)
	if (ho < 0.0) return 1.0;
	ho = std::sqrt(ho);
	double t0 = std::max(-b - ho, 0.5), t1 = std::min(std::min(-b + ho, dist), 200e3), hi = b * b - (oo - rIn * rIn);
	if (oo < rIn * rIn) t0 = std::max(t0, -b + std::sqrt(std::max(hi, 0.0)));
	else if (hi >= 0.0 && -b - std::sqrt(hi) > 0.0) t1 = std::min(t1, -b - std::sqrt(hi));
	if (!(t1 > t0)) return 1.0;
	double o[3] = { oL.x, oL.y, oL.z }, d[3] = { dL.x, dL.y, dL.z };
	FogMath::FieldFn fld = [this](const double p[3], float f[4], float *ef) { return Field(p, f, ef); };
	return FogMath::Transmittance(prm, fld, o, d, size, t0, t1, steps, 0.5, drift);
}

// finished bakes into the free texture of each pair; one in a running cross-fade waits in the box
void SceneFx::FogState::Collect ()
{
	double now = oapiGetSysTime();
	std::lock_guard<std::mutex> lk(box->mx);
	for (int c = 1; c >= 0; c--) {
		Cascade &C = cas[c];
		if (box->dropped[c]) { box->dropped[c] = false; C.asked = false; }
		if (!box->map[c]) continue;
		if (C.prev && now - C.fade0 < 1.0) continue;
		std::shared_ptr<const FogMath::Map> m = box->map[c];
		OBJHANDLE ob = box->obj[c];
		box->map[c].reset();
		C.asked = false;
		if (m->cover < 0.9f) { C.retry = now + C.backoff; C.backoff = std::min(30.0, 2.0 * C.backoff); continue; } // grids still loading (review NIT 8)
		C.backoff = 1.0;
		std::vector<uint16_t> h(m->px.size());
		for (size_t k = 0; k < h.size(); k++) h[k] = FogMath::ToHalf(m->px[k]);
		int slot = C.cur ? 1 - C.slot : C.slot;
		tMap[c][slot]->Upload(0, 0, h.data(), h.size() * sizeof(uint16_t));
		bool same = C.cur && C.obj == ob;
		C.prev = same ? C.cur : nullptr;
		C.cur = m;
		C.slot = slot;
		C.obj = ob;
		C.fade0 = now;
		if (C.builds++ == 0) LogAlw("SceneFx: height fog %s map: %.1f ms, cover %.2f, water %.3f, floor %.0f m, mean ground %.0f m", c ? "far" : "near", box->ms[c], m->cover, m->water, m->Fmin, m->Gmean);
		if (c == 1 && ramp0 < 0.0) ramp0 = now;
	}
	if (box->cldDone) {
		box->cldDone = false;
		if (box->cld.size() == size_t(VolCloud::MAPN) * VolCloud::MAPN) {
			tCld->Upload(0, 0, box->cld.data(), box->cld.size());
			cldFrame = box->cldFrame;
			cldObj = box->cldObj;
			covMean = box->covMean;
			cldValid = true;
		}
	}
}

// the ready elevation grids under a cascade's box: new tiles copied once, kept by horGen (review NIT 10)
int SceneFx::FogState::Grids (vPlanet *vp, double lat, double lng, int c, bool heights, std::vector<std::shared_ptr<const Horizon::Grid>> *out, std::vector<unsigned> &keys)
{
	gridUse = oapiGetSysTime();
	double la0, la1, lo0, lo1;
	FogMath::GridBox(lat, lng, HALF[c] * 1.415 + MARGIN[c], size, &la0, &la1, &lo0, &lo1); // all longitudes when the box holds a pole (review SHOULD 1)
	std::vector<unsigned> have;
	have.reserve(grids.size());
	for (const auto &e : grids) have.push_back(e.first);
	std::vector<Horizon::Grid> g;
	int n = -1;
	for (int lvl = LVL[c]; lvl >= 0 && n < 0; lvl--) n = vp->SurfMgr2()->ElevGrids(la0, la1, lo0, lo1, lvl, MAXGRIDS, heights, have, g, keys);
	if (!out || n <= 0) return n;
	double now = oapiGetSysTime();
	OBJHANDLE hP = vp->Object();
	auto Sane = [&](const Horizon::Grid &G) { // 5 inner nodes against the core's elevation: no flat or zero placeholders (review MUST 1)
		auto mm = std::minmax_element(G.h.begin(), G.h.end());
		double tol = std::max(50.0, 0.1 * double(*mm.second - *mm.first));
		std::vector<const Horizon::Grid*> one(1, &G);
		int good = 0;
		for (int k = 0; k < 5; k++) {
			double u = k == 0 ? 0.5 : ((k & 1) ? 0.25 : 0.75), v = k == 0 ? 0.5 : (k < 3 ? 0.25 : 0.75);
			double la = G.minlat + (G.maxlat - G.minlat) * v, lo = G.minlng + (G.maxlng - G.minlng) * u;
			float h;
			if (FogMath::GridHeight(one, la, lo, &h) && std::fabs(h - oapiSurfaceElevation(hP, lo, la)) <= tol) good++;
		}
		return good >= 3;
	};
	refused = 0;
	for (size_t i = 0; i < g.size(); i++) {
		auto it = grids.find(keys[i]);
		if (it == grids.end()) {
			if (g[i].h.empty()) continue;
			if (!Sane(g[i])) { refused++; continue; } // not cached: asked again at the next bake
			it = grids.emplace(keys[i], std::make_pair(std::make_shared<const Horizon::Grid>(std::move(g[i])), now)).first;
		}
		it->second.second = now;
		out->push_back(it->second.first);
	}
	for (auto it = grids.begin(); it != grids.end();) it = (now - it->second.second > 60.0) ? grids.erase(it) : std::next(it);
	return int(out->size());
}

static size_t KeySig (std::vector<unsigned> keys)
{
	std::sort(keys.begin(), keys.end());
	size_t s = keys.size();
	for (unsigned k : keys) s = s * 1000003u + k;
	return s;
}

// one bake on GfxPool (the far one also fills the cloud coverage)
void SceneFx::FogState::Submit (vPlanet *vp, int c, double lat, double lng)
{
	GfxPool &pool = GfxPool::Get();
	if (!poolId) poolId = pool.Register();
	double now = oapiGetSysTime();
	Cascade &C = cas[c];
	auto in = std::make_shared<FogMath::BakeIn>();
	std::vector<unsigned> keys;
	if (Grids(vp, lat, lng, c, true, &in->grids, keys) <= 0) { C.retry = now + 1.0; return; }
	C.at[0] = lat; C.at[1] = lng;
	C.sig = refused ? 0 : KeySig(keys);    // refused grids: a new bake after the next signature check
	C.sigCheck = refused ? now + 5.0 : now;
	int lv0 = 99, lv1 = -1;
	for (const auto &q : in->grids) { lv0 = std::min(lv0, q->lvl); lv1 = std::max(lv1, q->lvl); }
	if (C.builds < 3 || (refused && C.builds < 50)) LogAlw("SceneFx: height fog %s bake: %d grids, levels %d-%d, %d refused, core ground at the centre %.0f m", c ? "far" : "near", int(in->grids.size()), lv0, lv1, refused, oapiSurfaceElevation(vp->Object(), lng, lat));
	in->lat = lat; in->lng = lng; in->half = HALF[c]; in->R = size;
	in->sun[0] = sunL.x; in->sun[1] = sunL.y; in->sun[2] = sunL.z;
	in->mjd = oapiGetSimMJD();
	in->wet = vp->HasRipples(); // the planet cfg, not the launchpad's reflection option (review SHOULD 2)
	in->wFar = (c == 0 && cas[1].cur) ? cas[1].cur->water : -1.0;
	in->nightLen = 24.0 - FogMath::Sun(lat, lng, in->sun, sense).dayLen;
	if (c == 0 && cas[1].obj == vp->Object()) in->outer = cas[1].cur;
	std::vector<VolCloud::Src> srcs;
	VolCloud::Frame cf;
	bool cld = false;
	if (c == 1 && vp->prm.bCloud && vp->CloudMgr2()) {
		double lngc = VolCloud::CloudLng(lng, vp->prm.cloudrot);
		vp->CloudMgr2()->VolTiles(lat, lngc, (VolCloud::MAPHALF + 20e3) / size, srcs);
		if (!srcs.empty()) { cf.Set(lat, lngc); cld = true; }
	}
	std::shared_ptr<Box> b = box;
	OBJHANDLE hP = vp->Object();
	double R = size;
	GfxPool::Job j;
	j.owner = poolId;
	j.kind = GfxPool::FOGMAP;
	j.run = [b, in, c, hP, srcs, cf, cld, R]() {
		auto t0 = std::chrono::steady_clock::now();
		auto m = std::make_shared<FogMath::Map>();
		FogMath::Bake(*in, *m);
		std::vector<uint8_t> cov;
		float mean = -1.0f;
		if (cld) {
			VolCloud::Fill(cf, R, srcs, cov);
			double s = 0.0;
			int n = 0, mid = VolCloud::MAPN / 2, r = int(VolCloud::MAPN * 50e3 / (2.0 * VolCloud::MAPHALF));
			for (int y = mid - r; y < mid + r; y++)
				for (int x = mid - r; x < mid + r; x++)
					if ((x - mid) * (x - mid) + (y - mid) * (y - mid) <= r * r) { s += cov[size_t(y) * VolCloud::MAPN + x]; n++; }
			mean = n ? float(s / (255.0 * n)) : -1.0f;
		}
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::lock_guard<std::mutex> lk(b->mx);
		b->map[c] = m;
		b->obj[c] = hP;
		b->ms[c] = ms;
		if (cld) { b->cld.swap(cov); b->cldFrame = cf; b->cldObj = hP; b->covMean = mean; b->cldDone = true; }
	};
	j.dropped = [b, c]() { std::lock_guard<std::mutex> lk(b->mx); b->dropped[c] = true; };
	pool.Submit(std::move(j));
	C.asked = true;
	C.sys = now;
	C.sun[0] = sunL.x; C.sun[1] = sunL.y; C.sun[2] = sunL.z;
}

// far first, the near map once a far one is up; thresholds in FogMath::NeedRebuild
void SceneFx::FogState::Requests (vPlanet *vp, double lat, double lng)
{
	double now = oapiGetSysTime();
	OBJHANDLE hP = vp->Object();
	VECTOR3 up = unit(camL);
	double elev = std::asin(std::max(-1.0, std::min(1.0, dotp(up, sunL)))) * DEG;
	for (int c = 1; c >= 0; c--) {
		Cascade &C = cas[c];
		if (now < C.retry && C.backoff > 1.0 && std::acos(std::max(-1.0, std::min(1.0, std::sin(lat) * std::sin(C.at[0]) + std::cos(lat) * std::cos(C.at[0]) * std::cos(lng - C.at[1])))) * size > 0.2 * HALF[c]) { C.retry = -1e9; C.backoff = 1.0; } // moved on: no back-off
		if (C.asked || now < C.retry) continue;
		if (c == 0 && !(cas[1].cur && cas[1].obj == hP)) continue;
		FogMath::Rebuild r;
		r.valid = C.cur != nullptr;
		r.sameObj = C.obj == hP;
		r.sunElevDeg = elev;
		if (r.valid && r.sameObj) {
			double p[3] = { camL.x, camL.y, camL.z }, x, y;
			r.dist = C.cur->f.ToMap(p, size, &x, &y) ? std::hypot(x, y) : 1e30;
			r.sunDeg = std::acos(std::max(-1.0, std::min(1.0, dotp(V3(C.sun), sunL)))) * DEG;
			r.age = now - C.sys;
		}
		bool want = FogMath::NeedRebuild(r, c == 0);
		if (!want && now - C.sigCheck > SIGDT[c]) { // finer or newer tiles under the map
			C.sigCheck = now;
			std::vector<unsigned> keys;
			if (Grids(vp, C.cur->f.lat, C.cur->f.lng, c, false, nullptr, keys) > 0) {
				r.sigChanged = KeySig(keys) != C.sig;
				want = FogMath::NeedRebuild(r, c == 0);
			}
		}
		if (want) Submit(vp, c, lat, lng);
	}
}

// this frame's state; true when the fog draws
bool SceneFx::FogState::Update (SceneFx &fx, vPlanet *vp)
{
	sunT = 1.0f;
	wF = 0.0f;
	if (!grids.empty() && oapiGetSysTime() - gridUse > 60.0) { grids.clear(); cas[0].prev.reset(); cas[1].prev.reset(); } // out of range for a minute: drop the copies (review NIT 4)
	if (!ok || !vp || !fx.DepthValid() || !vp->HasAtmosphere() || vp->GetShaderID() == PLT_GIANT || !vp->SurfMgr2() || !vp->GetScatterConst()) return false;
	for (int t : { SUN_COLOR, RAY_LAND, MIE_LAND, ATN_LAND, SKY_AMBIENT }) if (!vp->GetScatterTable(t)) return false; // review SHOULD 6
	hc = vp->CameraAltitude();
	if (hc > 45e3) return false;
	OBJHANDLE hP = vp->Object();
	if (hP != planet) { planet = hP; ramp0 = -1.0; lamSim = -1e30; sense = 1; cldValid = false; }
	Collect();
	oapiGetRotationMatrix(hP, &R);
	oapiGetGlobalPos(hP, &P);
	size = vp->GetSize();
	camL = tmul(R, fx.scn->GetCameraGPos() - P);
	VECTOR3 gs;
	oapiGetGlobalPos(oapiGetGbodyByIndex(0), &gs);
	sunL = unit(tmul(R, gs - P));
	double cr = length(camL), lat = std::asin(std::max(-1.0, std::min(1.0, camL.y / cr))), lng = std::atan2(camL.z, camL.x);
	double simt = oapiGetSimTime();
	if (std::fabs(simt - lamSim) > 1.0) { // the subsolar point runs west on a prograde body (review NIT 6)
		double lam = std::atan2(sunL.z, sunL.x);
		if (lamSim > -1e29) { double dl = std::remainder(lam - lamS, 2.0 * PI); if (std::fabs(dl) > 1e-7) sense = (dl < 0.0) == (simt > lamSim) ? 1 : -1; }
		lamS = lam;
		lamSim = simt;
	}
	Requests(vp, lat, lng);
	const Cascade &Fc = cas[1];
	if (!Fc.cur || Fc.obj != hP) return false;

	FogMath::FrameIn in;
	in.lat = lat; in.lng = lng; in.mjd = oapiGetSimMJD();
	in.sun[0] = sunL.x; in.sun[1] = sunL.y; in.sun[2] = sunL.z;
	in.sense = sense;
	in.covMean = (cldValid && cldObj == hP) ? covMean : -1.0;
	in.wFar = Fc.cur->water;
	in.wet = vp->HasRipples();
	in.Gmean = Fc.cur->Gmean;
	prm = FogMath::Frame(in);
	const Cascade &Nc = cas[0];
	double fmin = Fc.cur->Fmin, top = std::max(double(Fc.cur->Ltop), Fc.cur->Gmax + prm.Hinv + 150.0);
	if (Nc.cur && Nc.obj == hP) { fmin = std::min(fmin, double(Nc.cur->Fmin)); top = std::max(top, double(Nc.cur->Ltop)); }
	top = std::max(top, 1.5 * prm.Ds + 5.0);
	rIn = size + fmin - 20.0;
	rOut = size + top + 200.0;

	double now = oapiGetSysTime();
	double ramp = ramp0 < 0.0 ? 0.0 : std::min(1.0, (now - ramp0) / 2.0);
	double w = (1.0 - FogSmooth(12e3, 30e3, hc - Fc.cur->Gmean)) * ramp;
	if (vp->prm.bCloud && !(cldValid && cldObj == hP)) w *= 1.0 - FogSmooth(vp->prm.cloudalt, vp->prm.cloudalt + 500.0, hc); // review SHOULD 3 without a coverage map
	wF = float(w);
	if (wF <= 1e-3f) return false;
	for (int c = 0; c < 2; c++) wPrev[c] = (cas[c].prev && now - cas[c].fade0 < 1.0) ? float(1.0 - (now - cas[c].fade0)) : 0.0f;
	drift = std::fmod(simt * 0.6, 64.0 * 700.0);
	if (drift < 0.0) drift += 64.0 * 700.0;
	sunT = float(1.0 - wF * (1.0 - Transmit(camL, sunL, 30e3, 16))); // the glare and the flare
	return true;
}

void SceneFx::FogState::Draw (SceneFx &fx, VkSurf *target, vPlanet *vp, float wV)
{
	const Cascade &Fc = cas[1], &Nc = cas[0];
	bool nearOK = Nc.cur && Nc.obj == planet;
	auto Axes = [&](const FogMath::Map &m, double wN, D3DXVECTOR4 &E, D3DXVECTOR4 &N, D3DXVECTOR4 &U) { // the map frame, geocentric global axes
		E = V4(mul(R, V3(m.f.E)), 1.0 / m.half);
		N = V4(mul(R, V3(m.f.N)), wN);
		U = V4(mul(R, V3(m.f.U)), m.R);
	};
	D3DXVECTOR4 fE, fN, fU, nE, nN, nU, pE, pN, pU, qE, qN, qU;
	Axes(*Fc.cur, 1.0, fE, fN, fU);
	if (nearOK) Axes(*Nc.cur, 1.0, nE, nN, nU);
	else { nE = fE; nN = fN; nU = fU; nN.w = 0.0f; }
	if (nearOK && wPrev[0] > 0.0f) Axes(*Nc.prev, wPrev[0], pE, pN, pU);
	else { pE = nE; pN = nN; pU = nU; pN.w = 0.0f; }
	if (wPrev[1] > 0.0f) Axes(*Fc.prev, wPrev[1], qE, qN, qU);
	else { qE = fE; qN = fN; qU = fU; qN.w = 0.0f; }

	bool cv = cldValid && cldObj == planet && vp->prm.bCloud;
	D3DXVECTOR4 cE = fE, cN = fN, cU = fU;
	if (cv) { // the cloud frame turned by cloudrot, as SceneFx::VolParams
		double rot = vp->prm.cloudrot, ca = std::cos(rot), sa = std::sin(rot);
		auto ToGlobal = [&](const double *v) { return mul(R, _V(v[0] * ca + v[2] * sa, v[1], v[2] * ca - v[0] * sa)); };
		cE = V4(ToGlobal(cldFrame.E), 1.0 / VolCloud::MAPHALF);
		cN = V4(ToGlobal(cldFrame.N), 0.0);
		cU = V4(ToGlobal(cldFrame.U), size);
	}
	double deck = vp->prm.bCloud ? vp->prm.cloudalt : 1e7;
	D3DXVECTOR4 cld(float(size + deck), cv ? 1.0f : 0.0f, 2.5f, std::max(covMean, 0.0f));
	D3DXVECTOR4 plA = V4(mul(R, _V(1, 0, 0)), drift), plB = V4(mul(R, _V(0, 1, 0)), 0.0), plC = V4(mul(R, _V(0, 0, 1)), 0.0);
	D3DXVECTOR4 vfog(prm.sf, prm.sh, prm.D0, prm.kv), vfog2(prm.Hinv, prm.Hh, prm.ss, prm.Ds);
	D3DXVECTOR4 vfog3(prm.Gmean, wF, wV, (cv && hc > deck) ? 1.0f : 0.0f);
	float kVt = float(1.0 - FogSmooth(30.0, 100.0, oapiGetTimeAcceleration())); // the shadow heights lag at high warp
	D3DXVECTOR4 light(0.0f, 1.0f, 2.0f, kVt);
	D3DXVECTOR4 shell = D3DXVECTOR4(float(rIn), float(rOut), float(size), float(hc)); // from Update
	D3DXVECTOR4 march(16.0f, fx.scn->TaaActive() ? float(fx.frame % 64) : -1.0f, 200e3f, 0.0f);
	D3DXVECTOR4 tex(1.0f / float(fx.W), 1.0f / float(fx.H), 1.0f / float(tFH[0]->w), 1.0f / float(tFH[0]->h));
	D3DXVECTOR4 camX, camY, camZ;
	fx.CameraRays(camX, camY, camZ);

	VkTex *tf = tMap[1][Fc.slot], *tn = nearOK ? tMap[0][Nc.slot] : tf;
	VkTex *tnp = (nearOK && wPrev[0] > 0.0f) ? tMap[0][1 - Nc.slot] : tn, *tfp = wPrev[1] > 0.0f ? tMap[1][1 - Fc.slot] : tf;
	auto Set = [&](const char *n, const D3DXVECTOR4 &v) { ip->SetFloat(n, &v, sizeof(v)); };

	ip->Activate("PSFogMarch");
	ip->SetStruct("Const", vp->GetScatterConst(), sizeof(ConstParams));
	Set("vCamX", camX); Set("vCamY", camY); Set("vCamZ", camZ);
	Set("vNE", nE); Set("vNN", nN); Set("vNU", nU);
	Set("vFE", fE); Set("vFN", fN); Set("vFU", fU);
	Set("vPE", pE); Set("vPN", pN); Set("vPU", pU);
	Set("vQE", qE); Set("vQN", qN); Set("vQU", qU);
	Set("vCE", cE); Set("vCN", cN); Set("vCU", cU);
	Set("vCld", cld);
	Set("vPlA", plA); Set("vPlB", plB); Set("vPlC", plC);
	Set("vFog", vfog); Set("vFog2", vfog2); Set("vFog3", vfog3);
	Set("vLight", light); Set("vShell", shell); Set("vMarch", march); Set("vTex", tex);
	ip->SetTextureNative("tSun", vp->GetScatterTable(SUN_COLOR), IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tLndRay", vp->GetScatterTable(RAY_LAND), IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tLndMie", vp->GetScatterTable(MIE_LAND), IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tLndAtn", vp->GetScatterTable(ATN_LAND), IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tAmbient", vp->GetScatterTable(SKY_AMBIENT), IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tDepthF", fx.tDepth, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tFogN", tn, IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tFogF", tf, IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tFogNp", tnp, IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tFogFp", tfp, IPF_LINEAR | IPF_CLAMP);
	ip->SetTextureNative("tCldF", cv ? tCld : tf, IPF_LINEAR | IPF_CLAMP);
	ip->SetOutputNative(0, sFH[0]);
	ip->SetOutputNative(1, sFD);
	bool marched = ip->Execute(true);
	ip->SetOutputNative(1, NULL);
	if (!marched) return;

	ip->Activate("PSFogBlur");
	Set("vTex", tex);
	ip->SetTextureNative("tDepthF", fx.tDepth, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tFH", tFH[0], IPF_POINT | IPF_CLAMP);
	ip->SetOutputNative(0, sFH[1]);
	if (!ip->Execute(true)) return;

	ip->Activate("PSFogComposite");
	Set("vTex", tex);
	ip->SetTextureNative("tDepthF", fx.tDepth, IPF_POINT | IPF_CLAMP);
	ip->SetTextureNative("tFH", tFH[1], IPF_POINT | IPF_CLAMP);
	ip->SetOutputNative(0, target);
	if (!ip->Execute(1, true)) return;
	drawFrame = fx.scn->GetFrameId();
	drawn = true;
}

// INT-5: the fog under or over the volume clouds by the camera's side of the slab; without the fog only the clouds, as before
void SceneFx::FogAndClouds (VkSurf *target, vPlanet *vp, bool clouds, float wV)
{
	if (!fog || !fog->Update(*this, vp)) {
		if (clouds) Clouds(target, vp, wV);
		return;
	}
	bool fogFirst = clouds && vp->CameraAltitude() > vp->prm.cloudalt + 500.0; // above the slab's middle the clouds cover the fog
	if (fogFirst) fog->Draw(*this, target, vp, wV);
	if (clouds) Clouds(target, vp, wV);
	if (!fogFirst) fog->Draw(*this, target, vp, clouds ? wV : 0.0f);
}

float SceneFx::FogAlongRay (const VECTOR3 &from, const VECTOR3 &dir, double dist) const
{
	if (!fog || !fog->drawn || scn->GetFrameId() - fog->drawFrame > 1 || !(dist > 0.0)) return 1.0f; // this frame's fog, or the last one's before AfterOpaque
	const FogState &s = *fog;
	VECTOR3 o = tmul(s.R, from - s.P), d = unit(tmul(s.R, dir));
	return float(1.0 - s.wF * (1.0 - s.Transmit(o, d, dist, 8))); // 8 steps: about 2.3 us a call against 3.3 us (scratch bench), the plumes call it up to ~130 times a frame (review SHOULD 2)
}

float SceneFx::FogTowardSun () const
{
	return (fog && fog->drawn && fog->drawFrame == scn->GetFrameId()) ? fog->sunT : 1.0f;
}

VkTex *SceneFx::FogTex () const
{
	return (fog && fog->drawn && fog->drawFrame == scn->GetFrameId()) ? fog->tFH[1] : nullptr;
}

VkTex *SceneFx::FogDist () const
{
	return (fog && fog->drawn && fog->drawFrame == scn->GetFrameId()) ? fog->tFD : nullptr;
}
