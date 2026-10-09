// not upstream: blast unit tests: deterministic sites and bonds, small hit, 70 m/s-class hit, per-frame budget, load restores the actors (design-CA-blast 2, 7)
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include "CollBlastA.h"

namespace {

// closed box hull, side L, n x n quads per face (12 n^2 triangles), weld ids from a 1e-4 grid
CollBlastInput Box (double L, int n, double mass, double size)
{
	CollBlastInput in;
	in.mass = mass; in.size = size;
	std::map<std::tuple<long, long, long>, uint32_t> wid;
	auto weld = [&] (const Vector &p) { auto k = std::make_tuple (std::lround (p.x * 1e4), std::lround (p.y * 1e4), std::lround (p.z * 1e4)); return wid.emplace (k, (uint32_t)wid.size ()).first->second; };
	auto put = [&] (const Vector &a, const Vector &b, const Vector &c) { for (const Vector *p : { &a, &b, &c }) { in.v.push_back (*p); in.w.push_back (weld (*p)); } in.piece.push_back (-1); };
	for (int ax = 0; ax < 3; ax++) for (int sg = -1; sg <= 1; sg += 2) {
		auto P = [&] (double u, double v) { double c[3]; c[ax] = sg * L / 2; c[(ax + 1) % 3] = u; c[(ax + 2) % 3] = v; return Vector (c[0], c[1], c[2]); };
		for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
			double u0 = -L / 2 + L * i / n, u1 = -L / 2 + L * (i + 1) / n, v0 = -L / 2 + L * j / n, v1 = -L / 2 + L * (j + 1) / n;
			put (P (u0, v0), P (u1, v0), P (u1, v1));
			put (P (u0, v0), P (u1, v1), P (u0, v1));
		}
	}
	return in;
}

double Q9 (double v) { char b[40]; snprintf (b, sizeof b, "%.9g", v); return strtod (b, nullptr); }

const double kJ70 = 2500.0 * 70, kDt = 0.02; // DG-like reduced mass x 70 m/s [N s], frame [s]

}

TEST_CASE ("blast 1: deterministic sites, cells and bonds on a 432-triangle box", "[blast]")
{
	CollBlastInput in = Box (4, 6, 5000, 10);
	REQUIRE (in.piece.size () == 432);
	CHECK (CollBlastA::SiteCount (in) == 64);                     // 96 m^2 / 1.44 m^2 = 67, capped at 64
	CollBlastA a, b;
	REQUIRE (a.Build (in));
	REQUIRE (b.Build (in));
	REQUIRE (a.site.size () == 64);
	bool same = true;
	for (size_t i = 0; i < a.site.size (); i++) if (a.site[i].x != b.site[i].x || a.site[i].y != b.site[i].y || a.site[i].z != b.site[i].z) same = false;
	CHECK (same);
	CHECK (a.cellOf == b.cellOf);
	REQUIRE (a.bond.size () == b.bond.size ());
	for (size_t i = 0; i < a.bond.size (); i++) { CHECK (a.bond[i].a == b.bond[i].a); CHECK (a.bond[i].b == b.bond[i].b); CHECK (a.bond[i].area == b.bond[i].area); }
	for (auto &s : a.site) { CHECK (s.x == Q9 (s.x)); CHECK (s.y == Q9 (s.y)); CHECK (s.z == Q9 (s.z)); }
	std::set<std::tuple<double, double, double>> u;
	for (auto &s : a.site) u.insert (std::make_tuple (s.x, s.y, s.z));
	CHECK (u.size () == 64);                                         // distinct sites
	CHECK (a.chunk.size () == 64);
	double m = 0; for (auto &c : a.chunk) m += c.mass;
	CHECK (std::fabs (m - 5000) < 1e-6);
	CHECK (std::fabs (a.t - 5000 / (2700 * 96.0)) < 1e-12);         // skin thickness from EmptyMass
	CHECK (a.bond.size () >= 64);
	for (auto &bd : a.bond) { CHECK (bd.a < bd.b); CHECK (bd.area >= 0.1 * std::sqrt (a.Acell) * a.t - 1e-15); CHECK (std::fabs (bd.n.length () - 1) < 1e-9); CHECK (bd.asset != UINT32_MAX); }
	CHECK (a.Partition ().size () == 1);                             // one connected actor
	CHECK (a.Broken ().empty ());
	CollBlastA c;                                                    // saved sites reproduce the same cells
	REQUIRE (c.Build (in, &a.site));
	CHECK (c.cellOf == a.cellOf);
	CHECK (CollBlastA::logErrors == 0);
}

TEST_CASE ("blast 2: a small hit breaks nothing", "[blast]")
{
	CollBlastInput in = Box (4, 6, 5000, 10);
	CollBlastA a;
	REQUIRE (a.Build (in));
	Vector c (0, 0, 2);
	a.Force (c, Vector (0, 0, -2500.0 * 1 / kDt));                   // 1 m/s
	a.Impact (c, 0.5, 4.0 / 300);                                    // eSpec 4 J/kg
	a.Spin (Vector (), Vector (0.1, 0, 0));
	CHECK (a.Step ().empty ());
	CHECK (a.Broken ().empty ());
	CHECK (a.Partition ().size () == 1);
}

TEST_CASE ("blast 3: a 70 m/s-class hit separates pieces; load restores the same actors", "[blast]")
{
	CollBlastInput in = Box (4, 6, 5000, 10);
	CollBlastA a;
	REQUIRE (a.Build (in));
	Vector c (0.3, 0.2, 2);
	a.Force (c, Vector (0, 0, -kJ70 / kDt));
	a.Impact (c, 1.5, 1.0);
	auto t0 = std::chrono::steady_clock::now ();
	std::vector<CollBlastSplit> sp = a.Step ();
	double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ();
	printf ("blast 70 m/s step: %.3f ms, %zu pieces, %zu broken bonds\n", ms, sp.size (), a.Broken ().size ());
	REQUIRE (sp.size () >= 1);
	std::vector<uint32_t> removed;
	for (auto &s : sp) {
		CHECK (s.mass > 0); CHECK (!s.chunks.empty ()); CHECK (std::fabs (s.n.length () - 1) < 1e-9);
		CHECK (s.c.z > 1.0);                                         // near the struck face
		for (uint32_t ch : s.chunks) { CHECK (a.gone[ch]); removed.push_back (ch); }
	}
	auto part = a.Partition ();
	CHECK (part.size () == sp.size () + 1);
	std::vector<uint32_t> broken = a.Broken ();
	CHECK (!broken.empty ());
	CollBlastA b;                                                    // load: saved sites, K bonds and VCUT cells
	REQUIRE (b.Build (in, &a.site));
	b.Restore (broken, removed);
	CHECK (b.Partition () == part);
	CHECK (b.Broken () == broken);
	CHECK (b.MainChunks () == a.MainChunks ());
	CollBlastA k;                                                    // K rows alone give the same partition
	REQUIRE (k.Build (in, &a.site));
	k.Restore (broken, {});
	CHECK (k.Partition () == part);
	CHECK (k.MainChunks () == a.MainChunks ());
	CHECK (CollBlastA::logErrors == 0);
}

TEST_CASE ("blast 4: per-frame cost for 64 cells below 0.5 ms", "[blast][budget]")
{
	CollBlastInput in = Box (4, 6, 5000, 10);
	CollBlastA a;
	REQUIRE (a.Build (in));
	REQUIRE (a.chunk.size () == 64);
	const int N = 400;
	double worst = 0, total = 0;
	for (int f = 0; f < N; f++) {
		auto t0 = std::chrono::steady_clock::now ();
		a.Spin (Vector (0.1, 0, 0), Vector (0.3, 0.2, 0.1));
		if (f % 50 == 0) a.Force (Vector (2, 0, 0), Vector (-2e5, 0, 0));
		auto sp = a.Step ();
		double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ();
		total += ms; worst = std::max (worst, ms);
		CHECK (sp.empty ());
	}
	double avg = total / N;
	printf ("blast budget: 64 cells, %zu bonds: avg %.4f ms, worst %.4f ms per frame\n", a.bond.size (), avg, worst);
	CHECK (avg < 0.25);                                              // half the 0.5 ms budget as margin
	CHECK (a.Partition ().size () == 1);
}
