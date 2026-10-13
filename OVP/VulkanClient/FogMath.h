// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: height fog maths, unit tested (design-height-fog 4, 6, 15 and Rev 2); no client types
// Hillaire 2015 (step integral), Timonen and Westerholm 2010 (shadow line sweep), Teschner et al. 2003 (hash), Koschmieder; written here

#ifndef __FOGMATH_H
#define __FOGMATH_H

#include "HorizonMap.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace FogMath {

const int MAPN = 256;          // map texels per side
const int SCAN = 16;           // scan intervals per ray (Rev 2, review MUST 1)
const float LIT = -1e4f;       // Hs of a texel that shadows nothing
const float DARK = 1e4f;       // Hs with the sun below the horizon

// tangent plane at a centre, planet-local axes (x = cos lat cos lng, y = sin lat, z = cos lat sin lng), gnomonic
struct Plane {
	double lat = 0.0, lng = 0.0;
	double U[3] = { 1, 0, 0 }, E[3] = { 0, 0, 1 }, N[3] = { 0, 1, 0 };
	void Set (double lat, double lng);
	bool ToMap (const double p[3], double R, double *x, double *y) const; // any planet-local point, m on the plane
	void FromMap (double x, double y, double R, double *lat, double *lng) const;
};

// one cascade, row 0 north: F floor, V valley depth, Hs sun-shadow height, m strength (m, m, m, 0..3)
struct Map {
	Plane f;
	double half = 0.0, R = 0.0;
	int n = MAPN;
	std::vector<float> px;     // n*n*4
	float Fmin = 0.0f, Gmax = 0.0f, Gmean = 0.0f, Ltop = 0.0f; // shell bounds and the mean ground
	float water = 0.0f;        // sea and lake fraction of the covered texels
	float cover = 0.0f;        // covered fraction of all texels
	bool Sample (const double p[3], float out[4], double *r) const; // bilinear as the GPU; r = Chebyshev radius / half; false off the plane
};

struct Params {                // per frame (Frame), the shader's vFog/vFog2
	float sf = 0.0f, sh = 0.0f, D0 = 0.0f, kv = 0.0f;  // radiation fog 1/m, haze 1/m, lid depth m, valley gain
	float Hinv = 400.0f, Hh = 350.0f, ss = 0.0f, Ds = 0.0f; // haze top over the ground m, haze scale height m, sea fog 1/m, its lid m
	float Gmean = 0.0f;        // ground beyond the far map
};

// density, 1/m: f = (F, V, Hs, m), ef the far map's edge weight, h altitude over the body radius, n drift noise 0..1
float Density (const Params &P, const float f[4], float ef, double h, float n);
float DensitySeg (const Params &P, const float f[4], float ef, double ha, double hb, float n); // mean over a step whose altitude runs from ha to hb (closed form)
float Bound (const Params &P, const float f[4], float ef, double ha, double hb); // layout weight: the lids at the mid altitude with n = 1, the haze's mean
float LidTop (const Params &P, const float f[4]);                       // highest lid for these fields

// drift noise: value noise on planet-local metres, 700 m cells, x periodic over 64 cells; off = the wind offset along x
uint32_t Hash3 (int x, int y, int z);
float Drift (double x, double y, double z, double off);

// rays: altitude along a ray from radius Rc (camera altitude hc) with mu = cos of the zenith angle; float-stable form
double RayAlt (double hc, double R, double mu, double t);
void ScanPoints (double t0, double t1, double *tk);                    // SCAN + 1 points, even in asinh(t / SA)
void Steps (const double *t, const double *w, int np, int n, double *edges); // n + 1 edges over [t0, t(np-1)], placed by the np - 1 interval weights
double Weight (double tau, double tauBefore, double len, double total);
float BoundLid (const Params &P, const float f[4]);                    // the valley fog's highest lid (n = 1, + 2 softness), -1e30 without fog
float SeaLid (const Params &P, const float f[4], float ef);             // the sea fog's highest lid, -1e30 without sea fog

typedef std::function<bool (const double p[3], float f[4], float *ef)> FieldFn; // planet-local point; false: no fog data
const int MAXP = SCAN + 9;     // layout points: the scan, the ray's lowest point and the lid crossings
struct Layout {
	int n = 0;
	double t[MAXP] = {}, w[MAXP] = {}; // points, and the weight of [t(i), t(i+1)]
};
void MakeLayout (const Params &P, const FieldFn &field, const double o[3], const double d[3], double R, double t0, double t1, Layout &L);
// transmittance through the fog from o along unit d over [t0, t1]; jit the step offset; drift < 0 turns the noise off (n = 0.5)
double Transmittance (const Params &P, const FieldFn &field, const double o[3], const double d[3], double R, double t0, double t1, int steps, double jit, double drift);

// sun and time
struct SunTime {
	double tau = 12.0;         // local solar time, h
	double dayLen = 12.0;      // h
	double a = 0.0, b = 0.0;   // hours since sunrise and since sunset, 0..24
	bool polarDay = false, polarNight = false;
	bool up = true;
};
SunTime Sun (double lat, double lng, const double s[3], int sense);    // s: planet-local unit vector to the sun; sense +1 prograde
double Diurnal (const SunTime &st, double moist, double *Hinv);        // f_tod and the haze top
double FogDayProb (double moist, double nightLen);
double DayNoise (double lat, double lng, double mjd, uint32_t seed, double cell, double days); // 0..1, near uniform at every point
double FogDay (double lat, double lng, double mjd, double p);          // m_day 0..1.35
double SeaDay (double lat, double lng, double mjd, double moist);      // 0..1

struct FrameIn {
	double lat = 0.0, lng = 0.0, mjd = 51544.5;
	double sun[3] = { 1, 0, 0 };
	int sense = 1;
	double covMean = -1.0;     // cloud coverage within 50 km, < 0 none
	double wFar = 0.0;         // the far map's water fraction
	bool wet = true;
	double Gmean = 0.0;
};
Params Frame (const FrameIn &in, SunTime *st = nullptr);

// bake (GfxPool job)
struct BakeIn {
	double lat = 0.0, lng = 0.0, half = 25e3, R = 6371e3;
	std::vector<std::shared_ptr<const Horizon::Grid>> grids; // any order, the finest covering one wins
	double sun[3] = { 0, 1, 0 }; // planet-local, toward the sun
	double mjd = 51544.5;
	bool wet = true;
	double wFar = -1.0;        // the far map's water fraction (near cascade); < 0: its own
	double nightLen = 12.0;    // h, at the centre
	std::shared_ptr<const Map> outer; // the far map: upstream shadow heights for the near cascade
};
void Bake (const BakeIn &in, Map &out);
bool GridHeight (const std::vector<const Horizon::Grid*> &g, double lat, double lng, float *h); // the first grid in g that covers the point
void Floor (const std::vector<float> &mean, int n, double step, std::vector<float> &F, std::vector<float> &G, std::vector<float> *lo = nullptr); // NODATA-aware; lo = min_r2km
void Water (const std::vector<float> &mean, const std::vector<float> &lo, int n, double step, bool wet, std::vector<float> &sea, std::vector<float> &lake); // lo = min_r2km
void ShadowSweep (const std::vector<float> &hp, int n, double step, double sE, double sN, double sU, const std::function<float (double x, double y)> &up, std::vector<float> &Hp);
void TileRange (double lat0, double lat1, double lng0, double lng1, int lvl, int *ia, int *ib, int *ja, int *jb); // internal level, ilat from the north
void GridBox (double lat, double lng, double ext, double R, double *la0, double *la1, double *lo0, double *lo1); // lat/lng box around a cap of radius ext (m); every longitude when it holds a pole

uint16_t ToHalf (float v);

struct Rebuild {
	bool valid = false, sameObj = false;
	double dist = 0.0, sunDeg = 0.0, sunElevDeg = 45.0, age = 0.0; // camera to the map centre m, sun turn since the bake, sun elevation, bake age s
	bool sigChanged = false;   // the elevation grids under the map changed
};
bool NeedRebuild (const Rebuild &r, bool nearMap);

}

#endif // !__FOGMATH_H
