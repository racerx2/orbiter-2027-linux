

// not upstream: streamed 3D buildings on a planet's surface (BldgMgr.h)

#include "BldgMgr.h"
#include "VPlanet.h"
#include "Scene.h"
#include "AABBUtil.h"
#include "Surfmgr2.h"
#include "D3D9Client.h"
#include "D3D9Surface.h"
#include "SceneShadows.h" // scene shadow casters
#include "AutoExpo.h" // NightLook::CellDusk (design-night-look 6)
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
const double MaxAlt = 15e3, DropAlt = 20e3, EnvRange = 2e3;
const double EditGap = 0.25; // s between two edits of a cell's mesh: each is a whole upload
const int Z = 14;
const int EditsPerFrame = 4; // cell meshes edited a frame
const int SunMarches = 8;    // main scene: fresh terrain sun marches a frame; a cached cell costs none
const int SunMarchesAux = 4; // the same for the custom camera and env map passes together
const int Workers = 2;       // threads that read, ground and build tiles
const size_t InFlight = 4;   // build jobs out at a time, results waiting for their upload included
const size_t Asks = 24;      // downloads asked a want pass, under the core's 64 building jobs
const size_t Listed = 32;    // tiles on disk listed for building a want pass
const size_t UploadVtx = 300000; // vertices made into meshes a frame, at least one tile
const double BudgetKm = 4e5;     // vertices of the class 1 and up tiles a km of building range: 8 M at 20 km (design-far-buildings D)
const double Margin = 2000.0;    // m past the building range before a tile is dropped
const double PxH = 1.5;          // a building is drawn while it is this many pixels tall
using BldgGeom::Lod::H;

VECTOR3 Up (double lat, double lng) { return _V(cos (lat) * cos (lng), sin (lat), cos (lat) * sin (lng)); }
VECTOR3 East (double lng) { return _V(-sin (lng), 0.0, cos (lng)); }
VECTOR3 North (double lat, double lng) { return _V(-sin (lat) * cos (lng), cos (lat), -sin (lat) * sin (lng)); }

void TileOf (double lat, double lng, int &x, int &y)
{
	int n = 1 << Z;
	lat = std::max (-1.4844, std::min (1.4844, lat));
	x = (int)std::floor ((lng / (2.0 * PI) + 0.5) * n);
	y = (int)std::floor ((1.0 - std::asinh (std::tan (lat)) / PI) * 0.5 * n);
	y = std::max (0, std::min (n - 1, y));
}

// a point in z14 tile units: x east from 180W, y south from the top
void Merc (double fx, double fy, double &lat, double &lng)
{
	double n = (double)(1 << Z);
	lng = fx / n * 2.0 * PI - PI;
	lat = std::atan (std::sinh (PI * (1.0 - 2.0 * fy / n)));
}

void Centre (int x, int y, double &lat, double &lng) { Merc (x + 0.5, y + 0.5, lat, lng); }

double Half (double R, double lat) { return 0.7072 * 2.0 * PI * R * cos (lat) / (double)(1 << Z); } // a tile's half diagonal, m

int CellOf (double f, int nc) { return f > 0.0 ? std::min (nc - 1, (int)(std::min (f, 1.0) * nc)) : 0; } // NaN goes to 0

uint64_t Mix (uint64_t x) { x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 29; x *= 0x94D049BB133111EBull; return x ^ (x >> 32); }
}

BldgMgr::BldgMgr (vPlanet *_vp, OBJHANDLE _hObj, const Scene *_scn)
: vp(_vp), hObj(_hObj), scn(_scn)
{
	R = oapiGetSize (hObj);
	texFacade = oapiLoadTexture ("Bldg/facade.dds"); // with its _emis and _norm maps
	if (texFacade && Config->NightLook) // NightLook: the night map with each window's colour and on-time replaces _emis (design-night-look 6)
		if (VkTex *nm = NatLoadSpecialTexture ("Bldg/facade.dds", "nite")) { SURFACE(texFacade)->AddMap (MAP_EMISSION, nm); nite = true; }
	texRoof = oapiLoadTexture ("Bldg/roof.dds");
	if (!texFacade || !texRoof) oapiWriteLog ("TileStream: no Textures/Bldg, buildings are drawn untextured");
	rangeM = Config->BldgRange * 1000.0;
	for (int i = 0; i < Workers; i++) workers.emplace_back (&BldgMgr::Work, this);
}

// runs at session close, before the surface manager and the core's tile stream go: the workers read both
BldgMgr::~BldgMgr ()
{
	{
		std::lock_guard<std::mutex> lk (mx);
		quit = true;
	}
	cv.notify_all();
	for (std::thread &w : workers) w.join();
	for (auto &p : tiles) Drop (p.second);
	if (texFacade) oapiReleaseTexture (texFacade); // after every cell mesh that drew with it
	if (texRoof) oapiReleaseTexture (texRoof);
}

// worker threads: no lock held while a file is read or grounds are looked up
void BldgMgr::Work ()
{
	std::unique_lock<std::mutex> lk (mx);
	for (;;) {
		cv.wait (lk, [this] { return quit || !todo.empty(); });
		if (quit) return;
		Job j = std::move (todo.front());
		todo.pop_front();
		lk.unlock();
		if (j.ground) {
			Moved o;
			o.key = j.key; o.gen = j.gen;
			try { Regrounds (j, o); }
			catch (...) { o.g.clear(); }
			lk.lock();
			moved.push_back (std::move (o));
		} else {
			Built o;
			try { Prepare (j, o); }
			catch (...) { o.ok = false; o.grps.clear(); o.t = Tile(); } // out of memory: skipped like an unreadable file
			o.key = j.key; o.cls = j.cls;
			lk.lock();
			ready.push_back (std::move (o));
		}
	}
}

// worker: oapiTileStreamBldgLoad reads and parses the file, and reads only core state fixed for the session
void BldgMgr::Prepare (const Job &j, Built &o) const
{
	TSBLDGMESH *m = oapiTileStreamBldgLoad (hObj, (int)(j.key >> 20), (int)(j.key & 0xfffff));
	if (!m) return;
	try { Fill (j, *m, o); }
	catch (...) { oapiTileStreamBldgFree (m); throw; }
	oapiTileStreamBldgFree (m); // the groups have their own copies
	o.ok = true;
}

void BldgMgr::Drop (Tile &t)
{
	for (Cell &c : t.cell) {
		delete c.mesh;
		c.mesh = nullptr;
	}
	if (t.cls > 0) farVtx -= std::min (farVtx, t.nv);
	t.nv = 0;
}

// ground under n points from the deepest loaded tile, 256 a lock, from any thread: rc 1 where found, else the ground 0
void BldgMgr::GroundsLoaded (const double *lng, const double *lat, size_t n, double *g, int *rc) const
{
	TileManager2<SurfTile> *sm = vp->SurfMgr2();
	for (size_t i0 = 0; i0 < n; i0 += 256) {
		int k = (int)std::min<size_t> (256, n - i0);
		if (sm) sm->GetLoadedElevations (k, lng + i0, lat + i0, g + i0, rc + i0);
		else for (int i = 0; i < k; i++) g[i0 + i] = 0.0, rc[i0 + i] = 0;
	}
}

// render thread: the same, where nothing is loaded the core's elevation; the drawn tree differs per pass
void BldgMgr::Grounds (const double *lng, const double *lat, size_t n, double *g) const
{
	std::vector<int> rc (n);
	GroundsLoaded (lng, lat, n, g, rc.data());
	for (size_t i = 0; i < n; i++)
		if (rc[i] != 1) g[i] = oapiSurfaceElevation (hObj, lng[i], lat[i]); // 2: no elevation yet, not sea level
}

// the camera to the tile's nearest ground point, m
double BldgMgr::Dist (int x, int y) const
{
	double lat, lng;
	Centre (x, y, lat, lng);
	return std::max (0.0, length (camL - Up (lat, lng) * R) - Half (R, lat));
}

int BldgMgr::NearTiles () const
{
	int n = 0;
	for (const auto &p : tiles) if (p.second.cls == 0 && !p.second.lat.empty()) n++;
	return n;
}

void BldgMgr::Update ()
{
	for (int k = 0; k < 2; k++) { // once a frame, before the passes
		sunLeft[k] = k ? SunMarchesAux : SunMarches;
		if (sunOff[k] >= 0) sunFrom[k] = sunOff[k]; // the marches go on from the first tile put off
		sunOff[k] = -1;
	}
	VECTOR3 cam = vp->CameraPos();
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	camL = tmul (rot, cam);
	double r = length (camL), alt = r - R;
	camLat = asin (camL.y / r);
	camLng = atan2 (camL.z, camL.x);
	px = 0.728 / std::clamp ((double)scn->ViewH(), 720.0, 2160.0); // a 40 deg field: zooming builds nothing again
	std::deque<Moved> mv;
	{
		std::lock_guard<std::mutex> lk (mx);
		if (alt > DropAlt) {
			for (const Job &j : todo) {
				if (j.ground) groundOut = false;
				else busy.erase (j.key);
			}
			todo.clear();
		}
		while (!ready.empty()) { got.push_back (std::move (ready.front())); ready.pop_front(); }
		mv.swap (moved);
	}
	if (alt > DropAlt) {
		for (const Built &b : got) busy.erase (b.key);
		got.clear();
		if (!mv.empty()) groundOut = false;
		for (auto &p : tiles) Drop (p.second);
		tiles.clear();
		stale.clear();
		dispatch.clear();
		return;
	}
	size_t up = 0;
	while (!got.empty()) { // made into meshes on the render thread, a few a frame
		Built &b = got.front();
		if (b.ok && b.nv && up && up + b.nv > UploadVtx) break; // the rest next frame
		if (!b.ok) bad.insert (b.key); // unreadable: skipped this session
		else {
			hist[b.key] = b.hist;
			if (Dist ((int)(b.key >> 20), (int)(b.key & 0xfffff)) <= rangeM + Margin) { // else the tile is gone
				up += b.nv + 1;
				Upload (b);
			}
		}
		busy.erase (b.key);
		got.pop_front();
	}
	for (Moved &m : mv) { // a far tile's grounds that moved: lifted by Edit as a class-0 tile's are
		groundOut = false;
		auto it = tiles.find (m.key);
		if (it == tiles.end() || it->second.gen != m.gen) continue; // built again or gone since
		Tile &t = it->second;
		for (const auto &p : m.g)
			if (p.first < t.ground.size()) {
				t.ground[p.first] = p.second;
				t.cell[t.cellOf[p.first]].dirty = true;
			}
	}
	for (auto it = tiles.begin(); it != tiles.end(); ) {
		if (length (camL - it->second.p0) - it->second.half > rangeM + Margin) {
			Drop (it->second);
			stale.erase (it->first);
			it = tiles.erase (it);
		} else ++it;
	}
	double t = oapiGetSysTime();
	if (alt < MaxAlt && t >= tWant) {
		tWant = t + 0.5;
		std::vector<VECTOR3> mv;
		if (fixed.Update (hObj, mv)) Stale (mv); // fixed structures made, moved or gone (design fixed-structures)
		Want (t);
	}
	while (busy.size() < InFlight && di < dispatch.size()) { // nearest first
		long long key = dispatch[di].first;
		int c = dispatch[di++].second;
		auto ti = tiles.find (key);
		if (busy.count (key) || bad.count (key) || (ti != tiles.end() && (ti->second.empty || (ti->second.cls == c && !stale.count (key))))) continue;
		Job j;
		j.key = key;
		j.cls = c;
		j.bases = Bases ((int)(key >> 20), (int)(key & 0xfffff));
		double la0, ln0;
		Centre ((int)(key >> 20), (int)(key & 0xfffff), la0, ln0);
		fixed.Near (Up (la0, ln0), R, 25e3, j.fixed);
		if (!zonesRead) { zones = ClearZones::Read (hObj, true); zonesRead = true; } // bases never change in a session
		ClearZones::Near (zones, Up (la0, ln0), R, 25e3, j.zones);
		stale.erase (key); // this build has the records as they are now
		busy.insert (key);
		{
			std::lock_guard<std::mutex> lk (mx);
			todo.push_back (std::move (j));
		}
		cv.notify_one();
	}
	if (!groundOut && busy.empty() && t >= tFar && !tiles.empty()) { // far tiles' grounds, while no build waits: one tile every 0.25 s
		tFar = t + 0.25;
		auto it = tiles.upper_bound (lastFar);
		for (size_t i = 0; i < tiles.size(); i++, ++it) {
			if (it == tiles.end()) it = tiles.begin();
			const Tile &T = it->second;
			if (T.cls == 0 || T.lat.empty()) continue;
			lastFar = it->first;
			Job j;
			j.key = it->first;
			j.ground = true;
			j.gen = T.gen;
			j.lat = T.lat; j.lng = T.lng; j.g = T.ground;
			groundOut = true;
			{
				std::lock_guard<std::mutex> lk (mx);
				todo.push_back (std::move (j));
			}
			cv.notify_one();
			break;
		}
	}
	if (t >= tGround && !tiles.empty()) { // the terrain under class-0 tiles refines as the camera comes down: one tile a pass
		auto it = tiles.upper_bound (lastGround);
		size_t i = 0;
		for (; i < tiles.size(); i++, ++it) {
			if (it == tiles.end()) it = tiles.begin();
			if (it->second.cls == 0 && !it->second.lat.empty()) break;
		}
		if (i == tiles.size()) settle = 0;
		else {
			lastGround = it->first;
			if (Reground (it->second)) settle = NearTiles(); // still refining: every frame until a round moves nothing
			else if (settle > 0) settle--;
		}
		tGround = settle > 0 ? t : t + 0.25;
	}
	Night();
	auto it = tiles.lower_bound (lastEdit); // the lifts, round the tiles: a few cell meshes a frame
	int n = 0;
	for (size_t i = 0; i < tiles.size(); i++, ++it) {
		if (it == tiles.end()) it = tiles.begin();
		for (Cell &c : it->second.cell)
			if (c.dirty && t >= c.tEdit && n < EditsPerFrame) { Edit (it->second, c, t); n++; }
		if (n == EditsPerFrame) { lastEdit = it->first; break; } // the rest of this tile next frame
	}
	if (scale != logScale || (t >= tLog && !tiles.empty())) { // at each budget step and once a minute
		oapiWriteLogV ("TileStream: buildings: %d tiles (%d near), far vertices %zu (estimate %.0f), budget scale %d", (int)tiles.size(), NearTiles(), farVtx, estNow, scale);
		logScale = scale;
		tLog = t + 60.0;
	}
}

// the tiles within the building range nearest first: the budget scale, their classes, downloads asked and builds listed
void BldgMgr::Want (double t)
{
	using namespace BldgGeom::Lod;
	double reach = rangeM + Margin;
	double dlat = reach / R, dlng = reach / (R * std::max (0.05, cos (camLat)));
	int x0, y0, x1, y1;
	TileOf (camLat + dlat, camLng - dlng, x0, y0);
	TileOf (camLat - dlat, camLng + dlng, x1, y1);
	if (x1 < x0) x1 += 1 << Z; // across 180
	struct K { double d; long long key; int cur; };
	std::vector<K> ks;
	for (int y = y0; y <= y1; y++)
		for (int xx = x0; xx <= x1; xx++) {
			int x = xx & ((1 << Z) - 1);
			double d = Dist (x, y);
			if (d > rangeM) continue;
			long long key = (long long)x << 20 | y;
			auto ti = tiles.find (key);
			ks.push_back ({d, key, ti == tiles.end() ? -1 : ti->second.cls});
		}
	std::sort (ks.begin(), ks.end(), [] (const K &a, const K &b) { return a.d < b.d; });
	double mean[NClass] = {};
	int known = 0;
	for (const K &k : ks) {
		auto h = hist.find (k.key);
		if (h == hist.end()) continue;
		for (int c = 0; c < NClass; c++) mean[c] += h->second[c];
		known++;
	}
	for (double &m : mean) m /= std::max (1, known);
	double real = 0.0, guess = 0.0;
	for (const auto &p : tiles) {
		if (p.second.cls <= 0) continue;
		auto h = hist.find (p.first);
		if (h != hist.end()) { real += (double)p.second.nv; guess += h->second[p.second.cls]; }
	}
	double cal = guess > 1e5 ? std::clamp (real / guess, 0.5, 3.0) : 1.0; // far mesh vertices to .bld ones, as built so far
	auto est = [&] (int s) { // the far vertices at a scale; a tile never built counts as the mean of the rest
		double e = 0.0, sc = Scale (s);
		for (const K &k : ks) {
			int c = Class (PxH * px * k.d, sc, k.cur);
			if (!c) continue;
			auto h = hist.find (k.key);
			e += h != hist.end() ? h->second[c] : mean[c];
		}
		return e * cal;
	};
	double budget = BudgetKm * rangeM / 1000.0;
	estNow = est (scale);
	scale = Step (scale, estNow, scale ? est (scale - 1) : -1.0, budget);
	double sc = Scale (scale);
	dispatch.clear();
	di = 0;
	std::vector<std::pair<int, int>> asked;
	for (const K &k : ks) {
		if (asked.size() >= Asks || dispatch.size() >= Listed) break;
		int c = Class (PxH * px * k.d, sc, k.cur);
		auto ti = tiles.find (k.key);
		if (ti != tiles.end() && (ti->second.empty || (ti->second.cls == c && !stale.count (k.key)))) continue;
		if (busy.count (k.key) || bad.count (k.key)) continue;
		auto lt = later.find (k.key);
		if (lt != later.end()) {
			if (t < lt->second) continue;
			later.erase (lt);
		}
		int x = (int)(k.key >> 20), y = (int)(k.key & 0xfffff);
		int w = oapiTileStreamBldgWant (hObj, x, y);
		if (w < 0) later[k.key] = t + 10.0; // not now: offline and not on disk, or no building source
		else if (w == 0) asked.push_back ({x, y});
		else dispatch.push_back ({k.key, c});
	}
	for (auto a = asked.rbegin(); a != asked.rend(); ++a) oapiTileStreamBldgWant (hObj, a->first, a->second); // far to near: the core starts the latest want first
}

// a base's structures in its frame, built once: the meshes the base draws, add-on bases and their own meshes included
const BldgMgr::BaseRec &BldgMgr::BaseOf (OBJHANDLE hBase)
{
	auto it = bases.find (hBase);
	if (it != bases.end()) return it->second;
	BaseRec &b = bases[hBase];
	VECTOR3 gp, gb;
	oapiGetRotationMatrix (hObj, &b.rp);
	oapiGetRotationMatrix (hBase, &b.rb);
	oapiGetGlobalPos (hObj, &gp);
	oapiGetGlobalPos (hBase, &gb);
	b.p = tmul (b.rp, gb - gp);
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
					if (ok) b.idx.Add (v[0], v[1], v[2]);
				}
			}
		}
	char name[64] = "";
	oapiGetObjectName (hBase, name, 64);
	if (b.idx.Count()) oapiWriteLogV ("TileStream: base %s: %u structure faces keep streamed buildings out", name, (unsigned)b.idx.Count());
	return b;
}

// render thread: the bases within 25 km of a tile, their structures made ready for a worker to read
std::vector<const BldgMgr::BaseRec *> BldgMgr::Bases (int x, int y)
{
	double lat0, lng0;
	Centre (x, y, lat0, lng0);
	VECTOR3 u0 = Up (lat0, lng0);
	std::vector<const BaseRec *> near;
	for (DWORD i = 0, nb = oapiGetBaseCount (hObj); i < nb; i++) {
		OBJHANDLE hb = oapiGetBaseByIndex (hObj, i);
		double lng, lat, rad;
		oapiGetBaseEquPos (hb, &lng, &lat, &rad);
		if (R * acos (std::min (1.0, dotp (Up (lat, lng), u0))) > 25e3) continue;
		const BaseRec &br = BaseOf (hb);
		if (!br.idx.Empty()) near.push_back (&br);
	}
	return near;
}

void BldgMgr::Stale (const std::vector<VECTOR3> &moved)
{
	size_t n = 0;
	auto mark = [&] (long long key) {
		double lat, lng;
		Centre ((int)(key >> 20), (int)(key & 0xfffff), lat, lng);
		VECTOR3 u = Up (lat, lng);
		for (const VECTOR3 &m : moved) {
			double l = length (m);
			if (l > 0 && R * acos (std::min (1.0, dotp (m / l, u))) <= 25e3) { n += stale.insert (key).second; return; }
		}
	};
	for (const auto &p : tiles) if (!p.second.empty) mark (p.first);
	for (long long k : busy) mark (k);
	if (n) oapiWriteLogV ("TileStream: %u building tiles built again for fixed structures", (unsigned)n);
}

// worker: the tile's buildings of its class, one mesh's groups a cell in the tile-centre frame; textured walls and pitched roofs, flat roofs
void BldgMgr::Fill (const Job &j, const TSBLDGMESH &m, Built &o) const
{
	Tile &t = o.t;
	t.x = (int)(j.key >> 20); t.y = (int)(j.key & 0xfffff);
	t.cls = j.cls;
	t.nc = j.cls ? 1 : NC;
	t.empty = m.nb == 0;
	double lat0, lng0;
	Centre (t.x, t.y, lat0, lng0);
	t.u0 = Up (lat0, lng0); t.e0 = East (lng0); t.n0 = North (lat0, lng0);
	t.p0 = t.u0 * R;
	t.half = Half (R, lat0);
	t.rad = 0.0;
	const int nc = t.nc, ncell = nc * nc;
	t.cell.assign (ncell, Cell());
	for (int c = 0; c < ncell; c++) {
		double la, ln;
		Merc (t.x + (c % nc + 0.5) / nc, t.y + (c / nc + 0.5) / nc, la, ln);
		t.cell[c].up = Up (la, ln);
		t.cell[c].nightAt = 0.6f * (float)(Mix ((uint64_t)t.x * 73856093ull ^ (uint64_t)t.y * 19349663ull ^ (uint64_t)c * 83492791ull) % 1000) / 1000.0f;
	}
	std::vector<DWORD> keep;
	for (DWORD b = 0; b < m.nb; b++) {
		for (int c = 0; c < NClass; c++) if (m.b[b].hmax >= H[c]) o.hist[c] += (float)m.b[b].nv;
		if (m.b[b].hmax >= H[t.cls]) keep.push_back (b);
	}
	const size_t n = keep.size();
	t.lat.resize (n); t.lng.resize (n);
	t.ground.assign (n, 0.0);
	t.runs.assign (n, {});
	t.cellOf.assign (n, 0);
	for (size_t i = 0; i < n; i++) t.lat[i] = m.b[keep[i]].lat, t.lng[i] = m.b[keep[i]].lng;
	std::vector<int> rc (n);
	GroundsLoaded (t.lng.data(), t.lat.data(), n, t.ground.data(), rc.data());
	double sum = 0.0;
	size_t found = 0;
	for (size_t i = 0; i < n; i++) if (rc[i] == 1) { sum += t.ground[i]; found++; }
	for (size_t i = 0; i < n; i++) if (rc[i] != 1) t.ground[i] = found ? sum / found : 0.0; // no terrain loaded there yet: the re-grounds find it
	const double zn = (double)(1 << Z);
	o.grps.assign (ncell, {});
	std::vector<std::array<int, 2>> cur (ncell, {-1, -1});
	BldgGeom::Out g;
	std::vector<std::array<double, 2>> foot;
	std::vector<VECTOR3> ring;
	for (size_t i = 0; i < n; i++) {
		const TSBLDG &B = m.b[keep[i]];
		uint64_t la, ln;
		memcpy (&la, &B.lat, 8); memcpy (&ln, &B.lng, 8);
		BldgGeom::Build (m.vtx + (size_t)B.v0 * 6, B.nv, m.idx + B.i0, B.ni, B.hmin, B.hmax, (uint32_t)Mix (la ^ (ln * 0x9E3779B97F4A7C15ull)), g, t.cls > 0);
		VECTOR3 ub = Up (B.lat, B.lng), eb = East (B.lng), nb = North (B.lat, B.lng);
		VECTOR3 pb = ub * (R + t.ground[i]) - t.p0;
		if ((!j.bases.empty() || !j.fixed.empty() || !j.zones.empty()) && g.ring.size() >= 3) { // inside or touching a base's own building, or a fixed structure's, or in a clear zone: left out
			auto hits = [&] (const MATRIX3 &rp, const MATRIX3 &rb, const VECTOR3 &p0, const BldgGeom::BaseIndex &idx, double clear) {
				foot.clear();
				for (const auto &q : g.ring) {
					VECTOR3 p = pb + t.p0 + eb * q[0] + nb * q[1] - p0, l = tmul (rb, mul (rp, p));
					foot.push_back ({l.x, l.z});
				}
				if (clear > 0.0 && BldgGeom::Reaches (foot, clear)) return true; // a fixed structure's clear zone (design fixed-clear)
				bool hit = false;
				double cx = 0, cz = 0;
				for (size_t k = 0; k < foot.size() && !hit; k++) {
					const auto &a = foot[k], &c = foot[(k + 1) % foot.size()];
					hit = idx.Near (a[0], a[1], 6.0) || idx.Near (0.5 * (a[0] + c[0]), 0.5 * (a[1] + c[1]), 6.0);
					cx += a[0]; cz += a[1];
				}
				return hit || idx.Near (cx / foot.size(), cz / foot.size(), 6.0) || idx.Inside (foot);
			};
			bool hit = false;
			if (!j.zones.empty()) { // a base file's clear zone (design base-clear)
				ring.clear();
				for (const auto &q : g.ring) ring.push_back (pb + t.p0 + eb * q[0] + nb * q[1]);
				for (const auto &z : j.zones) if (!hit) hit = ClearZones::Reaches (z, z.u * R, ring, foot);
			}
			for (const BaseRec *br : j.bases) if (!hit) hit = hits (br->rp, br->rb, br->p, br->idx, 0.0);
			for (const auto &fr : j.fixed) if (!hit) hit = hits (fr->rp, fr->rb, fr->p, fr->idx, fr->clear);
			if (hit) { o.hidden++; continue; }
		}
		double fx = (B.lng / (2.0 * PI) + 0.5) * zn - t.x, fy = (1.0 - std::asinh (std::tan (B.lat)) / PI) * 0.5 * zn - t.y;
		int cell = CellOf (fy, nc) * nc + CellOf (fx, nc);
		std::vector<Grp> &gs = o.grps[cell];
		bool any = false;
		for (int kind = 0; kind < 2; kind++) {
			const std::vector<BldgGeom::V> &vs = kind ? g.rv : g.fv;
			const std::vector<uint32_t> &is = kind ? g.ri : g.fi;
			if (vs.empty() || vs.size() > 65535) continue;
			int &cg = cur[cell][kind];
			if (cg < 0 || gs[cg].v.size() + vs.size() > 65535) { // a building that does not fit starts a new group
				cg = (int)gs.size();
				gs.push_back ({{}, {}, kind});
			}
			Grp &G = gs[cg];
			DWORD vn = (DWORD)G.v.size();
			for (const BldgGeom::V &s : vs) {
				VECTOR3 p = pb + eb * s.x + ub * s.y + nb * s.z;
				VECTOR3 nr = eb * s.nx + ub * s.ny + nb * s.nz;
				NTVERTEX v;
				v.x = (float)dotp (p, t.e0); v.y = (float)dotp (p, t.u0); v.z = (float)dotp (p, t.n0);
				v.nx = (float)dotp (nr, t.e0); v.ny = (float)dotp (nr, t.u0); v.nz = (float)dotp (nr, t.n0);
				v.tu = s.u; v.tv = s.v;
				G.v.push_back (v);
				t.rad = std::max (t.rad, length (p));
			}
			for (uint32_t k : is) G.i.push_back ((WORD)(vn + k));
			t.runs[i].push_back ({(DWORD)cg, vn, (DWORD)vs.size()});
			o.nv += vs.size();
			any = true;
		}
		if (any) {
			t.cell[cell].bldg.push_back ((DWORD)i);
			t.cellOf[i] = (uint16_t)cell;
		}
	}
	t.placed = t.ground;
}

// worker: a far tile's grounds again, those that moved more than 0.25 m; points with no terrain loaded keep theirs
void BldgMgr::Regrounds (const Job &j, Moved &o) const
{
	size_t n = j.lat.size();
	std::vector<double> g (n);
	std::vector<int> rc (n);
	GroundsLoaded (j.lng.data(), j.lat.data(), n, g.data(), rc.data());
	for (size_t i = 0; i < n; i++)
		if (rc[i] == 1 && std::fabs (g[i] - j.g[i]) > 0.25) o.g.push_back ({(DWORD)i, g[i]});
}

// render thread: a worker's tile made into meshes, in place of the tile's old ones
void BldgMgr::Upload (Built &b)
{
	Tile &T = tiles[b.key];
	Drop (T);
	T = std::move (b.t);
	T.gen = ++gens;
	T.nv = b.nv;
	if (T.cls > 0) farVtx += T.nv;
	if (b.hidden) oapiWriteLogV ("TileStream: buildings %d/%d/%d: %u left out (base and fixed structures, clear zones)", Z, T.x, T.y, (unsigned)b.hidden);
	bool tex = texFacade && texRoof;
	MATERIAL mat[2] = { // textured: white, the texture is the colour; else the grey of untextured walls and roofs
		{{1, 1, 1, 1}, {1, 1, 1, 1}, {0.06f, 0.06f, 0.06f, 1}, {0, 0, 0, 0}, 10.0f},
		{{1, 1, 1, 1}, {1, 1, 1, 1}, {0.03f, 0.03f, 0.03f, 1}, {0, 0, 0, 0}, 5.0f}};
	if (!tex) {
		mat[0] = {{0.78f, 0.76f, 0.72f, 1.0f}, {0.78f, 0.76f, 0.72f, 1.0f}, {0.1f, 0.1f, 0.1f, 1.0f}, {0, 0, 0, 0}, 10.0f};
		mat[1] = {{0.52f, 0.52f, 0.54f, 1.0f}, {0.52f, 0.52f, 0.54f, 1.0f}, {0.05f, 0.05f, 0.05f, 1.0f}, {0, 0, 0, 0}, 5.0f};
	}
	for (size_t c = 0; c < T.cell.size() && c < b.grps.size(); c++) {
		Cell &C = T.cell[c];
		if (b.grps[c].empty()) continue;
		MESHHANDLE hMesh = oapiCreateMesh (0, nullptr);
		DWORD mi[2] = {oapiAddMaterial (hMesh, &mat[0]), oapiAddMaterial (hMesh, &mat[1])};
		DWORD ti[2] = {SPEC_DEFAULT, SPEC_DEFAULT};
		if (tex) { ti[0] = oapiAddMeshTexture (hMesh, texFacade); ti[1] = oapiAddMeshTexture (hMesh, texRoof); }
		for (Grp &G : b.grps[c]) {
			MESHGROUP mg;
			memset (&mg, 0, sizeof mg);
			mg.Vtx = G.v.data(); mg.nVtx = (DWORD)G.v.size();
			mg.Idx = G.i.data(); mg.nIdx = (DWORD)G.i.size();
			mg.MtrlIdx = mi[G.kind];
			mg.TexIdx = ti[G.kind] == (DWORD)-1 ? SPEC_DEFAULT : ti[G.kind];
			oapiAddMeshGroup (hMesh, &mg);
		}
		C.mesh = new D3D9Mesh (hMesh);
		oapiDeleteMesh (hMesh); // the D3D9Mesh copied it all; BldgMgr's own texture references keep them
		if (tex) { SURFACE(texFacade)->DecRef(); SURFACE(texRoof)->DecRef(); } // the core mesh's: its release is skipped while a mesh draws them
		oapi::FVECTOR4 off (0.0f, 0.0f, 0.0f, 1.0f);
		C.mesh->SetMaterialEx (0, MatProp::Emission, &off); // no window light by day (the default is 1)
		C.mesh->SetMaterialEx (1, MatProp::Emission, &off);
		std::vector<Grp>().swap (b.grps[c]);
	}
	if (T.cls == 0) settle = NearTiles(); // placed on the terrain loaded so far
}

// window light per cell from the sun's elevation there: off above +2 deg, full by -6 deg, cells starting along that ramp
void BldgMgr::Night ()
{
	if (!texFacade || tiles.empty()) return;
	OBJHANDLE hSun = oapiGetGbodyByIndex (0);
	if (!hSun) return;
	VECTOR3 gs, gp;
	MATRIX3 rot;
	oapiGetGlobalPos (hSun, &gs);
	oapiGetGlobalPos (hObj, &gp);
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 sd = unit (tmul (rot, gs - gp));
	for (auto &p : tiles)
		for (Cell &c : p.second.cell) {
			if (!c.mesh) continue;
			double el = asin (std::clamp (dotp (sd, c.up), -1.0, 1.0)) * DEG;
			if (nite) { // NightLook: the cell's dusk progress in SpecialFX (0, D, intensity, 0.75 tag); the windows come on by their own on-times (design-night-look 6)
				float D = NightLook::CellDusk ((float)el, c.nightAt / 0.6f);
				if (std::fabs (D - c.night) < 0.003f && !(D == 0.0f && c.night != 0.0f) && !(D == 1.0f && c.night != 1.0f)) continue; // fine steps: a close window's step is 0.06 of D wide
				c.night = D;
				oapi::FVECTOR4 fx (0.0f, D, 1.0f, 0.75f);
				c.mesh->SetMaterialEx (0, MatProp::SpecialFX, &fx);
				continue;
			}
			float dusk = (float)std::clamp ((2.0 - el) / 8.0, 0.0, 1.0);
			float lit = std::clamp ((dusk - c.nightAt) / 0.4f, 0.0f, 1.0f);
			if (std::fabs (lit - c.night) < 0.02f && !(lit == 0.0f && c.night != 0.0f) && !(lit == 1.0f && c.night != 1.0f)) continue;
			c.night = lit;
			oapi::FVECTOR4 e (lit, lit, lit, 1.0f);
			c.mesh->SetMaterialEx (0, MatProp::Emission, &e);
		}
}

// the ground under each building: a cell whose buildings' ground moved more than 0.25 m is marked for Edit
bool BldgMgr::Reground (Tile &t)
{
	bool moved = false;
	std::vector<double> &gr = scratch;
	gr.resize (t.lat.size());
	Grounds (t.lng.data(), t.lat.data(), gr.size(), gr.data());
	for (Cell &c : t.cell)
		for (DWORD b : c.bldg) {
			double g = gr[b];
			if (!(std::fabs (g - t.ground[b]) > 0.25)) continue;
			t.ground[b] = g;
			c.dirty = moved = true;
		}
	return moved;
}

// the cell's buildings lifted by what their ground moved since the last edit: one EditGroup a group, one upload
void BldgMgr::Edit (Tile &t, Cell &c, double now)
{
	c.dirty = false;
	c.tEdit = now + EditGap;
	if (!c.mesh) return;
	std::map<DWORD, std::pair<std::vector<WORD>, std::vector<float>>> ed; // per group: vertices, their lift
	for (DWORD b : c.bldg) {
		double d = t.ground[b] - t.placed[b];
		if (d == 0.0) continue;
		t.placed[b] = t.ground[b];
		for (const Run &r : t.runs[b]) {
			auto &e = ed[r.grp];
			for (DWORD v = r.v0; v < r.v0 + r.nv; v++) {
				e.first.push_back ((WORD)v);
				e.second.push_back ((float)d); // along the tile's up: the anchor's up is within 2e-4 rad of it
			}
		}
	}
	for (auto &p : ed) { // the client's EditGroup takes no ADD flags: y is read from the mesh's copy and set
		std::vector<NTVERTEX> v (p.second.first.size());
		GROUPREQUESTSPEC grs;
		memset (&grs, 0, sizeof grs);
		grs.Vtx = v.data(); grs.nVtx = (DWORD)v.size(); grs.VtxPerm = p.second.first.data();
		if (c.mesh->GetGroup (p.first, &grs)) continue;
		for (size_t i = 0; i < v.size(); i++) v[i].y += p.second.second[i];
		GROUPEDITSPEC ges;
		memset (&ges, 0, sizeof ges);
		ges.flags = GRPEDIT_VTXCRDY;
		ges.Vtx = v.data(); ges.nVtx = (DWORD)v.size(); ges.vIdx = p.second.first.data();
		c.mesh->EditGroup (p.first, &ges);
	}
}

// the SceneFx distance pass: the cells in view, far ones too (fog over them), see-through groups left out by opt 2
void BldgMgr::RenderDepth (VkDev *dev, const D3DXMATRIX &mVP)
{
	if (tiles.empty()) return;
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 cam = vp->CameraPos();
	D3DXVECTOR4 F = D9LinearFieldOfView (scn->GetProjectionMatrix());
	D3DXMATRIX VP = mVP;
	for (auto &kv : tiles) {
		Tile &t = kv.second;
		if (!t.nv) continue;
		VECTOR3 e = mul (rot, t.e0), u = mul (rot, t.u0), n = mul (rot, t.n0), o = mul (rot, t.p0) - cam;
		D3DXMATRIX W, WV;
		D3DXMatrixIdentity (&W);
		W._11 = (float)e.x; W._12 = (float)e.y; W._13 = (float)e.z;
		W._21 = (float)u.x; W._22 = (float)u.y; W._23 = (float)u.z;
		W._31 = (float)n.x; W._32 = (float)n.y; W._33 = (float)n.z;
		W._41 = (float)o.x; W._42 = (float)o.y; W._43 = (float)o.z;
		D3DXMatrixMultiply (&WV, &W, scn->GetViewMatrix());
		for (Cell &c : t.cell)
			if (c.mesh && D9IsAABBVisible (c.mesh->GetAABB(), &WV, &F)) c.mesh->RenderShadowMap (&W, &VP, 2);
	}
}

// scene shadow casters: the cells in the cascade's light box, in view or not; opt 1 tests the alpha of 0x20 groups (design-scene-shadows 5)
void BldgMgr::RenderCasters (VkDev *dev, const SceneShadows &s)
{
	if (tiles.empty()) return;
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 cam = vp->CameraPos();
	for (auto &kv : tiles) {
		Tile &t = kv.second;
		if (!t.nv) continue;
		VECTOR3 e = mul (rot, t.e0), u = mul (rot, t.u0), n = mul (rot, t.n0), o = mul (rot, t.p0) - cam;
		if (!s.Cull (D3DXVECTOR3 ((float)o.x, (float)o.y, (float)o.z), (float)t.rad + 50.0f)) continue;
		D3DXMATRIX W;
		D3DXMatrixIdentity (&W);
		W._11 = (float)e.x; W._12 = (float)e.y; W._13 = (float)e.z;
		W._21 = (float)u.x; W._22 = (float)u.y; W._23 = (float)u.z;
		W._31 = (float)n.x; W._32 = (float)n.y; W._33 = (float)n.z;
		W._41 = (float)o.x; W._42 = (float)o.y; W._43 = (float)o.z;
		for (Cell &c : t.cell) {
			if (!c.mesh) continue;
			const D9BBox *bb = c.mesh->GetAABB();
			D3DXVECTOR3 bc;
			D3DXVec3TransformCoord (&bc, (const D3DXVECTOR3 *)&bb->bs, &W);
			if (s.Cull (bc, bb->bs.w)) c.mesh->RenderShadowMap (&W, s.CasterVP(), 1);
		}
	}
}

// each cell's mesh is lit by the tile's sun times the cell's terrain sun, its materials stay as made
void BldgMgr::Render (VkDev *dev)
{
	DWORD pass = scn->GetRenderPass();
	bool main = pass == RENDERPASS_MAINSCENE, env = pass == RENDERPASS_ENVCAM;
	if (tiles.empty() || !(main || env || pass == RENDERPASS_CUSTOMCAM)) return;
	if (!main && !(scn->GetRenderFlags() & 0x20)) return; // with the base structures: env maps in EnvMapMode 2
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 cam = vp->CameraPos(), gp = vp->GlobalPos(); // this pass's camera
	D3DXVECTOR4 F = D9LinearFieldOfView (scn->GetProjectionMatrix());
	int k = main ? 0 : 1;
	auto it = tiles.lower_bound (sunFrom[k]); // round the tiles from where the marches were put off
	for (size_t i = 0; i < tiles.size(); i++, ++it) {
		if (it == tiles.end()) it = tiles.begin();
		Tile &t = it->second;
		if (!t.nv) continue;
		VECTOR3 e = mul (rot, t.e0), u = mul (rot, t.u0), n = mul (rot, t.n0), g = mul (rot, t.p0), o = g - cam;
		if (env && length (o) > EnvRange + t.rad) continue;
		D3DXMATRIX W, WV;
		D3DXMatrixIdentity (&W);
		W._11 = (float)e.x; W._12 = (float)e.y; W._13 = (float)e.z;
		W._21 = (float)u.x; W._22 = (float)u.y; W._23 = (float)u.z;
		W._31 = (float)n.x; W._32 = (float)n.y; W._33 = (float)n.z;
		W._41 = (float)o.x; W._42 = (float)o.y; W._43 = (float)o.z;
		D3DXMatrixMultiply (&WV, &W, scn->GetViewMatrix());
		D3D9Sun sp;
		bool lit = false;
		for (Cell &c : t.cell) {
			if (!c.mesh || !D9IsAABBVisible (c.mesh->GetAABB(), &WV, &F)) continue; // culled by this pass's camera
			if (!lit) { sp = vp->GetObjectAtmoParams (g); lit = true; }
			c.vis = vp->TerrainSunVisibility (gp + mul (rot, c.up * R), c.ts, c.ts.owner ? &sunLeft[k] : nullptr); // a cell's first value is not put off
			if (sunLeft[k] < 0 && sunOff[k] < 0) sunOff[k] = it->first;
			D3D9Sun s = sp;
			s.Color *= c.vis; // the sun only: local lights and earth glow go through the materials
			c.mesh->SetSunLight (&s);
			c.mesh->Render (&W, RENDER_BASE);
		}
	}
}
