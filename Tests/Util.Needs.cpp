// not upstream: a module that needs Util.Dep, both with Orbitersdk's attach and detach (Util.Test's needed-library case)
#define DLLCLBK extern "C" __attribute__((visibility("default")))

void dummy (); // as Util.Dep
void calldummy () { dummy (); }

extern "C" int UtilDepValue ();

DLLCLBK void ExitModule (void *) {}
DLLCLBK int UtilNeedsValue () { return UtilDepValue (); }
