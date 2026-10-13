// not upstream: auto-exposure controller (OVP/VulkanClient/AutoExpo.cpp)

#include <catch2/catch_test_macros.hpp>
#include "AutoExpo.h"
#include <cmath>
#include <deque>
#include <utility>

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
