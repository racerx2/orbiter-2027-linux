// not upstream: auto-exposure controller and the night look maths (OVP/VulkanClient/AutoExpo.cpp)

#include <catch2/catch_test_macros.hpp>
#include "AutoExpo.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <random>
#include <utility>
#include <vector>

using namespace AutoExpo;

namespace {

float Curve (float x) { return x * std::pow(1.0f + x * x * x * x, -0.25f); } // the legacy tone curve

// a scene of fixed brightness L0, measured two frames late as the client does
struct Sim {
	Controller c;
	float L0, sat = 0.0f, weight = 1.0f, top = 2.5f;
	bool linear = false; // a measure without the curve: means past 1 can be given
	uint32_t frame = 0;
	std::deque<std::pair<uint32_t, float>> inflight;
	explicit Sim (float l0) : L0(l0) {}
	void Step (float dt = 1.0f / 60.0f) {
		frame++;
		if (inflight.size() >= 2) {
			auto m = inflight.front();
			inflight.pop_front();
			float L = linear ? L0 * m.second : Curve(L0 * m.second);
			c.Update(Sample{ std::log2(L), weight, sat }, m.first, dt, top);
		}
		c.Used(frame);
		inflight.push_back({ frame, c.Exposure() });
	}
	void Run (float s) { for (int i = 0; i < int(s * 60.0f); i++) Step(); }
};

} // namespace

TEST_CASE("AutoExpo: daylight in the band holds e = 1", "[autoexpo]")
{
	Sim s(0.3f);
	s.Run(10.0f);
	CHECK(std::fabs(s.c.EV()) < 1e-6f);
}

TEST_CASE("AutoExpo: dark scene rises to the low edge without overshoot", "[autoexpo]")
{
	Sim s(0.03f); // 2 EV under lo
	float prev = 0.0f;
	bool mono = true;
	for (int i = 0; i < 20 * 60; i++) {
		s.Step();
		if (s.c.EV() < prev - 1e-6f) mono = false;
		prev = s.c.EV();
	}
	CHECK(mono);
	CHECK(std::fabs(s.c.EV() - 2.0f) < 0.1f);
	CHECK(s.c.EV() <= 2.0f + 0.02f);
}

TEST_CASE("AutoExpo: 4 EV under stops at the clamp", "[autoexpo]")
{
	Sim s(0.12f / 16.0f);
	s.Run(30.0f);
	CHECK(std::fabs(s.c.EV() - 2.5f) < 0.01f);
	s.top = 1.0f; // out of the atmosphere
	s.Run(30.0f);
	CHECK(std::fabs(s.c.EV() - 1.0f) < 0.02f);
}

TEST_CASE("AutoExpo: bright scene falls to the high edge without overshoot", "[autoexpo]")
{
	Sim s(1.0f); // displays at 0.84
	float prev = 0.0f;
	bool mono = true;
	for (int i = 0; i < 10 * 60; i++) {
		s.Step();
		if (s.c.EV() > prev + 1e-6f) mono = false;
		prev = s.c.EV();
	}
	CHECK(mono);
	float L = Curve(s.L0 * s.c.Exposure());
	CHECK(L > 0.55f * 0.95f);
	CHECK(L < 0.55f * 1.1f);
}

TEST_CASE("AutoExpo: saturation never raises e", "[autoexpo]")
{
	Sim s(0.04f); // dark mean
	s.sat = 0.3f;  // but many blown pixels
	float prev = 0.0f;
	bool never = true;
	for (int i = 0; i < 10 * 60; i++) {
		s.Step();
		if (s.c.EV() > prev + 1e-6f) never = false;
		prev = s.c.EV();
	}
	CHECK(never);
	CHECK(s.c.EV() < -0.1f);
	CHECK(s.c.EV() >= -1.5f);
}

TEST_CASE("AutoExpo: weak measure holds, reset gives 1", "[autoexpo]")
{
	Sim s(0.01f);
	s.Run(3.0f);
	float ev = s.c.EV();
	CHECK(ev > 0.5f);
	s.weight = 0.01f;
	s.Run(3.0f);
	CHECK(s.c.EV() == ev);
	s.c.Reset();
	CHECK(s.c.Exposure() == 1.0f);
}

TEST_CASE("AutoExpo: stale or reused stamps are ignored", "[autoexpo]")
{
	Controller c;
	Sample dark{ std::log2(0.01f), 1.0f, 0.0f };
	CHECK_FALSE(c.Update(dark, 0, 0.1f, 2.5f));
	CHECK_FALSE(c.Update(dark, 7, 0.1f, 2.5f));
	c.Used(7);
	CHECK(c.Update(dark, 7, 0.1f, 2.5f));
	float ev = c.EV();
	CHECK(ev > 0.0f);
	CHECK_FALSE(c.Update(dark, 7, 0.1f, 2.5f));
	CHECK(c.EV() == ev);
	for (uint32_t i = 100; i < 120; i++) c.Used(i); // 7 has left the history
	CHECK_FALSE(c.Update(dark, 101, 0.1f, 2.5f));
	CHECK(c.Update(dark, 119, 0.1f, 2.5f));
}

TEST_CASE("AutoExpo: 2 EV above the band falls to its high edge without overshoot", "[autoexpo]")
{
	Sim s(0.55f * 4.0f);
	s.linear = true;
	Params p;
	p.minEV = -3.0f;
	s.c.SetParams(p);
	float prev = 0.0f;
	bool mono = true;
	for (int i = 0; i < 10 * 60; i++) {
		s.Step();
		if (s.c.EV() > prev + 1e-6f) mono = false;
		prev = s.c.EV();
	}
	CHECK(mono);
	CHECK(std::fabs(s.c.EV() + 2.0f) < 0.05f);
	CHECK(s.c.EV() >= -2.0f - 0.02f);
}

TEST_CASE("AutoExpo: the minEV clamp holds a scene brighter still", "[autoexpo]")
{
	Sim s(0.55f * 8.0f); // 3 EV above
	s.linear = true;
	s.Run(20.0f);
	CHECK(std::fabs(s.c.EV() + 1.5f) < 1e-4f);
	CHECK(s.c.EV() >= -1.5f);
	s.sat = 0.5f; // blown out as well: saturation pushes, the clamp holds
	s.Run(5.0f);
	CHECK(s.c.EV() >= -1.5f);
}

// ---- night look (design-night-look): the night rules, the shader and generator maths

namespace {

Params NightParams () { Params p; p.night = true; return p; }

// a lit city: the near-saturated share grows with e, as lights reach the threshold
struct CitySim : Sim {
	explicit CitySim (float l0) : Sim(l0) {}
	float SatAt (float e) const { return std::min(0.2f, std::max(0.0f, (e - 1.0f) * 0.06f)); }
};

} // namespace

TEST_CASE("NightLook: night rules off or night weight 0 give the legacy exposure to the bit", "[autoexpo][night]")
{
	float scenes[] = { 0.3f, 0.03f, 0.12f / 16.0f, 1.0f, 0.01f };
	for (float L0 : scenes) {
		Sim a(L0), b(L0);
		b.c.SetParams(NightParams());
		b.c.SetNight(0.0f);
		for (int i = 0; i < 20 * 60; i++) {
			if (i == 600) { a.sat = b.sat = 0.3f; }
			a.Step(); b.Step();
			REQUIRE(a.c.EV() == b.c.EV());
		}
	}
	Controller c(NightParams());
	c.SetNight(0.0f);
	CHECK(c.SatThreshold() == 254.0f / 255.0f);
	Controller d;
	d.SetNight(1.0f); // the weight does nothing without Params::night
	CHECK(d.Night() == 0.0f);
	CHECK(d.SatThreshold() == 254.0f / 255.0f);
}

TEST_CASE("NightLook: night weight from the sun over the camera's horizon", "[autoexpo][night]")
{
	auto W = [] (float deg, float cosDip = 1.0f) { return NightWeight(std::sin(deg * 0.01745329f), cosDip); };
	CHECK(W(3.05f) == 0.0f);
	CHECK(W(30.0f) == 0.0f);
	CHECK(W(-9.0f) > 0.999f);
	CHECK(W(-40.0f) == 1.0f);
	CHECK(std::fabs(W(-3.0f) - 0.5f) < 0.02f);
	float prev = 0.0f;
	for (float e = 5.0f; e >= -12.0f; e -= 0.25f) { CHECK(W(e) >= prev); prev = W(e); }
	float dip400 = 6371.0f / (6371.0f + 400.0f); // LEO: the camera is still in the sun with the sun 15 deg under its nadir's horizon
	CHECK(W(-15.0f, dip400) == 0.0f);
	CHECK(W(-35.0f, dip400) == 1.0f);
	CHECK(NightWeightAt(0.0, 6371e3, 0.0, 0.0, -1.0, 0.0, 6371e3) == 1.0f); // midnight on the ground
	CHECK(NightWeightAt(0.0, 6371e3, 0.0, 0.0, 1.0, 0.0, 6371e3) == 0.0f);
	CHECK(NightWeight(std::nanf(""), 1.0f) == 0.0f);
}

TEST_CASE("NightLook: a dark night city is not lifted to the day band", "[autoexpo][night]")
{
	Sim s(0.012f), legacy(0.012f);
	s.c.SetParams(NightParams());
	s.c.SetNight(1.0f);
	float prev = 0.0f;
	bool mono = true;
	for (int i = 0; i < 30 * 60; i++) {
		s.Step(); legacy.Step();
		if (s.c.EV() < prev - 1e-6f) mono = false;
		prev = s.c.EV();
	}
	CHECK(mono);
	CHECK(s.c.EV() <= std::log2(0.015f / 0.012f) + 0.05f);
	CHECK(s.c.EV() > 0.2f);
	CHECK(legacy.c.EV() > 2.4f);
	Sim dusk(0.012f); // half night: the low edge 1.5 EV lower
	dusk.c.SetParams(NightParams());
	dusk.c.SetNight(0.5f);
	dusk.Run(30.0f);
	CHECK(std::fabs(dusk.c.EV() - 1.75f) < 0.05f);
}

TEST_CASE("NightLook: dark countryside stops at the night clamp, bright content is legacy", "[autoexpo][night]")
{
	Sim s(0.001f);
	s.c.SetParams(NightParams());
	s.c.SetNight(1.0f);
	s.Run(40.0f);
	CHECK(std::fabs(s.c.EV() - 1.0f) < 0.02f);
	Sim lit(0.4f), legacy(0.4f); // a sunlit vessel or the day side seen from a night camera
	lit.c.SetParams(NightParams());
	lit.c.SetNight(1.0f);
	lit.Run(20.0f); legacy.Run(20.0f);
	CHECK(std::fabs(lit.c.EV() - legacy.c.EV()) < 1e-4f);
	Sim hot(0.55f * 4.0f), hotL(0.55f * 4.0f); // over the band at night: falls as legacy
	hot.linear = hotL.linear = true;
	hot.c.SetParams(NightParams());
	hot.c.SetNight(1.0f);
	hot.Run(10.0f); hotL.Run(10.0f);
	CHECK(std::fabs(hot.c.EV() - hotL.c.EV()) < 1e-4f);
}

TEST_CASE("NightLook: near-saturated lights brake the rise", "[autoexpo][night]")
{
	Sim s(0.005f);
	s.c.SetParams(NightParams());
	s.c.SetNight(1.0f);
	s.sat = 0.04f; // past brake1: no rise at all
	float prev = 0.0f;
	bool never = true;
	for (int i = 0; i < 20 * 60; i++) {
		s.Step();
		if (s.c.EV() > prev + 1e-6f) never = false;
		prev = s.c.EV();
	}
	CHECK(never);
	CitySim c(0.002f); // the share grows with e: the rise stops where the lights reach brake1
	c.c.SetParams(NightParams());
	c.c.SetNight(1.0f);
	for (int i = 0; i < 60 * 60; i++) { c.sat = c.SatAt(c.c.Exposure()); c.Step(); }
	CHECK(c.SatAt(c.c.Exposure()) < 0.031f);
	CHECK(c.c.EV() < 0.7f);
	CHECK(c.c.EV() > 0.3f);
	Sim d(0.3f); // in the band after a bright scene: the drift back up is braked too
	d.c.SetParams(NightParams());
	d.c.SetNight(1.0f);
	d.L0 = 3.0f; d.linear = true;
	d.Run(10.0f);
	float low = d.c.EV();
	CHECK(low < -0.5f);
	d.L0 = 0.3f; d.sat = 0.04f;
	d.Run(10.0f);
	CHECK(d.c.EV() <= low + 1e-4f);
}

TEST_CASE("NightLook: saturation threshold and the downward rule at night", "[autoexpo][night]")
{
	Controller c(NightParams());
	float prev = 254.0f / 255.0f;
	for (float n = 0.0f; n <= 1.0f; n += 0.1f) { c.SetNight(n); CHECK(c.SatThreshold() <= prev + 1e-6f); prev = c.SatThreshold(); }
	c.SetNight(1.0f);
	CHECK(std::fabs(c.SatThreshold() - 0.9f) < 1e-6f);
	Sim s(0.04f); // many blown 2x2 blocks: e falls as legacy
	s.c.SetParams(NightParams());
	s.c.SetNight(1.0f);
	s.sat = 0.3f;
	s.Run(10.0f);
	CHECK(s.c.EV() < -0.1f);
}

TEST_CASE("NightLook: building cell dusk and window on-times", "[night]")
{
	for (float h = 0.0f; h < 1.0f; h += 0.1f) {
		CHECK(NightLook::CellDusk(2.0f, h) == 0.0f);
		CHECK(NightLook::CellDusk(10.0f, h) == 0.0f);
		CHECK(NightLook::CellDusk(-8.0f, h) == 1.0f);
		CHECK(NightLook::CellDusk(-30.0f, h) == 1.0f);
		float prev = 0.0f;
		for (float e = 3.0f; e >= -10.0f; e -= 0.5f) { CHECK(NightLook::CellDusk(e, h) >= prev); prev = NightLook::CellDusk(e, h); }
	}
	for (float lod = -3.0f; lod <= 10.0f; lod += 1.0f)
		for (float a = 0.05f; a <= 0.85f + 1e-6f; a += 0.05f) {
			CHECK(NightLook::WindowOn(a, 0.0f, lod) == 0.0f); // dark by day at any lod
			CHECK(NightLook::WindowOn(a, 1.0f, lod) > 0.999f); // all on at night
			float prev = 0.0f;
			for (float D = 0.0f; D <= 1.0f; D += 0.02f) { CHECK(NightLook::WindowOn(a, D, lod) >= prev - 1e-6f); prev = NightLook::WindowOn(a, D, lod); }
			if (lod <= 0.0f) CHECK(NightLook::WindowOn(a, a - 0.04f, lod) < 0.001f); // a sharp step close up, not inverted
		}
	CHECK(NightLook::WindowOn(1.0f, 1.0f, 0.0f) < 0.51f);
}

TEST_CASE("NightLook: lamp field keeps its energy at every filter level", "[night]")
{
	using namespace NightLook;
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> U(0.0f, 1024.0f);
	struct F { float fx, fy, tol; bool head; };
	F cases[] = {{0.001f, 0.001f, 0.03f, false}, {0.03f, 0.03f, 0.04f, true}, {0.01f, 0.09f, 0.04f, true}, {0.09f, 0.01f, 0.04f, true},
		{0.05f, 0.2f, 0.03f, true}, {0.25f, 0.05f, 0.03f, true}, {0.3f, 0.3f, 0.03f, true}, {0.5f, 0.5f, 1e-4f, true}};
	for (float pSeg : {SegShare(0.0f), SegShare(1.0f)})
		for (const F &f : cases) {
			double sp = 0, sh = 0, ss = 0, cg = 0;
			const int n = f.head ? 400000 : 1000000; // fixed counts: the head mean's error is under 2 % once the heads are a pixel wide
			for (int i = 0; i < n; i++) {
				float wx, wy;
				Warp(U(rng), U(rng), wx, wy);
				Lamps l = LampField(wx, wy, f.fx, f.fy, pSeg);
				sp += l.pool; sh += l.head; ss += l.spill; cg += l.col[1] * l.pool;
			}
			Lamps m = LampMeans(pSeg);
			CHECK(std::fabs(sp / n / m.pool - 1.0) < f.tol);
			CHECK(std::fabs(ss / n / m.spill - 1.0) < f.tol);
			if (f.head) CHECK(std::fabs(sh / n / m.head - 1.0) < f.tol + 0.01);
			CHECK(std::fabs(cg / sp - m.col[1]) < 0.02);
		}
	Lamps far = LampField(100.3f, 200.7f, 0.4f, 0.4f, 0.7f), mean = LampMeans(0.7f);
	CHECK(std::fabs(far.pool - mean.pool) < 1e-6f);
	float wx, wy, ax, ay, bx, by;
	Warp(12.3f, 45.6f, wx, wy); Warp(12.3f + 1024.0f, 45.6f, ax, ay); Warp(12.3f, 45.6f + 1024.0f, bx, by);
	CHECK(std::fabs(ax - 1024.0f - wx) < 1e-3f); CHECK(std::fabs(ay - wy) < 1e-3f);
	CHECK(std::fabs(by - 1024.0f - wy) < 1e-3f);
	for (int i = 0; i < 2000; i++) { // the pattern repeats with the 16384-cell lattice: 1024 blocks
		float x = U(rng), y = U(rng);
		Lamps p = LampField(x, y, 0.01f, 0.01f, 0.8f), q = LampField(x + 1024.0f, y + 1024.0f, 0.01f, 0.01f, 0.8f);
		CHECK(std::fabs(p.pool - q.pool) < 2e-3f * (1.0f + p.pool));
	}
	int lit = 0;
	for (int i = 0; i < 1024; i++) for (int j = 0; j < 64; j++) lit += ClsHash(float(i) + 0.5f, float(j) + 0.5f) < 0.7f;
	CHECK(std::fabs(lit / (1024.0 * 64.0) - 0.7) < 0.02);
	CHECK(CityLevel(0.0f, 0.0f, 0.0f) == 0.0f);
	CHECK(std::fabs(CityLevel(1.0f, 0.98f, 0.94f) - 1.0f) < 1e-6f);
	CHECK(CityLevel(0.5f, 0.4f, 0.3f) < 0.25f);
}

TEST_CASE("NightLook: night map mips keep points and the expected light", "[night]")
{
	using namespace NightLook;
	const int w = 256;
	std::vector<uint8_t> l0((size_t)w * w * 4, 0);
	std::mt19937 rng(3);
	std::uniform_real_distribution<float> U(0.0f, 1.0f);
	for (int by = 0; by < w; by += 8) // 8x8 cells, a 4x5 window in each, 40 % lit with its own on-time
		for (int bx = 0; bx < w; bx += 8) {
			bool on = U(rng) < 0.4f;
			uint8_t a = (uint8_t)(255.0f * (0.05f + 0.8f * U(rng))), r = (uint8_t)(150 + 100 * U(rng));
			for (int y = by; y < by + 8; y++)
				for (int x = bx; x < bx + 8; x++) {
					uint8_t *p = &l0[((size_t)y * w + x) * 4];
					bool glass = x - bx >= 2 && x - bx < 6 && y - by >= 1 && y - by < 6;
					p[3] = on ? a : 0;
					if (on && glass) { p[0] = r; p[1] = (uint8_t)(r * 0.7f); p[2] = (uint8_t)(r * 0.4f); }
				}
		}
	auto Lit = [] (const std::vector<uint8_t> &img, float D) { double s = 0; for (size_t i = 0; i < img.size(); i += 4) if (img[i + 3] / 255.0f < D) s += img[i]; return s / (img.size() / 4); };
	std::vector<uint8_t> h1, cur = l0, nxt;
	NiteHalf(l0, w, w, h1);
	for (int i = 0; i < 64; i++) { // a lit and a dark texel: the on-time of the lit one
		int x = (i * 37) % (w / 2), y = (i * 53) % (w / 2);
		const uint8_t *o = &h1[((size_t)y * (w / 2) + x) * 4];
		bool any = false;
		for (int k = 0; k < 4; k++) { const uint8_t *c = &l0[((size_t)(2 * y + k / 2) * w + 2 * x + k % 2) * 4]; if (c[0] > 0) { any = true; CHECK(std::abs(int(o[3]) - int(c[3])) <= 1); } }
		if (!any) CHECK(o[0] == 0);
	}
	int cw = w;
	for (int level = 1; level <= 3; level++) { NiteHalf(cur, cw, cw, nxt); cur.swap(nxt); cw /= 2; }
	CHECK(std::fabs(Lit(cur, 2.0f) - Lit(l0, 2.0f)) < 0.01 * Lit(l0, 2.0f) + 0.5);
	for (int level = 4; level <= 6; level++) {
		std::vector<uint8_t> prev = cur;
		NiteNest(prev, cw, cw, level, nxt);
		int nw = cw / 2;
		for (int y = 0; y < nw; y++)
			for (int x = 0; x < nw; x++) { // each texel is one of its children
				const uint8_t *o = &nxt[((size_t)y * nw + x) * 4];
				bool found = false;
				for (int k = 0; k < 4; k++) { const uint8_t *c = &prev[((size_t)(2 * y + k / 2) * cw + 2 * x + k % 2) * 4]; if (!memcmp(o, c, 4)) found = true; }
				CHECK(found);
			}
		cur.swap(nxt); cw = nw;
	}
	std::vector<uint8_t> big((size_t)1024 * 1024 * 4, 0); // the expected value over many texels, at night and at dusk
	for (size_t i = 0; i < big.size(); i += 4) { bool on = U(rng) < 0.3f; big[i] = on ? 200 : 0; big[i + 3] = (uint8_t)(255.0f * U(rng)); }
	std::vector<uint8_t> n1;
	NiteNest(big, 1024, 1024, 4, n1);
	CHECK(std::fabs(Lit(n1, 2.0f) / Lit(big, 2.0f) - 1.0) < 0.02);
	CHECK(std::fabs(Lit(n1, 0.5f) / Lit(big, 0.5f) - 1.0) < 0.03);
}

TEST_CASE("NightLook: the cockpit keeps the day band, the brake stays", "[autoexpo][night]")
{
	Sim vc(0.03f), legacy(0.03f);
	vc.c.SetParams(NightParams());
	vc.c.SetNight(1.0f, true);
	vc.Run(30.0f); legacy.Run(30.0f);
	CHECK(std::fabs(vc.c.EV() - legacy.c.EV()) < 1e-4f);
	Sim lit(0.005f);
	lit.c.SetParams(NightParams());
	lit.c.SetNight(1.0f, true);
	lit.sat = 0.1f; // the city through the canopy near saturation, past the cockpit's 3x window: no rise
	lit.Run(20.0f);
	CHECK(lit.c.EV() <= 1e-6f);
}

TEST_CASE("NightLook: a far camera on the sunward side is not in the night", "[autoexpo][night]")
{
	float cd = std::cos(88.0f * 0.01745329f); // r about 29 R: dip 88 deg
	CHECK(NightWeight(std::sin(89.5f * 0.01745329f), cd) == 0.0f); // elevation + dip 177.5 deg would fold back to sin 2.5 deg
	CHECK(NightWeight(std::sin(60.0f * 0.01745329f), cd) == 0.0f);
	CHECK(NightWeight(std::sin(-89.9f * 0.01745329f), cd) > 0.3f); // sun 1.9 deg under the camera's own horizon
	CHECK(NightWeight(std::sin(-80.0f * 0.01745329f), cd) == 0.0f); // 8 deg over it
}
