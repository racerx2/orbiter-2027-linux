// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: streamed roads, railways, airports and waterways painted into a terrain tile's way textures, no client types (design-ways)

#ifndef __WAYGEOM_H
#define __WAYGEOM_H

#include "OrbiterAPI.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace WayGeom {

// a z14 way tile as TSWAYTILE holds it
struct Tile {
	struct F { uint8_t kind = 0, flags = 0; int8_t layer = 0; std::string ref; uint32_t p0 = 0, np = 0; };
	int z = 14, x = 0, y = 0;
	std::vector<F> f;
	std::vector<uint32_t> part;   // np + 1: part i is points part[i] to part[i + 1] - 1
	std::vector<int16_t> pt;      // x, y in 4096 units across the tile, y down
	double Lng (double u) const;  // radians
	double Lat (double v) const;
};
std::shared_ptr<const Tile> FromApi (const TSWAYTILE &t);

struct Style {
	double hw = 0.0;      // half width [m]
	float lum = 0.0f;     // surface luminance, the imagery's scale
	float share = 0.0f;   // the imagery's share in its colour
	int order = 0;        // paint order within an OSM layer
	int marks = 0;        // Marks
	bool lit = false;     // street lamps at night
	bool water = false;
	double lamp = 0.0;    // lamp spacing [m]
};
enum Marks { M_NONE, M_ROAD, M_TAXIWAY, M_RUNWAY, M_RAIL };
Style StyleOf (uint8_t kind, uint8_t flags);

// a terrain tile's box and what to paint
struct Input {
	double lat0 = 0, lat1 = 0, lng0 = 0, lng1 = 0; // radians, lat0 south
	double R = 6371010.0;
	int n = 512, nl = 256;                         // way and lamp texture sizes
	bool americas = false;                         // yellow centre and left edge lines, 3 m / 9 m dashes
	std::vector<std::shared_ptr<const Tile>> tiles;
	std::function<bool (double lat, double lng)> blocked; // bases, fixed structures, clear zones; may be empty
};
struct Output {
	bool any = false;   // something painted
	// RGBA8 mip chains, level 0 first, rows from the north
	// way: R paved share, G luminance x R, B yellow paint x R, A water share
	// lamp: sqrt (value / 4) of R pool, G lamp head, B spill; A the imagery's share in the pavement colour
	std::vector<std::vector<uint8_t>> way, lamp;
};
void Raster (const Input &in, Output &out);

// runway pieces in these tiles cut at a tile edge: the z14 tiles holding their next piece, not among them
std::vector<std::pair<int, int>> RunwayTiles (const std::vector<std::shared_ptr<const Tile>> &tiles);

// a runway from its pieces, local metres (x east, y north)
struct Runway {
	double ax = 0, ay = 0, bx = 0, by = 0; // its ends
	bool cutA = false, cutB = false;       // the end lies at a tile edge: not its threshold
	std::string ref;
	double w = 0.0;                        // width [m]
	std::string desA, desB;                // the designators painted at each end ("13L")
};
// 1D box-filter share of a pixel of width p at distance d from the centre of a band of half width h
double Cov1 (double d, double h, double p);
// share of [a - p/2, a + p/2] on dashes of length L every P
double DashCov (double a, double p, double L, double P);
// the end's designator from a ref ("13L/31R") and the bearing of landing from that end (deg)
std::string Designator (const std::string &ref, double bearing);

// no tree on a way surface (TreeMgr)
class Index {
public:
	void Build (const std::vector<std::shared_ptr<const Tile>> &tiles, double lat0, double lat1, double lng0, double lng1, double R);
	bool Covers (double lat, double lng, double margin) const; // within the surface plus margin
private:
	struct Seg { double ax, ay, bx, by, hw; };
	struct Poly { std::vector<std::vector<double>> rings; double x0, y0, x1, y1; };
	double latc = 0, lngc = 0, kx = 1, ky = 1, x0 = 0, y0 = 0;
	int nx = 0, ny = 0;
	static constexpr double Cell = 64.0;
	std::vector<Seg> segs;
	std::vector<Poly> polys;
	std::vector<std::vector<uint32_t>> cells; // segments, then polygons as segs.size() + i
};

} // namespace WayGeom

#endif // !__WAYGEOM_H
