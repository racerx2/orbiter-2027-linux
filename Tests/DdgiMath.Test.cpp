
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "DdgiMath.h"
#include <cmath>
#include <random>
#include <set>

using namespace DdgiMath;
using Catch::Matchers::WithinAbs;

TEST_CASE("DDGI: a probe keeps its slot while the grid scrolls", "[ddgi]")
{
	std::mt19937 rng (3);
	std::uniform_int_distribution<int> o (-100000, 100000), k (-30, 30);
	for (int n = 0; n < 300; n++) {
		I3 a = { o (rng), o (rng), o (rng) }, b = { a.x + k (rng), a.y + k (rng), a.z + k (rng) };
		std::set<int> seen;
		for (int s = 0; s < NPC; s += 7) {
			I3 c = CellOf (s, a);
			CHECK(Slot (c) == s);
			CHECK((c.x >= a.x && c.x < a.x + GX && c.y >= a.y && c.y < a.y + GY && c.z >= a.z && c.z < a.z + GZ));
			bool inB = c.x >= b.x && c.x < b.x + GX && c.y >= b.y && c.y < b.y + GY && c.z >= b.z && c.z < b.z + GZ;
			if (inB) { I3 d = CellOf (s, b); CHECK((d.x == c.x && d.y == c.y && d.z == c.z)); } // the same cell, the same slot
		}
	}
	for (int s = 0; s < NPC; s++) { I3 c = CellOf (s, { 0, 0, 0 }); CHECK(Slot (c) == s); }
}

TEST_CASE("DDGI: the grid stands on the ground under the camera, or around a camera high above it", "[ddgi]")
{
	for (double h : { 2900.0, 900.0, 191.0, 20.0, 5.9, 2.5, 0.0 }) {
		for (int c = 0; c < NCASC; c++) {
			double s = Spacing (c);
			V3 cam = { 123.4, 15.0 + h, -77.0 };
			I3 o = Origin (c, cam, 15.0);
			V3 g = { cam.x, 15.0, cam.z };
			bool low = h < 0.5 * GY * s;
			CHECK(Inside (c, o, cam, 0.0)); // the camera is in every cascade
			if (low) CHECK(Inside (c, o, g, 0.0)); // the ground under it where the grid stands on it
			else CHECK(!Inside (c, o, g, 0.0));
			if (low) CHECK(o.y * s <= 15.0 - 2.0 * s);
			CHECK(Blend (c, o, { cam.x, (o.y + 0.5) * s, cam.z }) == 0.0);
			CHECK(Blend (c, o, { cam.x, (o.y + 4.5) * s, cam.z }) == 1.0);
		}
	}
	CHECK(Spacing (0) == 1.0);
	CHECK(Spacing (5) == 1024.0);
}

TEST_CASE("DDGI: octahedral maps round trip and read smoothly across the folds", "[ddgi]")
{
	std::mt19937 rng (5);
	std::normal_distribution<double> g;
	for (int n = 0; n < 5000; n++) {
		V3 d = Unit ({ g (rng), g (rng), g (rng) });
		double u, v;
		OctEncode (d, &u, &v);
		CHECK(std::fabs (u) <= 1.0); CHECK(std::fabs (v) <= 1.0);
		V3 e = OctDecode (u, v);
		CHECK_THAT(Dot (d, e), WithinAbs (1.0, 1e-9));
	}
	for (int N : { IRR, DST }) {
		// a smooth field stored at the texel centres reads back close to itself everywhere, folds included
		auto f = [](const V3 &d) { return 0.5 + 0.3 * d.x - 0.2 * d.y + 0.4 * d.z; };
		std::vector<double> map (N * N);
		for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) map[j * N + i] = f (TexelDir (i, j, N));
		double worst = 0.0;
		for (int n = 0; n < 20000; n++) {
			V3 d = Unit ({ g (rng), g (rng), g (rng) });
			Tap t = OctBilinear (d, N);
			double s = 0.0, ws = 0.0;
			for (int k = 0; k < 4; k++) {
				CHECK((t.i[k] >= 0 && t.i[k] < N && t.j[k] >= 0 && t.j[k] < N));
				s += t.w[k] * map[t.j[k] * N + t.i[k]]; ws += t.w[k];
			}
			CHECK_THAT(ws, WithinAbs (1.0, 1e-9));
			worst = std::max (worst, std::fabs (s - f (d)));
		}
		CHECK(worst < (N == IRR ? 0.11 : 0.05)); // first order at the map's creases (y = 0), the same on both halves: the folds add nothing
		// two directions a hair apart across the z = 0 fold read nearly the same
		for (int n = 0; n < 2000; n++) {
			V3 d = Unit ({ g (rng), g (rng), 0.0 });
			V3 a = Unit ({ d.x, d.y, 1e-4 }), b = Unit ({ d.x, d.y, -1e-4 });
			auto rd = [&](const V3 &q) { Tap t = OctBilinear (q, N); double s = 0.0; for (int k = 0; k < 4; k++) s += t.w[k] * map[t.j[k] * N + t.i[k]]; return s; };
			CHECK_THAT(rd (a), WithinAbs (rd (b), 0.01));
		}
	}
	int i = -1, j = -1; WrapTexel (&i, &j, 6); CHECK((i == 5 && j == 5)); // a corner continues at the opposite one
	i = 6; j = 2; WrapTexel (&i, &j, 6); CHECK((i == 5 && j == 3));
}

TEST_CASE("DDGI: rays cover the sphere evenly and turn by a rotation", "[ddgi]")
{
	V3 m;
	for (int i = 0; i < RAYS; i++) { V3 d = Fibonacci (i, RAYS); CHECK_THAT(Dot (d, d), WithinAbs (1.0, 1e-12)); m = Add (m, d); }
	CHECK(std::sqrt (Dot (m, m)) / RAYS < 0.02);
	int up = 0;
	for (int i = 0; i < RAYS; i++) if (Fibonacci (i, RAYS).z > 0.5) up++;
	CHECK(std::abs (up - RAYS / 4) <= 2); // a cap of a quarter of the sphere holds a quarter of the rays
	for (unsigned s = 0; s < 500; s++) {
		double R[9];
		Rotation (s, R);
		for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) {
			double d = R[a * 3] * R[b * 3] + R[a * 3 + 1] * R[b * 3 + 1] + R[a * 3 + 2] * R[b * 3 + 2];
			CHECK_THAT(d, WithinAbs (a == b ? 1.0 : 0.0, 1e-9));
		}
		double det = R[0] * (R[4] * R[8] - R[5] * R[7]) - R[1] * (R[3] * R[8] - R[5] * R[6]) + R[2] * (R[3] * R[7] - R[4] * R[6]);
		CHECK_THAT(det, WithinAbs (1.0, 1e-9));
	}
}

TEST_CASE("DDGI: the Chebyshev test passes the near side and shuts the far side", "[ddgi]")
{
	CHECK(Chebyshev (10.0, 101.0, 5.0) == 1.0);
	CHECK(Chebyshev (10.0, 101.0, 10.0) == 1.0);
	CHECK(Chebyshev (10.0, 101.0, 12.0) < Chebyshev (10.0, 101.0, 11.0));
	CHECK(Chebyshev (10.0, 100.0, 15.0) < 1e-3); // a wall: no spread, nothing behind it
}

TEST_CASE("DDGI: the budget settles on the target", "[ddgi]")
{
	for (double per : { 2e-5, 6e-5, 2e-4 }) {
		int n = 256;
		double ms = 0.0;
		for (int f = 0; f < 20; f++) { ms = 0.02 + per * n; n = Budget (n, ms, 0.35, 256, 8192); }
		double best = std::clamp ((0.35 - 0.02) / per, 256.0, 8192.0);
		CHECK(std::fabs (n - best) <= 0.1 * best);
	}
	CHECK(Budget (1000, 0.0, 0.35, 256, 8192) == 1000);
}

TEST_CASE("DDGI: atlas blocks don't overlap and fit a texture", "[ddgi]")
{
	for (int N : { IRR, DST }) {
		int w, h;
		AtlasSize (N, &w, &h);
		CHECK(w <= 16384); CHECK(h <= 16384);
		std::set<long> first;
		for (int p = 0; p < NPROBE; p++) {
			int x, y;
			AtlasTexel (p, N, &x, &y);
			CHECK((x % N == 0 && y % N == 0 && x + N <= w && y + N <= h));
			first.insert ((long)y * w + x);
		}
		CHECK((int)first.size () == NPROBE);
	}
}

TEST_CASE("DDGI: beyond the traced region an open receiver gets what its shader gives", "[ddgi]")
{
	V3 up = { 0, 1, 0 }, sky = { 0.3, 0.4, 0.6 }, gnd = { 0.2, 0.15, 0.1 };
	V3 e = Irradiance (up, up, sky, gnd);
	CHECK_THAT(e.x, WithinAbs (sky.x, 1e-12)); // flat open ground: the sky ambient, so the composite adds nothing
	std::mt19937 rng (9);
	std::normal_distribution<double> g;
	for (int n = 0; n < 50; n++) {
		V3 nr = Unit ({ g (rng), g (rng), g (rng) });
		V3 s;
		const int M = 20000;
		for (int i = 0; i < M; i++) { // (1/pi) int L cos dw by a cosine-weighted Fibonacci set about nr
			V3 d = Fibonacci (i, M);
			double c = Dot (d, nr);
			if (c <= 0.0) continue;
			s = Add (s, Scale (MissRadiance (d, up, sky, gnd), c * 4.0 / M));
		}
		V3 r = Irradiance (nr, up, sky, gnd);
		CHECK_THAT(s.x, WithinAbs (r.x, 5e-3)); CHECK_THAT(s.z, WithinAbs (r.z, 5e-3));
	}
}
