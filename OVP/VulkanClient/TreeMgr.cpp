// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: 3D trees where the streamed land class says forest or shrub (TreeMgr.h)

#include "TreeMgr.h"
#include "VPlanet.h"
#include "Scene.h"
#include "Surfmgr2.h"
#include "AABBUtil.h"
#include "D3D9Client.h"
#include "D3D9Config.h"
#include "HorizonMap.h"
#include "VkTexFile.h"
#include "SceneShadows.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <list>

namespace {
const int LC = 16;               // the Class level level 0 cells are cut from
const double MaxAgl = 6000.0;    // m above ground: trees drawn below, faded over the last km (design-far-forest: was 3000)
const double KeepAgl = 7000.0;   // design-far-forest: cells kept below
const double FarMax = 120e3;     // design-far-forest: trees to the horizon, at most this far
const double MeshEnd = 350.0, FadeW = 100.0; // meshes to 350 m, impostors from there, cross-faded over 100 m
const int UploadsPerFrame = 16;  // design-far-forest: was 2
const int ChecksPerFrame = 16;   // was 4
const int Workers = 4;           // design-far-forest: was 1
const int AsksPerPass = 512;     // new cells asked each 0.5 s
const double DepthFar = 20e3;    // design-far-forest: trees in the distance pass to here

VECTOR3 Up (double lat, double lng) { return _V(cos (lat) * cos (lng), sin (lat), cos (lat) * sin (lng)); }
VECTOR3 East (double lng) { return _V(-sin (lng), 0.0, cos (lng)); }
VECTOR3 North (double lat, double lng) { return _V(-sin (lat) * cos (lng), cos (lat), -sin (lat) * sin (lng)); }

int ClassL (long long key) { return std::max (12, LC - TreeGeom::Quad::Lvl (key)); } // design-far-forest: a level m cell's Class level

void TileAt (int L, double lat, double lng, int &ilat, int &ilng)
{
	double span = PI / double(1 << (L - 4));
	ilat = std::min ((1 << (L - 4)) - 1, std::max (0, (int)std::floor ((0.5 * PI - lat) / span)));
	ilng = std::min ((2 << (L - 4)) - 1, std::max (0, (int)std::floor ((lng + PI) / span)));
}

// z14 Web Mercator tile of a point, as BldgMgr
void TileOf (double lat, double lng, int &x, int &y)
{
	int n = 1 << 14;
	lat = std::max (-1.4844, std::min (1.4844, lat));
	x = (int)std::floor ((lng / (2.0 * PI) + 0.5) * n);
	y = (int)std::floor ((1.0 - std::asinh (std::tan (lat)) / PI) * 0.5 * n);
	x = std::max (0, std::min (n - 1, x));
	y = std::max (0, std::min (n - 1, y));
}

// a decoded Class or Mask tile, B8G8R8A8
struct Pix { int L = 0, ilat = 0, ilng = 0; UINT w = 0, h = 0; std::vector<BYTE> px; };

struct Foot { double lat, lng, rad; std::vector<std::array<double, 2>> ring; };
}

TreeMgr::TreeMgr (vPlanet *_vp, OBJHANDLE _hObj, const Scene *_scn)
: vp(_vp), hObj(_hObj), scn(_scn)
{
	R = oapiGetSize (hObj);
	char name[64] = "", path[MAX_PATH] = "";
	oapiGetObjectName (hObj, name, 64);
	if (!vp->GetClient()->PlanetTexturePath (name, path)) snprintf (path, MAX_PATH, "Textures\\%s", name); // as the surface manager's DataRootDir
	root = path;
	MakeMeshes ();
}

// before the surface manager and BldgMgr go (vPlanet deletes it first)
TreeMgr::~TreeMgr ()
{
	{
		std::lock_guard<std::mutex> lk (mx);
		quit = true;
	}
	cv.notify_all();
	for (std::thread &w : workers) if (w.joinable()) w.join();
	for (auto &c : cells) delete c.second.buf;
	delete vb;
	delete ib;
	delete decl;
	delete sh;
}

void TreeMgr::MakeMeshes ()
{
	VkDev *dev = vp->GetClient()->GetDevice();
	std::vector<TreeGeom::Vtx> v, mv;
	std::vector<uint16_t> ix, mi;
	for (int k = 0; k <= TreeGeom::NKIND; k++) {
		if (k < TreeGeom::NKIND) TreeGeom::Mesh (k, mv, mi);
		else TreeGeom::Impostor (mv, mi);
		baseV[k] = (int)v.size();
		startI[k] = (int)ix.size();
		countI[k] = (int)mi.size();
		v.insert (v.end(), mv.begin(), mv.end());
		ix.insert (ix.end(), mi.begin(), mi.end());
	}
	vb = new VkBuf (dev, v.size() * sizeof (TreeGeom::Vtx), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false);
	vb->Upload (v.data(), v.size() * sizeof (TreeGeom::Vtx));
	ib = new VkBuf (dev, ix.size() * sizeof (uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, false);
	ib->Upload (ix.data(), ix.size() * sizeof (uint16_t));
	static const VkDeclElement el[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, DECL_POSITION, 0},
		{0, 12, VK_FORMAT_R32G32B32_SFLOAT, DECL_NORMAL, 0},
		{0, 24, VK_FORMAT_R32G32_SFLOAT, DECL_TEXCOORD, 0},
		{1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, DECL_TEXCOORD, 1},
		{1, 16, VK_FORMAT_R8G8B8A8_UNORM, DECL_COLOR, 0},
		{1, 20, VK_FORMAT_R8G8B8A8_UNORM, DECL_TEXCOORD, 2},
		{1, 24, VK_FORMAT_R8G8B8A8_UNORM, DECL_TEXCOORD, 3}};
	decl = new VkVertexDecl (el, 7, sizeof (TreeGeom::Vtx), sizeof (Inst), true);
	std::string fl = std::string(Config->Filmic ? "_FILMIC " : "") + (Config->TAA ? "_TAA " : "") + (Config->SSGI ? "_SSGI " : ""); // not upstream: _FILMIC, _TAA (the dither moves per frame), _SSGI (albedo and normal in the distance pass)
	if (SceneShadows::Enabled ()) fl += "_CSM "; // not upstream: scene shadow receivers (design-scene-shadows)
	sh = new ShaderClass (dev, "Modules/VulkanClient/Tree.glsl", "TreeVS", "TreePS", "Trees", fl.c_str());
}

// a job for the worker; building and road tiles still coming are left out and listed in the cell (design-preload)
bool TreeMgr::Ask (long long key, double dist)
{
	Job j;
	j.key = key;
	j.dist = dist;
	TreeGeom::Quad::Bounds (key, j.lat0, j.lat1, j.lng0, j.lng1);
	int m = TreeGeom::Quad::Lvl (key);
	double latc = 0.5 * (j.lat0 + j.lat1), lngc = 0.5 * (j.lng0 + j.lng1);
	std::vector<std::pair<int, int>> wait;
	if (m <= 1) { // design-far-forest: footprints and roads near only
		int x0, y0, x1, y1;
		TileOf (j.lat1, j.lng0, x0, y0);
		TileOf (j.lat0, j.lng1, x1, y1);
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++) {
				int w = oapiTileStreamBldgWant (hObj, x, y), ww = oapiTileStreamWayWant (hObj, x, y); // ways: design-ways
				if (w > 0) j.bldg.push_back ({x, y});
				if (ww > 0) j.way.push_back ({x, y});
				if (w == 0 || ww == 0) wait.push_back ({x, y}); // design-preload: placed without it for now
			}
	}
	VECTOR3 u = Up (latc, lngc);
	double reach = 25e3 + 0.75 * TreeGeom::Quad::Span (m) * R;
	bases.Near (vp, hObj, R, u, reach, j.keep.bases);
	fixed.Near (u, R, reach, j.keep.fixed);
	if (!zonesRead) { zones = ClearZones::Read (hObj, false); zonesRead = true; } // bases never change in a session; BldgMgr logs them
	ClearZones::Near (zones, u, R, reach, j.keep.zones);
	if (m <= 1 && Config->HorizonShadows && vp->SurfMgr2()) j.hor = vp->SurfMgr2()->HorizonAt (latc, lngc, j.hb);
	if (m >= 1) WantClass (key);
	if (workers.empty()) for (int i = 0; i < Workers; i++) workers.emplace_back (&TreeMgr::Work, this); // first job: bodies never low enough get no thread
	{
		std::lock_guard<std::mutex> lk (mx);
		todo.push_back (std::move (j));
	}
	cv.notify_one();
	Cell &c = cells[key];
	c.pending = true;
	c.stale = false; // this job has the fixed structures as they are now
	c.vecWait.swap (wait);
	return true;
}

// not upstream (design-preload): the cell's Class file is there (design-far-forest: at its level's)
bool TreeMgr::ClassThere (long long key) const
{
	double la0, la1, lo0, lo1;
	TreeGeom::Quad::Bounds (key, la0, la1, lo0, lo1);
	int L = ClassL (key), ilat, ilng;
	TileAt (L, 0.5 * (la0 + la1), 0.5 * (lo0 + lo1), ilat, ilng);
	char path[MAX_PATH];
	snprintf (path, MAX_PATH, "%s\\Class\\%02d\\%06d\\%06d.dds", root.c_str(), L, ilat, ilng);
	std::error_code ec;
	return std::filesystem::exists (oapiResolvePath (path), ec);
}

// not upstream (design-far-forest): a far cell's Class tile, where the terrain draws coarser
void TreeMgr::WantClass (long long key)
{
	double la0, la1, lo0, lo1;
	TreeGeom::Quad::Bounds (key, la0, la1, lo0, lo1);
	int L = ClassL (key), ilat, ilng;
	TileAt (L, 0.5 * (la0 + la1), 0.5 * (lo0 + lo1), ilat, ilng);
	long long tk = (long long)L << 48 | (long long)ilat << 24 | ilng;
	double t = oapiGetSysTime();
	auto it = classAsked.find (tk);
	if (it != classAsked.end() && t - it->second < 2.0) return; // its other cells asked for it just now
	if (classAsked.size() > 4096) classAsked.clear();
	classAsked[tk] = t;
	oapiTileStreamWant (hObj, L, ilat, ilng, 0);
}

// not upstream (design-far-forest): the cells drawn for the wanted ones
void TreeMgr::MakeDrawList (double t)
{
	drawDirty = false;
	std::vector<long long> keys;
	TreeGeom::Quad::DrawSet (want, [this](long long k) { auto it = cells.find (k); return it == cells.end() ? 0 : (it->second.placed ? 2 : 1); }, keys);
	draw.clear();
	for (long long k : keys) {
		auto it = cells.find (k);
		if (it == cells.end()) continue;
		it->second.tSeen = t;
		draw.push_back ({k, &it->second});
	}
}

void TreeMgr::Update ()
{
	bool proxy = scn->GetCameraProxyVisual() == vp;
	double agl = proxy ? scn->GetCameraElevation() : 1e9;
	active = Config->Trees && agl < KeepAgl; // design-far-forest: drawn below MaxAgl, kept a km above it
	if (!active) {
		if (!cells.empty()) {
			std::lock_guard<std::mutex> lk (mx);
			todo.clear();
			ready.clear();
		}
		for (auto &c : cells) delete c.second.buf;
		cells.clear();
		want.clear();
		draw.clear();
		return;
	}
	double a = std::clamp ((MaxAgl - agl) / 1000.0, 0.0, 1.0);
	aglKeep = a * a * (3.0 - 2.0 * a);
	VECTOR3 cam = vp->CameraPos();
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 loc = tmul (rot, cam);
	double r = length (loc);
	camLat = asin (loc.y / r);
	camLng = atan2 (loc.z, loc.x);
	double D0 = Config->TreeRange, t = oapiGetSysTime(), heff = 0.5 * std::max (0.0, agl);
	farLim = std::min (FarMax, sqrt (2.0 * R * std::max (0.0, agl)) + sqrt (2.0 * R * 2000.0)); // design-far-forest: the horizon and 2 km of terrain beyond

	std::deque<Result> got;
	{
		std::lock_guard<std::mutex> lk (mx);
		for (int n = 0; n < UploadsPerFrame && !ready.empty(); n++) {
			got.push_back (std::move (ready.front()));
			ready.pop_front();
		}
	}
	VkDev *dev = vp->GetClient()->GetDevice();
	for (Result &res : got) {
		auto it = cells.find (res.key);
		if (it == cells.end()) continue; // dropped meanwhile
		Cell &c = it->second;
		delete c.buf;
		c.buf = nullptr;
		if (!res.inst.empty()) {
			c.buf = new VkBuf (dev, res.inst.size() * sizeof (Inst), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false);
			c.buf->Upload (res.inst.data(), res.inst.size() * sizeof (Inst));
		}
		memcpy (c.first, res.first, sizeof c.first);
		c.p0 = res.p0; c.e0 = res.e0; c.u0 = res.u0; c.n0 = res.n0;
		memcpy (c.gc, res.gc, sizeof c.gc);
		c.hasHor = res.hasHor;
		if (!res.classMiss) c.tClass0 = 0.0;
		else if (c.tClass0 == 0.0) c.tClass0 = t;
		c.classWait = res.classMiss && t - c.tClass0 < 60.0; // design-preload: a Class tile still coming, looked for every 2 s for a minute
		c.complete = res.complete;
		c.pending = false;
		c.rad = res.rad;
		c.tSun = -1e9;
		if (!c.placed) drawDirty = true;
		c.placed = true;
		c.tCheck = t + (TreeGeom::Quad::Lvl (res.key) <= 1 ? 2.0 : 20.0);
	}

	bool sel = t >= tWant;
	if (sel) {
		tWant = t + 0.5;
		std::vector<VECTOR3> mv;
		if (fixed.Update (hObj, mv)) // fixed structures made, moved or gone: the cells within 25 km are made again (design fixed-structures)
			for (auto &kv : cells) {
				double la0, la1, lo0, lo1;
				TreeGeom::Quad::Bounds (kv.first, la0, la1, lo0, lo1);
				VECTOR3 uc = Up (0.5 * (la0 + la1), 0.5 * (lo0 + lo1));
				double reach = 25e3 + 0.75 * TreeGeom::Quad::Span (TreeGeom::Quad::Lvl (kv.first)) * R;
				for (const VECTOR3 &m : mv) {
					double l = length (m);
					if (l > 0 && R * acos (std::min (1.0, dotp (m / l, uc))) <= reach) { kv.second.stale = true; kv.second.tCheck = 0.0; break; }
				}
			}
		std::vector<TreeGeom::Quad::Want> ws; // design-far-forest: the quadtree's leaves to the far limit, nearest first
		TreeGeom::Quad::Select (camLat, camLng, heff, D0, farLim, R, ws);
		want.clear();
		int asked = 0;
		for (const auto &w : ws) {
			want.push_back (w.key);
			auto it = cells.find (w.key);
			if (it == cells.end()) {
				if (asked >= AsksPerPass) continue; // the rest at the next pass: no long frame on arrival
				asked++;
				Ask (w.key, w.d);
				it = cells.find (w.key);
			}
			it->second.tSeen = t;
		}
		drawDirty = true;
	}
	if (drawDirty) MakeDrawList (t);
	if (sel) { // cells neither wanted nor drawn for 10 s go, with their jobs
		std::set<long long> gone;
		for (auto it = cells.begin(); it != cells.end(); ) {
			if (t - it->second.tSeen > 10.0) {
				gone.insert (it->first);
				delete it->second.buf;
				it = cells.erase (it);
			} else ++it;
		}
		if (!gone.empty()) {
			std::lock_guard<std::mutex> lk (mx);
			todo.erase (std::remove_if (todo.begin(), todo.end(), [&](const Job &j) { return gone.count (j.key) > 0; }), todo.end());
		}
	}

	// the terrain under them refines, horizon maps arrive: a cell is made again
	int n = 0;
	for (size_t s = 0; s < want.size() && s < 512 && n < ChecksPerFrame; s++) {
		if (chk >= want.size()) chk = 0;
		long long key = want[chk++];
		auto it = cells.find (key);
		if (it == cells.end()) continue;
		Cell &c = it->second;
		if (c.pending || t < c.tCheck) continue;
		n++;
		int m = TreeGeom::Quad::Lvl (key);
		c.tCheck = t + (m <= 1 ? 2.0 : 20.0); // design-far-forest: far cells every 20 s
		double la0, la1, lo0, lo1;
		TreeGeom::Quad::Bounds (key, la0, la1, lo0, lo1);
		double lat[5] = {0.5 * (la0 + la1), la0 + 0.1 * (la1 - la0), la1 - 0.1 * (la1 - la0), la0 + 0.1 * (la1 - la0), la1 - 0.1 * (la1 - la0)};
		double lng[5] = {0.5 * (lo0 + lo1), lo0 + 0.1 * (lo1 - lo0), lo0 + 0.1 * (lo1 - lo0), lo1 - 0.1 * (lo1 - lo0), lo1 - 0.1 * (lo1 - lo0)};
		double g[5];
		int rc[5];
		vp->SurfMgr2()->GetLoadedElevations (5, lng, lat, g, rc);
		bool redo = !c.complete || c.stale;
		for (size_t i = 0; i < c.vecWait.size() && !redo; i++) // design-preload: a building or road tile it was placed without has answered
			redo = oapiTileStreamBldgWant (hObj, c.vecWait[i].first, c.vecWait[i].second) != 0 && oapiTileStreamWayWant (hObj, c.vecWait[i].first, c.vecWait[i].second) != 0;
		if (!redo && c.classWait) {
			if (t - c.tClass0 >= 60.0) c.classWait = false;
			else if (!(redo = ClassThere (key)) && m >= 1) WantClass (key);
		}
		for (int i = 0; i < 5 && !redo; i++) if (rc[i] == 1 && std::fabs (g[i] - c.gc[i]) > (m == 0 ? 0.3 : 2.0)) redo = true;
		if (!redo && m <= 1 && !c.hasHor && Config->HorizonShadows) {
			double hb[4];
			redo = vp->SurfMgr2()->HorizonAt (lat[0], lng[0], hb) != nullptr;
		}
		if (redo) Ask (key, std::hypot (TreeGeom::Quad::Near (key, camLat, camLng, R), heff));
	}
}

// worker: class and mask tiles, ground, footprints, placement; the core's building load is the one BldgMgr's worker makes
void TreeMgr::Work ()
{
	struct Cache { std::string layer; int L, ilat, ilng; std::shared_ptr<Pix> p; double t; };
	std::list<Cache> cache;
	auto Now = [] { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); };
	auto Tile = [&](const std::string &layer, int L, int ilat, int ilng) -> std::shared_ptr<Pix> {
		for (auto it = cache.begin(); it != cache.end(); ++it)
			if (it->layer == layer && it->L == L && it->ilat == ilat && it->ilng == ilng) {
				if (!it->p && Now() - it->t > 1.0) { cache.erase (it); break; } // a miss is looked for again: the stream may have written it (design-preload: 1 s, was 8)
				cache.splice (cache.begin(), cache, it);
				return cache.front().p;
			}
		std::shared_ptr<Pix> p;
		char path[MAX_PATH];
		snprintf (path, MAX_PATH, "%s\\%s\\%02d\\%06d\\%06d.dds", root.c_str(), layer.c_str(), L, ilat, ilng);
		std::string full = oapiResolvePath (path);
		VkPixels in, out;
		if (std::filesystem::exists (full) && VkLoadPixels (full.c_str(), in) && VkConvertPixels (in, out, VK_FORMAT_B8G8R8A8_UNORM, SWZ_NONE, 0, 0, 1) && !out.data.empty()) {
			p = std::make_shared<Pix>();
			p->L = L; p->ilat = ilat; p->ilng = ilng; p->w = out.w; p->h = out.h;
			p->px.swap (out.Level (0));
		}
		cache.push_front ({layer, L, ilat, ilng, p, Now()});
		if (cache.size() > 16) cache.pop_back();
		return p;
	};
	bool miss = false; // this job read a coarser Class tile than its level's, or none
	int lc = LC;       // design-far-forest: the job's Class level, L(16 - m) and at least 12
	auto Texel = [&](const std::string &layer, double lat, double lng, BYTE *bgra) -> bool {
		for (int L = lc; L >= std::max (12, lc - 2); L--) {
			double span = PI / double(1 << (L - 4));
			int ilat, ilng;
			TileAt (L, lat, lng, ilat, ilng);
			std::shared_ptr<Pix> p = Tile (layer, L, ilat, ilng);
			if (!p) continue;
			if (L < lc && layer == "Class") miss = true;
			double u = ((lng + PI) - ilng * span) / span, v = ((0.5 * PI - lat) - ilat * span) / span;
			UINT x = std::min (p->w - 1, (UINT)std::max (0.0, u * p->w)), y = std::min (p->h - 1, (UINT)std::max (0.0, v * p->h));
			memcpy (bgra, &p->px[(size_t(y) * p->w + x) * 4], 4);
			return true;
		}
		if (layer == "Class") miss = true;
		return false;
	};
	// a z14 building tile's footprints, the last 4 tiles kept: one tile covers about 64 cells
	struct BCache { int x, y; std::shared_ptr<const std::vector<Foot>> f; };
	std::list<BCache> bcache;
	auto Feet = [&](int tx, int ty) -> std::shared_ptr<const std::vector<Foot>> {
		for (auto it = bcache.begin(); it != bcache.end(); ++it)
			if (it->x == tx && it->y == ty) {
				bcache.splice (bcache.begin(), bcache, it);
				return bcache.front().f;
			}
		TSBLDGMESH *m = nullptr;
		try { m = oapiTileStreamBldgLoad (hObj, tx, ty); }
		catch (...) { m = nullptr; }
		if (!m) return nullptr; // not kept: it may load next time
		auto f = std::make_shared<std::vector<Foot>>();
		std::vector<std::vector<std::array<double, 2>>> rings;
		for (DWORD b = 0; b < m->nb; b++) {
			const TSBLDG &B = m->b[b];
			BldgGeom::Rings (m->vtx + (size_t)B.v0 * 6, B.nv, rings);
			if (rings.empty() || rings[0].size() < 3) continue;
			Foot q {B.lat, B.lng, 0.0, rings[0]};
			for (const auto &v : q.ring) q.rad = std::max (q.rad, std::hypot (v[0], v[1]));
			f->push_back (std::move (q));
		}
		oapiTileStreamBldgFree (m);
		bcache.push_front ({tx, ty, f});
		if (bcache.size() > 4) bcache.pop_back();
		return f;
	};
	TileManager2<SurfTile> *sm = vp->SurfMgr2();

	std::unique_lock<std::mutex> lk (mx);
	for (;;) {
		cv.wait (lk, [this] { return quit || !todo.empty(); });
		if (quit) return;
		auto best = std::min_element (todo.begin(), todo.end(), [](const Job &a, const Job &b) { return a.dist < b.dist; }); // design-far-forest: the nearest first
		Job j = std::move (*best);
		if (best != todo.end() - 1) *best = std::move (todo.back());
		todo.pop_back();
		lk.unlock();

		Result res;
		res.key = j.key;
		miss = false;
		int m = TreeGeom::Quad::Lvl (j.key);
		lc = std::max (12, LC - m);
		// ground: a 17 x 17 grid over the cell, holding no lock of ours
		const int G = 17;
		std::vector<double> glat (G * G), glng (G * G);
		std::vector<int> grc (G * G);
		TreeGeom::Ground grd;
		grd.n = G; grd.R = R;
		grd.lat0 = j.lat0; grd.lat1 = j.lat1; grd.lng0 = j.lng0; grd.lng1 = j.lng1;
		grd.h.assign (G * G, 0.0);
		for (int y = 0; y < G; y++)
			for (int x = 0; x < G; x++) {
				glat[y * G + x] = j.lat0 + (j.lat1 - j.lat0) * y / (G - 1);
				glng[y * G + x] = j.lng0 + (j.lng1 - j.lng0) * x / (G - 1);
			}
		if (sm) sm->GetLoadedElevations (G * G, glng.data(), glat.data(), grd.h.data(), grc.data());
		for (int i = 0; i < G * G; i++) if (m == 0 ? grc[i] != 1 : grc[i] < 1) res.complete = false; // design-far-forest: far, any loaded tile
		auto Ground = [&](double lat, double lng, double *slope) { return grd.At (lat, lng, slope); };

		// footprints of the buildings round the cell
		std::vector<const Foot *> feet;
		std::vector<std::shared_ptr<const std::vector<Foot>>> held;
		double margin = 300.0 / R, mlng = margin / std::max (0.05, cos (0.5 * (j.lat0 + j.lat1)));
		for (const auto &xy : j.bldg) {
			auto f = Feet (xy.first, xy.second);
			if (!f) continue;
			held.push_back (f);
			for (const Foot &q : *f)
				if (q.lat >= j.lat0 - margin && q.lat <= j.lat1 + margin && q.lng >= j.lng0 - mlng && q.lng <= j.lng1 + mlng) feet.push_back (&q);
		}

		// not upstream (design-ways): the ways round the cell
		std::vector<std::shared_ptr<const WayGeom::Tile>> wt;
		for (const auto &xy : j.way) {
			TSWAYTILE *t = oapiTileStreamWayLoad (hObj, xy.first, xy.second);
			if (!t) continue;
			wt.push_back (WayGeom::FromApi (*t));
			oapiTileStreamWayFree (t);
		}
		WayGeom::Index ways;
		ways.Build (wt, j.lat0 - margin, j.lat1 + margin, j.lng0 - mlng, j.lng1 + mlng, R);

		auto Blocked = [&](double lat, double lng) {
			double s;
			Ground (lat, lng, &s);
			if (s > tan (40.0 * RAD)) return true;
			for (const Foot *f : feet) {
				double dx = (lng - f->lng) * R * cos (f->lat), dz = (lat - f->lat) * R;
				if (std::fabs (dx) > f->rad + 2.0 || std::fabs (dz) > f->rad + 2.0) continue;
				if (TreeGeom::InRing (f->ring, dx, dz, 2.0)) return true;
			}
			if (ways.Covers (lat, lng, 2.0)) return true; // not upstream (design-ways): a road, rail, runway or river
			return j.keep.Blocked (Up (lat, lng), R, 6.0); // bases, fixed structures, clear zones
		};
		auto Cls = [&](double lat, double lng, TreeGeom::Class *c) {
			BYTE cl[4], mk[4];
			if (!Texel ("Class", lat, lng, cl)) return false;
			c->b = cl[0] / 255.0f; c->g = cl[1] / 255.0f; c->r = cl[2] / 255.0f;
			c->water = Texel ("Mask", lat, lng, mk) && mk[3] < 128;
			return true;
		};
		TreeGeom::Params P;
		P.R = R;
		P.density = Config->TreeDensity;
		std::vector<TreeGeom::Tree> trees;
		TreeGeom::Place (j.lat0, j.lat1, j.lng0, j.lng1, P, Cls, [&](double la, double lo) { return Ground (la, lo, nullptr); }, Blocked, trees, m);

		double latc = 0.5 * (j.lat0 + j.lat1), lngc = 0.5 * (j.lng0 + j.lng1);
		double gc = Ground (latc, lngc, nullptr);
		res.u0 = Up (latc, lngc); res.e0 = East (lngc); res.n0 = North (latc, lngc);
		res.p0 = res.u0 * (R + gc);
		double pl[5] = {latc, j.lat0 + 0.1 * (j.lat1 - j.lat0), j.lat1 - 0.1 * (j.lat1 - j.lat0), j.lat0 + 0.1 * (j.lat1 - j.lat0), j.lat1 - 0.1 * (j.lat1 - j.lat0)};
		double pg[5] = {lngc, j.lng0 + 0.1 * (j.lng1 - j.lng0), j.lng0 + 0.1 * (j.lng1 - j.lng0), j.lng1 - 0.1 * (j.lng1 - j.lng0), j.lng1 - 0.1 * (j.lng1 - j.lng0)};
		int prc[5] = {0, 0, 0, 0, 0};
		if (sm) sm->GetLoadedElevations (5, pg, pl, res.gc, prc); // raw, as the re-ground check reads them
		for (int i = 0; i < 5; i++) if (prc[i] != 1) res.gc[i] = Ground (pl[i], pg[i], nullptr);
		res.hasHor = j.hor != nullptr;
		res.classMiss = miss;
		std::stable_sort (trees.begin(), trees.end(), [](const TreeGeom::Tree &a, const TreeGeom::Tree &b) { return a.kind < b.kind; });
		res.inst.reserve (trees.size());
		for (const TreeGeom::Tree &tr : trees) {
			VECTOR3 p = Up (tr.lat, tr.lng) * (R + Ground (tr.lat, tr.lng, nullptr) - 0.5) - res.p0;
			Inst in;
			in.x = (float)dotp (p, res.e0); in.y = (float)dotp (p, res.u0); in.z = (float)dotp (p, res.n0);
			in.s = tr.h;
			in.yaw = uint8_t (tr.yaw / (2.0 * PI) * 255.0);
			in.kind = uint8_t (tr.kind | tr.lvl << 4); // design-far-forest: the level in the high bits
			in.t0 = tr.tint0; in.t1 = tr.tint1;
			memset (in.h, 0, sizeof in.h); // sin -0.25: never shadowed
			if (j.hor) {
				double u = (tr.lng - j.hb[2]) / (j.hb[3] - j.hb[2]), v = (j.hb[1] - tr.lat) / (j.hb[1] - j.hb[0]);
				if (u >= 0 && u <= 1 && v >= 0 && v <= 1) Horizon::Sample (*j.hor, u, v, in.h);
			}
			res.inst.push_back (in);
			res.rad = std::max (res.rad, length (p) + tr.h);
		}
		for (int k = 0, i = 0; k <= TreeGeom::NKIND; k++) {
			while (i < (int)trees.size() && trees[i].kind < k) i++;
			res.first[k] = (UINT)i;
		}

		lk.lock();
		ready.push_back (std::move (res));
	}
}

// meshes near, impostors far; per cell its sun, haze and horizon weights; depth: the SceneFx distance pass
void TreeMgr::DrawCells (VkDev *dev, int depth, const D3DXMATRIX *mVPd)
{
	if (draw.empty() || !sh || !decl) return;
	struct {
		D3DXMATRIX mW, mVP;
		D3DXVECTOR4 cam, band, sun, hw0, hw1, col, amb, trans, insc, lod, lod2; // lod, lod2: design-far-forest
	} prm;
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 cam = vp->CameraPos();
	D3DXVECTOR4 F = D9LinearFieldOfView (scn->GetProjectionMatrix());
	prm.mVP = mVPd ? *mVPd : *scn->GetProjectionViewMatrix();
	ConstParams *cp = vp->GetScatterConst();
	float expo = cp && cp->Expo > 0.0f ? cp->Expo : 1.0f;
	double D0 = Config->TreeRange, fpx = std::max (1.0, 0.5 * scn->ViewH() * scn->GetProjectionMatrix()->_22), t = oapiGetSysTime();
	bool keepSun = !depth && scn->GetRenderPass() == RENDERPASS_MAINSCENE; // design-far-forest: the atmosphere kept per cell in the main pass
	float time = (float)fmod (oapiGetSimTime(), 3600.0);
	prm.band = D3DXVECTOR4 ((float)MeshEnd, (float)MeshEnd, (float)FadeW, (float)farLim);
	prm.lod = D3DXVECTOR4 ((float)D0, (float)farLim, (float)(0.1 * farLim), (float)fpx);
	sh->Setup (decl, true, 0);
	if (SceneShadows *ss = SceneShadows::Get ()) { // not upstream: the cascades in the main colour pass of the proxy, off in every other draw (design-scene-shadows Rev 2)
		bool on = !depth && ss->ReceiversOn (vp);
		sh->SetPSConstants ("gCsm", (void *)ss->Block (on), sizeof (CsmGpu));
		if (on) { sh->SetTexture ("tCsm", ss->Atlas (), IPF_CLAMP | IPF_POINT); sh->UpdateTextures (); }
	}
	dev->SetCullMode (VK_CULL_MODE_NONE);
	dev->SetStreamSource (0, vb, 0, sizeof (TreeGeom::Vtx));
	dev->SetIndices (ib);
	struct Vis { Cell *c; double dc, cr; VECTOR3 g; };
	std::vector<Vis> vis; // design-far-forest: near to far, so near crowns hide far ones before they are shaded
	vis.reserve (draw.size());
	double reach = depth ? std::min (farLim, DepthFar) : farLim;
	for (auto &kc : draw) {
		Cell &c = *kc.second;
		if (!c.buf) continue;
		VECTOR3 g = mul (rot, c.p0), o = g - cam;
		double dc = length (o);
		double cr = c.rad + 30.0 * std::max (1.0, (dc + c.rad) / D0) + (dc + c.rad) / fpx; // design-far-forest: the relief, the widening and the 1 px height
		if (dc - cr > reach) continue;
		D9BBox bb;
		memset (&bb, 0, sizeof bb);
		bb.bs = D3DXVECTOR4 ((float)o.x, (float)o.y, (float)o.z, (float)cr); // the sphere in camera-relative world: only the view turns it
		if (D9IsBSVisible (&bb, scn->GetViewMatrix(), &F)) vis.push_back ({&c, dc, cr, g});
	}
	std::sort (vis.begin(), vis.end(), [](const Vis &a, const Vis &b) { return a.dc < b.dc; });
	for (const Vis &q : vis) {
		Cell &c = *q.c;
		VECTOR3 e = mul (rot, c.e0), u = mul (rot, c.u0), n = mul (rot, c.n0), g = q.g, o = g - cam;
		double dc = q.dc, cr = q.cr;
		D3DXMATRIX &W = prm.mW;
		D3DXMatrixIdentity (&W);
		W._11 = (float)e.x; W._12 = (float)e.y; W._13 = (float)e.z;
		W._21 = (float)u.x; W._22 = (float)u.y; W._23 = (float)u.z;
		W._31 = (float)n.x; W._32 = (float)n.y; W._33 = (float)n.z;
		W._41 = (float)o.x; W._42 = (float)o.y; W._43 = (float)o.z;
		prm.cam = D3DXVECTOR4 ((float)-dotp (o, e), (float)-dotp (o, u), (float)-dotp (o, n), time);
		D3DXVECTOR4 sc (0, 1, 0, 0);
		prm.amb = D3DXVECTOR4 (0, 0, 0, depth ? 1.0f : 0.0f);
		prm.col = prm.trans = D3DXVECTOR4 (1, 1, 1, expo);
		prm.insc = D3DXVECTOR4 (0, 0, 0, 0);
		prm.lod2 = D3DXVECTOR4 ((float)aglKeep, 0.0f, 0.0f, 0.0f);
		if (!depth) {
			if (!keepSun || t - c.tSun > 2.0 || length (o - c.sunAt) > 0.02 * dc + 10.0) { // design-far-forest: again when the camera moved 2 % of the way or after 2 s
				c.sun = vp->GetObjectAtmoParams (g);
				c.sunAt = o;
				c.sunD = dc;
				c.tSun = keepSun ? t : -1e9;
			}
			const D3D9Sun &s = c.sun;
			prm.lod2.y = (float)c.sunD;
			VECTOR3 sd = _V(-s.Dir.x, -s.Dir.y, -s.Dir.z);
			sc = D3DXVECTOR4 ((float)dotp (sd, e), (float)dotp (sd, u), (float)dotp (sd, n), 0.0f);
			prm.col = D3DXVECTOR4 (s.Color.x, s.Color.y, s.Color.z, expo);
			prm.amb = D3DXVECTOR4 (s.Ambient.x, s.Ambient.y, s.Ambient.z, 0.0f);
			prm.trans = D3DXVECTOR4 (s.Transmission.x, s.Transmission.y, s.Transmission.z, 0.0f);
			prm.insc = D3DXVECTOR4 (s.Incatter.x, s.Incatter.y, s.Incatter.z, 0.0f);
		}
		if (scn->IsJittered()) prm.insc.w = float(scn->GetFrameId() % 8); // not upstream: the TAA dither phase while TAA runs, the same in the distance pass
		float w[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		double az = atan2 (sc.z, sc.x) / (PI / 4.0); // east 0, counter-clockwise, as the horizon maps
		if (az < 0.0) az += 8.0;
		int k0 = (int)std::floor (az) & 7;
		float f = (float)(az - std::floor (az));
		w[k0] = 1.0f - f;
		w[(k0 + 1) & 7] = f;
		prm.hw0 = D3DXVECTOR4 (w[0], w[1], w[2], w[3]);
		prm.hw1 = D3DXVECTOR4 (w[4], w[5], w[6], w[7]);
		dev->SetStreamSource (1, c.buf, 0, sizeof (Inst));
		if (dc - cr < MeshEnd + FadeW) { // meshes
			prm.sun = D3DXVECTOR4 (sc.x, sc.y, sc.z, 0.0f);
			sh->SetVSConstants ("Prm", &prm, sizeof prm);
			sh->SetPSConstants ("Prm", &prm, sizeof prm);
			for (int k = 0; k < TreeGeom::NKIND; k++) {
				UINT n = c.first[k + 1] - c.first[k];
				if (n) dev->DrawIndexedPrimitiveInstanced (VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, baseV[k], startI[k], countI[k], n, c.first[k]);
			}
		}
		if (dc + cr > MeshEnd) { // impostors
			prm.sun = D3DXVECTOR4 (sc.x, sc.y, sc.z, 1.0f);
			sh->SetVSConstants ("Prm", &prm, sizeof prm);
			sh->SetPSConstants ("Prm", &prm, sizeof prm);
			int k = TreeGeom::NKIND;
			UINT n = c.first[TreeGeom::NKIND];
			if (n) dev->DrawIndexedPrimitiveInstanced (VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, baseV[k], startI[k], countI[k], n, 0);
		}
	}
	dev->SetCullMode (VK_CULL_MODE_BACK_BIT);
}

void TreeMgr::Render (VkDev *dev)
{
	DWORD pass = scn->GetRenderPass();
	if (!active || aglKeep <= 0.0 || !(pass == RENDERPASS_MAINSCENE || pass == RENDERPASS_CUSTOMCAM)) return; // aglKeep: design-far-forest
	DrawCells (dev, 0, nullptr);
}

void TreeMgr::RenderDepth (VkDev *dev, const D3DXMATRIX &mVP)
{
	if (!active || aglKeep <= 0.0) return;
	DrawCells (dev, 1, &mVP);
}

// scene shadow casters: the cells in the cascade's light box, meshes near the camera (C0 to 350 m, C1 to 150 m), sun-facing cards beyond, C2 cards only (design-scene-shadows 5, 7.3)
void TreeMgr::RenderCasters (VkDev *dev, const SceneShadows &s)
{
	ShaderClass *shc = s.TreeShader ();
	int ci = s.Cascade ();
	if (!active || draw.empty () || !shc || !decl || ci < 0 || ci > 2) return;
	static const double thr[3] = { MeshEnd, 150.0, 0.0 }, spread[3] = { FadeW, 50.0, 0.0 };
	struct { D3DXMATRIX mW, mVP; D3DXVECTOR4 cam, band, sun; } prm;
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 cam = vp->CameraPos();
	prm.mVP = *s.CasterVP ();
	double range = Config->TreeRange;
	float time = (float)fmod (oapiGetSimTime(), 3600.0);
	prm.band = D3DXVECTOR4 ((float)thr[ci], (float)spread[ci], (float)range, 0.0f);
	VECTOR3 ls = _V(s.ToSun ().x, s.ToSun ().y, s.ToSun ().z);
	shc->Setup (decl, true, 0);
	dev->SetCullMode (VK_CULL_MODE_NONE);
	dev->SetStreamSource (0, vb, 0, sizeof (TreeGeom::Vtx));
	dev->SetIndices (ib);
	for (auto &kc : draw) {
		Cell &c = *kc.second;
		if (!c.buf || TreeGeom::Quad::Lvl (kc.first) > 0) continue; // design-far-forest: level 0 within TreeRange as before
		VECTOR3 e = mul (rot, c.e0), u = mul (rot, c.u0), n = mul (rot, c.n0), o = mul (rot, c.p0) - cam;
		double dc = length (o), cr = c.rad + 40.0;
		if (dc - cr > range) continue;
		VECTOR3 bc = o + u * 15.0;
		if (!s.Cull (D3DXVECTOR3 ((float)bc.x, (float)bc.y, (float)bc.z), (float)cr)) continue;
		D3DXMATRIX &W = prm.mW;
		D3DXMatrixIdentity (&W);
		W._11 = (float)e.x; W._12 = (float)e.y; W._13 = (float)e.z;
		W._21 = (float)u.x; W._22 = (float)u.y; W._23 = (float)u.z;
		W._31 = (float)n.x; W._32 = (float)n.y; W._33 = (float)n.z;
		W._41 = (float)o.x; W._42 = (float)o.y; W._43 = (float)o.z;
		prm.cam = D3DXVECTOR4 ((float)-dotp (o, e), (float)-dotp (o, u), (float)-dotp (o, n), time);
		prm.sun = D3DXVECTOR4 ((float)dotp (ls, e), (float)dotp (ls, u), (float)dotp (ls, n), 0.0f);
		dev->SetStreamSource (1, c.buf, 0, sizeof (Inst));
		if (thr[ci] > 0.0 && dc - cr < thr[ci] + spread[ci]) { // meshes
			prm.sun.w = 0.0f;
			shc->SetVSConstants ("Prm", &prm, sizeof prm);
			shc->SetPSConstants ("Prm", &prm, sizeof prm);
			for (int k = 0; k < TreeGeom::NKIND; k++) {
				UINT nk = c.first[k + 1] - c.first[k];
				if (nk) dev->DrawIndexedPrimitiveInstanced (VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, baseV[k], startI[k], countI[k], nk, c.first[k]);
			}
		}
		if (dc + cr > thr[ci]) { // cards
			prm.sun.w = 1.0f;
			shc->SetVSConstants ("Prm", &prm, sizeof prm);
			shc->SetPSConstants ("Prm", &prm, sizeof prm);
			int k = TreeGeom::NKIND;
			UINT nk = c.first[TreeGeom::NKIND];
			if (nk) dev->DrawIndexedPrimitiveInstanced (VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, baseV[k], startI[k], countI[k], nk, 0);
		}
	}
}
