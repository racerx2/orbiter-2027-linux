
// not upstream: streamed 3D buildings on a planet's surface (BldgMgr.h)

#include "BldgMgr.h"
#include "VPlanet.h"
#include "Scene.h"
#include "AABBUtil.h"
#include "Surfmgr2.h"
#include "D3D9Client.h"
#include "D3D9Surface.h"
#include <cmath>
#include <cstring>

namespace {
const double MaxAlt = 15e3, DropAlt = 20e3, EnvRange = 2e3;
const double EditGap = 0.25; // s between two edits of a cell's mesh: each is a whole upload
const int Z = 14;
const int EditsPerFrame = 4; // cell meshes edited a frame
const int SunMarches = 8;    // main scene: fresh terrain sun marches a frame; a cached cell costs none
const int SunMarchesAux = 4; // the same for the custom camera and env map passes together
const size_t InFlight = 2; // tiles given to the worker at a time

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

int CellOf (double f, int nc) { return f > 0.0 ? std::min (nc - 1, (int)(std::min (f, 1.0) * nc)) : 0; } // NaN goes to 0

uint64_t Mix (uint64_t x) { x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 29; x *= 0x94D049BB133111EBull; return x ^ (x >> 32); }
}

BldgMgr::BldgMgr (vPlanet *_vp, OBJHANDLE _hObj, const Scene *_scn)
: vp(_vp), hObj(_hObj), scn(_scn)
{
	R = oapiGetSize (hObj);
	texFacade = oapiLoadTexture ("Bldg/facade.dds"); // with its _emis and _norm maps
	texRoof = oapiLoadTexture ("Bldg/roof.dds");
	if (!texFacade || !texRoof) oapiWriteLog ("TileStream: no Textures/Bldg, buildings are drawn untextured");
	worker = std::thread (&BldgMgr::Work, this);
}

// runs at session close, before the core stops its tile stream
BldgMgr::~BldgMgr ()
{
	{
		std::lock_guard<std::mutex> lk (mx);
		quit = true;
	}
	cv.notify_all();
	worker.join();
	for (auto &r : ready) if (r.second) oapiTileStreamBldgFree (r.second);
	for (auto &p : tiles) Drop (p.second);
	if (texFacade) oapiReleaseTexture (texFacade); // after every cell mesh that drew with it
	if (texRoof) oapiReleaseTexture (texRoof);
}

// worker thread: oapiTileStreamBldgLoad reads and parses the file, and reads only core state fixed for the session
void BldgMgr::Work ()
{
	std::unique_lock<std::mutex> lk (mx);
	for (;;) {
		cv.wait (lk, [this] { return quit || !todo.empty(); });
		if (quit) return;
		long long key = todo.front();
		todo.pop_front();
		lk.unlock();
		TSBLDGMESH *m = nullptr;
		try { m = oapiTileStreamBldgLoad (hObj, (int)(key >> 20), (int)(key & 0xfffff)); }
		catch (...) { m = nullptr; } // out of memory: skipped like an unreadable file
		lk.lock();
		ready.push_back ({key, m});
	}
}

void BldgMgr::Drop (Tile &t)
{
	for (Cell &c : t.cell) {
		delete c.mesh;
		c.mesh = nullptr;
	}
}

// ground under n points from the deepest loaded tile, 256 a lock; the drawn tree differs per pass
void BldgMgr::Grounds (const double *lng, const double *lat, size_t n, double *g) const
{
	TileManager2<SurfTile> *sm = vp->SurfMgr2();
	int rc[256];
	for (size_t i0 = 0; i0 < n; i0 += 256) {
		int k = (int)std::min<size_t> (256, n - i0);
		if (sm) sm->GetLoadedElevations (k, lng + i0, lat + i0, g + i0, rc);
		for (int i = 0; i < k; i++)
			if (!sm || rc[i] != 1) g[i0 + i] = oapiSurfaceElevation (hObj, lng[i0 + i], lat[i0 + i]); // 2: no elevation yet, not sea level
	}
}

bool BldgMgr::Near (int x, int y) const
{
	double lat, lng;
	Centre (x, y, lat, lng);
	return R * acos (std::min (1.0, dotp (Up (lat, lng), Up (camLat, camLng)))) <= range + 3000.0;
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
	VECTOR3 loc = tmul (rot, cam);
	double r = length (loc), alt = r - R;
	camLat = asin (loc.y / r);
	camLng = atan2 (loc.z, loc.x);
	range = 3000.0 + std::min (alt, MaxAlt) * 0.2;
	std::pair<long long, TSBLDGMESH*> got (-1, nullptr);
	{
		std::lock_guard<std::mutex> lk (mx);
		if (alt > DropAlt) {
			for (long long k : todo) busy.erase (k);
			todo.clear();
		}
		if (!ready.empty()) {
			got = ready.front();
			ready.pop_front();
		}
	}
	if (got.first >= 0) { // one mesh build a pass: it is on the render thread
		busy.erase (got.first);
		int x = (int)(got.first >> 20), y = (int)(got.first & 0xfffff);
		if (!got.second) bad.insert (got.first); // unreadable: skipped this session
		else {
			if (alt <= DropAlt && Near (x, y) && !tiles.count (got.first)) { // else the tile is gone
				Tile &T = tiles[got.first];
				T.x = x; T.y = y;
				Make (T, got.second);
				settle = (int)tiles.size(); // placed on the terrain loaded so far
			}
			oapiTileStreamBldgFree (got.second); // the D3D9Meshes have their own copies
		}
	}
	if (alt > DropAlt) {
		for (auto &p : tiles) Drop (p.second);
		tiles.clear();
		return;
	}
	double t = oapiGetSysTime();
	if (alt < MaxAlt && t >= tWant) {
		tWant = t + 0.5;
		double dlat = range / R, dlng = range / (R * std::max (0.05, cos (camLat)));
		int x0, y0, x1, y1;
		TileOf (camLat + dlat, camLng - dlng, x0, y0);
		TileOf (camLat - dlat, camLng + dlng, x1, y1);
		if (x1 < x0) x1 += 1 << Z; // across 180
		for (int y = y0; y <= y1; y++)
			for (int xx = x0; xx <= x1; xx++) {
				int x = xx & ((1 << Z) - 1);
				long long key = (long long)x << 20 | y;
				if (tiles.count (key) || bad.count (key) || busy.count (key)) continue;
				int w = oapiTileStreamBldgWant (hObj, x, y);
				if (w < 0) continue; // not now: offline and not on disk, or no building source yet; the rest are still asked
				if (w == 0 || busy.size() >= InFlight) continue;
				busy.insert (key);
				{
					std::lock_guard<std::mutex> lk (mx);
					todo.push_back (key);
				}
				cv.notify_one();
			}
	}
	for (auto it = tiles.begin(); it != tiles.end(); ) {
		if (!Near (it->second.x, it->second.y)) {
			Drop (it->second);
			it = tiles.erase (it);
		} else ++it;
	}
	if (t >= tGround && !tiles.empty()) { // the terrain under them refines as the camera comes down: one tile a pass
		auto it = tiles.upper_bound (lastGround);
		if (it == tiles.end()) it = tiles.begin();
		lastGround = it->first;
		if (Reground (it->second)) settle = (int)tiles.size(); // still refining: every frame until a round moves nothing
		else if (settle > 0) settle--;
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

// a tile's buildings, one mesh a cell in the tile-centre frame: textured walls and pitched roofs, flat roofs; <= 65535 vertices a group
void BldgMgr::Make (Tile &t, const TSBLDGMESH *m)
{
	double lat0, lng0;
	Centre (t.x, t.y, lat0, lng0);
	t.u0 = Up (lat0, lng0); t.e0 = East (lng0); t.n0 = North (lat0, lng0);
	t.p0 = t.u0 * R;
	t.rad = 0.0;
	for (int c = 0; c < NC * NC; c++) {
		double la, ln;
		Merc (t.x + (c % NC + 0.5) / NC, t.y + (c / NC + 0.5) / NC, la, ln);
		t.cell[c].up = Up (la, ln);
		t.cell[c].bldg.clear();
		t.cell[c].night = 0.0f;
		t.cell[c].nightAt = 0.6f * (float)(Mix ((uint64_t)t.x * 73856093ull ^ (uint64_t)t.y * 19349663ull ^ (uint64_t)c * 83492791ull) % 1000) / 1000.0f;
	}
	t.lat.assign (m->nb, 0.0);
	t.lng.assign (m->nb, 0.0);
	t.ground.assign (m->nb, 0.0);
	t.runs.assign (m->nb, {});
	for (DWORD b = 0; b < m->nb; b++) t.lat[b] = m->b[b].lat, t.lng[b] = m->b[b].lng;
	Grounds (t.lng.data(), t.lat.data(), m->nb, t.ground.data());
	std::vector<const BaseRec *> near; // bases within 25 km: their structures keep streamed buildings out
	for (DWORD i = 0, nb = oapiGetBaseCount (hObj); i < nb; i++) {
		OBJHANDLE hb = oapiGetBaseByIndex (hObj, i);
		double lng, lat, rad;
		oapiGetBaseEquPos (hb, &lng, &lat, &rad);
		if (R * acos (std::min (1.0, dotp (Up (lat, lng), t.u0))) > 25e3) continue;
		const BaseRec &br = BaseOf (hb);
		if (!br.idx.Empty()) near.push_back (&br);
	}
	const double n = (double)(1 << Z);
	struct Grp { std::vector<NTVERTEX> v; std::vector<WORD> i; int kind; };
	std::vector<Grp> grps[NC * NC]; // per cell, in its mesh's group order
	int cur[NC * NC][2];
	for (auto &c : cur) c[0] = c[1] = -1;
	BldgGeom::Out o;
	std::vector<std::array<double, 2>> foot;
	DWORD hidden = 0;
	for (DWORD b = 0; b < m->nb; b++) {
		const TSBLDG &B = m->b[b];
		uint64_t la, ln;
		memcpy (&la, &B.lat, 8); memcpy (&ln, &B.lng, 8);
		BldgGeom::Build (m->vtx + (size_t)B.v0 * 6, B.nv, m->idx + B.i0, B.ni, B.hmin, B.hmax, (uint32_t)Mix (la ^ (ln * 0x9E3779B97F4A7C15ull)), o);
		double g = t.ground[b];
		VECTOR3 ub = Up (B.lat, B.lng), eb = East (B.lng), nb = North (B.lat, B.lng);
		VECTOR3 pb = ub * (R + g) - t.p0;
		if (!near.empty() && o.ring.size() >= 3) { // inside or touching a base's own building: left out
			bool hit = false;
			for (const BaseRec *br : near) {
				foot.clear();
				for (const auto &q : o.ring) {
					VECTOR3 p = pb + t.p0 + eb * q[0] + nb * q[1] - br->p, l = tmul (br->rb, mul (br->rp, p));
					foot.push_back ({l.x, l.z});
				}
				double cx = 0, cz = 0;
				for (size_t k = 0; k < foot.size() && !hit; k++) {
					const auto &a = foot[k], &c = foot[(k + 1) % foot.size()];
					hit = br->idx.Near (a[0], a[1], 6.0) || br->idx.Near (0.5 * (a[0] + c[0]), 0.5 * (a[1] + c[1]), 6.0);
					cx += a[0]; cz += a[1];
				}
				hit = hit || br->idx.Near (cx / foot.size(), cz / foot.size(), 6.0) || br->idx.Inside (foot);
				if (hit) break;
			}
			if (hit) { hidden++; continue; }
		}
		double fx = (B.lng / (2.0 * PI) + 0.5) * n - t.x, fy = (1.0 - std::asinh (std::tan (B.lat)) / PI) * 0.5 * n - t.y;
		int cell = CellOf (fy, NC) * NC + CellOf (fx, NC);
		std::vector<Grp> &gs = grps[cell];
		bool any = false;
		for (int kind = 0; kind < 2; kind++) {
			const std::vector<BldgGeom::V> &vs = kind ? o.rv : o.fv;
			const std::vector<uint32_t> &is = kind ? o.ri : o.fi;
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
			for (uint32_t i : is) G.i.push_back ((WORD)(vn + i));
			t.runs[b].push_back ({(DWORD)cg, vn, (DWORD)vs.size()});
			any = true;
		}
		if (any) t.cell[cell].bldg.push_back (b);
	}
	if (hidden) oapiWriteLogV ("TileStream: buildings %d/%d/%d: %u inside base structures left out", Z, t.x, t.y, (unsigned)hidden);
	t.placed = t.ground;
	bool tex = texFacade && texRoof;
	MATERIAL mat[2] = { // textured: white, the texture is the colour; else the grey of untextured walls and roofs
		{{1, 1, 1, 1}, {1, 1, 1, 1}, {0.06f, 0.06f, 0.06f, 1}, {0, 0, 0, 0}, 10.0f},
		{{1, 1, 1, 1}, {1, 1, 1, 1}, {0.03f, 0.03f, 0.03f, 1}, {0, 0, 0, 0}, 5.0f}};
	if (!tex) {
		mat[0] = {{0.78f, 0.76f, 0.72f, 1.0f}, {0.78f, 0.76f, 0.72f, 1.0f}, {0.1f, 0.1f, 0.1f, 1.0f}, {0, 0, 0, 0}, 10.0f};
		mat[1] = {{0.52f, 0.52f, 0.54f, 1.0f}, {0.52f, 0.52f, 0.54f, 1.0f}, {0.05f, 0.05f, 0.05f, 1.0f}, {0, 0, 0, 0}, 5.0f};
	}
	for (int c = 0; c < NC * NC; c++) {
		Cell &C = t.cell[c];
		delete C.mesh;
		C.mesh = nullptr;
		if (grps[c].empty()) continue;
		MESHHANDLE hMesh = oapiCreateMesh (0, nullptr);
		DWORD mi[2] = {oapiAddMaterial (hMesh, &mat[0]), oapiAddMaterial (hMesh, &mat[1])};
		DWORD ti[2] = {SPEC_DEFAULT, SPEC_DEFAULT};
		if (tex) { ti[0] = oapiAddMeshTexture (hMesh, texFacade); ti[1] = oapiAddMeshTexture (hMesh, texRoof); }
		for (Grp &G : grps[c]) {
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
		std::vector<Grp>().swap (grps[c]);
	}
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

// the SceneFx distance pass: the cells the main pass draws, see-through groups left out by opt 2
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
		VECTOR3 e = mul (rot, t.e0), u = mul (rot, t.u0), n = mul (rot, t.n0), o = mul (rot, t.p0) - cam;
		if (length (o) > range + 6000.0) continue;
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
		VECTOR3 e = mul (rot, t.e0), u = mul (rot, t.u0), n = mul (rot, t.n0), g = mul (rot, t.p0), o = g - cam;
		double d = length (o);
		if (main ? d > range + 6000.0 : env && d > EnvRange + t.rad) continue;
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
