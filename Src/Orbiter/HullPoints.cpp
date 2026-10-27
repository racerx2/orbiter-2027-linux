// not upstream (terrain-contact): hull contact points from a vessel's meshes, and the pose of a vessel landed on a terrain
#include "HullPoints.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_set>
#include <utility>

namespace HullPoints {

static std::vector<Vector> MakeDirections ()
{
	const double t = (1.0 + std::sqrt (5.0)) / 2.0;
	std::vector<Vector> v = { {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1} };
	std::vector<std::array<int, 3>> f = { {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11}, {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
		{3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9}, {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1} };
	for (int s = 0; s < 3; s++) {
		std::map<std::pair<int, int>, int> mid;
		auto m = [&] (int a, int b) {
			std::pair<int, int> k (std::min (a, b), std::max (a, b));
			auto it = mid.find (k);
			if (it != mid.end ()) return it->second;
			v.push_back ((v[a] + v[b]) * 0.5);
			return mid[k] = (int)v.size () - 1;
		};
		std::vector<std::array<int, 3>> g;
		for (const auto &q : f) {
			int a = m (q[0], q[1]), b = m (q[1], q[2]), c = m (q[2], q[0]);
			g.push_back ({q[0], a, c}); g.push_back ({q[1], b, a}); g.push_back ({q[2], c, b}); g.push_back ({a, b, c});
		}
		f.swap (g);
	}
	for (Vector &x : v) x.unify ();
	return v;
}

const std::vector<Vector> &Directions ()
{
	static const std::vector<Vector> d = MakeDirections ();
	return d;
}

std::vector<Vector> Thin (const std::vector<Vector> &v, double cell)
{
	std::vector<Vector> out;
	std::unordered_set<uint64_t> seen;
	for (const Vector &p : v) {
		if (!std::isfinite (p.x) || !std::isfinite (p.y) || !std::isfinite (p.z)) continue;
		uint64_t k = 0;
		for (double c : {p.x, p.y, p.z}) k = (k << 21) | ((uint64_t)((int64_t)std::floor (c / cell) + (1 << 20)) & 0x1FFFFF);
		if (seen.insert (k).second) out.push_back (p);
	}
	return out;
}

std::vector<Vector> Support (const std::vector<Vector> &v)
{
	std::vector<Vector> out;
	if (v.empty ()) return out;
	for (const Vector &d : Directions ()) {
		size_t b = 0;
		double s = dotp (v[0], d);
		for (size_t i = 1; i < v.size (); i++) {
			double t = dotp (v[i], d);
			if (t > s) s = t, b = i;
		}
		bool dup = false;
		for (const Vector &q : out) if ((q - v[b]).length () < MERGE) { dup = true; break; }
		if (!dup) out.push_back (v[b]);
	}
	return out;
}

void GearZone (std::vector<Vector> &p, const Vector &p0, const Vector &n, double zone)
{
	p.erase (std::remove_if (p.begin (), p.end (), [&] (const Vector &x) { return dotp (x - p0, n) < zone; }), p.end ());
}

void Cap (std::vector<Vector> &p, size_t nmax)
{
	if (p.size () <= nmax) return;
	std::vector<Vector> out;
	std::vector<double> dmin (p.size (), 1e300);
	size_t k = 0;
	for (size_t n = 0; n < nmax; n++) {
		out.push_back (p[k]);
		size_t far = 0;
		double best = -1;
		for (size_t i = 0; i < p.size (); i++) {
			dmin[i] = std::min (dmin[i], (p[i] - p[k]).length ());
			if (dmin[i] > best) best = dmin[i], far = i;
		}
		k = far;
	}
	p.swap (out);
}

Matrix Turn (const Vector &a, const Vector &b)
{
	Vector k = crossp (a, b);
	double s = k.length (), c = dotp (a, b);
	if (s < 1e-12) return Matrix (1, 0, 0, 0, 1, 0, 0, 0, 1); // aligned (b is the terrain's up side, never opposite)
	k /= s;
	double u = 1.0 - c;
	return Matrix (c + k.x*k.x*u,       k.x*k.y*u - k.z*s, k.x*k.z*u + k.y*s,
	               k.y*k.x*u + k.z*s, c + k.y*k.y*u,       k.y*k.z*u - k.x*s,
	               k.z*k.x*u - k.y*s, k.z*k.y*u + k.x*s, c + k.z*k.z*u);
}

void Pose (const Vector tp[3], const Vector &nt, const std::vector<Vector> &all, const std::function<double (double, double)> &ground, Matrix &Rh, double &ycg)
{
	Vector q[3];
	double g[3];
	auto place = [&] () {
		for (int i = 0; i < 3; i++) { q[i] = mul (Rh, tp[i]); g[i] = ground (q[i].x, q[i].z); }
	};
	for (int round = 0; round < 4; round++) {
		place ();
		Vector G[3];
		for (int i = 0; i < 3; i++) G[i] = Vector (q[i].x, g[i], q[i].z);
		Vector N = crossp (G[1] - G[0], G[2] - G[0]);
		double l = N.length ();
		if (!(l > 0)) break;
		N /= l;
		if (N.y < 0) N = -N;
		Vector h = mul (Rh, nt);
		h.unify ();
		Rh.premul (Turn (h, N));
	}
	place ();
	ycg = 0;
	for (int i = 0; i < 3; i++) ycg += (g[i] - q[i].y) / 3.0;
	double deep = 0;
	for (const Vector &p : all) {
		Vector a = mul (Rh, p);
		deep = std::max (deep, ground (a.x, a.z) - (ycg + a.y));
	}
	ycg += deep;
}

}
