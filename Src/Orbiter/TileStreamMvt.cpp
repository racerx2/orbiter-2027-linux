
// not upstream: 3D buildings from Mapbox vector tiles: decode, triangulate, extrude, file (TileStream.h)

#include "TileStream.h"
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const double Rn = 6371010.0; // nominal radius for the metres around an anchor
const size_t MaxHoles = 64;      // per polygon; more are dropped, the outer ring kept
const size_t MaxPoly = 2000;     // vertices per polygon, holes and bridges included
const size_t MaxTile = 200000;   // ring vertices per tile
const double MaxHeight = 1000.0; // metres

struct Pb {
	const uint8_t *p, *e;
	Pb (const char *d, size_t n) : p((const uint8_t*)d), e((const uint8_t*)d + n) {}
	bool More () const { return p < e; }
	bool Var (uint64_t &v)
	{
		v = 0;
		for (int s = 0; s < 64 && p < e; s += 7) {
			uint8_t b = *p++;
			v |= (uint64_t)(b & 0x7F) << s;
			if (!(b & 0x80)) return true;
		}
		return false;
	}
	bool Key (int &field, int &wt)
	{
		uint64_t k;
		if (!Var (k)) return false;
		field = (int)(k >> 3); wt = (int)(k & 7);
		return true;
	}
	bool Bytes (Pb &sub)
	{
		uint64_t n;
		if (!Var (n) || n > (uint64_t)(e - p)) return false;
		sub = Pb ((const char*)p, (size_t)n);
		p += n;
		return true;
	}
	bool Skip (int wt)
	{
		uint64_t v;
		Pb s (nullptr, 0);
		switch (wt) {
		case 0: return Var (v);
		case 1: if (e - p < 8) return false; p += 8; return true;
		case 2: return Bytes (s);
		case 5: if (e - p < 4) return false; p += 4; return true;
		}
		return false;
	}
	bool Fixed (void *out, int n) { if (e - p < n) return false; memcpy (out, p, n); p += n; return true; }
};

int64_t Zz (uint64_t v) { return (int64_t)(v >> 1) ^ -(int64_t)(v & 1); }

// packed or not, a repeated uint32 field
bool U32s (Pb &pb, int wt, std::vector<uint32_t> &out)
{
	uint64_t v;
	if (wt == 0) { if (!pb.Var (v)) return false; out.push_back ((uint32_t)v); return true; }
	if (wt != 2) return pb.Skip (wt);
	Pb s (nullptr, 0);
	if (!pb.Bytes (s)) return false;
	while (s.More()) { if (!s.Var (v)) return false; out.push_back ((uint32_t)v); }
	return true;
}

struct Val { int kind = 0; double num = 0.0; std::string str; }; // kind 0 none, 1 number, 2 string, 3 bool

bool Value (Pb &pb, Val &v)
{
	int f, wt;
	while (pb.More()) {
		if (!pb.Key (f, wt)) return false;
		uint64_t u;
		if (f == 1 && wt == 2) { Pb s (nullptr, 0); if (!pb.Bytes (s)) return false; v.kind = 2; v.str.assign ((const char*)s.p, s.e - s.p); }
		else if (f == 2 && wt == 5) { float x; if (!pb.Fixed (&x, 4)) return false; v.kind = 1; v.num = x; }
		else if (f == 3 && wt == 1) { double x; if (!pb.Fixed (&x, 8)) return false; v.kind = 1; v.num = x; }
		else if ((f == 4 || f == 5) && wt == 0) { if (!pb.Var (u)) return false; v.kind = 1; v.num = f == 4 ? (double)(int64_t)u : (double)u; }
		else if (f == 6 && wt == 0) { if (!pb.Var (u)) return false; v.kind = 1; v.num = (double)Zz (u); }
		else if (f == 7 && wt == 0) { if (!pb.Var (u)) return false; v.kind = 3; v.num = u ? 1.0 : 0.0; }
		else if (!pb.Skip (wt)) return false;
	}
	return true;
}

double Area (const std::vector<TsBldg::Pt> &r)
{
	double a = 0.0;
	for (size_t i = 0, n = r.size(); i < n; i++) {
		const TsBldg::Pt &p = r[i], &q = r[(i + 1) % n];
		a += p.x * q.y - q.x * p.y;
	}
	return 0.5 * a;
}

// geometry commands to rings: MoveTo, LineTo, ClosePath with zigzag deltas
bool Rings (const std::vector<uint32_t> &g, std::vector<std::vector<TsBldg::Pt>> &rings)
{
	int64_t x = 0, y = 0;
	std::vector<TsBldg::Pt> cur;
	for (size_t i = 0; i < g.size(); ) {
		uint32_t id = g[i] & 7, n = g[i] >> 3;
		i++;
		if (id == 7) {
			if (cur.size() >= 3) rings.push_back (cur);
			cur.clear();
			continue;
		}
		if (id != 1 && id != 2) return false;
		if (i + 2 * (size_t)n > g.size()) return false;
		for (uint32_t k = 0; k < n; k++) {
			x += Zz (g[i++]);
			y += Zz (g[i++]);
			if (id == 1) cur.clear();
			cur.push_back ({(double)x, (double)y});
		}
	}
	return true;
}

double Cross (const TsBldg::Pt &a, const TsBldg::Pt &b, const TsBldg::Pt &c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool Same (const TsBldg::Pt &a, const TsBldg::Pt &b) { return a.x == b.x && a.y == b.y; }

bool Inside (const TsBldg::Pt &p, const TsBldg::Pt &a, const TsBldg::Pt &b, const TsBldg::Pt &c)
{
	if (Same (p, a) || Same (p, b) || Same (p, c)) return false;
	return Cross (a, b, p) >= 0 && Cross (b, c, p) >= 0 && Cross (c, a, p) >= 0;
}

// proper crossing of segments ab and cd
bool Crosses (const TsBldg::Pt &a, const TsBldg::Pt &b, const TsBldg::Pt &c, const TsBldg::Pt &d)
{
	if (Same (a, c) || Same (a, d) || Same (b, c) || Same (b, d)) return false;
	double d1 = Cross (a, b, c), d2 = Cross (a, b, d), d3 = Cross (c, d, a), d4 = Cross (c, d, b);
	return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 && d2 && d3 && d4;
}

}

bool TsBldg::Inflate (const std::string &in, std::string &out)
{
	z_stream s;
	memset (&s, 0, sizeof s);
	if (inflateInit2 (&s, 15 + 32) != Z_OK) return false;
	s.next_in = (Bytef*)in.data();
	s.avail_in = (uInt)in.size();
	out.clear();
	char buf[65536];
	int r;
	do {
		s.next_out = (Bytef*)buf;
		s.avail_out = sizeof buf;
		r = inflate (&s, Z_NO_FLUSH);
		if (r != Z_OK && r != Z_STREAM_END) { inflateEnd (&s); return false; }
		out.append (buf, sizeof buf - s.avail_out);
		if (out.size() > ((size_t)256 << 20)) { inflateEnd (&s); return false; }
	} while (r != Z_STREAM_END && (s.avail_in || !s.avail_out));
	inflateEnd (&s);
	return r == Z_STREAM_END;
}

bool TsBldg::Decode (const std::string &pbf, std::vector<Feature> &out, uint32_t &extent)
{
	out.clear();
	extent = 4096;
	bool seen = false;
	Pb tile (pbf.data(), pbf.size());
	int f, wt;
	while (tile.More()) {
		if (!tile.Key (f, wt)) return false;
		if (f != 3 || wt != 2) { if (!tile.Skip (wt)) return false; continue; }
		Pb layer (nullptr, 0);
		if (!tile.Bytes (layer)) return false;
		std::string name;
		std::vector<std::string> keys;
		std::vector<Val> vals;
		std::vector<Pb> feats;
		uint64_t ext = 4096;
		while (layer.More()) {
			if (!layer.Key (f, wt)) return false;
			Pb s (nullptr, 0);
			if (wt == 2 && (f == 1 || f == 2 || f == 3 || f == 4)) {
				if (!layer.Bytes (s)) return false;
				if (f == 1) name.assign ((const char*)s.p, s.e - s.p);
				else if (f == 2) feats.push_back (s);
				else if (f == 3) keys.emplace_back ((const char*)s.p, s.e - s.p);
				else { Val v; if (!Value (s, v)) return false; vals.push_back (v); }
			} else if (f == 5 && wt == 0) { if (!layer.Var (ext)) return false; }
			else if (!layer.Skip (wt)) return false;
		}
		if (name != "building") continue;
		if (ext < 1 || ext > (1u << 20)) return false;
		if (!seen) extent = (uint32_t)ext; // the first building layer's units; later layers scale to them
		double to = (double)extent / (double)ext;
		seen = true;
		for (Pb &fp : feats) {
			std::vector<uint32_t> tags, geom;
			uint64_t type = 0;
			while (fp.More()) {
				if (!fp.Key (f, wt)) return false;
				if (f == 2) { if (!U32s (fp, wt, tags)) return false; }
				else if (f == 4) { if (!U32s (fp, wt, geom)) return false; }
				else if (f == 3 && wt == 0) { if (!fp.Var (type)) return false; }
				else if (!fp.Skip (wt)) return false;
			}
			if (type != 3) continue;
			Feature F;
			for (size_t i = 0; i + 1 < tags.size(); i += 2) {
				if (tags[i] >= keys.size() || tags[i + 1] >= vals.size()) continue;
				const std::string &k = keys[tags[i]];
				const Val &v = vals[tags[i + 1]];
				double num = v.kind == 2 ? atof (v.str.c_str()) : v.num;
				if (!std::isfinite (num)) continue;
				if (k == "render_height") F.height = num;
				else if (k == "render_min_height") F.minHeight = num;
				else if (k == "hide_3d") F.hide = v.kind == 3 ? v.num != 0.0 : (v.kind == 2 ? v.str == "true" : v.num != 0.0);
			}
			if (!Rings (geom, F.rings)) return false;
			if (to != 1.0)
				for (auto &r : F.rings)
					for (Pt &p : r) { p.x *= to; p.y *= to; }
			if (!F.rings.empty()) out.push_back (std::move (F));
		}
	}
	return true;
}

bool TsBldg::Triangulate (const std::vector<std::vector<Pt>> &rings, std::vector<uint32_t> &tri)
{
	tri.clear();
	if (rings.empty() || rings[0].size() < 3) return false;
	struct V { Pt p; uint32_t i; };
	uint32_t base = 0;
	auto ring = [&] (size_t r, bool ccw) {
		std::vector<V> v;
		for (size_t k = 0; k < rings[r].size(); k++) v.push_back ({rings[r][k], base + (uint32_t)k});
		base += (uint32_t)rings[r].size();
		if ((Area (rings[r]) > 0) != ccw) std::reverse (v.begin(), v.end());
		return v;
	};
	if (rings[0].size() > MaxPoly) return false;
	std::vector<V> poly = ring (0, true);
	std::vector<std::vector<V>> holes;
	bool ok = true;
	size_t total = poly.size();
	for (size_t r = 1; r < rings.size(); r++)
		if (rings[r].size() >= 3 && holes.size() < MaxHoles && total + rings[r].size() + 2 <= MaxPoly) {
			holes.push_back (ring (r, false));
			total += rings[r].size() + 2; // each bridge repeats two vertices
		} else {
			if (rings[r].size() >= 3) ok = false; // hole dropped before bridging: roof covers it
			base += (uint32_t)rings[r].size();
		}
	auto maxx = [] (const std::vector<V> &h) { size_t m = 0; for (size_t k = 1; k < h.size(); k++) if (h[k].p.x > h[m].p.x) m = k; return m; };
	std::sort (holes.begin(), holes.end(), [&] (const std::vector<V> &a, const std::vector<V> &b) { return a[maxx (a)].p.x > b[maxx (b)].p.x; });
	std::vector<std::pair<double, size_t>> near;
	for (size_t h = 0; h < holes.size(); h++) {
		const std::vector<V> &H = holes[h];
		size_t m = maxx (H);
		const Pt M = H[m].p;
		size_t best = poly.size();
		near.clear();
		for (size_t k = 0; k < poly.size(); k++)
			near.push_back ({(poly[k].p.x - M.x) * (poly[k].p.x - M.x) + (poly[k].p.y - M.y) * (poly[k].p.y - M.y), k});
		std::stable_sort (near.begin(), near.end(), [] (const auto &a, const auto &b) { return a.first < b.first; });
		for (const auto &nk : near) { // the nearest visible vertex
			size_t k = nk.second;
			bool vis = true;
			for (size_t e = 0; e < poly.size() && vis; e++)
				vis = !Crosses (M, poly[k].p, poly[e].p, poly[(e + 1) % poly.size()].p);
			for (size_t g = h; g < holes.size() && vis; g++)
				for (size_t e = 0; e < holes[g].size() && vis; e++)
					vis = !Crosses (M, poly[k].p, holes[g][e].p, holes[g][(e + 1) % holes[g].size()].p);
			if (vis) { best = k; break; }
		}
		if (best == poly.size()) { ok = false; continue; } // hole dropped: roof covers it
		std::vector<V> np (poly.begin(), poly.begin() + best + 1);
		for (size_t k = 0; k <= H.size(); k++) np.push_back (H[(m + k) % H.size()]);
		np.push_back (poly[best]);
		np.insert (np.end(), poly.begin() + best + 1, poly.end());
		poly.swap (np);
	}
	if (poly.size() > MaxPoly) return false;
	std::vector<size_t> idx (poly.size());
	for (size_t k = 0; k < idx.size(); k++) idx[k] = k;
	size_t guard = 0;
	while (idx.size() > 3 && guard < idx.size()) {
		bool cut = false;
		for (size_t k = 0; k < idx.size(); k++) {
			size_t a = idx[(k + idx.size() - 1) % idx.size()], b = idx[k], c = idx[(k + 1) % idx.size()];
			if (Cross (poly[a].p, poly[b].p, poly[c].p) <= 0) continue;
			bool ear = true;
			for (size_t q : idx)
				if (q != a && q != b && q != c && Inside (poly[q].p, poly[a].p, poly[b].p, poly[c].p)) { ear = false; break; }
			if (!ear) continue;
			tri.insert (tri.end(), {poly[a].i, poly[c].i, poly[b].i}); // clockwise: the front face
			idx.erase (idx.begin() + k);
			cut = true;
			break;
		}
		if (!cut) {
			// degenerate rest: drop a flat vertex, else give up
			size_t k = 0;
			for (; k < idx.size(); k++)
				if (Cross (poly[idx[(k + idx.size() - 1) % idx.size()]].p, poly[idx[k]].p, poly[idx[(k + 1) % idx.size()]].p) == 0) break;
			if (k == idx.size()) return false;
			idx.erase (idx.begin() + k);
			guard++;
		}
	}
	if (idx.size() == 3 && Cross (poly[idx[0]].p, poly[idx[1]].p, poly[idx[2]].p) > 0)
		tri.insert (tri.end(), {poly[idx[0]].i, poly[idx[2]].i, poly[idx[1]].i});
	return ok;
}

void TsBldg::TileOf (double lat, double lng, int z, int &x, int &y)
{
	int n = 1 << z;
	lat = std::max (-1.4844, std::min (1.4844, lat));
	x = (int)std::floor ((lng / (2.0 * M_PI) + 0.5) * n);
	y = (int)std::floor ((1.0 - std::asinh (std::tan (lat)) / M_PI) * 0.5 * n);
	x = ((x % n) + n) % n;
	y = std::max (0, std::min (n - 1, y));
}

void TsBldg::LatLng (int z, double x, double y, double &lat, double &lng)
{
	double n = (double)(1 << z);
	lng = x / n * 2.0 * M_PI - M_PI;
	lat = std::atan (std::sinh (M_PI * (1.0 - 2.0 * y / n)));
}

namespace {
// one building's walls and flat roof from its rings (outer first) appended to m; false, m as it was, past a client group's 65535 vertices
bool Extrude (const std::vector<std::vector<TsBldg::Pt>> &rings, double bot, double top, TsBldg::Mesh &m, TsBldg::Building &b)
{
	using TsBldg::Pt;
	size_t at = m.vtx.size(), ix = m.idx.size();
	uint32_t nv = 0;
	auto vtx = [&] (double e, double h, double n, double nx, double ny, double nz) {
		m.vtx.insert (m.vtx.end(), {(float)e, (float)h, (float)n, (float)nx, (float)ny, (float)nz});
		return nv++;
	};
	// walls: outer ring clockwise and holes anticlockwise seen from above, faces to the left of travel
	for (size_t r = 0; r < rings.size(); r++) {
		std::vector<Pt> q = rings[r];
		if ((Area (q) < 0) != (r == 0)) std::reverse (q.begin(), q.end());
		for (size_t k = 0; k < q.size(); k++) {
			const Pt &p0 = q[k], &p1 = q[(k + 1) % q.size()];
			double de = p1.x - p0.x, dn = p1.y - p0.y, l = std::hypot (de, dn);
			if (l <= 0) continue;
			double nx = -dn / l, nz = de / l;
			uint32_t i0 = vtx (p0.x, bot, p0.y, nx, 0, nz);
			vtx (p1.x, bot, p1.y, nx, 0, nz);
			vtx (p1.x, top, p1.y, nx, 0, nz);
			vtx (p0.x, top, p0.y, nx, 0, nz);
			m.idx.insert (m.idx.end(), {i0, i0 + 1, i0 + 2, i0, i0 + 2, i0 + 3});
		}
	}
	std::vector<uint32_t> tri;
	TsBldg::Triangulate (rings, tri);
	uint32_t r0 = nv;
	for (const auto &r : rings)
		for (const Pt &p : r) vtx (p.x, top, p.y, 0, 1, 0);
	for (uint32_t t : tri) m.idx.push_back (r0 + t);
	if (nv > 65535) { m.vtx.resize (at); m.idx.resize (ix); return false; }
	b.v0 = (uint32_t)(at / 6); b.nv = nv; b.i0 = (uint32_t)ix; b.ni = (uint32_t)(m.idx.size() - ix);
	return true;
}

double Bot (float hmin) { return hmin > 0.0f ? (double)hmin : -4.0; } // Build's wall bottom from the stored hmin
}

TsBldg::Mesh TsBldg::Build (int z, int x, int y, const std::vector<Feature> &feats, uint32_t extent)
{
	Mesh m;
	m.z = z; m.x = x; m.y = y;
	double E = (double)extent;
	size_t budget = MaxTile;
	for (const Feature &F : feats) {
		if (F.hide) continue;
		double top = std::isfinite (F.height) && F.height > 0.0 ? std::min (F.height, MaxHeight) : 5.0;
		double bot = std::isfinite (F.minHeight) && F.minHeight > 0.0 ? std::min (F.minHeight, top) : -4.0; // sunk: slopes and terrain detail
		if (top - std::max (0.0, bot) < 0.5) continue;
		// split into polygons: a positive ring (tile y down) starts one, negative rings are its holes
		std::vector<std::vector<std::vector<Pt>>> polys;
		for (const std::vector<Pt> &r : F.rings) {
			double a = Area (r);
			if (a > 0) polys.push_back ({r});
			else if (a < 0 && !polys.empty()) polys.back().push_back (r);
		}
		for (const auto &P : polys) {
			double cx = 0, cy = 0, alat, alng;
			for (const Pt &p : P[0]) { cx += p.x; cy += p.y; }
			cx /= P[0].size(); cy /= P[0].size();
			if (cx < 0 || cy < 0 || cx >= E || cy >= E) continue; // in the buffer: the neighbour tile has it
			LatLng (z, x + cx / E, y + cy / E, alat, alng);
			std::vector<std::vector<Pt>> rings;
			double ce = Rn * std::cos (alat);
			for (const auto &r : P) {
				std::vector<Pt> q;
				for (const Pt &p : r) {
					double la, ln;
					LatLng (z, x + p.x / E, y + p.y / E, la, ln);
					Pt v {ce * (ln - alng), Rn * (la - alat)};
					if (!q.empty() && std::hypot (v.x - q.back().x, v.y - q.back().y) < 0.05) continue;
					q.push_back (v);
				}
				while (q.size() > 1 && std::hypot (q[0].x - q.back().x, q[0].y - q.back().y) < 0.05) q.pop_back();
				if (q.size() >= 3 && std::fabs (Area (q)) > 1.0) rings.push_back (q);
				else if (rings.empty()) break;
			}
			if (rings.empty() || rings[0].size() > MaxPoly) continue;
			size_t total = rings[0].size(), keep = 1;
			for (size_t r = 1; r < rings.size(); r++) // holes past the caps dropped, the outer ring kept
				if (keep <= MaxHoles && total + rings[r].size() + 2 <= MaxPoly) {
					total += rings[r].size() + 2;
					if (keep != r) rings[keep] = std::move (rings[r]);
					keep++;
				}
			rings.resize (keep);
			if (total > budget) continue; // the tile's vertex budget is spent
			budget -= total;
			for (auto &r : rings) for (Pt &p : r) p = {(double)(float)p.x, (double)(float)p.y}; // as a version 2 file keeps them
			Building b {alat, alng, (float)std::max (0.0, bot), (float)top, 0, 0, 0, 0};
			if (!Extrude (rings, bot, top, m, b)) continue; // a client group's limit
			b.r0 = (uint32_t)m.rsz.size();
			b.nr = (uint32_t)rings.size();
			for (const auto &r : rings) {
				m.rsz.push_back ((uint32_t)r.size());
				for (const Pt &p : r) m.pts.insert (m.pts.end(), {(float)p.x, (float)p.y});
			}
			m.b.push_back (b);
		}
	}
	return m;
}

namespace {
template<class T> void Put (std::string &s, const T &v) { s.append ((const char*)&v, sizeof v); }
template<class T> bool Get (const std::string &s, size_t &at, T &v)
{
	if (s.size() - at < sizeof v || at > s.size()) return false;
	memcpy (&v, s.data() + at, sizeof v);
	at += sizeof v;
	return true;
}
}

namespace {
// version 2: per building lat, lng, hmin, hmax and its ring count, then the ring sizes, then the points; walls and roofs made again
bool ReadV2 (const std::string &s, size_t at, uint32_t nb, uint32_t nr, uint32_t np, TsBldg::Mesh &m)
{
	if ((uint64_t)nb * 26 + (uint64_t)nr * 2 + (uint64_t)np * 8 != s.size() - at || np > MaxTile) return false;
	std::vector<uint16_t> cnt (nb);
	m.b.resize (nb);
	uint64_t rings = 0, pts = 0;
	for (uint32_t i = 0; i < nb; i++) {
		TsBldg::Building &b = m.b[i];
		Get (s, at, b.lat); Get (s, at, b.lng); Get (s, at, b.hmin); Get (s, at, b.hmax); Get (s, at, cnt[i]);
		if (!(std::fabs (b.lat) <= M_PI) || !(std::fabs (b.lng) <= 2.0 * M_PI) || !(b.hmin >= 0.0f) || !(b.hmax >= b.hmin) || !(b.hmax <= MaxHeight)) return false;
		if (cnt[i] < 1 || cnt[i] > MaxHoles + 1) return false;
		rings += cnt[i];
	}
	if (rings != nr) return false;
	m.rsz.resize (nr);
	for (uint32_t &r : m.rsz) {
		uint16_t n = 0;
		Get (s, at, n);
		if (n < 3 || n > MaxPoly) return false;
		r = n;
		pts += n;
	}
	if (pts != np) return false;
	m.pts.resize ((size_t)np * 2);
	if (np) memcpy (m.pts.data(), s.data() + at, m.pts.size() * sizeof (float));
	for (float v : m.pts) if (!(std::fabs (v) < 1e5f)) return false; // NaN too
	std::vector<std::vector<TsBldg::Pt>> rr;
	size_t r = 0, p = 0;
	for (uint32_t i = 0; i < nb; i++) {
		TsBldg::Building &b = m.b[i];
		rr.assign (cnt[i], {});
		size_t total = 0;
		for (size_t k = 0; k < cnt[i]; k++) {
			size_t n = m.rsz[r + k];
			total += n + (k ? 2 : 0); // Build's cap: holes count their bridge
			for (size_t j = 0; j < n; j++, p++) rr[k].push_back ({(double)m.pts[2 * p], (double)m.pts[2 * p + 1]});
		}
		if (total > MaxPoly) return false;
		b.r0 = (uint32_t)r;
		b.nr = cnt[i];
		r += cnt[i];
		if (!Extrude (rr, Bot (b.hmin), (double)b.hmax, m, b)) return false; // Build never writes one past the limit
	}
	return true;
}
}

std::string TsBldg::Encode (const Mesh &m)
{
	std::string s ("OBLD");
	size_t nr = 0, np = 0;
	bool v2 = true;
	for (const Building &b : m.b) {
		if (b.nr < 1 || b.r0 != nr || (size_t)b.r0 + b.nr > m.rsz.size()) { v2 = false; break; }
		for (uint32_t r = b.r0; r < b.r0 + b.nr; r++) np += m.rsz[r];
		nr += b.nr;
	}
	if (v2 && nr == m.rsz.size() && np * 2 == m.pts.size()) {
		Put (s, (uint32_t)2);
		Put (s, (int32_t)m.z); Put (s, (int32_t)m.x); Put (s, (int32_t)m.y);
		Put (s, (uint32_t)m.b.size()); Put (s, (uint32_t)nr); Put (s, (uint32_t)np);
		for (const Building &b : m.b) {
			Put (s, b.lat); Put (s, b.lng); Put (s, b.hmin); Put (s, b.hmax); Put (s, (uint16_t)b.nr);
		}
		for (uint32_t n : m.rsz) Put (s, (uint16_t)n);
		if (!m.pts.empty()) s.append ((const char*)m.pts.data(), m.pts.size() * sizeof (float));
		return s;
	}
	Put (s, (uint32_t)1);
	Put (s, (int32_t)m.z); Put (s, (int32_t)m.x); Put (s, (int32_t)m.y);
	Put (s, (uint32_t)m.b.size()); Put (s, (uint32_t)(m.vtx.size() / 6)); Put (s, (uint32_t)m.idx.size());
	for (const Building &b : m.b) {
		Put (s, b.lat); Put (s, b.lng); Put (s, b.hmin); Put (s, b.hmax);
		Put (s, b.v0); Put (s, b.nv); Put (s, b.i0); Put (s, b.ni);
	}
	if (!m.vtx.empty()) s.append ((const char*)m.vtx.data(), m.vtx.size() * sizeof (float));
	if (!m.idx.empty()) s.append ((const char*)m.idx.data(), m.idx.size() * sizeof (uint32_t));
	return s;
}

bool TsBldg::Read (const std::string &s, Mesh &m)
{
	m = Mesh();
	size_t at = 4;
	uint32_t ver, nb, nv, ni;
	int32_t z, x, y;
	if (s.size() < 4 || s.compare (0, 4, "OBLD") || !Get (s, at, ver) || (ver != 1 && ver != 2)) return false;
	if (!Get (s, at, z) || !Get (s, at, x) || !Get (s, at, y) || !Get (s, at, nb) || !Get (s, at, nv) || !Get (s, at, ni)) return false;
	if (ver == 2) { // nv, ni: rings and points
		m.z = z; m.x = x; m.y = y;
		return ReadV2 (s, at, nb, nv, ni, m);
	}
	if ((uint64_t)nb * 40 + (uint64_t)nv * 24 + (uint64_t)ni * 4 != s.size() - at) return false;
	m.z = z; m.x = x; m.y = y;
	m.b.resize (nb);
	for (Building &b : m.b) {
		Get (s, at, b.lat); Get (s, at, b.lng); Get (s, at, b.hmin); Get (s, at, b.hmax);
		Get (s, at, b.v0); Get (s, at, b.nv); Get (s, at, b.i0); Get (s, at, b.ni);
		if ((uint64_t)b.v0 + b.nv > nv || (uint64_t)b.i0 + b.ni > ni) return false;
	}
	m.vtx.resize ((size_t)nv * 6);
	if (nv) memcpy (m.vtx.data(), s.data() + at, m.vtx.size() * sizeof (float));
	at += m.vtx.size() * sizeof (float);
	m.idx.resize (ni);
	if (ni) memcpy (m.idx.data(), s.data() + at, m.idx.size() * sizeof (uint32_t));
	for (const Building &b : m.b)
		for (uint32_t i = b.i0; i < b.i0 + b.ni; i++)
			if (m.idx[i] >= b.nv) return false;
	return true;
}
