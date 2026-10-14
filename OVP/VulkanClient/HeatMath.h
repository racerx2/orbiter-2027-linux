// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: re-entry hull heating maths (design-reentry-heat): stagnation flux, local heating ratio, wall temperature with lag, blackbody colour

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace HeatMath {

constexpr double HPI = 3.14159265358979323846;
constexpr double SIGMA = 5.670374419e-8;		// [P] Stefan-Boltzmann, W/m2/K4
constexpr double LREF = 1.27e5 / HPI;			// [P] cd/m2 of a white Lambertian surface in full sun: scene value 1
constexpr int NBIN = 8;							// heating levels with their own wall temperature

struct Tune {
	static constexpr double camera = 2.0;		// [V] glow gain over the physical luminance
	static constexpr double Tref = 1500.0;		// [D] the lag is the e-folding time at this temperature
	static constexpr double lee = 0.005;		// [U] lee heating share of the stagnation flux
	static constexpr double epsVisMin = 0.2;	// [V] floor of the visible emissivity
	static constexpr double G0 = 2.0;			// [D] hottest bin
	static constexpr double Gr = 0.4;			// [D] ratio between bins
	static constexpr double glowOn = 650.0;		// [V] K: hottest bin from which the glow is drawn
	static constexpr double edge = 1.2;			// [V] cap of the edge level over stagnation (an edge of R <= R_n/3: sqrt(R_n/2R) >= 1.2)
	static constexpr double edgeTurn = 80.0;	// [D] deg of turning within edgeRho that make a real edge: both Newtonian sonic points (about 40 deg) on the feature
	static constexpr double edgeDelta = 2.0;	// [V] deg, the smallest turn of a candidate edge
	static constexpr double edgeExpose = 0.25;	// [V] share of hemisphere rays that escape: an exposed face
	static constexpr double edgeRho = 0.015;	// [V] neighbourhood radius per metre of mesh size (finds edges up to about 1.5 % of it in radius)
	static constexpr double edgeBeta = 15.0;	// [V] deg: an attachment line off the feature switches its edge heating off over this angle
	static constexpr double edgeSonic = 20.0;	// [D] deg from the end of the feature's turn below which the sonic line sits on the face beyond: edge off, full at edgeTurn/2
	static constexpr double edgeBand = 0.2;		// [V] floor of the band half-width over R_n
	static constexpr double edgeRmin = 0.05;	// [V] floor of the edge radius over R_n
	static constexpr double shieldH = 0.005;	// [D] protrusion per metre of mesh size below which a feature sits in the shock and boundary layers of the body upstream
	static constexpr double shieldL = 0.1;		// [V] reach of the shielding test per metre of mesh size
	static constexpr int edgeRays = 12;			// [V] hemisphere rays per face
	static constexpr int edgeSegs = 16;			// [V] attachment segments per vessel in the shader block
	static constexpr double nose = 0.03;		// [V] default nose radius per metre of hull size
	static constexpr double emis = 0.85;		// [P] HRSI/RCC class total emissivity
	static constexpr double lag = 25.0;			// [V] s
};

double SuttonGravesK (double R);				// [P] by the gas constant: air, CO2, H2-He
double SuttonGraves (double rho, double Rn, double V, double k);	// W/m2 cold wall, capped by the free-molecular 0.5 rho V^3
double HotWallFactor (double T, double cp, double h0);	// 1 - cp T / h0, at most 1; negative above the recovery temperature (convective cooling)
double KFlatPlate (double V);					// Tauber's laminar flat plate over Sutton-Graves (air)
double AngleFactor (double c);					// Lees hemisphere fit, c = sine of the inclination
double LocalRatio (double c, double x, double Rn, double kfp, double Ge);	// q_local / q_cw, the shader's HeatRatio: stagnation, flat plate, the edge bands' Ge, lee floor
double EdgeBand (double d, double w, double level, double nDotAtt);	// the shader's band of one attachment segment at distance d
double EdgeLevel (double rl, double Rn, double cL, double beta, double betaFeat);	// G_e on an attachment line: swept cylinder of radius rl, Lees around the feature, off when the line is off the feature

struct EdgeSeg { double a[3], b[3], n[3]; double w, level; };	// an attachment segment: ends, attachment normal, band half-width [m], level G_e

class EdgeSet {									// a mesh's real edges (design-reentry-heat Rev 3), built once in parts
public:
	void Begin (const std::vector<float> &xyz, const std::vector<uint32_t> &tri);	// mesh coordinates, 3 floats per vertex, 3 indices per triangle
	bool Step (size_t &rays);					// one part of the build; rays is the budget; true when built
	bool Done () const { return phase == 5; }
	void Attach (const double f[3], double Rn, std::vector<EdgeSeg> &out) const;	// f the downstream direction (mesh axes, unit)
	bool Shielded (const double p[3], const double u[3], const double t[3]) const;	// body upstream of p in its cross-section, past the protrusion floor
	size_t tris = 0, convex = 0, exposed = 0, real = 0;
	double size = 0.0, vox = 0.0;
private:
	int phase = 0;
	std::vector<float> inXyz;
	std::vector<uint32_t> inTri;
	std::vector<double> P, N;
	std::vector<uint32_t> T;
	struct Cand { uint32_t a, b, f1, f2; double d; };
	std::vector<Cand> cand;
	std::vector<uint32_t> faces, adj0, adj;
	std::vector<float> expo;
	size_t next = 0;
	double lo[3] = { 0.0, 0.0, 0.0 };
	int dim[3] = { 0, 0, 0 };
	std::vector<uint64_t> occ;
	struct Edge { double a[3], b[3], t[3], p1[3], q[3]; double arc, rl, flo, fth; uint32_t va, vb, nb0, nb1; bool face1, face2; };	// faceN: a plain face on that side (no parallel neighbour within rho); flo, fth: the feature's turn from p1 towards q
	std::vector<Edge> edges;
	std::vector<uint32_t> nbr;
	bool Occ (const double p[3]) const;
	void Prepare ();
	void Voxelize (size_t &rays);
	void Expose (size_t &rays);
	void Features ();
};

struct Wall { double eps, C, Tsink; };
double HeatCapacity (double lag, double eps);	// J/m2/K from the e-folding time at Tref
double EquilibriumWall (double qcw, double G, double cp, double h0, const Wall &w);
double StepWall (double T, double dt, double qcw, double G, double cp, double h0, const Wall &w);	// implicit Euler; dt <= 0 keeps T; dt huge gives equilibrium
double FlowTau (double lag, double T0);			// s: the flow frame's low-pass time at the hottest bin (review S3)
double LowPass (double dt, double tau);			// blend weight 1 - exp(-dt/tau), 0 for dt <= 0
double BinRatio (int k);						// G_k = G0 Gr^k
double BinCoord (double G);						// continuous bin index of G, clamped to [0, NBIN + 1]

void BlackbodyXYZ (double T, double xyz[3]);	// cd/m2 (CIE 1931 2 deg, Wyman 2013 fit)
void XYZToLinearSrgb (const double xyz[3], double rgb[3]);
void Blackbody (double T, double rgb[3]);		// linear sRGB in cd/m2, negatives clipped

struct ColourTable {
	double T0 = 500.0, T1 = 4000.0;
	std::vector<float> ln;						// 3 per entry, uniform in 1/T
	void Build (int n = 64, double t0 = 500.0, double t1 = 4000.0);
	void Lookup (double T, double rgb[3]) const;	// cd/m2, 0 below T0, clamped above T1
};

void SupportSet (const std::vector<std::array<double, 3>> &pts, std::vector<std::array<double, 3>> &out, int dirs = 128);	// extreme points along a Fibonacci sphere
std::array<double, 3> SupportQuery (const std::vector<std::array<double, 3>> &set, const double d[3]);	// the point furthest along d

struct HeatCfg {
	bool off = false;
	double nose = -1.0;							// m, -1 from the hull size
	double emis = Tune::emis;
	double lag = Tune::lag;
	double glow = 1.0;
	int bad = 0;								// tokens not understood
};
void ShaderBins (const double lnB[NBIN][3], double G, double out[3]);	// the shader's bin interpolation (a + f (b - a)), for the tests
bool ParseHeatLine (const std::string &line, HeatCfg &c);	// true when the line is a HEAT line (comments after ';' dropped)

}
