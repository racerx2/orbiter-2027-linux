// GPSNav: the waypoint database of one body, and the readers of its sources (none of them needs Orbiter)
#pragma once
#include "Geo.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gps {

enum class Kind : uint8_t { Airport, Heliport, Seaplane, Base, Pad, Vor, Ndb, Dme, Ils, User, Landmark }; // Landmark: a body's marker (size 1: a city)
const char *KindName (Kind k);
bool IsAirport (Kind k); // airports, heliports, seaplane bases and Orbiter bases
bool IsNavaid (Kind k);

struct Runway {
	std::string id1, id2;     // "15", "33L"
	LatLon e1, e2;            // the ends (thresholds)
	double elev1 = 0.0, elev2 = 0.0, width = 0.0, length = 0.0; // metres
	float ils1 = 0.0f, ils2 = 0.0f; // MHz: the ILS landing toward e2 (from e1's side) and toward e1; 0: none
	std::string surface;
};

struct Wpt {
	std::string ident, name;
	Kind kind = Kind::User;
	LatLon pos;
	double elev = 0.0;        // metres above the body's mean radius
	float freq = 0.0f;        // MHz (VOR, ILS, VTOL pad), kHz (NDB); 0: none
	int size = 0;             // airports: 0 small, 1 medium, 2 large
	bool tunable = false;     // an Orbiter beacon: a NAV receiver can tune it
	std::string source;       // "Orbiter", "OurAirports", "User"
	std::vector<int> runways; // into Body::rwys
};

// a body's waypoints, runways and map lines; a 1-degree grid for the nearest lists and the map
class Body {
public:
	std::string name;
	double R = 1.0;
	std::vector<Wpt> wpts;
	std::vector<Runway> rwys;
	std::vector<std::vector<LatLon>> lines; // coastlines or contours
	bool coast = false;                      // the lines are a coast: the body has a sea at 0 m

	void Index ();                                   // after the lists change
	template <class F> void Near (LatLon p, double ang, F f) const; // f(index) for every waypoint within ang (a superset)
	std::vector<int> Nearest (LatLon p, int n, bool (*want) (const Wpt &)) const;
	std::vector<int> Find (const std::string &text, LatLon from, int n) const; // ident (or name) starts with text, nearest first

private:
	static int Cell (double lat, double lng);
	std::unordered_map<int, std::vector<int>> grid;
};

// the add-on's CSV files (OurAirports converted, and the user's): one per kind, a header line, comma separated, quotes for commas
//   airports.csv  ident,name,type,lat,lon,elev_m          type: large_airport medium_airport small_airport heliport seaplane_base
//   runways.csv   airport,id1,lat1,lon1,elev1_m,id2,lat2,lon2,elev2_m,width_m,length_m,surface
//   navaids.csv   ident,name,type,freq,lat,lon,elev_m     type: VOR VOR-DME VORTAC TACAN DME NDB NDB-DME; freq MHz, NDB kHz
//   waypoints.csv ident,name,lat,lon,elev_m
std::vector<std::string> CsvFields (const std::string &line);
int ReadAirports (const std::string &path, const std::string &source, Body &b);
int ReadRunways (const std::string &path, Body &b);
int ReadNavaids (const std::string &path, const std::string &source, Body &b);
int ReadWaypoints (const std::string &path, const std::string &source, Body &b);

// Orbiter's files
// a body's cfg or a base's: the NAVBEACON list (TYPE IDENT LNG LAT FREQ [RANGE] [; name])
int ReadNavbeacons (const std::string &path, Body &b);
// a base's file: its RUNWAY blocks, the ends in its frame (x south, z east, metres) about the base at ref
int ReadBaseRunways (const std::string &path, LatLon ref, Body &b, std::vector<int> &added);
// a Map MFD line file (coast.vec, contour.vec)
int ReadVec (const std::string &path, Body &b);
// a marker file (Config/<body>/Marker/*.mkr): BEGIN_DATA lines "lng lat : name"
int ReadMarkers (const std::string &path, bool city, Body &b);

std::string RunwayIdent (double hdg); // "01" to "36" from a true heading

} // namespace gps

template <class F> void gps::Body::Near (LatLon p, double ang, F f) const
{
	double dlat = ang * DEG + 1.0;
	double c = std::cos (p.lat);
	double dlng = c > 1e-3 ? std::fmin (180.0, dlat / c) : 180.0;
	int la0 = (int)std::floor (p.lat * DEG - dlat), la1 = (int)std::floor (p.lat * DEG + dlat);
	int lo0 = (int)std::floor (p.lng * DEG - dlng), lo1 = (int)std::floor (p.lng * DEG + dlng);
	if (lo1 - lo0 >= 360) lo0 = -180, lo1 = 179;
	for (int la = std::max (la0, -90); la <= std::min (la1, 89); la++)
		for (int lo = lo0; lo <= lo1; lo++) {
			int l = ((lo + 180) % 360 + 360) % 360 - 180;
			auto it = grid.find (Cell ((la + 0.5) * RAD, (l + 0.5) * RAD));
			if (it != grid.end ()) for (int i : it->second) f (i);
		}
}
