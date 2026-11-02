// not upstream: the ground materials' maths (OVP/VulkanClient/GroundGeom.cpp, design-ground-detail 1)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "GroundGeom.h"
#include <cmath>
#include <random>

using namespace GroundGeom;
using Catch::Matchers::WithinAbs;

namespace {
V3 C (float x, float y, float z) { V3 v; v.x = x; v.y = y; v.z = z; return v; }
float Sum (const float *w, int n) { float s = 0.0f; for (int i = 0; i < n; i++) s += w[i]; return s; }
int Argmax (const float W[NMAT]) { int b = 0; for (int i = 1; i < NMAT; i++) if (W[i] > W[b]) b = i; return b; }
float Wrap (float x) { return x - std::floor (x); }
float WrapDiff (float a, float b) { float d = Wrap (a) - Wrap (b); return std::fabs (d - std::round (d)); }
} // namespace

TEST_CASE("Ground materials: weights by class, colour and slope", "[ground]")
{
	const float flat = 1.0f, steep = std::cos (55.0f * 3.14159265f / 180.0f);
	V3 green = C (0.20f, 0.32f, 0.15f), brown = C (0.40f, 0.33f, 0.25f), sand = C (0.80f, 0.66f, 0.45f), grey = C (0.45f, 0.45f, 0.45f);
	float W[NMAT];
	Weights (C (0, 1, 0), green, flat, W); CHECK(Argmax (W) == GRASS); CHECK_THAT(W[GRASS], WithinAbs (1.0, 1e-5));
	Weights (C (0, 1, 0), brown, flat, W); CHECK(Argmax (W) == SOIL);
	Weights (C (1, 0, 0), brown, flat, W); CHECK(Argmax (W) == FOREST);
	Weights (C (0, 0, 1), sand, flat, W); CHECK(Argmax (W) == SAND);
	Weights (C (0, 0, 1), grey, flat, W); CHECK(Argmax (W) == GRAVEL);
	Weights (C (0, 0, 1), green, flat, W); CHECK(Argmax (W) == GRASS); // a lawn in a built-up area
	Weights (C (0, 1, 0), green, steep, W); CHECK(Argmax (W) == ROCK); CHECK_THAT(W[ROCK], WithinAbs (1.0, 1e-5));
	Weights (C (0, 0, 0), green, flat, W); CHECK(Sum (W, NMAT) == 0.0f); // snow, water, no data: today's look
	Weights (C (0, 0, 0), grey, steep, W); CHECK_THAT(W[ROCK], WithinAbs (1.0, 1e-5)); // a cliff in snow or glacier class is rock
	Weights (C (0, 0, 160.0f / 255.0f), grey, flat, W); CHECK_THAT(Sum (W, NMAT), WithinAbs (160.0 / 255.0, 1e-5)); // built-up keeps a plain share

	std::mt19937 rng (7);
	std::uniform_real_distribution<float> u (0.0f, 1.0f);
	for (int i = 0; i < 2000; i++) {
		V3 cls = C (u (rng), u (rng), u (rng)), s = C (u (rng), u (rng), u (rng));
		float c = u (rng), r = 1.0f - Smooth (0.64278761f, 0.81915204f, c);
		Weights (cls, s, c, W);
		float tot = std::min (1.0f, cls.x + cls.y + cls.z) * (1.0f - r) + r;
		CHECK_THAT(Sum (W, NMAT), WithinAbs (tot, 1e-4));
		for (float w : W) CHECK(w >= 0.0f);
		int idx[3]; float w3[3], h[3] = { u (rng), u (rng), u (rng) }, hb[3];
		Top3 (W, idx, w3);
		CHECK_THAT(Sum (w3, 3), WithinAbs (tot, 1e-4));
		CHECK(W[idx[0]] >= W[idx[1]]); CHECK(W[idx[1]] >= W[idx[2]]);
		HeightBlend (w3, h, hb);
		CHECK_THAT(Sum (hb, 3), WithinAbs (Sum (w3, 3), 1e-4));
	}
}

TEST_CASE("Ground materials: weights without land class (Moon, Mars)", "[ground]")
{
	const float flat = 1.0f, steep = std::cos (55.0f * 3.14159265f / 180.0f);
	float W[NMAT];
	WeightsFree (steep, 0.5f, 0.0f, W); CHECK_THAT(W[FROCK], WithinAbs (1.0, 1e-5));
	WeightsFree (flat, 0.5f, 1.0f, W); CHECK(Argmax (W) == BRIGHT);
	WeightsFree (flat, 1.0f, 0.0f, W); CHECK(Argmax (W) == FINE);
	WeightsFree (flat, 0.0f, 0.0f, W); CHECK(Argmax (W) == COARSE);
	std::mt19937 rng (9);
	std::uniform_real_distribution<float> u (0.0f, 1.0f);
	for (int i = 0; i < 2000; i++) {
		WeightsFree (u (rng), Patches (u (rng), u (rng)), Third (u (rng)), W);
		CHECK_THAT(Sum (W, NMAT), WithinAbs (1.0, 1e-4)); // all of the ground has a material
		for (int k = NFREE; k < NMAT; k++) CHECK(W[k] == 0.0f);
		for (float w : W) CHECK(w >= 0.0f);
	}
	CHECK(Patches (0.0f, 0.0f) == 0.0f);
	CHECK(Patches (1.0f, 1.0f) == 1.0f);
	CHECK(Third (0.5f) == 0.0f);
	CHECK(Third (0.9f) == 1.0f);
}

TEST_CASE("Ground materials: the height blend lets a minor material show in patches", "[ground]")
{
	std::mt19937 rng (3);
	std::uniform_real_distribution<float> u (0.0f, 1.0f);
	const float w[3] = { 0.85f, 0.15f, 0.0f };
	int shown = 0, n = 20000;
	for (int i = 0; i < n; i++) {
		float h[3] = { u (rng), u (rng), u (rng) }, o[3];
		HeightBlend (w, h, o);
		CHECK(o[2] == 0.0f);
		if (o[1] > 0.5f) shown++;
	}
	CHECK(shown > n / 100); // patches of the 15 %, not a smear and not gone
	CHECK(shown < n * 15 / 100);
}

TEST_CASE("Ground materials: hex grid weights and continuity", "[ground]")
{
	std::mt19937 rng (11);
	std::uniform_real_distribution<float> u (0.0f, 400.0f);
	for (int i = 0; i < 5000; i++) {
		float x = u (rng), y = u (rng);
		Tri t;
		Grid (x, y, 0.3f, 0.7f, 100.0f, 200.0f, t);
		CHECK_THAT(t.w[0] + t.w[1] + t.w[2], WithinAbs (1.0, 1e-4));
		for (float w : t.w) { CHECK(w >= -1e-4f); CHECK(w <= 1.0001f); }
		float hw[3], h[3] = { 0.2f, 0.9f, 0.5f };
		HexWeights (t.w, h, hw);
		CHECK_THAT(hw[0] + hw[1] + hw[2], WithinAbs (1.0, 1e-5));

		// a value per vertex, blended: continuous across the triangle edges
		auto value = [](float px, float py) {
			Tri q;
			Grid (px, py, 0.3f, 0.7f, 100.0f, 200.0f, q);
			float v = 0.0f;
			for (int j = 0; j < 3; j++) v += q.w[j] * (float)std::fmod (q.id[j][0] * 7.0f + q.id[j][1] * 13.0f, 17.0f);
			return v;
		};
		CHECK_THAT(value (x, y), WithinAbs (value (x + 1e-3f, y + 1e-3f), 0.2));
	}
}

TEST_CASE("Ground materials: two tiles agree on every hex vertex and tap", "[ground]")
{
	// tile A at material origin (oa), tile B at oa + d; the same ground point in each tile's local uv
	std::mt19937 rng (5);
	std::uniform_int_distribution<int> ori (0, 2000000);
	std::uniform_int_distribution<int> rep (1, 11);
	std::uniform_real_distribution<float> f (0.0f, 1.0f);
	for (int i = 0; i < 3000; i++) {
		int r2 = 1 << rep (rng);
		double oau = double(ori (rng)) * r2, oav = double(ori (rng) % 300000) * r2;
		double obu = oau + r2 * (i % 3 - 1), obv = oav + r2 * ((i / 3) % 3 - 1);
		float lu = f (rng) * r2, lv = f (rng) * r2;
		float bu = float(oau + lu - obu), bv = float(oav + lv - obv);
		float ca[2], fa[2], cb[2], fb[2];
		SkewSplit (oau, oav, &ca[0], &ca[1], &fa[0], &fa[1]);
		SkewSplit (obu, obv, &cb[0], &cb[1], &fb[0], &fb[1]);
		Tri ta, tb;
		Grid (lu, lv, fa[0], fa[1], ca[0], ca[1], ta);
		Grid (bu, bv, fb[0], fb[1], cb[0], cb[1], tb);
		float va = 0.0f, vb = 0.0f;
		for (int j = 0; j < 3; j++) {
			float ua, wa, ub, wb;
			TapUV (lu, lv, ta, j, &ua, &wa);
			TapUV (bu, bv, tb, j, &ub, &wb);
			bool same = ta.id[j][0] == tb.id[j][0] && ta.id[j][1] == tb.id[j][1];
			if (same) { CHECK(WrapDiff (ua, ub) < 2e-3f); CHECK(WrapDiff (wa, wb) < 2e-3f); CHECK_THAT(ta.w[j], WithinAbs (tb.w[j], 2e-3)); }
			else { CHECK(ta.w[j] < 2e-3f); CHECK(tb.w[j] < 2e-3f); } // only on an edge may the two pick other triangles
			va += ta.w[j] * (float)std::fmod (ta.id[j][0] * 7.0f + ta.id[j][1] * 13.0f, 17.0f);
			vb += tb.w[j] * (float)std::fmod (tb.id[j][0] * 7.0f + tb.id[j][1] * 13.0f, 17.0f);
		}
		CHECK_THAT(va, WithinAbs (vb, 0.05));
	}
}

TEST_CASE("Ground materials: a tap's normal turns back with its texture", "[ground]")
{
	// a height h(t) = a t.x + b t.y in the texture (t.y grows down the image): its map normal is (-a, b), y image-up;
	// on the ground (v grows south) its normal in east, north is (-dh/du, dh/dv)
	std::mt19937 rng (17);
	std::uniform_real_distribution<float> uf (0.0f, 50.0f), af (-1.0f, 1.0f);
	for (int i = 0; i < 500; i++) {
		float u = uf (rng), v = uf (rng), a = af (rng), b = af (rng), e = 1e-2f;
		Tri t;
		Grid (u, v, 0.3f, 0.6f, 7.0f, 9.0f, t);
		for (int j = 0; j < 3; j++) {
			float t0u, t0v, tuu, tuv, tvu, tvv, rot, ou, ov, wx, wy;
			TapUV (u, v, t, j, &t0u, &t0v);
			TapUV (u + e, v, t, j, &tuu, &tuv);
			TapUV (u, v + e, t, j, &tvu, &tvv);
			float h0 = a * t0u + b * t0v, dhu = (a * tuu + b * tuv - h0) / e, dhv = (a * tvu + b * tvv - h0) / e;
			VertexXf (t.id[j], &rot, &ou, &ov);
			TurnNormal (-a, b, rot, &wx, &wy);
			CHECK_THAT(wx, WithinAbs (-dhu, 2e-2));
			CHECK_THAT(wy, WithinAbs (dhv, 2e-2));
		}
	}
	float wx, wy;
	TurnNormal (0.0f, -1.0f, 0.0f, &wx, &wy); // height rising north leans the normal south
	CHECK_THAT(wy, WithinAbs (-1.0, 1e-5));
	Tri t;
	Grid (3.3f, 1.7f, 0.0f, 0.0f, 0.0f, 0.0f, t);
	float ua, va, ub, vb, e = 1e-3f;
	float rr, ou, ov;
	VertexXf (t.id[0], &rr, &ou, &ov);
	TapUV (3.3f, 1.7f, t, 0, &ua, &va);
	TapUV (3.3f + e, 1.7f, t, 0, &ub, &vb);
	CHECK_THAT((ub - ua) / e, WithinAbs (std::cos (rr), 2e-2)); // the tap's u grows by cos(rot) per lattice u
}

TEST_CASE("Ground materials: the micro textures give way by what the scan shows", "[ground]")
{
	CHECK(Handover (0.5f) == 1.0f);
	CHECK(Handover (4.0f) == 1.0f);   // 4 texels a pixel: the scan in full
	CHECK(Handover (32.0f) == 0.0f);  // 32: only its mean is left, the micro textures are back
	CHECK(Handover (1000.0f) == 0.0f);
	CHECK(Handover (0.0f) == 1.0f);
	float last = 1.0f;
	for (float t = 1.0f; t < 64.0f; t *= 1.1f) { float h = Handover (t); CHECK(h <= last); last = h; }
	// a 4 m scan of 1024 texels, 2508 px across a 50 deg view: from 2 m up the scan holds at 10 m, from 100 m up it's gone at 200 m
	auto texels = [](double eye, double dist) {
		double px = 0.8727 / 2508.0, along = px * dist * dist / eye, across = px * dist;
		return float(std::sqrt (along * across) / 4.0 * 1024.0);
	};
	CHECK(Handover (texels (2.0, 10.0)) > 0.9f);
	CHECK(Handover (texels (100.0, 200.0)) < 0.1f);
}

TEST_CASE("Ground materials: the Lambert ratio", "[ground]")
{
	V3 up = C (0, 0, 1);
	for (float el : { -20.0f, -2.0f, 0.0f, 3.0f, 10.0f, 30.0f, 60.0f, 90.0f }) {
		float a = el * 3.14159265f / 180.0f;
		V3 L = C (std::cos (a), 0.0f, std::sin (a));
		CHECK(LambertRatio (up, up, L) == 1.0f); // a flat detail normal changes nothing at any sun
	}
	V3 tilt = C (0.3f, 0.0f, 0.9539392f);
	auto at = [&](float el) { float a = el * 3.14159265f / 180.0f; return LambertRatio (tilt, up, C (std::cos (a), 0.0f, std::sin (a))); };
	CHECK(at (10.0f) > at (30.0f)); // relief shows most in a low sun
	CHECK(at (30.0f) > at (60.0f));
	CHECK(at (60.0f) > 1.0f);
	V3 away = C (-0.3f, 0.0f, 0.9539392f);
	auto aw = [&](float el) { float a = el * 3.14159265f / 180.0f; return LambertRatio (away, up, C (std::cos (a), 0.0f, std::sin (a))); };
	CHECK(aw (10.0f) < aw (60.0f));
	CHECK(aw (60.0f) < 1.0f);
	CHECK(aw (5.0f) >= 0.0f);
}

TEST_CASE("Ground materials: the B-spline taps reproduce a ramp", "[ground]")
{
	// a 1D texture t[i] = 3 i + 1 with linear filtering; the cubic B-spline reproduces linear data exactly
	auto lin = [](float x) { float c = x - 0.5f, i = std::floor (c), fr = c - i; return (3.0f * i + 1.0f) * (1.0f - fr) + (3.0f * (i + 1.0f) + 1.0f) * fr; }; // x in texels
	for (float x = 4.0f; x < 20.0f; x += 0.137f) {
		float c = x - 0.5f, i = std::floor (c), fr = c - i, g0, g1, h0, h1;
		BSpline (fr, &g0, &g1, &h0, &h1);
		CHECK_THAT(g0 + g1, WithinAbs (1.0, 1e-6));
		float v = g0 * lin (i + h0) + g1 * lin (i + h1);
		CHECK_THAT(v, WithinAbs (3.0f * c + 1.0f, 2e-4));
	}
}
