// not upstream: a base file's clear zones (BEGIN_CLEARZONES): no streamed building or tree within a radius of a point (design base-clear)

#ifndef __BASECLEAR_H
#define __BASECLEAR_H

#include <istream>
#include <string>
#include <vector>

struct BaseClearZone { double lng, lat, rad; }; // centre [rad], radius [m]

const double BaseClearMax = 5000.0; // a larger radius is clamped to this [m], as FixedStructureClear

// the lines after BEGIN_CLEARZONES up to END_CLEARZONES or the end: "<lng> <lat> <radius>" [deg, deg, m], text after them ignored; a bad line is skipped with a warning
void ReadClearZones (std::istream &is, std::vector<BaseClearZone> &out, std::vector<std::string> &warn);

#endif // !__BASECLEAR_H
