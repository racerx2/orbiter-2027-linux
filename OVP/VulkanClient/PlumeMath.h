// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: plume, smoke and plasma maths, unit tested (design-volumetric-plumes 17)
// isentropic relations and Prandtl-Meyer after Anderson 2003, shock cells after Pack 1950, vacuum plume after Simons 1972

#ifndef __PLUMEMATH_H
#define __PLUMEMATH_H

#include <unordered_map>
#include <vector>

namespace PlumeMath {

inline int Version () { return 2; }

const double PSL = 101325.0;     // sea-level pressure, Pa
const double GAMMA = 1.2;        // [P] combustion products
const double PC = 7.0e6;         // [V] chamber pressure, Pa

double Smoothstep (double e0, double e1, double x);
double Condensate (double pa, double pe); // 0..1: the plume expands to under ~1e-4 of its exit pressure, cold enough to freeze (design-vacuum-plume)
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

// a vessel cfg's ExhaustStyle (design-exhaust-style): Orbiter's flame or the volume
enum Style { STYLE_NONE = -1, STYLE_LEGACY = 0, STYLE_VOLUMETRIC = 1 };
int StyleFromName (const char *s);                // the value's first word: legacy, volumetric (any case); else STYLE_NONE
bool Volumetric (int style, int cls);             // without a style: the propellant classes, not TEXTURE

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
const int TOUCH_MAX = 8; // [V] touching nozzles stay apart up to this many: the march keeps 8 capsules per ray (design-plume-nozzles 1)
void Cluster (const std::vector<Nozzle> &nz, int budget, std::vector<std::vector<int>> &groups);

// the exit and the Mach disk are sheets: integrated along a ray in closed form, not marched (design-plume-light 1)
double ExitSheet ();             // the exit slab exp(-(x/0.15 re)^2)/(0.27 re) times back, along a ray from behind: 0.15 sqrt(pi)/0.27
double MachSheet (double cosA);  // the Mach disk exp(-(x/0.06 rj re)^2) 0.5/(0.12 rj re) along a ray at cosA to the axis; either side, edge-on capped

// the plumes' own light on meshes and the ground (design-plume-light 2)
const double LIGHT_FAR = 2000.0;   // [V] m from the camera
const int LIGHT_MAX = 8, LIGHT_PER_VESSEL = 2; // [V] the most flux first; a vessel's per class (design-plume-nozzles 3)
struct Light { double rgb[3]; double k, x, att2, range, flux; }; // colour (max 1), strength, metres past the exit, 1/falloff^2, range, k rf^2
Light PlumeLight (int cls, double I, double re, const double c0[3], const double c1[3], double scale);
double LightRange (double k, double rf2); // where k / (1 + d^2/rf2) drops to 0.01, at most 300 m
double SphereMean (double k, double a, double r, double d); // mean of k / (1 + a s^2) over a sphere of radius r whose centre is d away: how a plume light ranks for a mesh (design-plume-nozzles 3)
double SphereMeanAtt (double I, double c, double b, double a, double r, double d); // the same for I / (c + b s + a s^2): a point light inside a mesh's bounds (design-plume-nozzles 3c)

// a vessel's exhaust lights merged into one: flux kept, the fall-off their second moment (design-plume-light 2.3)
struct Spot { double p[3], rgb[3]; double k, rf2; };
Spot MergeSpots (const std::vector<Spot> &s);

bool CapsuleVisible (const double planes[6][4], const double a[3], const double b[3], double r); // planes: n.p + w >= 0 inside
void FrustumPlanes (const double vp[16], double planes[6][4]); // from a row-vector view-projection, z in [0,1]

// a base's upward-facing structure surfaces in its own frame (y up), 16 m cells: where exhaust meets a pad (design-pad-plume)
class SurfaceGrid {
public:
	void Add (const double a[3], const double b[3], const double c[3], const double nrm[3]); // nrm: its vertex normals' sum; kept when within 45 deg of up (no normals: either side)
	double Top (double x, double z, double ceil) const;                 // highest surface at (x, z) not above ceil, NONE if there is none
	double Hit (const double o[3], const double d[3], double tmax) const; // first t <= tmax where the ray passes under a surface, NONE if none
	double Deck (double x, double z, double r, double ceil, int &ring) const; // highest Top at the point and rings of 8 at r/2 and r; ring: outer samples within 3 m of it
	size_t Count () const { return tris.size(); }
	static constexpr double NONE = -1e30;
private:
	struct T { double x[3], y[3], z[3]; };
	std::vector<T> tris;
	std::unordered_map<long long, std::vector<unsigned>> cells;
	double ymax = NONE; // the highest vertex kept: a march starts there
};

}

#endif // !__PLUMEMATH_H
