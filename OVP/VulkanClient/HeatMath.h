// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: re-entry hull heating maths (design-reentry-heat): stagnation flux, local heating ratio, wall temperature with lag, blackbody colour

#pragma once

#include <array>
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
	static constexpr double edge = 1.2;			// [V] leading-edge level over stagnation (an edge of R <= R_n/3: sqrt(R_n/2R) >= 1.2)
	static constexpr double gate0 = 2.0;		// [V] R_n k1 where an edge starts to count
	static constexpr double gate1 = 4.0;		// [V] R_n k1 where it fully counts
	static constexpr double attach0 = 0.85;		// [V] c_perp from which an edge holds an attachment line (windward creases do not); HullHeat.glsl HeatRatio has the same four gate numbers
	static constexpr double attach1 = 0.97;		// [V] c_perp where it fully does
	static constexpr double nose = 0.03;		// [V] default nose radius per metre of hull size
	static constexpr double emis = 0.85;		// [P] HRSI/RCC class total emissivity
	static constexpr double lag = 25.0;			// [V] s
};

double SuttonGravesK (double R);				// [P] by the gas constant: air, CO2, H2-He
double SuttonGraves (double rho, double Rn, double V, double k);	// W/m2 cold wall, capped by the free-molecular 0.5 rho V^3
double HotWallFactor (double T, double cp, double h0);	// 1 - cp T / h0, at most 1; negative above the recovery temperature (convective cooling)
double KFlatPlate (double V);					// Tauber's laminar flat plate over Sutton-Graves (air)
double AngleFactor (double c);					// Lees hemisphere fit, c = sine of the inclination
double LocalRatio (double c, double x, double k1, double k2, double fd2, double Rn, double kfp);	// q_local / q_cw, the shader's HeatRatio (fd2 = |f . d2|)
bool Weingarten (const double N[3], const double dNx[3], const double dNy[3], const double dPx[3], const double dPy[3],
	double &k1, double &k2, double d2[3]);		// the shader's HeatGeom: principal curvatures (>= 0) and the low-curvature direction

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
