// not upstream: streamed buildings' walls, roofs and texture atlas layout, no client types (BldgMgr, Utils/bldgtex, Tests)

#ifndef __BLDGGEOM_H
#define __BLDGGEOM_H

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace BldgGeom {

// facade.dds: 4x4 regions of 1024 px, 8 px gutters; a facade region has 8 bays, 6 upper floors, a plain band, the ground floor
const int Atlas = 4096, Region = 1024, Gutter = 8, Bays = 8, BayPx = 126;
const int UpperY = 8, UpperFloors = 6, FloorPx = 122, PlainY = 748, PlainPx = 96, GroundY = 852, GroundPx = 164;
const int NFacade = 14, RoofRegion = 14;        // regions 14 and 15: pitched roof tiles, two variants each
const int RoofY[2] = {8, 520}, RoofPx = 496;    // a variant's rows; 1008 px across
const double RoofAlong = 12.0, RoofDown = 6.0;  // metres a roof variant covers along the ridge and down the slope
const double FlatRoofM = 16.0;                  // roof.dds repeats every 16 m

struct Style { float floorH, bayW, groundH; };  // metres
extern const Style Styles[NFacade];

int PickStyle (double h, double area, uint32_t hash);

struct V { float x, y, z, nx, ny, nz, u, v; };
struct Out {
	std::vector<V> fv, rv;          // facades and pitched roofs (facade.dds); flat roofs (roof.dds)
	std::vector<uint32_t> fi, ri;   // triangles, clockwise from the front
	std::vector<std::array<double, 2>> ring; // outer footprint, x east, z north
	int style = 0;
	bool pitched = false;
};

// one .bld building: vtx 6 floats a vertex (x east, y up, z north, normal), idx its triangles; walls are quads before the roof
void Build (const float *vtx, uint32_t nv, const uint32_t *idx, uint32_t ni, float hmin, float hmax, uint32_t hash, Out &out);

// not upstream (trees): the footprint rings Build finds, outer ring first, x east, z north
void Rings (const float *vtx, uint32_t nv, std::vector<std::vector<std::array<double, 2>>> &rings);

// a base's structures in its own frame (y up): steep triangles taller than 1.5 m; flat or sloped pads never count
class BaseIndex {
public:
	BaseIndex () : all(false) {}
	explicit BaseIndex (bool every) : all(every) {}               // not upstream: every triangle (pads, aprons, roads), for the trees
	void Add (const double a[3], const double b[3], const double c[3]);
	bool Near (double x, double z, double margin) const;           // a structure within margin of the point
	bool Covers (double x, double z, double margin) const;         // not upstream: on a triangle, or within margin of one
	bool Inside (const std::vector<std::array<double, 2>> &poly) const; // a structure's centroid inside the polygon
	bool Empty () const { return tris.empty(); }
	size_t Count () const { return tris.size(); }
private:
	struct T { double x[3], z[3], cx, cz; };
	bool all;
	std::vector<T> tris;
	std::unordered_map<int64_t, std::vector<uint32_t>> cells; // 64 m buckets
};

} // namespace BldgGeom

#endif // !__BLDGGEOM_H
