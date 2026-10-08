// not upstream: test-only plugin of the collision addon scenario tests: runs harness.lua synchronously once per post-step (Design CA E4 7.1)
#define ORBITER_MODULE
#include "Orbitersdk.h"

namespace { // a scenario script's per-frame hand-over (LuaInline post-step) can skip or repeat frames under load; oapiExecScriptCmd waits until the command ran

const char *START = "local ok, e = pcall (function () H = dofile ('./Script/Tests/Coll/harness.lua'); H.start (dofile ('./Script/Tests/Coll/run.lua')) end) "
	"if not ok then oapi.write_log ('CollTestHarness error: ' .. tostring (e)) end";
const char *FRAME = "if H then local ok, e = pcall (H.frame) if not ok then oapi.write_log ('CollTestHarness error: ' .. tostring (e)); H = nil end end";
const char *STOP = "if H then pcall (H.stop) end";

class CollTestHarness : public oapi::Module {
public:
	explicit CollTestHarness (void *h) : oapi::Module (h) {}
	void clbkSimulationStart (RenderMode) override
	{
		hi = oapiCreateInterpreter ();
		if (!hi) {
			oapiWriteLog ("CollTestHarness error: no interpreter");
			return;
		}
		oapiExecScriptCmd (hi, START);
	}
	void clbkPostStep (double, double, double) override
	{
		if (hi) oapiExecScriptCmd (hi, FRAME); // after every plugin listed before this one, the addon included
	}
	void clbkSimulationEnd () override
	{
		if (!hi) return;
		oapiExecScriptCmd (hi, STOP); // closes the dump only: no world access here
		oapiDelInterpreter (hi);
		hi = nullptr;
	}
private:
	INTERPRETERHANDLE hi = nullptr;
};

CollTestHarness *g_harness = nullptr;

}

DLLCLBK void InitModule (void *h)
{
	g_harness = new CollTestHarness (h);
	oapiRegisterModule (g_harness);
}

DLLCLBK void ExitModule (void *)
{
	delete g_harness;
	g_harness = nullptr;
}
