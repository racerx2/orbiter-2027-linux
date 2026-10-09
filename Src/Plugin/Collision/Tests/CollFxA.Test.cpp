// not upstream: collision addon, dmg3 area F unit tests: flakes, vent, sparks, dust, caps, cleanup (design-CA-dmg3-F 8)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include "CollFakeSdk.h"
#include "CollFxA.h"
#include "CollSolve.h"

using Catch::Approx;

namespace {

const DentMaterial mAl { "al_skin", 0.2e6, 0.3, 1, 0.3, 1, 0.5, false };
const DentMaterial mSteel { "steel_structure", 1e6, 2, 1, 0.3, 1, 0.5, false };
const DentMaterial mGear { "gear", 0.5e6, 0.3, 3, 0.1, 3, 0.5, false };
const DentMaterial mConc { "concrete", 30e6, 0.3, 1, 0.2, 1, 0.6, false };
const DentMaterial mGlass { "glass", 0.5e6, 0.5, 1, 0.3, 1, 0.5, true };

struct Env {
	CollFakeSdk sdk;
	CollCfgValues cfg;
	std::map<uint32_t, CollH> ids;
	std::set<uint32_t> dead;
	std::unique_ptr<CollFxA> fx;
	Env ()
	{
		cfg.logLevel = 2;
		sdk.simT = 10;
		CollFxWorld w;
		w.handle = [this] (uint32_t id) { auto it = ids.find (id); return it == ids.end () ? (CollH)nullptr : it->second; };
		w.destroyed = [this] (uint32_t id) { return dead.count (id) > 0; };
		w.wrecks = [this] (std::vector<std::pair<uint32_t, CollH>> &o) { o.clear (); for (auto id : dead) if (ids.count (id)) o.push_back ({ id, ids[id] }); };
		fx = std::make_unique<CollFxA> (sdk, cfg, std::move (w));
	}
	CollH Add (uint32_t id, const char *name, double prop = 0, const char *cls = "")
	{
		auto *v = sdk.AddVessel (name, cls);
		if (prop > 0) { v->tank.push_back ({ 2 * prop, prop, true }); v->tankList.push_back (&v->tank.back ()); }
		ids[id] = v;
		return v;
	}
	CollDamageHit Hit (uint32_t id, double vn, double E, double eSpec = 0, double t = 10)
	{
		CollDamageHit x;
		x.id = id; x.h = ids[id]; x.c = Vector (1, 0, 0); x.n = Vector (0, 0, 1); x.vn = vn; x.E = E; x.eSpec = eSpec; x.simt = t;
		return x;
	}
	CollFxContact Scrape (uint32_t id, double vt, double Jt, const DentMaterial *a, const DentMaterial *b, Vector tdir = Vector (1, 0, 0))
	{
		CollFxContact c;
		c.id = id; c.h = ids[id]; c.c = Vector (0, -2, 0); c.n = Vector (0, -1, 0); c.tdir = tdir;
		c.vn = 0.5; c.vt = vt; c.Jn = 100; c.Jt = Jt; c.dt = 0.02; c.mat = a; c.matOther = b;
		return c;
	}
	void Step (double dt = 0.02) { sdk.simT += dt; fx->Post (sdk.simT, dt); }
	int Slot (CollH h, uint8_t k) const { return fx->Find (h, k); }
	bool Logged (const std::string &s) const { for (auto &l : sdk.log) if (l.find (s) != std::string::npos) return true; return false; }
};

}

TEST_CASE ("F-1 flakes: one stream, spec from the hit, deleted after 0.5 s")
{
	Env e;
	CollH h = e.Add (1, "A");
	e.fx->Hit (e.Hit (1, 20, 20e3));
	REQUIRE (e.sdk.fx.size () == 1);
	const auto &f = e.sdk.fx[0];
	CHECK (f.v == h);
	CHECK (f.s.ltype == CollSdk::FX_DIFFUSE);
	CHECK (f.s.lmap == CollSdk::FX_LVL_FLAT);
	CHECK (f.s.tex == CollSdk::FX_TEX_FLAKE);
	CHECK (f.s.v0 == Approx (5));
	CHECK (f.s.rate == Approx (25));
	CHECK (f.s.size == Approx (0.3));
	CHECK (f.s.life == Approx (8));
	CHECK (f.dir.x == Approx (0)); CHECK (f.dir.z == Approx (1));
	CHECK (f.pos.x == Approx (1)); CHECK (f.pos.z == Approx (0.5));
	CHECK (*f.lvl == 1);
	CHECK (e.Logged ("Collision fx t=10 'A' kind=flakes lvl=1 ps=1"));
	e.fx->Post (10.3, 0.3);
	CHECK (e.sdk.fxDels == 0);
	e.fx->Post (10.6, 0.3);
	CHECK (e.sdk.fxDels == 1);
	CHECK (!e.sdk.fx[0].alive);
	CHECK (e.fx->Live () == 0);
	CHECK (e.Logged ("kind=flakes del"));
	CHECK (e.fx->Counters ().requests == 1);
	CHECK (e.fx->Counters ().dels == 1);
	CHECK (e.sdk.misuse == 0);
}

TEST_CASE ("F-1b flakes: gates, slip drag, stronger hit replaces")
{
	Env e;
	e.Add (1, "A");
	e.fx->Hit (e.Hit (1, 2.9, 20e3));
	e.fx->Hit (e.Hit (1, 20, 1999));
	CHECK (e.sdk.fx.empty ());
	auto x = e.Hit (1, 4, 4e3);
	x.tdir = Vector (1, 0, 0); x.vt = 8;
	e.fx->Hit (x);
	REQUIRE (e.sdk.fx.size () == 1);
	CHECK (e.sdk.fx[0].dir.x == Approx (-std::sqrt (0.5)));
	CHECK (e.sdk.fx[0].s.rate == Approx (3 / 0.4));
	e.fx->Hit (e.Hit (1, 4, 3e3));
	CHECK (e.sdk.fx.size () == 1);
	e.fx->Hit (e.Hit (1, 30, 100e3));
	REQUIRE (e.sdk.fx.size () == 2);
	CHECK (!e.sdk.fx[0].alive);
	CHECK (e.sdk.fx[1].s.rate == Approx (25 / 0.4));
	CHECK (e.sdk.fx[1].s.v0 == Approx (7.5));
	CHECK (e.fx->Live () == 1);
}

TEST_CASE ("F-2 vent: L0 from eSpec, exp decay, deleted below 0.1, needs propellant")
{
	Env e;
	CollH h = e.Add (1, "A", 100);
	e.Add (2, "B");
	e.fx->Hit (e.Hit (1, 1, 1000, 300));
	e.fx->Hit (e.Hit (2, 1, 1000, 300));
	REQUIRE (e.sdk.fx.size () == 1);
	int i = e.Slot (h, CFX_VENT);
	REQUIRE (i >= 0);
	CHECK (e.sdk.fx[0].s.tex == CollSdk::FX_TEX_VENT);
	CHECK (e.sdk.fx[0].s.lmap == CollSdk::FX_LVL_LIN);
	CHECK (*e.sdk.fx[0].lvl == Approx (0.5));
	e.fx->Post (16, 6);
	CHECK (std::fabs (e.fx->Slots ()[i].lvl - 0.5 / std::exp (1.0)) < 1e-12);
	e.fx->Post (19.6, 3.6);
	CHECK (e.sdk.fx[0].alive);
	e.fx->Post (19.8, 0.2);
	CHECK (!e.sdk.fx[0].alive);
	CHECK (e.fx->Live () == 0);
	e.fx->Hit (e.Hit (1, 1, 1000, 149));
	CHECK (e.sdk.fx.size () == 1);
}

TEST_CASE ("F-2b vent: retrigger keeps the stream, surplus counts, destroyed vents at the last hit")
{
	Env e;
	CollH h = e.Add (1, "A", 100);
	e.fx->Hit (e.Hit (1, 1, 1000, 300));
	e.fx->Post (16, 6);
	e.fx->Hit (e.Hit (1, 1, 1000, 240, 16));
	REQUIRE (e.sdk.fx.size () == 1);
	CHECK (e.fx->Slots ()[e.Slot (h, CFX_VENT)].L0 == Approx (0.4));
	Env g;
	g.Add (1, "A", 100);
	auto x = g.Hit (1, 1, 1000, 100);
	x.Esurplus = 1000;
	g.fx->Hit (x);
	REQUIRE (g.sdk.fx.size () == 1);
	CHECK (*g.sdk.fx[0].lvl == Approx (200.0 / 600));
	Env d;
	d.Add (1, "A", 100);
	d.Add (2, "B");
	auto y = d.Hit (1, 1, 100, 1);
	y.c = Vector (0, 0, 7);
	d.fx->Hit (y);
	d.fx->Destroyed (1);
	d.fx->Destroyed (2);
	REQUIRE (d.sdk.fx.size () == 1);
	CHECK (d.sdk.fx[0].pos.z == Approx (7.5));
	CHECK (*d.sdk.fx[0].lvl == Approx (1));
}

TEST_CASE ("F-3 air and vacuum tables")
{
	for (double rho : { 0.0, 1.2 }) {
		Env e;
		CollH h = e.Add (1, "A", 100);
		e.sdk.atmF[(const CollFakeSdk::Ves *)h] = rho;
		bool air = rho > 0;
		e.fx->Hit (e.Hit (1, 20, 20e3, 300));
		REQUIRE (e.sdk.fx.size () == 2);
		CHECK (e.sdk.fx[0].s.life == Approx (air ? 3 : 8));
		CHECK (e.sdk.fx[1].s.life == Approx (air ? 5 : 2.5));
		CHECK (e.sdk.fx[1].s.grow == Approx (air ? 1 : 3));
		e.fx->Contact (e.Scrape (1, 10, 100, &mSteel, nullptr));
		REQUIRE (e.sdk.fx.size () == 3);
		CHECK (e.sdk.fx[2].s.rate == Approx (air ? 60 : 25 * 0.3));
		CHECK (e.sdk.fx[2].s.life == Approx (air ? 0.25 : 0.5));
	}
}

TEST_CASE ("F-4 sparks: yield in the rate, held while refreshed, decays, recreated on a turn")
{
	Env e;
	CollH h = e.Add (1, "A");
	e.sdk.atmF[(const CollFakeSdk::Ves *)h] = 1.2;
	e.fx->Contact (e.Scrape (1, 2, 1000, &mSteel, &mConc));
	e.fx->Contact (e.Scrape (1, 10, 0.5, &mSteel, &mConc));
	e.fx->Contact (e.Scrape (1, 10, 100, &mGlass, nullptr));
	CHECK (e.sdk.fx.empty ());
	e.fx->Contact (e.Scrape (1, 10, 100, &mAl, &mConc));
	REQUIRE (e.sdk.fx.size () == 1);
	const auto &f = e.sdk.fx[0];
	CHECK (f.s.ltype == CollSdk::FX_EMISSIVE);
	CHECK (f.s.tex == CollSdk::FX_TEX_SPARK);
	CHECK (f.s.rate == Approx (60 * 0.25));
	CHECK (f.s.v0 == Approx (5));
	CHECK (*f.lvl == Approx (0.8));
	CHECK (f.dir.x == Approx (-1 / std::sqrt (1.09)));
	CHECK (f.dir.y == Approx (-0.3 / std::sqrt (1.09)));
	for (int k = 0; k < 10; k++) { e.Step (); e.fx->Contact (e.Scrape (1, 10, 100, &mAl, &mConc)); }
	e.Step ();
	CHECK (e.sdk.fx.size () == 1);
	CHECK (*e.sdk.fx[0].lvl == Approx (0.8));
	double tLast = e.sdk.simT;
	for (int k = 0; k < 5; k++) e.Step ();
	CHECK (e.sdk.fx[0].alive);
	CHECK (*e.sdk.fx[0].lvl == Approx (0.8 * std::exp (-(e.sdk.simT - tLast) / 0.1)));
	while (e.sdk.fx[0].alive && e.sdk.simT < tLast + 1) e.Step ();
	CHECK (!e.sdk.fx[0].alive);
	CHECK (e.sdk.simT - tLast <= 0.3 + 1e-9);
	e.fx->Contact (e.Scrape (1, 10, 100, &mAl, &mConc));
	REQUIRE (e.sdk.fx.size () == 2);
	e.Step ();
	e.fx->Contact (e.Scrape (1, 10, 100, &mAl, &mConc, Vector (std::sqrt (0.5), 0, std::sqrt (0.5))));
	REQUIRE (e.sdk.fx.size () == 3);
	CHECK (!e.sdk.fx[1].alive);
	CHECK (e.sdk.fx[2].alive);
	CHECK (e.sdk.misuse == 0);
}

TEST_CASE ("F-4b yields: gear only when destroyed or hard, vacuum factor in the rate")
{
	CHECK (CollFxA::Yield (&mGear, false) == 0);
	CHECK (CollFxA::Yield (&mGear, true) == Approx (0.6));
	CHECK (CollFxA::Yield (&mSteel, false) == 1);
	CHECK (CollFxA::Yield (&mAl, false) == Approx (0.25));
	CHECK (CollFxA::Yield (&mConc, false) == Approx (0.15));
	CHECK (CollFxA::Yield (&mGlass, false) == 0);
	CHECK (CollFxA::Yield (nullptr, true) == 0);
	Env e;
	e.Add (1, "A");
	e.fx->Contact (e.Scrape (1, 10, 100, &mGear, nullptr));
	CHECK (e.sdk.fx.empty ());
	e.dead.insert (1);
	e.fx->Contact (e.Scrape (1, 10, 100, &mGear, nullptr));
	REQUIRE (e.sdk.fx.size () == 1);
	CHECK (e.sdk.fx[0].s.rate == Approx (25 * 0.3 * 0.6));
}

TEST_CASE ("F-5 dust: touchdown burst at 5 m/s, wreck scrape held, healthy roll only with RollDust")
{
	Env e;
	CollH a = e.Add (1, "A"), b = e.Add (2, "B");
	auto &ga = e.sdk.groundF[(const CollFakeSdk::Ves *)a];
	auto &gb = e.sdk.groundF[(const CollFakeSdk::Ves *)b];
	ga.vLoc = Vector (0, -6, 0); ga.alt = 2;
	gb.vLoc = Vector (0, -3, 0); gb.alt = 2;
	e.Step ();
	CHECK (e.sdk.fx.empty ());
	ga.on = gb.on = true; ga.vLoc = gb.vLoc = Vector ();
	e.Step ();
	REQUIRE (e.sdk.fx.size () == 1);
	CHECK (e.sdk.fx[0].v == a);
	CHECK (e.sdk.fx[0].s.tex == CollSdk::FX_TEX_DUST);
	CHECK (e.sdk.fx[0].s.v0 == Approx (1.8));
	CHECK (*e.sdk.fx[0].lvl == Approx (0.3));
	CHECK (e.sdk.fx[0].pos.y == Approx (-2));
	for (int k = 0; k < 35; k++) e.Step ();
	CHECK (!e.sdk.fx[0].alive);
	ga.vLoc = gb.vLoc = Vector (5, 0, 0);
	e.Step (); e.Step ();
	CHECK (e.sdk.fx.size () == 1);
	e.dead.insert (2);
	e.Step ();
	REQUIRE (e.sdk.fx.size () == 2);
	CHECK (e.sdk.fx[1].v == b);
	CHECK (*e.sdk.fx[1].lvl == Approx (0.25));
	CHECK (e.sdk.fx[1].dir.x == Approx (-1 / std::sqrt (1.16)));
	CHECK (e.sdk.fx[1].dir.y == Approx (-0.4 / std::sqrt (1.16)));
	for (int k = 0; k < 20; k++) e.Step ();
	CHECK (e.sdk.fx[1].alive);
	gb.vLoc = Vector ();
	for (int k = 0; k < 60; k++) e.Step ();
	CHECK (!e.sdk.fx[1].alive);
	e.cfg.fxRollDust = true;
	e.Step ();
	REQUIRE (e.sdk.fx.size () == 3);
	CHECK (e.sdk.fx[2].v == a);
}

TEST_CASE ("F-5b dust on an upward concrete building face")
{
	Env e;
	e.Add (1, "A");
	CollFxContact c = e.Scrape (1, 0, 0, &mAl, &mConc);
	c.building = true; c.nOther = Vector (0, 1, 0); c.flags = COLLEV_FIRST; c.vn = 4;
	e.fx->Contact (c);
	CHECK (e.sdk.fx.empty ());
	c.vn = 6;
	c.nOther = Vector (1, 0, 0);
	e.fx->Contact (c);
	CHECK (e.sdk.fx.empty ());
	c.nOther = Vector (0, 1, 0);
	e.fx->Contact (c);
	REQUIRE (e.sdk.fx.size () == 1);
	CHECK (e.sdk.fx[0].s.tex == CollSdk::FX_TEX_DUST);
	CHECK (*e.sdk.fx[0].lvl == Approx (0.3));
}

TEST_CASE ("F-6 caps: total, eviction by priority, per frame")
{
	Env e;
	for (uint32_t i = 1; i <= 30; i++) e.Add (i, ("V" + std::to_string (i)).c_str ());
	CollH p = e.Add (99, "P", 100);
	for (uint32_t i = 1; i <= 30; i++) { e.fx->Hit (e.Hit (i, 20, 20e3)); if (i % 4 == 0) e.fx->Post (10, 0); }
	CHECK (e.fx->Live () == 24);
	CHECK (e.fx->Counters ().capped == 6);
	e.fx->Post (10, 0);
	e.fx->Hit (e.Hit (99, 1, 100, 600));
	CHECK (e.fx->Live () == 24);
	CHECK (e.Slot (p, CFX_VENT) >= 0);
	CHECK (e.sdk.fxDels == 1);
	CHECK (e.sdk.misuse == 0);
	Env f;
	for (uint32_t i = 1; i <= 5; i++) { f.Add (i, "X"); f.fx->Hit (f.Hit (i, 20, 20e3)); }
	CHECK (f.fx->Live () == 4);
	CHECK (f.fx->Counters ().capped == 1);
	CHECK (f.fx->Counters ().requests == 5);
}

TEST_CASE ("F-7 no client: ghost slot, same requests, never deleted through the SDK")
{
	Env e;
	e.sdk.fxNull = true;
	e.Add (1, "A");
	e.fx->Hit (e.Hit (1, 20, 20e3));
	CHECK (e.sdk.fx.empty ());
	CHECK (e.fx->Counters ().nulls == 1);
	CHECK (e.fx->Counters ().requests == 1);
	CHECK (e.Logged ("kind=flakes lvl=1 ps=0"));
	e.fx->Post (10.6, 0.6);
	CHECK (e.sdk.fxDels == 0);
	CHECK (e.fx->Live () == 0);
	CHECK (e.Logged ("kind=flakes del"));
}

TEST_CASE ("F-8 DropVessel deletes that vessel's streams, other level addresses stay")
{
	Env e;
	CollH a = e.Add (1, "A"), b = e.Add (2, "B");
	e.fx->Hit (e.Hit (1, 20, 20e3));
	e.fx->Hit (e.Hit (2, 20, 20e3));
	const double *lb = e.sdk.fx[1].lvl;
	int ib = e.Slot (b, CFX_FLAKES);
	e.fx->DropVessel (1, a);
	CHECK (e.sdk.fxDels == 1);
	CHECK (!e.sdk.fx[0].alive);
	CHECK (e.sdk.fx[1].alive);
	CHECK (e.sdk.fx[1].lvl == lb);
	CHECK (&e.fx->Slots ()[ib].lvl == lb);
	e.fx->Hit (e.Hit (2, 1, 100, 600));
	e.fx->TimeJump ();
	CHECK (!e.sdk.fx[1].alive);
	CHECK (e.fx->Live () == 0);
	CHECK (e.sdk.misuse == 0);
}

TEST_CASE ("F-9 stale handle: counted and logged once, no misuse")
{
	Env e;
	CollH a = e.Add (1, "A");
	e.fx->Hit (e.Hit (1, 20, 20e3));
	e.sdk.FxDetach (a);
	e.fx->Post (10.6, 0.6);
	CHECK (e.fx->Counters ().stale == 1);
	CHECK (e.fx->Counters ().dels == 0);
	CHECK (e.sdk.misuse == 0);
	CHECK (e.Logged ("stale particle stream of 'A'"));
	CHECK (e.fx->Live () == 0);
}

TEST_CASE ("F-10 End makes no SDK call, Quiet zeroes every level")
{
	Env e;
	e.Add (1, "A", 100);
	e.fx->Hit (e.Hit (1, 20, 20e3, 600));
	REQUIRE (e.sdk.fx.size () == 2);
	e.fx->Quiet ();
	for (auto &s : e.fx->Slots ()) CHECK (s.lvl == 0);
	CHECK (*e.sdk.fx[0].lvl == 0);
	CHECK (*e.sdk.fx[1].lvl == 0);
	e.fx->Hit (e.Hit (1, 30, 50e3, 900));
	e.fx->Post (11, 1);
	CHECK (e.sdk.fx.size () == 2);
	CHECK (*e.sdk.fx[1].lvl == 0);
	Env g;
	g.Add (1, "A", 100);
	g.fx->Hit (g.Hit (1, 20, 20e3, 600));
	g.sdk.ended = true;
	int adds = g.sdk.fxAdds, dels = g.sdk.fxDels, reads = g.sdk.fxReads;
	size_t logs = g.sdk.log.size ();
	g.fx->End ();
	CHECK (g.sdk.fxAdds == adds);
	CHECK (g.sdk.fxDels == dels);
	CHECK (g.sdk.fxReads == reads);
	CHECK (g.sdk.log.size () == logs);
	CHECK (g.sdk.misuse == 0);
	CHECK (g.fx->Live () == 0);
}

TEST_CASE ("F-11 playback with FxPlayback off does nothing; debris vessels never")
{
	Env e;
	e.cfg.fxPlayback = false;
	e.Add (1, "A", 100);
	auto x = e.Hit (1, 20, 20e3, 600);
	x.playback = true;
	e.fx->Hit (x);
	auto c = e.Scrape (1, 10, 100, &mSteel, nullptr);
	c.playback = true;
	e.fx->Contact (c);
	CollBreakEvent b; b.id = 1; b.kind = CBRK_PART; b.r = 1; b.playback = true;
	e.fx->Break (b);
	CHECK (e.sdk.fx.empty ());
	CHECK (e.fx->Counters ().requests == 0);
	x.playback = false;
	e.fx->Hit (x);
	CHECK (e.sdk.fx.size () == 2);
	Env d;
	CollH h = d.Add (1, "D", 100, "CollDEBRIS");
	d.sdk.groundF[(const CollFakeSdk::Ves *)h] = { true, Vector (9, 0, 0), Vector (0, 1, 0), 1 };
	d.cfg.fxRollDust = true;
	d.fx->Hit (d.Hit (1, 20, 20e3, 600));
	d.fx->Contact (d.Scrape (1, 10, 100, &mSteel, nullptr));
	d.Step ();
	CHECK (d.sdk.fx.empty ());
}

TEST_CASE ("F-12 warp above 10 creates nothing; mask and intensity")
{
	Env e;
	e.Add (1, "A", 100);
	e.sdk.warp = 100;
	e.fx->Hit (e.Hit (1, 20, 20e3, 600));
	CHECK (e.sdk.fx.empty ());
	CHECK (e.fx->Counters ().skipped == 2);
	e.sdk.warp = 10;
	e.cfg.fxMask = 2;
	e.fx->Hit (e.Hit (1, 20, 20e3, 600));
	REQUIRE (e.sdk.fx.size () == 1);
	CHECK (e.sdk.fx[0].s.tex == CollSdk::FX_TEX_VENT);
	e.cfg.fxIntensity = 0;
	e.fx->Hit (e.Hit (1, 30, 90e3, 900));
	CHECK (e.sdk.fx.size () == 1);
}

TEST_CASE ("F-13 break events: glass burst, part flakes and vent, interior nothing")
{
	Env e;
	CollH h = e.Add (1, "A", 100);
	CollBreakEvent b; b.id = 1; b.kind = CBRK_INTERIOR; b.c = Vector (0, 0, 5); b.n = Vector (0, 0, 1); b.r = 1.5;
	e.fx->Break (b);
	CHECK (e.sdk.fx.empty ());
	b.kind = CBRK_GLASS;
	e.fx->Break (b);
	REQUIRE (e.sdk.fx.size () == 1);
	CHECK (e.sdk.fx[0].s.ltype == CollSdk::FX_EMISSIVE);
	CHECK (e.Slot (h, CFX_GLASS) >= 0);
	e.fx->Post (10.4, 0.4);
	CHECK (!e.sdk.fx[0].alive);
	b.kind = CBRK_PART;
	e.fx->Break (b);
	REQUIRE (e.sdk.fx.size () == 3);
	CHECK (e.sdk.fx[1].s.tex == CollSdk::FX_TEX_FLAKE);
	CHECK (e.sdk.fx[1].s.rate == Approx (15 / 0.4));
	CHECK (e.sdk.fx[2].s.tex == CollSdk::FX_TEX_VENT);
	CHECK (*e.sdk.fx[2].lvl == Approx (0.8));
	CHECK (e.sdk.misuse == 0);
}
