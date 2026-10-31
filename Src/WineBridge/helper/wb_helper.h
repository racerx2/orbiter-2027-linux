// WineBridge helper: what its parts share
#pragma once
#include "../proto/wb_proto.h"
#include <stdlib.h>

class VESSEL;
class VESSEL2;
class VESSEL3;
namespace oapi { class Font; class Pen; }

namespace wb {
	bool Open (const char *shm, const char *fifoIn, const char *fifoOut);
	WbWriter &Begin (uint32_t fn);   // a call to the Linux side: write its arguments, then Call
	WbReader Call ();                // send it, serve the calls that come meanwhile, return the reply
	void Serve ();                   // the main loop: Linux's calls, until H_QUIT
	void Dispatch (uint32_t fn, WbReader &r, WbWriter &w); // main.cpp: one call from Linux
	void Log (const char *fmt, ...);
	void OutStr (WbReader &r, char *buf, int n); // a string result into the module's buffer of n chars, always terminated
	void InStr (WbWriter &w, const char *buf, int n); // the buffer's string as it is, at most n - 1 chars
	void OutStrIf (WbReader &r, char *buf, int n); // the string, if Orbiter changed it

	// text (text.cpp): Orbiter shows UTF-8; a module's text is in its ANSI code page, or UTF-8
	const int API_UTF8 = 251217; // upstream's text is UTF-8 from here (its dialogs ported to ImGui)
	struct Utf8 { // a module's text in UTF-8: as it is when it is UTF-8 (ASCII is), else from the ANSI code page
		Utf8 (const char *str, int len = -1);   // len < 0: up to the terminating zero; s null when str is
		Utf8 (const wchar_t *w, uint32_t wn);    // UTF-16
		~Utf8 () { free (own); }
		Utf8 (const Utf8 &) = delete;
		Utf8 &operator= (const Utf8 &) = delete;
		const char *s;
		uint32_t n;
	private:
		char *own;
	};
	void TextStr (WbWriter &w, const char *s);                       // w.str of a module's text
	uint32_t Utf8Cut (const char *s, uint32_t n, uint32_t room);     // the bytes of s that fit room, ending at a character
	char *ToModule (const char *utf8, uint32_t n, const void *code); // Orbiter's text for the module code is in (malloc'ed)

	VESSEL *Obj (uint32_t vid);
	int CallerApi (const void *addr); // the SDK date (GetModuleVersion) of the module the code at addr is in
	int VesselApi (uint32_t vid);      // ... of the module that made the vessel
	void InitLibCalled (void *hModule);
	int ModuleNumber (const void *hModule); // the helper's number of a module (also the one being loaded); 0: not one
	int AddrModule (const void *addr);       // ... of the module the code or data at addr is in
	const void *Resolve (const void *p);     // a vtable entry through the import thunks to the function
	// the vtable slot a pointer to a virtual member function calls through: its vcall thunk loads the vtable (mov eax, [ecx]), then
	// jumps through an entry (jmp [eax + offset]) or loads it first (mov eax, [eax + offset]; jmp eax)
	template <class F> int VcallSlot (F f)
	{
		void *code;
		memcpy (&code, &f, sizeof code); // the thunk comes first, also in the larger member pointers
		const unsigned char *b = (const unsigned char *)Resolve (code);
		if (b[0] != 0x8B || b[1] != 0x01) return -1;
		int32_t off;
		if (b[2] == 0xFF || b[2] == 0x8B) { // jmp [eax + ...], or mov eax, [eax + ...] then jmp eax
			int len = b[3] == 0x20 || b[3] == 0x00 ? 0 : b[3] == 0x60 || b[3] == 0x40 ? 1 : b[3] == 0xA0 || b[3] == 0x80 ? 4 : -1;
			if (len < 0) return -1;
			off = len == 0 ? 0 : len == 1 ? (int8_t)b[4] : (memcpy (&off, b + 4, 4), off);
			if (b[2] == 0x8B && (b[4 + len] != 0xFF || b[5 + len] != 0xE0)) return -1;
			return off / 4;
		}
		return -1;
	}

	// live items (live.cpp): module memory Orbiter reads every frame; the value at addr is sent whenever it changed
	enum { TAG_NONE, TAG_EXHAUST, TAG_STREAM, TAG_ECC, TAG_LIGHT, TAG_BEACON }; // what Orbiter stops reading when it is deleted
	uint32_t Live (uint32_t vid, int kind, const void *addr, uint32_t n = 0); // its id; n: a string buffer's size, the bytes' count
	const uint8_t *LiveValue (uint32_t id, uint32_t *len); // the value the registering call sends
	void LiveTag (uint32_t id, int tagkind, uint32_t tag);
	void LiveDropTag (uint32_t vid, int tagkind, uint32_t tag);
	void LiveDropTagKind (uint32_t vid, int tagkind);
	uint32_t LiveFind (uint32_t vid, int kind, const void *addr); // the item of that memory (0: none)
	const void *LiveAddr (uint32_t id);                             // ... the memory of an item
	void LiveDrop (uint32_t vid);                    // the vessel's items, at ovcExit
	void LiveClear ();                               // all: the session ends and the modules are unloaded
	uint32_t LiveSync (uint8_t *out, uint32_t room); // the changed values as WbSync records

	// meshes (manual.cpp): the module's copies of Orbiter's mesh groups and materials go back when they changed
	void MeshUse (const void *hMesh);  // before a call that names the mesh (0: any mesh)
	void MeshFrame ();                 // once a frame
	bool MeshRecent ();                // copies fetched during the call from Orbiter now ending
	void MeshStoreRecent ();           // ... go back before its reply
	void VesselGone (uint32_t vid);    // ovcExit: its live items and lights
	bool LightsChanged ();             // a light object changed with no setter (light.cpp) ...
	void LightsStore ();               // ... then it goes to Orbiter
	bool WatchChanged ();              // a watched object of a module changed (vdata.cpp: 2006 animation components, transforms, animation copies) ...
	void WatchStore ();                // ... then it goes to Orbiter
	void MeshRefresh (uint32_t mesh, uint32_t idx); // the module's copy of a group Orbiter changed is read again (manual.cpp)
	bool VisualsWanted ();             // a module has a body's visual slot (oapi.cpp) ...
	void VisualsRefresh ();            // ... which gets Orbiter's handle before each call from Linux
	bool AnimsCopied ();               // a module has a copy of a vessel's animations
	void AnimsRefresh ();              // the animation copies a module has read Orbiter's states into, before a call from Linux
	bool AnimsChanged ();              // a state a module wrote into its copy of the animations ...
	void AnimsStore ();                // ... goes into Orbiter's
	void AnimsGone (uint32_t vid);
	void WatchGone (uint32_t vid);     // a deleted vessel: its watched objects and animation copy go
	void PutVS (WbWriter &w, const void *vs); // a VESSELSTATUS on the wire, handles as ids (manual.cpp)
	void GetVS (WbReader &r, void *vs);
	void TransformMade (uint32_t vid, const void *comp, void *trans); // AddAnimationComponent: the module's transform of a component

	// drawing (draw.cpp)
	bool DrawHUD3 (VESSEL3 *v, bool old, int mode, const void *hps, uint32_t surf, uint32_t skp); // with a sketchpad for Orbiter's, of the 2010 SDK if old
	bool DrawHUD2 (VESSEL2 *v, int mode, const void *hps, uint32_t surf, uint32_t skp); // hud.cpp: the HDC HUD, drawn with Orbiter's sketchpad
	bool HudDefault (uint32_t vid, int mode, const void *hps, void *hDC); // VESSEL2's own HDC HUD: Orbiter's default, for the HUD's DC
	uint32_t SketchpadId (void *skp); // Orbiter's sketchpad a helper sketchpad stands for; 0: not one of them
	bool SkpClbk (bool old, uint32_t surf, uint32_t skp, bool (*f) (void *ctx, void *skp), void *ctx); // f with one for Orbiter's
	void *DcOpen (WbReader &r);           // an HDC on a copy of the image the reader holds (i32 w, h, rows); 0 if none
	void DcClose (void *hDC, WbWriter &w); // its changed part (b, then i32 x, y, w, h, rows); the DC freed
	void *GdiFont (int height, bool prop, const char *face, int style, int orientation); // as the 2010 clients made it
	oapi::Font *FontOf (uint32_t id); // the helper's object for Orbiter's font, pen of that id
	oapi::Pen *PenOf (uint32_t id);

	// MFD modes, MFDs and external MFDs (mfd.cpp); module objects (module.cpp)
	int RegisterMode (const char *name, unsigned long key, void *context, const void *msgproc, uint32_t vessel); // Orbiter's mode id
	bool UnregisterMode (int id, uint32_t vessel);
	void ServeMfd (uint32_t fn, WbReader &r, WbWriter &w); // H_MFDOPEN, H_MFDCLOSED, H_MFD, H_EMFD
	void ServeModule (WbReader &r, WbWriter &w);           // H_MODULE
	void MfdFreed (int module);    // the module was freed (its ExitModule ran): its modes, MFDs and external MFDs
	void ModuleFreed (int module); // ... its module objects
	bool MfdProbe ();              // the classes' vtable slots are the 2010 SDK's

	// the module's entry point e (WbExport), or of that name; its handle; its operator delete or free (0: none it imports)
	void *ModuleExport (int module, int e);
	void *ModuleExport (int module, const char *name);
	void *ModuleHandle (int module);
	void *ModuleDelete (int module);
	void *ModuleAlloc (int module, size_t n);  // memory the module's own delete frees (its operator new or malloc; else the helper's)
	void ModuleRelease (int module, void *p);  // ... and back (its operator delete or free; else the helper's)

	// Launchpad items, custom commands, help (lpad.cpp); camera modes and external camera control (camera.cpp)
	void ServeLpad (WbReader &r, WbWriter &w); // H_LPAD
	void ServeCustomCmd (WbReader &r);         // H_CUSTOMCMD
	void LpadFreed (int module);
	void ServeEcc (WbReader &r, WbWriter &w);  // H_ECC
	void ServeEccGone (WbReader &r);            // H_ECCGONE
	void CameraFreed (int module);
	void ScriptFreed (int module); // script.cpp: the interpreter library's module gone

	// dialogs (dialog.cpp)
	void Pump ();                        // H_PUMP: the helper's window messages
	void ServeDialogs (WbReader &r);     // H_DIALOGS
	void DialogsFreed (void *hModule);   // the module is being freed: its dialogs go first

	// Linux's Lua states in the modules' hands (luaremote.cpp)
	void ServeLua (uint32_t fn, WbReader &r, WbWriter &w);           // H_LUACALL, H_LUAHOOK, H_LUAFREE
	int LuaGeneric (VESSEL3 *v, int msg, int prm, uint32_t L, bool *err); // clbkGeneric with Linux's state L; err: a Lua error raised
	uint32_t LuaRemoteId (const void *L); // Linux's state a remote state stands for; 0: not one
	void LuaRaise ();                     // the Lua error Linux reported: to the innermost call from Linux (if any)
	void *LuaBarrier ();                  // a served call: a Lua error in it doesn't unwind past it ...
	void LuaRestore (void *t);            // ... then the outer calls'
	void LuaRelease ();                   // the outermost call from Linux returned: the copies go

	// the helper's Lua states in native modules' hands (luaserve.cpp)
	void ServeLuaH (WbReader &r, WbWriter &w); // H_LUA
	uint32_t LuaServeState (void *L);     // a helper state a Linux vessel gets: its id (0: none)
	void LuaServeRaise (void *L);         // the native module's Lua error, raised on the helper state (doesn't return)
	void LuaServeFreed (WbWriter &w);     // H_LUAFREE's reply: the native blocks Lua collected
	void CountLuaFree ();                 // channel.cpp: one more such block (the header's count)
	uint32_t VesselIdOf (const void *v);  // the vessel whose helper object v is (a bridged module's, a proxy); 0 if none

	// celestial body and atmosphere modules (celbody.cpp)
	bool CelbodyProbe ();                         // the classes' vtable slots are the SDKs'
	void ServeCelbody (WbReader &r, WbWriter &w); // H_CELBODY
	void CelbodyFreed (int module);               // the module was freed: its objects are gone

	// vessel hooks (hooks.cpp): vessel vtables a module may patch
	bool HooksProbe (void **proxyVt);     // the class copies take VirtualProtect's WRITECOPY, as a DLL's vtable
	void HooksTrack (void **vt, int n);   // a module's vessel class: its first n slots are watched
	void **ProxyClass (uint32_t cls);     // the vtable of the Proxies of that Linux class (its copy); 0: none free
	void ClassGone (uint32_t cls);        // the class's last Linux vessel is gone: its copy goes
	void HooksFreed (const void *base, size_t size); // a module's image is gone: its classes are watched no more
	bool HooksScan ();                    // a watched vtable changed since Linux was told (once until H_HOOKS)
	void ServeHooks (WbWriter &w);        // H_HOOKS
	int Slot (int c);                     // main.cpp: the vtable slot of callback c
	void ObjMasks (void **vt, WbWriter &w, uint32_t *n); // main.cpp: the masks of the module objects of that class, counted in n
	VESSEL *ProxyOf (uint32_t vid);       // manual.cpp: the helper's object of a Linux vessel; 0 if none made yet
	uint32_t ProxyIdOf (const void *v);   // ... the Linux vessel whose object v is; 0 if none
	void ProxyGone (uint32_t vid);
	void **ProxyVt ();                    // the Proxy class's own vtable
	void PutHps (WbWriter &w, const void *hps); // api2010.cpp: a HUDPAINTSPEC

	// VESSELSTATUS2 (vs2 on the wire, manual.cpp); lists the helper reads in are its own (new[])
	void PutStatus (WbWriter &w, const void *vs2);
	void GetStatus (WbReader &r, void *vs2);  // into a struct: a list that differs replaces the old one, as Orbiter's parser does
	void FreeStatus (void *vs2);

	inline uint32_t Id (const void *h) { return (uint32_t)(uintptr_t)h; }
	inline void *Ptr (uint32_t id) { return (void *)(uintptr_t)id; }
}
