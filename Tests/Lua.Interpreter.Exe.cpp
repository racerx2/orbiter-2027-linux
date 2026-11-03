// not upstream: test doubles for the exe-side symbols LuaInterpreter.so binds at load time; exe_stubs.py makes the rest (the test calls none of those)

#include <string>

#define TESTEXPORT __attribute__((visibility("default")))

TESTEXPORT void InitLib (void *hModule) {}

// the ones the tests reach: the confinement setting (off: content scripts are confined), path resolution, the error report
TESTEXPORT bool oapiScriptFullAccess () { return false; }
TESTEXPORT std::string oapiResolvePath (const char *path) { return path ? path : ""; }
TESTEXPORT void __writeLogError (const char *func, const char *file, int line, const char *format, ...) {}
TESTEXPORT void oapiAddNotification (int type, const char *title, const char *content) {}

extern "C" {
TESTEXPORT const void *mfd2_vtable[64] __asm__("_ZTV4MFD2") = {}; // vtable for MFD2 placeholder
TESTEXPORT const void *mfd2_typeinfo[4] __asm__("_ZTI4MFD2") = {}; // typeinfo for MFD2 placeholder
}
