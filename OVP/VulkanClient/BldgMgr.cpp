
// not upstream: streamed 3D buildings on a planet's surface (BldgMgr.h)

#include "BldgMgr.h"
#include "VPlanet.h"
#include "Scene.h"
#include <cmath>

namespace {
const double MaxAlt = 15e3, DropAlt = 20e3;
const int Z = 14;

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

void Centre (int x, int y, double &lat, double &lng)
{
	double n = (double)(1 << Z);
	lng = (x + 0.5) / n * 2.0 * PI - PI;
	lat = std::atan (std::sinh (PI * (1.0 - 2.0 * (y + 0.5) / n)));
}
}

BldgMgr::BldgMgr (vPlanet *_vp, OBJHANDLE _hObj, const Scene *_scn)
: vp(_vp), hObj(_hObj), scn(_scn)
{
	R = oapiGetSize (hObj);
}

BldgMgr::~BldgMgr ()
{
	for (auto &p : tiles) Drop (p.second);
}

void BldgMgr::Drop (Tile &t)
{
	delete t.mesh;
	t.mesh = nullptr;
	if (t.hMesh) oapiDeleteMesh (t.hMesh);
	t.hMesh = nullptr;
	if (t.src) oapiTileStreamBldgFree (t.src);
	t.src = nullptr;
}

// the rendered terrain under a point, so the buildings stand on what is drawn
double BldgMgr::Ground (double lat, double lng) const
{
	double e = 0.0;
	if (vp->GetElevation (lng, lat, &e) >= 1) return e;
	return oapiSurfaceElevation (hObj, lng, lat);
}

void BldgMgr::Update ()
{
	VECTOR3 cam = vp->CameraPos();
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 loc = tmul (rot, cam);
	double r = length (loc), alt = r - R;
	if (alt > DropAlt) {
		for (auto &p : tiles) Drop (p.second);
		tiles.clear();
		return;
	}
	camLat = asin (loc.y / r);
	camLng = atan2 (loc.z, loc.x);
	range = 3000.0 + std::min (alt, MaxAlt) * 0.2;
	double t = oapiGetSysTime();
	bool load = true;
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
				if (tiles.count (key) || bad.count (key)) continue;
				int w = oapiTileStreamBldgWant (hObj, x, y);
				if (w < 0) { off = true; break; } // no buildings for this body, or offline
				if (w == 0 || !load) continue;
				Tile T;
				T.x = x; T.y = y;
				if (!(T.src = oapiTileStreamBldgLoad (hObj, x, y))) { bad.insert (key); continue; } // unreadable: skipped this session
				load = false; // one tile a pass: the mesh build is on the render thread
				Make (T, false);
				tiles[key] = T;
			}
	}
	for (auto it = tiles.begin(); it != tiles.end(); ) {
		double lat, lng;
		Centre (it->second.x, it->second.y, lat, lng);
		double d = R * acos (std::min (1.0, dotp (Up (lat, lng), Up (camLat, camLng))));
		if (d > range + 3000.0) {
			Drop (it->second);
			it = tiles.erase (it);
		} else ++it;
	}
	if (t >= tGround && !tiles.empty()) { // the terrain under them refines as the camera comes down: one tile a pass
		tGround = t + 0.25;
		auto it = tiles.upper_bound (lastGround);
		if (it == tiles.end()) it = tiles.begin();
		lastGround = it->first;
		Make (it->second, true);
	}
}

// the tile's buildings as one mesh in a frame at the tile centre: walls and roofs, <= 65535 vertices a group
void BldgMgr::Make (Tile &t, bool reground)
{
	const TSBLDGMESH *m = t.src;
	std::vector<double> g (m->nb);
	bool moved = !reground || t.ground.size() != m->nb;
	for (DWORD i = 0; i < m->nb; i++) {
		g[i] = Ground (m->b[i].lat, m->b[i].lng);
		if (!moved && std::fabs (g[i] - t.ground[i]) > 0.25) moved = true;
	}
	if (!moved) return;
	double lat0, lng0;
	Centre (t.x, t.y, lat0, lng0);
	t.u0 = Up (lat0, lng0); t.e0 = East (lng0); t.n0 = North (lat0, lng0);
	t.p0 = t.u0 * R;
	t.ground = g;
	struct Grp { std::vector<NTVERTEX> v; std::vector<WORD> i; int kind; };
	std::vector<Grp> grps;
	int cur[2] = {-1, -1};
	std::vector<int> remap;
	for (DWORD b = 0; b < m->nb; b++) {
		const TSBLDG &B = m->b[b];
		VECTOR3 ub = Up (B.lat, B.lng), eb = East (B.lng), nb = North (B.lat, B.lng);
		VECTOR3 pb = ub * (R + g[b]) - t.p0;
		remap.assign (B.nv, -1);
		for (DWORD k = 0; k + 2 < B.ni; k += 3) {
			const DWORD *tri = m->idx + B.i0 + k;
			int kind = m->vtx[(B.v0 + tri[0]) * 6 + 4] > 0.5f ? 1 : 0;
			if (cur[kind] < 0 || grps[cur[kind]].v.size() + B.nv > 65535) {
				cur[kind] = (int)grps.size();
				grps.push_back ({{}, {}, kind});
				remap.assign (B.nv, -1); // a new group: this building's vertices start again
			}
			Grp &G = grps[cur[kind]];
			for (int c = 0; c < 3; c++) {
				DWORD vi = tri[c];
				if (remap[vi] < 0 || remap[vi] >= (int)G.v.size()) {
					const float *s = m->vtx + (B.v0 + vi) * 6;
					VECTOR3 p = pb + eb * s[0] + ub * s[1] + nb * s[2];
					VECTOR3 n = eb * s[3] + ub * s[4] + nb * s[5];
					NTVERTEX v;
					v.x = (float)dotp (p, t.e0); v.y = (float)dotp (p, t.u0); v.z = (float)dotp (p, t.n0);
					v.nx = (float)dotp (n, t.e0); v.ny = (float)dotp (n, t.u0); v.nz = (float)dotp (n, t.n0);
					v.tu = v.tv = 0.0f;
					remap[vi] = (int)G.v.size();
					G.v.push_back (v);
				}
				G.i.push_back ((WORD)remap[vi]);
			}
		}
	}
	delete t.mesh;
	t.mesh = nullptr;
	if (t.hMesh) oapiDeleteMesh (t.hMesh);
	t.hMesh = nullptr;
	if (grps.empty()) return;
	t.hMesh = oapiCreateMesh (0, nullptr);
	MATERIAL wall = {{0.78f, 0.76f, 0.72f, 1.0f}, {0.78f, 0.76f, 0.72f, 1.0f}, {0.1f, 0.1f, 0.1f, 1.0f}, {0, 0, 0, 0}, 10.0f};
	MATERIAL roof = {{0.52f, 0.52f, 0.54f, 1.0f}, {0.52f, 0.52f, 0.54f, 1.0f}, {0.05f, 0.05f, 0.05f, 1.0f}, {0, 0, 0, 0}, 5.0f};
	DWORD mw = oapiAddMaterial (t.hMesh, &wall), mr = oapiAddMaterial (t.hMesh, &roof);
	for (Grp &G : grps) {
		MESHGROUP mg;
		memset (&mg, 0, sizeof mg);
		mg.Vtx = G.v.data(); mg.nVtx = (DWORD)G.v.size();
		mg.Idx = G.i.data(); mg.nIdx = (DWORD)G.i.size();
		mg.MtrlIdx = G.kind ? mr : mw;
		mg.TexIdx = SPEC_DEFAULT;
		oapiAddMeshGroup (t.hMesh, &mg);
	}
	t.mesh = new D3D9Mesh (t.hMesh);
}

void BldgMgr::Render (VkDev *dev)
{
	if (tiles.empty() || scn->GetRenderPass() != RENDERPASS_MAINSCENE) return;
	MATRIX3 rot;
	oapiGetRotationMatrix (hObj, &rot);
	VECTOR3 cam = vp->CameraPos();
	for (auto &p : tiles) {
		Tile &t = p.second;
		if (!t.mesh) continue;
		VECTOR3 e = mul (rot, t.e0), u = mul (rot, t.u0), n = mul (rot, t.n0), g = mul (rot, t.p0), o = g - cam;
		if (length (o) > range + 6000.0) continue;
		D3DXMATRIX W;
		D3DXMatrixIdentity (&W);
		W._11 = (float)e.x; W._12 = (float)e.y; W._13 = (float)e.z;
		W._21 = (float)u.x; W._22 = (float)u.y; W._23 = (float)u.z;
		W._31 = (float)n.x; W._32 = (float)n.y; W._33 = (float)n.z;
		W._41 = (float)o.x; W._42 = (float)o.y; W._43 = (float)o.z;
		D3D9Sun sp = vp->GetObjectAtmoParams (g);
		t.mesh->SetSunLight (&sp);
		t.mesh->Render (&W, RENDER_BASE);
	}
}
