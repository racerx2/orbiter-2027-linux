// WineBridge helper: the script interpreter, as Orbiter.exe's ScriptInterface: Orbiter loads the helper's LuaInline (Modules/WineBridge)
// as a plugin at the first call, through the bridge as any Windows plugin; its opc functions do the work
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include "wb_helper.h"

static int g_lib; // LuaInline's module number; 0: not loaded (tried again at the next call, as Orbiter.exe does)

static void *Proc (const char *name)
{
	if (!g_lib) {
		wb::Begin (L_LoadInterpreterLib);
		WbReader r = wb::Call ();
		int m = r.i32 ();
		if (!r.bad && m > 0) g_lib = m;
	}
	return g_lib ? wb::ModuleExport (g_lib, name) : 0;
}

void wb::ScriptFreed (int module)
{
	if (module == g_lib) g_lib = 0;
}

INTERPRETERHANDLE oapiCreateInterpreter ()
{
	INTERPRETERHANDLE (*proc) () = (INTERPRETERHANDLE (*) ())Proc ("opcNewInterpreter");
	return proc ? proc () : 0;
}

int oapiDelInterpreter (INTERPRETERHANDLE hInterp)
{
	int (*proc) (INTERPRETERHANDLE) = (int (*) (INTERPRETERHANDLE))Proc ("opcDelInterpreter");
	if (!g_lib) return 0; // no library, as Orbiter.exe's
	return proc ? proc (hInterp) : 3;
}

bool oapiExecScriptCmd (INTERPRETERHANDLE hInterp, const char *cmd)
{
	bool (*proc) (INTERPRETERHANDLE, const char *) = (bool (*) (INTERPRETERHANDLE, const char *))Proc ("opcExecScriptCmd");
	return proc ? proc (hInterp, cmd) : false;
}

bool oapiAsyncScriptCmd (INTERPRETERHANDLE hInterp, const char *cmd)
{
	bool (*proc) (INTERPRETERHANDLE, const char *) = (bool (*) (INTERPRETERHANDLE, const char *))Proc ("opcAsyncScriptCmd");
	return proc ? proc (hInterp, cmd) : false;
}

lua_State *oapiGetLua (INTERPRETERHANDLE hInterp)
{
	lua_State *(*proc) (INTERPRETERHANDLE) = (lua_State *(*) (INTERPRETERHANDLE))Proc ("opcGetLua");
	return proc ? proc (hInterp) : 0;
}
