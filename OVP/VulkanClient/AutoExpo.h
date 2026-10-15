// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: auto-exposure controller, pure maths (design-ground-graphics 6); night look maths (design-night-look)

#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace AutoExpo {

struct Params {
	float lo = 0.12f, hi = 0.55f;		// band of the measured display luminance
	float tauUp = 2.0f;					// s, below the band (dark adaptation is slow)
	float tauDown = 0.7f;				// s, above the band and saturation
	float tauDrift = 4.0f;				// s, inside the band back toward e = 1
	float minEV = -1.5f, maxEV = 2.5f;
	float satLimit = 0.15f;
	float minWeight = 0.05f;
	bool night = false;					// NightLook: the rules below scale with the night weight
	float nightDropEV = 3.0f;			// the band's low edge at full night, EV under the day's
	float nightMaxEV = 1.0f;			// the upper clamp at full night
	float brake0 = 0.005f, brake1 = 0.03f;	// near-saturated share over which a rise slows to a stop
	float satThrNight = 0.9f;			// the measure's near-saturation threshold at full night
	float speed = 1.0f;					// adaptation speed, the time constants divided by it (0.25...4)
	float snapWeight = 0.2f;			// the first measure after Reset with this weight or more darkens e at once; above 1 never
};

struct Sample {
	float meanLog2;	// weighted mean log2 luminance
	float weight;	// weight sum as a fraction of the pixels, 0..1
	float sat;		// fraction of saturated pixels (near-saturated 2x2 blocks at night with NightLook)
};

class Controller {
public:
	explicit Controller (const Params &p = Params()) : prm(p) { Reset(); }
	void Reset ();
	void SetParams (const Params &p);		// ev kept inside the new clamps
	void SetBias (float b) { bias = std::isfinite(b) ? b : 0.0f; } // EV added to e; measures are judged without it
	float Bias () const { return bias; }
	float Exposure () const;				// e, the bias included
	float EV () const { return ev; }		// log2 e
	void Used (uint32_t stamp);				// the frame with this stamp renders with the current e
	// a measure of the frame with this stamp; topEV: the upper clamp now; false: stale or too weak, e held
	bool Update (const Sample &s, uint32_t stamp, float dt, float topEV);
	float Target (const Sample &s, float evUsed, float *tau) const;	// log2 e wanted, and its time constant
	void SetNight (float n, bool internal = false); // night weight 0..1 (NightWeight), used only with Params::night; internal: cockpit, the band and clamp stay the day's, the brake stays
	float Night () const { return prm.night ? night : 0.0f; }
	float SatThreshold () const;			// the measure's saturation threshold: 254/255 by day

private:
	static const int NHIST = 8;
	Params prm;
	float ev;
	float night = 0.0f;
	float band = 1.0f;		// 0 in the cockpit: the night band drop and clamp off
	float bias = 0.0f;
	bool fresh = true;		// no measure since Reset
	uint32_t histStamp[NHIST];
	float histEV[NHIST];
	float histBias[NHIST];
	int histNext;
};

// the sun over the camera's own horizon (elevation + dip, cos dip = R / (R + h)): 0 at +3 deg and above, 1 at -9 deg and below
float NightWeight (float sinElev, float cosDip = 1.0f);
float NightWeightAt (double cx, double cy, double cz, double sx, double sy, double sz, double R); // camera from the planet centre, unit sun direction

} // namespace AutoExpo

// the night look's shader and generator maths, mirrored for the tests (design-night-look 5, 6)
namespace NightLook {

float Smooth (float e0, float e1, float x);		// GLSL smoothstep; e0 > e1 gives the falling step
float CellDusk (float elevDeg, float h);			// a building cell's dusk progress D, h its hash 0..1
float WindowOn (float a, float D, float lod);		// PBR.fx: a window with on-time a at dusk progress D, lod the emission map's lod
float CityLevel (float r, float g, float b);		// the light map's level with contrast
float SegShare (float city);						// share of lit street segments

// ground lamps (NewPlanet.glsl NightLamps): profile exp(-r^2/s^2), integral pi s^2; q in 128 m blocks
const float Sp = 0.07f, Sh = 0.012f, Hw = 0.04f, Side = 0.035f, Exist = 0.85f, PerBlock = 4.0f;
const float Line0 = 0.0625f, Line1 = 0.125f, Area0 = 0.2f, Area1 = 0.35f; // along footprint: points to line; across: to the area mean
extern const float Pal[3][3], PalW[3];
struct Lamps { float pool, head, spill; float col[3]; }; // col: the pools' mean colour
void Warp (float qx, float qy, float &wx, float &wy);		// streets bend by up to ~30 m over ~1 km
Lamps LampMeans (float pSeg);								// area means a block^2, palette mean colour
Lamps LampField (float wx, float wy, float fx, float fy, float pSeg); // at warped q, fx fy the footprints of wx wy a pixel

float ClsHash (float x, float y);					// CPU copy of NewPlanet.glsl's ClsHash (Hoskins, MIT, CREDITS)
float ClsNoise (float x, float y, float P);

// night map mips, RGBA8, alpha the on-time: Half the box with the on-time weighted by light; Nest each texel one of its 4 children by hash
void NiteHalf (const std::vector<uint8_t> &src, int w, int h, std::vector<uint8_t> &out);
void NiteNest (const std::vector<uint8_t> &src, int w, int h, int level, std::vector<uint8_t> &out);

} // namespace NightLook
