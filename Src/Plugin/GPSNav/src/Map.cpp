// GPSNav: the OpenStreetMap map's water (Map.h)
#include "Map.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gps::map {

namespace {

// a protobuf message's fields, every read bounded by its end
struct Pb {
	const uint8_t *p = nullptr, *e = nullptr;
	bool More () const { return p < e; }
	bool Var (uint64_t &v)
	{
		v = 0;
		for (int s = 0; s < 64; s += 7) {
			if (p >= e) return false;
			uint8_t b = *p++;
			v |= (uint64_t)(b & 0x7F) << s;
			if (!(b & 0x80)) return true;
		}
		return false;
	}
	bool Key (uint32_t &f, uint32_t &wt)
	{
		uint64_t k;
		if (!Var (k)) return false;
		f = (uint32_t)(k >> 3), wt = (uint32_t)(k & 7);
		return f != 0;
	}
	bool Bytes (Pb &sub)
	{
		uint64_t n;
		if (!Var (n) || n > (uint64_t)(e - p)) return false;
		sub.p = p, sub.e = p + n, p += n;
		return true;
	}
	bool Skip (uint32_t wt)
	{
		uint64_t v;
		Pb s;
		switch (wt) {
		case 0: return Var (v);
		case 1: if (e - p < 8) return false; p += 8; return true;
		case 2: return Bytes (s);
		case 5: if (e - p < 4) return false; p += 4; return true;
		default: return false;
		}
	}
};

const size_t MaxPoints = 200000; // a tile's water, past any real one: a hostile tile stops here
const size_t MaxWork = (size_t)1 << 24;  // a build's row and texel steps, past any real tile
const size_t MaxScan = (size_t)1 << 26;  // its shore tests' edge visits; past them the shores so far are kept

// a polygon's geometry (MoveTo, LineTo, ClosePath with zig-zag deltas) as its rings
bool Polygon (Pb g, Feature &f, size_t &points)
{
	int64_t x = 0, y = 0;
	std::vector<int32_t> *ring = nullptr;
	while (g.More ()) {
		uint64_t c;
		if (!g.Var (c)) return false;
		uint32_t id = (uint32_t)(c & 7), count = (uint32_t)(c >> 3);
		if (id == 7) { // ClosePath; a ring of under 3 points encloses nothing
			if (!ring) return false;
			if (ring->size () < 6) f.rings.pop_back ();
			ring = nullptr;
			continue;
		}
		if ((id != 1 && id != 2) || (id == 1 && count != 1) || count > (uint64_t)(g.e - g.p) / 2) return false;
		if (id == 1) ring = &f.rings.emplace_back ();
		else if (!ring) return false;
		for (uint32_t i = 0; i < count; i++) {
			uint64_t dx, dy;
			if (!g.Var (dx) || !g.Var (dy)) return false;
			x += (int64_t)(dx >> 1) ^ -(int64_t)(dx & 1), y += (int64_t)(dy >> 1) ^ -(int64_t)(dy & 1);
			if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX || ++points > MaxPoints) return false;
			ring->push_back ((int32_t)x), ring->push_back ((int32_t)y);
		}
	}
	return !ring; // every ring closed
}

} // namespace

bool TileXY (LatLon p, int z, double &tx, double &ty)
{
	if (!(std::fabs (p.lat) <= MaxLat) || !std::isfinite (p.lng) || z < 0 || z > MaxZoom) return false;
	double n = (double)(1 << z);
	tx = std::fmod ((WrapPi (p.lng) + PI) / (2.0 * PI), 1.0) * n;
	ty = (1.0 - std::asinh (std::tan (p.lat)) / PI) * 0.5 * n;
	ty = std::clamp (ty, 0.0, std::nextafter (n, 0.0));
	return true;
}

LatLon TileLatLon (int z, double tx, double ty)
{
	double n = (double)(1 << z);
	return {std::atan (std::sinh (PI * (1.0 - 2.0 * ty / n))), WrapPi (tx / n * 2.0 * PI - PI)};
}

bool WaterLayer (const uint8_t *tile, size_t n, std::string &layer)
{
	layer.clear ();
	Pb t {tile, tile + n};
	uint32_t f, wt;
	while (t.More ()) {
		if (!t.Key (f, wt)) return false;
		if (f != 3 || wt != 2) { if (!t.Skip (wt)) return false; continue; }
		Pb l;
		if (!t.Bytes (l)) return false;
		for (Pb q = l; q.More (); ) {
			if (!q.Key (f, wt)) return false;
			if (f == 1 && wt == 2) {
				Pb s;
				if (!q.Bytes (s)) return false;
				if (s.e - s.p == 5 && !memcmp (s.p, "water", 5)) layer.assign ((const char *)l.p, (size_t)(l.e - l.p));
				break;
			}
			if (!q.Skip (wt)) return false;
		}
	}
	return true;
}

bool DecodeLayer (const uint8_t *d, size_t n, std::vector<Feature> &water, uint32_t &extent)
{
	water.clear ();
	extent = 4096;
	Pb l {d, d + n};
	uint32_t f, wt;
	size_t points = 0;
	while (l.More ()) {
		if (!l.Key (f, wt)) return false;
		if (f == 5 && wt == 0) {
			uint64_t e;
			if (!l.Var (e) || e < 256 || e > (1u << 20)) return false; // not upstream: 256, a texel at most 2 units
			extent = (uint32_t)e;
		} else if (f == 2 && wt == 2) {
			Pb feat, geom {};
			if (!l.Bytes (feat)) return false;
			uint64_t type = 0;
			bool hasGeom = false;
			while (feat.More ()) {
				if (!feat.Key (f, wt)) return false;
				if (f == 3 && wt == 0) { if (!feat.Var (type)) return false; }
				else if (f == 4 && wt == 2) { if (!feat.Bytes (geom)) return false; hasGeom = true; }
				else if (!feat.Skip (wt)) return false;
			}
			if (type != 3 || !hasGeom) continue; // polygons only
			Feature p;
			if (!Polygon (geom, p, points)) return false;
			if (!p.rings.empty ()) water.push_back (std::move (p));
		} else if (!l.Skip (wt)) return false;
	}
	const int32_t lo = -(int32_t)extent, hi = 2 * (int32_t)extent; // not upstream: the tile and a tile each way, past any real buffer
	for (Feature &p : water)
		for (auto &r : p.rings)
			for (int32_t &v : r) v = std::clamp (v, lo, hi);
	return true;
}

bool WaterTile::Build (int tz, int tx, int ty, const std::vector<Feature> &water, uint32_t extent)
{
	z = tz, x = tx, y = ty;
	bits.assign ((size_t)Raster * Raster / 64, 0);
	shore.clear (), mid.clear ();
	if (extent < 256) return false; // not upstream: as DecodeLayer
	size_t work = 0, scan = 0;
	auto Over = [&] (size_t n) { work += n; if (work <= MaxWork) return false; bits.clear (), shore.clear (), mid.clear (); return true; };
	const double s = (double)Raster / extent;
	// the mask: each feature's rings even-odd, row by row at the texel centres
	std::vector<std::vector<double>> rowx (Raster);
	std::vector<int> touched;
	for (const Feature &f : water) {
		for (const auto &r : f.rings) {
			size_t np = r.size () / 2;
			for (size_t i = 0; i < np; i++) {
				size_t j = (i + 1) % np;
				double x0 = r[2 * i] * s, y0 = r[2 * i + 1] * s, x1 = r[2 * j] * s, y1 = r[2 * j + 1] * s;
				if (y0 == y1) continue;
				double lo = std::min (y0, y1), hi = std::max (y0, y1);
				int r0 = std::max (0, (int)std::ceil (lo - 0.5)), r1 = std::min (Raster - 1, (int)std::ceil (hi - 0.5) - 1);
				if (r1 >= r0 && Over ((size_t)(r1 - r0 + 1))) return false;
				for (int row = r0; row <= r1; row++) {
					double yc = row + 0.5;
					if (rowx[row].empty ()) touched.push_back (row);
					rowx[row].push_back (x0 + (yc - y0) * (x1 - x0) / (y1 - y0));
				}
			}
		}
		for (int row : touched) {
			std::vector<double> &xs = rowx[row];
			std::sort (xs.begin (), xs.end ());
			for (size_t k = 0; k + 1 < xs.size (); k += 2) {
				int c0 = std::max (0, (int)std::ceil (xs[k] - 0.5)), c1 = std::min (Raster - 1, (int)std::ceil (xs[k + 1] - 0.5) - 1);
				if (c1 >= c0 && Over ((size_t)(c1 - c0 + 1))) return false;
				for (int c = c0; c <= c1; c++) {
					size_t at = (size_t)row * Raster + c;
					bits[at >> 6] |= (uint64_t)1 << (at & 63);
				}
			}
			xs.clear ();
		}
		touched.clear ();
	}
	// the shores: ring edges in the tile, not its cut edges, with water on one side only (tested on the polygons, half a texel off)
	const int B = 2, NB = Raster + 2 * B; // rows of edges, a little past the tile for the tests along its border
	struct Edge { double x0, y0, x1, y1; uint32_t f; };
	std::vector<Edge> edges;
	std::vector<std::vector<uint32_t>> bin (NB);
	for (size_t fi = 0; fi < water.size (); fi++)
		for (const auto &r : water[fi].rings) {
			size_t np = r.size () / 2;
			for (size_t i = 0; i < np; i++) {
				size_t j = (i + 1) % np;
				Edge e {r[2 * i] * s, r[2 * i + 1] * s, r[2 * j] * s, r[2 * j + 1] * s, (uint32_t)fi};
				if (e.y0 == e.y1) continue; // never crosses a test's row (half open)
				int b0 = std::max (0, (int)std::floor (std::min (e.y0, e.y1)) + B), b1 = std::min (NB - 1, (int)std::floor (std::max (e.y0, e.y1)) + B);
				if (b0 > b1) continue;
				if (Over ((size_t)(b1 - b0 + 1))) return false;
				for (int b = b0; b <= b1; b++) bin[b].push_back ((uint32_t)edges.size ());
				edges.push_back (e);
			}
		}
	std::vector<unsigned char> par (water.size (), 0);
	std::vector<uint32_t> hit;
	auto Wet = [&] (double px, double py) { // in raster units: 1 water, 0 land, -1 past the rows kept
		int b = (int)std::floor (py) + B;
		if (b < 0 || b >= NB) return -1;
		scan += bin[b].size ();
		for (uint32_t i : bin[b]) {
			const Edge &e = edges[i];
			if (py < std::min (e.y0, e.y1) || py >= std::max (e.y0, e.y1)) continue;
			if (e.x0 + (py - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0) < px) par[e.f] ^= 1, hit.push_back (e.f);
		}
		int w = 0;
		for (uint32_t f : hit) w |= par[f], par[f] = 0;
		hit.clear ();
		return w;
	};
	const double eps = 0.5 / extent, off = 0.5; // a cut edge's tolerance (tile units), the test's distance (texels)
	for (const Feature &f : water)
		for (const auto &r : f.rings) {
			size_t np = r.size () / 2;
			double area = 0.0, per = 0.0;
			for (size_t i = 0; i < np; i++) {
				size_t j = (i + 1) % np;
				double x0 = r[2 * i] * s, y0 = r[2 * i + 1] * s, x1 = r[2 * j] * s, y1 = r[2 * j + 1] * s;
				area += x0 * y1 - x1 * y0, per += std::hypot (x1 - x0, y1 - y0);
			}
			double qp = 0.25 * per;
			if (qp - std::sqrt (std::max (0.0, qp * qp - 0.5 * std::fabs (area))) < 1.0) continue; // narrower than a texel (a rectangle of its area and perimeter): no shore
			for (size_t i = 0; i < np; i++) {
				size_t j = (i + 1) % np;
				double ax = (double)r[2 * i] / extent, ay = (double)r[2 * i + 1] / extent, bx = (double)r[2 * j] / extent, by = (double)r[2 * j + 1] / extent;
				double dx = bx - ax, dy = by - ay, t0 = 0.0, t1 = 1.0;
				auto clip = [&] (double p, double q) { // Liang-Barsky: keep p t <= q
					if (p == 0.0) return q >= 0.0;
					double t = q / p;
					if (p < 0.0) { if (t > t1) return false; t0 = std::max (t0, t); }
					else { if (t < t0) return false; t1 = std::min (t1, t); }
					return true;
				};
				if (!(clip (-dx, ax) && clip (dx, 1.0 - ax) && clip (-dy, ay) && clip (dy, 1.0 - ay)) || t1 <= t0) continue;
				double px0 = ax + t0 * dx, py0 = ay + t0 * dy, px1 = ax + t1 * dx, py1 = ay + t1 * dy;
				auto along = [eps] (double a, double b, double v) { return std::fabs (a - v) < eps && std::fabs (b - v) < eps; };
				if (along (px0, px1, 0.0) || along (px0, px1, 1.0) || along (py0, py1, 0.0) || along (py0, py1, 1.0)) continue;
				double nx = -(py1 - py0), ny = px1 - px0, len = std::hypot (nx, ny);
				if (len <= 0.0) continue;
				double mx = 0.5 * (px0 + px1) * Raster, my = 0.5 * (py0 + py1) * Raster, ox = off * nx / len, oy = off * ny / len;
				int w1 = Wet (mx + ox, my + oy), w2 = Wet (mx - ox, my - oy);
				if (scan > MaxScan) return true; // not upstream: the shores so far
				if (w1 >= 0 && w2 >= 0 && w1 == w2) continue; // water or land on both sides: not a shore
				double u[3];
				Local::Unit (TileLatLon (z, x + px0, y + py0), u);
				shore.insert (shore.end (), {(float)u[0], (float)u[1], (float)u[2]});
				Local::Unit (TileLatLon (z, x + px1, y + py1), u);
				shore.insert (shore.end (), {(float)u[0], (float)u[1], (float)u[2]});
				mid.insert (mid.end (), {(float)(0.5 * (px0 + px1)), (float)(0.5 * (py0 + py1))});
			}
		}
	return true;
}

bool WaterTile::Water (double fx, double fy) const
{
	int px = std::clamp ((int)std::floor (fx * Raster), 0, Raster - 1), py = std::clamp ((int)std::floor (fy * Raster), 0, Raster - 1);
	return !bits.empty () && Bit (px, py);
}

} // namespace gps::map
