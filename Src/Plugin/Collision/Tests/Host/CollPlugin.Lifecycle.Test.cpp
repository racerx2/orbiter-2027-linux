// not upstream: plays Orbiter's side of sessions and module loads for Collision.so (Design CA E4 11.6, 11.7): five sessions, both close paths, unload, re-load
#include "OrbiterAPI.h"
#include "ModuleAPI.h"
#include "VesselAPI.h"
#include <catch2/catch_test_macros.hpp>
#include <dlfcn.h>
#include <link.h>
#include <strings.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#if __has_include(<sanitizer/asan_interface.h>)
#include <sanitizer/asan_interface.h> // the poison macros are no-ops without -fsanitize=address
#else
#define ASAN_POISON_MEMORY_REGION(a, s) ((void)(a), (void)(s))
#define ASAN_UNPOISON_MEMORY_REGION(a, s) ((void)(a), (void)(s))
#endif

namespace {

struct FakeVessel { double size; int damage; char name[20]; };
struct Cmd { int id; CustomFunc fn; void *ctx; const char *desc; };

struct Host {
	int inits = 0, regs = 0, unregs = 0, dlgGone = 0, opens = 0, cfgOpens = 0, cfgCloses = 0, worldBad = 0, bugs = 0;
	oapi::Module *module = nullptr;
	bool dlgMgr = false, noWorld = false, unloading = false; // noWorld: a callback where E4 11.2 allows no world call
	bool throwPos = false, throwIsVessel = false;            // a host call throws: the module must catch it
	bool throwReg = false, throwRead = false;                // fix2 area E: command registration, scenario read
	std::vector<ImGuiDialog *> dlgList;
	ImGuiDialog *loadDialog = nullptr;  // the one dialog object of the current load
	std::vector<Cmd> cmds;
	int nextCmd = 1;
	FakeVessel slots[8];                // fixed addresses: later sessions get the same handle values
	std::vector<OBJHANDLE> world;
	std::vector<std::string> scnIn, scnOut, log;
	size_t scnPos = 0;
	std::string cfg;                    // Config/Collision.cfg text; missing while cfgMissing
	bool cfgMissing = true;
	std::string lineBuf;
} H;

void Bug (const char *what)
{
	H.bugs++;
	printf ("BUG: %s\n", what);
}

bool Live (OBJHANDLE h) { return std::find (H.world.begin (), H.world.end (), h) != H.world.end (); }

void WorldCall ()
{
	if (H.noWorld) { H.worldBad++; Bug ("world call in a callback that allows none"); }
}

void *OwnProc (void *h, const char *name) // GetProcAddress semantics, as Celbody.Test
{
	void *p = dlsym (h, name);
	struct link_map *lm;
	Dl_info i;
	if (p && (dlinfo (h, RTLD_DI_LINKMAP, &lm) || !dladdr (p, &i) || strcmp (i.dli_fname, lm->l_name))) p = nullptr;
	return p;
}

}

DLLEXPORT struct ImGuiContext *GImGui = nullptr;

DLLEXPORT void InitLib (void *h)
{
	void (*f) (void *) = (void (*) (void *))OwnProc (h, "InitModule");
	if (f) { H.inits++; f (h); }
}

DLLEXPORT int Date2Int (char *) { return 1; }

void oapiRegisterModule (oapi::Module *m) { H.module = m; }

void oapiWriteLogV (const char *fmt, ...)
{
	char b[1024];
	va_list a;
	va_start (a, fmt);
	vsnprintf (b, sizeof b, fmt, a);
	va_end (a);
	H.log.push_back (b);
	printf ("log: %s\n", b);
}

DWORD oapiRegisterCustomCmd (const char *, const char *desc, CustomFunc f, void *ctx)
{
	if (H.throwReg) throw std::runtime_error ("host RegisterCustomCmd");
	H.regs++;
	H.cmds.push_back ({ H.nextCmd, f, ctx, desc });
	return H.nextCmd++;
}

bool oapiUnregisterCustomCmd (int id)
{
	for (auto it = H.cmds.begin (); it != H.cmds.end (); ++it)
		if (it->id == id) { H.cmds.erase (it); H.unregs++; return true; }
	return false;
}

FILEHANDLE oapiOpenFile (const char *fname, FileAccessMode mode, PathRoot root)
{
	if (strcmp (fname, "Collision.cfg")) return nullptr; // other cfg files (E2's client cfg) are missing here
	if (root != CONFIG || mode != FILE_IN_ZEROONFAIL) Bug ("unexpected oapiOpenFile");
	H.cfgOpens++;
	return H.cfgMissing ? nullptr : (FILEHANDLE)&H.cfg;
}

void oapiCloseFile (FILEHANDLE f, FileAccessMode)
{
	if (f != (FILEHANDLE)&H.cfg) Bug ("oapiCloseFile of an unknown file");
	H.cfgCloses++;
}

bool oapiReadItem_string (FILEHANDLE f, char *item, char *val) // "key = value" lines, first case-insensitive match
{
	if (f != (FILEHANDLE)&H.cfg) { Bug ("oapiReadItem_string of an unknown file"); return false; }
	size_t p = 0;
	while (p < H.cfg.size ()) {
		size_t e = H.cfg.find ('\n', p);
		std::string l = H.cfg.substr (p, e == std::string::npos ? std::string::npos : e - p);
		p = e == std::string::npos ? H.cfg.size () : e + 1;
		size_t q = l.find (" = ");
		if (q == std::string::npos || q != strlen (item) || strncasecmp (l.c_str (), item, q)) continue;
		strcpy (val, l.c_str () + q + 3);
		return *val != 0;
	}
	return false;
}

bool oapiReadScenario_nextline (FILEHANDLE, char *&line)
{
	if (H.throwRead) throw std::runtime_error ("host nextline");
	if (H.scnPos >= H.scnIn.size ()) return false;
	H.lineBuf = H.scnIn[H.scnPos++];
	line = H.lineBuf.data ();
	return true;
}

void oapiWriteLine (FILEHANDLE, const char *line) { H.scnOut.push_back (line); }

DWORD oapiGetVesselCount () { WorldCall (); return (DWORD)H.world.size (); }

OBJHANDLE oapiGetVesselByIndex (int i)
{
	WorldCall ();
	return i >= 0 && i < (int)H.world.size () ? H.world[i] : nullptr;
}

VESSEL *oapiGetVesselInterface (OBJHANDLE h)
{
	WorldCall ();
	if (!Live (h)) Bug ("oapiGetVesselInterface with a dead handle");
	return (VESSEL *)h; // the fake vessel stands for the interface; only GetDamageModel is called
}

int VESSEL::GetDamageModel () const { WorldCall (); return ((const FakeVessel *)(const void *)this)->damage; } // ASan: a destroyed vessel is poisoned

// the rest of the SDK the module imports (CollSdkOrbiter.cpp): world reads see an empty, mesh-less world; every one is a world call
namespace {
const FakeVessel *Fv (const VESSEL *v) { return (const FakeVessel *)(const void *)v; }
VECTOR3 Z () { return _V (0, 0, 0); }
}
double oapiGetMass (OBJHANDLE) { WorldCall (); return 1000; }
double oapiGetSize (OBJHANDLE h) { WorldCall (); return Live (h) ? ((const FakeVessel *)h)->size : 0; }
bool oapiIsVessel (OBJHANDLE h) { WorldCall (); if (H.throwIsVessel) throw std::runtime_error ("host IsVessel"); return Live (h); }
double oapiGetSimMJD () { return 51544.5; }
double oapiGetSimTime () { return 0; }
double oapiGetSysTime () { return 0; }
MESHGROUPEX *oapiMeshGroupEx (MESHHANDLE, DWORD) { WorldCall (); return nullptr; }
std::string oapiResolvePath (const char *path) { return path; }
DWORD oapiGetBaseCount (OBJHANDLE) { WorldCall (); return 0; }
void oapiGetGlobalPos (OBJHANDLE, VECTOR3 *pos) { WorldCall (); *pos = Z (); }
void oapiGetGlobalVel (OBJHANDLE, VECTOR3 *vel) { WorldCall (); *vel = Z (); }
int oapiGetMeshGroup (DEVMESHHANDLE, DWORD, GROUPREQUESTSPEC *) { WorldCall (); return -1; }
bool oapiReadItem_int (FILEHANDLE, char *, int &) { return false; }
bool oapiReadItem_bool (FILEHANDLE, char *, bool &) { return false; }
bool oapiReadItem_float (FILEHANDLE, const char *, double &) { return false; }
int oapiEditMeshGroup (DEVMESHHANDLE, DWORD, GROUPEDITSPEC *) { WorldCall (); return -1; }
void oapiGetBaseEquPos (OBJHANDLE, double *lng, double *lat, double *rad) { WorldCall (); *lng = *lat = 0; if (rad) *rad = 0; }
DWORD oapiGetGbodyCount () { WorldCall (); return 0; }
void oapiGetObjectName (OBJHANDLE, char *name, int n) { WorldCall (); if (n > 0) *name = 0; }
int oapiGetObjectType (OBJHANDLE h) { WorldCall (); return Live (h) ? OBJTP_VESSEL : OBJTP_INVALID; }
OBJHANDLE oapiGetBaseByIndex (OBJHANDLE, int) { WorldCall (); return nullptr; }
DWORD oapiMeshGroupCount (MESHHANDLE) { WorldCall (); return 0; }
void oapiAddNotification (int, const char *, const char *) {}
OBJHANDLE oapiGetGbodyByIndex (int) { WorldCall (); return nullptr; }
const char *oapiGetMeshFilename (MESHHANDLE) { WorldCall (); return nullptr; }
double oapiGetPlanetPeriod (OBJHANDLE) { WorldCall (); return 0; }
VISHANDLE *oapiObjectVisualPtr (OBJHANDLE) { WorldCall (); return nullptr; }
void oapiAnnotationSetPos (NOTEHANDLE, double, double, double, double) {}
NOTEHANDLE oapiCreateAnnotation (bool, double, const VECTOR3 &) { return nullptr; }
double oapiSurfaceElevation (OBJHANDLE, double, double) { WorldCall (); return 0; }
void oapiAnnotationSetText (NOTEHANDLE, char *) {}
void oapiGetRotationMatrix (OBJHANDLE, MATRIX3 *m) { WorldCall (); *m = _M (1, 0, 0, 0, 1, 0, 0, 0, 1); }
double oapiGetTimeAcceleration () { WorldCall (); return 1; }
void oapiSetTimeAcceleration (double) { WorldCall (); }
UINT VESSEL::GetAnimPtr (ANIMATION **anim) const { WorldCall (); *anim = nullptr; return 0; }
DEVMESHHANDLE VESSEL::GetDevMesh (VISHANDLE, UINT) const { WorldCall (); return nullptr; }
const char *VESSEL::GetMeshName (UINT) const { WorldCall (); return nullptr; }
char *VESSEL::GetClassName () const { WorldCall (); static char c[] = "FakeVessel"; return c; }
double VESSEL::GetEmptyMass () const { WorldCall (); return 1000; }
void VESSEL::GetGlobalPos (VECTOR3 &pos) const { WorldCall (); if (H.throwPos) throw std::runtime_error ("host GetGlobalPos"); pos = _V (Fv (this)->size * 1000, 0, 0); }
void VESSEL::GetGlobalVel (VECTOR3 &vel) const { WorldCall (); vel = Z (); }
UINT VESSEL::GetMeshCount () const { WorldCall (); return 0; }
void VESSEL::DefSetStateEx (const void *) const { WorldCall (); Bug ("state write in a mesh-less world"); }
void VESSEL::GetAngularAcc (VECTOR3 &a) const { WorldCall (); a = Z (); }
void VESSEL::GetAngularVel (VECTOR3 &a) const { WorldCall (); a = Z (); }
DOCKHANDLE VESSEL::GetDockHandle (UINT) const { WorldCall (); return nullptr; }
void VESSEL::GetDockParams (DOCKHANDLE, VECTOR3 &pos, VECTOR3 &dir, VECTOR3 &rot) const { WorldCall (); pos = dir = rot = Z (); }
OBJHANDLE VESSEL::GetDockStatus (DOCKHANDLE) const { WorldCall (); return nullptr; }
const OBJHANDLE VESSEL::GetGravityRef () const { WorldCall (); return nullptr; }
bool VESSEL::GetMeshOffset (UINT, VECTOR3 &ofs) const { WorldCall (); ofs = Z (); return false; }
bool VESSEL::GroundContact () const { WorldCall (); return false; }
void VESSEL::SetAngularVel (const VECTOR3 &) const { WorldCall (); Bug ("spin write in a mesh-less world"); }
bool VESSEL::GetForceVector (VECTOR3 &F) const { WorldCall (); F = Z (); return true; }
SUPERVESSELHANDLE VESSEL::GetSupervessel () const { WorldCall (); return nullptr; }
DWORD VESSEL::AttachmentCount (bool) const { WorldCall (); return 0; }
const char *VESSEL::GetAttachmentId (ATTACHMENTHANDLE) const { WorldCall (); return nullptr; }
DWORD VESSEL::GetFlightStatus () const { WorldCall (); return 0; }
const MESHHANDLE VESSEL::GetMeshTemplate (UINT) const { WorldCall (); return nullptr; }
bool VESSEL::GetThrustVector (VECTOR3 &T) const { WorldCall (); T = Z (); return false; }
bool VESSEL::GetWeightVector (VECTOR3 &G) const { WorldCall (); G = Z (); return true; }
DWORD VESSEL::GetThrusterCount () const { WorldCall (); return 0; }
double VESSEL::GetPropellantMass (PROPELLANT_HANDLE) const { WorldCall (); return 0; }
void VESSEL::GetRotationMatrix (MATRIX3 &R) const { WorldCall (); R = _M (1, 0, 0, 0, 1, 0, 0, 0, 1); }
void VESSEL::SetPropellantMass (PROPELLANT_HANDLE, double) const { WorldCall (); Bug ("tank write"); }
void VESSEL::SetRotationMatrix (const MATRIX3 &) const { WorldCall (); Bug ("attitude write in a mesh-less world"); }
DWORD VESSEL::GetPropellantCount () const { WorldCall (); return 0; }
ATTACHMENTHANDLE VESSEL::GetAttachmentHandle (bool, DWORD) const { WorldCall (); return nullptr; }
void VESSEL::GetAttachmentParams (ATTACHMENTHANDLE, VECTOR3 &pos, VECTOR3 &dir, VECTOR3 &rot) const { WorldCall (); pos = dir = rot = Z (); }
OBJHANDLE VESSEL::GetAttachmentStatus (ATTACHMENTHANDLE) const { WorldCall (); return nullptr; }
bool VESSEL::GetSuperstructureCG (VECTOR3 &cg) const { WorldCall (); cg = Z (); return false; }
PROPELLANT_HANDLE VESSEL::GetThrusterResource (THRUSTER_HANDLE) const { WorldCall (); return nullptr; }
void VESSEL::SetThrusterResource (THRUSTER_HANDLE, PROPELLANT_HANDLE) const { WorldCall (); Bug ("thruster link write"); }
double VESSEL::GetPropellantMaxMass (PROPELLANT_HANDLE) const { WorldCall (); return 0; }
void VESSEL::DelPropellantResource (PROPELLANT_HANDLE &) const { WorldCall (); Bug ("tank delete"); }
WORD VESSEL::GetMeshVisibilityMode (UINT) const { WorldCall (); return 0; }
PROPELLANT_HANDLE VESSEL::CreatePropellantResource (double, double, double) const { WorldCall (); Bug ("tank create"); return nullptr; }
THRUSTER_HANDLE VESSEL::GetThrusterHandleByIndex (DWORD) const { WorldCall (); return nullptr; }
PROPELLANT_HANDLE VESSEL::GetPropellantHandleByIndex (DWORD) const { WorldCall (); return nullptr; }
void VESSEL::GetPMI (VECTOR3 &pmi) const { WorldCall (); pmi = _V (1, 1, 1); }
double VESSEL::GetMass () const { WorldCall (); return 1000; }
char *VESSEL::GetName () const { WorldCall (); return const_cast<char *> (Fv (this)->name); }
void VESSEL::AddForce (const VECTOR3 &, const VECTOR3 &) const { WorldCall (); Bug ("force in a mesh-less world"); }
bool VESSEL::Playback () const { WorldCall (); return false; }
UINT VESSEL::DockCount () const { WorldCall (); return 0; }
bool VESSEL::Recording () const { WorldCall (); return false; }
bool VESSEL::ShiftMesh (UINT, const VECTOR3 &) const { WorldCall (); return false; }

void oapiOpenDialog (ImGuiDialog *d) // OrbiterAPI.cpp, DlgMgr.h: added once to the session's manager, activated
{
	if (!H.dlgMgr) { Bug ("oapiOpenDialog without a dialog manager"); return; }
	if (!H.loadDialog) H.loadDialog = d;
	if (d != H.loadDialog) Bug ("a second dialog object in one load");
	if (std::find (H.dlgList.begin (), H.dlgList.end (), d) != H.dlgList.end ()) return;
	H.dlgList.push_back (d);
	H.opens++;
}

void oapiCloseDialog (ImGuiDialog *d)
{
	auto it = std::find (H.dlgList.begin (), H.dlgList.end (), d);
	if (it != H.dlgList.end ()) H.dlgList.erase (it);
}

ImGuiDialog::~ImGuiDialog ()
{
	H.dlgGone++;
	if (!H.unloading) Bug ("dialog object deleted outside ExitModule");
	oapiCloseDialog (this);
}

void ImGuiDialog::Display () {}

oapi::ModuleNV::ModuleNV (void *h) : version (1), hModule (h) {}
oapi::Module::Module (void *h) : ModuleNV (h) {}
oapi::Module::~Module () {}
void oapi::Module::clbkSimulationStart (RenderMode) {}
void oapi::Module::clbkSimulationEnd () {}
void oapi::Module::clbkPreStep (double, double, double) {}
void oapi::Module::clbkPostStep (double, double, double) {}
void oapi::Module::clbkFocusChanged (OBJHANDLE, OBJHANDLE) {}
void oapi::Module::clbkTimeAccChanged (double, double) {}
void oapi::Module::clbkDeleteVessel (OBJHANDLE) {}
void oapi::Module::clbkPause (bool) {}

namespace {

const char *SoPath ()
{
	const char *e = getenv ("COLL_LIFECYCLE_SO"); // a hand-built variant for the negative checks
	return e && *e ? e : COLL_SO;
}

void *Load () // Orbiter.cpp LoadModule: dlopen runs the ELF constructor, which calls InitModule
{
	H.module = nullptr;
	H.loadDialog = nullptr;
	int before = H.inits;
	void *h = dlopen (SoPath (), RTLD_NOW);
	if (!h) printf ("dlopen: %s\n", dlerror ());
	REQUIRE (h);
	CHECK (H.inits == before + 1);
	REQUIRE (H.module);
	return h;
}

void Unload (void *h) // CloseApp (true): ModuleDetach runs ExitModule, then dlclose
{
	H.unloading = H.noWorld = true;
	void (*d) () = (void (*) ())OwnProc (h, "ModuleDetach");
	REQUIRE (d);
	d ();
	H.unloading = H.noWorld = false;
	dlclose (h);
	H.module = nullptr;
	CHECK (H.cmds.empty ());
	void *again = dlopen (SoPath (), RTLD_NOW | RTLD_NOLOAD);
	CHECK_FALSE (again); // unmapped: -fno-gnu-unique keeps no STB_GNU_UNIQUE reference
	if (again) dlclose (again);
}

OBJHANDLE Make (int i, const char *name, int damage)
{
	FakeVessel *v = &H.slots[i];
	ASAN_UNPOISON_MEMORY_REGION (v, sizeof *v);
	v->size = 10 + i;
	v->damage = damage;
	snprintf (v->name, sizeof v->name, "%s", name);
	H.world.push_back (v);
	return v;
}

void Kill (OBJHANDLE h) // the vessel's memory is gone: any later read is an ASan report
{
	H.world.erase (std::find (H.world.begin (), H.world.end (), h));
	ASAN_POISON_MEMORY_REGION (h, sizeof (FakeVessel));
}

void DestroyWorld () { while (!H.world.empty ()) Kill (H.world.back ()); }

void Frames (int n)
{
	for (int k = 0; k < n; k++) {
		H.module->clbkPreStep (k * 0.02, 0.02, 51544.5);
		H.module->clbkPostStep (k * 0.02, 0.02, 51544.5);
	}
}

void LoadState (void *h, std::vector<std::string> lines) // Orbiter.cpp: opcLoadState only when the scenario has the block
{
	H.scnIn = std::move (lines);
	H.scnPos = 0;
	void (*f) (FILEHANDLE) = (void (*) (FILEHANDLE))OwnProc (h, "opcLoadState");
	REQUIRE (f);
	H.noWorld = true; // scenario lines only, also when a stale session ends here
	f (nullptr);
	H.noWorld = false;
}

std::vector<std::string> SaveState (void *h)
{
	H.scnOut.clear ();
	void (*f) (FILEHANDLE) = (void (*) (FILEHANDLE))OwnProc (h, "opcSaveState");
	REQUIRE (f);
	f (nullptr);
	return H.scnOut;
}

void Command () // Ctrl-F4 "Collision damage" inside a session
{
	REQUIRE (H.cmds.size () == 1);
	CHECK (std::string (H.cmds[0].desc).rfind ("Collision damage", 0) == 0);
	H.cmds[0].fn (H.cmds[0].ctx);
	CHECK (H.dlgList.size () == 1);
}

void Outside (OBJHANDLE v) // outside Running: the callbacks E4 11.4 gates, a warp from PostCreation included, and the command do nothing
{
	REQUIRE (H.cmds.size () == 1);
	int opens = H.opens, bugs = H.bugs;
	size_t logs = H.log.size ();
	char kstate[256] = {};
	H.noWorld = true;
	H.module->clbkTimeAccChanged (10.0, 1.0);
	H.module->clbkVesselJump (v);
	H.module->clbkPause (true);
	H.module->clbkPause (false);
	CHECK_FALSE (H.module->clbkProcessKeyboardImmediate (kstate, false));
	H.cmds[0].fn (H.cmds[0].ctx);
	H.noWorld = false;
	CHECK (H.opens == opens);
	CHECK (H.bugs == bugs);
	CHECK (H.log.size () == logs);
}

void NormalClose () // Orbiter.cpp CloseSession, ShutdownMode 0: dialog manager, then DestroyWorld, then clbkSimulationEnd
{
	H.dlgMgr = false;
	H.dlgList.clear ();
	DestroyWorld ();
	H.noWorld = true;
	H.module->clbkSimulationEnd ();
	H.noWorld = false;
}

std::vector<std::string> Lines (const char *prefix)
{
	std::vector<std::string> out;
	for (const std::string &l : H.log) if (!l.compare (0, strlen (prefix), prefix)) out.push_back (l);
	return out;
}

std::string A1 (int n, int model, int check, int damage, int log)
{
	char b[256];
	snprintf (b, sizeof b, "Collision: active version=%s session=%d model=%d response=1 check=%d damage=%d log=%d render=0", COLL_ADDON_VERSION, n, model, check, damage, log);
	return b;
}

}

TEST_CASE ("CollPlugin lifecycle: sessions, close paths, unload and re-load")
{
	void *h = Load ();
	H.cfgMissing = false;
	H.cfg = "CollisionModel = 1\nCollisionCheck = TRUE\nCollisionLog = 2\n";

	// S1: a save and the gated callbacks before any session, scenario block, a vessel created in PostCreation that sets the warp, the command twice, a save, normal close, the command after it
	Make (0, "A", 1);
	Make (1, "B", 1);
	H.dlgMgr = true;
	CHECK (SaveState (h) == std::vector<std::string> { "COLLA 1" });
	Outside (H.world[0]);
	LoadState (h, { "COLLA 1", "VESSEL 0 A ShuttlePB", "XDMG 1 7 0", "END_VESSEL" }); // class ShuttlePB: no live match, kept dormant
	H.module->clbkNewVessel (Make (2, "C", 1));
	Outside (H.world[2]);
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	Frames (3);
	Command ();
	Command ();
	CHECK (SaveState (h) == std::vector<std::string> { "COLLA 1", "VESSEL 0 A ShuttlePB", "  XDMG 1 7 0", "END_VESSEL" });
	NormalClose ();
	Outside (&H.slots[0]); // a dead handle: ASan reports any read

	// S2: no cfg file; a vessel created before the block is read keeps the session; a save and the gated callbacks before the start; the same handle values; delete and reuse
	H.cfgMissing = true;
	Make (0, "A2", 0);
	H.module->clbkNewVessel (Make (1, "B2", 0));
	LoadState (h, { "COLLA 1", "BASE Earth:Cape Canaveral", "END_BASE" });
	H.dlgMgr = true;
	CHECK (SaveState (h) == std::vector<std::string> { "COLLA 1", "BASE Earth:Cape Canaveral", "END_BASE" }); // the pending block, before the start
	Outside (H.world[1]);
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	Frames (2);
	OBJHANDLE b = H.world[1];
	H.noWorld = true;
	H.module->clbkDeleteVessel (b);
	H.noWorld = false;
	Kill (b);
	H.module->clbkNewVessel (Make (1, "D", 0));
	Frames (2);
	NormalClose ();

	// S3: the cfg changes inside the session; clbkSimulationEnd never comes
	H.cfgMissing = false;
	H.cfg = "CollisionLog = 3\n";
	Make (0, "A3", 1);
	H.dlgMgr = true;
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	H.cfg = "CollisionLog = 4\n";
	Frames (1);
	Command ();
	H.dlgMgr = false;
	H.dlgList.clear ();
	DestroyWorld ();

	// S4: the next launch reads a block (the stale session ends there); fast close: clbkSimulationEnd with the world alive, then unload
	Make (0, "A4", 1);
	H.dlgMgr = true;
	LoadState (h, { "COLLA 1" });
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	Frames (1);
	H.dlgMgr = false;
	H.dlgList.clear ();
	H.noWorld = true;
	H.module->clbkSimulationEnd ();
	H.noWorld = false;
	Unload (h);
	DestroyWorld ();

	// S5: re-tick, an end without a start (a failed launch), one more session, untick
	h = Load ();
	H.noWorld = true;
	H.module->clbkSimulationEnd ();
	H.noWorld = false;
	Make (0, "A5", 1);
	H.dlgMgr = true;
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	Frames (1);
	Command ();
	NormalClose ();
	Unload (h);

	CHECK (H.inits == 2);
	CHECK (H.regs == 2);
	CHECK (H.unregs == 2);
	CHECK (H.dlgGone == 2);
	CHECK (H.opens == 3);
	CHECK (H.cfgOpens == 5);
	CHECK (H.cfgCloses == 4);
	CHECK (H.worldBad == 0);
	CHECK (H.bugs == 0);
	const std::vector<std::string> lifecycle = {
		"Collision: session 1 created (load)", "Collision: session 1 ended (end)",
		"Collision: session 2 created (new vessel)", "Collision: session 2 ended (end)",
		"Collision: session 3 created (start)", "Collision: session 3 ended (stale)",
		"Collision: session 4 created (load)", "Collision: session 4 ended (end)", "Collision: unloaded cmds=0",
		"Collision: session 1 created (start)", "Collision: session 1 ended (end)", "Collision: unloaded cmds=0" };
	std::vector<std::string> got;
	for (const std::string &l : H.log) if (!l.compare (0, 19, "Collision: session ") || !l.compare (0, 20, "Collision: unloaded ")) got.push_back (l);
	CHECK (got == lifecycle);
	CHECK (Lines ("Collision: loaded (addon ").size () == 2);
	CHECK (Lines ("Collision: active ") == std::vector<std::string> { A1 (1, 1, 1, 1, 2), A1 (2, 1, 0, 0, 1), A1 (3, 1, 0, 1, 3), A1 (4, 1, 0, 1, 4), A1 (1, 1, 0, 1, 4) });
	CHECK (Lines ("Collision damage block: ") == std::vector<std::string> { "Collision damage block: vessels=1 bases=0 unknown=0 skipped=0", "Collision damage block: vessels=0 bases=0 unknown=2 skipped=0", "Collision damage block: vessels=0 bases=0 unknown=0 skipped=0" });
	CHECK (Lines ("Collision: Config/Collision.cfg not found").size () == 1);
	std::vector<std::string> sums = Lines ("Collision summary: ");
	REQUIRE (sums.size () == 5);
	CHECK (sums[0].rfind ("Collision summary: frames=3 contacts=0 ", 0) == 0);
	CHECK (sums[1].rfind ("Collision summary: frames=4 contacts=0 ", 0) == 0);
	CHECK (Lines ("Collision perf: ").size () == 5);
}

TEST_CASE ("CollPlugin lifecycle: an exception in a callback or an export turns the session off, the next session runs")
{
	void *h = Load ();
	H.cfgMissing = false;
	H.cfg = "CollisionModel = 1\nCollisionLog = 1\n";
	size_t log0 = H.log.size ();
	int bugs = H.bugs, bad = H.worldBad, opens = H.opens;
	auto since = [&] (const char *prefix) { std::vector<std::string> out; for (size_t i = log0; i < H.log.size (); i++) if (!H.log[i].compare (0, strlen (prefix), prefix)) out.push_back (H.log[i]); return out; };
	int (*repair) (OBJHANDLE) = (int (*) (OBJHANDLE))OwnProc (h, "collaRepairVessel");
	REQUIRE (repair);

	// F1: a throw inside the pre-step; later callbacks, the command and the export make no world call
	Make (0, "E1", 1);
	H.dlgMgr = true;
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	Frames (1);
	H.throwPos = true;
	Frames (1);
	H.throwPos = false;
	CHECK (since ("Collision: error in ") == std::vector<std::string> { "Collision: error in clbkPreStep: host GetGlobalPos; collisions off until the session ends" });
	char kstate[256] = {};
	H.noWorld = true;
	Frames (3);
	H.module->clbkTimeAccChanged (10.0, 1.0);
	H.module->clbkPause (true);
	CHECK_FALSE (H.module->clbkProcessKeyboardImmediate (kstate, true));
	H.cmds[0].fn (H.cmds[0].ctx);
	CHECK (repair (H.world[0]) == 0);
	H.noWorld = false;
	H.module->clbkNewVessel (Make (1, "E2", 1)); // the id map only
	CHECK (SaveState (h).front () == "COLLA 1");
	NormalClose ();
	CHECK (H.opens == opens);
	CHECK (since ("Collision summary: ").empty ()); // an off session has no summary
	CHECK (since ("Collision: session ").size () == 2);

	// F2: the next session runs; a throw inside an export turns it off once
	Make (0, "E3", 1);
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	Frames (2);
	H.throwIsVessel = true;
	CHECK (repair (H.world[0]) == 0);
	CHECK (repair (H.world[0]) == 0);
	H.throwIsVessel = false;
	H.noWorld = true;
	Frames (2);
	H.noWorld = false;
	NormalClose ();
	CHECK (since ("Collision: error in ") == std::vector<std::string> { "Collision: error in clbkPreStep: host GetGlobalPos; collisions off until the session ends",
		"Collision: error in collaRepairVessel: host IsVessel; collisions off until the session ends" });

	// F3: a clean session after both
	Make (0, "E4", 1);
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	Frames (2);
	NormalClose ();
	std::vector<std::string> sums = since ("Collision summary: ");
	REQUIRE (sums.size () == 1);
	CHECK (sums[0].rfind ("Collision summary: frames=2 ", 0) == 0);
	Unload (h);
	CHECK (H.worldBad == bad);
	CHECK (H.bugs == bugs);
}

TEST_CASE ("CollPlugin lifecycle: a throwing InitModule leaves no command or dialog; a load that threw ends as stale at the next load")
{
	size_t log0 = H.log.size ();
	int bugs = H.bugs, regs = H.regs, gone = H.dlgGone;
	auto since = [&] (const char *prefix) { std::vector<std::string> out; for (size_t i = log0; i < H.log.size (); i++) if (!H.log[i].compare (0, strlen (prefix), prefix)) out.push_back (H.log[i]); return out; };

	// T1: registration throws, the command is registered last, the dialog object goes with the module object
	H.throwReg = H.unloading = true;
	H.module = nullptr;
	void *h = dlopen (SoPath (), RTLD_NOW);
	H.throwReg = H.unloading = false;
	REQUIRE (h);
	CHECK (H.module == nullptr);
	CHECK (H.cmds.empty ());
	CHECK (H.regs == regs);
	CHECK (H.dlgGone == gone + 1);
	CHECK (since ("Collision: error in ") == std::vector<std::string> { "Collision: error in InitModule: host RegisterCustomCmd; collisions off until the session ends" });
	void (*d) () = (void (*) ())OwnProc (h, "ModuleDetach");
	REQUIRE (d);
	d ();
	dlclose (h);

	// T2: opcLoadState throws and no clbkSimulationEnd follows; the next load ends that session as stale and runs
	h = Load ();
	H.cfgMissing = false;
	H.cfg = "CollisionModel = 1\nCollisionLog = 1\n";
	Make (0, "L1", 1);
	H.throwRead = true;
	LoadState (h, { "COLLA 1" });
	H.throwRead = false;
	LoadState (h, { "COLLA 1" });
	H.module->clbkSimulationStart (oapi::Module::RENDER_NONE);
	Frames (2);
	NormalClose ();
	Unload (h);
	CHECK (since ("Collision: error in ") == std::vector<std::string> { "Collision: error in InitModule: host RegisterCustomCmd; collisions off until the session ends",
		"Collision: error in opcLoadState: host nextline; collisions off until the session ends" });
	CHECK (since ("Collision: session ") == std::vector<std::string> { "Collision: session 1 created (load)", "Collision: session 1 ended (stale)",
		"Collision: session 2 created (load)", "Collision: session 2 ended (end)" });
	std::vector<std::string> sums = since ("Collision summary: ");
	REQUIRE (sums.size () == 1);
	CHECK (sums[0].rfind ("Collision summary: frames=2 ", 0) == 0);
	CHECK (H.bugs == bugs);
}
