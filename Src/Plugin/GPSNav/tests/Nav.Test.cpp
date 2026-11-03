// GPSNav tests: the flight plan and the guidance
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Nav.h"

using namespace gps;
using Catch::Matchers::WithinAbs;

static LatLon D (double lat, double lng) { return {lat * RAD, lng * RAD}; }
static PlanWpt P (const char *id, double lat, double lng) { PlanWpt w; w.ident = id; w.name = std::string ("the ") + id; w.pos = {lat * RAD, lng * RAD}; return w; }

TEST_CASE("a flight plan: legs, guidance, sequencing", "[nav]")
{
	const double R = 6371e3;
	Plan p;
	p.Insert (0, P ("A", 0, 0));
	REQUIRE((p.HasLeg () && p.active == 0)); // one waypoint: to it from here
	Guidance g0 = Guide (p, {1.0 * RAD, 0.0}, R, 0.0, -100.0, 0.0);
	REQUIRE((g0.leg && g0.toIdent == "A"));
	REQUIRE_THAT(g0.dtk * DEG, WithinAbs (180.0, 1e-6));
	p.Insert (1, P ("B", 0, 1));
	p.Insert (2, P ("C", 1, 1));
	REQUIRE(p.active == 0);
	REQUIRE(!Sequence (p, {0.1 * RAD, 0.0}, R, 100.0));
	REQUIRE(Sequence (p, {0.001 * RAD, 0.0}, R, 100.0)); // A reached: the leg A to B
	REQUIRE(p.active == 1);
	Guidance g = Guide (p, {-0.01 * RAD, 0.5 * RAD}, R, 100.0, 0.0, -2.0); // south of the leg, flying east
	REQUIRE(g.leg);
	REQUIRE(g.toIdent == "B");
	REQUIRE_THAT(g.xtk, WithinAbs (0.01 * RAD * R, 1.0)); // right of the course
	REQUIRE_THAT(g.dtk * DEG, WithinAbs (90.0, 0.01));
	REQUIRE_THAT(g.trk * DEG, WithinAbs (90.0, 1e-9));
	REQUIRE_THAT(g.ete, WithinAbs (0.5 * RAD * R / 100.0, 2.0));
	REQUIRE(!Sequence (p, {0.0, 0.5 * RAD}, R, 100.0));
	REQUIRE(Sequence (p, {0.0, 0.9999 * RAD}, R, 100.0)); // within the lead: on to C
	REQUIRE(p.active == 2);
	REQUIRE(!Sequence (p, {1.0 * RAD, 1.0 * RAD}, R, 100.0)); // the last leg stays

	p.DirectTo (P ("B", 0, 1), {0.5 * RAD, 0.5 * RAD});
	REQUIRE((p.direct && p.active == 1));
	REQUIRE(Sequence (p, {0.0, 0.99999 * RAD}, R, 100.0)); // B reached: on along the plan
	REQUIRE((!p.direct && p.active == 2));

	p.Remove (0);
	REQUIRE(p.active == 1);
	p.Invert ();
	REQUIRE((p.wpts[0].ident == "C" && p.active == 1));
}

TEST_CASE("the flight plan in the scenario", "[nav]")
{
	Plan p;
	p.body = "Earth";
	p.Insert (0, P ("KTTS", 28.615, -80.6945));
	p.Insert (1, P ("ORL", 28.5427, -81.3348));
	p.DirectTo (P ("KMCO", 28.4294, -81.309), {28.6 * RAD, -80.7 * RAD});
	p.wpts[0].name = "Shuttle; Landing/Facility";
	REQUIRE(p.Save ().find (';') == std::string::npos); // Orbiter cuts a scenario line at ';'
	Plan q;
	REQUIRE(q.Load (p.Save ()));
	REQUIRE(q.wpts[0].name == "Shuttle  Landing Facility");
	REQUIRE(q.body == "Earth");
	REQUIRE(q.wpts.size () == 2);
	REQUIRE(q.wpts[1].ident == "ORL");
	REQUIRE(q.wpts[1].name == "the ORL");
	REQUIRE_THAT(q.wpts[1].pos.lng * DEG, WithinAbs (-81.3348, 1e-8));
	REQUIRE((q.direct && q.dto.ident == "KMCO" && q.active == p.active));
	REQUIRE_THAT(q.dfrom.lat * DEG, WithinAbs (28.6, 1e-8));
	p.Insert (2, P ("Shanghai", 31.2, 121.5));
	p.wpts[2].kind = Kind::Landmark;
	REQUIRE((q.Load (p.Save ()) && q.wpts[2].kind == Kind::Landmark));
	REQUIRE(!q.Load ("rubbish"));
}

TEST_CASE("vertical guidance", "[nav]")
{
	REQUIRE_THAT(PathHeight (5 * NM, 3.0 * RAD, 15.0), WithinAbs (15.0 + 5 * NM * std::tan (3.0 * RAD), 1e-9));
	REQUIRE_THAT(PathVs (70.0, 3.0 * RAD), WithinAbs (-3.6685, 1e-3));
	REQUIRE_THAT(RequiredVs (3000.0, 1000.0, 20000.0, 100.0), WithinAbs (-10.0, 1e-12));
	REQUIRE(RequiredVs (3000.0, 1000.0, 20000.0, 0.0) == 0.0);
}

TEST_CASE("the CDI", "[nav]")
{
	REQUIRE(CdiScale (0, 40.0 * NM) == 5.0 * NM);
	REQUIRE(CdiScale (0, 20.0 * NM) == 1.0 * NM);
	REQUIRE(CdiScale (3, 100.0 * NM) == 0.3 * NM);
	REQUIRE(CdiNeedle (0.5 * NM, 1.0 * NM) == -0.5);   // right of course: the needle (the course) left
	REQUIRE(CdiNeedle (-3.0 * NM, 1.0 * NM) == 1.0);   // pegged
	Plan p;
	p.Insert (0, P ("A", 0.0, 0.0));
	p.Insert (1, P ("B", 0.0, 1.0));
	p.Activate (1); // the leg A to B (a new plan starts with the leg to A)
	REQUIRE(!Guide (p, {0.0, 0.5 * RAD}, 6371e3, 100, 0, 0).past);
	REQUIRE(Guide (p, {0.0, 1.1 * RAD}, 6371e3, 100, 0, 0).past); // past B
}

TEST_CASE("GPS roll steering", "[nav]")
{
	double dtk = 90.0 * RAD;
	REQUIRE_THAT(GpssTrack (dtk, 0.0, 100.0), WithinAbs (dtk, 1e-12));               // on the line: its track
	REQUIRE(GpssTrack (dtk, 2.0 * NM, 100.0) < dtk);                                 // right of it: turn left onto it
	REQUIRE_THAT(GpssTrack (dtk, 100.0 * NM, 100.0), WithinAbs (45.0 * RAD, 1e-9));  // far off: 45 degrees
	REQUIRE(GpssBank (100.0 * RAD, 90.0 * RAD) < 0.0);                               // a turn right: negative bank
	REQUIRE_THAT(GpssBank (200.0 * RAD, 90.0 * RAD), WithinAbs (-25.0 * RAD, 1e-12)); // at most 25 degrees
	REQUIRE_THAT(GpssBank (350.0 * RAD, 10.0 * RAD), WithinAbs (25.0 * RAD, 1e-12));  // across north: left
}

TEST_CASE("the desired track past the leg's end", "[nav]")
{
	Plan p;
	p.Insert (0, P ("A", 28.615, -80.6945));
	p.Insert (1, P ("B", 28.102, -80.641));
	p.Activate (1);
	double c = Bearing (p.wpts[0].pos, p.wpts[1].pos) * DEG;
	Guidance before = Guide (p, D (28.7, -80.8), 6371e3, 0, -100, 0), past = Guide (p, D (27.9, -81.4), 6371e3, 0, -100, 0);
	REQUIRE_THAT(before.dtk * DEG, WithinAbs (c, 0.5));
	REQUIRE_THAT(past.dtk * DEG, WithinAbs (c, 0.5));   // not turned back to B
	REQUIRE(past.past);
}

TEST_CASE("the GPS signal: Earth below the satellites", "[nav]")
{
	CHECK(Signal ("Earth", 0.0, true));
	CHECK(Signal ("Earth", 20000e3, true));
	CHECK_FALSE(Signal ("Earth", GPS_ALT, true)); // at the satellites: gone
	CHECK_FALSE(Signal ("Earth", 36000e3, true));
	CHECK(Signal ("earth", 400e3, false));
	// back in range only 10 km below the line; going out, the line itself
	CHECK_FALSE(Signal ("Earth", GPS_ALT - 5e3, false));
	CHECK(Signal ("Earth", GPS_ALT - 5e3, true));
	CHECK(Signal ("Earth", GPS_ALT - GPS_REACQ - 1.0, false));
	for (const char *b : {"Moon", "Mars", "Venus", "Earth2", "Eart", ""}) {
		INFO(b);
		CHECK_FALSE(Signal (b, 0.0, true));
		CHECK_FALSE(Signal (b, 0.0, false));
	}
}
