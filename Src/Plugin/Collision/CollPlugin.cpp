// not upstream: collision addon, module entry, session lifecycle and callback dispatch (Design CA E4 11.2-11.4)
#define ORBITER_MODULE // exactly one file: ModuleDate and the module glue of the SDK library
#include <cstdio>
#include <string>
#include <vector>
#include "CollPlugin.h"
#include "CollSession.h"
#include "CollApiA.h"
#include "CollCfg.h"
#include "CollDamageA.h"
#include "CollDialogA.h"
#include "CollGeom.h"
#include "CollSdkOrbiter.h"

namespace {

CollPlugin *g_plugin = nullptr;
char g_cmdDesc[128] = "";            // the core keeps this pointer for the process
int g_logLevel = 0;                  // CollisionLog of the live session
bool g_failed = false;               // an exception reached a callback or an export: the addon is off until the session ends

void Fail (const char *where, const char *what) // CollGuard's hook: one log line per session
{
	if (g_failed) return;
	try { if (CollSession *x = g_plugin ? g_plugin->Session () : nullptr) x->Quiet (); } catch (...) {} // dmg3: effects quiet, before Session () turns off
	g_failed = true;
	CollLogF ("Collision: error in %s: %s; collisions off until the session ends", where, what ? what : "");
}

void LogSink (int level, const char *msg) // the Phase A log hook while a session lives
{
	if (g_logLevel <= 0 || (level == COLLLOG_FINE && g_logLevel < 3)) return;
	CollLogLine (msg);
}

CollDmgSession *CurDmg () // the running session's E3 part for the dialog and the exported API
{
	CollSession *x = g_plugin ? g_plugin->Session () : nullptr;
	return x ? x->Dmg () : nullptr;
}

CollCfgValues ReadCfg () // once per session, at its creation (E4 11.3)
{
	FILEHANDLE f = CollOpenCfg ("Collision.cfg");
	if (!f) {
		CollLogLine ("Collision: Config/Collision.cfg not found, defaults used");
		return CollCfgValues ();
	}
	std::vector<std::string> bad;
	CollCfgValues v = CollCfg::Read ([f] (const char *item, std::string &val) { return CollReadItem (f, item, val); }, &bad);
	CollCloseCfg (f);
	for (const std::string &k : bad) CollLogF ("Collision: Collision.cfg %s: value not understood, default kept", k.c_str ());
	return v;
}

}

CollPlugin::CollPlugin (CollHModule h) : oapi::Module (h)
{
	proc = CollSdkOrbiterCreate (true);
	snprintf (g_cmdDesc, sizeof g_cmdDesc, "%s", CollUiA::Description ());
	try {
#if COLL_HAVE_IMGUI
		dlg = new CollDialogA (CurDmg); // no SDK call: the constructor stores the name
#endif
		CollApiA::SetSession (CurDmg);
		cmd = proc->RegisterCmd (CollUiA::Label (), g_cmdDesc, OnCustomCmd, this); // last: once per process, as Framerate
	} catch (...) { // the core must not keep a command whose context is gone
		if (cmd) proc->UnregisterCmd (cmd);
		cmd = 0;
		CollApiA::SetSession (nullptr);
#if COLL_HAVE_IMGUI
		delete dlg;
#endif
		dlg = nullptr;
		throw;
	}
}

CollPlugin::~CollPlugin () = default;

void CollPlugin::Shutdown ()
{
	End ("unload");
	CollApiA::SetSession (nullptr);
	CollGuard ("ExitModule", [&] { if (cmd) proc->UnregisterCmd (cmd); });
	cmd = 0;
#if COLL_HAVE_IMGUI
	delete dlg; // ~ImGuiDialog calls oapiCloseDialog, a no-op without a dialog manager
#endif
	dlg = nullptr;
	CollLogF ("Collision: unloaded cmds=%lld", (long long)proc->Count ().cmds);
	proc.reset ();
	g_collFail = nullptr;
}

CollSession *CollPlugin::Session () const { return phase == Phase::Running && !g_failed ? s.get () : nullptr; }

CollSession *CollPlugin::Want (bool startsSession, const char *why) // NULL while the session is off after an error
{
	if (phase == Phase::Running && startsSession) End ("stale");
	if (g_failed) return nullptr;
	if (!s) {
		CollCfgValues c = ReadCfg ();
		g_logLevel = c.logLevel;
		g_collLog = LogSink;
		s = std::make_unique<CollSession> (++serial, c);
		phase = Phase::Loading;
		CollLogF ("Collision: session %u created (%s)", serial, why);
	}
	return s.get ();
}

void CollPlugin::End (const char *why) // no world access: on the normal close path every vessel is gone already
{
	phase = Phase::None;
	jump = Jump ();
	bool failed = g_failed;
	if (s) {
		uint32_t n = s->Serial ();
		if (!failed) CollGuard ("session end", [&] { s->Close (); });
		else CollGuard ("session end", [&] { s->Abort (); }); // an off session has no summary; its side file is still flushed
		s.reset ();
		g_collLog = nullptr;
		CollLogF ("Collision: session %u ended (%s)", n, why);
	}
	g_failed = false;
	loaded = false;
}

void CollPlugin::LoadState (FILEHANDLE scn)
{
	if (g_failed && loaded) End ("stale"); // a previous load threw and no clbkSimulationEnd came
	loaded = true;
	CollGuard ("opcLoadState", [&] { if (CollSession *x = Want (true, "load")) x->Load (scn); });
}

void CollPlugin::SaveState (FILEHANDLE scn) // also when off: the id map stays exact, so the damage records reach the saved scenario
{
	CollGuard ("opcSaveState", [&] {
		if (s) s->Save (scn);
		else CollWriteLine (scn, "COLLA 1");
	});
}

void CollPlugin::clbkSimulationStart (RenderMode mode)
{
	CollGuard ("clbkSimulationStart", [&] {
		CollSession *x = Want (true, "start");
		phase = Phase::Running; // also when off: the next load or start ends it as stale
		if (x) x->Start ((int)mode);
	});
}

void CollPlugin::clbkSimulationEnd () { End ("end"); }

void CollPlugin::clbkNewVessel (OBJHANDLE h)
{
	CollGuard ("clbkNewVessel", [&] {
		if (g_failed) { if (s) s->NewVessel (h, true); return; } // off: only the id map, the queue is never applied
		Want (false, "new vessel")->NewVessel (h, inStep);
	});
}

void CollPlugin::clbkDeleteVessel (OBJHANDLE h)
{
	CollGuard ("clbkDeleteVessel", [&] { if (s) s->DeleteVessel (h, inStep || g_failed); });
}

void CollPlugin::clbkPreStep (double simt, double simdt, double)
{
	if (inStep || !Session ()) return;
	inStep = true;
	CollGuard ("clbkPreStep", [&] {
		s->FrameBegin ();
		s->PreStep (simt, simdt); // PS1-PS7
		if (g_failed) return;     // an export called from a vessel inside the step failed
		s->ApplyQueued ();        // PS8: vessels created or deleted by module code inside the pre-step
		if (jump.pending) {       // PS8: a time jump raised inside the pre-step
			jump.pending = false;
			TimeJump (jump.simt, jump.simdt, jump.mjd);
		}
		s->FrameEnd ();
	});
	inStep = false;
}

void CollPlugin::clbkPostStep (double, double, double)
{
	CollGuard ("clbkPostStep", [&] { if (CollSession *x = Session ()) x->PostStep (); }); // PO1-PO4
}

void CollPlugin::clbkTimeJump (double simt, double simdt, double mjd)
{
	CollGuard ("clbkTimeJump", [&] {
		if (!Session ()) return;
		if (inStep) jump = Jump { true, simt, simdt, mjd };
		else TimeJump (simt, simdt, mjd);
	});
}

void CollPlugin::TimeJump (double, double, double) { if (CollSession *x = Session ()) x->TimeJump (); }

void CollPlugin::clbkTimeAccChanged (double newWarp, double)
{
	CollGuard ("clbkTimeAccChanged", [&] { if (CollSession *x = Session ()) x->TimeAccChanged (newWarp); }); // a warp set in a vessel's PostCreation or the scenario script arrives before the start
}

void CollPlugin::clbkVesselJump (OBJHANDLE h)
{
	CollGuard ("clbkVesselJump", [&] { if (CollSession *x = Session ()) x->VesselJump (h); }); // DefSetStateEx in PostCreation raises it before the start
}

void CollPlugin::clbkPause (bool pause)
{
	CollGuard ("clbkPause", [&] { if (CollSession *x = Session ()) x->Pause (pause); }); // a start paused arrives after the start, in the first time step
}

bool CollPlugin::clbkProcessKeyboardImmediate (char[256], bool)
{
	CollGuard ("clbkProcessKeyboardImmediate", [&] { if (CollSession *x = Session ()) x->KeyPass (); });
	return false;
}

void CollPlugin::OnCustomCmd (void *ctx)
{
	CollGuard ("custom command", [&] {
		CollPlugin *p = (CollPlugin *)ctx;
		CollSession *x = p->Session ();
		if (!x || !p->proc) return; // the core lists custom commands only in a session
		CollUiA::OnCommand (*p->proc, x->Dmg (), p->dlg);
	});
}

DLLCLBK void InitModule (CollHModule h)
{
	g_collFail = Fail;
	g_failed = false;
	CollGuard ("InitModule", [&] {
		g_plugin = new CollPlugin (h);
		oapiRegisterModule (g_plugin);
		CollLogF ("Collision: loaded (addon %s)", COLL_ADDON_VERSION);
	});
}

DLLCLBK void ExitModule (CollHModule)
{
	if (!g_plugin) return;
	g_plugin->Shutdown ();
	delete g_plugin;
	g_plugin = nullptr;
}

DLLCLBK void opcLoadState (FILEHANDLE scn) { if (g_plugin) g_plugin->LoadState (scn); }

DLLCLBK void opcSaveState (FILEHANDLE scn) { if (g_plugin) g_plugin->SaveState (scn); }
