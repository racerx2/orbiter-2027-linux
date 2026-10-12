
// not upstream: streamed 3D buildings on a planet's surface (BldgMgr.h)

#include "BldgMgr.h"
#include "VPlanet.h"
#include "Scene.h"
#include "AABBUtil.h"
#include <cmath>

namespace {
const double MaxAlt = 15e3, DropAlt = 20e3, EnvRange = 2e3;
const int Z = 14;
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
}

BldgMgr::BldgMgr (vPlanet *_vp, OBJHANDLE _hObj, const Scene *_scn)
: vp(_vp), hObj(_hObj), scn(_scn)
{
	R = oapiGetSize (hObj);
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
	delete t.mesh;
	t.mesh = nullptr;
}

// the rendered terrain under a point, so the buildings stand on what is drawn
double BldgMgr::Ground (double lat, double lng) const
{
	double e = 0.0;
	if (vp->GetElevation (lng, lat, &e) >= 1) return e;
	return oapiSurfaceElevation (hObj, lng, lat);
}

bool BldgMgr::Near (int x, int y) const
{
	double lat, lng;
	Centre (x, y, lat, lng);
	return R * acos (std::min (1.0, dotp (Up (lat, lng), Up (camLat, camLng)))) <= range + 3000.0;
}

void BldgMgr::Update ()
{
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
			}
			oapiTileStreamBldgFree (got.second); // the D3D9Mesh has its own copy
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
		bool off = false;
		for (int y = y0; y <= y1 && !off; y++)
			for (int xx = x0; xx <= x1 && !off; xx++) {
				int x = xx & ((1 << Z) - 1);
				long long key = (long long)x << 20 | y;
				if (tiles.count (key) || bad.count (key) || busy.count (key)) continue;
				int w = oapiTileStreamBldgWant (hObj, x, y);
				if (w < 0) { off = true; break; } // no buildings for this body, or offline
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
		tGround = t + 0.25;
		auto it = tiles.upper_bound (lastGround);
		if (it == tiles.end()) it = tiles.begin();
		lastGround = it->first;
		Reground (it->second);
	}
}

// the tile's buildings as one mesh in a frame at the tile centre: walls and roofs per cell, <= 65535 vertices a group
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
		t.cellUp[c] = Up (la, ln);
		t.mtrl[c][0] = t.mtrl[c][1] = -1;
		t.vis[c] = 1.0f;
	}
	t.lat.assign (m->nb, 0.0);
	t.lng.assign (m->nb, 0.0);
	t.ground.assign (m->nb, 0.0);
	t.runs.assign (m->nb, {});
	const double n = (double)(1 << Z);
	struct Grp { std::vector<NTVERTEX> v; std::vector<WORD> i; int cell, kind; };
	std::vector<Grp> grps;
	int cur[NC * NC][2];
	for (auto &c : cur) c[0] = c[1] = -1;
	std::vector<std::pair<int, int>> remap; // building vertex: group, index in it
	for (DWORD b = 0; b < m->nb; b++) {
		const TSBLDG &B = m->b[b];
		double g = Ground (B.lat, B.lng);
		t.lat[b] = B.lat; t.lng[b] = B.lng; t.ground[b] = g;
		double fx = (B.lng / (2.0 * PI) + 0.5) * n - t.x, fy = (1.0 - std::asinh (std::tan (B.lat)) / PI) * 0.5 * n - t.y;
		int cell = CellOf (fy, NC) * NC + CellOf (fx, NC);
		for (int k = 0; k < 2; k++) // a building that does not fit starts a new group
			if (cur[cell][k] >= 0 && grps[cur[cell][k]].v.size() + B.nv > 65535) cur[cell][k] = -1;
		VECTOR3 ub = Up (B.lat, B.lng), eb = East (B.lng), nb = North (B.lat, B.lng);
		VECTOR3 pb = ub * (R + g) - t.p0;
		remap.assign (B.nv, {-1, 0});
		for (DWORD k = 0; k + 2 < B.ni; k += 3) {
			const DWORD *tri = m->idx + B.i0 + k;
			int kind = m->vtx[(B.v0 + tri[0]) * 6 + 4] > 0.5f ? 1 : 0;
			int &cg = cur[cell][kind];
			if (cg < 0 || grps[cg].v.size() + 3 > 65535) {
				cg = (int)grps.size();
				grps.push_back ({{}, {}, cell, kind});
			}
			Grp &G = grps[cg];
			for (int c = 0; c < 3; c++) {
				DWORD vi = tri[c];
				if (remap[vi].first != cg) {
					const float *s = m->vtx + (B.v0 + vi) * 6;
					VECTOR3 p = pb + eb * s[0] + ub * s[1] + nb * s[2];
					VECTOR3 nr = eb * s[3] + ub * s[4] + nb * s[5];
					NTVERTEX v;
					v.x = (float)dotp (p, t.e0); v.y = (float)dotp (p, t.u0); v.z = (float)dotp (p, t.n0);
					v.nx = (float)dotp (nr, t.e0); v.ny = (float)dotp (nr, t.u0); v.nz = (float)dotp (nr, t.n0);
					v.tu = v.tv = 0.0f;
					std::vector<Run> &rs = t.runs[b];
					DWORD vn = (DWORD)G.v.size();
					if (!rs.empty() && rs.back().grp == (DWORD)cg && rs.back().v0 + rs.back().nv == vn) rs.back().nv++;
					else rs.push_back ({(DWORD)cg, vn, 1});
					remap[vi] = {cg, (int)vn};
					G.v.push_back (v);
					t.rad = std::max (t.rad, length (p));
				}
				G.i.push_back ((WORD)remap[vi].second);
			}
		}
	}
	delete t.mesh;
	t.mesh = nullptr;
	if (grps.empty()) return;
	MESHHANDLE hMesh = oapiCreateMesh (0, nullptr);
	MATERIAL mat[2] = {
		{{0.78f, 0.76f, 0.72f, 1.0f}, {0.78f, 0.76f, 0.72f, 1.0f}, {0.1f, 0.1f, 0.1f, 1.0f}, {0, 0, 0, 0}, 10.0f},  // wall
		{{0.52f, 0.52f, 0.54f, 1.0f}, {0.52f, 0.52f, 0.54f, 1.0f}, {0.05f, 0.05f, 0.05f, 1.0f}, {0, 0, 0, 0}, 5.0f}}; // roof
	for (Grp &G : grps) {
		int &mi = t.mtrl[G.cell][G.kind];
		if (mi < 0) mi = (int)oapiAddMaterial (hMesh, &mat[G.kind]);
		MESHGROUP mg;
		memset (&mg, 0, sizeof mg);
		mg.Vtx = G.v.data(); mg.nVtx = (DWORD)G.v.size();
		mg.Idx = G.i.data(); mg.nIdx = (DWORD)G.i.size();
		mg.MtrlIdx = (DWORD)mi;
		mg.TexIdx = SPEC_DEFAULT;
		oapiAddMeshGroup (hMesh, &mg);
	}
	t.mesh = new D3D9Mesh (hMesh);
	oapiDeleteMesh (hMesh); // the D3D9Mesh copied it all
	for (int k = 0; k < 2; k++)
		for (int c = 0; c < NC * NC; c++)
			if (t.mtrl[c][k] >= 0) { t.mesh->GetMaterial (&t.base[k], (DWORD)t.mtrl[c][k]); break; }
}

// buildings whose ground moved more than 0.25 m are lifted in the mesh's own vertex copy: one EditGroup a group, one upload
void BldgMgr::Reground (Tile &t)
{
	if (!t.mesh) return;
	std::map<DWORD, std::pair<std::vector<WORD>, std::vector<float>>> ed; // per group: vertices, their lift
	for (size_t b = 0; b < t.ground.size(); b++) {
		double g = Ground (t.lat[b], t.lng[b]), d = g - t.ground[b];
		if (!(std::fabs (d) > 0.25)) continue;
		t.ground[b] = g;
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
		if (t.mesh->GetGroup (p.first, &grs)) continue;
		for (size_t i = 0; i < v.size(); i++) v[i].y += p.second.second[i];
		GROUPEDITSPEC ges;
		memset (&ges, 0, sizeof ges);
		ges.flags = GRPEDIT_VTXCRDY;
		ges.Vtx = v.data(); ges.nVtx = (DWORD)v.size(); ges.vIdx = p.second.first.data();
		t.mesh->EditGroup (p.first, &ges);
	}
}

// each cell's materials carry its own terrain sun: the sun light stays the tile's
void BldgMgr::Sun (Tile &t, const MATRIX3 &rot)
{
	for (int c = 0; c < NC * NC; c++) {
		if (t.mtrl[c][0] < 0 && t.mtrl[c][1] < 0) continue;
		float v = vp->TerrainSunVisibility (vp->GlobalPos() + mul (rot, t.cellUp[c] * R), t.ts[c]); // lifted onto the drawn ground
		bool end = (v == 0.0f || v == 1.0f) && v != t.vis[c];
		if (!end && std::fabs (v - t.vis[c]) <= 0.02f) continue;
		t.vis[c] = v;
		for (int k = 0; k < 2; k++) {
			if (t.mtrl[c][k] < 0) continue;
			D3D9MatExt mt = t.base[k];
			mt.Diffuse.x *= v; mt.Diffuse.y *= v; mt.Diffuse.z *= v;
			mt.Specular.x *= v; mt.Specular.y *= v; mt.Specular.z *= v;
			t.mesh->SetMaterial (&mt, (DWORD)t.mtrl[c][k], false);
		}
	}
}

void BldgMgr::Render (VkDev *dev)
{
	DWORD pass = scn->GetRenderPass();
	bool main = pass == RENDERPASS_MAINSCENE, env = pass == RENDERPASS_ENVCAM;
	if (tiles.empty() || !(main || env || pass == RENDERPASS_CUSTOMCAM)) return;
	if (!main && !(scn->GetRenderFlags() & 0x20)) return; // with the base structures: env maps in EnvMapMode 2
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 cam = vp->CameraPos(); // this pass's camera
	D3DXVECTOR4 F = D9LinearFieldOfView (scn->GetProjectionMatrix());
	for (auto &p : tiles) {
		Tile &t = p.second;
		if (!t.mesh) continue;
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
		if (!D9IsAABBVisible (t.mesh->GetAABB(), &WV, &F)) continue; // culled by this pass's camera
		if (main) Sun (t, rot); // no updates from the other passes
		D3D9Sun sp = vp->GetObjectAtmoParams (g);
		t.mesh->SetSunLight (&sp);
		t.mesh->Render (&W, RENDER_BASE);
	}
}
