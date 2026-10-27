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

struct Tree { double lat, lng; float h, yaw; uint8_t kind, tint0, tint1, lvl = 0; }; // lvl: not upstream (design-far-forest)

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

// the trees in a lat/lng box, deterministic per lattice cell; not upstream (design-far-forest): only the candidates of level or more
void Place (double lat0, double lat1, double lng0, double lng1, const Params &p, const ClassFn &cls, const GroundFn &ground, const BlockFn &blocked, std::vector<Tree> &out, int level = 0);

// not upstream (design-far-forest): a candidate's level on the 4 m grid (column X, row Y), the trailing zero bits of both, at most MaxLevel
const int MaxLevel = 8;
int Level (int64_t X, int64_t Y);
// the share of a level's trees drawn at lambda = log2 (d / D0); Tree.glsl does the same
double DrawShare (int lvl, double lambda);

// not upstream (design-far-forest): the cells, level m an L(16 - m) tile in 16 x 16, row 0 at the north pole
namespace Quad {
long long Key (int m, long long row, long long col);
int Lvl (long long k);
double Span (int m);
void Bounds (long long k, double &lat0, double &lat1, double &lng0, double &lng1);
long long Parent (long long k); // -1 above MaxLevel
void Children (long long k, long long c[4]);
double Near (long long k, double lat, double lng, double R); // ground distance from a point to the cell, m
struct Want { double d; long long key; };
// the leaves within far: split while closer than D0 x 2^m, with heff of height; nearest first
void Select (double lat, double lng, double heff, double D0, double far, double R, std::vector<Want> &out);
// what to draw: a leaf placed, else its nearest placed ancestor, else its placed descendants; state 0 absent, 1 not placed, 2 placed
void DrawSet (const std::vector<long long> &want, const std::function<int(long long)> &state, std::vector<long long> &out);
} // namespace Quad

// meshes of unit height, y up; part 0 bark, 1 leaves; ao baked 0..1
struct Vtx { float x, y, z, nx, ny, nz, part, ao; };
void Mesh (int kind, std::vector<Vtx> &v, std::vector<uint16_t> &idx);
void Impostor (std::vector<Vtx> &v, std::vector<uint16_t> &idx);	// the far quad, x -0.5..0.5, y 0..1, part 2

} // namespace TreeGeom
