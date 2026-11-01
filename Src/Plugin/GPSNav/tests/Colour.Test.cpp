// GPSNav tests: the map's colours for heights
#include <catch2/catch_test_macros.hpp>
#include "Colour.h"

using namespace gps;

static int R (uint32_t c) { return c & 255; }
static int G (uint32_t c) { return (c >> 8) & 255; }
static int B (uint32_t c) { return (c >> 16) & 255; }

TEST_CASE("chart tints with a sea", "[colour]")
{
	uint32_t sea = Topo (-50.0, true, 0, 0), low = Topo (20.0, true, 0, 0), mid = Topo (1000.0, true, 0, 0), peak = Topo (5500.0, true, 0, 0);
	REQUIRE((B (sea) > R (sea) && B (sea) > G (sea)));          // blue
	REQUIRE((G (low) > R (low) && G (low) > B (low)));          // green
	REQUIRE((R (mid) > G (mid) && G (mid) > B (mid)));          // tan
	REQUIRE((R (peak) > 200 && G (peak) > 200 && B (peak) > 200)); // snow
	REQUIRE(B (Topo (0.0, true, 0, 0)) > G (Topo (0.0, true, 0, 0))); // 0 m is sea
	REQUIRE(B (Topo (-3000.0, true, 0, 0)) < B (sea));          // deeper, darker
	REQUIRE((sea >> 24) == 255);
}

TEST_CASE("tints without a sea and against an altitude", "[colour]")
{
	uint32_t lo = Topo (-2000.0, false, -2000.0, 3000.0), hi = Topo (3000.0, false, -2000.0, 3000.0);
	REQUIRE((R (hi) > R (lo) && G (hi) > G (lo)));               // the highest in view lightest
	REQUIRE(Topo (500.0, false, 500.0, 500.0) == Topo (0.0, false, 0.0, 0.0)); // no spread: the middle
	const double FT = 0.3048;
	REQUIRE(Warn (950 * FT, 1000 * FT, true, 0, 0) == Rgb (205, 30, 30));   // within 100 ft
	REQUIRE(Warn (1200 * FT, 1000 * FT, true, 0, 0) == Rgb (205, 30, 30));  // above
	REQUIRE(Warn (500 * FT, 1000 * FT, true, 0, 0) == Rgb (215, 190, 30));  // within 1000 ft
	REQUIRE(Warn (10.0, 1000.0, true, 0, 0) == Scale (Topo (10.0, true, 0, 0), 0.45));
	REQUIRE(Scale (Rgb (200, 100, 50), 2.0) == Rgb (255, 200, 100));
}

TEST_CASE("the map decides land and water", "[colour]")
{
	uint32_t polder = Topo (-5.0, true, 0, 0, 0), lake = Topo (200.0, true, 0, 0, 1), sea = Topo (-50.0, true, 0, 0, 1);
	REQUIRE((G (polder) > R (polder) && G (polder) > B (polder))); // land below sea level: lowland green
	REQUIRE(polder == Topo (0.0, true, 0, 0, 0));
	REQUIRE((B (lake) > R (lake) && B (lake) > G (lake)));          // a lake above sea level: water
	REQUIRE(lake == Topo (0.0, true, 0, 0, 1));
	REQUIRE(sea == Topo (-50.0, true, 0, 0));                       // the map agrees with the height: as before
	REQUIRE(Topo (-5.0, true, 0, 0, -1) == Topo (-5.0, true, 0, 0)); // no map: by height
	REQUIRE(Topo (300.0, false, 0, 600, 1) == Topo (300.0, false, 0, 600)); // a body without a sea ignores it
	REQUIRE(Warn (10.0, 1000.0, true, 0, 0, 1) == Scale (Topo (10.0, true, 0, 0, 1), 0.45));
	REQUIRE(IsWater (100.0, 1));
	REQUIRE_FALSE(IsWater (-100.0, 0));
	REQUIRE(IsWater (-1.0, -1));
	REQUIRE_FALSE(IsWater (1.0, -1));
}
