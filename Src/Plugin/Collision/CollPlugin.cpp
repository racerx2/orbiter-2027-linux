// not upstream: collision addon, module entry, session lifecycle and callback dispatch (Design CA E4 11.2-11.4)
#define ORBITER_MODULE // exactly one file: ModuleDate and the module glue of the SDK library
#include <string>
#include <vector>
#include "CollPlugin.h"
#include "CollSession.h"
#include "CollCfg.h"
#include "CollGeom.h"
#if COLL_HAVE_IMGUI
#include "imgui.h"

class CollDialogA : public ImGuiDialog { // stand-in for E3's content (CollDialog.cpp, Phase D); E4 owns the object
public:
	explicit CollDialogA (const CollPlugin *p) : ImGuiDialog ("Collision damage"), plugin (p) {}
protected:
	void OnDraw () override
	{
		const CollSession *x = plugin->Session ();
		if (x) ImGui::Text ("Collision session %u", x->Serial ());
		else ImGui::TextUnformatted ("no session");
	}
private:
	const CollPlugin *plugin;
};
#endif

namespace {

CollPlugin *g_plugin = nullptr;
char g_cmdDesc[] = "Collision damage: vessels, buildings, repair"; // the core keeps this pointer for the process
int g_logLevel = 0;                                                  // CollisionLog of the live session

void LogSink (int level, const char *msg) // the Phase A log hook while a session lives
{
	if (g_logLevel <= 0 || (level == COLLLOG_FINE && g_logLevel < 3)) return;
	CollLogLine (msg);
}

CollCfgValues ReadCfg () // once per session, at its creation (E4 11.3); E2 step 9 moves it to CollSdk::CfgString
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
	cmd = CollRegisterCmd ("Collision damage", g_cmdDesc, OnCustomCmd, this); // once per process, as Framerate
	if (cmd >= 0) cmds++;
#if COLL_HAVE_IMGUI
	dlg = new CollDialogA (this); // no SDK call: the constructor stores the name
#endif
}

CollPlugin::~CollPlugin () = default;

void CollPlugin::Shutdown ()
{
	End ("unload");
	if (cmd >= 0 && CollUnregisterCmd (cmd)) cmds--;
	cmd = -1;
#if COLL_HAVE_IMGUI
	delete dlg; // ~ImGuiDialog calls oapiCloseDialog, a no-op without a dialog manager
#endif
	dlg = nullptr;
	CollLogF ("Collision: unloaded cmds=%d", cmds);
}

CollSession *CollPlugin::Session () const { return phase == Phase::Running ? s.get () : nullptr; }

CollSession *CollPlugin::Want (bool startsSession, const char *why)
{
	if (phase == Phase::Running && startsSession) End ("stale");
	if (!s) {
		CollCfgValues c = ReadCfg ();
		s = std::make_unique<CollSession> (++serial, c);
		g_logLevel = c.logLevel;
		g_collLog = LogSink;
		phase = Phase::Loading;
		CollLogF ("Collision: session %u created (%s)", serial, why);
	}
	return s.get ();
}

void CollPlugin::End (const char *why) // no world access: on the normal close path every vessel is gone already
{
	phase = Phase::None;
	jump = Jump ();
	if (!s) return;
	uint32_t n = s->Serial ();
	s->Close ();
	g_collLog = nullptr;
	s.reset ();
	CollLogF ("Collision: session %u ended (%s)", n, why);
}

void CollPlugin::LoadState (FILEHANDLE scn) { Want (true, "load")->Load (scn); }

void CollPlugin::SaveState (FILEHANDLE scn)
{
	if (s) s->Save (scn);
	else CollWriteLine (scn, "COLLA 1");
}

void CollPlugin::clbkSimulationStart (RenderMode mode)
{
	CollSession *x = Want (true, "start");
	phase = Phase::Running;
	x->Start ((int)mode);
}

void CollPlugin::clbkSimulationEnd () { End ("end"); }

void CollPlugin::clbkNewVessel (OBJHANDLE h) { Want (false, "new vessel")->IdOf (h); }

void CollPlugin::clbkDeleteVessel (OBJHANDLE h) { if (s) s->Forget (h); }

void CollPlugin::clbkPreStep (double, double, double)
{
	if (phase != Phase::Running || inStep) return;
	inStep = true;
	s->FrameBegin (); // PS1 BeginFrame: E2 PreStep and E1 snapshot come with Phases G and F
	// PS2-PS7 in this order come with Phases G, F, D: E2 Deliver to E3 ShapesUpdated, E3 PrePhysics, E1 physics, E3 Commit, notices, E1 warp, E3 thrust cut
	if (jump.pending) { // PS8: a time jump raised inside the pre-step
		jump.pending = false;
		TimeJump (jump.simt, jump.simdt, jump.mjd);
	}
	s->FrameEnd ();
	inStep = false;
}

void CollPlugin::clbkPostStep (double, double, double)
{
	if (phase != Phase::Running) return;
	s->PostBegin (); // PO1-PO4 come with Phases G and D: E2 animation polls, E3 PostStep, test slot check, UiTick
	s->PostEnd ();
}

void CollPlugin::clbkTimeJump (double simt, double simdt, double mjd)
{
	if (phase != Phase::Running) return;
	if (inStep) jump = Jump { true, simt, simdt, mjd };
	else TimeJump (simt, simdt, mjd);
}

void CollPlugin::TimeJump (double, double, double) {} // E1's time-jump rule and E2's MarkJump (Phases F, G)

void CollPlugin::clbkTimeAccChanged (double, double) {} // E1's warp clamp, Running only (Phase F)

void CollPlugin::clbkVesselJump (OBJHANDLE) {} // E1, Running only (Phase F)

void CollPlugin::clbkPause (bool) {} // E3's visual pass on pause entry (Phase D)

bool CollPlugin::clbkProcessKeyboardImmediate (char[256], bool) { return false; } // E3's optional pass, CollisionKeyPass (Phase D)

void CollPlugin::OnCustomCmd (void *ctx)
{
	CollPlugin *p = (CollPlugin *)ctx;
	CollSession *x = p->Session ();
	if (!x) return; // the core lists custom commands only in a session
#if COLL_HAVE_IMGUI
	if (p->dlg) CollOpenDialog (p->dlg);
#else
	CollLogF ("Collision damage: session %u (no dialog in this Orbiter; the report comes with Phase D)", x->Serial ());
#endif
}

DLLCLBK void InitModule (CollHModule h)
{
	g_plugin = new CollPlugin (h);
	oapiRegisterModule (g_plugin);
	CollLogF ("Collision: loaded (addon %s)", COLL_ADDON_VERSION);
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
