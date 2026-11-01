// GPSNav: each body's database
#include "Sources.h"
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <strings.h>

using namespace gps;
namespace fs = std::filesystem;

std::string ConfigFile (const char *fileName)
{
	std::string p = fileName ? fileName : "";
	for (char &c : p) if (c == '\\') c = '/';
	if (p.size () < 4 || strcasecmp (p.c_str () + p.size () - 4, ".cfg")) p = "Config/" + p + ".cfg"; // a name, not the path Orbiter read
	return oapiResolvePath (p.c_str ());
}

Library::~Library () { Clear (); }

void Library::Clear ()
{
	for (auto &e : m) if (e.second->worker.joinable ()) e.second->worker.join ();
	m.clear ();
}

const Body *Library::Get (OBJHANDLE hBody)
{
	if (!hBody) return nullptr;
	auto it = m.find (hBody);
	if (it == m.end ()) {
		auto e = std::make_unique<Entry> ();
		Start (hBody, *e);
		it = m.emplace (hBody, std::move (e)).first;
	}
	Entry &e = *it->second;
	if (!e.merged) {
		if (!e.done) return nullptr;
		e.worker.join ();
		Merge (e);
		oapiWriteLogV ("GPSNav: %s", Summary (hBody).c_str ());
	}
	return &e.body;
}

bool Library::Loading (OBJHANDLE hBody) const
{
	auto it = m.find (hBody);
	return it != m.end () && !it->second->merged;
}

std::string Library::Summary (OBJHANDLE hBody) const
{
	auto it = m.find (hBody);
	if (it == m.end ()) return "";
	const Entry &e = *it->second;
	char b[256];
	snprintf (b, sizeof b, "%s: %d waypoints from Orbiter, %d from files, %zu runways, %zu map lines",
		e.body.name.c_str (), e.nOrbiter, e.nFiles, e.body.rwys.size (), e.body.lines.size ());
	return b;
}

void Library::Start (OBJHANDLE hBody, Entry &e)
{
	char name[256];
	oapiGetObjectName (hBody, name, sizeof name);
	Body &b = e.body;
	b.name = name;
	b.R = oapiGetSize (hBody);

	// Orbiter's bases (every one loaded, so the installed ones too), their pads and files
	for (DWORD i = 0, n = oapiGetBaseCount (hBody); i < n; i++) {
		OBJHANDLE hb = oapiGetBaseByIndex (hBody, (int)i);
		if (!hb) continue;
		char bn[256];
		oapiGetObjectName (hb, bn, sizeof bn);
		double lng, lat, rad;
		oapiGetBaseEquPos (hb, &lng, &lat, &rad);
		Wpt w;
		w.ident = w.name = bn;
		w.kind = Kind::Base, w.source = "Orbiter";
		w.pos = {lat, lng};
		w.elev = rad - b.R;
		std::string file = ConfigFile (oapiGetObjectFileName (hb));
		std::vector<int> rw;
		ReadBaseRunways (file, w.pos, b, rw);
		for (int r : rw) b.rwys[r].elev1 = b.rwys[r].elev2 = w.elev;
		w.runways = rw;
		b.wpts.push_back (w);
		ReadNavbeacons (file, b);
		for (DWORD p = 0, np = oapiGetBasePadCount (hb); p < np; p++) {
			double plng, plat;
			if (!oapiGetBasePadEquPos (hb, p, &plng, &plat)) continue;
			Wpt pw;
			pw.ident = pw.name = std::string (bn) + " Pad " + std::to_string (p + 1);
			pw.kind = Kind::Pad, pw.source = "Orbiter";
			pw.pos = {plat, plng};
			pw.elev = w.elev;
			if (NAVHANDLE nav = oapiGetBasePadNav (hb, p)) pw.freq = oapiGetNavFreq (nav), pw.tunable = true;
			b.wpts.push_back (pw);
		}
	}
	ReadNavbeacons (ConfigFile (oapiGetObjectFileName (hBody)), b); // the body's own (Earth's VORs)
	e.nOrbiter = (int)b.wpts.size ();

	// the files, on the worker: the paths now (oapiResolvePath is Orbiter's), the reading there
	std::string data = oapiResolvePath ((std::string ("GPSNav/Data/") + name).c_str ());
	std::string user = oapiResolvePath ((std::string ("GPSNav/User/") + name).c_str ());
	std::string cfg = oapiResolvePath ((std::string ("Config/") + name + "/Data").c_str ());
	std::string marker = oapiResolvePath ((std::string ("Config/") + name + "/Marker").c_str ());
	e.files.name = b.name, e.files.R = b.R;
	e.worker = std::thread ([&e, data, user, cfg, marker] () {
		Body &f = e.files;
		auto kind = [] (const fs::path &p, const char *k) { return !strncasecmp (p.filename ().c_str (), k, strlen (k)) && !strcasecmp (p.extension ().c_str (), ".csv"); };
		std::vector<fs::path> users;
		std::error_code ec;
		for (auto &d : fs::directory_iterator (user, ec)) users.push_back (d.path ());
		std::sort (users.begin (), users.end ());
		ReadAirports (data + "/airports.csv", "OurAirports", f);
		ReadRunways (data + "/runways.csv", f);
		for (auto &p : users) if (kind (p, "airports")) ReadAirports (p.string (), "User", f);
		for (auto &p : users) if (kind (p, "runways")) ReadRunways (p.string (), f); // to the user's airport of an ident first
		ReadNavaids (data + "/navaids.csv", "OurAirports", f);
		for (auto &p : users) if (kind (p, "navaids")) ReadNavaids (p.string (), "User", f);
		ReadWaypoints (data + "/waypoints.csv", "User", f);
		for (auto &p : users) if (kind (p, "waypoints")) ReadWaypoints (p.string (), "User", f);
		if (ReadVec (cfg + "/coast.vec", f) >= 0) f.coast = true;
		else ReadVec (cfg + "/contour.vec", f);
		std::vector<fs::path> mk;
		for (auto &d : fs::directory_iterator (marker, ec)) if (!strcasecmp (d.path ().extension ().c_str (), ".mkr")) mk.push_back (d.path ());
		std::sort (mk.begin (), mk.end ());
		for (auto &p : mk) ReadMarkers (p.string (), strcasestr (p.filename ().c_str (), "city") != nullptr, f);
		e.done = true;
	});
}

// the files' waypoints after Orbiter's; a navaid that is an Orbiter beacon (the same ident within 1 km) is one entry, tunable
void Library::Merge (Entry &e)
{
	Body &b = e.body, &f = e.files;
	int off = (int)b.rwys.size ();
	b.rwys.insert (b.rwys.end (), f.rwys.begin (), f.rwys.end ());
	b.Index ();
	for (Wpt &w : f.wpts) {
		for (int &r : w.runways) r += off;
		if (IsNavaid (w.kind)) {
			bool same = false;
			b.Near (w.pos, 1000.0 / b.R, [&] (int i) {
				Wpt &o = b.wpts[i];
				if (!same && o.tunable && o.ident == w.ident && Angle (o.pos, w.pos) * b.R < 1000.0) {
					same = true;
					if (o.name.empty ()) o.name = w.name;
					if (w.kind != Kind::Vor) o.kind = w.kind;
				}
			});
			if (same) continue;
		}
		b.wpts.push_back (std::move (w));
		e.nFiles++;
	}
	b.lines = std::move (f.lines);
	b.coast = f.coast;
	b.Index ();
	f = Body ();
	e.merged = true;
}
