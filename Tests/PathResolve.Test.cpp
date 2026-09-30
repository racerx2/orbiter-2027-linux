// not upstream: unit tests for Src/Orbiter/PathResolve (Windows file name semantics on Linux)
#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "OrbiterAPI.h"

namespace fs = std::filesystem;

struct TmpTree {
	fs::path root, old;
	TmpTree () {
		char tmpl[] = "/tmp/ob_resolve_XXXXXX";
		root = mkdtemp (tmpl);
		fs::create_directories (root / "Config" / "Vessels");
		fs::create_directories (root / "Scenarios" / "Quicksave");
		std::ofstream (root / "Config" / "Earth.cfg") << "x\n";
		std::ofstream (root / "Config" / "Vessels" / "DeltaGlider.cfg") << "x\n";
		old = fs::current_path ();
		fs::current_path (root);
	}
	~TmpTree () { fs::current_path (old); fs::remove_all (root); }
};

TEST_CASE("Backslashes and case are resolved against the disk", "[resolve]")
{
	TmpTree t;
	REQUIRE(oapiResolvePath ("Config\\Earth.cfg") == "Config/Earth.cfg");
	REQUIRE(oapiResolvePath ("CONFIG\\earth.CFG") == "Config/Earth.cfg");
	REQUIRE(oapiResolvePath (".\\config\\vessels\\deltaglider.cfg") == "./Config/Vessels/DeltaGlider.cfg");
	REQUIRE(oapiResolvePath ((t.root.string() + "\\config\\EARTH.cfg").c_str()) == (t.root / "Config" / "Earth.cfg").string());
}

TEST_CASE("Case folding is ASCII, whatever the locale", "[resolve]")
{
	TmpTree t;
	if (!setlocale (LC_CTYPE, "tr_TR.UTF-8")) SKIP("tr_TR.UTF-8 locale not installed");
	std::string a = oapiResolvePath ("CONFIG\\VESSELS\\DELTAGLIDER.CFG"), b = oapiResolvePath ("config\\earth.cfg");
	setlocale (LC_CTYPE, "C");
	REQUIRE(a == "Config/Vessels/DeltaGlider.cfg"); // Turkish rules would keep I and i apart
	REQUIRE(b == "Config/Earth.cfg");
}

TEST_CASE("Unmatched tails are kept for files to be created", "[resolve]")
{
	TmpTree t;
	REQUIRE(oapiResolvePath ("scenarios\\quicksave\\New Save.scn") == "Scenarios/Quicksave/New Save.scn");
	REQUIRE(oapiResolvePath ("Scenarios\\Missing\\a.scn") == "Scenarios/Missing/a.scn");
	REQUIRE(oapiResolvePath ("scenarios\\") == "Scenarios/");
	REQUIRE(oapiResolvePath ("") == "");
}

TEST_CASE("Folders that differ only in case are one folder, as on Windows", "[resolve]")
{
	TmpTree t;
	fs::create_directories ("Textures/Base");
	fs::create_directories ("textures/MyAddon");
	std::ofstream ("Textures/Base/a.dds") << "x\n";
	std::ofstream ("textures/MyAddon/b.dds") << "x\n";
	REQUIRE(oapiResolvePath ("Textures\\MyAddon\\b.dds") == "textures/MyAddon/b.dds");
	REQUIRE(oapiResolvePath ("TEXTURES\\base\\A.DDS") == "Textures/Base/a.dds");
	REQUIRE(oapiResolvePath ("Textures\\MyAddon\\new.dds") == "textures/MyAddon/new.dds"); // a new file: the deepest existing folder
}

TEST_CASE("Names that differ only in case resolve alike whatever their order on disk", "[resolve]")
{
	TmpTree t;
	const char *order[2][3] = {{"a", "x.cfg", "X.cfg"}, {"b", "X.cfg", "x.cfg"}}; // folder, created first, created second
	for (auto &o : order) {
		fs::create_directories (o[0]);
		std::ofstream (fs::path (o[0]) / o[1]) << "x\n";
		std::ofstream (fs::path (o[0]) / o[2]) << "x\n";
		std::string d (o[0]);
		REQUIRE(oapiResolvePath ((d + "\\x.CFG").c_str()) == d + "/X.cfg"); // byte order: 'X' before 'x'
		REQUIRE(oapiResolvePath ((d + "\\x.cfg").c_str()) == d + "/x.cfg"); // the exact spelling first
	}
}

TEST_CASE("A file created after a lookup is found in any case: the folder's new mtime", "[resolve]")
{
	TmpTree t;
	fs::last_write_time ("Config", fs::file_time_type::clock::now () - std::chrono::seconds (10)); // an old folder: its listing is kept
	REQUIRE(oapiResolvePath ("config\\new.cfg") == "Config/new.cfg");
	std::ofstream ("Config/New.cfg") << "x\n";
	REQUIRE(oapiResolvePath ("CONFIG\\NEW.CFG") == "Config/New.cfg");
}

TEST_CASE("A file created after a lookup is found in any case: a folder changed within 2 s", "[resolve]")
{
	TmpTree t;
	auto m = fs::last_write_time ("Config");
	REQUIRE(oapiResolvePath ("config\\new.cfg") == "Config/new.cfg");
	std::ofstream ("Config/New.cfg") << "x\n";
	fs::last_write_time ("Config", m); // the same mtime as when listed (coarse file times): only the 2 s rule sees the change
	REQUIRE(oapiResolvePath ("CONFIG\\NEW.CFG") == "Config/New.cfg");
}

TEST_CASE("A folder that can be entered but not listed resolves by its exact name", "[resolve]")
{
	TmpTree t;
	fs::create_directories ("Locked/Inner");
	std::ofstream ("Locked/Inner/f.cfg") << "x\n";
	fs::permissions ("Locked", fs::perms::owner_exec, fs::perm_options::replace); // 0100: search, no read
	std::string r = oapiResolvePath ("locked\\Inner\\F.CFG");
	fs::permissions ("Locked", fs::perms::owner_all, fs::perm_options::replace);
	REQUIRE(r == "Locked/Inner/f.cfg");
}
