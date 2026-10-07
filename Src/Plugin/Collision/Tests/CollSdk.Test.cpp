// not upstream: collision addon, unit tests of the counted CollSdk calls on the fake (design E2-U13)
#include <catch2/catch_test_macros.hpp>
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
