// not upstream: Config/TileStream.cfg, the tile stream's sources per body (TileStream.h)

#include "TileStream.h"
#include <fstream>
#include <sstream>

static bool Source (std::istringstream &is, TsSource &s, bool levels)
{
	int a = 0, b = 0;
	if (!(is >> s.kind >> s.url >> a)) return false;
	if (s.kind != "eox" && s.kind != "gibs") return false;
	if (levels && !(is >> b)) return false;
	if (s.kind == "eox") { s.lmin = a; s.lmax = b; }
	else { s.nmax = a; s.lmin = 1; s.lmax = levels ? b : 21; }
	if (s.kind == "eox" && !levels) return false;
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
		else if (w == "LIGHTS_BLACK") ok = (bool)(is >> cur.black[0] >> cur.black[1] >> cur.black[2]);
		else if (w == "WATER_GREY") ok = (bool)(is >> cur.waterGrey);
		else if (w == "EOX_FILL_RGB") ok = (bool)(is >> cur.fill[0] >> cur.fill[1] >> cur.fill[2]);
		else if (w == "CREDIT") {
			std::string t;
			std::getline (is >> std::ws, t);
			while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
			ok = !t.empty();
			if (ok) cur.credit.push_back (t);
		} else if (w == "END") {
			if (cur.surf.kind.empty() || cur.lights.kind.empty() || cur.water.kind.empty()) {
				err = at + cur.name + " needs SURF, LIGHTS and WATER"; return false;
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
