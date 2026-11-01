// GPSNav tests: the great-circle maths
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Geo.h"

using namespace gps;
using Catch::Matchers::WithinAbs;

static LatLon D (double lat, double lng) { return {lat * RAD, lng * RAD}; }

TEST_CASE("distance and bearing", "[geo]")
{
	const double R = 6371e3;
	LatLon jfk = D (40.6413, -73.7781), lhr = D (51.4700, -0.4543);
	REQUIRE_THAT(Angle (jfk, lhr) * R / 1000.0, WithinAbs (5540.0, 0.5)); // on the sphere (5555 on the ellipsoid)
	REQUIRE_THAT(Bearing (jfk, lhr) * DEG, WithinAbs (51.4, 0.2));
	REQUIRE_THAT(Bearing (D (0, 0), D (0, 1)) * DEG, WithinAbs (90.0, 1e-9));
	REQUIRE_THAT(Bearing (D (0, 0), D (-1, 0)) * DEG, WithinAbs (180.0, 1e-9));
	REQUIRE_THAT(Angle (D (10, 20), D (10, 20)), WithinAbs (0.0, 1e-15));
	LatLon d = Destination (jfk, Bearing (jfk, lhr), Angle (jfk, lhr));
	REQUIRE_THAT(Angle (d, lhr) * R, WithinAbs (0.0, 0.01));
}

TEST_CASE("cross-track and along-track", "[geo]")
{
	const double R = 6371e3;
	LatLon a = D (53.3206, -1.7297), b = D (53.1887, 0.1334), p = D (53.2611, -0.7972);
	REQUIRE_THAT(CrossTrack (a, b, p) * R, WithinAbs (-307.5, 0.5));
	REQUIRE_THAT(AlongTrack (a, b, p) * R / 1000.0, WithinAbs (62.331, 0.01));
	REQUIRE(CrossTrack (D (0, 0), D (0, 1), D (-0.01, 0.5)) > 0.0); // south of an eastbound course: right
	REQUIRE(AlongTrack (D (0, 0), D (0, 1), D (0, -0.5)) < 0.0);   // behind the start
}

TEST_CASE("many points' angle and bearing from one frame", "[geo]")
{
	LatLon c = D (28.6, -80.7);
	Local f (c);
	for (LatLon p : {D (28.1, -80.64), D (51.47, -0.45), D (-33.9, 151.2), D (28.6, -80.7001), D (89.0, 10.0)}) {
		double u[3], ang, sb, cb;
		Local::Unit (p, u);
		f.Polar (u, ang, sb, cb);
		REQUIRE_THAT(ang, WithinAbs (Angle (c, p), 1e-12));
		REQUIRE_THAT(Wrap2Pi (std::atan2 (sb, cb)), WithinAbs (Bearing (c, p), 1e-9));
	}
}

