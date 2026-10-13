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
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <list>

namespace {
const int LC = 16;               // the Class level the cells are cut from
const int NCELL = 16;            // cells a side of an L16 tile, ~300 m
const double MaxAgl = 3000.0;    // m above ground: trees below
const double MeshEnd = 350.0, FadeW = 100.0; // meshes to 350 m, impostors from there, cross-faded over 100 m
const int UploadsPerFrame = 2;
const int ChecksPerFrame = 4;

VECTOR3 Up (double lat, double lng) { return _V(cos (lat) * cos (lng), sin (lat), cos (lat) * sin (lng)); }
VECTOR3 East (double lng) { return _V(-sin (lng), 0.0, cos (lng)); }
VECTOR3 North (double lat, double lng) { return _V(-sin (lat) * cos (lng), cos (lat), -sin (lat) * sin (lng)); }

double CellSpan () { return PI / double(1 << (LC - 4)) / NCELL; }

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
	if (worker.joinable()) worker.join();
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
	sh = new ShaderClass (dev, "Modules/VulkanClient/Tree.glsl", "TreeVS", "TreePS", "Trees", Config->Filmic ? "_FILMIC " : ""); // not upstream: _FILMIC
}

// cell key: L16 tile row and column, cell column and row inside it
void TreeMgr::Bounds (long long key, double &lat0, double &lat1, double &lng0, double &lng1) const
{
	int ilat = int(key >> 32), ilng = int((key >> 8) & 0xffffff), j = int((key >> 4) & 15), i = int(key & 15);
	double cs = CellSpan ();
	lat1 = 0.5 * PI - (double(ilat) * NCELL + j) * cs;
	lat0 = lat1 - cs;
	lng0 = -PI + (double(ilng) * NCELL + i) * cs;
	lng1 = lng0 + cs;
}

void TreeMgr::Cells (double lat, double lng, double range, std::vector<long long> &out) const
{
	out.clear();
	double cs = CellSpan (), dlat = range / R, dlng = range / (R * std::max (0.05, cos (lat)));
	long long ny = (1LL << (LC - 4)) * NCELL, nx = 2 * ny;
	long long y0 = (long long)std::floor ((0.5 * PI - lat - dlat) / cs), y1 = (long long)std::floor ((0.5 * PI - lat + dlat) / cs);
	long long x0 = (long long)std::floor ((lng + PI - dlng) / cs), x1 = (long long)std::floor ((lng + PI + dlng) / cs);
	VECTOR3 u = Up (lat, lng);
	for (long long y = std::max (0LL, y0); y <= std::min (ny - 1, y1); y++)
		for (long long xx = x0; xx <= x1; xx++) {
			long long x = ((xx % nx) + nx) % nx;
			long long key = ((y / NCELL) << 32) | ((x / NCELL) << 8) | ((y % NCELL) << 4) | (x % NCELL);
			double la0, la1, lo0, lo1;
			Bounds (key, la0, la1, lo0, lo1);
			if (R * acos (std::min (1.0, dotp (Up (0.5 * (la0 + la1), 0.5 * (lo0 + lo1)), u))) <= range) out.push_back (key);
		}
}

// a base's structures, every triangle, once
const std::shared_ptr<const TreeMgr::Base> &TreeMgr::BaseOf (OBJHANDLE hBase)
{
	auto it = bases.find (hBase);
	if (it != bases.end()) return it->second;
	auto b = std::make_shared<Base>();
	VECTOR3 gp, gb;
	oapiGetRotationMatrix (hObj, &b->rp);
	oapiGetRotationMatrix (hBase, &b->rb);
	oapiGetGlobalPos (hObj, &gp);
	oapiGetGlobalPos (hBase, &gb);
	b->p = tmul (b->rp, gb - gp);
	MESHHANDLE *sbs = nullptr, *sas = nullptr;
	DWORD nbs = 0, nas = 0;
	vp->GetClient()->GetBaseStructures (hBase, &sbs, &nbs, &sas, &nas);
	for (int pass = 0; pass < 2; pass++)
		for (DWORD k = 0; k < (pass ? nas : nbs); k++) {
			MESHHANDLE hm = pass ? sas[k] : sbs[k];
			for (DWORD g = 0; hm && g < oapiMeshGroupCount (hm); g++) {
				const MESHGROUP *G = oapiMeshGroup (hm, g);
				if (!G || !G->Vtx || !G->Idx) continue;
				for (DWORD i = 0; i + 2 < G->nIdx; i += 3) {
					double v[3][3];
					bool ok = true;
					for (int c = 0; c < 3; c++) {
						WORD n = G->Idx[i + c];
						if (n >= G->nVtx) { ok = false; break; }
						v[c][0] = G->Vtx[n].x; v[c][1] = G->Vtx[n].y; v[c][2] = G->Vtx[n].z;
					}
					if (ok) b->idx.Add (v[0], v[1], v[2]);
				}
			}
		}
	return bases[hBase] = b;
}

// a job for the worker: false while a building tile it needs is still coming
bool TreeMgr::Ask (long long key)
{
	Job j;
	j.key = key;
	Bounds (key, j.lat0, j.lat1, j.lng0, j.lng1);
	double latc = 0.5 * (j.lat0 + j.lat1), lngc = 0.5 * (j.lng0 + j.lng1);
	int x0, y0, x1, y1;
	TileOf (j.lat1, j.lng0, x0, y0);
	TileOf (j.lat0, j.lng1, x1, y1);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			int w = oapiTileStreamBldgWant (hObj, x, y);
			if (w == 0) return false; // wanted, not there yet
			if (w > 0) j.bldg.push_back ({x, y});
		}
	VECTOR3 u = Up (latc, lngc);
	for (DWORD i = 0, nb = oapiGetBaseCount (hObj); i < nb; i++) {
		OBJHANDLE hb = oapiGetBaseByIndex (hObj, i);
		double lng, lat, rad;
		oapiGetBaseEquPos (hb, &lng, &lat, &rad);
		if (R * acos (std::min (1.0, dotp (Up (lat, lng), u))) > 25e3) continue;
		const std::shared_ptr<const Base> &b = BaseOf (hb);
		if (!b->idx.Empty()) j.bases.push_back (b);
	}
	if (Config->HorizonShadows && vp->SurfMgr2()) j.hor = vp->SurfMgr2()->HorizonAt (latc, lngc, j.hb);
	if (!worker.joinable()) worker = std::thread (&TreeMgr::Work, this); // first job: bodies never low enough get no thread
	{
		std::lock_guard<std::mutex> lk (mx);
		todo.push_back (std::move (j));
	}
	cv.notify_one();
	cells[key].pending = true;
	return true;
}

void TreeMgr::Update ()
{
	bool proxy = scn->GetCameraProxyVisual() == vp;
	double agl = proxy ? scn->GetCameraElevation() : 1e9;
	active = Config->Trees && agl < MaxAgl;
	if (!active) {
		if (!cells.empty()) {
			std::lock_guard<std::mutex> lk (mx);
			todo.clear();
			ready.clear();
		}
		for (auto &c : cells) delete c.second.buf;
		cells.clear();
		return;
	}
	VECTOR3 cam = vp->CameraPos();
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 loc = tmul (rot, cam);
	double r = length (loc);
	camLat = asin (loc.y / r);
	camLng = atan2 (loc.z, loc.x);
	double range = Config->TreeRange, t = oapiGetSysTime();

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
		c.miss = res.classMiss ? c.miss + 1 : 0;
		c.complete = res.complete && !(res.classMiss && c.miss < 6); // a Class tile still coming: a few tries, 10 s apart
		c.pending = false;
		c.tCheck = t + (res.classMiss ? 10.0 : 2.0);
	}

	if (t >= tWant) {
		tWant = t + 0.5;
		std::vector<long long> want;
		Cells (camLat, camLng, range + 300.0, want);
		VECTOR3 uc = Up (camLat, camLng);
		std::vector<std::pair<double, long long>> order;
		for (long long k : want) if (!cells.count (k)) {
			double la0, la1, lo0, lo1;
			Bounds (k, la0, la1, lo0, lo1);
			order.push_back ({-dotp (Up (0.5 * (la0 + la1), 0.5 * (lo0 + lo1)), uc), k});
		}
		std::sort (order.begin(), order.end()); // nearest first
		for (const auto &q : order) Ask (q.second);
		for (auto it = cells.begin(); it != cells.end(); ) {
			double la0, la1, lo0, lo1;
			Bounds (it->first, la0, la1, lo0, lo1);
			if (R * acos (std::min (1.0, dotp (Up (0.5 * (la0 + la1), 0.5 * (lo0 + lo1)), Up (camLat, camLng)))) > range + 600.0) {
				delete it->second.buf;
				it = cells.erase (it);
			} else ++it;
		}
	}

	// the terrain under them refines, horizon maps arrive: a cell is made again
	int n = 0;
	for (auto &kv : cells) {
		Cell &c = kv.second;
		if (c.pending || t < c.tCheck || n >= ChecksPerFrame) continue;
		n++;
		c.tCheck = t + 2.0;
		double la0, la1, lo0, lo1;
		Bounds (kv.first, la0, la1, lo0, lo1);
		double lat[5] = {0.5 * (la0 + la1), la0 + 0.1 * (la1 - la0), la1 - 0.1 * (la1 - la0), la0 + 0.1 * (la1 - la0), la1 - 0.1 * (la1 - la0)};
		double lng[5] = {0.5 * (lo0 + lo1), lo0 + 0.1 * (lo1 - lo0), lo0 + 0.1 * (lo1 - lo0), lo1 - 0.1 * (lo1 - lo0), lo1 - 0.1 * (lo1 - lo0)};
		double g[5];
		int rc[5];
		vp->SurfMgr2()->GetLoadedElevations (5, lng, lat, g, rc);
		bool redo = !c.complete;
		for (int i = 0; i < 5 && !redo; i++) if (rc[i] == 1 && std::fabs (g[i] - c.gc[i]) > 0.3) redo = true;
		if (!redo && !c.hasHor && Config->HorizonShadows) {
			double hb[4];
			redo = vp->SurfMgr2()->HorizonAt (lat[0], lng[0], hb) != nullptr;
		}
		if (redo) Ask (kv.first);
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
				if (!it->p && Now() - it->t > 8.0) { cache.erase (it); break; } // a miss is looked for again: the stream may have written it
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
	bool miss = false; // this job read a coarser Class tile than L16, or none
	auto Texel = [&](const std::string &layer, double lat, double lng, BYTE *bgra) -> bool {
		for (int L = LC; L >= LC - 2; L--) {
			double span = PI / double(1 << (L - 4));
			int ilat = std::min ((1 << (L - 4)) - 1, std::max (0, (int)std::floor ((0.5 * PI - lat) / span)));
			int ilng = std::min ((2 << (L - 4)) - 1, std::max (0, (int)std::floor ((lng + PI) / span)));
			std::shared_ptr<Pix> p = Tile (layer, L, ilat, ilng);
			if (!p) continue;
			if (L < LC && layer == "Class") miss = true;
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
		Job j = std::move (todo.front());
		todo.pop_front();
		lk.unlock();

		Result res;
		res.key = j.key;
		miss = false;
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
		for (int i = 0; i < G * G; i++) if (grc[i] != 1) res.complete = false;
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

		auto Blocked = [&](double lat, double lng) {
			double s;
			Ground (lat, lng, &s);
			if (s > tan (40.0 * RAD)) return true;
			for (const Foot *f : feet) {
				double dx = (lng - f->lng) * R * cos (f->lat), dz = (lat - f->lat) * R;
				if (std::fabs (dx) > f->rad + 2.0 || std::fabs (dz) > f->rad + 2.0) continue;
				if (TreeGeom::InRing (f->ring, dx, dz, 2.0)) return true;
			}
			for (const auto &b : j.bases) {
				VECTOR3 p = Up (lat, lng) * R - b->p, l = tmul (b->rb, mul (b->rp, p));
				if (b->idx.Covers (l.x, l.z, 6.0)) return true;
			}
			return false;
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
		TreeGeom::Place (j.lat0, j.lat1, j.lng0, j.lng1, P, Cls, [&](double la, double lo) { return Ground (la, lo, nullptr); }, Blocked, trees);

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
			in.kind = tr.kind;
			in.t0 = tr.tint0; in.t1 = tr.tint1;
			memset (in.h, 0, sizeof in.h); // sin -0.25: never shadowed
			if (j.hor) {
				double u = (tr.lng - j.hb[2]) / (j.hb[3] - j.hb[2]), v = (j.hb[1] - tr.lat) / (j.hb[1] - j.hb[0]);
				if (u >= 0 && u <= 1 && v >= 0 && v <= 1) Horizon::Sample (*j.hor, u, v, in.h);
			}
			res.inst.push_back (in);
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
	if (cells.empty() || !sh || !decl) return;
	struct {
		D3DXMATRIX mW, mVP;
		D3DXVECTOR4 cam, band, sun, hw0, hw1, col, amb, trans, insc;
	} prm;
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 cam = vp->CameraPos();
	D3DXVECTOR4 F = D9LinearFieldOfView (scn->GetProjectionMatrix());
	prm.mVP = mVPd ? *mVPd : *scn->GetProjectionViewMatrix();
	ConstParams *cp = vp->GetScatterConst();
	float expo = cp && cp->Expo > 0.0f ? cp->Expo : 1.0f;
	double range = Config->TreeRange, cr = 0.75 * CellSpan() * R + 40.0;
	float time = (float)fmod (oapiGetSimTime(), 3600.0);
	prm.band = D3DXVECTOR4 ((float)MeshEnd, (float)MeshEnd, (float)FadeW, (float)range);
	sh->Setup (decl, true, 0);
	dev->SetCullMode (VK_CULL_MODE_NONE);
	dev->SetStreamSource (0, vb, 0, sizeof (TreeGeom::Vtx));
	dev->SetIndices (ib);
	for (auto &kv : cells) {
		Cell &c = kv.second;
		if (!c.buf) continue;
		VECTOR3 e = mul (rot, c.e0), u = mul (rot, c.u0), n = mul (rot, c.n0), g = mul (rot, c.p0), o = g - cam;
		double dc = length (o);
		if (dc - cr > range) continue;
		D3DXMATRIX &W = prm.mW, WV;
		D3DXMatrixIdentity (&W);
		W._11 = (float)e.x; W._12 = (float)e.y; W._13 = (float)e.z;
		W._21 = (float)u.x; W._22 = (float)u.y; W._23 = (float)u.z;
		W._31 = (float)n.x; W._32 = (float)n.y; W._33 = (float)n.z;
		W._41 = (float)o.x; W._42 = (float)o.y; W._43 = (float)o.z;
		D3DXMatrixMultiply (&WV, &W, scn->GetViewMatrix());
		D9BBox bb;
		memset (&bb, 0, sizeof bb);
		bb.bs = D3DXVECTOR4 (0.0f, 15.0f, 0.0f, (float)cr);
		if (!D9IsBSVisible (&bb, &WV, &F)) continue;
		prm.cam = D3DXVECTOR4 ((float)-dotp (o, e), (float)-dotp (o, u), (float)-dotp (o, n), time);
		D3DXVECTOR4 sc (0, 1, 0, 0);
		prm.amb = D3DXVECTOR4 (0, 0, 0, depth ? 1.0f : 0.0f);
		prm.col = prm.trans = D3DXVECTOR4 (1, 1, 1, expo);
		prm.insc = D3DXVECTOR4 (0, 0, 0, 0);
		if (!depth) {
			D3D9Sun s = vp->GetObjectAtmoParams (g);
			VECTOR3 sd = _V(-s.Dir.x, -s.Dir.y, -s.Dir.z);
			sc = D3DXVECTOR4 ((float)dotp (sd, e), (float)dotp (sd, u), (float)dotp (sd, n), 0.0f);
			prm.col = D3DXVECTOR4 (s.Color.x, s.Color.y, s.Color.z, expo);
			prm.amb = D3DXVECTOR4 (s.Ambient.x, s.Ambient.y, s.Ambient.z, 0.0f);
			prm.trans = D3DXVECTOR4 (s.Transmission.x, s.Transmission.y, s.Transmission.z, 0.0f);
			prm.insc = D3DXVECTOR4 (s.Incatter.x, s.Incatter.y, s.Incatter.z, 0.0f);
		}
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
	if (!active || !(pass == RENDERPASS_MAINSCENE || pass == RENDERPASS_CUSTOMCAM)) return;
	DrawCells (dev, 0, nullptr);
}

void TreeMgr::RenderDepth (VkDev *dev, const D3DXMATRIX &mVP)
{
	if (!active) return;
	DrawCells (dev, 1, &mVP);
}
