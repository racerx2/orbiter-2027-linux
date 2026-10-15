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
