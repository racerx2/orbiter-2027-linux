// GPSNav: the map's colours for heights
#include "Colour.h"
#include <algorithm>
#include <cmath>

namespace gps {

namespace {

struct Stop { double at; int r, g, b; };

uint32_t Ramp (const Stop *s, int n, double x)
{
	if (x <= s[0].at) return Rgb (s[0].r, s[0].g, s[0].b);
	for (int i = 1; i < n; i++)
		if (x <= s[i].at) {
			double t = (x - s[i - 1].at) / (s[i].at - s[i - 1].at);
			auto L = [t] (int a, int b) { return (int)std::lround (a + (b - a) * t); };
			return Rgb (L (s[i - 1].r, s[i].r), L (s[i - 1].g, s[i].g), L (s[i - 1].b, s[i].b));
		}
	return Rgb (s[n - 1].r, s[n - 1].g, s[n - 1].b);
}

// land above a sea, metres: lowland greens, olive, tan, browns, rock grey, snow
const Stop LAND[] = {{0, 40, 88, 44}, {100, 60, 106, 50}, {300, 96, 124, 58}, {600, 136, 138, 74}, {1000, 150, 128, 80},
	{1600, 138, 106, 72}, {2400, 120, 92, 74}, {3200, 140, 132, 126}, {4200, 200, 200, 200}, {6000, 235, 235, 235}};
const Stop SEA[] = {{-4000, 10, 26, 62}, {-200, 18, 44, 92}, {0, 26, 58, 112}};
// a body without a sea, the share of the heights in view: dark to light, greys and tans
const Stop DRY[] = {{0.0, 58, 54, 50}, {0.25, 86, 78, 66}, {0.5, 118, 106, 86}, {0.75, 150, 138, 112}, {1.0, 190, 182, 160}};

} // namespace

uint32_t Scale (uint32_t c, double f)
{
	auto C = [f] (uint32_t v) { return (uint32_t)std::clamp ((int)std::lround (v * f), 0, 255); };
	return (c & 0xFF000000u) | C ((c >> 16) & 255) << 16 | C ((c >> 8) & 255) << 8 | C (c & 255);
}

uint32_t Topo (double elev, bool sea, double lo, double hi)
{
	if (sea) return elev <= 0.0 ? Ramp (SEA, 3, elev) : Ramp (LAND, 10, elev);
	double t = hi > lo ? (elev - lo) / (hi - lo) : 0.5;
	return Ramp (DRY, 5, t);
}

uint32_t Warn (double elev, double alt, bool sea, double lo, double hi)
{
	const double FT = 0.3048;
	if (elev > alt - 100.0 * FT) return Rgb (205, 30, 30);
	if (elev > alt - 1000.0 * FT) return Rgb (215, 190, 30);
	return Scale (Topo (elev, sea, lo, hi), 0.45);
}

} // namespace gps
