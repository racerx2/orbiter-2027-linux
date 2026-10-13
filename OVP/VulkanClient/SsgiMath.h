
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space GI maths, unit tested; SSGI.glsl mirrors it (design-ssgi 5, 12; Rev 2)
// slice measure after Jimenez et al. 2016 (GTAO), visibility bitmask after Therrien et al. 2023, octahedral normals after Cigolle et al. 2014

#ifndef __SSGIMATH_H
#define __SSGIMATH_H

namespace SsgiMath {

inline int Version () { return 2; }

const double PI = 3.14159265358979323846;

struct V3 { double x, y, z; };
V3 Add (const V3 &a, const V3 &b);
V3 Sub (const V3 &a, const V3 &b);
V3 Scale (const V3 &a, double s);
double Dot (const V3 &a, const V3 &b);
V3 Cross (const V3 &a, const V3 &b);
V3 Unit (const V3 &a);

// display curves, one channel
double TwF (double u, bool filmic);         // planet: 1 - exp(-u), or the ACES fit of 0.568 u
double TwI (double y, bool filmic);         // its inverse, y < 1 (filmic: the positive root of the fit)
double TmF (double u, bool glow);           // Metalness and PbrMesh: u (1 + u^4)^-1/4; identity under _LIGHTGLOW
double TmI (double y, bool glow);           // its inverse, y < 1
double TaF (double u, double g);            // airless terrain: min(u, 1)^g
double TaI (double y, double g);
double S2L (double c);                      // sRGB to linear (PbrBRDF.glsl)
double L2S (double c);
double LightFX (double x);                  // 2x / (1 + x), airless terrain (Scatter.glsl)
double LightFXSq (double S);                // 1.8 sqrt(S) / sqrt(2 + S), Metalness on one channel

enum Cls { TERRAIN = 0, TREE = 1, BASE = 2, VESSEL = 3 };
enum Fam { LEGACY = 0, METAL = 1, PBR = 2, TILE = 3 };

struct Curve {
	bool filmic = false, glow = false, air = true;
	double g = 1.0;      // TrGamma
	double s = 1.0;      // terrain: Expo / e with air, TrExpo / e without
	double sTree = 1.0;  // trees: their Expo / e
	double e = 1.0;      // exposure
	double uMax = 3.0;   // sender cap
	double expo = 1.0;   // Const.Expo, inscatter in the planet curve's units
};

// sender radiance in exposed linear units (e x) from the display value y; A transmittance, Iraw the land inscatter before Expo, Iobj the objects' display inscatter
double SenderV (double y, double A, double Iraw, double Iobj, int cls, int fam, const Curve &c);
// display increment of a receiver for the added irradiance E (exposed units, strength and fade in it); >= 0
double CompositeDy (double y, double A, double Iraw, double Iobj, double rho, double E, int cls, int fam, const Curve &c);

// octahedral normal, 10-bit unorm each
void OctEncode (const V3 &n, double *u, double *v);
V3 OctDecode (double u, double v);
double Quant10 (double x);

// slice measure mu(t) = cos(t - g) |sin t| on [g - pi/2, g + pi/2], t from the view vector
double SliceMass (double g);                // cos g + g sin g
double SliceCdf (double t, double g);       // integral of mu from g - pi/2 to t
unsigned BitMask (double ta, double tb, double g, double jit = 0.5); // the 32 sectors, uniform in the CDF, that [ta, tb] covers; jit in [0, 1) dithers the edges per slice
int PopCount (unsigned m);
// one sample of a slice through V and T (unit, T normal to V, toward the slice's + side): front S, back S + thick * unit(S), from P
unsigned SampleBits (const V3 &P, const V3 &S, double thick, const V3 &V, const V3 &T, double g, double jit = 0.5);
double SliceGamma (const V3 &N, const V3 &V, const V3 &T, double *np); // angle of N projected into the slice (clamped to +-pi/2), np its length

// march radius: R = max(R0, k d), its half-res pixel radius clamped, R back from it
double Radius (double d, double R0, double k);
double PixelRadius (double R, double d, double tanY, double H2, double pmin, double pmax);
double RadiusFromPixels (double px, double d, double tanY, double H2);

// sender weights for vessel receivers lit by irradiance probes (Metalness, PbrMesh): terrain, trees, bases, vessels
void ProbeWeights (int envMapMode, bool irradiance, bool probesExist, double w[4]);

// GetObjectAtmoParams' sky ambient at geocentric p (VPlanetAtmo.cpp), plus the config level
struct Atmo {
	double PlanetRad = 6.371e6, AtmoAlt = 2e5, AtmoRad = 6.571e6, TW_Dst = 0.2;
	double cAmbient[3] = { 0.2, 0.3, 0.5 }, cSun[3] = { 1, 1, 1 };
	double RayWave[3] = { 0.08, 0.2, 0.5 }, MieWave[3] = { 0.05, 0.05, 0.05 };
	double iH[2] = { 1.0 / 8000.0, 1.0 / 1200.0 }, rmO[2] = { 1e-5, 1e-5 }, rmI[2] = { 1e-6, 1e-6 };
	double twBld = 1.0, cfg[3] = { 0, 0, 0 };
};
double RayLength (double cosDir, double r0, double r1);
void Gauss7 (double cosDir, double r0, double dist, const double iH[2], const Atmo &a, double out[2]);
void MeshAmbient (const Atmo &a, const V3 &p, const V3 &toSun, double out[3]);

}

#endif // !__SSGIMATH_H
