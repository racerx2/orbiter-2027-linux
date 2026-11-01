// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream (stream-rwylights): a streamed runway's frame on its planet, x east, y up, z north

#ifndef __RWYFRAME_H
#define __RWYFRAME_H

#include "OrbiterAPI.h"
#include "WayGeom.h"
#include <cmath>

namespace RwyFrame {

// a point in the planet frame, as Body::EquatorialToLocal
inline VECTOR3 Point (double lat, double lng, double rad)
{
	return _V(rad * std::cos (lat) * std::cos (lng), rad * std::sin (lat), rad * std::cos (lat) * std::sin (lng));
}

// as Body::LocalToEquatorial
inline void LatLng (const VECTOR3 &q, double &lat, double &lng)
{
	lng = std::atan2 (q.z, q.x);
	lat = std::asin (q.y / length (q));
}

struct Axes {
	VECTOR3 o;  // origin, planet frame [m]
	MATRIX3 r;  // the frame's axes in the planet frame, as columns: east, up, north
};

inline Axes At (double lat, double lng, double rad)
{
	double sa = std::sin (lat), ca = std::cos (lat), so = std::sin (lng), co = std::cos (lng);
	Axes a;
	a.o = Point (lat, lng, rad);
	a.r = _M(-so, ca * co, -sa * co,
	          0.0, sa,      ca,
	          co,  ca * so, -sa * so);
	return a;
}

inline VECTOR3 ToPlanet (const Axes &a, const VECTOR3 &p) { return mul (a.r, p) + a.o; }
inline VECTOR3 FromPlanet (const Axes &a, const VECTOR3 &q) { return tmul (a.r, q - a.o); }

inline double Length (const WayGeom::RunwayLL &d, double R)
{
	return length (Point (d.lat[0], d.lng[0], R) - Point (d.lat[1], d.lng[1], R));
}

// one runway listed by two boxes: the same ref or one without, both ends within 30 m either way round
inline bool Same (const WayGeom::RunwayLL &a, const WayGeom::RunwayLL &b, double R)
{
	if (a.ref != b.ref && !a.ref.empty () && !b.ref.empty ()) return false;
	auto d = [&] (int i, int j) { return length (Point (a.lat[i], a.lng[i], R) - Point (b.lat[j], b.lng[j], R)); };
	return (d (0, 0) < 30.0 && d (1, 1) < 30.0) || (d (0, 1) < 30.0 && d (1, 0) < 30.0);
}

// a streamed runway (ends A, B) is a base's (ends a, b, planet frame): within 10 deg of it, its midpoint within 200 m of its line and 500 m of its ends
inline bool OnBase (const VECTOR3 &A, const VECTOR3 &B, const VECTOR3 &a, const VECTOR3 &b)
{
	VECTOR3 v = b - a;
	double L = length (v);
	if (L < 1.0 || length (B - A) < 1.0) return false;
	v = v * (1.0 / L);
	if (std::fabs (dotp (unit (B - A), v)) < std::cos (10.0 * RAD)) return false;
	VECTOR3 w = (A + B) * 0.5 - a;
	double t = dotp (w, v);
	return t > -500.0 && t < L + 500.0 && length (w - v * t) < 200.0;
}

} // namespace RwyFrame

#endif // !__RWYFRAME_H
