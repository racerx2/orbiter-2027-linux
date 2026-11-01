// GPSNav: the map's terrain
#include "Terrain.h"
#include "Colour.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

using namespace gps;

namespace {
const size_t MaxMapTiles = 36; // a grid's map tiles at its zoom; more and the zoom goes down
}

Terrain::Terrain () { cache = InitTileCache (64); }
Terrain::~Terrain () { if (cache) ReleaseTileCache (cache); }

void Terrain::Reset ()
{
	if (mapUsed) oapiWriteLogV ("GPSNav: map: %s", map.Summary ().c_str ());
	map.Stop ();
	mapUsed = false;
	cur = Grid (), next = Grid ();
	done = -1;
}

// a new grid when the view has moved an eighth of itself, the range or track up changed, or 5 s on (tiles streamed in since)
bool Terrain::Need (const View &v, double sys) const
{
	if (done >= 0) return false;
	if (!cur.nx || cur.body != v.body || cur.osm != v.osm) return true;
	double w = v.x1 - v.x0, h = v.y1 - v.y0, r = v.k / cur.k;
	if (Angle (cur.pos, v.pos) * v.R * v.k > 0.125 * std::min (w, h)) return true;
	if (r < 0.8 || r > 1.25 || std::fabs (WrapPi (v.up - cur.up)) > 10.0 * gps::RAD) return true;
	return sys - cur.t > 5.0;
}

void Terrain::Start (const View &v, double sys)
{
	float w = v.x1 - v.x0, h = v.y1 - v.y0, mx = 0.15f * w, my = 0.15f * h;
	float W = w + 2.0f * mx, H = h + 2.0f * my, cell = std::max (3.0f, std::sqrt (W * H / 24000.0f));
	next = Grid ();
	next.body = v.body, next.pos = v.pos, next.k = v.k, next.up = v.up, next.t = sys, next.osm = v.osm;
	next.nx = (int)std::ceil (W / cell) + 1, next.ny = (int)std::ceil (H / cell) + 1;
	int n = next.nx * next.ny;
	next.p.resize (n), next.u.resize (3 * n), next.e.assign (n, 0.0f), next.ok.assign (n, 0), next.shade.assign (n, 1.0f), next.w.assign (n, -1);
	for (int j = 0; j < next.ny; j++)
		for (int i = 0; i < next.nx; i++) { // the screen point's place: back through the map's projection
			int q = j * next.nx + i;
			double dx = v.x0 - mx + i * cell - v.ox, dy = v.oy - (v.y0 - my + j * cell);
			LatLon p = next.p[q] = Destination (v.pos, std::atan2 (dx, dy) + v.up, std::hypot (dx, dy) / v.k / v.R);
			Local::Unit (p, &next.u[3 * q]);
		}
	cellM = cell / v.k;
	double tiles = gps::PI * v.R / (256.0 * cellM); // tiles down a meridian at the grid's spacing (256 points a tile)
	level = 7 + std::clamp ((int)std::ceil (std::log2 (std::max (tiles, 1.0))), 0, 18); // the API's level: that tile depth + 7 (ElevationManager)
	if (v.osm) MapStart (next, v);
	done = 0;
	began = oapiGetSysTime ();
}

// the finest zoom whose texels are no wider than the grid's spacing, at most MaxMapTiles under it; the tiles two zooms up asked first
void Terrain::MapStart (Grid &g, const View &v)
{
	double c = std::max (std::cos (v.pos.lat), 1e-3);
	int z = std::clamp ((int)std::ceil (std::log2 (std::max (2.0 * gps::PI * v.R * c / (map::Raster * cellM), 1.0))), 0, map::MaxZoom);
	size_t n = g.p.size ();
	std::unordered_set<uint64_t> s;
	for (;; z--) {
		s.clear ();
		for (size_t q = 0; q < n && s.size () <= MaxMapTiles; q++) {
			double tx, ty;
			if (map::TileXY (g.p[q], z, tx, ty)) s.insert (map::Pack (z, (int)tx, (int)ty));
		}
		if (s.size () <= MaxMapTiles || z == 0) break;
	}
	g.mz = z;
	g.mt.assign (2 * n, NAN);
	for (size_t q = 0; q < n; q++) map::TileXY (g.p[q], z, g.mt[2 * q], g.mt[2 * q + 1]);
	double ox = 0.0, oy = 0.0;
	map::TileXY (v.pos, z, ox, oy);
	auto dist = [ox, oy] (uint64_t k) { return std::hypot (map::KeyX (k) + 0.5 - ox, map::KeyY (k) + 0.5 - oy); };
	g.keys.assign (s.begin (), s.end ());
	std::sort (g.keys.begin (), g.keys.end (), [&dist] (uint64_t a, uint64_t b) { return dist (a) < dist (b); });
	std::vector<uint64_t> want;
	int up = std::min (2, z);
	for (uint64_t k : g.keys) {
		uint64_t p = map::Pack (z - up, map::KeyX (k) >> up, map::KeyY (k) >> up);
		if (std::find (want.begin (), want.end (), p) == want.end ()) want.push_back (p);
	}
	if (up) want.insert (want.end (), g.keys.begin (), g.keys.end ());
	map.Want (want, v.download);
	mapUsed = true;
}

// each point's water from its tile at mz, else the nearest ancestor come; the shores of the tiles under the grid likewise
void Terrain::MapWater (Grid &g)
{
	g.gen = map.Generation ();
	std::unordered_map<uint64_t, std::shared_ptr<const map::WaterTile>> got;
	auto Find = [&] (int x, int y) -> std::shared_ptr<const map::WaterTile> {
		for (int d = 0; d <= g.mz; d++) {
			uint64_t k = map::Pack (g.mz - d, x >> d, y >> d);
			auto it = got.find (k);
			if (it == got.end ()) it = got.emplace (k, map.Get (k)).first;
			if (it->second) return it->second;
		}
		return nullptr;
	};
	int lx = -1, ly = -1;
	std::shared_ptr<const map::WaterTile> t;
	double f = 1.0;
	for (size_t q = 0; q < g.w.size (); q++) {
		double tx = g.mt[2 * q], ty = g.mt[2 * q + 1];
		if (!std::isfinite (tx)) { g.w[q] = -1; continue; }
		if ((int)tx != lx || (int)ty != ly) lx = (int)tx, ly = (int)ty, t = Find (lx, ly), f = t ? std::ldexp (1.0, t->z - g.mz) : 1.0;
		g.w[q] = t ? (signed char)t->Water (tx * f - t->x, ty * f - t->y) : -1;
	}
	g.shores.clear ();
	g.covered = !g.keys.empty ();
	for (uint64_t k : g.keys) {
		int x = map::KeyX (k), y = map::KeyY (k);
		auto a = Find (x, y);
		if (!a) { g.covered = false; continue; }
		double s = std::ldexp (1.0, a->z - g.mz);
		g.shores.push_back ({a, (float)(x * s - a->x), (float)(y * s - a->y), (float)((x + 1) * s - a->x), (float)((y + 1) * s - a->y)});
	}
}

void Terrain::Sample ()
{
	auto t0 = std::chrono::steady_clock::now ();
	int n = (int)next.p.size ();
	while (done < n) {
		int l = 0;
		next.e[done] = (float)oapiSurfaceElevationEx (next.body, next.p[done].lng, next.p[done].lat, level, cache, nullptr, &l);
		next.ok[done] = l >= 1;
		if ((++done & 31) == 0 && std::chrono::steady_clock::now () - t0 > std::chrono::milliseconds (4)) break;
	}
	if (done >= n) Finish ();
}

// the heights' spread (the 2nd to the 98th hundredth) and the hill shading, light from the screen's upper left
void Terrain::Finish ()
{
	Grid &g = next;
	std::vector<float> v;
	for (size_t q = 0; q < g.e.size (); q++) if (g.ok[q]) v.push_back (g.e[q]);
	g.any = !v.empty ();
	if (g.any) {
		size_t a = v.size () / 50, b = v.size () - 1 - v.size () / 50;
		std::nth_element (v.begin (), v.begin () + a, v.end ()), g.lo = v[a];
		std::nth_element (v.begin (), v.begin () + b, v.end ()), g.hi = std::max ((double)v[b], g.lo + 1.0);
	}
	double V = std::clamp (std::sqrt (cellM / 30.0), 1.0, 25.0); // more relief over larger areas
	auto E = [&] (int i, int j, float here) {
		int q = std::clamp (j, 0, g.ny - 1) * g.nx + std::clamp (i, 0, g.nx - 1);
		return g.ok[q] ? g.e[q] : here;
	};
	for (int j = 0; j < g.ny; j++)
		for (int i = 0; i < g.nx; i++) {
			int q = j * g.nx + i;
			if (!g.ok[q]) continue;
			float e = g.e[q];
			double sx = (std::min (i + 1, g.nx - 1) - std::max (i - 1, 0)) * cellM, sy = (std::min (j + 1, g.ny - 1) - std::max (j - 1, 0)) * cellM;
			double gx = (E (i + 1, j, e) - E (i - 1, j, e)) / sx, gy = (E (i, j + 1, e) - E (i, j - 1, e)) / sy; // screen x right, y down
			double nx = -V * gx, ny = -V * gy, len = std::sqrt (nx * nx + ny * ny + 1.0);
			double s = (-0.5 * nx - 0.5 * ny + 0.7071) / len; // the normal on the light (-0.5, -0.5, 0.7071)
			g.shade[q] = (float)std::clamp (0.3 + 0.95 * s, 0.3, 1.3);
		}
	if (g.osm) MapWater (g);
	if (logged < 3 && ++logged) {
		char m[96] = "";
		if (g.osm) snprintf (m, sizeof m, "; map zoom %d, %zu tiles, %s", g.mz, g.keys.size (), g.covered ? "all come" : "some to come");
		oapiWriteLogV ("GPSNav: terrain %dx%d points, level %d, %.0f m apart, %.2f s%s%s", g.nx, g.ny, level, cellM, oapiGetSysTime () - began, g.any ? "" : ", no elevation data", m);
	}
	cur = std::move (next);
	next = Grid ();
	done = -1;
}

bool Terrain::Draw (ImDrawList *dl, const View &v, Mode mode, double alt, bool sea, double sys, unsigned shoreCol, bool *shore)
{
	if (shore) *shore = false;
	if (Need (v, sys)) Start (v, sys);
	if (done >= 0) Sample ();
	Grid &g = cur;
	if (!g.nx || g.body != v.body || !g.any) return false;
	if (g.osm && g.gen != map.Generation ()) MapWater (g); // tiles have come
	int n = g.nx * g.ny;
	ImVec2 uv = ImGui::GetFontTexUvWhitePixel ();
	dl->PrimReserve ((g.nx - 1) * (g.ny - 1) * 6, n);
	unsigned base = dl->_VtxCurrentIdx;
	Local f (v.pos); // the map's projection about the vessel, without a haversine and a bearing a point
	double cu = std::cos (v.up), su = std::sin (v.up), rk = v.R * v.k;
	auto Proj = [&] (const double *u) {
		double ang, sb, cb;
		f.Polar (u, ang, sb, cb);
		double d = ang * rk;
		return ImVec2 (v.ox + (float)((sb * cu - cb * su) * d), v.oy - (float)((cb * cu + sb * su) * d)); // the bearing less up
	};
	for (int q = 0; q < n; q++) {
		int w = g.osm ? g.w[q] : -1;
		ImU32 c;
		if (!g.ok[q]) c = IM_COL32 (3, 9, 26, 255);
		else if (mode == WARN) c = Scale (Warn (g.e[q], alt, sea, g.lo, g.hi, w), g.shade[q]);
		else if (sea && IsWater (g.e[q], w)) c = Topo (g.e[q], sea, g.lo, g.hi, w); // a calm sea, unshaded
		else c = Scale (Topo (g.e[q], sea, g.lo, g.hi, w), g.shade[q]);
		dl->PrimWriteVtx (Proj (&g.u[3 * q]), uv, c);
	}
	for (int j = 0; j + 1 < g.ny; j++)
		for (int i = 0; i + 1 < g.nx; i++) {
			unsigned a = base + j * g.nx + i, b = a + 1, c = a + g.nx + 1, d = a + g.nx;
			dl->PrimWriteIdx ((ImDrawIdx)a), dl->PrimWriteIdx ((ImDrawIdx)b), dl->PrimWriteIdx ((ImDrawIdx)c);
			dl->PrimWriteIdx ((ImDrawIdx)a), dl->PrimWriteIdx ((ImDrawIdx)c), dl->PrimWriteIdx ((ImDrawIdx)d);
		}
	if (g.osm && g.covered) { // the map's shores over the colours, each tile's in the part of it under the grid
		for (const Shore &s : g.shores) {
			const std::vector<float> &u = s.t->shore, &m = s.t->mid;
			for (size_t i = 0; 2 * i + 1 < m.size () && 6 * i + 5 < u.size (); i++) {
				if (m[2 * i] < s.x0 || m[2 * i] >= s.x1 || m[2 * i + 1] < s.y0 || m[2 * i + 1] >= s.y1) continue;
				double a[3] = {u[6 * i], u[6 * i + 1], u[6 * i + 2]}, b[3] = {u[6 * i + 3], u[6 * i + 4], u[6 * i + 5]};
				dl->AddLine (Proj (a), Proj (b), (ImU32)shoreCol, 1.2f);
			}
		}
		if (shore) *shore = true;
	}
	return true;
}
