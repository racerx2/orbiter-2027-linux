// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: auto-exposure controller, pure maths (design-ground-graphics 6)

#pragma once
#include <cstdint>

namespace AutoExpo {

struct Params {
	float lo = 0.12f, hi = 0.55f;		// band of the measured display luminance
	float tauUp = 2.0f;					// s, below the band (dark adaptation is slow)
	float tauDown = 0.7f;				// s, above the band and saturation
	float tauDrift = 4.0f;				// s, inside the band back toward e = 1
	float minEV = -1.5f, maxEV = 2.5f;
	float satLimit = 0.15f;
	float minWeight = 0.05f;
};

struct Sample {
	float meanLog2;	// weighted mean log2 luminance
	float weight;	// weight sum as a fraction of the pixels, 0..1
	float sat;		// fraction of saturated pixels
};

class Controller {
public:
	explicit Controller (const Params &p = Params()) : prm(p) { Reset(); }
	void Reset ();
	void SetParams (const Params &p) { prm = p; }
	float Exposure () const;				// e
	float EV () const { return ev; }		// log2 e
	void Used (uint32_t stamp);				// the frame with this stamp renders with the current e
	// a measure of the frame with this stamp; topEV: the upper clamp now; false: stale or too weak, e held
	bool Update (const Sample &s, uint32_t stamp, float dt, float topEV);
	float Target (const Sample &s, float evUsed, float *tau) const;	// log2 e wanted, and its time constant

private:
	static const int NHIST = 8;
	Params prm;
	float ev;
	uint32_t histStamp[NHIST];
	float histEV[NHIST];
	int histNext;
};

} // namespace AutoExpo
