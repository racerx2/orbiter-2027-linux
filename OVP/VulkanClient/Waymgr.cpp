// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: way texture jobs for the terrain tiles: streamed roads, rail, airports and rivers (design-ways)

#include "Surfmgr2.h"
#include "VPlanet.h"
#include "Scene.h"
#include "D3D9Config.h"
#include "GfxPool.h"
#include "VkTexFile.h"
#include "WayGeom.h"
#include "BaseShapes.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <list>
#include <mutex>

// g++: the specializations declared before SurfTile::WayParams uses them
template<> void TileManager2<SurfTile>::WayRequest (SurfTile *t, double dist);
template<> void TileManager2<SurfTile>::WayClaim ();
template<> void TileManager2<SurfTile>::WaySubmit ();
template<> void TileManager2<SurfTile>::WayUpload ();

namespace {
const int WayL0 = 16;           // file levels with their own way textures
const double WayRange = 60e3;   // camera distance a tile asks within [m]
const double WayMargin = 60.0;  // way tiles read past the tile's edge [m]: the widest half width and its markings
const int WaySize = 512, LampSize = 256;
const int LoadsPerFrame = 2, UploadsPerFrame = 2;
const size_t CacheTiles = 64;

// z14 Web Mercator tile of a point, as TreeMgr
void TileOf (double lat, double lng, int &x, int &y)
{
	int n = 1 << 14;
	lat = std::max (-1.4844, std::min (1.4844, lat));
	x = (int)std::floor ((lng / (2.0 * PI) + 0.5) * n);
	y = (int)std::floor ((1.0 - std::asinh (std::tan (lat)) / PI) * 0.5 * n);
	x = std::max (0, std::min (n - 1, x));
	y = std::max (0, std::min (n - 1, y));
}
}

unsigned WayNewGen ()
{
	static std::atomic<unsigned> gens{0};
	return ++gens;
}

struct WayState {
	struct Cand { SurfTile *t; double dist; };
	struct Ask { int lvl, ilat, ilng; unsigned gen; double lat0, lat1, lng0, lng1, dist; };
	struct Result { int lvl, ilat, ilng; unsigned gen; std::shared_ptr<const WayGeom::Output> out; }; // out NULL: dropped
	struct Box { std::mutex mx; std::deque<Result> done; };
	struct Back { int lvl, ilat, ilng; unsigned gen; int state; }; // WaySubmit's answer: 0 asked again later, 3 no way data here
	int id = 0;
	std::shared_ptr<Box> box = std::make_shared<Box>();
	Cand cand[2];
	int ncand = 0;
	std::vector<Ask> asked;
	std::vector<Back> back;
	std::list<std::pair<long long, std::shared_ptr<const WayGeom::Tile>>> cache; // read tiles, the latest first
	BaseShapes::Set bases;
	FixedVes::Set fixed{true};
	std::vector<ClearZones::Zone> zones;
	bool zonesRead = false;
	double tFixed = 0.0;
};

void WayStateFree (WayState *w)
{
	if (!w) return;
	if (GfxPool *p = GfxPool::Peek()) p->Unregister(w->id);
	delete w;
}

// -----------------------------------------------------------------------

void SurfTile::WayParams ()
{
	vPlanet *vp = mgr->GetPlanet();
	FlowControlPS *fc = vp->GetFlowControl();
	fc->bWay = false;
	fc->bWayN = false;
	if (!smgr->bStream || !(smgr->streamLayers & TSLAYER_BLDG) || !mgr->GetShader()->bWays || lvl + 4 < WayL0) return;
	const Scene *scene = mgr->GetScene();
	if (scene->GetRenderPass() == RENDERPASS_MAINSCENE && vp == scene->GetCameraProxyVisual()) {
		double d = GetCameraDistance();
		if (d < WayRange) smgr->WayRequest(this, d);
	}
	SurfTile *o = this;
	while (o && o->wayState != 2) {
		o = o->getSurfParent();
		if (o && o->lvl + 4 < WayL0) o = NULL;
	}
	if (!o) return;
	int nb = 1 << (lvl - o->lvl), mask = nb - 1;
	double sc = 1.0 / nb;
	vp->GetTerrainParams()->vWayOff = FVECTOR4(float((ilng & mask) * sc), float((ilat & mask) * sc), float(sc), float(sc)); // v from the north
	fc->bWayN = true;
	if (o->wtex) {
		mgr->GetShader()->SetTexture("tWay", o->wtex, IPF_CLAMP | IPF_ANISOTROPIC, Config->Anisotrophy);
		fc->bWay = true;
	}
	mgr->GetShader()->SetTexture("tWayN", o->wntex, IPF_CLAMP | IPF_LINEAR); // NULL: the default black, no lamps
}

// -----------------------------------------------------------------------

template<>
void TileManager2<SurfTile>::WayRequest (SurfTile *t, double dist)
{
	if (!ways) {
		ways = new WayState;
		ways->id = GfxPool::Get().Register();
	}
	double now = oapiGetSysTime();
	if (t->wayState == 1 && now - t->wayAsk > 10.0) t->wayState = 0; // a lost job
	if (t->wayState != 0 || now - t->wayAsk < 1.0) return;
	WayState &W = *ways;
	if (W.ncand < 2) W.cand[W.ncand++] = {t, dist};
	else {
		int w = W.cand[0].dist > W.cand[1].dist ? 0 : 1; // the farther
		if (dist < W.cand[w].dist) W.cand[w] = {t, dist};
	}
}

template<>
void TileManager2<SurfTile>::WayClaim ()
{
	if (!ways) return;
	WayState &W = *ways;
	double now = oapiGetSysTime();
	for (int i = 0; i < W.ncand; i++) {
		SurfTile *t = W.cand[i].t;
		t->wayState = 1;
		t->wayAsk = now;
		W.asked.push_back({t->lvl, t->ilat, t->ilng, t->wayGen, t->bnd.minlat, t->bnd.maxlat, t->bnd.minlng, t->bnd.maxlng, W.cand[i].dist});
	}
	W.ncand = 0;
}

template<>
void TileManager2<SurfTile>::WaySubmit ()
{
	if (!ways || ways->asked.empty()) return;
	WayState &W = *ways;
	OBJHANDLE hObj = Cbody();
	double R = obj_size;
	int loads = 0;
	// a way tile from the cache, else read: 1 there, 0 not this frame, -1 it would not read
	auto get = [&] (int x, int y, std::vector<std::shared_ptr<const WayGeom::Tile>> &out) {
		long long key = ((long long)x << 32) | (unsigned)y;
		for (auto it = W.cache.begin(); it != W.cache.end(); ++it)
			if (it->first == key) {
				W.cache.splice(W.cache.begin(), W.cache, it);
				out.push_back(it->second);
				return 1;
			}
		if (loads >= LoadsPerFrame) return 0;
		loads++;
		TSWAYTILE *t = oapiTileStreamWayLoad(hObj, x, y);
		if (!t) return -1;
		W.cache.push_front({key, WayGeom::FromApi(*t)});
		oapiTileStreamWayFree(t);
		if (W.cache.size() > CacheTiles) W.cache.pop_back();
		out.push_back(W.cache.front().second);
		return 1;
	};
	for (const WayState::Ask &a : W.asked) {
		double latc = 0.5 * (a.lat0 + a.lat1), lngc = 0.5 * (a.lng0 + a.lng1);
		double ml = WayMargin / R, mg = ml / std::max(0.05, cos(latc));
		int x0, y0, x1, y1;
		TileOf(a.lat1 + ml, a.lng0 - mg, x0, y0);
		TileOf(a.lat0 - ml, a.lng1 + mg, x1, y1);
		std::vector<std::shared_ptr<const WayGeom::Tile>> tiles;
		bool wait = false;
		int n = 0, never = 0;
		for (int y = y0; y <= y1 && !wait; y++)
			for (int x = x0; x <= x1 && !wait; x++) {
				n++;
				int w = oapiTileStreamWayWant(hObj, x, y);
				if (w == 0) wait = true;
				else if (w < 0) never++;
				else {
					int g = get(x, y, tiles);
					if (g == 0) wait = true;
					else if (g < 0) never++;
				}
			}
		for (int round = 0; round < 3 && !wait; round++) { // a runway cut at a tile edge: its next piece
			std::vector<std::pair<int, int>> more = WayGeom::RunwayTiles(tiles);
			if (more.empty()) break;
			for (const auto &xy : more) {
				int w = oapiTileStreamWayWant(hObj, xy.first, xy.second);
				if (w == 0) wait = true;
				else if (w > 0 && get(xy.first, xy.second, tiles) == 0) wait = true;
				if (wait) break;
			}
		}
		if (wait || !GfxPool::Get().Wants(GfxPool::WAYS, a.dist)) { W.back.push_back({a.lvl, a.ilat, a.ilng, a.gen, 0}); continue; }
		if (never == n) { W.back.push_back({a.lvl, a.ilat, a.ilng, a.gen, 3}); continue; } // no way data here: the night look's lattice stays

		auto keep = std::make_shared<BaseShapes::Keep>();
		VECTOR3 u = BaseShapes::Up(latc, lngc);
		double reach = 25e3;
		W.bases.Near(GetPlanet(), hObj, R, u, reach, keep->bases);
		W.fixed.Near(u, R, reach, keep->fixed);
		if (!W.zonesRead) { W.zones = ClearZones::Read(hObj, false); W.zonesRead = true; } // bases never change in a session; BldgMgr logs them
		ClearZones::Near(W.zones, u, R, reach, keep->zones);

		auto in = std::make_shared<WayGeom::Input>();
		in->lat0 = a.lat0; in->lat1 = a.lat1; in->lng0 = a.lng0; in->lng1 = a.lng1;
		in->R = R;
		in->n = WaySize;
		in->nl = LampSize;
		double lngd = lngc * DEG;
		in->americas = lngd >= -170.0 && lngd <= -30.0;
		in->tiles = std::move(tiles);
		if (!keep->Empty()) in->blocked = [keep, R] (double lat, double lng) { return keep->Blocked(BaseShapes::Up(lat, lng), R, 6.0); };

		auto box = W.box;
		WayState::Ask k = a;
		GfxPool::Job j;
		j.owner = W.id;
		j.kind = GfxPool::WAYS;
		j.prio = a.dist;
		j.run = [in, box, k]() {
			auto out = std::make_shared<WayGeom::Output>();
			WayGeom::Raster(*in, *out);
			std::lock_guard<std::mutex> lk(box->mx);
			box->done.push_back({k.lvl, k.ilat, k.ilng, k.gen, out});
		};
		j.dropped = [box, k]() {
			std::lock_guard<std::mutex> lk(box->mx);
			box->done.push_back({k.lvl, k.ilat, k.ilng, k.gen, nullptr});
		};
		GfxPool::Get().Submit(std::move(j));
	}
	W.asked.clear();
}

template<>
void TileManager2<SurfTile>::WayUpload ()
{
	if (!ways) return;
	WayState &W = *ways;
	W.ncand = 0;
	double now = oapiGetSysTime();
	if (now - W.tFixed > 1.0) { // fixed structures made, moved or gone: the rasters within 25 km asked again (design fixed-structures)
		W.tFixed = now;
		std::vector<VECTOR3> mv;
		if (W.fixed.Update(Cbody(), mv) && !mv.empty()) {
			double R = obj_size;
			std::vector<QuadTreeNode<SurfTile>*> st = {&tiletree[0], &tiletree[1]};
			while (!st.empty()) {
				QuadTreeNode<SurfTile> *nd = st.back();
				st.pop_back();
				SurfTile *t = nd->Entry();
				if (!t) continue;
				if (t->lvl + 4 >= WayL0 && t->wayState >= 1) { // a job in flight too: its result comes back under the old gen
					VECTOR3 u = BaseShapes::Up(0.5 * (t->bnd.minlat + t->bnd.maxlat), 0.5 * (t->bnd.minlng + t->bnd.maxlng));
					for (const VECTOR3 &m : mv)
						if (R * acos(std::min(1.0, dotp(u, unit(m)))) < 25e3) {
							t->wayState = 0;
							t->wayAsk = -1e9;
							t->wayGen = WayNewGen();
							break;
						}
				}
				for (int c = 0; c < 4; c++) if (nd->Child(c)) st.push_back(nd->Child(c));
			}
		}
	}
	for (const WayState::Back &b : W.back) {
		SurfTile *t = NodeAt(b.lvl, b.ilat, b.ilng);
		if (t && t->wayGen == b.gen && t->wayState == 1) t->wayState = b.state;
	}
	W.back.clear();
	for (int n = 0; n < UploadsPerFrame; n++) {
		WayState::Result r;
		{
			std::lock_guard<std::mutex> lk(W.box->mx);
			if (W.box->done.empty()) return;
			r = std::move(W.box->done.front());
			W.box->done.pop_front();
		}
		SurfTile *t = NodeAt(r.lvl, r.ilat, r.ilng);
		if (!t || t->wayGen != r.gen) { n--; continue; } // gone, made again, or its fixed structures changed
		if (!r.out) { t->wayState = 0; n--; continue; } // dropped: asked again later
		VkTex *nw = NULL, *nl = NULL;
		if (r.out->any) {
			auto make = [&] (const std::vector<std::vector<uint8_t>> &lv, int size) {
				VkPixels px;
				px.w = px.h = size;
				px.levels = (UINT)lv.size();
				px.layers = 1;
				px.fmt = VK_FORMAT_R8G8B8A8_UNORM;
				for (const auto &l : lv) px.data.emplace_back(l.begin(), l.end());
				return VkCreateTexture(Dev(), px, VK_IMAGE_USAGE_SAMPLED_BIT);
			};
			nw = make(r.out->way, WaySize);
			nl = make(r.out->lamp, LampSize);
			if (!nw || !nl) { delete nw; delete nl; t->wayState = 0; continue; }
		}
		delete t->wtex;
		delete t->wntex;
		t->wtex = nw;
		t->wntex = nl;
		t->wayState = 2;
	}
}
