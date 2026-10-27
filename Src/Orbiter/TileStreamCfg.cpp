// not upstream: Config/TileStream.cfg, the tile stream's sources per body (TileStream.h)

#include "TileStream.h"
#include <algorithm>
#include <fstream>
#include <sstream>

static bool Source (std::istringstream &is, TsSource &s, bool levels)
{
	int a = 0, b = 0;
	if (!(is >> s.kind >> s.url >> a)) return false;
	if (s.kind != "eox" && s.kind != "gibs" && s.kind != "terrarium" && s.kind != "cog") return false; // mars-stream: cog
	if (levels && !(is >> b)) return false;
	if (s.kind == "eox" || s.kind == "cog") { s.lmin = a; s.lmax = b; }
	else { s.nmax = a; s.lmin = 1; s.lmax = levels ? b : 21; }
	if ((s.kind == "eox" || s.kind == "cog") && !levels) return false;
	return s.lmin >= 1 && s.lmax >= s.lmin && s.lmax <= 21 && s.nmax >= 0 && s.nmax <= 20;
}

bool TsReadCfg (const std::string &file, std::vector<TsBodyCfg> &bodies, std::string &err)
{
	bodies.clear();
	std::ifstream ifs (file);
	if (!ifs) { err = "cannot read " + file; return false; }
	std::string line;
	TsBodyCfg cur;
	bool in = false;
	for (int ln = 1; std::getline (ifs, line); ln++) {
		size_t c = line.find (';');
		if (c != std::string::npos) line.erase (c);
		std::istringstream is (line);
		std::string w;
		if (!(is >> w)) continue;
		std::string at = file + ":" + std::to_string (ln) + ": ";
		bool ok = true;
		if (w == "BODY") {
			if (in) { err = at + "BODY before END"; return false; }
			cur = TsBodyCfg();
			ok = (bool)(is >> cur.name);
			in = true;
		} else if (!in) {
			err = at + w + " outside BODY"; return false;
		} else if (w == "SURF") ok = Source (is, cur.surf, true);
		else if (w == "SURF_FALLBACK") ok = Source (is, cur.fallback, true);
		else if (w == "LIGHTS") ok = Source (is, cur.lights, false) && cur.lights.kind == "gibs";
		else if (w == "WATER") ok = Source (is, cur.water, false) && cur.water.kind == "gibs";
		else if (w == "ELEV") ok = Source (is, cur.elev, true) && (cur.elev.kind == "terrarium" || (cur.elev.kind == "cog" && cur.elev.lmin >= 4));
		else if (w == "DETAIL") { // not upstream (mars-stream): a cog past the colour's levels, inside its bounds
			TsSource d;
			double *b = d.box;
			ok = Source (is, d, true) && d.kind == "cog" && (is >> b[0] >> b[1] >> b[2] >> b[3])
				&& b[0] >= -180.0 && b[0] < b[2] && b[2] <= 180.0 && b[1] >= -90.0 && b[1] < b[3] && b[3] <= 90.0;
			if (ok) cur.detail.push_back (d);
		} else if (w == "PALETTE") { // not upstream (mars-stream): brightness to colour
			TsBodyCfg::Pal q;
			ok = (is >> q.v >> q.r >> q.g >> q.b) && q.v >= 0.0 && q.v <= 255.0 && (cur.palette.empty() || q.v > cur.palette.back().v);
			for (double c : {q.r, q.g, q.b}) ok = ok && c >= 0.0 && c <= 255.0;
			if (ok) cur.palette.push_back (q);
		} else if (w == "RETIRE") ok = (is >> cur.retire) && cur.retire.find ('/') == std::string::npos && cur.retire.find ("..") == std::string::npos;
		else if (w == "BLDG") ok = (is >> cur.bldgJson >> cur.bldgUrl) && cur.bldgUrl.find ("{x}") != std::string::npos;
		else if (w == "COVER") {
			ok = (is >> cur.cover.kind >> cur.cover.url >> cur.cover.lmin) && cur.cover.kind == "worldcover" && cur.cover.lmin >= 4 && cur.cover.lmin <= 21;
			cur.cover.lmax = 21;
		} else if (w == "ELEV_SCALE") ok = (is >> cur.elevScale) && cur.elevScale > 0.0;
		else if (w == "LIGHTS_BLACK") ok = (bool)(is >> cur.black[0] >> cur.black[1] >> cur.black[2]);
		else if (w == "WATER_GREY") ok = (bool)(is >> cur.waterGrey);
		else if (w == "EOX_FILL_RGB") ok = (bool)(is >> cur.fill[0] >> cur.fill[1] >> cur.fill[2]);
		else if (w == "FLOOR") {
			TsBodyCfg::Floor f;
			ok = (is >> f.lng0 >> f.lat0 >> f.lng1 >> f.lat1 >> f.m) && f.lng0 >= -180.0 && f.lng0 < f.lng1 && f.lng1 <= 180.0
				&& f.lat0 >= -90.0 && f.lat0 < f.lat1 && f.lat1 <= 90.0 && f.m <= 0.0 && f.m >= -11000.0;
			if (ok) cur.floor.push_back (f);
		} else if (w == "CLOUD") ok = (is >> cur.cloudSmall >> cur.cloudBig >> cur.cloudMax) && cur.cloudMax >= 5 && cur.cloudMax <= 9; // earth-sky
		else if (w == "CREDIT") {
			std::string t;
			std::getline (is >> std::ws, t);
			while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
			ok = !t.empty();
			if (ok) cur.credit.push_back (t);
		} else if (w == "END") {
			bool cog = cur.Cog(); // not upstream (mars-stream): a cog body reads its own sources, no Mask
			if (cur.surf.kind.empty() || (!cog && (cur.lights.kind.empty() || cur.water.kind.empty()))) {
				err = at + cur.name + " needs SURF, LIGHTS and WATER"; return false;
			}
			bool mixed = !cog ? cur.elev.kind == "cog" || !cur.detail.empty() || !cur.palette.empty()
				: (!cur.elev.kind.empty() && cur.elev.kind != "cog") || !cur.fallback.kind.empty() || !cur.cover.kind.empty() || !cur.bldgUrl.empty() || cur.Masks();
			if (mixed) {
				err = at + cur.name + ": cog sources go with cog sources only"; return false;
			}
			if (cur.palette.size() == 1) {
				err = at + cur.name + ": PALETTE needs two points or more"; return false;
			}
			bodies.push_back (cur);
			in = false;
		} else {
			err = at + "unknown " + w; return false;
		}
		if (!ok) { err = at + "bad " + w; return false; }
	}
	if (in) { err = file + ": " + cur.name + " has no END"; return false; }
	return true;
}

int TsCfgElev (const std::string &file, const std::string &body)
{
	std::vector<TsBodyCfg> bodies;
	std::string err;
	if (!TsReadCfg (file, bodies, err)) return 0;
	for (const TsBodyCfg &c : bodies)
		if (c.name == body) return c.elev.kind.empty() ? 0 : c.elev.lmax;
	return 0;
}

// not upstream (mars-stream)
int TsBodyCfg::SurfMax () const
{
	int m = surf.lmax;
	for (const TsSource &d : detail) m = std::max (m, d.lmax);
	return m;
}

bool TsSource::Over (int L, int ilat, int ilng) const
{
	if (L < lmin || L > lmax) return false;
	double w, n, dw, dh;
	TsImg::Bounds (L, ilat, ilng, w, n, dw, dh);
	return w < box[2] && w + dw > box[0] && n - dh < box[3] && n > box[1];
}

bool TsBodyCfg::Covers (int L, int ilat, int ilng) const
{
	if (L >= surf.lmin && L <= surf.lmax) return true;
	for (const TsSource &d : detail)
		if (d.Over (L, ilat, ilng)) return true;
	return false;
}
