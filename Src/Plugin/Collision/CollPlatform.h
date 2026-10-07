// not upstream: collision addon, the SDK differences between the Linux port and Orbiter 2024 (Windows) and the one feature macro
#ifndef COLLPLATFORM_H
#define COLLPLATFORM_H
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include "OrbiterAPI.h"
#include "ModuleAPI.h"
#include "VesselAPI.h"
#include "CollSdk.h"                          // COLLN_*

#ifndef COLL_HAVE_IMGUI // the build never sets it; only the negative compile check (Collision.Win2024.Compile) forces it
#ifdef OAPINOTIF_WARNING
#define COLL_HAVE_IMGUI 1 // ImGuiDialog and oapiAddNotification arrived in one upstream commit (70cc506), with OAPINOTIF_*
#else
#define COLL_HAVE_IMGUI 0
#endif
#endif

using CollHModule = decltype (std::declval<oapi::ModuleNV &> ().GetModule ()); // HINSTANCE in Orbiter 2024, void* in the port

#ifdef OAPINOTIF_WARNING
static_assert (COLLN_SUCCESS == OAPINOTIF_SUCCESS && COLLN_WARNING == OAPINOTIF_WARNING && COLLN_ERROR == OAPINOTIF_ERROR && COLLN_INFO == OAPINOTIF_INFO);
#endif

// the skeleton's SDK calls until E2 step 9 moves them behind CollSdk; char* parameters of 2024 get writable copies
inline void CollLogLine (const char *s) { oapiWriteLogV ("%s", s); }

inline void CollLogF (const char *fmt, ...)
{
	char buf[1024];
	va_list a;
	va_start (a, fmt);
	vsnprintf (buf, sizeof buf, fmt, a);
	va_end (a);
	oapiWriteLogV ("%s", buf);
}

inline void CollWriteLine (FILEHANDLE f, const char *s) { std::string b (s); oapiWriteLine (f, b.data ()); }

inline bool CollReadLine (FILEHANDLE f, std::string &line)
{
	char *l = nullptr;
	if (!oapiReadScenario_nextline (f, l) || !l) return false;
	line = l;
	return true;
}

inline int CollRegisterCmd (const char *label, char *descStatic, CustomFunc fn, void *ctx) // the core copies the label and keeps desc
{
	std::string l (label);
	return (int)oapiRegisterCustomCmd (l.data (), descStatic, fn, ctx);
}

inline bool CollUnregisterCmd (int id) { return oapiUnregisterCustomCmd (id); }

inline FILEHANDLE CollOpenCfg (const char *name) { return oapiOpenFile (name, FILE_IN_ZEROONFAIL, CONFIG); } // NULL if missing

inline void CollCloseCfg (FILEHANDLE f) { oapiCloseFile (f, FILE_IN_ZEROONFAIL); }

inline bool CollReadItem (FILEHANDLE f, const char *item, std::string &val)
{
	std::string it (item);
	char buf[1024] = ""; // the core copies at most 511 bytes
	if (!oapiReadItem_string (f, it.data (), buf)) return false;
	val = buf;
	return true;
}

#if COLL_HAVE_IMGUI
inline void CollOpenDialog (ImGuiDialog *d) { oapiOpenDialog (d); } // a second open of an open dialog is ignored
#endif

inline uint32_t CollVesselCount () { return (uint32_t)oapiGetVesselCount (); }

inline OBJHANDLE CollVesselByIndex (uint32_t i) { return oapiGetVesselByIndex ((int)i); }

inline int CollDamageModel () // the session's Damage model setting through the first vessel, -1 without vessels
{
	if (!oapiGetVesselCount ()) return -1;
	VESSEL *v = oapiGetVesselInterface (oapiGetVesselByIndex (0));
	return v ? v->GetDamageModel () : -1;
}

inline std::string CollResolvePath (const char *path)
{
#ifdef _WIN32
	return path; // no oapiResolvePath in 2024; the file system ignores case
#else
	return oapiResolvePath (path);
#endif
}
#endif
