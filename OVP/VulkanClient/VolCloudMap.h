// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: volumetric cloud coverage map and noise, pure maths (design-ground-graphics 4.2, 4.3)

#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace VolCloud {

const int MAPN = 256;			// map texels per side
const double MAPHALF = 100e3;	// m, half the map side
const int DENSN = 256;			// density texels per cloud tile side

double CloudLng (double lng, double cloudrot);	// planet longitude to the cloud layer's, wrapped to -pi..pi

// tangent plane at a centre on the unit sphere (planet-local axes: x = cos lat cos lng, y = sin lat, z = cos lat sin lng)
struct Frame {
	double lat = 0.0, lng = 0.0;
	double U[3], E[3], N[3];	// up, east, north
	void Set (double lat, double lng);
	void ToMap (double lat, double lng, double R, double *x, double *y) const;	// gnomonic, m
	void FromMap (double x, double y, double R, double *lat, double *lng) const;
};

bool NeedRebuild (double camx, double camy, bool valid);	// camera position on the current map, m

struct Src {	// one cloud tile's density, row 0 north
	int lvl = 0;
	double minlat = 0, maxlat = 0, minlng = 0, maxlng = 0;
	std::shared_ptr<const std::vector<uint8_t>> d;
	bool Covers (double lat, double lng, double *u, double *v) const;	// texel coordinates, lng wrapped
	float At (double u, double v) const;									// bilinear, 0..1
};

void Fill (const Frame &f, double R, std::vector<Src> srcs, std::vector<uint8_t> &map); // MAPN x MAPN, row 0 north

// tileable noise volumes, RGBA8, n^3 texels (x fastest)
void ShapeNoise (int n, std::vector<uint8_t> &rgba);	// R Perlin-Worley, G B A Worley at 4, 8, 16 cells
void DetailNoise (int n, std::vector<uint8_t> &rgba);	// R G B Worley at 4, 8, 16 cells, A 255

} // namespace VolCloud
