// GPSNav tests: the files the database is read from
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Data.h"

using namespace gps;
using Catch::Matchers::WithinAbs;

static const std::string T = GPSNAV_TESTDATA;

TEST_CASE("CSV fields with quotes", "[data]")
{
	auto f = CsvFields ("a,\"b, c\",\"say \"\"hi\"\"\",,e\r");
	REQUIRE(f.size () == 5);
	REQUIRE(f[1] == "b, c");
	REQUIRE(f[2] == "say \"hi\"");
	REQUIRE(f[3].empty ());
	REQUIRE(f[4] == "e");
}

TEST_CASE("the add-on's airports, runways and navaids", "[data]")
{
	Body b;
	b.R = 6371010;
	REQUIRE(ReadAirports (T + "/airports.csv", "OurAirports", b) == 4);
	REQUIRE(ReadRunways (T + "/runways.csv", b) == 2); // one of an airport not there
	REQUIRE(ReadNavaids (T + "/navaids.csv", "OurAirports", b) == 2);
	REQUIRE(ReadAirports (T + "/none.csv", "User", b) == -1);
	REQUIRE(b.wpts.size () == 6);
	REQUIRE(b.wpts[0].name == "Cape Canaveral Space Force Station Skid Strip, FL");
	REQUIRE(b.wpts[2].size == 2);
	REQUIRE(b.wpts[3].kind == Kind::Heliport);
	REQUIRE(b.wpts[1].runways.size () == 1);
	const Runway &r = b.rwys[b.wpts[1].runways[0]];
	REQUIRE((r.id1 == "15" && r.id2 == "33"));
	REQUIRE(b.wpts[4].kind == Kind::Vor);
	REQUIRE_THAT(b.wpts[4].freq, WithinAbs (112.2, 1e-4));
	REQUIRE(b.wpts[5].kind == Kind::Ndb);
	b.Index ();
	auto near = b.Nearest (b.wpts[1].pos, 3, [] (const Wpt &w) { return IsAirport (w.kind); });
	REQUIRE(near.size () == 3);
	REQUIRE(near[0] == 1);
	auto f = b.Find ("KM", b.wpts[1].pos, 5);
	REQUIRE((f.size () == 1 && f[0] == 2));
	f = b.Find ("orl", b.wpts[1].pos, 5); // names as well as idents, the ident first
	REQUIRE(f.size () == 3);
	REQUIRE(b.wpts[f[0]].ident == "ORL");
}

TEST_CASE("Orbiter's beacons and a base's runways", "[data]")
{
	Body b;
	b.R = 6.37101e6;
	REQUIRE(ReadNavbeacons (T + "/Earth.cfg", b) == 3);
	REQUIRE(b.wpts[2].ident == "TNV");
	REQUIRE(b.wpts[2].name == "NAVASOTA");
	REQUIRE(b.wpts[2].tunable);
	REQUIRE_THAT(b.wpts[2].pos.lng * DEG, WithinAbs (-96.0582, 1e-9));
	REQUIRE(ReadNavbeacons (T + "/Canaveral.cfg", b) == 1);
	REQUIRE(b.wpts.back ().ident == "KSCX");

	std::vector<int> added;
	REQUIRE(ReadBaseRunways (T + "/Canaveral.cfg", {28.58438 * RAD, -80.65165 * RAD}, b, added) == 2);
	const Runway &slf = b.rwys[added[0]];
	REQUIRE((slf.id1 == "33" && slf.id2 == "15")); // the Shuttle Landing Facility, END1 at its south end
	LatLon mid = {0.5 * (slf.e1.lat + slf.e2.lat), 0.5 * (slf.e1.lng + slf.e2.lng)};
	REQUIRE_THAT(mid.lat * DEG, WithinAbs (28.615, 0.002));
	REQUIRE_THAT(mid.lng * DEG, WithinAbs (-80.6946, 0.002));
	REQUIRE_THAT(slf.length, WithinAbs (5131.2, 0.5));
	REQUIRE_THAT(slf.width, WithinAbs (100.0, 1e-9));
	REQUIRE_THAT(slf.ils1, WithinAbs (134.2, 1e-4));

	Body m;
	m.R = 1.73753e6;
	REQUIRE(ReadNavbeacons (T + "/Brighton.cfg", m) == 1);
	REQUIRE((m.wpts[0].ident == "LBB" && m.wpts[0].kind == Kind::Vor));
	REQUIRE_THAT(m.wpts[0].freq, WithinAbs (116.3, 1e-4));
}

TEST_CASE("map lines", "[data]")
{
	Body b;
	REQUIRE(ReadVec (T + "/test.vec", b) == 2);
	REQUIRE(b.lines[0].size () == 3);
	REQUIRE_THAT(b.lines[1][1].lat * DEG, WithinAbs (51.0, 1e-12));
	REQUIRE_THAT(b.lines[1][1].lng * DEG, WithinAbs (11.0, 1e-12));
}

TEST_CASE("runway idents from headings", "[data]")
{
	REQUIRE(RunwayIdent (330.0 * RAD) == "33");
	REQUIRE(RunwayIdent (4.0 * RAD) == "36");
	REQUIRE(RunwayIdent (6.0 * RAD) == "01");
	REQUIRE(RunwayIdent (184.9 * RAD) == "18");
}

TEST_CASE("a user's airports after OurAirports", "[data]")
{
	Body b;
	b.R = 6371010;
	ReadAirports (T + "/airports.csv", "OurAirports", b);
	ReadRunways (T + "/runways.csv", b);
	REQUIRE(ReadAirports (T + "/user/airports-home.csv", "User", b) == 2);
	REQUIRE(ReadRunways (T + "/user/runways-home.csv", b) == 2);
	const Wpt &ours = b.wpts[1], &mine = b.wpts[4], &home = b.wpts[5];
	REQUIRE((ours.ident == "KTTS" && mine.ident == "KTTS" && mine.source == "User"));
	REQUIRE((ours.runways.size () == 1 && b.rwys[ours.runways[0]].id1 == "15")); // OurAirports' runway stays with its airport
	REQUIRE((mine.runways.size () == 1 && b.rwys[mine.runways[0]].id1 == "09")); // the user's goes to the user's airport
	REQUIRE(home.name == "Home, Strip");
	const Runway &r = b.rwys[home.runways[0]];
	REQUIRE((r.id1 == "18" && r.id2 == "36")); // empty idents from the heading
}

TEST_CASE("the shipped Earth data", "[data]")
{
	Body b;
	b.R = 6371010;
	REQUIRE(ReadAirports (std::string (GPSNAV_DATA) + "/Earth/airports.csv", "OurAirports", b) > 70000);
	REQUIRE(ReadRunways (std::string (GPSNAV_DATA) + "/Earth/runways.csv", b) > 30000);
	REQUIRE(ReadNavaids (std::string (GPSNAV_DATA) + "/Earth/navaids.csv", "OurAirports", b) > 10000);
	b.Index ();
	auto f = b.Find ("KTTS", {28.6 * RAD, -80.7 * RAD}, 1);
	REQUIRE(f.size () == 1);
	const Wpt &w = b.wpts[f[0]];
	REQUIRE(w.runways.size () == 1);
	REQUIRE_THAT(b.rwys[w.runways[0]].length, WithinAbs (4572.0, 1.0));
	f = b.Find ("OMN", {29.3 * RAD, -81.1 * RAD}, 1);
	REQUIRE((f.size () == 1 && b.wpts[f[0]].kind == Kind::Vor));
	REQUIRE_THAT(b.wpts[f[0]].freq, WithinAbs (112.6, 1e-4));
}

TEST_CASE("marker files", "[data]")
{
	Body b;
	REQUIRE(ReadMarkers (T + "/City.mkr", true, b) == 3);
	REQUIRE((b.wpts[0].ident == "Shanghai" && b.wpts[0].kind == Kind::Landmark && b.wpts[0].size == 1));
	REQUIRE_THAT(b.wpts[0].pos.lng * DEG, WithinAbs (121.45806, 1e-9));
	REQUIRE(b.wpts[1].name == "London");
	REQUIRE_THAT(b.wpts[2].pos.lng * DEG, WithinAbs (-160.0, 1e-9)); // 200 east is 160 west
	REQUIRE(ReadMarkers (T + "/none.mkr", false, b) == -1);
}
