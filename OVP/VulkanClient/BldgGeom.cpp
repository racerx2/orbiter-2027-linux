// not upstream: see BldgGeom.h

#include "BldgGeom.h"
#include <algorithm>
#include <cmath>

namespace BldgGeom {

const Style Styles[NFacade] = {
	{3.2f, 3.4f, 4.0f}, // 0 red brick, shops
	{3.1f, 3.2f, 3.6f}, // 1 brown brick
	{3.2f, 3.4f, 4.0f}, // 2 yellow brick, shops
	{3.0f, 3.2f, 3.0f}, // 3 white render
	{3.0f, 3.2f, 3.0f}, // 4 ochre render
	{2.8f, 3.0f, 2.8f}, // 5 grey concrete panels
	{2.9f, 3.3f, 3.2f}, // 6 beige concrete
	{3.4f, 3.0f, 4.5f}, // 7 prewar stone
	{3.8f, 3.0f, 5.0f}, // 8 blue-green glass
	{3.8f, 3.0f, 5.0f}, // 9 dark glass
	{3.7f, 3.6f, 4.2f}, // 10 ribbon windows
	{3.6f, 3.4f, 4.5f}, // 11 dark metal
	{2.8f, 3.4f, 2.8f}, // 12 house siding
	{5.0f, 6.0f, 5.0f}, // 13 industrial
};

int PickStyle (double h, double area, uint32_t hash)
{
	static const int house[] = {12, 3, 4, 0}, ind[] = {13, 6}, tower[] = {8, 9, 10, 11, 7}, mid[] = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11};
	if (h <= 9.0 && area <= 400.0) return house[hash % 4];
	if (h <= 14.0 && area >= 2000.0) return ind[hash % 2];
	if (h > 45.0) return tower[hash % 5];
	return mid[hash % 10];
}

namespace {

struct P { double x, z; };
double Len (P a, P b) { return std::hypot (b.x - a.x, b.z - a.z); }
P Lerp (P a, P b, double t) { return {a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t}; }
float U (int reg, double px) { return (float)(((reg % 4) * Region + px) / Atlas); }
float Vt (int reg, double px) { return (float)(((reg / 4) * Region + px) / Atlas); }

// a b c turned so that cross(b-a, c-a) points along n: the .bld's front faces
void Tri (std::vector<uint32_t> &ix, const std::vector<V> &vs, uint32_t a, uint32_t b, uint32_t c, double nx, double ny, double nz)
{
	const V &A = vs[a], &B = vs[b], &C = vs[c];
	double e1[3] = {B.x - A.x, B.y - A.y, B.z - A.z}, e2[3] = {C.x - A.x, C.y - A.y, C.z - A.z};
	double cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2], cz = e1[0] * e2[1] - e1[1] * e2[0];
	if (cx * nx + cy * ny + cz * nz < 0) std::swap (b, c);
	ix.insert (ix.end(), {a, b, c});
}

struct Wall { P p0, p1; double bot, top, nx, nz; };

enum Mode { Full, Simple, Far }; // Simple: one band a face; Far: one band a face, its floors as the height holds

// one wall face from u0 to u1 bays: a plinth below ground, the ground floor, chunks of up to 6 floors; cut at every 8 bays
void Face (const Style &st, int reg, const Wall &w, double u0, double u1, Mode mode, Out &o)
{
	double top = w.top;
	struct B { double y0, y1, v0, v1; }; // heights and their pixel rows in the region
	std::vector<B> bands;
	if (mode == Simple) bands.push_back ({w.bot, top, UpperY + UpperFloors * FloorPx, (double)UpperY});
	else if (mode == Far) {
		int F = std::clamp ((int)std::lround ((top - std::max (w.bot, 0.0)) / st.floorH), 1, UpperFloors);
		bands.push_back ({w.bot, top, UpperY + UpperFloors * FloorPx, (double)UpperY + (UpperFloors - F) * FloorPx});
	} else {
		if (w.bot < -0.01) bands.push_back ({w.bot, std::min (0.0, top), (double)PlainY + PlainPx, (double)PlainY});
		double y0 = std::max (w.bot, 0.0), ys = y0;
		if (top - y0 > 0.05 && y0 < 0.5) {
			ys = top - y0 < 1.5 * st.groundH ? top : y0 + st.groundH;
			bands.push_back ({y0, ys, (double)GroundY + GroundPx, (double)GroundY});
		}
		if (top - ys > 0.05 && top - ys < 0.6 * st.floorH) bands.push_back ({ys, top, (double)PlainY + PlainPx, (double)PlainY});
		else if (top - ys > 0.05) {
			int F = std::max (1, (int)std::lround ((top - ys) / st.floorH));
			double fh = (top - ys) / F;
			for (int k = 0; k < F; ) {
				int n = std::min (UpperFloors, F - k);
				bands.push_back ({ys + k * fh, ys + (k + n) * fh, (double)UpperY + UpperFloors * FloorPx, (double)UpperY + (UpperFloors - n) * FloorPx});
				k += n;
			}
		}
	}
	double L = u1 - u0;
	if (!(L > 0)) return;
	for (double a = u0; a < u1 - 1e-9; ) {
		double base = std::floor (a / Bays) * Bays, b = std::min (u1, base + Bays);
		if (b - a < 1e-9) { a = base + Bays; continue; }
		double ua = Gutter + std::clamp (a - base, 0.0, (double)Bays) * BayPx, ub = Gutter + std::clamp (b - base, 0.0, (double)Bays) * BayPx;
		P qa = Lerp (w.p0, w.p1, (a - u0) / L), qb = Lerp (w.p0, w.p1, (b - u0) / L);
		for (const B &bd : bands) {
			if (bd.y1 - bd.y0 < 1e-3) continue;
			uint32_t i0 = (uint32_t)o.fv.size();
			float nx = (float)w.nx, nz = (float)w.nz;
			o.fv.push_back ({(float)qa.x, (float)bd.y0, (float)qa.z, nx, 0, nz, U (reg, ua), Vt (reg, bd.v0)});
			o.fv.push_back ({(float)qb.x, (float)bd.y0, (float)qb.z, nx, 0, nz, U (reg, ub), Vt (reg, bd.v0)});
			o.fv.push_back ({(float)qb.x, (float)bd.y1, (float)qb.z, nx, 0, nz, U (reg, ub), Vt (reg, bd.v1)});
			o.fv.push_back ({(float)qa.x, (float)bd.y1, (float)qa.z, nx, 0, nz, U (reg, ua), Vt (reg, bd.v1)});
			o.fi.insert (o.fi.end(), {i0, i0 + 1, i0 + 2, i0, i0 + 2, i0 + 3}); // the .bld's wall order
		}
		a = b;
	}
}

// the walls of each ring, u carried round corners that barely turn and across short faces, else whole bays a face
void Walls (const Style &st, int reg, const std::vector<std::vector<Wall>> &rings, uint32_t hash, Mode mode, Out &o)
{
	for (size_t r = 0; r < rings.size(); r++) {
		double u = (double)(((hash >> 8) + r * 3) % Bays);
		for (size_t j = 0; j < rings[r].size(); j++) {
			const Wall &w = rings[r][j];
			double W = Len (w.p0, w.p1), bw = st.bayW;
			bool cont = false;
			if (j) {
				const Wall &q = rings[r][j - 1];
				double ax = q.p1.x - q.p0.x, az = q.p1.z - q.p0.z, bx = w.p1.x - w.p0.x, bz = w.p1.z - w.p0.z;
				double c = (ax * bx + az * bz) / std::max (1e-9, std::hypot (ax, az) * std::hypot (bx, bz));
				cont = c > std::cos (25.0 * M_PI / 180.0) || W < 0.6 * bw;
			}
			double u0 = cont ? u : std::ceil (u - 1e-6), u1 = cont ? u + W / bw : u0 + std::max (1.0, std::round (W / bw));
			if (mode != Full) { u0 = 0; u1 = std::min ((double)Bays, std::max (1.0, std::round (W / bw))); } // one piece a face
			Face (st, reg, w, u0, u1, mode, o);
			u = u1;
		}
	}
}

// ring corners with corners that turn less than 10 degrees dropped; each keeps the index of the wall leaving it
std::vector<size_t> Corners (const std::vector<Wall> &ring)
{
	std::vector<size_t> c (ring.size());
	for (size_t i = 0; i < c.size(); i++) c[i] = i;
	for (bool again = true; again && c.size() > 3; ) {
		again = false;
		for (size_t k = 0; k < c.size(); k++) {
			P a = ring[c[(k + c.size() - 1) % c.size()]].p0, b = ring[c[k]].p0, d = ring[c[(k + 1) % c.size()]].p0;
			double ax = b.x - a.x, az = b.z - a.z, bx = d.x - b.x, bz = d.z - b.z;
			double l = std::hypot (ax, az) * std::hypot (bx, bz);
			if (l > 0 && (ax * bx + az * bz) / l > std::cos (10.0 * M_PI / 180.0)) { c.erase (c.begin() + k); again = true; break; }
		}
	}
	return c;
}

// two slopes from the eave line e0-e1 up to the ridge r0-r1, a roof tile variant, cut every RoofAlong m along the ridge
void Slope (int var, P e0, P e1, P r0, P r1, double eave, double ridge, double off, Out &o)
{
	int reg = RoofRegion + var / 2, row = var % 2;
	double d1[3] = {e1.x - e0.x, 0, e1.z - e0.z}, d2[3] = {r0.x - e0.x, ridge - eave, r0.z - e0.z};
	double n[3] = {d1[1] * d2[2] - d1[2] * d2[1], d1[2] * d2[0] - d1[0] * d2[2], d1[0] * d2[1] - d1[1] * d2[0]};
	double nl = std::sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (!(nl > 0)) return;
	for (double &x : n) x /= nl;
	if (n[1] < 0) for (double &x : n) x = -x;
	double Le = Len (e0, e1), Ls = std::hypot (Len (Lerp (e0, e1, 0.5), Lerp (r0, r1, 0.5)), ridge - eave);
	double vE = RoofY[row] + RoofPx, vR = RoofY[row] + RoofPx * std::max (0.0, 1.0 - Ls / RoofDown);
	if (!(Le > 0)) return;
	for (double a = off; a < off + Le - 1e-9; ) {
		double base = std::floor (a / RoofAlong) * RoofAlong, b = std::min (off + Le, base + RoofAlong);
		if (b - a < 1e-9) { a = base + RoofAlong; continue; }
		double ua = Gutter + (a - base) / RoofAlong * (Region - 2 * Gutter), ub = Gutter + (b - base) / RoofAlong * (Region - 2 * Gutter);
		double ta = (a - off) / Le, tb = (b - off) / Le;
		P ea = Lerp (e0, e1, ta), eb = Lerp (e0, e1, tb), ra = Lerp (r0, r1, ta), rb = Lerp (r0, r1, tb);
		uint32_t i0 = (uint32_t)o.fv.size();
		float nx = (float)n[0], ny = (float)n[1], nz = (float)n[2];
		o.fv.push_back ({(float)ea.x, (float)eave, (float)ea.z, nx, ny, nz, U (reg, ua), Vt (reg, vE)});
		o.fv.push_back ({(float)eb.x, (float)eave, (float)eb.z, nx, ny, nz, U (reg, ub), Vt (reg, vE)});
		o.fv.push_back ({(float)rb.x, (float)ridge, (float)rb.z, nx, ny, nz, U (reg, ub), Vt (reg, vR)});
		o.fv.push_back ({(float)ra.x, (float)ridge, (float)ra.z, nx, ny, nz, U (reg, ua), Vt (reg, vR)});
		Tri (o.fi, o.fv, i0, i0 + 1, i0 + 2, n[0], n[1], n[2]);
		Tri (o.fi, o.fv, i0, i0 + 2, i0 + 3, n[0], n[1], n[2]);
		a = b;
	}
}

// gable end above the eaves of wall q0-q1, on the plain band
void Gable (const Style &st, int reg, P q0, P q1, double nx, double nz, double eave, double ridge, Out &o)
{
	P m = Lerp (q0, q1, 0.5);
	double w = std::min ((double)Bays, Len (q0, q1) / st.bayW) * BayPx;
	uint32_t i0 = (uint32_t)o.fv.size();
	float fx = (float)nx, fz = (float)nz;
	o.fv.push_back ({(float)q0.x, (float)eave, (float)q0.z, fx, 0, fz, U (reg, Gutter), Vt (reg, PlainY + PlainPx)});
	o.fv.push_back ({(float)q1.x, (float)eave, (float)q1.z, fx, 0, fz, U (reg, Gutter + w), Vt (reg, PlainY + PlainPx)});
	o.fv.push_back ({(float)m.x, (float)ridge, (float)m.z, fx, 0, fz, U (reg, Gutter + w / 2), Vt (reg, PlainY)});
	Tri (o.fi, o.fv, i0, i0 + 1, i0 + 2, nx, 0, nz);
}

double Shoelace (const std::vector<Wall> &ring)
{
	double a = 0;
	for (const Wall &w : ring) a += w.p0.x * w.p1.z - w.p1.x * w.p0.z;
	return 0.5 * a;
}

// the wall quads in order, split into rings where a wall does not start at the last one's end
std::vector<std::vector<Wall>> WallRings (const float *vtx, uint32_t nv, uint32_t &nw)
{
	nw = 0;
	while (nw * 4 + 3 < nv && vtx[(size_t)nw * 4 * 6 + 4] < 0.5f) nw++;
	std::vector<std::vector<Wall>> rings;
	for (uint32_t j = 0; j < nw; j++) {
		const float *a = vtx + (size_t)j * 24;
		Wall w {{a[0], a[2]}, {a[6], a[8]}, a[1], a[13], a[3], a[5]};
		if (rings.empty() || Len (rings.back().back().p1, w.p0) > 0.01) rings.emplace_back();
		rings.back().push_back (w);
	}
	return rings;
}

} // namespace

void Rings (const float *vtx, uint32_t nv, std::vector<std::vector<std::array<double, 2>>> &rings)
{
	uint32_t nw;
	rings.clear();
	for (const std::vector<Wall> &r : WallRings (vtx, nv, nw)) {
		rings.emplace_back();
		for (const Wall &w : r) rings.back().push_back ({w.p0.x, w.p0.z});
	}
}

void Build (const float *vtx, uint32_t nv, const uint32_t *idx, uint32_t ni, float hmin, float hmax, uint32_t hash, Out &o, bool far)
{
	Mode mode = far ? Far : Full;
	o = Out();
	uint32_t nw = 0;
	std::vector<std::vector<Wall>> rings = WallRings (vtx, nv, nw);
	double area = 0;
	for (size_t r = 0; r < rings.size(); r++) area += (r ? -1 : 1) * std::fabs (Shoelace (rings[r]));
	area = std::max (0.0, area);
	if (!rings.empty())
		for (const Wall &w : rings[0]) o.ring.push_back ({w.p0.x, w.p0.z});
	o.style = PickStyle (hmax, area, hash);
	const Style &st = Styles[o.style];
	int reg = o.style;

	// pitched: four square corners, no holes, a small footprint at ground
	if (rings.size() == 1 && hmin <= 0.01f && hmax <= 12.0f && area >= 25.0 && area <= 400.0) {
		const std::vector<Wall> &R = rings[0];
		std::vector<size_t> c = Corners (R);
		bool ok = c.size() == 4;
		double turn = 0;
		for (size_t k = 0; ok && k < 4; k++) {
			P a = R[c[(k + 3) % 4]].p0, b = R[c[k]].p0, d = R[c[(k + 1) % 4]].p0;
			double ax = b.x - a.x, az = b.z - a.z, bx = d.x - b.x, bz = d.z - b.z, l = std::hypot (ax, az) * std::hypot (bx, bz);
			double cr = ax * bz - az * bx;
			if (!(l > 0) || std::fabs ((ax * bx + az * bz) / l) > std::sin (20.0 * M_PI / 180.0)) ok = false;
			else if (k && cr * turn <= 0) ok = false;
			turn = cr;
		}
		if (ok) {
			P q[4];
			double nx[4], nz[4];
			for (int k = 0; k < 4; k++) q[k] = R[c[k]].p0;
			int s = Len (q[0], q[1]) >= Len (q[1], q[2]) ? 0 : 1; // long sides q[s]q[s+1] and q[s+2]q[s+3]
			P k0 = q[s], k1 = q[(s + 1) % 4], k2 = q[(s + 2) % 4], k3 = q[(s + 3) % 4];
			std::vector<Wall> W (4);
			P kk[4] = {k0, k1, k2, k3};
			for (int k = 0; k < 4; k++) {
				const Wall &src = R[c[(s + k) % 4]];
				P a = kk[k], b = kk[(k + 1) % 4];
				double ex = b.x - a.x, ez = b.z - a.z, l = std::hypot (ex, ez);
				nx[k] = -ez / l; nz[k] = ex / l;
				if (nx[k] * src.nx + nz[k] * src.nz < 0) { nx[k] = -nx[k]; nz[k] = -nz[k]; }
				W[k] = {a, b, src.bot, src.top, nx[k], nz[k]};
			}
			double shortSide = std::min (Len (k1, k2), Len (k3, k0));
			double rise = std::min (0.5 * shortSide * std::tan (35.0 * M_PI / 180.0), 4.5);
			double eave = hmax == 5.0f ? 5.0 : hmax - rise, ridge;
			if (eave < 2.4) eave = 2.4;
			ridge = eave + rise;
			for (Wall &w : W) w.top = eave;
			Walls (st, reg, {W}, hash, mode, o);
			Gable (st, reg, k1, k2, nx[1], nz[1], eave, ridge, o);
			Gable (st, reg, k3, k0, nx[3], nz[3], eave, ridge, o);
			P m1 = Lerp (k1, k2, 0.5), m2 = Lerp (k3, k0, 0.5);
			int var = (int)((hash >> 16) % 4);
			double off = (double)((hash >> 20) % 97) / 97.0 * RoofAlong;
			Slope (var, k0, k1, m2, m1, eave, ridge, off, o);
			Slope (var, k2, k3, m1, m2, eave, ridge, off, o);
			o.pitched = true;
			return;
		}
	}

	Walls (st, reg, rings, hash, mode, o);
	if (o.fv.size() > 60000) { // a vast or curved tower: one band a face
		o.fv.clear(); o.fi.clear();
		Walls (st, reg, rings, hash, Simple, o);
	}
	double ox = (double)((hash >> 12) % 101) / 101.0 * FlatRoofM, oz = (double)((hash >> 4) % 103) / 103.0 * FlatRoofM;
	uint32_t r0 = nw * 4;
	for (uint32_t i = r0; i < nv; i++) {
		const float *a = vtx + (size_t)i * 6;
		o.rv.push_back ({a[0], a[1], a[2], a[3], a[4], a[5], (float)((a[0] + ox) / FlatRoofM), (float)((a[2] + oz) / FlatRoofM)});
	}
	for (uint32_t k = 0; k + 2 < ni; k += 3)
		if (idx[k] >= r0 && idx[k + 1] >= r0 && idx[k + 2] >= r0 && idx[k] < nv && idx[k + 1] < nv && idx[k + 2] < nv)
			o.ri.insert (o.ri.end(), {idx[k] - r0, idx[k + 1] - r0, idx[k + 2] - r0});
}

// ---- far buildings: distance classes

double Lod::Scale (int s) { return std::pow (2.0, 0.5 * std::clamp (s, 0, NScale - 1)); }

int Lod::Class (double h, double s, int cur)
{
	const double k = 1.15; // the hysteresis band
	if (h < (cur == 0 ? H[1] * k : cur > 0 ? H[1] / k : H[1])) return 0; // the class-0 edge does not move with s
	double hs = h * s;
	if (cur > 0 && hs >= H[cur] / k && (cur + 1 == NClass || hs < H[cur + 1] * k)) return cur;
	int c = 1;
	while (c + 1 < NClass && H[c + 1] <= hs) c++;
	return c;
}

int Lod::Step (int s, double est, double down, double budget)
{
	if (est > budget && s + 1 < NScale) return s + 1;
	if (s > 0 && down >= 0.0 && down < 0.9 * budget) return s - 1;
	return s;
}

// ---- base structures

namespace {
int64_t Key (int64_t x, int64_t z) { return (int64_t)(((uint64_t)x << 32) ^ (uint64_t)(uint32_t)z); }
const double Cell = 64.0;

double SegDist (double px, double pz, double ax, double az, double bx, double bz)
{
	double dx = bx - ax, dz = bz - az, l = dx * dx + dz * dz;
	double t = l > 0 ? std::clamp (((px - ax) * dx + (pz - az) * dz) / l, 0.0, 1.0) : 0.0;
	return std::hypot (px - ax - t * dx, pz - az - t * dz);
}
}

void BaseIndex::Add (const double a[3], const double b[3], const double c[3])
{
	double e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
	double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
	double l = std::sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	double y0 = std::min ({a[1], b[1], c[1]}), y1 = std::max ({a[1], b[1], c[1]});
	if (!(l > 1e-9) || (!all && (std::fabs (n[1] / l) >= 0.7 || y1 - y0 <= 1.5))) return;
	T t {{a[0], b[0], c[0]}, {a[2], b[2], c[2]}, (a[0] + b[0] + c[0]) / 3, (a[2] + b[2] + c[2]) / 3};
	if (!std::isfinite (t.cx) || !std::isfinite (t.cz)) return;
	uint32_t id = (uint32_t)tris.size();
	tris.push_back (t);
	int64_t x0 = (int64_t)std::floor (std::min ({a[0], b[0], c[0]}) / Cell), x1 = (int64_t)std::floor (std::max ({a[0], b[0], c[0]}) / Cell);
	int64_t z0 = (int64_t)std::floor (std::min ({a[2], b[2], c[2]}) / Cell), z1 = (int64_t)std::floor (std::max ({a[2], b[2], c[2]}) / Cell);
	if (x1 - x0 > 64 || z1 - z0 > 64) return; // a 4 km triangle is no building: kept out of the buckets
	for (int64_t x = x0; x <= x1; x++)
		for (int64_t z = z0; z <= z1; z++) cells[Key (x, z)].push_back (id);
}

bool BaseIndex::Near (double x, double z, double m) const
{
	for (int64_t cx = (int64_t)std::floor ((x - m) / Cell); cx <= (int64_t)std::floor ((x + m) / Cell); cx++)
		for (int64_t cz = (int64_t)std::floor ((z - m) / Cell); cz <= (int64_t)std::floor ((z + m) / Cell); cz++) {
			auto it = cells.find (Key (cx, cz));
			if (it == cells.end()) continue;
			for (uint32_t id : it->second) {
				const T &t = tris[id];
				double d = std::min ({SegDist (x, z, t.x[0], t.z[0], t.x[1], t.z[1]), SegDist (x, z, t.x[1], t.z[1], t.x[2], t.z[2]), SegDist (x, z, t.x[2], t.z[2], t.x[0], t.z[0])});
				if (d <= m) return true;
			}
		}
	return false;
}

bool BaseIndex::Covers (double x, double z, double m) const
{
	if (Near (x, z, m)) return true;
	auto it = cells.find (Key ((int64_t)std::floor (x / Cell), (int64_t)std::floor (z / Cell)));
	if (it == cells.end()) return false;
	for (uint32_t id : it->second) {
		const T &t = tris[id];
		double d1 = (x - t.x[1]) * (t.z[0] - t.z[1]) - (t.x[0] - t.x[1]) * (z - t.z[1]);
		double d2 = (x - t.x[2]) * (t.z[1] - t.z[2]) - (t.x[1] - t.x[2]) * (z - t.z[2]);
		double d3 = (x - t.x[0]) * (t.z[2] - t.z[0]) - (t.x[2] - t.x[0]) * (z - t.z[0]);
		bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
		if (!(neg && pos)) return true; // inside, either winding
	}
	return false;
}

bool BaseIndex::Inside (const std::vector<std::array<double, 2>> &poly) const
{
	if (poly.size() < 3) return false;
	double x0 = poly[0][0], x1 = x0, z0 = poly[0][1], z1 = z0;
	for (const auto &p : poly) { x0 = std::min (x0, p[0]); x1 = std::max (x1, p[0]); z0 = std::min (z0, p[1]); z1 = std::max (z1, p[1]); }
	if (x1 - x0 > 64 * Cell || z1 - z0 > 64 * Cell) return false;
	for (int64_t cx = (int64_t)std::floor (x0 / Cell); cx <= (int64_t)std::floor (x1 / Cell); cx++)
		for (int64_t cz = (int64_t)std::floor (z0 / Cell); cz <= (int64_t)std::floor (z1 / Cell); cz++) {
			auto it = cells.find (Key (cx, cz));
			if (it == cells.end()) continue;
			for (uint32_t id : it->second) {
				double px = tris[id].cx, pz = tris[id].cz;
				if (px < x0 || px > x1 || pz < z0 || pz > z1) continue;
				bool in = false;
				for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
					if ((poly[i][1] > pz) != (poly[j][1] > pz) && px < (poly[j][0] - poly[i][0]) * (pz - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0]) in = !in;
				if (in) return true;
			}
		}
	return false;
}

bool Reaches (const std::vector<std::array<double, 2>> &poly, double r)
{
	if (poly.size() < 3 || !(r > 0.0)) return false;
	bool in = false;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		const auto &a = poly[j], &b = poly[i];
		if ((b[1] > 0.0) != (a[1] > 0.0) && 0.0 < (a[0] - b[0]) * (0.0 - b[1]) / (a[1] - b[1]) + b[0]) in = !in;
		double dx = b[0] - a[0], dz = b[1] - a[1], l2 = dx * dx + dz * dz;
		double s = l2 > 0.0 ? std::clamp (-(a[0] * dx + a[1] * dz) / l2, 0.0, 1.0) : 0.0;
		double px = a[0] + s * dx, pz = a[1] + s * dz;
		if (px * px + pz * pz <= r * r) return true;
	}
	return in;
}

} // namespace BldgGeom
