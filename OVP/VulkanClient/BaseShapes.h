// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: what streamed trees and ways keep off: a planet's bases (every structure triangle), fixed structures, clear zones (design-ways, from TreeMgr)

#ifndef __BASESHAPES_H
#define __BASESHAPES_H

#include "OrbiterAPI.h"
#include "BldgGeom.h"
#include "FixedVes.h"
#include "ClearZones.h"
#include <map>
#include <memory>
#include <vector>

class vPlanet;

namespace BaseShapes {

VECTOR3 Up (double lat, double lng); // planet-local unit vector

struct Base {                   // a base's structures, every triangle; planet-local to its frame through rp, then rb
	MATRIX3 rp, rb;
	VECTOR3 p;
	BldgGeom::BaseIndex idx{true};
};

// a job's share, made on the render thread
struct Keep {
	std::vector<std::shared_ptr<const Base>> bases;
	std::vector<std::shared_ptr<const FixedVes::Rec>> fixed; // fixed structures, every triangle (design fixed-structures)
	std::vector<ClearZones::Zone> zones;                     // the base files' clear zones (design base-clear)
	bool Empty () const { return bases.empty() && fixed.empty() && zones.empty(); }
	bool Blocked (const VECTOR3 &up, double R, double margin) const; // a ground point in a clear zone, or within margin of a base's or fixed structure's triangle
};

// a planet's bases, each read once (render thread)
class Set {
public:
	void Near (vPlanet *vp, OBJHANDLE hPlanet, double R, const VECTOR3 &up, double range, std::vector<std::shared_ptr<const Base>> &out);
private:
	const std::shared_ptr<const Base> &Of (vPlanet *vp, OBJHANDLE hPlanet, OBJHANDLE hBase);
	std::map<OBJHANDLE, std::shared_ptr<const Base>> bases;
};

} // namespace BaseShapes

#endif // !__BASESHAPES_H
