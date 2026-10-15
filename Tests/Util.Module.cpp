// not upstream: a module for Util.Test; ModuleAttach and ModuleDetach count their calls
#define EXPORT __attribute__((visibility("default")))

extern "C" {
EXPORT int nAttach = 0, nDetach = 0;
EXPORT void ModuleAttach () { nAttach++; }
EXPORT void ModuleDetach () { nDetach++; }
}
