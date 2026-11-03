// not upstream: unit tests for Src/Orbiter/Util (NTFS listing order, MakePath without a working-directory prefix, module counts)
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <dlfcn.h>
#include "Util.h"

namespace fs = std::filesystem;

struct TmpDir {
	fs::path root, old;
	TmpDir (size_t minlen = 0) {
		char tmpl[] = "/tmp/ob_util_XXXXXX";
		root = mkdtemp (tmpl);
		fs::path p = root;
		while (p.string ().size () < minlen) p /= std::string (60, 'd');
		fs::create_directories (p);
		old = fs::current_path ();
		fs::current_path (p);
	}
	~TmpDir () { fs::current_path (old); fs::remove_all (root); }
};

TEST_CASE("SortedEntries lists a folder in NTFS order", "[util]")
{
	TmpDir t;
	for (const char *n : {"Zulu.cfg", "canaveral.cfg", "\xC3\x84rzte.cfg", "Alcantara.cfg", "Canaveral.cfg", "baikonur.cfg", "Al_Anbar.cfg"})
		std::ofstream (n) << "x\n";
	std::vector<std::string> names;
	for (const auto &e : SortedEntries (fs::directory_iterator ("."))) names.push_back (e.path ().filename ().string ());
	REQUIRE(names == std::vector<std::string>{"Alcantara.cfg", "Al_Anbar.cfg", "baikonur.cfg", "Canaveral.cfg", "canaveral.cfg", "Zulu.cfg", "\xC3\x84rzte.cfg"});
}

TEST_CASE("MakePath needs no working-directory prefix", "[util]")
{
	TmpDir t (300);
	REQUIRE(fs::current_path ().string ().size () >= 300);
	REQUIRE(MakePath ("capture/images/0000") == true);
	REQUIRE(fs::is_directory ("capture/images"));
	REQUIRE(MakePath ("capture/images/0001") == false); // exists already
	REQUIRE(MakePath ("noslash") == false);
	std::string longdir (300, 'x');
	REQUIRE(MakePath ((longdir + "/f").c_str ()) == false);
	REQUIRE(!fs::exists (longdir.substr (0, 255)));
}

TEST_CASE("ModuleFree detaches on the last free, ModuleLoad attaches a reopened module", "[util]")
{
	void *keep = dlopen (UTIL_MODULE, RTLD_NOW); // keeps the module mapped, as unique symbols do
	REQUIRE(keep);
	int *nAttach = (int*)dlsym (keep, "nAttach"), *nDetach = (int*)dlsym (keep, "nDetach");
	REQUIRE((nAttach && nDetach));
	void *h = ModuleLoad (UTIL_MODULE);
	REQUIRE(h == keep);
	REQUIRE(ModuleLoad (UTIL_MODULE) == h);
	REQUIRE(*nAttach == 0); // a first load doesn't attach
	ModuleFree (h);
	REQUIRE(*nDetach == 0); // still in use
	ModuleFree (h);
	REQUIRE(*nDetach == 1);
	ModuleFree (h);
	REQUIRE(*nDetach == 1); // freed twice: ignored
	REQUIRE(ModuleLoad (UTIL_MODULE) == h);
	REQUIRE(*nAttach == 1); // reopened after its last free
	ModuleFree (h);
	REQUIRE(*nDetach == 2);
	REQUIRE(ModuleLoad ("/nonexistent/Util.Module.so") == nullptr);
	dlclose (keep);
}

TEST_CASE("ConfigFragments lists <cfg>.d/*.cfg in NTFS order", "[util]")
{
	TmpDir t;
	fs::create_directories ("Config/Earth.d");
	for (const char *n : {"Zeta.cfg", "ssv.cfg", "Alpha.CFG", "notes.txt"})
		std::ofstream (std::string ("Config/Earth.d/") + n) << "x\n";
	fs::create_directories ("Config/Earth.d/Sub.cfg");
	std::vector<std::string> v = ConfigFragments ("Config/Earth.cfg");
	std::vector<std::string> names;
	for (const std::string &p : v) names.push_back (fs::path (p).filename ().string ());
	REQUIRE(names == std::vector<std::string>{"Alpha.CFG", "ssv.cfg", "Zeta.cfg"});
	REQUIRE(ConfigFragments ("config/earth.cfg").size () == 3); // case-insensitive, as every config path
	REQUIRE(ConfigFragments ("Config/Mars.cfg").empty ());
	REQUIRE(ConfigFragments ("Config/Earth").empty ());
}

TEST_CASE("ModuleLoad doesn't attach a module unmapped by its last free", "[util]")
{
	void *h = ModuleLoad (UTIL_MODULE);
	REQUIRE(h);
	ModuleFree (h);
	REQUIRE(dlopen (UTIL_MODULE, RTLD_NOW | RTLD_NOLOAD) == nullptr);
	void *g = ModuleLoad (UTIL_MODULE);
	REQUIRE(g);
	int *nAttach = (int*)dlsym (g, "nAttach");
	REQUIRE((nAttach && *nAttach == 0));
	ModuleFree (g);
}

static int depExits = 0;
extern "C" __attribute__((visibility("default"))) void UtilDepExited () { depExits++; }
__attribute__((visibility("default"))) void InitLib (void *) {} // the core's, which Orbitersdk's attach calls
__attribute__((visibility("default"))) int Date2Int (char *) { return 0; } // the core's, which Orbitersdk links to

// the destructor of a library a module needs runs that library's ExitModule, not the module's detach again
TEST_CASE("A needed library's ExitModule runs when it unloads", "[util]")
{
	depExits = 0;
	void *h = ModuleLoad (UTIL_NEEDS);
	REQUIRE(h);
	int (*value)() = (int(*)())ModuleProc (h, "UtilNeedsValue");
	REQUIRE(value);
	REQUIRE(value () == 7);
	ModuleFree (h);
	REQUIRE(dlopen (UTIL_NEEDS, RTLD_NOW | RTLD_NOLOAD) == nullptr); // unmapped, with the library it needs
	REQUIRE(depExits == 1);
}

// a fast exit detaches the modules still loaded and keeps them mapped; a free after it does nothing
TEST_CASE("ModuleDetachAll detaches each loaded module once", "[util]")
{
	void *h = ModuleLoad (UTIL_MODULE);
	REQUIRE(h);
	int *nDetach = (int*)dlsym (h, "nDetach");
	REQUIRE(nDetach);
	int n0 = *nDetach;
	ModuleDetachAll ();
	REQUIRE(*nDetach == n0 + 1);
	ModuleDetachAll ();
	REQUIRE(*nDetach == n0 + 1); // once
	REQUIRE(dlopen (UTIL_MODULE, RTLD_NOW | RTLD_NOLOAD) == h); // still mapped
	dlclose (h); // the NOLOAD reference
	ModuleFree (h);
	REQUIRE(*nDetach == n0 + 1); // freed after the detach: ignored
	REQUIRE(dlopen (UTIL_MODULE, RTLD_NOW | RTLD_NOLOAD) == h); // and still mapped, as at exit
	dlclose (h); // the NOLOAD reference
	dlclose (h); // ModuleLoad's, so later cases start unmapped
}

// not upstream: module and class names from data stay in their folder (design-security-fixes B12)
TEST_CASE("SafeRelName keeps a name in its folder", "[util]")
{
	for (const char *n : {"DeltaGlider", "XR/XR2", "Celbody\\Earth", "a..b", ".hidden", "a/./b"}) {
		INFO(n);
		CHECK(SafeRelName (n));
	}
	for (const char *n : {"", "../x", "a/../b", "a\\..\\b", "/etc/x", "\\x", "C:x", "..", "x/.."}) {
		INFO(n);
		CHECK_FALSE(SafeRelName (n));
	}
	CHECK_FALSE(SafeRelName (nullptr));
}

// not upstream: vessel and record names as one file name in the Flights folder (design-security-fixes D)
TEST_CASE("SafeFileName makes one file name of a vessel or record name", "[util]")
{
	CHECK(SafeFileName ("GL-01") == "GL-01");
	CHECK(SafeFileName ("Delta Glider (2)") == "Delta Glider (2)");
	CHECK(SafeFileName ("../../.ssh/x") == ".._.._.ssh_x");
	CHECK(SafeFileName ("a\\b") == "a_b");
	CHECK(SafeFileName ("a\nb\x7f") == "a_b_");
	CHECK(SafeFileName ("..") == "_");
	CHECK(SafeFileName (".") == "_");
	CHECK(SafeFileName ("") == "_");
	CHECK(SafeFileName (nullptr) == "_");
	CHECK(SafeFileName ("/") == "_");
}
