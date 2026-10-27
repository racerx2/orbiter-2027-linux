// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: streamed ways painted into a terrain tile's way textures (WayGeom.h, design-ways)

#include "WayGeom.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>

namespace WayGeom {

namespace {

const double Pi = 3.14159265358979323846;

// luminances on the imagery's scale, neutral grey: asphalt darker than the imagery's roads so it stands out (user: "Neutral grey")
const float Asphalt = 0.32f, Concrete = 0.72f, Dirt = 0.50f, Ballast = 0.42f, Walk = 0.62f, Deck = 0.52f;
const float White = 0.95f, Yellow = 0.85f, Steel = 0.22f, Sleeper = 0.55f;
const double LineH = 0.075;   // half width of road and taxiway lines [m]
const double LaneW = 3.6;     // one-way lanes [m]
const double LaneW2 = 3.5;    // two-way lanes [m]
const double PoolS = 8.96, HeadS = 1.54; // lamp pool and head profile exp(-d^2/s^2) [m], the night look's Sp and Sh (NightLamps)
const int MaskN = 128;        // base mask cells a side

struct P { double x, y; };

// the box's local frame: x east, y north [m] from its centre
struct Frame {
	int n = 0;
	double W = 0, H = 0, px = 0, py = 0;
	void Set (int nn, double w, double h) { n = nn; W = w; H = h; px = W / n; py = H / n; }
	double X (int i) const { return -0.5 * W + (i + 0.5) * px; }
	double Y (int j) const { return 0.5 * H - (j + 0.5) * py; }
	int Lo (double v) const { return (int)std::max (0.0, std::min ((double)n, std::ceil (v - 0.5))); }        // first index at or past v texels
	int Hi (double v) const { return (int)std::max (-1.0, std::min ((double)(n - 1), std::floor (v - 0.5))); } // last index at or before
	int I0 (double x) const { return Lo ((x + 0.5 * W) / px); }
	int I1 (double x) const { return Hi ((x + 0.5 * W) / px); }
	int J0 (double y) const { return Lo ((0.5 * H - y) / py); }
	int J1 (double y) const { return Hi ((0.5 * H - y) / py); }
};

struct Geo {
	double latc, lngc, R, kx;
	P Local (double lat, double lng) const
	{
		double d = lng - lngc;
		d -= 2.0 * Pi * std::floor ((d + Pi) / (2.0 * Pi));
		return {d * kx, (lat - latc) * R};
	}
	void LatLng (const P &p, double &lat, double &lng) const { lat = latc + p.y / R; lng = lngc + p.x / kx; }
};

struct Shape {
	Style s;
	uint8_t kind = 0, flags = 0;
	int layer = 0;
	bool area = false;
	std::vector<std::vector<P>> parts;
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;           // its box
	double cx0 = -1e30, cy0 = -1e30, cx1 = 1e30, cy1 = 1e30;       // its tile's inside: the texels it paints
	const Runway *rw = nullptr;
};

bool Aero (uint8_t k) { return k == TSWAY_RUNWAY || k == TSWAY_TAXIWAY || k == TSWAY_APRON || k == TSWAY_HELIPAD; }

// a tile's features as shapes in the frame; runway lines as pieces
void Shapes (const Tile &T, const Geo &g, std::vector<Shape> &out, std::vector<Runway> *pieces)
{
	P w = g.Local (T.Lat (0), T.Lng (0)), e = g.Local (T.Lat (4096), T.Lng (4096));
	for (const Tile::F &F : T.f) {
		Shape S;
		S.kind = F.kind;
		S.flags = F.flags;
		S.area = (F.flags & TSWAY_AREA) != 0;
		S.layer = F.layer + ((F.flags & TSWAY_BRIDGE) && F.layer == 0 ? 1 : 0); // a bridge over what it crosses
		S.s = StyleOf (F.kind, F.flags);
		S.cx0 = w.x; S.cx1 = e.x; S.cy0 = e.y; S.cy1 = w.y;
		for (uint32_t p = F.p0; p < F.p0 + F.np && p + 1 < T.part.size(); p++) {
			std::vector<P> q;
			bool cut[2] = {false, false};
			for (uint32_t i = T.part[p]; i < T.part[p + 1] && 2 * i + 1 < T.pt.size(); i++) {
				double u = T.pt[2 * i], v = T.pt[2 * i + 1];
				P a = g.Local (T.Lat (v), T.Lng (u));
				bool c = u < 0.5 || u > 4095.5 || v < 0.5 || v > 4095.5;
				if (q.empty()) cut[0] = c;
				cut[1] = c;
				q.push_back (a);
				S.x0 = std::min (S.x0, a.x); S.x1 = std::max (S.x1, a.x);
				S.y0 = std::min (S.y0, a.y); S.y1 = std::max (S.y1, a.y);
			}
			if (q.size() < 2) continue;
			if (pieces && F.kind == TSWAY_RUNWAY && !S.area) {
				Runway r;
				r.ax = q.front().x; r.ay = q.front().y; r.bx = q.back().x; r.by = q.back().y;
				r.cutA = cut[0]; r.cutB = cut[1];
				r.ref = F.ref;
				pieces->push_back (r);
				continue;
			}
			S.parts.push_back (std::move (q));
		}
		if (!S.parts.empty()) out.push_back (std::move (S));
	}
}

// pieces of one runway across tile edges and OSM ways: the same ref or one without, within 2 deg and 5 m, overlapping or 5 m apart
void Join (std::vector<Runway> &rw)
{
	const double sin2 = std::sin (2.0 * Pi / 180.0);
	for (bool merged = true; merged; ) {
		merged = false;
		for (size_t i = 0; i < rw.size() && !merged; i++)
			for (size_t j = i + 1; j < rw.size() && !merged; j++) {
				bool swap = std::hypot (rw[j].bx - rw[j].ax, rw[j].by - rw[j].ay) > std::hypot (rw[i].bx - rw[i].ax, rw[i].by - rw[i].ay);
				Runway &a = swap ? rw[j] : rw[i], &b = swap ? rw[i] : rw[j]; // the longer one's axis: a short piece's direction is coarse
				if (a.ref != b.ref && !a.ref.empty() && !b.ref.empty()) continue;
				double dx = a.bx - a.ax, dy = a.by - a.ay, L = std::hypot (dx, dy);
				double ex = b.bx - b.ax, ey = b.by - b.ay, M = std::hypot (ex, ey);
				if (L < 1.0 || M < 1.0) continue;
				double ux = dx / L, uy = dy / L;
				if (std::fabs (ux * ey - uy * ex) / M > sin2) continue;
				auto off = [&] (double x, double y) { return std::fabs ((x - a.ax) * uy - (y - a.ay) * ux); };
				if (off (b.ax, b.ay) > 5.0 || off (b.bx, b.by) > 5.0) continue;
				double t[4] = {0.0, L, (b.ax - a.ax) * ux + (b.ay - a.ay) * uy, (b.bx - a.ax) * ux + (b.by - a.ay) * uy};
				if (std::min (t[2], t[3]) > L + 5.0 || std::max (t[2], t[3]) < -5.0) continue;
				double px[4] = {a.ax, a.bx, b.ax, b.bx}, py[4] = {a.ay, a.by, b.ay, b.by};
				bool cut[4] = {a.cutA, a.cutB, b.cutA, b.cutB};
				int lo = 0, hi = 0;
				for (int k = 1; k < 4; k++) {
					if (t[k] < t[lo] - 1e-6 || (std::fabs (t[k] - t[lo]) <= 1e-6 && !cut[k])) lo = k;
					if (t[k] > t[hi] + 1e-6 || (std::fabs (t[k] - t[hi]) <= 1e-6 && !cut[k])) hi = k;
				}
				Runway m = a;
				if (m.ref.empty()) m.ref = b.ref;
				m.ax = px[lo]; m.ay = py[lo]; m.cutA = cut[lo];
				m.bx = px[hi]; m.by = py[hi]; m.cutB = cut[hi];
				a = m;
				if (swap) rw[i] = m;
				rw.erase (rw.begin() + j);
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

// a runway's width from the runway areas round it: their edges either side at a quarter, half and three quarters, else by its length
double RunwayWidth (const Runway &r, const std::vector<Shape> &sh)
{
	double dx = r.bx - r.ax, dy = r.by - r.ay, L = std::hypot (dx, dy);
	std::vector<double> ws;
	if (L > 1.0) {
		double nx = -dy / L, ny = dx / L;
		for (double f : {0.25, 0.5, 0.75}) {
			double x = r.ax + dx * f, y = r.ay + dy * f;
			for (const Shape &s : sh) {
				if (s.kind != TSWAY_RUNWAY || !s.area || x < s.x0 || x > s.x1 || y < s.y0 || y > s.y1 || !InRings (s.parts, x, y)) continue;
				double side[2] = {1e30, 1e30};
				for (const auto &ring : s.parts)
					for (size_t i = 0, k = ring.size() - 1; i < ring.size(); k = i++) {
						double ex = ring[i].x - ring[k].x, ey = ring[i].y - ring[k].y;
						double den = nx * ey - ny * ex;
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
		return std::max (10.0, ws[ws.size() / 2]);
	}
	return L >= 2400.0 ? 45.0 : L >= 1500.0 ? 30.0 : L >= 900.0 ? 23.0 : 18.0;
}

struct Seg { double ax, ay, bx, by; };

// FAA-style designator glyphs in a unit box, y from the threshold, x to the pilot's right
const std::vector<std::vector<P>> &Glyph (char c)
{
	static const std::vector<std::vector<P>> none;
	static const std::vector<std::vector<P>> g[13] = {
		{{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}}},
		{{{0.5, 0}, {0.5, 1}}},
		{{{0, 1}, {1, 1}, {1, 0.5}, {0, 0.5}, {0, 0}, {1, 0}}},
		{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}, {{0.2, 0.5}, {1, 0.5}}},
		{{{0, 1}, {0, 0.45}, {1, 0.45}}, {{0.75, 1}, {0.75, 0}}},
		{{{1, 1}, {0, 1}, {0, 0.5}, {1, 0.5}, {1, 0}, {0, 0}}},
		{{{1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 0.5}, {0, 0.5}}},
		{{{0, 1}, {1, 1}, {0.4, 0}}},
		{{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}}, {{0, 0.5}, {1, 0.5}}},
		{{{1, 0.5}, {0, 0.5}, {0, 1}, {1, 1}, {1, 0}, {0, 0}}},
		{{{0, 1}, {0, 0}, {1, 0}}},
		{{{1, 1}, {0, 1}, {0, 0}, {1, 0}}},
		{{{0, 0}, {0, 1}, {1, 1}, {1, 0.5}, {0, 0.5}}, {{0.35, 0.5}, {1, 0}}}
	};
	if (c >= '0' && c <= '9') return g[c - '0'];
	if (c == 'L') return g[10];
	if (c == 'C') return g[11];
	if (c == 'R') return g[12];
	return none;
}

// a runway's markings, laid out once (runway frame: t from end A along it, s to the left of A to B)
struct RwMarks {
	double L = 0, hw = 0, w = 0, clw = 0.45, sp = 8.8, aw = 3.7, al = 30.5;
	int nstripe = 4;
	bool prec = false, aim = false, side = false;
	double c0[2] = {0, 0};    // the centre line's start from each end
	bool cut[2] = {false, false};
	std::vector<Seg> glyph[2]; // runway frame
	double gt0[2] = {0, 0}, gt1[2] = {0, 0}; // their extent in t from that end
};

RwMarks Layout (const Runway &r)
{
	RwMarks m;
	m.L = std::hypot (r.bx - r.ax, r.by - r.ay);
	m.w = r.w;
	m.hw = 0.5 * r.w;
	m.nstripe = r.w >= 60.0 ? 16 : r.w >= 45.0 ? 12 : r.w >= 30.0 ? 8 : r.w >= 23.0 ? 6 : 4;
	if (r.w >= 45.0) { m.aw = 9.1; m.sp = 22.0; }
	else if (r.w >= 30.0) { m.aw = 6.1; m.sp = 14.6; }
	else if (r.w >= 23.0) { m.aw = 4.6; m.sp = 11.0; }
	m.prec = r.w >= 30.0 && m.L >= 1800.0;
	m.aim = r.w >= 23.0 && m.L >= 1280.0;
	m.al = m.L >= 1280.0 ? 45.7 : 30.5;
	m.side = r.w >= 30.0;
	m.clw = r.w >= 30.0 ? 0.9 : 0.45;
	m.cut[0] = r.cutA; m.cut[1] = r.cutB;
	const double gw = 6.1, gh = 18.3, st = 0.75, gap = 4.5;
	for (int e = 0; e < 2; e++) {
		const std::string &d = e ? r.desB : r.desA;
		std::string num, let;
		for (char c : d) (std::isdigit ((unsigned char)c) ? num : let) += c;
		if (let.size() > 1) let.resize (1);
		double t = 6.0 + 45.7 + 12.0;
		m.gt0[e] = t;
		auto put = [&] (char c, double sc, double t0) {
			for (const auto &line : Glyph (c))
				for (size_t i = 0; i + 1 < line.size(); i++) {
					double se[2], te[2];
					for (int k = 0; k < 2; k++) {
						se[k] = sc - (line[i + k].x - 0.5) * (gw - 2.0 * st);
						te[k] = t0 + st + line[i + k].y * (gh - 2.0 * st);
					}
					if (e) m.glyph[e].push_back ({-se[0], m.L - te[0], -se[1], m.L - te[1]});
					else m.glyph[e].push_back ({se[0], te[0], se[1], te[1]});
				}
		};
		if (!let.empty() && !num.empty()) { put (let[0], 0.0, t); t += gh + 6.0; }
		if (num.size() == 1) put (num[0], 0.0, t);
		else if (num.size() >= 2) { put (num[0], 0.5 * gw + 0.5 * gap, t); put (num[1], -(0.5 * gw + 0.5 * gap), t); }
		if (!num.empty()) t += gh;
		m.gt1[e] = t;
		m.c0[e] = m.cut[e] ? 0.0 : (num.empty() ? 6.0 + 45.7 + 12.0 : t + 12.0);
	}
	return m;
}

double SegDist (const Seg &g, double x, double y)
{
	double dx = g.bx - g.ax, dy = g.by - g.ay, l2 = dx * dx + dy * dy;
	double t = l2 > 0 ? std::max (0.0, std::min (1.0, ((x - g.ax) * dx + (y - g.ay) * dy) / l2)) : 0.0;
	return std::hypot (x - g.ax - t * dx, y - g.ay - t * dy);
}

// paint share of a runway's markings at (s, t)
double RunwayMark (const RwMarks &m, double s, double t, double pn, double pa)
{
	double v = 0.0;
	if (m.side) v = std::max (v, Cov1 (std::fabs (std::fabs (s) - (m.hw - 0.75)), 0.45, pn) * Cov1 (std::fabs (t - 0.5 * m.L), 0.5 * m.L, pa));
	double ca = m.c0[0], cb = m.L - m.c0[1];
	if (cb > ca && t > ca - pa && t < cb + pa)
		v = std::max (v, Cov1 (std::fabs (s), 0.5 * m.clw, pn) * DashCov (t - ca, pa, 36.5, 60.8) * Cov1 (std::fabs (t - 0.5 * (ca + cb)), 0.5 * (cb - ca), pa));
	for (int e = 0; e < 2; e++) {
		if (m.cut[e]) continue;
		double te = e ? m.L - t : t, se = e ? -s : s, as = std::fabs (se);
		if (te < -pa || te > 1000.0) continue;
		if (te < 51.7 + pa) {
			double ct = Cov1 (std::fabs (te - 28.85), 22.85, pa);
			for (int k = 0; k < m.nstripe / 2 && ct > 0.0; k++) v = std::max (v, ct * Cov1 (std::fabs (as - (2.625 + 3.5 * k)), 0.875, pn));
		}
		if (m.aim) v = std::max (v, Cov1 (std::fabs (te - (311.0 + 0.5 * m.al)), 0.5 * m.al, pa) * Cov1 (std::fabs (as - (0.5 * m.sp + 0.5 * m.aw)), 0.5 * m.aw, pn));
		if (m.prec) {
			static const double x[5] = {152.0, 457.0, 610.0, 762.0, 914.0};
			static const int nb[5] = {3, 2, 2, 1, 1};
			for (int g = 0; g < 5; g++) {
				if (x[g] + 22.9 > 0.5 * m.L - 270.0) break;
				double ct = Cov1 (std::fabs (te - (x[g] + 11.45)), 11.45, pa);
				for (int b = 0; b < nb[g] && ct > 0.0; b++) v = std::max (v, ct * Cov1 (std::fabs (as - (0.5 * m.sp + 3.3 * b + 0.9)), 0.9, pn));
			}
		}
		if (te > m.gt0[e] - 2.0 && te < m.gt1[e] + 2.0 && as < 20.0) {
			double d = 1e30;
			for (const Seg &g : m.glyph[e]) d = std::min (d, SegDist (g, s, t));
			v = std::max (v, Cov1 (d, 0.75, 0.5 * (pn + pa)));
		}
	}
	return std::min (1.0, v);
}

struct LineMark { double o, h; float lum, yel; bool dash, centre; };

std::vector<LineMark> RoadLines (const Shape &S, bool am)
{
	std::vector<LineMark> v;
	double hw = S.s.hw;
	bool one = (S.flags & (TSWAY_ONEWAY | TSWAY_RAMP)) != 0;
	float side = am ? Yellow : White, sy = am ? 1.0f : 0.0f;
	if (one) {
		int n = (S.flags & TSWAY_RAMP) ? 1 : std::max (1, (int)std::floor ((2.0 * hw - 1.8) / LaneW + 0.25));
		double shl = std::max (0.3, std::min (1.5, (2.0 * hw - n * LaneW) / 3.0));
		double sl = hw - shl, sr = std::max (sl - n * LaneW, -hw + 0.3);
		v.push_back ({sl, LineH, side, sy, false, false});
		v.push_back ({sr, LineH, White, 0.0f, false, false});
		for (int k = 1; k < n && sl - k * LaneW > sr + 1.0; k++) v.push_back ({sl - k * LaneW, LineH, White, 0.0f, true, false});
		return v;
	}
	int n = S.kind == TSWAY_MOTORWAY ? 3 : (S.kind == TSWAY_TRUNK || S.kind == TSWAY_PRIMARY) ? 2 : 1;
	n = std::min (n, std::max (1, (int)std::floor ((hw - 0.3) / LaneW2)));
	double e = std::min (n * LaneW2, hw - 0.2);
	v.push_back ({e, LineH, White, 0.0f, false, false});
	v.push_back ({-e, LineH, White, 0.0f, false, false});
	if (am && S.kind <= TSWAY_SECONDARY) {
		v.push_back ({0.15, LineH, Yellow, 1.0f, false, true});
		v.push_back ({-0.15, LineH, Yellow, 1.0f, false, true});
	} else v.push_back ({0.0, LineH, side, sy, !(S.kind <= TSWAY_TRUNK), true});
	for (int k = 1; k < n; k++) {
		v.push_back ({k * LaneW2, LineH, White, 0.0f, true, false});
		v.push_back ({-k * LaneW2, LineH, White, 0.0f, true, false});
	}
	return v;
}

struct Canvas {
	Frame f, fl;                // way and lamp textures
	std::vector<float> cov, lum, yel, wat, shr, lay, apr, mask;
	std::vector<float> pool, head, spill;
	std::vector<float> fc, fd, fs, fa, fpn, fpa;
	std::vector<uint8_t> in;
	std::vector<uint32_t> seen;
	std::vector<int> touched;
	uint32_t gen = 0;

	void Init (int n, int nl, double W, double H)
	{
		f.Set (n, W, H);
		fl.Set (nl, W, H);
		size_t N = (size_t)n * n, NL = (size_t)nl * nl;
		for (auto *v : {&cov, &lum, &yel, &wat, &shr, &lay, &apr, &mask}) v->assign (N, 0.0f);
		for (auto *v : {&pool, &head, &spill}) v->assign (NL, 0.0f);
		for (auto *v : {&fc, &fd, &fs, &fa, &fpn, &fpa}) v->assign (N, 0.0f);
		in.assign (N, 0);
		seen.assign (N, 0);
	}
	void Begin () { gen++; touched.clear(); }
	void Touch (int k)
	{
		if (seen[k] == gen) return;
		seen[k] = gen;
		fc[k] = 0.0f; fd[k] = 1e30f; in[k] = 0;
		touched.push_back (k);
	}

	// a line shape's share per texel, and at each the nearest segment's lateral offset, along distance and footprints
	void Line (const Shape &S, double hw, const Frame &F, bool clip = true)
	{
		Begin();
		double r = hw + 1.5 * std::max (F.px, F.py);
		double cx0 = clip ? S.cx0 : -1e30, cx1 = clip ? S.cx1 - 1e-7 : 1e30, cy0 = clip ? S.cy0 + 1e-7 : -1e30, cy1 = clip ? S.cy1 : 1e30;
		for (const auto &q : S.parts) {
			double s0 = 0.0;
			for (size_t k = 0; k + 1 < q.size(); k++) {
				const P &a = q[k], &b = q[k + 1];
				double dx = b.x - a.x, dy = b.y - a.y, L = std::hypot (dx, dy);
				if (L < 1e-6) continue;
				double ux = dx / L, uy = dy / L, nx = -uy, ny = ux;
				double pnI = std::fabs (nx) * F.px + std::fabs (ny) * F.py, paI = std::fabs (ux) * F.px + std::fabs (uy) * F.py;
				int i0 = F.I0 (std::max (std::min (a.x, b.x) - r, cx0)), i1 = F.I1 (std::min (std::max (a.x, b.x) + r, cx1));
				int j0 = F.J0 (std::min (std::max (a.y, b.y) + r, cy1)), j1 = F.J1 (std::max (std::min (a.y, b.y) - r, cy0));
				for (int j = j0; j <= j1; j++) {
					double y = F.Y (j);
					for (int i = i0; i <= i1; i++) {
						double x = F.X (i), rx = x - a.x, ry = y - a.y;
						double t = rx * ux + ry * uy, s = rx * nx + ry * ny;
						double tc = std::max (0.0, std::min (L, t)), qx = rx - ux * tc, qy = ry - uy * tc, d = std::hypot (qx, qy);
						if (d > r) continue;
						double pn = pnI;
						bool cap = t < 0.0 || t > L;
						if (cap && d > 1e-9) pn = std::fabs (qx / d) * F.px + std::fabs (qy / d) * F.py;
						int idx = j * F.n + i;
						Touch (idx);
						float c = (float)Cov1 (d, hw, pn);
						if (c > fc[idx]) fc[idx] = c;
						if (d < fd[idx]) {
							fd[idx] = (float)d;
							fs[idx] = (float)(cap ? (s >= 0.0 ? d : -d) : s);
							fa[idx] = (float)(s0 + tc);
							fpn[idx] = (float)pnI;
							fpa[idx] = (float)paI;
						}
					}
				}
				s0 += L;
			}
		}
	}

	// an area shape's share: rings filled by rows, edges by the box filter across them
	void Area (const Shape &S)
	{
		Begin();
		const Frame &F = f;
		double cx0 = S.cx0, cx1 = S.cx1 - 1e-7, cy0 = S.cy0 + 1e-7, cy1 = S.cy1;
		int j0 = F.J0 (std::min (S.y1, cy1)), j1 = F.J1 (std::max (S.y0, cy0));
		std::vector<double> xs;
		for (int j = j0; j <= j1; j++) {
			double y = F.Y (j);
			xs.clear();
			for (const auto &r : S.parts)
				for (size_t i = 0, k = r.size() - 1; i < r.size(); k = i++)
					if ((r[i].y > y) != (r[k].y > y)) xs.push_back (r[k].x + (y - r[k].y) * (r[i].x - r[k].x) / (r[i].y - r[k].y));
			std::sort (xs.begin(), xs.end());
			for (size_t m = 0; m + 1 < xs.size(); m += 2)
				for (int i = F.I0 (std::max (xs[m], cx0)), i1 = F.I1 (std::min (xs[m + 1], cx1)); i <= i1; i++) {
					int idx = j * F.n + i;
					Touch (idx);
					in[idx] = 1;
					fc[idx] = 1.0f;
				}
		}
		for (const auto &r : S.parts)
			for (size_t i = 0, k = r.size() - 1; i < r.size(); k = i++) {
				const P &a = r[k], &b = r[i];
				double dx = b.x - a.x, dy = b.y - a.y, L = std::hypot (dx, dy);
				if (L < 1e-6) continue;
				double ux = dx / L, uy = dy / L, pnI = std::fabs (uy) * F.px + std::fabs (ux) * F.py;
				int i0 = F.I0 (std::max (std::min (a.x, b.x) - pnI, cx0)), i1 = F.I1 (std::min (std::max (a.x, b.x) + pnI, cx1));
				int jj0 = F.J0 (std::min (std::max (a.y, b.y) + pnI, cy1)), jj1 = F.J1 (std::max (std::min (a.y, b.y) - pnI, cy0));
				for (int j = jj0; j <= jj1; j++) {
					double y = F.Y (j);
					for (int ii = i0; ii <= i1; ii++) {
						double x = F.X (ii), rx = x - a.x, ry = y - a.y;
						double tc = std::max (0.0, std::min (L, rx * ux + ry * uy)), d = std::hypot (rx - ux * tc, ry - uy * tc);
						if (d > pnI) continue;
						int idx = j * F.n + ii;
						Touch (idx);
						if (d < fd[idx]) { fd[idx] = (float)d; fpn[idx] = (float)pnI; }
					}
				}
			}
		for (int idx : touched)
			if (fd[idx] < 1e29f) fc[idx] = (float)std::max (0.0, std::min (1.0, in[idx] ? 0.5 + fd[idx] / fpn[idx] : 0.5 - fd[idx] / fpn[idx]));
	}

	void Surface (int k, float c, const Style &s, bool apron)
	{
		c *= 1.0f - mask[k];
		if (c <= 0.0f) return;
		float o = 1.0f - c;
		if (s.water) {
			wat[k] = wat[k] * o + c;
			cov[k] *= o; lum[k] *= o; yel[k] *= o; shr[k] *= o;
			return;
		}
		cov[k] = cov[k] * o + c;
		lum[k] = lum[k] * o + c * s.lum;
		yel[k] *= o;
		shr[k] = shr[k] * o + c * s.share;
		wat[k] *= o;
		lay[k] += c;
		if (apron) apr[k] += c;
	}
	void Paint (int k, float m, float l, float y)
	{
		m *= 1.0f - mask[k];
		if (m <= 0.0f) return;
		float o = 1.0f - m;
		lum[k] = lum[k] * o + m * cov[k] * l;
		yel[k] = yel[k] * o + m * cov[k] * y;
		shr[k] *= o; // paint takes none of the imagery
	}
};

void Gauss (std::vector<float> &buf, const Frame &F, double x, double y, double s, float w)
{
	double s2 = s * s + (F.px * F.px + F.py * F.py) / 12.0, k = w * s * s / s2, r = 3.0 * std::sqrt (s2);
	for (int j = F.J0 (y + r), j1 = F.J1 (y - r); j <= j1; j++) {
		double dy = F.Y (j) - y;
		for (int i = F.I0 (x - r), i1 = F.I1 (x + r); i <= i1; i++) {
			double dx = F.X (i) - x;
			buf[j * F.n + i] += (float)(k * std::exp (-(dx * dx + dy * dy) / s2));
		}
	}
}

std::vector<std::vector<uint8_t>> Mips (int n, const std::vector<float> &c0, const std::vector<float> &c1, const std::vector<float> &c2, const std::vector<float> &c3, bool lamp)
{
	std::vector<std::vector<uint8_t>> out;
	std::vector<float> a[4] = {c0, c1, c2, c3};
	for (int m = n; m >= 1; m /= 2) {
		std::vector<uint8_t> px ((size_t)m * m * 4);
		for (int k = 0; k < m * m; k++)
			for (int c = 0; c < 4; c++) {
				double v = a[c][k];
				if (lamp && c < 3) v = std::sqrt (std::max (0.0, v) / 4.0);
				px[4 * k + c] = (uint8_t)std::lround (std::max (0.0, std::min (1.0, v)) * 255.0);
			}
		out.push_back (std::move (px));
		if (m == 1) break;
		int h = m / 2;
		for (int c = 0; c < 4; c++) {
			std::vector<float> b ((size_t)h * h);
			for (int j = 0; j < h; j++)
				for (int i = 0; i < h; i++)
					b[j * h + i] = 0.25f * (a[c][2 * j * m + 2 * i] + a[c][2 * j * m + 2 * i + 1] + a[c][(2 * j + 1) * m + 2 * i] + a[c][(2 * j + 1) * m + 2 * i + 1]);
			a[c].swap (b);
		}
	}
	return out;
}

} // namespace

double Tile::Lng (double u) const
{
	return (x + u / 4096.0) / double(1 << z) * 2.0 * Pi - Pi;
}

double Tile::Lat (double v) const
{
	return std::atan (std::sinh (Pi * (1.0 - 2.0 * (y + v / 4096.0) / double(1 << z))));
}

std::shared_ptr<const Tile> FromApi (const TSWAYTILE &t)
{
	auto T = std::make_shared<Tile>();
	T->z = t.z; T->x = t.x; T->y = t.y;
	T->f.reserve (t.nf);
	for (DWORD i = 0; i < t.nf; i++) {
		const TSWAY &w = t.f[i];
		Tile::F F;
		F.kind = w.kind; F.flags = w.flags; F.layer = w.layer;
		F.ref.assign (w.ref, strnlen (w.ref, sizeof w.ref));
		F.p0 = w.p0; F.np = w.np;
		if ((uint64_t)w.p0 + w.np > t.np) continue;
		T->f.push_back (F);
	}
	T->part.assign (t.part, t.part + t.np + 1);
	T->pt.assign (t.pt, t.pt + 2 * (size_t)t.npt);
	for (uint32_t &p : T->part) p = std::min<uint32_t> (p, t.npt);
	return T;
}

Style StyleOf (uint8_t kind, uint8_t flags)
{
	Style s;
	bool one = (flags & TSWAY_ONEWAY) != 0;
	auto road = [&] (double hw1, double hw2, int order, bool marks, double lamp) {
		s.hw = one ? hw1 : hw2; s.lum = Asphalt; s.share = 0.2f; s.order = order;
		s.marks = marks ? M_ROAD : M_NONE; s.lit = lamp > 0.0; s.lamp = lamp;
	};
	switch (kind) {
	case TSWAY_MOTORWAY:   road (5.75, 12.0, 14, true, 45.0); break;
	case TSWAY_TRUNK:      road (5.0, 8.0, 13, true, 40.0); break;
	case TSWAY_PRIMARY:    road (4.5, 7.5, 12, true, 35.0); break;
	case TSWAY_SECONDARY:  road (3.75, 4.5, 11, true, 35.0); break;
	case TSWAY_TERTIARY:   road (3.25, 3.75, 10, true, 35.0); break;
	case TSWAY_MINOR:      road (2.25, 3.25, 9, false, 35.0); break;
	case TSWAY_SERVICE:    road (2.0, 2.0, 8, false, 35.0); break;
	case TSWAY_PARKING:    road (2.75, 2.75, 8, false, 30.0); break;
	case TSWAY_DRIVEWAY:   road (1.5, 1.5, 8, false, 0.0); break;
	case TSWAY_BUSWAY:     road (3.5, 3.5, 9, false, 35.0); break;
	case TSWAY_RACEWAY:    road (6.0, 6.0, 9, false, 0.0); break;
	case TSWAY_TRACK:      s.hw = 1.5; s.lum = Dirt; s.share = 0.7f; s.order = 7; break;
	case TSWAY_FOOTWAY:    s.hw = 1.0; s.lum = Walk; s.share = 0.3f; s.order = 6; break;
	case TSWAY_CYCLEWAY:   s.hw = 1.25; s.lum = Asphalt; s.share = 0.25f; s.order = 6; break;
	case TSWAY_PATH:       s.hw = 0.75; s.lum = Dirt; s.share = 0.7f; s.order = 6; break;
	case TSWAY_STEPS:      s.hw = 1.0; s.lum = Walk; s.share = 0.3f; s.order = 6; break;
	case TSWAY_PEDESTRIAN: s.hw = 2.5; s.lum = Walk; s.share = 0.3f; s.order = 6; s.lit = true; s.lamp = 30.0; break;
	case TSWAY_PLATFORM:   s.hw = 2.0; s.lum = Concrete; s.share = 0.3f; s.order = 6; break;
	case TSWAY_PIER:       s.hw = 2.0; s.lum = Deck; s.share = 0.3f; s.order = 6; break;
	case TSWAY_RAIL:       s.hw = 1.7; s.lum = Ballast; s.share = 0.3f; s.order = 15; s.marks = M_RAIL; break;
	case TSWAY_TRAM:       s.hw = 0.0; s.order = 16; s.marks = M_RAIL; break;
	case TSWAY_RUNWAY:     s.hw = 22.5; s.lum = Concrete; s.share = 0.25f; s.order = 5; s.marks = M_RUNWAY; break;
	case TSWAY_TAXIWAY:    s.hw = 11.5; s.lum = Concrete; s.share = 0.25f; s.order = 3; s.marks = M_TAXIWAY; break;
	case TSWAY_APRON:      s.lum = Concrete; s.share = 0.3f; s.order = 1; break;
	case TSWAY_HELIPAD:    s.lum = Concrete; s.share = 0.25f; s.order = 1; break;
	case TSWAY_RIVER:      s.hw = 7.5; s.water = true; break;
	case TSWAY_CANAL:      s.hw = 6.0; s.water = true; break;
	case TSWAY_STREAM:     s.hw = 1.5; s.water = true; break;
	case TSWAY_DRAIN:      s.hw = 1.25; s.water = true; break;
	case TSWAY_DITCH:      s.hw = 0.75; s.water = true; break;
	default: break;
	}
	if ((flags & TSWAY_RAMP) && kind >= TSWAY_MOTORWAY && kind <= TSWAY_TERTIARY) s.hw = std::min (s.hw, 3.25);
	if ((flags & TSWAY_UNPAVED) && !s.water && kind != TSWAY_RAIL && kind != TSWAY_TRAM && s.lum > 0.0f) {
		s.lum = Dirt; s.share = 0.7f; s.marks = M_NONE; s.lit = false;
	}
	if (flags & TSWAY_AREA) {
		s.marks = M_NONE;
		s.lit = false;
		if (kind == TSWAY_TAXIWAY) s.order = 2;
		if (kind == TSWAY_RUNWAY) s.order = 4;
	}
	return s;
}

double Cov1 (double d, double h, double p)
{
	if (h <= 0.0) return 0.0;
	if (p <= 1e-9) return d <= h ? 1.0 : 0.0;
	double lo = std::max (d - 0.5 * p, -h), hi = std::min (d + 0.5 * p, h);
	return hi > lo ? (hi - lo) / p : 0.0;
}

double DashCov (double a, double p, double L, double P)
{
	auto F = [L, P] (double x) { double q = std::floor (x / P); return q * L + std::min (x - q * P, L); }; // dash length up to x
	if (p <= 1e-9) { double r = a - std::floor (a / P) * P; return r < L ? 1.0 : 0.0; }
	return std::max (0.0, std::min (1.0, (F (a + 0.5 * p) - F (a - 0.5 * p)) / p));
}

std::string Designator (const std::string &ref, double bearing)
{
	std::vector<std::string> half;
	size_t at = 0;
	for (;;) {
		size_t sl = ref.find ('/', at);
		std::string h = ref.substr (at, sl == std::string::npos ? std::string::npos : sl - at);
		h.erase (std::remove_if (h.begin(), h.end(), [] (char c) { return std::isspace ((unsigned char)c); }), h.end());
		if (!h.empty()) half.push_back (h);
		if (sl == std::string::npos) break;
		at = sl + 1;
	}
	auto num = [] (const std::string &h) { int n = 0, d = 0; for (char c : h) { if (!std::isdigit ((unsigned char)c) || d == 2) break; n = n * 10 + (c - '0'); d++; } return d ? n : -1; };
	auto diff = [] (double a, double b) { double d = std::fmod (std::fabs (a - b), 360.0); return d > 180.0 ? 360.0 - d : d; };
	std::string best;
	double bd = 1e9;
	for (const std::string &h : half) {
		int n = num (h);
		if (n < 1 || n > 36) continue;
		double d = diff (n * 10.0, bearing);
		if (d < bd) { bd = d; best = h; }
	}
	if (best.empty()) return std::string();
	if (bd <= 90.0) return best;
	int n = num (best), r = (n + 17) % 36 + 1; // the reciprocal of the only half given
	std::string out = (best.size() >= 2 && best[0] == '0' && r < 10 ? "0" : "") + std::to_string (r);
	for (char c : best) if (c == 'L') out += 'R'; else if (c == 'R') out += 'L'; else if (c == 'C') out += 'C';
	return out;
}

std::vector<std::pair<int, int>> RunwayTiles (const std::vector<std::shared_ptr<const Tile>> &tiles)
{
	std::vector<std::pair<int, int>> out;
	if (tiles.empty()) return out;
	const Tile &T0 = *tiles[0];
	Geo g {T0.Lat (2048), T0.Lng (2048), 6371010.0, 0.0};
	g.kx = g.R * std::cos (g.latc);
	std::vector<Shape> sh;
	std::vector<Runway> rw;
	std::set<std::pair<int, int>> have;
	for (const auto &t : tiles) {
		have.insert ({t->x, t->y});
		Shapes (*t, g, sh, &rw);
	}
	Join (rw);
	int n = 1 << 14;
	for (const Runway &r : rw)
		for (int e = 0; e < 2; e++) {
			if (!(e ? r.cutB : r.cutA)) continue;
			double dx = e ? r.bx - r.ax : r.ax - r.bx, dy = e ? r.by - r.ay : r.ay - r.by, L = std::hypot (dx, dy);
			if (L < 1.0) continue;
			P p {(e ? r.bx : r.ax) + dx / L * 100.0, (e ? r.by : r.ay) + dy / L * 100.0};
			double lat, lng;
			g.LatLng (p, lat, lng);
			lat = std::max (-1.4844, std::min (1.4844, lat));
			int x = (int)std::floor ((lng / (2.0 * Pi) + 0.5) * n), y = (int)std::floor ((1.0 - std::asinh (std::tan (lat)) / Pi) * 0.5 * n);
			x = ((x % n) + n) % n;
			y = std::max (0, std::min (n - 1, y));
			if (have.insert ({x, y}).second) out.push_back ({x, y});
		}
	return out;
}

void Raster (const Input &in, Output &out)
{
	out = Output();
	Geo g {0.5 * (in.lat0 + in.lat1), 0.5 * (in.lng0 + in.lng1), in.R, 0.0};
	g.kx = in.R * std::cos (g.latc);
	double W = (in.lng1 - in.lng0) * g.kx, H = (in.lat1 - in.lat0) * in.R;
	if (!(W > 0.0) || !(H > 0.0) || in.n < 2 || in.nl < 2 || in.n % in.nl) return;
	Canvas C;
	C.Init (in.n, in.nl, W, H);
	const Frame &F = C.f;

	if (in.blocked) { // the base mask, by cells, bilinear on the texels
		std::vector<float> M ((size_t)MaskN * MaskN);
		bool anyM = false;
		for (int j = 0; j < MaskN; j++)
			for (int i = 0; i < MaskN; i++) {
				double lat = in.lat1 - (j + 0.5) / MaskN * (in.lat1 - in.lat0), lng = in.lng0 + (i + 0.5) / MaskN * (in.lng1 - in.lng0);
				anyM |= (M[j * MaskN + i] = in.blocked (lat, lng) ? 1.0f : 0.0f) > 0.0f;
			}
		if (anyM)
			for (int j = 0; j < F.n; j++)
				for (int i = 0; i < F.n; i++) {
					double u = (i + 0.5) / F.n * MaskN - 0.5, v = (j + 0.5) / F.n * MaskN - 0.5;
					int i0 = std::max (0, std::min (MaskN - 1, (int)std::floor (u))), j0 = std::max (0, std::min (MaskN - 1, (int)std::floor (v)));
					int i1 = std::min (MaskN - 1, i0 + 1), j1 = std::min (MaskN - 1, j0 + 1);
					double fu = std::max (0.0, std::min (1.0, u - i0)), fv = std::max (0.0, std::min (1.0, v - j0));
					C.mask[j * F.n + i] = (float)((M[j0 * MaskN + i0] * (1 - fu) + M[j0 * MaskN + i1] * fu) * (1 - fv) + (M[j1 * MaskN + i0] * (1 - fu) + M[j1 * MaskN + i1] * fu) * fv);
				}
	}

	std::vector<Shape> sh;
	std::vector<Runway> rw;
	for (const auto &t : in.tiles) if (t) Shapes (*t, g, sh, &rw);
	auto blockedAt = [&] (const P &p) {
		double lat, lng;
		g.LatLng (p, lat, lng);
		return in.blocked && in.blocked (lat, lng);
	};
	auto aeroBlocked = [&] (const std::vector<std::vector<P>> &parts) { // a base draws this airport part
		for (const auto &q : parts)
			for (size_t k = 0; k < q.size(); k++) {
				if (blockedAt (q[k])) return true;
				if (k + 1 < q.size() && blockedAt ({0.5 * (q[k].x + q[k + 1].x), 0.5 * (q[k].y + q[k + 1].y)})) return true;
			}
		return false;
	};
	if (in.blocked) {
		sh.erase (std::remove_if (sh.begin(), sh.end(), [&] (const Shape &s) { return Aero (s.kind) && aeroBlocked (s.parts); }), sh.end());
		rw.erase (std::remove_if (rw.begin(), rw.end(), [&] (const Runway &r) { return aeroBlocked ({{{r.ax, r.ay}, {r.bx, r.by}}}); }), rw.end());
	}
	Join (rw);
	double maxW = 0.0;
	for (Runway &r : rw) {
		r.w = RunwayWidth (r, sh);
		maxW = std::max (maxW, r.w);
		double brg = std::atan2 (r.bx - r.ax, r.by - r.ay) * 180.0 / Pi;
		r.desA = Designator (r.ref, brg < 0 ? brg + 360.0 : brg);
		double brb = brg + 180.0;
		r.desB = Designator (r.ref, brb >= 360.0 ? brb - 360.0 : brb);
	}
	for (const Runway &r : rw) {
		Shape S;
		S.kind = TSWAY_RUNWAY;
		S.s = StyleOf (TSWAY_RUNWAY, 0);
		S.s.hw = 0.5 * r.w;
		S.rw = &r;
		S.parts.push_back ({{r.ax, r.ay}, {r.bx, r.by}});
		S.x0 = std::min (r.ax, r.bx); S.x1 = std::max (r.ax, r.bx);
		S.y0 = std::min (r.ay, r.by); S.y1 = std::max (r.ay, r.by);
		sh.push_back (std::move (S));
	}
	if (maxW > 0.0 && maxW < 30.0)
		for (Shape &s : sh) if (s.kind == TSWAY_TAXIWAY && !s.area) s.s.hw = 7.5;

	double mg = 60.0; // the widest half width and its markings
	sh.erase (std::remove_if (sh.begin(), sh.end(), [&] (const Shape &s) {
		return s.x1 + mg < -0.5 * W || s.x0 - mg > 0.5 * W || s.y1 + mg < -0.5 * H || s.y0 - mg > 0.5 * H || (s.s.hw <= 0.0 && !s.area && !s.s.marks);
	}), sh.end());
	std::stable_sort (sh.begin(), sh.end(), [] (const Shape &a, const Shape &b) { return a.layer != b.layer ? a.layer < b.layer : a.s.order < b.s.order; });

	for (size_t s0 = 0; s0 < sh.size(); ) {
		size_t s1 = s0;
		while (s1 < sh.size() && sh[s1].layer == sh[s0].layer) s1++;
		std::fill (C.lay.begin(), C.lay.end(), 0.0f);
		std::fill (C.apr.begin(), C.apr.end(), 0.0f);
		for (size_t i = s0; i < s1; i++) { // surfaces, then the runways' markings over them
			const Shape &S = sh[i];
			if (S.rw) {
				const Runway &r = *S.rw;
				RwMarks m = Layout (r);
				double L = m.L;
				if (L < 1.0) continue;
				double ux = (r.bx - r.ax) / L, uy = (r.by - r.ay) / L, nx = -uy, ny = ux;
				double pn = std::fabs (nx) * F.px + std::fabs (ny) * F.py, pa = std::fabs (ux) * F.px + std::fabs (uy) * F.py, e = m.hw + 2.0 * std::max (F.px, F.py);
				double xs[4] = {r.ax + nx * e, r.ax - nx * e, r.bx + nx * e, r.bx - nx * e}, ys[4] = {r.ay + ny * e, r.ay - ny * e, r.by + ny * e, r.by - ny * e};
				double bx0 = *std::min_element (xs, xs + 4) - e, bx1 = *std::max_element (xs, xs + 4) + e, by0 = *std::min_element (ys, ys + 4) - e, by1 = *std::max_element (ys, ys + 4) + e;
				for (int j = F.J0 (by1), j1 = F.J1 (by0); j <= j1; j++) {
					double y = F.Y (j);
					for (int ii = F.I0 (bx0), i1 = F.I1 (bx1); ii <= i1; ii++) {
						double rx = F.X (ii) - r.ax, ry = y - r.ay, t = rx * ux + ry * uy, s = rx * nx + ry * ny;
						if (std::fabs (s) > m.hw + pn || t < -pa || t > L + pa) continue;
						int k = j * F.n + ii;
						float c = (float)(Cov1 (std::fabs (s), m.hw, pn) * Cov1 (std::fabs (t - 0.5 * L), 0.5 * L, pa));
						C.Surface (k, c, S.s, false);
						float v = (float)RunwayMark (m, s, t, pn, pa);
						if (v > 0.0f) C.Paint (k, v * std::min (1.0f, c), White, 0.0f);
					}
				}
				continue;
			}
			if (S.area) C.Area (S);
			else if (S.s.hw > 0.0) C.Line (S, S.s.hw, F);
			else continue;
			for (int k : C.touched) if (C.fc[k] > 0.0f) C.Surface (k, C.fc[k], S.s, S.kind == TSWAY_APRON);
		}
		for (size_t i = s0; i < s1; i++) { // the layer's markings, left out where another surface of the layer covers them
			const Shape &S = sh[i];
			if (S.rw || S.area || !S.s.marks) continue;
			if (S.s.marks == M_RAIL) {
				double hw = S.kind == TSWAY_TRAM ? 1.0 : S.s.hw;
				C.Line (S, hw, F);
				for (int k : C.touched) {
					double s = C.fs[k], a = C.fa[k], pn = C.fpn[k], pa = C.fpa[k];
					if (S.kind == TSWAY_RAIL) C.Paint (k, (float)(Cov1 (std::fabs (s), 1.3, pn) * DashCov (a, pa, 0.25, 0.6)), Sleeper, 0.0f);
					C.Paint (k, (float)Cov1 (std::fabs (std::fabs (s) - 0.7175), 0.035, pn), Steel, 0.0f);
				}
				continue;
			}
			std::vector<LineMark> lines;
			if (S.s.marks == M_ROAD) lines = RoadLines (S, in.americas);
			else if (S.s.marks == M_TAXIWAY) {
				double hw = S.s.hw;
				lines = {{0.0, LineH, Yellow, 1.0f, false, true},
					{hw - 0.3, LineH, Yellow, 1.0f, false, false}, {hw - 0.6, LineH, Yellow, 1.0f, false, false},
					{-(hw - 0.3), LineH, Yellow, 1.0f, false, false}, {-(hw - 0.6), LineH, Yellow, 1.0f, false, false}};
			}
			if (lines.empty()) continue;
			double dl = in.americas ? 3.0 : 6.0, dp = in.americas ? 12.0 : 18.0;
			C.Line (S, S.s.hw, F);
			for (int k : C.touched) {
				float own = C.fc[k];
				if (own < 0.02f) continue;
				double s = C.fs[k], a = C.fa[k], pn = C.fpn[k], pa = C.fpa[k];
				float others = std::max (0.0f, C.lay[k] - own);
				for (const LineMark &l : lines) {
					float ot = S.s.marks == M_TAXIWAY && l.centre ? std::max (0.0f, others - C.apr[k]) : others;
					float allow = std::max (0.0f, std::min (1.0f, 1.0f - 2.0f * ot));
					if (allow <= 0.0f) continue;
					double m = Cov1 (std::fabs (s - l.o), l.h, pn) * (l.dash ? DashCov (a, pa, dl, dp) : 1.0);
					if (m > 0.0) C.Paint (k, (float)(m * allow * std::min (1.0f, own)), l.lum, l.yel);
				}
			}
		}
		s0 = s1;
	}

	// night: lamps along the lit roads, their pools, heads and the spill on the street
	const Frame &FL = C.fl;
	for (const Shape &S : sh) {
		if (!S.s.lit || S.area || S.rw || S.s.hw <= 0.0) continue;
		C.Line (S, S.s.hw + 3.0, FL);
		for (int k : C.touched) C.spill[k] = std::max (C.spill[k], C.fc[k]);
		bool one = (S.flags & (TSWAY_ONEWAY | TSWAY_RAMP)) != 0, major = S.kind <= TSWAY_TERTIARY;
		double arm = std::max (0.0, S.s.hw - 1.5);
		for (const auto &q : S.parts) {
			double next = 0.5 * S.s.lamp, s0 = 0.0;
			int n = 0;
			for (size_t k = 0; k + 1 < q.size(); k++) {
				const P &a = q[k], &b = q[k + 1];
				double L = std::hypot (b.x - a.x, b.y - a.y);
				if (L < 1e-6) continue;
				double ux = (b.x - a.x) / L, uy = (b.y - a.y) / L;
				for (; next <= s0 + L; next += S.s.lamp, n++) {
					double side = one || !major ? -1.0 : (n & 1 ? 1.0 : -1.0), t = next - s0;
					P p {a.x + ux * t - uy * side * arm, a.y + uy * t + ux * side * arm};
					if (p.x < S.cx0 || p.x >= S.cx1 || p.y <= S.cy0 || p.y > S.cy1) continue; // its tile's lamp
					if (p.x < -0.5 * W - 30.0 || p.x > 0.5 * W + 30.0 || p.y < -0.5 * H - 30.0 || p.y > 0.5 * H + 30.0) continue;
					if (in.blocked && blockedAt (p)) continue;
					Gauss (C.pool, FL, p.x, p.y, PoolS, 1.0f);
					Gauss (C.head, FL, p.x, p.y, HeadS, 1.0f);
				}
				s0 += L;
			}
		}
	}

	int r = in.n / in.nl;
	std::vector<float> share ((size_t)in.nl * in.nl, 0.25f);
	for (int j = 0; j < in.nl; j++)
		for (int i = 0; i < in.nl; i++) {
			double sc = 0.0, ss = 0.0;
			for (int b = 0; b < r; b++)
				for (int a = 0; a < r; a++) {
					int k = (j * r + b) * in.n + i * r + a;
					sc += C.cov[k];
					ss += C.shr[k];
				}
			if (sc > 1e-3) share[j * in.nl + i] = (float)(ss / sc);
		}
	for (size_t k = 0; k < C.cov.size() && !out.any; k++) out.any = C.cov[k] > 0.5f / 255.0f || C.wat[k] > 0.5f / 255.0f;
	for (size_t k = 0; k < C.pool.size() && !out.any; k++) out.any = C.pool[k] > 1e-3f;
	if (!out.any) return;
	out.way = Mips (in.n, C.cov, C.lum, C.yel, C.wat, false);
	out.lamp = Mips (in.nl, C.pool, C.head, C.spill, share, true);
}

void Index::Build (const std::vector<std::shared_ptr<const Tile>> &tiles, double lat0, double lat1, double lng0, double lng1, double R)
{
	segs.clear(); polys.clear(); cells.clear();
	latc = 0.5 * (lat0 + lat1); lngc = 0.5 * (lng0 + lng1);
	kx = R * std::cos (latc); ky = R;
	double W = (lng1 - lng0) * kx + 100.0, H = (lat1 - lat0) * R + 100.0;
	x0 = -0.5 * W; y0 = -0.5 * H;
	nx = std::max (1, (int)std::ceil (W / Cell)); ny = std::max (1, (int)std::ceil (H / Cell));
	cells.assign ((size_t)nx * ny, {});
	Geo g {latc, lngc, R, kx};
	std::vector<Shape> sh;
	for (const auto &t : tiles) if (t) Shapes (*t, g, sh, nullptr);
	auto add = [&] (double ax, double ay, double bx, double by, double r, uint32_t id) {
		int i0 = std::max (0, (int)std::floor ((std::min (ax, bx) - r - x0) / Cell)), i1 = std::min (nx - 1, (int)std::floor ((std::max (ax, bx) + r - x0) / Cell));
		int j0 = std::max (0, (int)std::floor ((std::min (ay, by) - r - y0) / Cell)), j1 = std::min (ny - 1, (int)std::floor ((std::max (ay, by) + r - y0) / Cell));
		for (int j = j0; j <= j1; j++)
			for (int i = i0; i <= i1; i++)
				if (cells[j * nx + i].empty() || cells[j * nx + i].back() != id) cells[j * nx + i].push_back (id);
	};
	std::vector<uint32_t> pid;
	for (const Shape &S : sh) {
		double hw = S.kind == TSWAY_TRAM ? 1.25 : S.s.hw;
		if (S.area) {
			Poly p {{}, S.x0, S.y0, S.x1, S.y1};
			for (const auto &q : S.parts) {
				std::vector<double> r;
				for (const P &v : q) { r.push_back (v.x); r.push_back (v.y); }
				p.rings.push_back (std::move (r));
			}
			polys.push_back (std::move (p));
			pid.push_back ((uint32_t)polys.size() - 1);
			continue;
		}
		if (hw <= 0.0) continue;
		for (const auto &q : S.parts)
			for (size_t k = 0; k + 1 < q.size(); k++) {
				segs.push_back ({q[k].x, q[k].y, q[k + 1].x, q[k + 1].y, hw});
				add (q[k].x, q[k].y, q[k + 1].x, q[k + 1].y, hw + 10.0, (uint32_t)segs.size() - 1);
			}
	}
	for (uint32_t p : pid) {
		const Poly &q = polys[p];
		add (q.x0, q.y0, q.x1, q.y1, 10.0, (uint32_t)segs.size() + p);
	}
}

bool Index::Covers (double lat, double lng, double margin) const
{
	if (cells.empty()) return false;
	double d = lng - lngc;
	d -= 2.0 * Pi * std::floor ((d + Pi) / (2.0 * Pi));
	double x = d * kx, y = (lat - latc) * ky;
	int i = (int)std::floor ((x - x0) / Cell), j = (int)std::floor ((y - y0) / Cell);
	if (i < 0 || j < 0 || i >= nx || j >= ny) return false;
	for (uint32_t id : cells[j * nx + i]) {
		if (id < segs.size()) {
			const Seg &s = segs[id];
			if (SegDist ({s.ax, s.ay, s.bx, s.by}, x, y) <= s.hw + margin) return true;
			continue;
		}
		const Poly &p = polys[id - segs.size()];
		if (x < p.x0 - margin || x > p.x1 + margin || y < p.y0 - margin || y > p.y1 + margin) continue;
		bool inside = false;
		double dm = 1e30;
		for (const auto &r : p.rings) {
			size_t n = r.size() / 2;
			for (size_t a = 0, b = n - 1; a < n; b = a++) {
				double ax = r[2 * a], ay = r[2 * a + 1], bx = r[2 * b], by = r[2 * b + 1];
				if ((ay > y) != (by > y) && x < bx + (y - by) * (ax - bx) / (ay - by)) inside = !inside;
				dm = std::min (dm, SegDist ({ax, ay, bx, by}, x, y));
			}
		}
		if (inside || dm <= margin) return true;
	}
	return false;
}

} // namespace WayGeom
