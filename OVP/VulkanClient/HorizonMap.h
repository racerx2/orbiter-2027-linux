// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: terrain horizon maps, 8 azimuths per elevation node (design-ground-graphics 2); no client types

#ifndef __HORIZONMAP_H
#define __HORIZONMAP_H

#include <cstdint>
#include <vector>

namespace Horizon {

const int N = 257;            // nodes per side
const int W = 2 * N;          // texture width: azimuths 0-3, then 4-7
const int STRIDE = 259;       // an elevation grid with its pad
const int NEAR = 32;          // near field, node spacings
const int NG = N + 2 * NEAR;  // near grid side
const int FAR = 8;            // far field on every FAR-th node
const float NODATA = -1e9f;

struct Grid {                 // a tile's elevation grid, row 0 south, metres
	int lvl = 0;              // internal level
	double minlat = 0.0, maxlat = 0.0, minlng = 0.0, maxlng = 0.0;
	std::vector<float> h;     // STRIDE x STRIDE, node (i,j) at (i+1)*STRIDE+j+1
	bool Covers (double lat, double lng, double &fi, double &fj) const; // node coords, pad included
	float At (double fi, double fj) const;                              // bilinear
};

struct Input {
	Grid owner;
	std::vector<Grid> grids;  // the others, any order
	double R = 0.0;           // body radius, m
	double dmax = 30e3;       // far field limit, m
};

struct Map {
	std::vector<uint8_t> px;  // W x N RGBA8, row 0 north
	const uint8_t *Texel (int x, int y) const { return &px[(size_t(y) * W + x) * 4]; }
};

inline uint8_t Encode (double s)
{
	double v = (s + 0.25) / 1.25;
	v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
	return uint8_t(v * 255.0 + 0.5);
}

inline double Decode (double v) { return v / 255.0 * 1.25 - 0.25; }

double DmaxFor (double span);                               // clamp(10 x span, 30 km, 300 km)
void Compute (const Input &in, Map &out);
void Sample (const Map &m, double u, double v, uint8_t out[8]); // u east, v from north, 0..1 over the owner

} // namespace Horizon

#endif // !__HORIZONMAP_H
