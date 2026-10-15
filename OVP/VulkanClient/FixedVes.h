// not upstream: fixed structures, landed vessels whose class has FixedStructure = TRUE: their meshes keep streamed buildings and trees out as a base's (design fixed-structures)

#ifndef __FIXEDVES_H
#define __FIXEDVES_H

#include "OrbiterAPI.h"
#include "BldgGeom.h"
#include <memory>
#include <string>
#include <vector>

namespace FixedVes {

const double Move = 0.5;      // a structure moved this far makes its record again [m] (CollBldgA COLL_FIXED_MOVE)
const double Turn = 1e-3;     // or turned this far [rad] (CollBldgA COLL_FIXED_TURN)

// a structure's faces in the horizon frame at its origin; planet-local to that frame through rp, then rb, as a base record
struct Rec {
	explicit Rec (bool every) : idx(every) {}
	OBJHANDLE hVessel = nullptr;
	std::string name;
	MATRIX3 rp, rb;
	VECTOR3 p;                 // its origin, planet-local
	double clear = 0.0;        // FixedStructureClear: no streamed building or tree within this distance of the origin [m], 0 off
	BldgGeom::BaseIndex idx;
};

bool FixedClass (OBJHANDLE hVessel); // FixedStructure = TRUE in Config/Vessels/<class>.cfg (else Config/<class>.cfg), BaseClass first; once per class
double ClearOf (OBJHANDLE hVessel);  // its class's FixedStructureClear [m], 0 when unset or not positive, at most MaxClear
const double MaxClear = 5000.0;

// a planet's fixed structures, made again when one is new, gone, moved, turned or its meshes changed (render thread)
class Set {
public:
	explicit Set (bool every) : every(every) {}
	bool Update (OBJHANDLE hPlanet, std::vector<VECTOR3> &moved);  // moved: planet-local places of the records made or dropped; true if any
	void Near (const VECTOR3 &up, double R, double range, std::vector<std::shared_ptr<const Rec>> &out) const; // records within range of the unit up vector on the planet of radius R
	size_t Size () const { return held.size(); }
private:
	struct Held { OBJHANDLE h; VECTOR3 loc; MATRIX3 Rp; unsigned long long sig; std::shared_ptr<const Rec> rec; };
	bool every;                // the trees' index: every triangle
	std::vector<Held> held;
};

} // namespace FixedVes

#endif // !__FIXEDVES_H
