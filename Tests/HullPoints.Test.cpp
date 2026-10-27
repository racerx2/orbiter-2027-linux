// not upstream (terrain-contact): the hull's contact points and the landed pose
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include "HullPoints.h"

using namespace HullPoints;

static double Det (const Matrix &R)
{
	return R.m11*(R.m22*R.m33 - R.m23*R.m32) - R.m12*(R.m21*R.m33 - R.m23*R.m31) + R.m13*(R.m21*R.m32 - R.m22*R.m31);
}

TEST_CASE ("642 unit directions, every direction within 9 degrees of one", "[HullPoints]")
{
	const std::vector<Vector> &d = Directions ();
	REQUIRE (d.size () == 642);
	for (const Vector &v : d) CHECK (std::fabs (v.length () - 1.0) < 1e-12);
	std::mt19937 rng (7);
	std::normal_distribution<double> n;
	for (int k = 0; k < 2000; k++) {
		Vector u (n (rng), n (rng), n (rng));
		u.unify ();
		double best = -1;
		for (const Vector &v : d) best = std::max (best, dotp (u, v));
		CHECK (best > std::cos (9.0 * 3.14159265358979 / 180.0));
	}
}

TEST_CASE ("A box's support points are its 8 corners", "[HullPoints]")
{
	std::vector<Vector> v;
	for (int i = 0; i < 8; i++) v.push_back (Vector (i & 1 ? 2 : -2, i & 2 ? 1 : -1, i & 4 ? 5 : -5));
	for (int i = 0; i <= 10; i++)
		for (int j = 0; j <= 10; j++) {
			double a = -2 + 0.4*i, c = -5 + 1.0*j;
			v.push_back (Vector (a, 1, c)); v.push_back (Vector (a, -1, c)); v.push_back (Vector (a, -1 + 0.2*i, c));
		}
	std::vector<Vector> s = Support (v);
	REQUIRE (s.size () == 8);
	for (const Vector &p : s) CHECK ((std::fabs (std::fabs (p.x) - 2) < 1e-12 && std::fabs (std::fabs (p.y) - 1) < 1e-12 && std::fabs (std::fabs (p.z) - 5) < 1e-12));
}

TEST_CASE ("A sphere's support points reach any plane within 3 % of its size", "[HullPoints]")
{
	std::mt19937 rng (11);
	std::normal_distribution<double> n;
	std::vector<Vector> v;
	for (int k = 0; k < 20000; k++) { Vector u (n (rng), n (rng), n (rng)); u.unify (); v.push_back (u * 10.0); }
	std::vector<Vector> s = Support (Thin (v, 10.0/64));
	CHECK (s.size () > 300);
	for (int k = 0; k < 1000; k++) {
		Vector u (n (rng), n (rng), n (rng));
		u.unify ();
		double all = -1e9, sup = -1e9;
		for (const Vector &p : v) all = std::max (all, dotp (p, u));
		for (const Vector &p : s) sup = std::max (sup, dotp (p, u));
		CHECK (all - sup < 0.03 * 20.0);
	}
}

TEST_CASE ("Thin keeps one vertex per cell; GearZone drops the gear's; Cap keeps the spread", "[HullPoints]")
{
	std::vector<Vector> v = { {0.01, 0.01, 0.01}, {0.02, 0.03, 0.04}, {0.2, 0, 0}, {0.01, 0.01, 0.01} };
	CHECK (Thin (v, 0.1).size () == 2);
	std::vector<Vector> p = { {0, -2.5, 0}, {0, -2.3, 3}, {0, -2.2, 0}, {5, 0, 0} };
	GearZone (p, Vector (0, -2.57, 10), Vector (0, 1, 0), 0.3);
	REQUIRE (p.size () == 2);
	CHECK (p[0].y == -2.2);
	std::vector<Vector> g;
	for (int i = 0; i < 1000; i++) g.push_back (Vector (i % 10, (i / 10) % 10, i / 100));
	Cap (g, 128);
	REQUIRE (g.size () == 128);
	double dmin = 1e9;
	for (size_t i = 0; i < g.size (); i++) for (size_t j = i + 1; j < g.size (); j++) dmin = std::min (dmin, (g[i] - g[j]).length ());
	CHECK (dmin >= 1.0);
}

TEST_CASE ("Turn is the least rotation taking a onto b", "[HullPoints]")
{
	Vector a (0, 1, 0), b (Vector (0.3, 0.9, -0.2).unit ());
	Matrix R = Turn (a, b);
	CHECK ((mul (R, a) - b).length () < 1e-12);
	CHECK (std::fabs (Det (R) - 1) < 1e-12);
	Vector k = crossp (a, b).unit ();
	CHECK ((mul (R, k) - k).length () < 1e-12);
	Matrix I = Turn (a, a);
	CHECK ((mul (I, Vector (1, 2, 3)) - Vector (1, 2, 3)).length () < 1e-15);
}

static void OnGround (const Vector tp[3], const Matrix &Rh, double ycg, const std::function<double (double, double)> &g)
{
	for (int i = 0; i < 3; i++) {
		Vector a = mul (Rh, tp[i]);
		CHECK (std::fabs (a.y + ycg - g (a.x, a.z)) < 1e-3);
	}
}

TEST_CASE ("Pose stands the primary points on the ground: flat, slopes, a dome; raised over a bump", "[HullPoints]")
{
	const Vector tp[3] = { {0, -2.57, 10}, {-3.5, -2.57, -1}, {3.5, -2.57, -1} }; // the DG's wheels
	const Vector nt (0, 1, 0);
	std::vector<Vector> own (tp, tp + 3);
	const double D = 3.14159265358979 / 180.0;
	SECTION ("flat") {
		Matrix Rh (1, 0, 0, 0, 1, 0, 0, 0, 1);
		double y = 0;
		auto g = [] (double, double) { return 0.0; };
		Pose (tp, nt, own, g, Rh, y);
		CHECK (std::fabs (y - 2.57) < 1e-12);
		CHECK ((mul (Rh, Vector (0, 1, 0)) - Vector (0, 1, 0)).length () < 1e-12);
	}
	SECTION ("20 and 32 degree slopes, turned") {
		for (double sl : {20.0, 32.0})
			for (double hd : {0.0, 30.0, 90.0}) {
				Matrix Rh (std::cos (hd*D), 0, std::sin (hd*D), 0, 1, 0, -std::sin (hd*D), 0, std::cos (hd*D));
				double y = 0;
				auto g = [&] (double x, double z) { return x * std::tan (sl*D) + 0.3 * z * std::tan (sl*D); };
				Pose (tp, nt, own, g, Rh, y);
				OnGround (tp, Rh, y, g);
				Vector N = Vector (-std::tan (sl*D), 1, -0.3*std::tan (sl*D)).unit ();
				CHECK (dotp (mul (Rh, nt), N) > 1 - 1e-9);
				CHECK (std::fabs (Det (Rh) - 1) < 1e-9);
			}
	}
	SECTION ("a dome") {
		Matrix Rh (1, 0, 0, 0, 1, 0, 0, 0, 1);
		double y = 0;
		auto g = [] (double x, double z) { return -(x*x + z*z) / 200.0 + 0.1 * x; };
		Pose (tp, nt, own, g, Rh, y);
		OnGround (tp, Rh, y, g);
	}
	SECTION ("a bump under the belly raises it") {
		Matrix Rh (1, 0, 0, 0, 1, 0, 0, 0, 1);
		double y = 0;
		std::vector<Vector> all = own;
		all.push_back (Vector (0, -1.0, 4));
		auto g = [] (double x, double z) { return (std::fabs (x) < 1 && std::fabs (z - 4) < 1) ? 2.0 : 0.0; };
		Pose (tp, nt, all, g, Rh, y);
		CHECK (std::fabs (y - 3.0) < 1e-9); // the belly point on the bump, the wheels 0.43 m over the ground
	}
}
