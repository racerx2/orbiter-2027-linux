// GPSNav: the map's terrain
#include "Terrain.h"
#include "Colour.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>

using namespace gps;

Terrain::Terrain () { cache = InitTileCache (64); }
Terrain::~Terrain () { if (cache) ReleaseTileCache (cache); }

void Terrain::Reset ()
{
	cur = Grid (), next = Grid ();
	done = -1;
}

// a new grid when the view has moved an eighth of itself, the range or track up changed, or 5 s on (tiles streamed in since)
bool Terrain::Need (const View &v, double sys) const
{
	if (done >= 0) return false;
	if (!cur.nx || cur.body != v.body) return true;
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
	next.body = v.body, next.pos = v.pos, next.k = v.k, next.up = v.up, next.t = sys;
	next.nx = (int)std::ceil (W / cell) + 1, next.ny = (int)std::ceil (H / cell) + 1;
	int n = next.nx * next.ny;
	next.p.resize (n), next.u.resize (3 * n), next.e.assign (n, 0.0f), next.ok.assign (n, 0), next.shade.assign (n, 1.0f);
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
	done = 0;
	began = oapiGetSysTime ();
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
	if (logged < 3 && ++logged) oapiWriteLogV ("GPSNav: terrain %dx%d points, level %d, %.0f m apart, %.2f s%s", g.nx, g.ny, level, cellM, oapiGetSysTime () - began, g.any ? "" : ", no elevation data");
	cur = std::move (next);
	next = Grid ();
	done = -1;
}

bool Terrain::Draw (ImDrawList *dl, const View &v, Mode mode, double alt, bool sea, double sys)
{
	if (Need (v, sys)) Start (v, sys);
	if (done >= 0) Sample ();
	const Grid &g = cur;
	if (!g.nx || g.body != v.body || !g.any) return false;
	int n = g.nx * g.ny;
	ImVec2 uv = ImGui::GetFontTexUvWhitePixel ();
	dl->PrimReserve ((g.nx - 1) * (g.ny - 1) * 6, n);
	unsigned base = dl->_VtxCurrentIdx;
	Local f (v.pos); // the map's projection about the vessel, without a haversine and a bearing a point
	double cu = std::cos (v.up), su = std::sin (v.up), rk = v.R * v.k;
	for (int q = 0; q < n; q++) {
		double ang, sb, cb;
		f.Polar (&g.u[3 * q], ang, sb, cb);
		double d = ang * rk;
		ImVec2 p (v.ox + (float)((sb * cu - cb * su) * d), v.oy - (float)((cb * cu + sb * su) * d)); // the bearing less up
		ImU32 c;
		if (!g.ok[q]) c = IM_COL32 (3, 9, 26, 255);
		else if (mode == WARN) c = Scale (Warn (g.e[q], alt, sea, g.lo, g.hi), g.shade[q]);
		else if (sea && g.e[q] <= 0.0f) c = Topo (g.e[q], sea, g.lo, g.hi); // a calm sea, unshaded
		else c = Scale (Topo (g.e[q], sea, g.lo, g.hi), g.shade[q]);
		dl->PrimWriteVtx (p, uv, c);
	}
	for (int j = 0; j + 1 < g.ny; j++)
		for (int i = 0; i + 1 < g.nx; i++) {
			unsigned a = base + j * g.nx + i, b = a + 1, c = a + g.nx + 1, d = a + g.nx;
			dl->PrimWriteIdx ((ImDrawIdx)a), dl->PrimWriteIdx ((ImDrawIdx)b), dl->PrimWriteIdx ((ImDrawIdx)c);
			dl->PrimWriteIdx ((ImDrawIdx)a), dl->PrimWriteIdx ((ImDrawIdx)c), dl->PrimWriteIdx ((ImDrawIdx)d);
		}
	return true;
}
