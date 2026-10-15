// not upstream: a base file's clear zones (design base-clear)

#include "BaseClear.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <strings.h>

void ReadClearZones (std::istream &is, std::vector<BaseClearZone> &out, std::vector<std::string> &warn)
{
	const double deg = std::acos (-1.0) / 180.0;
	std::string line;
	while (std::getline (is, line)) {
		while (!line.empty () && std::isspace ((unsigned char)line.back ())) line.pop_back (); // CRLF files too
		size_t b = 0;
		while (b < line.size () && std::isspace ((unsigned char)line[b])) b++;
		const char *p = line.c_str () + b;
		if (!strncasecmp (p, "END_CLEARZONES", 14)) return;
		if (!*p || *p == ';') continue;
		double lng, lat, rad;
		if (sscanf (p, "%lf%lf%lf", &lng, &lat, &rad) != 3) { warn.push_back ("not <lng> <lat> <radius>, skipped: " + std::string (p)); continue; }
		if (!std::isfinite (lng) || !std::isfinite (lat) || !std::isfinite (rad)) { warn.push_back ("not a finite number, skipped: " + std::string (p)); continue; }
		if (std::fabs (lat) > 90.0) { warn.push_back ("latitude outside -90..90, skipped: " + std::string (p)); continue; }
		if (rad <= 0.0) { warn.push_back ("radius not positive, skipped: " + std::string (p)); continue; }
		if (rad > BaseClearMax) { warn.push_back ("radius above 5000 m, 5000 m used: " + std::string (p)); rad = BaseClearMax; }
		out.push_back ({lng * deg, lat * deg, rad});
	}
}
