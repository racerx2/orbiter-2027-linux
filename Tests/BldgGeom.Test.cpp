// not upstream: streamed buildings' walls, roofs and atlas UVs on .bld meshes the core makes, base structures, the generated textures

#include <catch2/catch_test_macros.hpp>
#include "BldgGeom.h"
#include "TileStream.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <algorithm>
#include <string>

using namespace BldgGeom;

namespace {

const int TZ = 14, TX = 8000, TY = 8192; // a tile at the equator: about 1.675 tile units a metre
const double UPM = 4096.0 / (40075016.686 / 16384.0);

// a ring in tile units, y down, the order the core takes for an outer ring (positive area); hole: reversed
std::vector<TsBldg::Pt> Ring (const std::vector<std::array<double, 2>> &m, bool hole = false)
{
	std::vector<TsBldg::Pt> r;
	for (const auto &p : m) r.push_back ({2048 + p[0] * UPM, 2048 - p[1] * UPM});
	double a = 0;
	for (size_t i = 0; i < r.size(); i++) a += r[i].x * r[(i + 1) % r.size()].y - r[(i + 1) % r.size()].x * r[i].y;
	if ((a > 0) == hole) std::reverse (r.begin(), r.end());
	return r;
}

std::vector<std::array<double, 2>> Box (double w, double d) { return {{-w / 2, -d / 2}, {w / 2, -d / 2}, {w / 2, d / 2}, {-w / 2, d / 2}}; }

Out Make (const std::vector<std::vector<TsBldg::Pt>> &rings, double height, uint32_t hash = 12345, double minHeight = 0.0)
{
	TsBldg::Feature f;
	f.rings = rings;
	f.height = height;
	f.minHeight = minHeight;
	TsBldg::Mesh m = TsBldg::Build (TZ, TX, TY, {f}, 4096);
	REQUIRE (m.b.size() == 1);
	const TsBldg::Building &b = m.b[0];
	Out o;
	Build (m.vtx.data() + (size_t)b.v0 * 6, b.nv, m.idx.data() + b.i0, b.ni, b.hmin, b.hmax, hash, o);
	return o;
}

double TriArea (const V &a, const V &b, const V &c)
{
	double e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
	double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
	return 0.5 * std::sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
}

// every triangle faces along its vertices' normal, as the .bld's own walls do
void FrontFacing (const std::vector<V> &vs, const std::vector<uint32_t> &is)
{
	for (size_t k = 0; k + 2 < is.size(); k += 3) {
		const V &a = vs[is[k]], &b = vs[is[k + 1]], &c = vs[is[k + 2]];
		double e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
		double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
		if (TriArea (a, b, c) < 1e-6) continue;
		CHECK (n[0] * a.nx + n[1] * a.ny + n[2] * a.nz > 0);
	}
}

// facade UVs inside one region's upper floors, plain band or ground floor, never in a gutter
void InRegion (const std::vector<V> &vs, int reg)
{
	double rx = (reg % 4) * Region, ry = (reg / 4) * Region, e = 1e-3;
	for (const V &v : vs) {
		double u = v.u * Atlas - rx, w = v.v * Atlas - ry;
		if (v.ny > 0.3f) continue; // pitched roof slopes: checked on their own
		CHECK (u >= Gutter - e);
		CHECK (u <= Region - Gutter + e);
		bool up = w >= UpperY - e && w <= UpperY + UpperFloors * FloorPx + e;
		bool plain = w >= PlainY - e && w <= PlainY + PlainPx + e;
		bool ground = w >= GroundY - e && w <= GroundY + GroundPx + e;
		CHECK ((up || plain || ground));
	}
}

double WallArea (const Out &o)
{
	double a = 0;
	for (size_t k = 0; k + 2 < o.fi.size(); k += 3)
		if (std::fabs (o.fv[o.fi[k]].ny) < 0.1f) a += TriArea (o.fv[o.fi[k]], o.fv[o.fi[k + 1]], o.fv[o.fi[k + 2]]);
	return a;
}

}

TEST_CASE ("The .bld walls and roof the client rebuilds face outwards the way the client draws them", "[BldgGeom]")
{
	TsBldg::Feature f;
	f.rings = {Ring (Box (20, 10))};
	f.height = 15;
	TsBldg::Mesh m = TsBldg::Build (TZ, TX, TY, {f}, 4096);
	REQUIRE (m.b.size() == 1);
	std::vector<V> vs;
	for (size_t i = 0; i < m.vtx.size(); i += 6) vs.push_back ({m.vtx[i], m.vtx[i + 1], m.vtx[i + 2], m.vtx[i + 3], m.vtx[i + 4], m.vtx[i + 5], 0, 0});
	FrontFacing (vs, std::vector<uint32_t> (m.idx.begin(), m.idx.end()));
}

TEST_CASE ("A mid-rise block: whole walls from the sunk base to the top, floors and bays inside its region, a flat textured roof", "[BldgGeom]")
{
	Out o = Make ({Ring (Box (20, 10))}, 15);
	CHECK_FALSE (o.pitched);
	REQUIRE (o.style >= 0);
	REQUIRE (o.style < NFacade);
	InRegion (o.fv, o.style);
	FrontFacing (o.fv, o.fi);
	FrontFacing (o.rv, o.ri);
	double perim = 60.0, height = 15.0 + 4.0; // walls start 4 m under the ground
	CHECK (std::fabs (WallArea (o) - perim * height) < 0.02 * perim * height);
	REQUIRE_FALSE (o.rv.empty());
	for (const V &v : o.rv) {
		CHECK (v.ny > 0.99f);
		CHECK (std::fabs (v.y - 15.0f) < 0.01f);
	}
	CHECK (o.ring.size() == 4);
	bool ground = false, plinth = false;
	double ry = (o.style / 4) * Region;
	for (const V &v : o.fv) {
		double w = v.v * Atlas - ry;
		if (v.y < -0.01f) { plinth = true; CHECK (w >= PlainY - 1e-3); CHECK (w <= PlainY + PlainPx + 1e-3); }
		if (std::fabs (v.y) < 1e-3 && w > GroundY + GroundPx - 1e-3) ground = true; // the ground floor's sill row at y 0
	}
	CHECK (plinth);
	CHECK (ground);
}

TEST_CASE ("Bays are whole on each face of a box, and a run never leaves its 8-bay region", "[BldgGeom]")
{
	for (uint32_t h = 0; h < 64; h++) {
		Out o = Make ({Ring (Box (37.3, 11.7))}, 22, h * 2654435761u);
		InRegion (o.fv, o.style);
		const Style &st = Styles[o.style];
		// per face (by normal) the pixel width summed over its pieces is a whole number of bays
		std::map<std::pair<int, int>, double> px;
		for (size_t k = 0; k + 3 < o.fv.size(); k += 4) {
			const V &a = o.fv[k], &b = o.fv[k + 1];
			if (std::fabs (a.ny) > 0.1f || a.y > -3.9f) continue; // the plinth band: one piece each run
			px[{(int)std::lround (a.nx * 10), (int)std::lround (a.nz * 10)}] += std::fabs (b.u - a.u) * Atlas / BayPx;
		}
		CHECK (px.size() == 4);
		for (auto &p : px) {
			CHECK (std::fabs (p.second - std::round (p.second)) < 1e-3);
			double w = std::fabs (p.first.first) > 5 ? 11.7 : 37.3;
			CHECK (std::fabs (p.second - std::max (1.0, std::round (w / st.bayW))) < 1e-3);
		}
	}
}

TEST_CASE ("A courtyard block gets the courtyard's walls too; a part above the ground has no ground floor", "[BldgGeom]")
{
	Out o = Make ({Ring (Box (40, 40)), Ring (Box (16, 16), true)}, 18);
	CHECK (std::fabs (WallArea (o) - (160.0 + 64.0) * 22.0) < 0.03 * 224.0 * 22.0);
	FrontFacing (o.fv, o.fi);
	InRegion (o.fv, o.style);

	Out p = Make ({Ring (Box (20, 20))}, 30, 7, 12.0);
	double ry = (p.style / 4) * Region;
	for (const V &v : p.fv) {
		double w = v.v * Atlas - ry;
		CHECK_FALSE ((w > GroundY + 1e-3 && w < GroundY + GroundPx - 1e-3));
		CHECK (v.y >= 12.0f - 1e-3f);
	}
}

TEST_CASE ("An untagged small house gets a gable roof above its 5 m eaves; a tagged one keeps its height at the ridge", "[BldgGeom]")
{
	Out o = Make ({Ring (Box (10, 8))}, 0.0); // no height: the core's 5 m
	REQUIRE (o.pitched);
	CHECK (o.rv.empty());
	double rise = std::min (4.0 * std::tan (35.0 * M_PI / 180.0), 4.5), top = 0;
	for (const V &v : o.fv) top = std::max (top, (double)v.y);
	CHECK (std::fabs (top - (5.0 + rise)) < 0.01);
	FrontFacing (o.fv, o.fi);
	int slopes = 0;
	for (const V &v : o.fv)
		if (v.ny > 0.3f) {
			slopes++;
			int reg = (int)(v.u * Atlas) / Region + 4 * ((int)(v.v * Atlas) / Region);
			CHECK ((reg == RoofRegion || reg == RoofRegion + 1));
		}
	CHECK (slopes >= 8);
	CHECK (std::fabs (WallArea (o) - (36.0 * 9.0 + 2 * 0.5 * 8.0 * rise)) < 0.03 * 400.0);

	Out t = Make ({Ring (Box (12, 9))}, 9.0);
	REQUIRE (t.pitched);
	top = 0;
	for (const V &v : t.fv) top = std::max (top, (double)v.y);
	CHECK (std::fabs (top - 9.0) < 0.01);

	Out L = Make ({Ring ({{0, 0}, {12, 0}, {12, 6}, {6, 6}, {6, 12}, {0, 12}})}, 0.0);
	CHECK_FALSE (L.pitched);
	Out big = Make ({Ring (Box (30, 20))}, 0.0);
	CHECK_FALSE (big.pitched);
	Out tall = Make ({Ring (Box (10, 8))}, 14.0);
	CHECK_FALSE (tall.pitched);
}

TEST_CASE ("A round tower carries its bays round the curve and stays in its region", "[BldgGeom]")
{
	std::vector<std::array<double, 2>> c;
	for (int k = 0; k < 40; k++) c.push_back ({15 * std::cos (k * 2 * M_PI / 40), 15 * std::sin (k * 2 * M_PI / 40)});
	Out o = Make ({Ring (c)}, 120.0);
	InRegion (o.fv, o.style);
	FrontFacing (o.fv, o.fi);
	double span = 0;
	for (size_t k = 0; k + 3 < o.fv.size(); k += 4)
		if (o.fv[k].y < -3.9f) span += std::fabs (o.fv[k + 1].u - o.fv[k].u) * Atlas / BayPx;
	double perim = 40 * 2 * 15 * std::sin (M_PI / 40);
	CHECK (std::fabs (span - perim / Styles[o.style].bayW) < 1.0);
	CHECK (o.fv.size() < 20000);
}

TEST_CASE ("Styles by size: houses, industrial halls, towers", "[BldgGeom]")
{
	for (uint32_t h = 0; h < 50; h++) {
		int s = PickStyle (7, 150, h);
		CHECK ((s == 12 || s == 3 || s == 4 || s == 0));
		s = PickStyle (10, 5000, h);
		CHECK ((s == 13 || s == 6));
		s = PickStyle (120, 900, h);
		CHECK ((s == 8 || s == 9 || s == 10 || s == 11 || s == 7));
	}
}

TEST_CASE ("Base structures: walls count, pads do not, near within the margin, a structure inside a footprint", "[BldgGeom]")
{
	BaseIndex b;
	auto wall = [&] (double x0, double z0, double x1, double z1, double y0, double y1) {
		double a[3] = {x0, y0, z0}, c[3] = {x1, y0, z1}, d[3] = {x1, y1, z1}, e[3] = {x0, y1, z0};
		b.Add (a, c, d);
		b.Add (a, d, e);
	};
	double p0[3] = {-50, 0, -50}, p1[3] = {50, 0, -50}, p2[3] = {50, 0, 50};
	b.Add (p0, p1, p2);                               // a flat pad
	double s0[3] = {-50, 0, 60}, s1[3] = {50, 0, 60}, s2[3] = {50, 30, 140};
	b.Add (s0, s1, s2);                               // a pad on a slope
	CHECK (b.Empty());
	wall (0, 0, 20, 0, 0, 10); wall (20, 0, 20, 20, 0, 10); wall (20, 20, 0, 20, 0, 10); wall (0, 20, 0, 0, 0, 10);
	CHECK (b.Count() == 8);
	CHECK (b.Near (10, -5.5, 6.0));
	CHECK_FALSE (b.Near (10, -7.0, 6.0));
	CHECK_FALSE (b.Near (10, 10, 6.0));               // the middle of a 20 m hall, 10 m from every wall
	CHECK (b.Inside ({{-5, -5}, {25, -5}, {25, 25}, {-5, 25}}));
	CHECK_FALSE (b.Inside ({{100, 100}, {120, 100}, {120, 120}, {100, 120}}));
	BaseIndex low;
	double q0[3] = {0, 0, 0}, q1[3] = {10, 0, 0}, q2[3] = {10, 1.2, 0};
	low.Add (q0, q1, q2);                             // a kerb, not a building
	CHECK (low.Empty());
}

TEST_CASE ("A sliver of a building part is plain wall, not a squashed floor", "[BldgGeom]")
{
	Out o = Make ({Ring (Box (20, 20))}, 10.9, 7, 10.0); // the core keeps parts of 0.5 m and more
	double ry = (o.style / 4) * Region;
	REQUIRE_FALSE (o.fv.empty());
	for (const V &v : o.fv) {
		double w = v.v * Atlas - ry;
		CHECK (w >= PlainY - 1e-3);
		CHECK (w <= PlainY + PlainPx + 1e-3);
	}
}

#ifdef BLDGTEX_DIR
TEST_CASE ("The build drew every texture: DXT1 with the full mip chain at its size", "[BldgGeom]")
{
	struct F { const char *name; uint32_t size; } files[] = {{"facade.dds", 4096}, {"facade_emis.dds", 2048}, {"facade_norm.dds", 2048}, {"roof.dds", 1024}, {"roof_norm.dds", 1024}};
	for (const F &f : files) {
		CAPTURE (f.name);
		std::ifstream in (std::string (BLDGTEX_DIR) + "/" + f.name, std::ios::binary);
		REQUIRE (in);
		std::string d ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
		REQUIRE (d.size() > 128);
		uint32_t h, w, mips;
		memcpy (&h, &d[12], 4); memcpy (&w, &d[16], 4); memcpy (&mips, &d[28], 4);
		CHECK (d.compare (0, 4, "DDS ") == 0);
		CHECK (d.compare (84, 4, "DXT1") == 0);
		CHECK (w == f.size);
		CHECK (h == f.size);
		uint32_t levels = 1;
		while (f.size >> levels) levels++;
		CHECK (mips == levels);
		size_t bytes = 128;
		for (uint32_t s = f.size; ; s /= 2) { bytes += (size_t)std::max (1u, (s + 3) / 4) * std::max (1u, (s + 3) / 4) * 8; if (s == 1) break; }
		CHECK (d.size() == bytes);
	}
}
#endif
