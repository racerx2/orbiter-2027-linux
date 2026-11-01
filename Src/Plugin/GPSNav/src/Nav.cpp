// GPSNav: the flight plan and the guidance
#include "Nav.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace gps {

PlanWpt ToPlan (const Wpt &w) { return {w.ident, w.name, w.kind, w.pos, w.elev}; }

void Plan::Insert (int at, const PlanWpt &w)
{
	at = std::clamp (at, 0, (int)wpts.size ());
	wpts.insert (wpts.begin () + at, w);
	if (active >= at) active++;
	if (active < 0) active = 0; // the first waypoint: a leg to it from where the vessel is
}

void Plan::Remove (int at)
{
	if (at < 0 || at >= (int)wpts.size ()) return;
	wpts.erase (wpts.begin () + at);
	if (active > at || active >= (int)wpts.size ()) active--;
	if (wpts.size () < 2) active = wpts.empty () ? -1 : 0;
}

void Plan::Invert ()
{
	std::reverse (wpts.begin (), wpts.end ());
	active = wpts.size () >= 2 ? 1 : wpts.empty () ? -1 : 0;
	direct = false;
}

void Plan::Activate (int to)
{
	if (to < 0 || to >= (int)wpts.size ()) return;
	active = to;
	direct = false;
}

void Plan::DirectTo (const PlanWpt &w, LatLon from)
{
	direct = true, dto = w, dfrom = from;
	for (int i = 0; i < (int)wpts.size (); i++) // a flight plan waypoint: its leg follows
		if (wpts[i].ident == w.ident && Angle (wpts[i].pos, w.pos) < 1e-7) { active = i; break; }
}

namespace {

std::string Esc (const std::string &s) // the line's separators out of names, and ';' (a comment in a scenario)
{
	std::string r;
	for (char c : s) r += (c == ',' || c == ';' || c == '|' || c == '/') ? ' ' : c;
	return r;
}

std::string One (const PlanWpt &w)
{
	char b[128];
	snprintf (b, sizeof b, ",%.9f,%.9f,%.1f,%d,", w.pos.lat * DEG, w.pos.lng * DEG, w.elev, (int)w.kind);
	return Esc (w.ident) + b + Esc (w.name);
}

bool Parse (const std::string &s, PlanWpt &w)
{
	std::vector<std::string> f;
	std::stringstream ss (s);
	std::string t;
	while (std::getline (ss, t, ',')) f.push_back (t);
	if (f.size () < 6 || f[0].empty ()) return false;
	w.ident = f[0];
	w.pos = {atof (f[1].c_str ()) * RAD, atof (f[2].c_str ()) * RAD};
	w.elev = atof (f[3].c_str ());
	int k = atoi (f[4].c_str ());
	w.kind = k >= 0 && k <= (int)Kind::Landmark ? (Kind)k : Kind::User;
	w.name = f[5];
	return true;
}

} // namespace

// body|active|direct|dto|dfrom lat,lng|wpt/wpt/... (no ';': Orbiter cuts a scenario line there)
std::string Plan::Save () const
{
	char b[96];
	snprintf (b, sizeof b, "|%d|%d|", active, direct ? 1 : 0);
	std::string s = Esc (body) + b + (direct ? One (dto) : std::string ());
	snprintf (b, sizeof b, "|%.9f,%.9f|", dfrom.lat * DEG, dfrom.lng * DEG);
	s += b;
	for (size_t i = 0; i < wpts.size (); i++) s += (i ? "/" : "") + One (wpts[i]);
	return s;
}

bool Plan::Load (const std::string &line)
{
	std::vector<std::string> f;
	std::stringstream ss (line);
	std::string t;
	while (std::getline (ss, t, '|')) f.push_back (t);
	if (f.size () < 5) return false;
	*this = Plan ();
	body = f[0];
	active = atoi (f[1].c_str ());
	direct = atoi (f[2].c_str ()) != 0 && Parse (f[3], dto);
	double la = 0, lo = 0;
	if (sscanf (f[4].c_str (), "%lf,%lf", &la, &lo) == 2) dfrom = {la * RAD, lo * RAD};
	if (f.size () > 5) {
		std::stringstream ws (f[5]);
		while (std::getline (ws, t, '/')) { PlanWpt w; if (Parse (t, w)) wpts.push_back (w); }
	}
	if (active >= (int)wpts.size ()) active = (int)wpts.size () - 1;
	if (active < 0 && !wpts.empty ()) active = 0;
	return true;
}

Guidance Guide (const Plan &p, LatLon pos, double R, double gE, double gN, double gU)
{
	Guidance g;
	g.gs = std::hypot (gE, gN);
	g.vs = gU;
	g.moving = g.gs >= 1.0;
	if (g.moving) g.trk = Wrap2Pi (std::atan2 (gE, gN));
	if (!p.HasLeg ()) return g;
	g.leg = true;
	if (p.direct) g.from = p.dfrom, g.to = p.dto.pos, g.toIdent = p.dto.ident;
	else {
		g.to = p.wpts[p.active].pos, g.toIdent = p.wpts[p.active].ident;
		g.from = p.active > 0 ? p.wpts[p.active - 1].pos : pos;
	}
	g.dis = Angle (pos, g.to) * R;
	g.brg = Bearing (pos, g.to);
	bool leg = Angle (g.from, g.to) > 1e-9;
	if (leg) { // the course here: the great circle's direction at the vessel's foot on it, the same before the leg, on it and past its end
		double c0 = Bearing (g.from, g.to), at = AlongTrack (g.from, g.to, pos);
		g.dtk = Bearing (Destination (g.from, c0, at), Destination (g.from, c0, at + 1e-5));
	} else g.dtk = g.brg;
	g.xtk = leg ? CrossTrack (g.from, g.to, pos) * R : 0.0;
	g.past = leg && AlongTrack (g.from, g.to, pos) > Angle (g.from, g.to);
	if (g.moving) {
		double closing = g.gs * std::cos (g.trk - g.brg);
		g.ete = closing > 0.1 ? g.dis / closing : -1.0;
	}
	return g;
}

bool Sequence (Plan &p, LatLon pos, double R, double gs)
{
	if (p.direct) {
		if (Angle (pos, p.dto.pos) * R > 0.3 * NM) return false;
		for (int i = 0; i < (int)p.wpts.size (); i++) // reached: on along the flight plan from it, if it is in it
			if (p.wpts[i].ident == p.dto.ident && i + 1 < (int)p.wpts.size ()) { p.direct = false; p.active = i + 1; return true; }
		return false;
	}
	if (p.active < 0 || p.active + 1 >= (int)p.wpts.size ()) return false;
	double lead = std::fmin (std::fmax (gs * 10.0, 0.1 * NM), 2.0 * NM); // a few seconds before, as a turn starts
	if (p.active == 0) { // from where the vessel was: the first waypoint reached
		if (Angle (pos, p.wpts[0].pos) * R > std::fmax (lead, 0.3 * NM)) return false;
		p.active = 1;
		return true;
	}
	LatLon a = p.wpts[p.active - 1].pos, b = p.wpts[p.active].pos;
	double len = Angle (a, b) * R, at = AlongTrack (a, b, pos) * R;
	if (at < len - lead) return false;
	p.active++;
	return true;
}

} // namespace gps
