// GPSNav: the waypoint database and the readers of its files
#include "Data.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <strings.h>

namespace gps {

const char *KindName (Kind k)
{
	switch (k) {
	case Kind::Airport: return "Airport";
	case Kind::Heliport: return "Heliport";
	case Kind::Seaplane: return "Seaplane base";
	case Kind::Base: return "Base";
	case Kind::Pad: return "Pad";
	case Kind::Vor: return "VOR";
	case Kind::Ndb: return "NDB";
	case Kind::Dme: return "DME";
	case Kind::Ils: return "ILS";
	case Kind::Landmark: return "Landmark";
	default: return "User";
	}
}

bool IsAirport (Kind k) { return k == Kind::Airport || k == Kind::Heliport || k == Kind::Seaplane || k == Kind::Base; }
bool IsNavaid (Kind k) { return k == Kind::Vor || k == Kind::Ndb || k == Kind::Dme || k == Kind::Ils; }

int Body::Cell (double lat, double lng)
{
	int la = std::clamp ((int)std::floor (lat * DEG), -90, 89);
	int lo = ((int)std::floor (lng * DEG) + 180) % 360;
	if (lo < 0) lo += 360;
	return (la + 90) * 360 + lo;
}

void Body::Index ()
{
	grid.clear ();
	for (int i = 0; i < (int)wpts.size (); i++) grid[Cell (wpts[i].pos.lat, wpts[i].pos.lng)].push_back (i);
}

std::vector<int> Body::Nearest (LatLon p, int n, bool (*want) (const Wpt &)) const
{
	std::vector<std::pair<double, int>> v;
	for (double ang = 2.0 * RAD; ang < 4.0; ang *= 4.0) { // widen until n are found
		v.clear ();
		Near (p, ang, [&] (int i) { if (want (wpts[i])) v.push_back ({Angle (p, wpts[i].pos), i}); });
		int inside = 0;
		for (auto &e : v) if (e.first <= ang) inside++;
		if (inside >= n) break;
	}
	std::sort (v.begin (), v.end ());
	std::vector<int> r;
	for (size_t i = 0; i < v.size () && (int)r.size () < n; i++) r.push_back (v[i].second);
	return r;
}

std::vector<int> Body::Find (const std::string &text, LatLon from, int n) const
{
	std::vector<std::pair<std::pair<int, double>, int>> v; // (an ident match first, distance), index
	if (text.empty ()) return {};
	for (int i = 0; i < (int)wpts.size (); i++) {
		const Wpt &w = wpts[i];
		bool id = !strncasecmp (w.ident.c_str (), text.c_str (), text.size ());
		bool nm = !id && text.size () >= 3 && !strncasecmp (w.name.c_str (), text.c_str (), text.size ());
		if (!id && !nm) continue;
		bool exact = id && w.ident.size () == text.size ();
		v.push_back ({{exact ? 0 : id ? 1 : 2, Angle (from, w.pos)}, i});
	}
	std::sort (v.begin (), v.end ());
	std::vector<int> r;
	for (size_t i = 0; i < v.size () && (int)r.size () < n; i++) r.push_back (v[i].second);
	return r;
}

std::string RunwayIdent (double hdg)
{
	int n = (int)std::lround (Wrap2Pi (hdg) * DEG / 10.0);
	if (n <= 0) n += 36;
	char b[16];
	snprintf (b, sizeof b, "%02d", n > 36 ? n - 36 : n);
	return b;
}

// ---------------------------------------------------------------------
// the add-on's CSV files

std::vector<std::string> CsvFields (const std::string &line)
{
	std::vector<std::string> f (1);
	bool q = false;
	for (size_t i = 0; i < line.size (); i++) {
		char c = line[i];
		if (q) {
			if (c == '"' && i + 1 < line.size () && line[i + 1] == '"') f.back () += '"', i++;
			else if (c == '"') q = false;
			else f.back () += c;
		} else if (c == '"') q = true;
		else if (c == ',') f.emplace_back ();
		else if (c != '\r' && c != '\n') f.back () += c;
	}
	return f;
}

namespace {

double Num (const std::string &s, double def = 0.0)
{
	char *e;
	double v = strtod (s.c_str (), &e);
	return e == s.c_str () ? def : v;
}

bool HasNum (const std::string &s)
{
	char *e;
	strtod (s.c_str (), &e);
	return e != s.c_str ();
}

// the file's rows by the header's column names; missing columns read as empty
template <class F> int Rows (const std::string &path, std::initializer_list<const char *> cols, F f)
{
	std::ifstream in (path);
	if (!in) return -1;
	std::string line;
	if (!std::getline (in, line)) return 0;
	std::vector<std::string> head = CsvFields (line);
	std::vector<int> at;
	for (const char *c : cols) {
		int k = -1;
		for (int j = 0; j < (int)head.size (); j++) if (!strcasecmp (head[j].c_str (), c)) k = j;
		at.push_back (k);
	}
	int n = 0;
	std::vector<std::string> row (at.size ());
	while (std::getline (in, line)) {
		if (line.empty () || line[0] == ';' || line[0] == '#') continue;
		std::vector<std::string> v = CsvFields (line);
		for (size_t j = 0; j < at.size (); j++) row[j] = at[j] >= 0 && at[j] < (int)v.size () ? v[at[j]] : std::string ();
		if (f (row)) n++;
	}
	return n;
}

} // namespace

int ReadAirports (const std::string &path, const std::string &source, Body &b)
{
	return Rows (path, {"ident", "name", "type", "lat", "lon", "elev_m"}, [&] (const std::vector<std::string> &r) {
		if (r[0].empty () || !HasNum (r[3]) || !HasNum (r[4])) return false;
		Wpt w;
		w.ident = r[0], w.name = r[1], w.source = source;
		const std::string &t = r[2];
		w.kind = t == "heliport" ? Kind::Heliport : t == "seaplane_base" ? Kind::Seaplane : Kind::Airport;
		w.size = t == "large_airport" ? 2 : t == "medium_airport" ? 1 : 0;
		w.pos = {Num (r[3]) * RAD, Num (r[4]) * RAD};
		w.elev = Num (r[5]);
		b.wpts.push_back (w);
		return true;
	});
}

int ReadRunways (const std::string &path, Body &b)
{
	std::unordered_map<std::string, int> apt; // the airports by ident; the last read of one ident wins
	for (int i = 0; i < (int)b.wpts.size (); i++)
		if (b.wpts[i].kind == Kind::Airport || b.wpts[i].kind == Kind::Heliport || b.wpts[i].kind == Kind::Seaplane) apt[b.wpts[i].ident] = i;
	return Rows (path, {"airport", "id1", "lat1", "lon1", "elev1_m", "id2", "lat2", "lon2", "elev2_m", "width_m", "length_m", "surface"},
		[&] (const std::vector<std::string> &r) {
		if (r[0].empty () || !HasNum (r[2]) || !HasNum (r[3]) || !HasNum (r[6]) || !HasNum (r[7])) return false;
		Runway w;
		w.e1 = {Num (r[2]) * RAD, Num (r[3]) * RAD};
		w.e2 = {Num (r[6]) * RAD, Num (r[7]) * RAD};
		w.id1 = r[1].empty () ? RunwayIdent (Bearing (w.e1, w.e2)) : r[1];
		w.id2 = r[5].empty () ? RunwayIdent (Bearing (w.e2, w.e1)) : r[5];
		w.elev1 = Num (r[4]), w.elev2 = Num (r[8]), w.width = Num (r[9]), w.length = Num (r[10]);
		w.surface = r[11];
		auto it = apt.find (r[0]);
		if (it == apt.end ()) return false;
		b.rwys.push_back (w);
		b.wpts[it->second].runways.push_back ((int)b.rwys.size () - 1);
		return true;
	});
}

int ReadNavaids (const std::string &path, const std::string &source, Body &b)
{
	return Rows (path, {"ident", "name", "type", "freq", "lat", "lon", "elev_m"}, [&] (const std::vector<std::string> &r) {
		if (r[0].empty () || !HasNum (r[4]) || !HasNum (r[5])) return false;
		Wpt w;
		w.ident = r[0], w.name = r[1], w.source = source;
		const std::string &t = r[2];
		w.kind = !strncasecmp (t.c_str (), "NDB", 3) ? Kind::Ndb : t == "DME" || t == "TACAN" ? Kind::Dme : Kind::Vor;
		w.freq = (float)Num (r[3]);
		w.pos = {Num (r[4]) * RAD, Num (r[5]) * RAD};
		w.elev = Num (r[6]);
		b.wpts.push_back (w);
		return true;
	});
}

int ReadWaypoints (const std::string &path, const std::string &source, Body &b)
{
	return Rows (path, {"ident", "name", "lat", "lon", "elev_m"}, [&] (const std::vector<std::string> &r) {
		if (r[0].empty () || !HasNum (r[2]) || !HasNum (r[3])) return false;
		Wpt w;
		w.ident = r[0], w.name = r[1], w.source = source, w.kind = Kind::User;
		w.pos = {Num (r[2]) * RAD, Num (r[3]) * RAD};
		w.elev = Num (r[4]);
		b.wpts.push_back (w);
		return true;
	});
}

// ---------------------------------------------------------------------
// Orbiter's files

namespace {

std::string Trim (const std::string &s)
{
	size_t a = s.find_first_not_of (" \t\r\n"), e = s.find_last_not_of (" \t\r\n");
	return a == std::string::npos ? std::string () : s.substr (a, e - a + 1);
}

bool Is (const std::string &line, const char *key) { return !strncasecmp (line.c_str (), key, strlen (key)); }

} // namespace

int ReadNavbeacons (const std::string &path, Body &b)
{
	std::ifstream in (path);
	if (!in) return -1;
	std::string line;
	bool on = false;
	int n = 0;
	while (std::getline (in, line)) {
		line = Trim (line);
		if (Is (line, "BEGIN_NAVBEACON")) { on = true; continue; }
		if (Is (line, "END_NAVBEACON")) { on = false; continue; }
		if (!on || line.empty () || line[0] == ';') continue;
		std::string name;
		size_t sc = line.find (';');
		if (sc != std::string::npos) name = Trim (line.substr (sc + 1)), line = line.substr (0, sc);
		std::istringstream ss (line);
		std::string type, ident;
		double lng, lat, freq;
		if (!(ss >> type >> ident >> lng >> lat >> freq)) continue;
		Wpt w;
		w.ident = ident, w.name = name, w.source = "Orbiter", w.tunable = true;
		w.kind = !strcasecmp (type.c_str (), "ILS") ? Kind::Ils : !strcasecmp (type.c_str (), "VOR") ? Kind::Vor : Kind::Vor;
		w.pos = {lat * RAD, lng * RAD};
		w.freq = (float)freq;
		b.wpts.push_back (w);
		n++;
	}
	return n;
}

int ReadBaseRunways (const std::string &path, LatLon ref, Body &b, std::vector<int> &added)
{
	std::ifstream in (path);
	if (!in) return -1;
	std::string line;
	bool rwy = false, has1 = false, has2 = false;
	double x1 = 0, z1 = 0, x2 = 0, z2 = 0, y;
	Runway r;
	int n = 0;
	double c = std::cos (ref.lat);
	auto at = [&] (double x, double z) { return LatLon {ref.lat - x / b.R, ref.lng + z / (b.R * (c > 1e-6 ? c : 1e-6))}; };
	while (std::getline (in, line)) {
		line = Trim (line);
		size_t sc = line.find (';');
		if (sc != std::string::npos) line = Trim (line.substr (0, sc));
		if (!rwy) {
			if (!strcasecmp (line.c_str (), "RUNWAY")) rwy = true, has1 = has2 = false, r = Runway ();
			continue;
		}
		std::istringstream ss (line);
		std::string key;
		ss >> key;
		if (!strcasecmp (key.c_str (), "END")) {
			rwy = false;
			if (!has1 || !has2) continue;
			r.e1 = at (x1, z1), r.e2 = at (x2, z2);
			r.length = std::hypot (x2 - x1, z2 - z1);
			r.id1 = RunwayIdent (Bearing (r.e1, r.e2)), r.id2 = RunwayIdent (Bearing (r.e2, r.e1));
			b.rwys.push_back (r);
			added.push_back ((int)b.rwys.size () - 1);
			n++;
		} else if (!strcasecmp (key.c_str (), "END1")) has1 = !!(ss >> x1 >> y >> z1);
		else if (!strcasecmp (key.c_str (), "END2")) has2 = !!(ss >> x2 >> y >> z2);
		else if (!strcasecmp (key.c_str (), "WIDTH")) ss >> r.width;
		else if (!strcasecmp (key.c_str (), "ILS1")) ss >> r.ils1;
		else if (!strcasecmp (key.c_str (), "ILS2")) ss >> r.ils2;
		else if (!strcasecmp (key.c_str (), "RWTEX")) ss >> r.surface;
	}
	return n;
}

int ReadVec (const std::string &path, Body &b)
{
	std::ifstream in (path);
	if (!in) return -1;
	std::string line;
	int nvtx = 0, npoly = 0;
	if (!std::getline (in, line) || sscanf (line.c_str (), "%d", &nvtx) != 1) return 0;
	if (!std::getline (in, line) || sscanf (line.c_str (), "%d", &npoly) != 1) return 0;
	int n = 0;
	for (int i = 0; i < npoly && std::getline (in, line); i++) {
		int npt = 0;
		if (sscanf (line.c_str (), "%d", &npt) != 1 || npt < 0) break;
		std::vector<LatLon> pl;
		for (int j = 0; j < npt && std::getline (in, line); j++) {
			double lng, lat;
			if (sscanf (line.c_str (), "%lf%lf", &lng, &lat) == 2) pl.push_back ({lat * RAD, lng * RAD});
		}
		if (pl.size () > 1) b.lines.push_back (std::move (pl)), n++;
	}
	return n;
}

int ReadMarkers (const std::string &path, bool city, Body &b)
{
	std::ifstream in (path);
	if (!in) return -1;
	std::string line;
	bool on = false;
	int n = 0;
	while (std::getline (in, line)) {
		line = Trim (line);
		if (Is (line, "BEGIN_DATA")) { on = true; continue; }
		if (Is (line, "END_DATA")) { on = false; continue; }
		size_t c = line.find (':');
		if (!on || c == std::string::npos) continue;
		double lng, lat;
		std::string name = Trim (line.substr (c + 1));
		size_t c2 = name.find (':');
		if (c2 != std::string::npos) name = Trim (name.substr (0, c2));
		if (name.empty () || sscanf (line.c_str (), "%lf%lf", &lng, &lat) != 2) continue;
		Wpt w;
		w.ident = w.name = name, w.kind = Kind::Landmark, w.size = city ? 1 : 0, w.source = "Orbiter";
		w.pos = {lat * RAD, WrapPi (lng * RAD)};
		b.wpts.push_back (w);
		n++;
	}
	return n;
}

} // namespace gps
