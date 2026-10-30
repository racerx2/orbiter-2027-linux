// not upstream (dll-modules): Windows modules (.dll) opened through the Wine bridge (Modules/WineBridge.so)
#include "WinModule.h"
#include "Log.h"
#include "OrbiterAPI.h"
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <set>
#include <strings.h>
#include <unistd.h>

static bool EndsWith (const std::string &s, const char *ext)
{
	size_t n = strlen (ext);
	return s.size () > n && !strcasecmp (s.c_str () + s.size () - n, ext);
}

static bool WindowsFile (const std::string &path)
{
	std::ifstream f (path, std::ios::binary);
	char mz[2];
	return f.read (mz, 2) && mz[0] == 'M' && mz[1] == 'Z';
}

std::string WinModulePath (const char *path)
{
	std::string p (path ? path : "");
	if (EndsWith (p, ".dll")) return WindowsFile (p) ? p : "";
	if (!EndsWith (p, ".so") || !access (p.c_str (), F_OK)) return "";
	std::string dll = oapiResolvePath ((p.substr (0, p.size () - 3) + ".dll").c_str ());
	return WindowsFile (dll) ? dll : "";
}

bool WinModuleEntry (const std::filesystem::path &file)
{
	std::string f = file.string ();
	if (!EndsWith (f, ".dll") || !WindowsFile (f)) return false;
	return access (oapiResolvePath ((f.substr (0, f.size () - 4) + ".so").c_str ()).c_str (), F_OK) != 0;
}

struct WbApi {
	void *(*load) (const char *);
	void *(*proc) (void *, const char *);
	int (*free) (void *); // the opens left
};

// the bridge's entry points, looked up once; why says what is missing
static const WbApi *Bridge (std::string &why)
{
	static WbApi api {};
	static int state = 0;
	static std::string err;
	if (!state) {
		std::string so = oapiResolvePath ("Modules/WineBridge.so");
		void *h = dlopen (so.c_str (), RTLD_NOW); // kept for the session, as the modules it opens
		const char *e = h ? nullptr : dlerror ();
		if (h) {
			api.load = (void *(*) (const char *))dlsym (h, "wbModuleLoad");
			api.proc = (void *(*) (void *, const char *))dlsym (h, "wbModuleProc");
			api.free = (int (*) (void *))dlsym (h, "wbModuleFree");
		}
		state = api.load && api.proc && api.free ? 1 : -1;
		if (state < 0) err = h ? "the Wine bridge in Modules/WineBridge.so is too old to load modules" : std::string ("the Wine bridge is not installed (") + (e ? e : so.c_str ()) + ")";
		dlerror ();
	}
	why = err;
	return state > 0 ? &api : nullptr;
}

static std::map<void *, std::string> &g_handles = *new std::map<void *, std::string>; // never destroyed: modules are freed at exit

void *WinModuleLoad (const char *dllpath)
{
	std::string why;
	const WbApi *b = Bridge (why);
	if (!b) {
		static std::set<std::string> &told = *new std::set<std::string>;
		if (told.insert (dllpath).second) LOGOUT_ERR ("Windows module %s not loaded: %s", dllpath, why.c_str ());
		return nullptr;
	}
	void *h = b->load (dllpath); // the bridge logs why it failed
	if (h) g_handles[h] = dllpath;
	return h;
}

bool WinModuleHandle (void *hModule)
{
	return hModule && g_handles.count (hModule);
}

void *WinModuleProc (void *hModule, const char *name)
{
	std::string why;
	const WbApi *b = Bridge (why);
	return b && WinModuleHandle (hModule) ? b->proc (hModule, name) : nullptr;
}

void WinModuleFree (void *hModule)
{
	std::string why;
	const WbApi *b = Bridge (why);
	if (b && WinModuleHandle (hModule) && b->free (hModule) <= 0) g_handles.erase (hModule);
}

const char *WinModuleFileName (void *hModule)
{
	auto it = g_handles.find (hModule);
	return it == g_handles.end () ? "" : it->second.c_str ();
}
