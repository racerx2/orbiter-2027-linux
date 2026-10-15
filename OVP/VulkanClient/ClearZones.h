// not upstream: the clear zones a planet's base files declare (BEGIN_CLEARZONES): no streamed building or tree in them (design base-clear)

#ifndef __CLEARZONES_H
#define __CLEARZONES_H

#include "OrbiterAPI.h"
#include <array>
#include <vector>

namespace ClearZones {

struct Zone {
	VECTOR3 u, e, n;           // centre as a planet-local unit vector, its east and north
	double r = 0.0;            // radius [m]
};

std::vector<Zone> Read (OBJHANDLE hPlanet, bool log); // every base's zones on the planet (render thread, once a session); log: a line per base with zones
void Near (const std::vector<Zone> &all, const VECTOR3 &up, double R, double range, std::vector<Zone> &out); // the zones whose centre is within range + radius of the unit up vector
bool Covers (const Zone &z, const VECTOR3 &up, double R); // a ground point's unit up vector within the radius, along the ground
bool Reaches (const Zone &z, const VECTOR3 &centre, const std::vector<VECTOR3> &ring, std::vector<std::array<double, 2>> &foot); // a footprint (planet-local points) reaching into the zone; centre: z.u * R; foot: scratch

} // namespace ClearZones

#endif // !__CLEARZONES_H
