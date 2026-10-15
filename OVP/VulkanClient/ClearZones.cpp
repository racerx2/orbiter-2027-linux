// not upstream: the clear zones a planet's base files declare (design base-clear)

#include "ClearZones.h"
#include "BldgGeom.h"
#include <algorithm>
#include <cmath>

namespace ClearZones {

namespace {
double Ground (const VECTOR3 &a, const VECTOR3 &b, double R) { return 2.0 * R * asin (std::min (1.0, 0.5 * length (a - b))); } // along the ground between unit vectors
}

std::vector<Zone> Read (OBJHANDLE hPlanet, bool log)
{
	std::vector<Zone> out;
	for (DWORD i = 0, nb = oapiGetBaseCount (hPlanet); i < nb; i++) {
		OBJHANDLE hb = oapiGetBaseByIndex (hPlanet, i);
		DWORD n = oapiGetBaseClearZones (hb, nullptr, 0);
		if (!n) continue;
		std::vector<BASECLEARZONE> z (n);
		n = std::min (n, oapiGetBaseClearZones (hb, z.data(), n));
		for (DWORD k = 0; k < n; k++) {
			double cl = cos (z[k].lat), sl = sin (z[k].lat), cn = cos (z[k].lng), sn = sin (z[k].lng);
			Zone c;
			c.u = _V(cl * cn, sl, cl * sn);
			c.e = _V(-sn, 0.0, cn);
			c.n = _V(-sl * cn, cl, -sl * sn);
			c.r = z[k].rad;
			out.push_back (c);
		}
		if (log) {
			char name[64] = "";
			oapiGetObjectName (hb, name, 64);
			oapiWriteLogV ("TileStream: base %s: %u clear zones keep streamed buildings and trees out", name, (unsigned)n);
		}
	}
	return out;
}

void Near (const std::vector<Zone> &all, const VECTOR3 &up, double R, double range, std::vector<Zone> &out)
{
	for (const Zone &z : all) if (Ground (up, z.u, R) <= range + z.r) out.push_back (z);
}

bool Covers (const Zone &z, const VECTOR3 &up, double R)
{
	return Ground (up, z.u, R) <= z.r;
}

bool Reaches (const Zone &z, const VECTOR3 &centre, const std::vector<VECTOR3> &ring, std::vector<std::array<double, 2>> &foot)
{
	foot.clear();
	for (const VECTOR3 &p : ring) {
		VECTOR3 l = p - centre;
		foot.push_back ({dotp (l, z.e), dotp (l, z.n)});
	}
	return BldgGeom::Reaches (foot, z.r);
}

} // namespace ClearZones
