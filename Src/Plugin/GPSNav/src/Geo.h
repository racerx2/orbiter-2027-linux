// GPSNav: great-circle navigation on a sphere; angles in radians, distances as central angles (times the radius for metres)
#pragma once
#include <cmath>

namespace gps {

constexpr double PI = 3.14159265358979323846, RAD = PI / 180.0, DEG = 180.0 / PI;
constexpr double NM = 1852.0, FT = 0.3048, KT = NM / 3600.0;

struct LatLon { double lat = 0.0, lng = 0.0; };

inline double Wrap2Pi (double a) { a = std::fmod (a, 2.0 * PI); return a < 0.0 ? a + 2.0 * PI : a; }
inline double WrapPi (double a) { return Wrap2Pi (a + PI) - PI; }

// haversine: good from millimetres to antipodes
inline double Angle (LatLon a, LatLon b)
{
	double s1 = std::sin (0.5 * (b.lat - a.lat)), s2 = std::sin (0.5 * (b.lng - a.lng));
	double h = s1 * s1 + std::cos (a.lat) * std::cos (b.lat) * s2 * s2;
	return 2.0 * std::asin (std::sqrt (std::fmin (1.0, h)));
}

// the initial true bearing from a to b, 0..2pi
inline double Bearing (LatLon a, LatLon b)
{
	double dl = b.lng - a.lng;
	double y = std::sin (dl) * std::cos (b.lat);
	double x = std::cos (a.lat) * std::sin (b.lat) - std::sin (a.lat) * std::cos (b.lat) * std::cos (dl);
	return Wrap2Pi (std::atan2 (y, x));
}

inline LatLon Destination (LatLon a, double brg, double ang)
{
	double lat = std::asin (std::sin (a.lat) * std::cos (ang) + std::cos (a.lat) * std::sin (ang) * std::cos (brg));
	double lng = a.lng + std::atan2 (std::sin (brg) * std::sin (ang) * std::cos (a.lat), std::cos (ang) - std::sin (a.lat) * std::sin (lat));
	return {lat, WrapPi (lng)};
}

// p's angle off the great circle a to b: positive to the right of the course
inline double CrossTrack (LatLon a, LatLon b, LatLon p)
{
	return std::asin (std::sin (Angle (a, p)) * std::sin (Bearing (a, p) - Bearing (a, b)));
}

// how far along the course a to b p's foot is (negative: behind a)
inline double AlongTrack (LatLon a, LatLon b, LatLon p)
{
	double d = Angle (a, p), xt = CrossTrack (a, b, p);
	double at = std::acos (std::fmax (-1.0, std::fmin (1.0, std::cos (d) / std::cos (xt))));
	return std::cos (Bearing (a, p) - Bearing (a, b)) < 0.0 ? -at : at;
}

// the frame at a point (up, east, north), for many points' angle and bearing from it at once: they as unit vectors
struct Local {
	double U[3], E[3], N[3];
	explicit Local (LatLon p)
	{
		double cla = std::cos (p.lat), sla = std::sin (p.lat), clo = std::cos (p.lng), slo = std::sin (p.lng);
		U[0] = cla * clo, U[1] = cla * slo, U[2] = sla;
		E[0] = -slo, E[1] = clo, E[2] = 0.0;
		N[0] = -sla * clo, N[1] = -sla * slo, N[2] = cla;
	}
	static void Unit (LatLon p, double *u) { u[0] = std::cos (p.lat) * std::cos (p.lng), u[1] = std::cos (p.lat) * std::sin (p.lng), u[2] = std::sin (p.lat); }
	// the central angle to u, and the sine and cosine of its bearing
	void Polar (const double *u, double &ang, double &sb, double &cb) const
	{
		double c = u[0] * U[0] + u[1] * U[1] + u[2] * U[2], e = u[0] * E[0] + u[1] * E[1], n = u[0] * N[0] + u[1] * N[1] + u[2] * N[2];
		double h = std::sqrt (e * e + n * n);
		ang = std::atan2 (h, c), sb = h > 0.0 ? e / h : 0.0, cb = h > 0.0 ? n / h : 1.0;
	}
};

} // namespace gps
