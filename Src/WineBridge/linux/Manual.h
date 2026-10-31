// WineBridge: the hand-written calls of step 2 (Manual.cpp, Draw.cpp)
#pragma once
#include "../proto/wb_proto.h"

class VESSEL;
class VESSEL3;
struct lua_State;
class CELBODY;
class CELBODY2;
class ATMOSPHERE;
class QImage;
class QPainter;
namespace oapi { class Sketchpad; }

// drawing (Draw.cpp)
bool WbServeDraw (uint32_t fn, WbReader &r, WbWriter &w); // false: not a drawing call
void WbSkpBegin (oapi::Sketchpad *skp);                  // a sketchpad Orbiter passes to a callback, for that call
void WbSkpEnd (oapi::Sketchpad *skp);
void WbPutHps (WbWriter &w, const void *hps);            // a HUDPAINTSPEC
void WbGetHps (WbReader &r, void *hps);
void WbDrawClear ();
QImage *WbPainterImage (QPainter *p);                    // the image a painter draws on; null if it isn't one in memory
void WbPutImage (WbWriter &w, const QImage &img);        // i32 w, h, the rows (32 bits)
void WbDrawChanged (WbReader &r, QPainter *p);           // the pixels GDI changed in the helper's copy, drawn as they are
uint32_t WbToolId (const void *tool);                    // a font, pen or brush of Orbiter's, handed to the helper
oapi::Sketchpad *WbSkpOf (uint32_t id);                  // a sketchpad the helper may use now; null if none

// MFD modes, MFDs, external MFDs and module objects (Mfd.cpp)
bool WbServeMfd (uint32_t fn, WbReader &r, WbWriter &w); // false: not one of these
void WbModuleObjectsFreed (int module);                  // the module is freed: its module objects' proxies go
void *WbSlotFor (int module);                            // the slot of the helper's module (Slots.cpp); null if none
int WbSlotModule (void *hModule);                        // the helper's module of a slot; 0 if not a slot

// celestial body and atmosphere modules (Celbody.cpp)
bool WbServeCelbody (uint32_t fn, WbReader &r, WbWriter &w); // false: not L_Celbody
CELBODY *WbCbInit (int module, void *hBody);                // a slot's InitInstance, ExitInstance, CreateAtmosphere, DeleteAtmosphere
void WbCbExit (int module, CELBODY *m);
ATMOSPHERE *WbAtmCreate (int module, CELBODY2 *cb);
void WbAtmDelete (int module, ATMOSPHERE *a);
void *WbLegacyProc (int module, const char *name);          // a 2006 interface function <body>_... of the module; null if none
void WbCelbodyFreed (int module);                           // the module is freed

// Launchpad items, custom commands, camera control and modes, help (Extra.cpp)
bool WbServeExtra (uint32_t fn, WbReader &r, WbWriter &w); // false: not one of these
void WbExtraFreed (int module);                            // the module is freed: its items, commands, camera control go
void WbExtraStopped ();                                    // ... the helper stopped: all of them

// meshes a module makes, HUD meshes, VESSEL3 2D panels, CreateAirfoil2 and 4, EditAirfoil, GetAirfoilParam, nav data (Panel.cpp)
bool WbServePanel (uint32_t fn, WbReader &r, WbWriter &w); // false: not one of these

// a Windows module's lights (Light.cpp)
bool WbServeLight (uint32_t fn, WbReader &r, WbWriter &w); // false: not one of these

// a vessel's beacons, exhausts, animations, 2006 components, scenario helpers; a module's watched objects (Vdata.cpp)
bool WbServeVdata (uint32_t fn, WbReader &r, WbWriter &w); // false: not one of these
bool WbServeWatched (uint32_t fn, WbReader &r);

// the oapi functions a module asks Orbiter's data of (Oapi.cpp); oapiLoadMeshGlobal with a module's callback (Manual.cpp)
bool WbServeOapi (uint32_t fn, WbReader &r, WbWriter &w); // false: not one of these
MESHHANDLE WbLoadMeshGlobal (const char *name, uint32_t fn);

bool WbServeManual (uint32_t fn, WbReader &r, WbWriter &w); // false: not one of these

// Linux's Lua states for the helper's modules (Lua.cpp)
bool WbServeLua (uint32_t fn, WbReader &r, WbWriter &w); // false: not L_LUA
uint32_t WbLuaStateId (lua_State *L);                     // a state the helper gets (VMSG_LUAINTERPRETER, VMSG_LUAINSTANCE)
void WbLuaRaise (lua_State *L);                           // the error of the module's call just made, raised here (doesn't return)
int WbLuaNativeGeneric (VESSEL3 *v, int msg, int prm, uint32_t sid, bool *err); // a native vessel's, with Linux's state a module passed
void WbLuaFlush ();                                       // the blocks of the modules' userdata Lua collected, freed in the helper
bool WbServeLuaRemote (uint32_t fn, WbReader &r, WbWriter &w); // LuaRemote.cpp: L_LUACALL, L_LUAHOOK (false: not one)
int WbLuaHelperGeneric (VESSEL3 *v, int msg, int prm, uint32_t sid, bool *err); // a native vessel's, with a helper state
void *WbLuaRemoteBarrier ();                               // a served call: a Lua error in it doesn't unwind past it ...
void WbLuaRemoteRestore (void *t);                         // ... then the outer calls'; the outermost's end frees the copies
void WbLuaRemoteFreed (WbReader &r);                       // H_LUAFREE's reply: the native blocks the helper's Lua collected

// vessel hooks (Hooks.cpp)
uint32_t WbHookClassOf (const void *hVessel, VESSEL *own); // L_VesselInfo: the vessel's class for the helper (0: none)
bool WbHookProxy (const void *hVessel);  // the helper has its object for that Linux vessel (H_PROXY once)
void WbHooksApply (WbReader &r);         // H_HOOKS's reply
void WbHooksStopped ();                  // the helper stopped: what it had is gone
const char *WbClbkName (int c);          // Bridge.cpp: a callback's name (WbClbk)

// OrbiterSound for the Windows modules on XRSound (Sound.cpp)
bool WbServeSound (uint32_t fn, WbReader &r, WbWriter &w); // false: not a sound call
bool WbSoundPresent ();                       // the XRSound plugin is loaded
void WbSoundVesselGone (const void *hVessel); // a bridged vessel's sound IDs go with it
void WbSoundClear ();                         // the bridge stops: every sound ID and MFD ID
void WbXrsVesselGone (const void *hVessel);   // any vessel: XRSound will destroy its engine
void WbXrsSessionEnd ();                      // XRSound will destroy every engine

void WbApplySync (const uint8_t *p, uint32_t n);            // the live memory records after a helper message
uint32_t WbLiveBytes (WbReader &r, void *dst, uint32_t n);   // a live item of n bytes for a proxy's member (its id; 0: bad)
void WbLiveRelease (uint32_t id);                            // ... its proxy is gone
const void *WbLiveRef (WbReader &r, int kind);              // a live item of a double or VECTOR3: where its value is (null: bad)
uint32_t WbLiveIdOf (const void *p);                         // the item whose value Orbiter reads at p (0: none)
BEACONLIGHTSPEC *WbLiveBeacon (uint32_t id);                 // ... a beacon item's spec
void WbPutVS (WbWriter &w, const VESSELSTATUS &s);           // a VESSELSTATUS on the wire, handles as ids
void WbGetVS (WbReader &r, VESSELSTATUS &s);
bool WbMeshCopies ();                                        // the helper holds mesh copies: H_FRAME once a frame
void WbManualClear ();                                       // the session's end

// VESSELSTATUS2 on the wire (vs2 in wb_proto.h); lists read in are allocated with new[], as Orbiter's own
void WbPutStatus (WbWriter &w, const void *vs2);
void WbGetStatus (WbReader &r, void *vs2); // a list that differs replaces the old one, as Orbiter's parser does
void WbFreeStatus (void *vs2);
