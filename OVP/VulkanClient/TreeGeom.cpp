// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: tree placement and meshes (design-ground-graphics 5.3, 5.5)

#include "TreeGeom.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace TreeGeom {

static const double PI = 3.14159265358979323846;

double Ground::At (double lat, double lng, double *slope) const
{
	if (n < 2 || h.size() < size_t(n) * n) { if (slope) *slope = 0.0; return 0.0; }
	double dla = (lat1 - lat0) / (n - 1), dlo = (lng1 - lng0) / (n - 1);
	double fy = std::clamp((lat - lat0) / dla, 0.0, n - 1.001), fx = std::clamp((lng - lng0) / dlo, 0.0, n - 1.001);
	int x = (int)fx, y = (int)fy;
	double ax = fx - x, ay = fy - y;
	double g00 = h[y * n + x], g10 = h[y * n + x + 1], g01 = h[(y + 1) * n + x], g11 = h[(y + 1) * n + x + 1];
	if (slope) {
		double gx = ((g10 - g00) * (1 - ay) + (g11 - g01) * ay) / (dlo * R * std::cos(lat));
		double gy = ((g01 - g00) * (1 - ax) + (g11 - g10) * ax) / (dla * R);
		*slope = std::hypot(gx, gy);
	}
	return (g00 * (1 - ax) + g10 * ax) * (1 - ay) + (g01 * (1 - ax) + g11 * ax) * ay;
}

bool InRing (const std::vector<std::array<double, 2>> &r, double x, double z, double margin)
{
	if (r.size() < 3) return false;
	bool in = false;
	for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
		const auto &a = r[i], &b = r[j];
		if ((a[1] > z) != (b[1] > z) && x < (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]) + a[0]) in = !in;
		double dx = b[0] - a[0], dz = b[1] - a[1], l = dx * dx + dz * dz;
		double t = l > 0 ? std::clamp(((x - a[0]) * dx + (z - a[1]) * dz) / l, 0.0, 1.0) : 0.0;
		if (std::hypot(x - a[0] - t * dx, z - a[1] - t * dz) <= margin) return true;
	}
	return in;
}

double LatticeC (double R)
{
	return std::exp2(std::round(std::log2(PI * R / 8.0))) / PI;
}

void Lattice (double lat, double lng, double c, double *px, double *py)
{
	*px = std::fmod((lng + PI) * c, P);
	*py = std::fmod((0.5 * PI - lat) * c, P);
	if (*px < 0.0) *px += P;
	if (*py < 0.0) *py += P;
}

static inline float Fract (float x) { return x - std::floor(x); }
static inline float Mod (float x, float y) { return x - y * std::floor(x / y); }

static float ClsHash (float x, float y) // the shader's, in float; hash12 of "Hash without Sine", Dave Hoskins, MIT (CREDITS)
{
	float a = Fract(x * 0.1031f), b = Fract(y * 0.1031f), c = Fract(x * 0.1031f);
	float d = a * (b + 33.33f) + b * (c + 33.33f) + c * (a + 33.33f);
	a += d; b += d; c += d;
	return Fract((a + b) * c);
}

float ClsNoise (float px, float py, float period)
{
	float ix = std::floor(px), iy = std::floor(py), fx = px - ix, fy = py - iy;
	fx = fx * fx * (3.0f - 2.0f * fx);
	fy = fy * fy * (3.0f - 2.0f * fy);
	float ax = Mod(ix, period), ay = Mod(iy, period), bx = Mod(ix + 1.0f, period), by = Mod(iy + 1.0f, period);
	float r0 = ClsHash(ax, ay) + (ClsHash(bx, ay) - ClsHash(ax, ay)) * fx;
	float r1 = ClsHash(ax, by) + (ClsHash(bx, by) - ClsHash(ax, by)) * fx;
	return r0 + (r1 - r0) * fy;
}

float Canopy (double px, double py)
{
	float x = float(px), y = float(py);
	float n1 = ClsNoise(x, y, 16384.0f) * 0.6f + ClsNoise(2.0f * x, 2.0f * y, 32768.0f) * 0.4f;
	return std::min(1.0f, std::max(0.0f, (n1 - 0.30f) * 2.5f));
}

static double MeanCanopy ()
{
	double s = 0.0;
	int n = 0;
	for (int j = 0; j < 200; j++)
		for (int i = 0; i < 200; i++, n++) s += Canopy(37.0 + i * 1.37, 911.0 + j * 1.29);
	return std::max(0.05, s / n);
}

static inline uint64_t Mix (uint64_t x) { x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 29; x *= 0x94D049BB133111EBull; return x ^ (x >> 32); }
static inline double U (uint64_t h, int k) { return double(Mix(h + uint64_t(k + 1) * 0x9E3779B97F4A7C15ull) >> 11) * (1.0 / 9007199254740992.0); }

void Place (double lat0, double lat1, double lng0, double lng1, const Params &p, const ClassFn &cls, const GroundFn &ground, const BlockFn &blocked, std::vector<Tree> &out)
{
	static const double meanC = MeanCanopy();
	double c = LatticeC(p.R);
	int64_t ix0 = int64_t(std::floor((lng0 + PI) * c)), ix1 = int64_t(std::floor((lng1 + PI) * c));
	int64_t iy0 = int64_t(std::floor((0.5 * PI - lat1) * c)), iy1 = int64_t(std::floor((0.5 * PI - lat0) * c));
	for (int64_t iy = iy0; iy <= iy1; iy++)
		for (int64_t ix = ix0; ix <= ix1; ix++)
			for (int sub = 0; sub < 4; sub++) {
				uint64_t h = Mix(uint64_t(ix) * 0x9E3779B97F4A7C15ull ^ uint64_t(iy) * 0xC2B2AE3D27D4EB4Full ^ uint64_t(sub + 1) * 0x165667B19E3779F9ull);
				double fx = ix + 0.5 * (sub & 1) + 0.5 * U(h, 0), fy = iy + 0.5 * (sub >> 1) + 0.5 * U(h, 1);
				double lng = fx / c - PI, lat = 0.5 * PI - fy / c;
				if (lat < lat0 || lat >= lat1 || lng < lng0 || lng >= lng1) continue; // half open: neighbour boxes share none
				Class cl;
				if (!cls(lat, lng, &cl)) continue;
				if (cl.water || cl.b > 0.3f || cl.r + cl.g + cl.b < 0.1f) continue;
				float can = Canopy(std::fmod(fx, P), std::fmod(fy, P));
				double subHa = p.R * p.R * std::cos(lat) * 0.25 / (c * c) / 1e4;
				double k = p.perHa * subHa / meanC * p.density;
				bool shrub = cl.r > 0.25f && cl.r < 0.6f && cl.g > 0.35f;
				double keep = (shrub ? 0.3 : cl.r) * can * k;
				if (U(h, 2) >= keep) continue;
				if (blocked && blocked(lat, lng)) continue;
				double g = ground ? ground(lat, lng) : 0.0, alat = std::fabs(lat) * 180.0 / PI;
				int kind = BUSH;
				if (!shrub) {
					double conifer = (alat > 52.0 || g > 1600.0) ? 0.85 : (alat < 23.0 ? 0.10 : 0.5);
					double u = U(h, 3);
					kind = u < conifer ? (u < conifer * 0.6 ? SPRUCE : PINE) : (U(h, 4) < 0.5 ? BROAD1 : BROAD2);
				}
				Tree t;
				t.lat = lat;
				t.lng = lng;
				t.h = float(kind == BUSH ? 1.0 + 2.0 * U(h, 5) : (8.0 + 17.0 * U(h, 5)) * (alat < 23.0 ? 1.2 : 1.0));
				t.yaw = float(2.0 * PI * U(h, 6));
				t.kind = uint8_t(kind);
				t.tint0 = uint8_t(h >> 40);
				t.tint1 = uint8_t(h >> 48);
				out.push_back(t);
			}
}

// meshes ----------------------------------------------------------------

namespace {

struct Builder {
	std::vector<Vtx> &v;
	std::vector<uint16_t> &ix;
	void Tri (uint16_t a, uint16_t b, uint16_t c) { ix.push_back(a); ix.push_back(b); ix.push_back(c); }
	uint16_t Add (double x, double y, double z, double nx, double ny, double nz, float part, float ao)
	{
		double l = std::sqrt(nx * nx + ny * ny + nz * nz);
		if (l > 0) { nx /= l; ny /= l; nz /= l; }
		v.push_back({ float(x), float(y), float(z), float(nx), float(ny), float(nz), part, ao });
		return uint16_t(v.size() - 1);
	}
};

// outward winding: a front face has cross(b - a, c - a) along its normal
void Oriented (Builder &B, uint16_t a, uint16_t b, uint16_t c, double nx, double ny, double nz)
{
	const Vtx &A = B.v[a], &P = B.v[b], &C = B.v[c];
	double ux = P.x - A.x, uy = P.y - A.y, uz = P.z - A.z, wx = C.x - A.x, wy = C.y - A.y, wz = C.z - A.z;
	double cx = uy * wz - uz * wy, cy = uz * wx - ux * wz, cz = ux * wy - uy * wx;
	if (cx * nx + cy * ny + cz * nz >= 0.0) B.Tri(a, b, c);
	else B.Tri(a, c, b);
}

// a closed cone: base ring at y0 (radius r), apex at y1
void Cone (Builder &B, double y0, double y1, double r, int n, float part, float ao0, float ao1)
{
	double slope = r / (y1 - y0);
	uint16_t apex0 = uint16_t(B.v.size());
	std::vector<uint16_t> ring, apex, cap;
	for (int i = 0; i < n; i++) {
		double a = 2.0 * PI * i / n, x = std::cos(a), z = std::sin(a);
		ring.push_back(B.Add(x * r, y0, z * r, x, slope, z, part, ao0));
		double am = 2.0 * PI * (i + 0.5) / n;
		apex.push_back(B.Add(0, y1, 0, std::cos(am), slope, std::sin(am), part, ao1));
	}
	uint16_t c0 = B.Add(0, y0, 0, 0, -1, 0, part, ao0 * 0.6f);
	for (int i = 0; i < n; i++) cap.push_back(B.Add(B.v[ring[i]].x, y0, B.v[ring[i]].z, 0, -1, 0, part, ao0 * 0.6f));
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n;
		double am = 2.0 * PI * (i + 0.5) / n;
		Oriented(B, ring[i], ring[j], apex[i], std::cos(am), slope, std::sin(am));
		Oriented(B, c0, cap[i], cap[j], 0, -1, 0);
	}
	(void)apex0;
}

// a closed cylinder (trunk)
void Cylinder (Builder &B, double y0, double y1, double r, int n, float ao0, float ao1)
{
	std::vector<uint16_t> lo, hi, clo, chi;
	for (int i = 0; i < n; i++) {
		double a = 2.0 * PI * i / n, x = std::cos(a), z = std::sin(a);
		lo.push_back(B.Add(x * r, y0, z * r, x, 0, z, 0.0f, ao0));
		hi.push_back(B.Add(x * r * 0.7, y1, z * r * 0.7, x, 0, z, 0.0f, ao1));
		clo.push_back(B.Add(x * r, y0, z * r, 0, -1, 0, 0.0f, ao0));
		chi.push_back(B.Add(x * r * 0.7, y1, z * r * 0.7, 0, 1, 0, 0.0f, ao1));
	}
	uint16_t cl = B.Add(0, y0, 0, 0, -1, 0, 0.0f, ao0), ch = B.Add(0, y1, 0, 0, 1, 0, 0.0f, ao1);
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n;
		double am = 2.0 * PI * (i + 0.5) / n, x = std::cos(am), z = std::sin(am);
		Oriented(B, lo[i], lo[j], hi[i], x, 0, z);
		Oriented(B, lo[j], hi[j], hi[i], x, 0, z);
		Oriented(B, cl, clo[i], clo[j], 0, -1, 0);
		Oriented(B, ch, chi[i], chi[j], 0, 1, 0);
	}
}

// a displaced icosphere (one subdivision, 80 faces): a leaf clump
void Blob (Builder &B, double cx, double cy, double cz, double rx, double ry, double rz, uint32_t seed, float aoLow, float aoHigh)
{
	const double t = (1.0 + std::sqrt(5.0)) / 2.0;
	std::vector<std::array<double, 3>> p = { {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1} };
	std::vector<std::array<int, 3>> f = { {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1} };
	for (auto &q : p) { double l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]); for (double &c : q) c /= l; }
	std::vector<std::array<int, 3>> f2;
	std::vector<std::pair<uint64_t, int>> mid;
	auto Mid = [&](int a, int b) {
		uint64_t key = a < b ? (uint64_t(a) << 32 | uint32_t(b)) : (uint64_t(b) << 32 | uint32_t(a));
		for (auto &m : mid) if (m.first == key) return m.second;
		std::array<double, 3> q = { p[a][0] + p[b][0], p[a][1] + p[b][1], p[a][2] + p[b][2] };
		double l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
		for (double &c : q) c /= l;
		p.push_back(q);
		mid.push_back({ key, int(p.size() - 1) });
		return int(p.size() - 1);
	};
	for (auto &tr : f) {
		int a = Mid(tr[0], tr[1]), b = Mid(tr[1], tr[2]), c = Mid(tr[2], tr[0]);
		f2.push_back({ tr[0], a, c }); f2.push_back({ tr[1], b, a }); f2.push_back({ tr[2], c, b }); f2.push_back({ a, b, c });
	}
	uint16_t base = uint16_t(B.v.size());
	for (size_t i = 0; i < p.size(); i++) {
		uint64_t h = Mix(uint64_t(seed) * 1315423911ull + i);
		double d = 0.85 + 0.3 * (double(h >> 11) * (1.0 / 9007199254740992.0)); // lumpy surface
		double x = p[i][0] * d, y = p[i][1] * d, z = p[i][2] * d;
		float ao = aoLow + (aoHigh - aoLow) * float(0.5 + 0.5 * p[i][1]);
		B.Add(cx + x * rx, cy + y * ry, cz + z * rz, p[i][0] / rx, p[i][1] / ry, p[i][2] / rz, 1.0f, ao);
	}
	for (auto &tr : f2) {
		const std::array<double, 3> &a = p[tr[0]];
		const std::array<double, 3> &b = p[tr[1]];
		const std::array<double, 3> &c = p[tr[2]];
		Oriented(B, uint16_t(base + tr[0]), uint16_t(base + tr[1]), uint16_t(base + tr[2]), a[0] + b[0] + c[0], a[1] + b[1] + c[1], a[2] + b[2] + c[2]);
	}
}

} // namespace

void Mesh (int kind, std::vector<Vtx> &v, std::vector<uint16_t> &idx)
{
	v.clear();
	idx.clear();
	Builder B{ v, idx };
	switch (kind) {
	case SPRUCE:
		Cylinder(B, 0.0, 0.3, 0.025, 6, 0.4f, 0.6f);
		for (int i = 0; i < 5; i++) {
			double y0 = 0.12 + i * 0.16, r = 0.26 * (1.0 - i * 0.16);
			Cone(B, y0, std::min(1.0, y0 + 0.32), r, 9, 1.0f, 0.45f + 0.1f * i, 1.0f);
		}
		break;
	case PINE:
		Cylinder(B, 0.0, 0.75, 0.03, 6, 0.4f, 0.8f);
		Blob(B, 0.0, 0.78, 0.0, 0.22, 0.12, 0.22, 11, 0.5f, 1.0f);
		Blob(B, 0.08, 0.9, 0.05, 0.15, 0.09, 0.15, 12, 0.6f, 1.0f);
		break;
	case BROAD1:
		Cylinder(B, 0.0, 0.45, 0.035, 6, 0.4f, 0.7f);
		Blob(B, 0.0, 0.62, 0.0, 0.3, 0.26, 0.3, 21, 0.45f, 1.0f);
		Blob(B, 0.14, 0.78, 0.05, 0.22, 0.2, 0.22, 22, 0.55f, 1.0f);
		Blob(B, -0.12, 0.8, -0.08, 0.2, 0.18, 0.2, 23, 0.55f, 1.0f);
		break;
	case BROAD2:
		Cylinder(B, 0.0, 0.35, 0.04, 6, 0.4f, 0.7f);
		Blob(B, 0.0, 0.58, 0.0, 0.36, 0.3, 0.32, 31, 0.45f, 1.0f);
		Blob(B, -0.1, 0.78, 0.1, 0.26, 0.2, 0.26, 32, 0.6f, 1.0f);
		Blob(B, 0.15, 0.72, -0.1, 0.22, 0.2, 0.22, 33, 0.55f, 1.0f);
		break;
	default: // BUSH
		Blob(B, 0.0, 0.45, 0.0, 0.5, 0.45, 0.5, 41, 0.4f, 1.0f);
		Blob(B, 0.3, 0.35, 0.15, 0.32, 0.32, 0.32, 42, 0.4f, 1.0f);
		Blob(B, -0.25, 0.32, -0.2, 0.3, 0.3, 0.3, 43, 0.4f, 1.0f);
		break;
	}
}

void Impostor (std::vector<Vtx> &v, std::vector<uint16_t> &idx)
{
	v = { { -0.5f, 0.0f, 0.0f, 0, 0, -1, 2.0f, 0.0f }, { 0.5f, 0.0f, 0.0f, 0, 0, -1, 2.0f, 0.0f },
		{ 0.5f, 1.0f, 0.0f, 0, 0, -1, 2.0f, 1.0f }, { -0.5f, 1.0f, 0.0f, 0, 0, -1, 2.0f, 1.0f } };
	idx = { 0, 3, 2, 0, 2, 1 };
}

} // namespace TreeGeom
