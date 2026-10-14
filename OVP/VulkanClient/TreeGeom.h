// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: tree placement on the land class lattice and the tree meshes, pure maths (design-ground-graphics 5.3, 5.5)

#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace TreeGeom {

const double P = 16384.0;		// the shader's lattice period, cells
enum Kind { SPRUCE = 0, PINE, BROAD1, BROAD2, BUSH, NKIND };

double LatticeC (double R);		// lattice cells per radian: 2^round(log2(pi R / 8)) / pi
void Lattice (double lat, double lng, double c, double *px, double *py);	// NewPlanet.glsl's class lattice point, mod P
float ClsNoise (float px, float py, float period);	// NewPlanet.glsl ClsNoise (ClsHash: Dave Hoskins, "Hash without Sine", MIT)
float Canopy (double px, double py);				// 0..1 canopy clumps from the shader's n1

struct Class { float r = 0, g = 0, b = 0; bool water = false; };	// a Class tile texel and the Mask's water

struct Tree { double lat, lng; float h, yaw; uint8_t kind, tint0, tint1; };

struct Params {
	double R = 6371e3;			// planet radius, m
	double perHa = 130.0;		// trees per hectare where the class is all trees and the canopy average
	float density = 1.0f;		// TreeDensity
};

using ClassFn = std::function<bool(double lat, double lng, Class *c)>;	// false: no data there
using GroundFn = std::function<double(double lat, double lng)>;		// m above the radius
using BlockFn = std::function<bool(double lat, double lng)>;			// footprints, bases, slopes

// a cell's ground: n x n samples over a lat/lng box, row 0 at lat0, bilinear; slope as rise over run
struct Ground {
	int n = 0;
	double lat0 = 0, lat1 = 0, lng0 = 0, lng1 = 0, R = 6371e3;
	std::vector<double> h;
	double At (double lat, double lng, double *slope = nullptr) const;
};

// a footprint ring, x east, z north, m: inside it or within margin of an edge
bool InRing (const std::vector<std::array<double, 2>> &ring, double x, double z, double margin);

// the trees in a lat/lng box, deterministic per lattice cell
void Place (double lat0, double lat1, double lng0, double lng1, const Params &p, const ClassFn &cls, const GroundFn &ground, const BlockFn &blocked, std::vector<Tree> &out);

// meshes of unit height, y up; part 0 bark, 1 leaves; ao baked 0..1
struct Vtx { float x, y, z, nx, ny, nz, part, ao; };
void Mesh (int kind, std::vector<Vtx> &v, std::vector<uint16_t> &idx);
void Impostor (std::vector<Vtx> &v, std::vector<uint16_t> &idx);	// the far quad, x -0.5..0.5, y 0..1, part 2

} // namespace TreeGeom
