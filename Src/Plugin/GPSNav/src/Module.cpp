// GPSNav: the module: the custom command that opens the unit, the flight plans and the unit's settings in the scenario
#define ORBITER_MODULE
#include "Unit.h"
#include <cstring>

namespace {

class GPSNav : public oapi::Module {
public:
	GPSNav (void *hDLL) : Module (hDLL), unit (lib, plans, opt)
	{
		cmd = oapiRegisterCustomCmd ("GPS navigator", "A panel GPS navigator: a moving map, Direct-To, flight plans, the nearest airports and beacons, a glide path to a runway.", OpenUnit, this);
		menuCmd = oapiRegisterCustomMenuCmd ("GPS", "MenuInfoBar/GPSNav.png", OpenUnit, this); // an icon on the menu bar
	}
	~GPSNav ()
	{
		oapiUnregisterCustomMenuCmd (menuCmd);
		oapiUnregisterCustomCmd ((int)cmd);
	}

	static void OpenUnit (void *ctx) { oapiOpenDialog (&((GPSNav *)ctx)->unit); }

	void clbkSimulationStart (RenderMode mode) override
	{
		if (open && mode != RENDER_NONE) oapiOpenDialog (&unit);
		open = false;
	}
	void clbkSimulationEnd () override
	{
		unit.Reset ();
		lib.Clear ();
		plans.clear ();
		open = false;
	}
	void clbkPreStep (double simt, double simdt, double mjd) override { unit.Step (simdt); }

	// BEGIN_GPSNav: OPTIONS, OPEN, GPSS (the vessel it steers), and a PLAN line per vessel (its name, then the plan)
	void Save (FILEHANDLE scn)
	{
		oapiWriteScenario_string (scn, "OPTIONS", opt.Save ().c_str ());
		if (unit.IsActive ()) oapiWriteScenario_string (scn, "OPEN", "1");
		if (!unit.GpssVessel ().empty ()) oapiWriteScenario_string (scn, "GPSS", unit.GpssVessel ().c_str ());
		for (auto &p : plans)
			if (!p.second.wpts.empty () || p.second.direct) oapiWriteScenario_string (scn, "PLAN", (p.first + " " + p.second.Save ()).c_str ());
	}
	void Load (FILEHANDLE scn)
	{
		char *line;
		plans.clear ();
		while (oapiReadScenario_nextline (scn, line)) {
			if (!strncasecmp (line, "OPTIONS ", 8)) opt.Load (line + 8);
			else if (!strncasecmp (line, "OPEN ", 5)) open = atoi (line + 5) != 0;
			else if (!strncasecmp (line, "GPSS ", 5)) unit.GpssResume (line + 5);
			else if (!strncasecmp (line, "PLAN ", 5)) {
				const char *s = line + 5, *sp = strchr (s, ' ');
				gps::Plan p;
				if (sp && p.Load (sp + 1)) plans[std::string (s, sp - s)] = p;
			}
		}
	}

private:
	Library lib;
	std::map<std::string, gps::Plan> plans; // by vessel name: the scenario's are read before its vessels exist
	Options opt;
	Unit unit;
	DWORD cmd;
	int menuCmd;
	bool open = false;                      // the scenario had the unit open
};

GPSNav *g = nullptr;

} // namespace

DLLCLBK void InitModule (void *hDLL)
{
	g = new GPSNav (hDLL);
	oapiRegisterModule (g);
}

DLLCLBK void ExitModule (void *hDLL)
{
	delete g;
	g = nullptr;
}

DLLCLBK void opcSaveState (FILEHANDLE scn) { if (g) g->Save (scn); }
DLLCLBK void opcLoadState (FILEHANDLE scn) { if (g) g->Load (scn); }
