// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: plume, smoke and plasma maths, unit tested (design-volumetric-plumes 17)
// isentropic relations and Prandtl-Meyer after Anderson 2003, shock cells after Pack 1950, vacuum plume after Simons 1972

#ifndef __PLUMEMATH_H
#define __PLUMEMATH_H

#include <vector>

namespace PlumeMath {

inline int Version () { return 2; }

const double PSL = 101325.0;     // sea-level pressure, Pa
const double GAMMA = 1.2;        // [P] combustion products
const double PC = 7.0e6;         // [V] chamber pressure, Pa

double Smoothstep (double e0, double e1, double x);
double PressureRatio (double M, double g);         // p0/p
double AreaRatio (double M, double g);             // A/A*
double MachFromPressureRatio (double r, double g); // inverse of PressureRatio, r >= 1
double PrandtlMeyer (double M, double g);          // nu(M), rad
double PrandtlMeyerMax (double g);                 // nu(inf), rad

double PressureFraction (double isp0, double ispRef); // phi = 1 - Isp(pRef)/Isp0, 0 when not pressure dependent
double ExitArea (double F0, double isp0, double ispRef, double pRef); // A_e = F0 phi / pRef, m^2
double ExitPressure (double phi, double F0);       // estimated nozzle exit pressure, Pa

struct Jet {
	double pe, pa;       // exit and ambient pressure, Pa
	double Me, Mj;       // exit and fully expanded jet Mach
	double rj;           // fully expanded jet radius / exit radius
	double Ls;           // shock-cell length / exit radius
	double xmd;          // Mach disk distance / exit radius, < 0 none
	double thInf;        // vacuum limiting turning angle, rad
	double watm;         // 1 jet in air, 0 vacuum plume
	double n;            // pe/pa
	bool jet;            // the jet terms are valid (watm >= 1e-3)
};
Jet JetState (double pe, double pa, double pc = PC, double g = GAMMA); // lengths in exit radii; finite in vacuum
double PlumeRadius (const Jet &j, double x);      // jet radius / exit radius at x exit radii downstream
double Simons (double d, double theta, double thInf, double g = GAMMA); // vacuum density, d in exit radii, theta off the axis

enum Class { HYDROLOX = 0, METHALOX, KEROLOX, HYPERGOLIC, SOLID, TEXTURE, NCLASS };
Class Classify (bool hasThruster, double F0, double isp0);
const char *ClassName (int c);
int ClassFromName (const char *s);                // -1 unknown

void BlackbodyXY (double T, double &x, double &y); // CIE 1931 chromaticity of a Planckian radiator (Wyman 2013 fit)
void Blackbody (double T, double rgb[3]);          // linear sRGB, max channel 1
void PlasmaColour (double Vkms, double rgb[3]);    // [U] CEA-based table, clamped
double PlasmaIntensity (double q, double V);       // q dynamic pressure Pa, V airspeed m/s

double ChordIntegral (double R, double d, double u0, double u1); // int (1 - (d^2+u^2)/R^2) du over [u0,u1] inside the sphere
double ChordFull (double R, double rho);            // the whole chord at offset rho: 4/3 h^3/R^2
double HG (double c, double g);                    // Henyey-Greenstein phase, 1/sr
double BandIntegral (double lambda, double cosA, double cap); // int exp(-s|cosA|/lambda) ds over s >= 0, capped at cap*lambda

struct Puff { double p[3]; double r; double sigma; };
void DeepOpacity (const std::vector<Puff> &puffs, const double sun[3], int N, std::vector<double> &tau); // optical depth toward the sun at each centre

void FlowViewProj (const double c[3], const double f[3], double ext, double eyeDist, double zn, double zf, double vp[16], double X[3], double Y[3]); // row-major, row vectors; RH look-at + ortho as RenderShadowMap

struct Nozzle { double ref[3], axis[3]; double re, level, rpx; int cls, vessel; };
void Cluster (const std::vector<Nozzle> &nz, int budget, std::vector<std::vector<int>> &groups);

bool CapsuleVisible (const double planes[6][4], const double a[3], const double b[3], double r); // planes: n.p + w >= 0 inside
void FrustumPlanes (const double vp[16], double planes[6][4]); // from a row-vector view-projection, z in [0,1]

}

#endif // !__PLUMEMATH_H
