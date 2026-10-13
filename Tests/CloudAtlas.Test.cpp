// not upstream: atlas uv ranges of cloud and surface tiles (OVP/VulkanClient/CloudAtlas.cpp)

#include <catch2/catch_test_macros.hpp>
#include "CloudAtlas.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace CloudAtlas;

namespace {
const double PI = 3.14159265358979323846;

struct Bnd { double minlat, maxlat, minlng, maxlng; };

Bnd Extents (int lvl, int ilat, int ilng) // as Tile::Extents, internal level >= 1
{
	int nlat = 1 << lvl, nlng = 2 << lvl;
	Bnd b;
	b.minlat = PI * (nlat / 2 - ilat - 1) / double(nlat);
	b.maxlat = PI * (nlat / 2 - ilat) / double(nlat);
	b.minlng = -PI + 2.0 * PI * ilng / double(nlng);
	b.maxlng = b.minlng + 2.0 * PI / double(nlng);
	return b;
}

double Wrap1 (double u) { u = std::fmod(u, 1.0); return u < 0.0 ? u + 1.0 : u; }
double DU (double a, double b) { double d = std::fabs(Wrap1(a) - Wrap1(b)); return std::min(d, 1.0 - d); } // u distance on the repeat

void Uv (const Range &r, double tu, double tv, double &u, double &v) { u = tu * r.z + r.x; v = tv * r.w + r.y; }

const int W = Cols(MAXLVL) * TILE, H = Rows(MAXLVL) * TILE; // the finest atlas, for tolerances in texels
} // namespace

TEST_CASE("CloudAtlas: a source tile texel lands on its atlas texel", "[cloudatlas]")
{
	for (int fl = MINLVL; fl <= MAXLVL; fl++) {
	const int W = Cols(fl) * TILE, H = Rows(fl) * TILE;
	CHECK(H * 2 == W);
	for (int r = 0; r < Rows(fl); r++)
		for (int c = 0; c < Cols(fl); c++) {
			Bnd b = Extents(fl - 4, r, c);
			Range a = Quad(b.minlat, b.maxlat, b.minlng, b.maxlng);
			int x0, y0;
			Place(r, c, x0, y0);
			for (int i : { 0, 17, 255, 511 })
				for (int j : { 0, 100, 511 }) {
					double u, v;
					Uv(a, (i + 0.5) / TILE, (j + 0.5) / TILE, u, v);
					CHECK(DU(u, (x0 + i + 0.5) / W) * W < 1e-3);
					CHECK(std::fabs(v * H - (y0 + j + 0.5)) < 1e-3);
				}
		}
	}
	CHECK(Cols(7) * TILE == 8192);
}

TEST_CASE("CloudAtlas: a tile texel lands on its geographic point", "[cloudatlas]")
{
	for (int lvl : { 1, 3, 5, 9 }) // internal levels (file 5, 7, 9, 13)
		for (int ilat : { 0, (1 << lvl) / 2 - 1, (1 << lvl) - 1 })
			for (int ilng : { 0, 1, (2 << lvl) - 1 }) {
				Bnd b = Extents(lvl, ilat, ilng);
				Range a = Quad(b.minlat, b.maxlat, b.minlng, b.maxlng);
				for (double tu : { 0.0, 0.3, 1.0 })
					for (double tv : { 0.0, 0.7, 1.0 }) {
						double u, v;
						Uv(a, tu, tv, u, v);
						double lng = b.minlng + tu * (b.maxlng - b.minlng), lat = b.maxlat - tv * (b.maxlat - b.minlat); // tv 0 at maxlat
						CHECK(DU(u, (lng + PI) / (2.0 * PI)) * W < 0.01);
						CHECK(std::fabs(v - (0.5 * PI - lat) / PI) * H < 0.01);
					}
			}
}

TEST_CASE("CloudAtlas: neighbours meet on their shared edges", "[cloudatlas]")
{
	for (int lvl : { 1, 4, 7 }) {
		int nlat = 1 << lvl, nlng = 2 << lvl;
		for (int ilat : { 0, nlat / 2 - 1, nlat - 2 })
			for (int ilng : { 0, nlng / 2, nlng - 1 }) {
				Bnd b = Extents(lvl, ilat, ilng), e = Extents(lvl, ilat, (ilng + 1) % nlng), s = Extents(lvl, ilat + 1, ilng);
				Range a = Quad(b.minlat, b.maxlat, b.minlng, b.maxlng);
				Range ae = Quad(e.minlat, e.maxlat, e.minlng, e.maxlng);
				Range as = Quad(s.minlat, s.maxlat, s.minlng, s.maxlng);
				double u1, v1, u2, v2;
				Uv(a, 1.0, 0.4, u1, v1);
				Uv(ae, 0.0, 0.4, u2, v2); // east neighbour, across +-180 deg for the last column
				CHECK(DU(u1, u2) * W < 0.01);
				CHECK(std::fabs(v1 - v2) * H < 0.01);
				Uv(a, 0.6, 1.0, u1, v1);
				Uv(as, 0.6, 0.0, u2, v2); // south neighbour
				CHECK(DU(u1, u2) * W < 0.01);
				CHECK(std::fabs(v1 - v2) * H < 0.01);
			}
	}
}

TEST_CASE("CloudAtlas: hemisphere tiles", "[cloudatlas]")
{
	const double du = 0.5 / 512.0;
	for (int ilng : { 0, 1 }) {
		Range a = Hemi(ilng);
		double u, v;
		Uv(a, du, 0.25, u, v); // west edge of the hemisphere
		CHECK(DU(u, 0.5 * ilng) * W < 0.01);
		CHECK(std::fabs(v - 0.25) < 1e-6); // tv is colatitude / pi
		Uv(a, 1.0 - du, 0.75, u, v); // east edge
		CHECK(DU(u, 0.5 * ilng + 0.5) * W < 0.01);
		Uv(a, 0.5, 0.5, u, v); // centre longitude
		CHECK(DU(u, 0.5 * ilng + 0.25) * W < 0.01);
	}
}

TEST_CASE("CloudAtlas: surface tiles in the rotated cloud frame", "[cloudatlas]")
{
	for (double rot : { 0.0, 215.2 * PI / 180.0, 359.9 * PI / 180.0, -0.4 })
		for (int lvl : { 2, 6, 10 })
			for (int ilng : { 0, (2 << lvl) / 3, (2 << lvl) - 1 }) {
				Bnd b = Extents(lvl, (1 << lvl) / 3, ilng);
				Range a = Quad(b.minlat, b.maxlat, b.minlng, b.maxlng, rot);
				for (double tu : { 0.0, 0.5, 1.0 }) {
					double u, v;
					Uv(a, tu, 0.5, u, v);
					double lng = b.minlng + tu * (b.maxlng - b.minlng) + rot; // cloud frame lng = ground lng + rot
					CHECK(DU(u, (lng + PI) / (2.0 * PI)) * W < 0.01);
				}
			}
	for (int ilng : { 0, 1 }) { // a surface hemisphere: its west edge moves by rot
		double rot = 1.0;
		Range a = Hemi(ilng, rot);
		double u, v;
		Uv(a, 0.5 / 512.0, 0.5, u, v);
		CHECK(DU(u, 0.5 * ilng + rot / (2.0 * PI)) * W < 0.01);
		for (double r2 : { rot + 2.0 * PI, rot - 2.0 * PI }) { // rot outside [0, 2pi) wraps
			Range b = Hemi(ilng, r2);
			CHECK(DU(b.x, a.x) * W < 0.01);
		}
	}
}

TEST_CASE("CloudAtlas: Unseam meets at the joint mean and leaves the rest", "[cloudatlas]")
{
	const int w = 64, h = 32, tile = 16, band = 4;
	std::vector<unsigned char> px(size_t(w) * h * 4), ref;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			for (int c = 0; c < 4; c++) px[(size_t(y) * w + x) * 4 + c] = (unsigned char)(40 + 20 * (x / tile) + 30 * (y / tile) + c); // a step at every joint
	ref = px;
	Unseam(px.data(), w, h, 4, tile, band);
	auto at = [&](const std::vector<unsigned char> &p, int x, int y) { return int(p[(size_t(y) * w + x) * 4]); };
	for (int y : { 3, 20 }) {
		for (int b = 0; b < w; b += tile) { // vertical joints, x 0 wraps to x w-1
			int xl = (b - 1 + w) % w, xr = b;
			CHECK(std::abs(at(px, xl, y) - at(px, xr, y)) <= 1);
		}
		CHECK(at(px, 8, y) == at(ref, 8, y)); // tile middles are untouched
	}
	for (int x : { 5, 40 }) CHECK(std::abs(at(px, x, tile - 1) - at(px, x, tile)) <= 1); // horizontal joint
	for (int y : { tile - 1, tile }) // corners: a vertical joint inside the horizontal ramp
		for (int b = 0; b < w; b += tile) CHECK(std::abs(at(px, (b - 1 + w) % w, y) - at(px, b, y)) <= 1);
	CHECK(at(px, 8, 0) == at(ref, 8, 0)); // no wrap at the poles
	CHECK(at(px, 8, h - 1) == at(ref, 8, h - 1));
}

TEST_CASE("CloudAtlas: Unseam does not streak texture detail at a joint", "[cloudatlas]")
{
	const int w = 64, h = 16, tile = 16, band = 4; // one row of tiles, no horizontal joint
	std::vector<unsigned char> px(size_t(w) * h * 4, 120);
	for (int y = 0; y < h; y++) px[(size_t(y) * w + tile - 1) * 4] = (y & 1) ? 140 : 100; // zero-mean detail beside the joint at x = tile
	Unseam(px.data(), w, h, 4, tile, band);
	for (int y = 2; y < h - 2; y++) CHECK(std::abs(int(px[(size_t(y) * w + tile + 1) * 4]) - 120) <= 2);
}

TEST_CASE("CloudAtlas: Unseam on one channel and the 2x2 box", "[cloudatlas]")
{
	const int w = 32, h = 16, tile = 16;
	std::vector<unsigned char> px(size_t(w) * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) px[size_t(y) * w + x] = (unsigned char)(x < tile ? 60 : 100);
	Unseam(px.data(), w, h, 1, tile, 4);
	for (int y : { 0, 7, 15 }) {
		CHECK(std::abs(int(px[size_t(y) * w + tile - 1]) - int(px[size_t(y) * w + tile])) <= 1); // the joint at x = tile
		CHECK(std::abs(int(px[size_t(y) * w + w - 1]) - int(px[size_t(y) * w])) <= 1); // the +-180 deg joint
		CHECK(px[size_t(y) * w + 8] == 60);
	}
	std::vector<unsigned char> q(size_t(w / 2) * (h / 2));
	unsigned char s[4] = { 10, 20, 30, 41 };
	Half(s, 2, 2, q.data());
	CHECK(q[0] == 25);
}
