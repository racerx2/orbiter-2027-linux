// custom: launcher skins; unit tests of the skin.cfg, Launcher.cfg and scenario facts parsers
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include "Custom/LauncherFacts.h"

namespace fs = std::filesystem;
using namespace custom;

struct TmpDir {
	fs::path root;
	TmpDir () {
		char tmpl[] = "/tmp/ob_launcher_XXXXXX";
		root = mkdtemp (tmpl);
	}
	~TmpDir () { std::error_code ec; fs::remove_all (root, ec); }
	fs::path Write (const std::string &rel, const std::string &text) const {
		fs::path p = root / rel;
		fs::create_directories (p.parent_path ());
		std::ofstream (p, std::ios::binary) << text;
		return p;
	}
};

TEST_CASE("ReadCfg: comments, BOM, CRLF, values with # and ;", "[launcher]")
{
	std::istringstream is ("\xEF\xBB\xBF; comment\r\n  # also a comment\r\nName = Horizon\r\n KEY=  a # b ; c  \r\nnoequals\r\n= empty key\r\n");
	auto e = ReadCfg (is);
	REQUIRE(e.size () == 2);
	CHECK(e[0].key == "name");
	CHECK(e[0].value == "Horizon");
	CHECK(e[1].key == "key");
	CHECK(e[1].value == "a # b ; c");
}

TEST_CASE("ReadSkin: a valid QSS skin and defaults", "[launcher]")
{
	TmpDir t;
	t.Write ("Dark/skin.cfg", "Qss = style.qss\n");
	t.Write ("Dark/style.qss", "QWidget {}\n");
	SkinManifest m = ReadSkin ((t.root / "Dark").string ());
	CHECK(m.ok);
	CHECK(m.id == "Dark");
	CHECK(m.name == "Dark");
	CHECK(m.api == 1);
	CHECK(m.qss == "style.qss");
	CHECK(m.dir == fs::canonical (t.root / "Dark").string ());
	SkinManifest m2 = ReadSkin ((t.root / "Dark/").string ());
	CHECK(m2.id == "Dark");
}

TEST_CASE("ReadSkin: rejects bad manifests", "[launcher]")
{
	TmpDir t;
	t.Write ("outside.qss", "x");
	t.Write ("A/skin.cfg", "Qss = /etc/hostname\n");
	t.Write ("B/skin.cfg", "Qss = ../outside.qss\n");
	t.Write ("C/skin.cfg", "Qml = missing.qml\n");
	t.Write ("D/skin.cfg", "Api = 2\nQss = s.qss\n");
	t.Write ("D/s.qss", "x");
	t.Write ("E/skin.cfg", "Name = Nothing\n");
	t.Write ("F/skin.cfg", "Api = zero\nQss = s.qss\n");
	t.Write ("F/s.qss", "x");
	fs::create_directories (t.root / "G");
	for (const char *d : {"A", "B", "C", "D", "E", "F", "G"}) {
		SkinManifest m = ReadSkin ((t.root / d).string ());
		INFO(d << ": " << m.reason);
		CHECK(!m.ok);
		CHECK(!m.reason.empty ());
	}
	CHECK(ReadSkin ((t.root / "D").string ()).reason.find ("newer") != std::string::npos);
	CHECK(!ReadSkin ((t.root / "missing").string ()).ok);
}

TEST_CASE("ReadSkin: QML skins may not link out or load plugins", "[launcher]")
{
	TmpDir t;
	t.Write ("ok/skin.cfg", "Qml = qml/Main.qml\n");
	t.Write ("ok/qml/Main.qml", "import QtQuick\nItem {}\n");
	t.Write ("ok/qml/qmldir", "singleton Theme 1.0 Theme.qml\n");
	CHECK(ReadSkin ((t.root / "ok").string ()).ok);

	t.Write ("plug/skin.cfg", "Qml = Main.qml\n");
	t.Write ("plug/Main.qml", "Item {}\n");
	t.Write ("plug/Mod/qmldir", "module Mod\noptional plugin evil\n");
	SkinManifest p = ReadSkin ((t.root / "plug").string ());
	CHECK(!p.ok);
	CHECK(p.reason.find ("plugin") != std::string::npos);

	t.Write ("link/skin.cfg", "Qml = Main.qml\n");
	t.Write ("link/Main.qml", "Item {}\n");
	fs::create_directory_symlink ("/etc", t.root / "link/etc");
	SkinManifest l = ReadSkin ((t.root / "link").string ());
	CHECK(!l.ok);
	CHECK(l.reason.find ("links outside") != std::string::npos);

	t.Write ("qmlout/skin.cfg", "Qml = Main.qml\n");
	fs::create_symlink (t.root / "ok/qml/Main.qml", t.root / "qmlout/Main.qml");
	CHECK(!ReadSkin ((t.root / "qmlout").string ()).ok);
}

TEST_CASE("Launcher.cfg round trip, recents and favourites", "[launcher]")
{
	TmpDir t;
	std::string path = (t.root / "Launcher.cfg").string ();
	LauncherCfg c;
	c.skin = "Horizon";
	AddRecent (c, "Delta-glider/Brighton Beach");
	AddRecent (c, "2024 Edition/# Welcome to Orbiter 2024");
	AddRecent (c, "Delta-glider/Brighton Beach");
	REQUIRE(c.recent.size () == 2);
	CHECK(c.recent[0] == "Delta-glider/Brighton Beach");
	CHECK(ToggleFavourite (c, "Demo/DG ISS Approach; x"));
	CHECK(!ToggleFavourite (c, ""));
	AddRecent (c, "bad\nline");
	REQUIRE(SaveLauncherCfg (path, c));
	CHECK(!fs::exists (path + ".tmp"));

	LauncherCfg r;
	REQUIRE(LoadLauncherCfg (path, r));
	CHECK(r.skin == "Horizon");
	REQUIRE(r.recent.size () == 2);
	CHECK(r.recent[1] == "2024 Edition/# Welcome to Orbiter 2024");
	REQUIRE(r.favourites.size () == 1);
	CHECK(r.favourites[0] == "Demo/DG ISS Approach; x");
	CHECK(!ToggleFavourite (r, "Demo/DG ISS Approach; x"));
	CHECK(r.favourites.empty ());

	for (int i = 0; i < 20; i++) AddRecent (r, "s" + std::to_string (i));
	CHECK(r.recent.size () == MAX_RECENT);
	CHECK(r.recent[0] == "s19");

	LauncherCfg none;
	none.skin = "x";
	CHECK(!LoadLauncherCfg ((t.root / "missing.cfg").string (), none));
	CHECK(none.skin.empty ());
}

TEST_CASE("ReadScenario: synthetic cases", "[launcher]")
{
	std::string scn =
		"BEGIN_DESC\r\nText\r\nEND_DESC\r\n"
		"BEGIN_ENVIRONMENT\r\n  System Sol\r\n  Date JD 2451545.0\r\nEND_ENVIRONMENT\r\n"
		"BEGIN_FOCUS\r\n  Ship gl-01\r\nEND_FOCUS\r\n"
		"BEGIN_SHIPS\r\n"
		"Mir\r\n  STATUS Orbiting Earth\r\nEND\r\n"
		"GL-01 : DeltaGlider\r\n  STATUS Landed Earth\r\n  BASE Habana : 2 \r\nEND\r\n"
		"X:\r\nEND\r\n"
		"END_SHIPS\r\n";
	std::istringstream is (scn);
	ScenarioFacts f = ReadScenario (is);
	CHECK(f.system == "Sol");
	CHECK(f.dateKind == 'J');
	CHECK(f.dateValue == Catch::Approx (2451545.0));
	CHECK(f.focus == "gl-01");
	CHECK(f.vesselCount == 3);
	REQUIRE(f.vessels.size () == 3);
	CHECK(f.vessels[0].name == "Mir");
	CHECK(f.vessels[0].cls == "Mir");
	CHECK(f.vessels[2].cls == "X");
	CHECK(f.focusClass == "DeltaGlider");
	CHECK(f.focusStatus == "Landed");
	CHECK(f.focusBody == "Earth");
	CHECK(f.focusBase == "Habana");
	CHECK(f.focusPad == 2);

	std::istringstream je ("BEGIN_ENVIRONMENT\nDate JE 2000.5\nEND_ENVIRONMENT\n");
	ScenarioFacts g = ReadScenario (je);
	CHECK(g.dateKind == 'E');
	CHECK(g.dateValue == Catch::Approx (2000.5));

	std::istringstream empty ("BEGIN_SHIPS\nA:B\n");
	ScenarioFacts h = ReadScenario (empty);
	CHECK(h.dateKind == 0);
	CHECK(h.vesselCount == 1);
	CHECK(h.focusStatus.empty ());

	std::string longline = "BEGIN_ENVIRONMENT\n" + std::string (1 << 20, 'x') + "\nDate MJD 51544.5\nEND_ENVIRONMENT\n";
	std::istringstream ll (longline);
	ScenarioFacts k = ReadScenario (ll);
	CHECK(k.dateKind == 'M');
	CHECK(k.dateValue == Catch::Approx (51544.5));

	std::istringstream bad ("BEGIN_ENVIRONMENT\nDate MJD\nDate JD nope\nEND_ENVIRONMENT\n");
	CHECK(ReadScenario (bad).dateKind == 0);
}

TEST_CASE("ReadScenario: stock scenarios", "[launcher]")
{
	const fs::path scn = SCN_DIR;
	{
		std::ifstream is (scn / "Delta-glider/Brighton Beach.scn");
		REQUIRE(is);
		ScenarioFacts f = ReadScenario (is);
		CHECK(f.system == "Sol");
		CHECK(f.dateKind == 'M');
		CHECK(f.dateValue == Catch::Approx (52006.7485132526));
		CHECK(f.focus == "GL-NT");
		CHECK(f.vesselCount == 8);
		CHECK(f.vessels[1].name == "Mir");
		CHECK(f.vessels[1].cls == "Mir");
		CHECK(f.focusClass == "DeltaGlider");
		CHECK(f.focusStatus == "Landed");
		CHECK(f.focusBody == "Moon");
		CHECK(f.focusBase == "Brighton Beach");
		CHECK(f.focusPad == 4);
	}
	{
		std::ifstream is (scn / "Demo/DG ISS Approach.scn");
		REQUIRE(is);
		ScenarioFacts f = ReadScenario (is);
		CHECK(f.focus == "DG-01");
		CHECK(f.vesselCount == 2);
		CHECK(f.focusStatus == "Orbiting");
		CHECK(f.focusBody == "Earth");
		CHECK(f.focusBase.empty ());
	}
	{
		std::ifstream is (scn / "2024 Edition/# Welcome to Orbiter 2024.scn");
		REQUIRE(is);
		ScenarioFacts f = ReadScenario (is);
		CHECK(f.focus == "GL-NT");
		CHECK(f.focusStatus == "Orbiting");
		CHECK(f.focusBody == "Moon");
		CHECK(f.vesselCount == 8);
	}
}
