// not upstream: collision addon, unit tests of the counted CollSdk calls on the fake (design E2-U13)
#include <catch2/catch_test_macros.hpp>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include "CollFakeSdk.h"

TEST_CASE ("E2-U13 counted calls count and log", "[CollSdk]")
{
	CollFakeSdk s;
	auto *v = s.AddVessel ("GL-01");
	s.simT = 1.5;
	s.logLevel = 1;
	s.SetState (v, CollStateWrite {});
	REQUIRE (s.log.empty ());
	s.logLevel = 2;
	s.SetSpin (v, Vector ());
	REQUIRE (s.log.back () == "Collision write t=1.5 'GL-01' spin");
	s.SetWarp (10);
	REQUIRE (s.log.back () == "Collision write t=1.5 '-' warp");
	CollH tk = s.CreateTank (v, 10, 5);
	s.SetTankMass (v, tk, 1);
	s.SetTank (v, nullptr, tk);
	s.DelTank (v, tk);
	REQUIRE (s.Count ().n[CSK_TANK] == 4);
	REQUIRE (s.Count ().Writes () == 7);
	size_t nlog = s.log.size ();
	REQUIRE (s.ProbeSlot (v, 0) == false);
	REQUIRE (s.Count ().n[CSK_PROBE] == 1);
	REQUIRE (s.Count ().Writes () == 7);
	REQUIRE (s.log.size () == nlog);
	REQUIRE (s.misuse == 0);
}

TEST_CASE ("E2-U13 notification fallback and annotation timeout", "[CollSdk]")
{
	CollFakeSdk s (false);
	s.Notification (COLLN_WARNING, "Collision", "GL-01 destroyed");
	REQUIRE (s.noteCalls == 0);
	REQUIRE (s.LogCount ("Collision notice: Collision: GL-01 destroyed") == 1);
	REQUIRE (s.annotation == "Collision: GL-01 destroyed");
	s.sysT = 7.9; s.UiTick ();
	REQUIRE (!s.annotation.empty ());
	s.sysT = 8.1; s.UiTick ();
	REQUIRE (s.annotation.empty ());
	REQUIRE (s.OpenDialog (nullptr) == false);
	CollFakeSdk t (true);
	t.Notification (COLLN_INFO, "a", "b");
	REQUIRE (t.noteCalls == 1);
	REQUIRE (t.OpenDialog (nullptr) == true);
}

TEST_CASE ("E2-U13 command balance", "[CollSdk]")
{
	CollFakeSdk s;
	int a = s.RegisterCmd ("Collision", "desc", nullptr, nullptr);
	int b = s.RegisterCmd ("Collision2", "desc", nullptr, nullptr);
	REQUIRE (s.Count ().cmds == 2);
	s.UnregisterCmd (a); s.UnregisterCmd (b);
	REQUIRE (s.Count ().cmds == 0);
	REQUIRE (s.Count ().Writes () == 0);
}

TEST_CASE ("fix1 E: CollGuard stops every exception and reports it to the hook", "[CollSdk]")
{
	static std::vector<std::string> got;
	got.clear ();
	g_collFail = [] (const char *where, const char *what) { got.push_back (std::string (where) + ": " + what); };
	CHECK (CollGuard ("a", -1, [] () -> int { throw std::runtime_error ("boom"); }) == -1);
	CHECK (CollGuard ("b", -1, [] () -> int { throw 7; }) == -1);
	CHECK (CollGuard ("c", -1, [] { return 3; }) == 3);
	bool after = false;
	CollGuard ("d", [&] { throw std::bad_alloc (); after = true; });
	CHECK_FALSE (after);
	CollGuard ("e", [&] { after = true; });
	CHECK (after);
	CHECK (got == std::vector<std::string> { "a: boom", "b: unknown exception", "d: std::bad_alloc" });
	g_collFail = nullptr;
	CHECK (CollGuard ("f", 0, [] () -> int { throw std::logic_error ("x"); }) == 0); // no hook: dropped
	CHECK (got.size () == 3);
}
