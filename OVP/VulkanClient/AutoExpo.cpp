// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: auto-exposure controller (design-ground-graphics 6)

#include "AutoExpo.h"
#include <algorithm>
#include <cmath>

namespace AutoExpo {

void Controller::Reset ()
{
	ev = 0.0f;
	for (int i = 0; i < NHIST; i++) { histStamp[i] = 0; histEV[i] = 0.0f; }
	histNext = 0;
}

float Controller::Exposure () const
{
	return std::exp2(ev);
}

void Controller::Used (uint32_t stamp)
{
	histStamp[histNext] = stamp;
	histEV[histNext] = ev;
	histNext = (histNext + 1) % NHIST;
}

float Controller::Target (const Sample &s, float evUsed, float *tau) const
{
	float l2 = s.meanLog2;
	float toLo = std::log2(prm.lo) - l2, toHi = std::log2(prm.hi) - l2;
	if (s.sat > prm.satLimit) { // saturation only ever lowers e
		*tau = prm.tauDown;
		return evUsed + std::min(0.0f, toHi) - 2.0f * (s.sat - prm.satLimit);
	}
	if (toLo > 0.0f) { *tau = prm.tauUp; return evUsed + toLo; }
	if (toHi < 0.0f) { *tau = prm.tauDown; return evUsed + toHi; }
	*tau = prm.tauDrift; // in the band: back toward 0, as far as the band allows
	return std::min(std::max(0.0f, evUsed + toLo), evUsed + toHi);
}

bool Controller::Update (const Sample &s, uint32_t stamp, float dt, float topEV)
{
	if (stamp == 0 || !(s.weight >= prm.minWeight) || !std::isfinite(s.meanLog2) || !std::isfinite(s.sat)) return false;
	int k = -1;
	for (int i = 0; i < NHIST; i++) if (histStamp[i] == stamp) k = i;
	if (k < 0) return false;
	histStamp[k] = 0; // each measure counts once
	float tau;
	float t = Target(s, histEV[k], &tau);
	t = std::min(std::max(t, prm.minEV), std::min(prm.maxEV, topEV));
	if (s.sat > prm.satLimit) t = std::min(t, ev);
	float a = dt > 0.0f ? 1.0f - std::exp(-dt / tau) : 0.0f;
	ev += (t - ev) * a;
	ev = std::min(std::max(ev, prm.minEV), prm.maxEV);
	return true;
}

} // namespace AutoExpo
