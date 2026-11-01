// not upstream (stream-rwyflat): streamed runways from z11 vector tiles: joined, measured, fitted, kept (TileStream.h)

#include "TileStream.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

struct P { double x, y; }; // local metres, x east, y north

// a frame round a point: metres east and north
struct Frame {
	double latc = 0.0, lngc = 0.0, R = 6371010.0, kx = 1.0;
	Frame (double lat, double lng, double r): latc (lat), lngc (lng), R (r), kx (r * std::cos (lat)) {}
	P Local (double lat, double lng) const { return {std::remainder (lng - lngc, 2.0 * M_PI) * kx, (lat - latc) * R}; }
	void LatLng (const P &p, double &lat, double &lng) const { lat = latc + p.y / R; lng = std::remainder (lngc + p.x / kx, 2.0 * M_PI); }
};

struct Piece { P a, b; bool cutA = false, cutB = false; std::string ref; };

// a ref as runways.idx keeps it: no control bytes, no edge spaces, at most 15 bytes, else none
std::string CleanRef (const std::string &s)
{
	std::string r;
	for (char c : s) if ((unsigned char)c >= 0x20 && c != 0x7f) r += c;
	size_t a = r.find_first_not_of (' '), b = r.find_last_not_of (' ');
	r = a == std::string::npos ? std::string() : r.substr (a, b - a + 1);
	return r.size() <= 15 ? r : std::string();
}

// pieces of one runway across tile edges: the same ref or one without, within 2 deg and 10 m of the longer one's line, overlapping or 10 m apart
void Join (std::vector<Piece> &rw)
{
	const double sin2 = std::sin (2.0 * M_PI / 180.0), tol = 10.0;
	for (bool merged = true; merged; ) {
		merged = false;
		for (size_t i = 0; i < rw.size() && !merged; i++)
			for (size_t j = i + 1; j < rw.size() && !merged; j++) {
				bool swap = std::hypot (rw[j].b.x - rw[j].a.x, rw[j].b.y - rw[j].a.y) > std::hypot (rw[i].b.x - rw[i].a.x, rw[i].b.y - rw[i].a.y);
				Piece &a = swap ? rw[j] : rw[i], &b = swap ? rw[i] : rw[j];
				if (a.ref != b.ref && !a.ref.empty() && !b.ref.empty()) continue;
				double dx = a.b.x - a.a.x, dy = a.b.y - a.a.y, L = std::hypot (dx, dy);
				double ex = b.b.x - b.a.x, ey = b.b.y - b.a.y, M = std::hypot (ex, ey);
				if (L < 1.0 || M < 1.0) continue;
				double ux = dx / L, uy = dy / L;
				if (std::fabs (ux * ey - uy * ex) / M > sin2) continue;
				auto off = [&] (const P &p) { return std::fabs ((p.x - a.a.x) * uy - (p.y - a.a.y) * ux); };
				if (off (b.a) > tol || off (b.b) > tol) continue;
				double t[4] = {0.0, L, (b.a.x - a.a.x) * ux + (b.a.y - a.a.y) * uy, (b.b.x - a.a.x) * ux + (b.b.y - a.a.y) * uy};
				if (std::min (t[2], t[3]) > L + tol || std::max (t[2], t[3]) < -tol) continue;
				P pt[4] = {a.a, a.b, b.a, b.b};
				bool cut[4] = {a.cutA, a.cutB, b.cutA, b.cutB};
				int lo = 0, hi = 0;
				for (int k = 1; k < 4; k++) {
					if (t[k] < t[lo] - 1e-6 || (std::fabs (t[k] - t[lo]) <= 1e-6 && !cut[k])) lo = k;
					if (t[k] > t[hi] + 1e-6 || (std::fabs (t[k] - t[hi]) <= 1e-6 && !cut[k])) hi = k;
				}
				Piece m = a;
				if (m.ref.empty()) m.ref = b.ref;
				m.a = pt[lo]; m.cutA = cut[lo];
				m.b = pt[hi]; m.cutB = cut[hi];
				if (swap) rw[j] = m; else rw[i] = m;
				rw.erase (rw.begin() + (swap ? i : j));
				merged = true;
			}
	}
}

bool InRings (const std::vector<std::vector<P>> &rings, double x, double y)
{
	bool in = false;
	for (const auto &r : rings)
		for (size_t i = 0, k = r.size() - 1; i < r.size(); k = i++)
			if ((r[i].y > y) != (r[k].y > y) && x < r[k].x + (y - r[k].y) * (r[i].x - r[k].x) / (r[i].y - r[k].y)) in = !in;
	return in;
}

// the width from the runway areas round it, across at a quarter, half and three quarters (their median), else by its length
double Width (const Piece &r, const std::vector<std::vector<std::vector<P>>> &areas)
{
	double dx = r.b.x - r.a.x, dy = r.b.y - r.a.y, L = std::hypot (dx, dy);
	std::vector<double> ws;
	if (L > 1.0) {
		double nx = -dy / L, ny = dx / L;
		for (double f : {0.25, 0.5, 0.75}) {
			double x = r.a.x + dx * f, y = r.a.y + dy * f;
			for (const auto &rings : areas) {
				if (!InRings (rings, x, y)) continue;
				double side[2] = {1e30, 1e30};
				for (const auto &ring : rings)
					for (size_t i = 0, k = ring.size() - 1; i < ring.size(); k = i++) {
						double ex = ring[i].x - ring[k].x, ey = ring[i].y - ring[k].y, den = nx * ey - ny * ex;
						if (std::fabs (den) < 1e-12) continue;
						double qx = ring[k].x - x, qy = ring[k].y - y;
						double tt = (qx * ey - qy * ex) / den, uu = (qx * ny - qy * nx) / den;
						if (uu < 0.0 || uu > 1.0) continue;
						if (tt > 0.0) side[0] = std::min (side[0], tt);
						else side[1] = std::min (side[1], -tt);
					}
				if (side[0] < 120.0 && side[1] < 120.0) ws.push_back (side[0] + side[1]);
				break;
			}
		}
	}
	if (!ws.empty()) {
		std::sort (ws.begin(), ws.end());
		return std::clamp (ws[ws.size() / 2], 18.0, 80.0);
	}
	return L >= 2400.0 ? 45.0 : L >= 1500.0 ? 30.0 : L >= 900.0 ? 23.0 : 18.0;
}

// a runway's ends as metres round its own base point
void Ends (const TsBase &b, double R, double lat[2], double lng[2])
{
	const TsBase::Part &p = b.parts[0];
	double k = R * std::cos (b.lat);
	lat[0] = b.lat + p.n0 / R; lng[0] = std::remainder (b.lng + p.e0 / k, 2.0 * M_PI);
	lat[1] = b.lat + p.n1 / R; lng[1] = std::remainder (b.lng + p.e1 / k, 2.0 * M_PI);
}

// the closest points of segments a0-a1 and b0-b1: their parameters and distance
double SegSeg (P a0, P a1, P b0, P b1, double &s, double &t)
{
	double ux = a1.x - a0.x, uy = a1.y - a0.y, vx = b1.x - b0.x, vy = b1.y - b0.y, wx = a0.x - b0.x, wy = a0.y - b0.y;
	double a = ux * ux + uy * uy, b = ux * vx + uy * vy, c = vx * vx + vy * vy, d = ux * wx + uy * wy, e = vx * wx + vy * wy;
	const double eps = 1e-9;
	if (a <= eps && c <= eps) s = t = 0.0;
	else if (a <= eps) { s = 0.0; t = std::clamp (e / c, 0.0, 1.0); }
	else if (c <= eps) { t = 0.0; s = std::clamp (-d / a, 0.0, 1.0); }
	else {
		double D = a * c - b * b;
		s = D > eps * a * c ? std::clamp ((b * e - c * d) / D, 0.0, 1.0) : 0.0;
		t = (b * s + e) / c;
		if (t < 0.0) { t = 0.0; s = std::clamp (-d / a, 0.0, 1.0); }
		else if (t > 1.0) { t = 1.0; s = std::clamp ((b - d) / a, 0.0, 1.0); }
	}
	return std::hypot (wx + s * ux - t * vx, wy + s * uy - t * vy);
}

} // namespace

namespace TsRwy {

TsWay::Tile Keep (const TsWay::Tile &t)
{
	TsWay::Tile o;
	o.z = t.z, o.x = t.x, o.y = t.y;
	for (const TsWay::Feature &f : t.f)
		if (f.kind == TsWay::RUNWAY) o.f.push_back (f);
	return o;
}

void TileOf (double lat, double lng, int &x, int &y)
{
	int n = 1 << Z;
	lat = std::clamp (lat, -1.4844, 1.4844);
	x = (int)std::floor ((std::remainder (lng, 2.0 * M_PI) / (2.0 * M_PI) + 0.5) * n);
	x = (x % n + n) % n;
	y = std::clamp ((int)std::floor ((1.0 - std::asinh (std::tan (lat)) / M_PI) * 0.5 * n), 0, n - 1);
}

void TileBox (int x, int y, double &lat0, double &lat1, double &lng0, double &lng1)
{
	TsBldg::LatLng (Z, x, y + 1, lat0, lng0);
	TsBldg::LatLng (Z, x + 1, y, lat1, lng1);
	if (lng1 < lng0) lng1 += 2.0 * M_PI;
}

std::vector<Run> Runways (const std::vector<TsWay::Tile> &tiles, double R)
{
	std::vector<Run> out;
	if (tiles.empty()) return out;
	double la0, la1, ln0, ln1;
	TileBox (tiles[0].x, tiles[0].y, la0, la1, ln0, ln1);
	Frame g (0.5 * (la0 + la1), 0.5 * (ln0 + ln1), R);
	std::vector<Piece> pieces;
	std::vector<std::vector<std::vector<P>>> areas;
	for (const TsWay::Tile &T : tiles)
		for (const TsWay::Feature &F : T.f) {
			if (F.kind != TsWay::RUNWAY) continue;
			bool area = (F.flags & TsWay::AREA) != 0;
			std::vector<std::vector<P>> rings;
			for (const auto &q : F.parts) {
				std::vector<P> pts;
				bool cut[2] = {false, false};
				for (size_t i = 0; i + 1 < q.size(); i += 2) {
					double u = q[i], v = q[i + 1], lat, lng;
					TsBldg::LatLng (T.z, T.x + u / TsWay::Extent, T.y + v / TsWay::Extent, lat, lng);
					bool c = u < 0.5 || u > TsWay::Extent - 0.5 || v < 0.5 || v > TsWay::Extent - 0.5;
					if (pts.empty()) cut[0] = c;
					cut[1] = c;
					pts.push_back (g.Local (lat, lng));
				}
				if (pts.size() < 2) continue;
				if (area) rings.push_back (std::move (pts));
				else pieces.push_back ({pts.front(), pts.back(), cut[0], cut[1], CleanRef (F.ref)});
			}
			if (area && !rings.empty()) areas.push_back (std::move (rings));
		}
	Join (pieces);
	for (const Piece &p : pieces) {
		Run r;
		g.LatLng (p.a, r.lat[0], r.lng[0]);
		g.LatLng (p.b, r.lat[1], r.lng[1]);
		r.cut[0] = p.cutA, r.cut[1] = p.cutB;
		r.ref = p.ref;
		r.w = Width (p, areas);
		out.push_back (r);
	}
	return out;
}

double Length (const Run &r, double R)
{
	double c = std::cos (0.5 * (r.lat[0] + r.lat[1]));
	return std::hypot ((r.lat[1] - r.lat[0]) * R, std::remainder (r.lng[1] - r.lng[0], 2.0 * M_PI) * R * c);
}

bool OnBase (const Run &r, const std::vector<TsBase> &bases, double R)
{
	double pl[3] = {r.lat[0], r.lat[1], 0.5 * (r.lat[0] + r.lat[1])};
	double pg[3] = {r.lng[0], r.lng[1], r.lng[0] + 0.5 * std::remainder (r.lng[1] - r.lng[0], 2.0 * M_PI)};
	for (const TsBase &b : bases) {
		if (b.rwy) continue;
		for (int i = 0; i < 3; i++) {
			double nn = (pl[i] - b.lat) * R, ee = std::remainder (pg[i] - b.lng, 2.0 * M_PI) * R * std::cos (b.lat);
			if (std::fabs (nn) > b.ext || std::fabs (ee) > b.ext) continue;
			for (const TsBase::Part &p : b.parts)
				if (TsSky::PartDist (p, nn, ee) <= 0.0) return true;
		}
	}
	return false;
}

bool Same (const Run &r, const TsBase &b, double R)
{
	if (!b.rwy || b.parts.empty()) return false;
	if (!r.ref.empty() && !b.name.empty() && b.name != "runway " + r.ref && b.name != "runway") return false;
	double lat[2], lng[2];
	Ends (b, R, lat, lng);
	auto d = [R] (double la0, double ln0, double la1, double ln1) {
		return std::hypot ((la1 - la0) * R, std::remainder (ln1 - ln0, 2.0 * M_PI) * R * std::cos (la0));
	};
	return (d (r.lat[0], r.lng[0], lat[0], lng[0]) < 20.0 && d (r.lat[1], r.lng[1], lat[1], lng[1]) < 20.0)
		|| (d (r.lat[0], r.lng[0], lat[1], lng[1]) < 20.0 && d (r.lat[1], r.lng[1], lat[0], lng[0]) < 20.0);
}

TsBase Flat (const Run &r, double R, double h0, double h1)
{
	TsBase b;
	b.name = r.ref.empty() ? "runway" : "runway " + r.ref;
	b.lat = 0.5 * (r.lat[0] + r.lat[1]);
	b.lng = std::remainder (r.lng[0] + 0.5 * std::remainder (r.lng[1] - r.lng[0], 2.0 * M_PI), 2.0 * M_PI);
	Frame g (b.lat, b.lng, R);
	P a = g.Local (r.lat[0], r.lng[0]), e = g.Local (r.lat[1], r.lng[1]);
	double rad = 0.5 * r.w + Shoulder;
	b.parts.push_back ({a.y, a.x, e.y, e.x, rad});
	b.ext = std::max (std::hypot (a.x, a.y), std::hypot (e.x, e.y)) + rad;
	b.rwy = true;
	b.h[0] = h0, b.h[1] = h1;
	return b;
}

std::vector<std::pair<double, double>> Meets (const Run &r, const std::vector<const TsBase *> &fixed, double R)
{
	std::vector<std::pair<double, double>> out;
	double rr = 0.5 * r.w + Shoulder;
	for (const TsBase *f : fixed) {
		if (!f || !f->rwy || f->parts.empty()) continue;
		Frame g (f->lat, f->lng, R);
		P a = g.Local (r.lat[0], r.lng[0]), b = g.Local (r.lat[1], r.lng[1]);
		const TsBase::Part &p = f->parts[0];
		if (std::fabs (a.y) > f->ext + rr + 1e4 || std::fabs (a.x) > f->ext + rr + 1e4) continue; // nowhere near
		double s, t, d = SegSeg (a, b, {p.e0, p.n0}, {p.e1, p.n1}, s, t);
		if (d < rr + p.r) out.push_back ({s, f->h[0] + t * (f->h[1] - f->h[0])});
	}
	return out;
}

bool Fit (const std::vector<std::pair<double, double>> &s, const std::vector<std::pair<double, double>> &meets, double step, double len, double &h0, double &h1)
{
	if (s.size() < 2 && meets.empty()) return false;
	std::vector<char> on (s.size(), 1);
	auto line = [&] (bool held, double &a, double &b) {
		double S = 0, St = 0, Stt = 0, Sh = 0, Sth = 0;
		auto add = [&] (double t, double h, double w) { S += w; St += w * t; Stt += w * t * t; Sh += w * h; Sth += w * t * h; };
		for (size_t i = 0; i < s.size(); i++) if (on[i]) add (s[i].first, s[i].second, 1.0);
		if (held) for (const auto &m : meets) add (m.first, m.second, 1000.0);
		if (S <= 0.0) return false;
		double D = S * Stt - St * St;
		if (D > 1e-12 * S * S) { b = (S * Sth - St * Sh) / D; a = (Sh - b * St) / S; }
		else { b = 0.0; a = Sh / S; }
		return true;
	};
	double a = 0.0, b = 0.0, sig = 0.0;
	size_t n = 0;
	if (s.size() >= 2) { // the ground's own line, outliers dropped twice
		for (int it = 0; it < 3 && line (false, a, b); it++) {
			if (it == 2) break;
			std::vector<double> r;
			for (size_t i = 0; i < s.size(); i++) if (on[i]) r.push_back (std::fabs (s[i].second - a - b * s[i].first));
			if (r.size() < 3) break;
			std::nth_element (r.begin(), r.begin() + r.size() / 2, r.end());
			double lim = std::max (0.5, 3.0 * 1.4826 * r[r.size() / 2]);
			size_t left = 0;
			for (size_t i = 0; i < s.size(); i++) {
				if (on[i] && std::fabs (s[i].second - a - b * s[i].first) > lim) on[i] = 0;
				left += on[i];
			}
			if (left < 2) break;
		}
		for (size_t i = 0; i < s.size(); i++)
			if (on[i]) { double r = s[i].second - a - b * s[i].first; sig += r * r; n++; }
		sig = n > 2 ? std::sqrt (sig / (n - 2)) : 0.0;
	}
	// level unless its slope stands out of the heights' noise: about one independent height every 30 m (SRTM), 2 sigma, and at least 1 m end to end
	double nEff = std::max (2.0, len / 30.0);
	if (s.size() < 2 || std::fabs (b) < std::max (1.0, 2.0 * sig * std::sqrt (12.0 / nEff))) {
		double H;
		if (meets.empty()) H = step > 0.0 ? std::round ((a + 0.5 * b) / step) * step : a + 0.5 * b; // on the height step: no step along it
		else {
			H = 0.0;
			for (const auto &m : meets) H += m.second;
			H /= meets.size();
		}
		h0 = h1 = H;
	}
	else {
		if (!meets.empty() && !line (true, a, b)) return false; // held where it meets the fixed ones
		h0 = a, h1 = a + b;
	}
	return std::isfinite (h0) && std::isfinite (h1);
}

std::string Line (const TsBase &b, double R)
{
	if (!b.rwy || b.parts.empty()) return std::string();
	double lat[2], lng[2];
	Ends (b, R, lat, lng);
	const double deg = 180.0 / M_PI;
	char s[160];
	snprintf (s, sizeof s, "%.7f %.7f %.7f %.7f %.1f %.3f %.3f ", lat[0] * deg, lng[0] * deg, lat[1] * deg, lng[1] * deg, 2.0 * (b.parts[0].r - Shoulder), b.h[0], b.h[1]);
	return s + (b.name.size() > 7 ? b.name.substr (7) : std::string());
}

bool Parse (const std::string &line, double R, TsBase &b)
{
	double v[7];
	int n = 0;
	if (sscanf (line.c_str(), "%lf %lf %lf %lf %lf %lf %lf %n", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, &n) != 7) return false;
	for (double x : v) if (!std::isfinite (x)) return false;
	if (std::fabs (v[0]) > 85.1 || std::fabs (v[2]) > 85.1 || std::fabs (v[1]) > 180.0 || std::fabs (v[3]) > 180.0) return false;
	if (v[4] < 1.0 || v[4] > 200.0 || std::fabs (v[5]) > 12000.0 || std::fabs (v[6]) > 12000.0) return false;
	Run r;
	const double rad = M_PI / 180.0;
	r.lat[0] = v[0] * rad, r.lng[0] = v[1] * rad, r.lat[1] = v[2] * rad, r.lng[1] = v[3] * rad, r.w = v[4];
	r.ref = line.substr (std::min ((size_t)n, line.size()));
	while (!r.ref.empty() && (r.ref.back() == '\r' || r.ref.back() == '\n' || r.ref.back() == ' ')) r.ref.pop_back();
	if (r.ref.size() > 15 || Length (r, R) < 1.0) return false;
	b = Flat (r, R, v[5], v[6]);
	return true;
}

} // namespace TsRwy
