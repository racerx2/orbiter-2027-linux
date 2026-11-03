// not upstream: a library a module needs, with its own ExitModule (Util.Test's needed-library case)
#define DLLCLBK extern "C" __attribute__((visibility("default")))

void dummy (); // pulls in Orbitersdk's attach and detach, as OrbiterAPI.h's ORBITER_MODULE block does
void calldummy () { dummy (); }

extern "C" void UtilDepExited (); // in Util.Test

DLLCLBK void ExitModule (void *) { UtilDepExited (); }
DLLCLBK int UtilDepValue () { return 7; }
