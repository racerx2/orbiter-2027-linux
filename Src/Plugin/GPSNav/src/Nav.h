// GPSNav: the flight plan, Direct-To, and the guidance to the active leg
#pragma once
#include "Data.h"
#include <string>
#include <vector>

namespace gps {

struct PlanWpt {
	std::string ident, name;
	Kind kind = Kind::User;
	LatLon pos;
	double elev = 0.0;
};
PlanWpt ToPlan (const Wpt &w);

struct Plan {
	std::string body;              // the body the waypoints are on
	std::vector<PlanWpt> wpts;
	int active = -1;               // the leg to wpts[active] from wpts[active - 1]; -1: no flight plan leg
	bool direct = false;           // Direct-To, in place of the flight plan leg
	PlanWpt dto;
	LatLon dfrom;                  // where Direct-To was set

	void Insert (int at, const PlanWpt &w);
	void Remove (int at);
	void Invert ();
	void Activate (int to);        // the leg to wpts[to]
	void DirectTo (const PlanWpt &w, LatLon from);
	bool HasLeg () const { return direct || (active >= 0 && active < (int)wpts.size ()); }
	std::string Save () const;     // one line, for the scenario
	bool Load (const std::string &line);
};

// what the unit shows about the active leg; distances in metres, angles in radians, speeds in m/s, time in s
struct Guidance {
	bool leg = false;
	LatLon from, to;
	std::string toIdent;
	double dis = 0.0, brg = 0.0, dtk = 0.0, xtk = 0.0, ete = -1.0;
	double gs = 0.0, trk = 0.0, vs = 0.0;
	bool moving = false;           // ground speed of at least 1 m/s: track and times mean something
	bool past = false;             // past the leg's waypoint (the CDI's FROM)
};

// pos: the vessel; gE, gN, gU: its ground speed east, north and up (the horizon frame)
Guidance Guide (const Plan &p, LatLon pos, double R, double gE, double gN, double gU);
// the flight plan's next leg once the vessel passes its waypoint; true if it moved on
bool Sequence (Plan &p, LatLon pos, double R, double gs);

// vertical: the glide path's height above a threshold at a distance from it, and the vertical speed a path angle asks at a ground speed
inline double PathHeight (double dist, double angle, double tch) { return tch + dist * std::tan (angle); }
inline double PathVs (double gs, double angle) { return -gs * std::tan (angle); }
// the vertical speed to reach an altitude over a distance at a ground speed; 0 if not moving
inline double RequiredVs (double alt, double target, double dist, double gs) { return gs < 1.0 || dist < 1.0 ? 0.0 : (target - alt) * gs / dist; }

// the CDI's full scale (m): mode 0 AUTO (5 nm, 1 nm within 30 nm of the waypoint), 1 to 3 fixed 5, 1, 0.3 nm
inline double CdiScale (int mode, double dis)
{
	const double fixed[4] = {0.0, 5.0 * NM, 1.0 * NM, 0.3 * NM};
	return mode > 0 && mode < 4 ? fixed[mode] : dis < 30.0 * NM ? 1.0 * NM : 5.0 * NM;
}
// its needle, -1 (full left) to 1: the course is where it points, so left when the vessel is right of it
inline double CdiNeedle (double xtk, double scale) { return std::fmax (-1.0, std::fmin (1.0, -xtk / scale)); }

// the GPS satellites' altitude over Earth's mean radius, and how far below it a lost signal comes back
constexpr double GPS_ALT = 20180e3, GPS_REACQ = 10e3;

// a GPS fix: only on Earth (the one body with satellites), below them; had: the last answer, kept in the band between
inline bool Signal (const std::string &body, double alt, bool had)
{
	const char *e = "earth";
	if (body.size () != 5) return false;
	for (int i = 0; i < 5; i++) if ((body[i] | 0x20) != e[i]) return false;
	return alt < (had ? GPS_ALT : GPS_ALT - GPS_REACQ);
}

// GPS roll steering: the track to fly (onto the leg, an intercept that eases as the line nears, at most 45 degrees)
inline double GpssTrack (double dtk, double xtk, double gs)
{
	double L = std::fmax (NM, 30.0 * gs);
	return Wrap2Pi (dtk - std::fmax (-0.25 * PI, std::fmin (0.25 * PI, std::atan (xtk / L))));
}
// and the bank for it, at most 25 degrees; in Orbiter's sense (GetBank): negative is right wing down, a turn right
inline double GpssBank (double want, double trk)
{
	return std::fmax (-25.0 * RAD, std::fmin (25.0 * RAD, -1.5 * WrapPi (want - trk)));
}

} // namespace gps
