// not upstream (terrain-contact): hull contact points from a vessel's meshes, and the pose of a vessel landed on a terrain
#ifndef __HULLPOINTS_H
#define __HULLPOINTS_H

#include <functional>
#include <vector>
#include "Vecmat.h"

namespace HullPoints {

const double GEAR_ZONE  = 0.3;   // points less than this over the primary touchdown plane are the gear's [m]
const size_t MAX_POINTS = 128;   // hull points of a vessel at most
const double MERGE      = 0.01;  // support points this near are one [m]
const double HULL_K     = 196.0; // stiffness per kg: one point holds the weight at 5 cm on Earth [N/m/kg]
const double HULL_ZETA  = 0.3;   // damping ratio of one point
const double HULL_MU    = 0.8;   // friction of the hull on the ground
const double REST_MAX   = 0.5;   // largest static compression the free length takes [m]

const std::vector<Vector> &Directions ();                                // 642 unit vectors: an icosahedron split 3 times
std::vector<Vector> Thin (const std::vector<Vector> &v, double cell);     // one vertex per cubic cell, the first met
std::vector<Vector> Support (const std::vector<Vector> &v);               // the farthest vertex along each direction, MERGE apart
void GearZone (std::vector<Vector> &p, const Vector &p0, const Vector &n, double zone); // drops points less than zone over the plane through p0 with normal n
void Cap (std::vector<Vector> &p, size_t nmax);                           // farthest-point order from the first, nmax kept

// Rh: vessel frame to the horizon frame at the CG (x east, y up, z north), in: level, out: the primary points tp on the ground
// ground (x, z): the terrain's height over the CG's ground point; nt: the touchdown normal (vessel frame, towards the CG)
// ycg: the CG's height over its ground point, raised by the deepest of all (the vessel's own touchdown points)
void Pose (const Vector tp[3], const Vector &nt, const std::vector<Vector> &all, const std::function<double (double, double)> &ground, Matrix &Rh, double &ycg);

Matrix Turn (const Vector &a, const Vector &b);                          // the least rotation taking unit a onto unit b

}

#endif // !__HULLPOINTS_H
