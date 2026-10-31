// not upstream (nose-wheel): see NoseWheel.h
#include "NoseWheel.h"
#include <algorithm>
#include <cmath>

namespace NoseWheel {

static const double DEG = 3.14159265358979323846 / 180.0;

bool IsWheel (double mu_lng, double mu)
{
	return mu_lng < mu;
}

double SteerMax (double v)
{
	v = std::fabs (v);
	return (v <= 5.0 ? 60.0 : std::max (6.0, 300.0 / v)) * DEG;
}

double Grip (double v, double steer)
{
	if (std::fabs (steer) > 0.01) return 1.0;
	return std::min (1.0, std::max (0.0, (std::fabs (v) - FREE) / FREE));
}

double Follow (double cur, double cmd, double dt)
{
	cmd = std::min (1.0, std::max (-1.0, cmd));
	double step = RATE * std::max (0.0, dt);
	return cmd > cur ? std::min (cmd, cur + step) : std::max (cmd, cur - step);
}

void Frame (double delta, double vlon, double vlat, double &vw, double &vn)
{
	double c = std::cos (delta), s = std::sin (delta);
	vw = vlon * c + vlat * s;
	vn = vlat * c - vlon * s;
}

void ToVessel (double delta, double fw, double fn, double &flon, double &flat)
{
	double c = std::cos (delta), s = std::sin (delta);
	flon = fw * c - fn * s;
	flat = fw * s + fn * c;
}

}
