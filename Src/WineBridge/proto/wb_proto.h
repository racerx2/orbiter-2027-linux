// WineBridge: the channel between Orbiter (Linux) and the helper (Orbiter.exe under Wine).
// Built by GCC for Linux and by clang for i686-windows; plain C++ with no library types.
#pragma once
#include <stdint.h>
#include <string.h>

#define WB_MAGIC 0x31425757u
#define WB_VERSION 21u
#define WB_SIZE (64u << 20)  // a panel bitmap of 2048 x 1024 is 8 MiB; tmpfs pages are used only when written
#define WB_SPIN_NS 50000

enum { WB_LINUX = 0, WB_HELPER = 1 };
enum { WB_CALL = 1, WB_RET = 2 };

struct WbHeader {
	uint32_t magic, version;
	int32_t turn;        // whose message is in the buffer: WB_LINUX or WB_HELPER may act
	int32_t sleeping[2]; // that side waits in its FIFO
	uint32_t kind, fn, len;
	uint32_t slen;       // the helper's live memory that changed, right after the message (WbSync records)
	uint32_t hooks;      // the helper's count of vessel vtables it saw change (hooks); Linux asks H_HOOKS when it moves
	uint32_t lfree;      // the helper's count of native userdata blocks its Lua collected; Linux asks H_LUAFREE when it moves
	uint32_t pad[5];
};
struct WbSync { uint32_t id, len; }; // then len bytes: one live item's new value
// live items' wire values: a double; a VECTOR3; a beacon (u32 shape, pos, col, f64 size falloff period duration tofs, u8 active); string bytes;
// bytes of a member both sides lay out alike
enum { WB_LIVE_DOUBLE = 1, WB_LIVE_VECTOR3, WB_LIVE_BEACON, WB_LIVE_STRING, WB_LIVE_BYTES };
#define WB_MSG_MAX (WB_SIZE - (uint32_t)sizeof (WbHeader))

static inline uint8_t *WbMsg (WbHeader *h) { return (uint8_t *)(h + 1); }
static inline int32_t WbLoad (int32_t *p) { return __atomic_load_n (p, __ATOMIC_SEQ_CST); }
static inline void WbStore (int32_t *p, int32_t v) { __atomic_store_n (p, v, __ATOMIC_SEQ_CST); }

// calls the Linux side makes; the helper serves them
enum WbHelperCall {
	H_HELLO = 1, // u32 version, u32 generated list -> i32 ok
	H_LOAD,      // str dll path, b XRSound there (the helper loads its OrbiterSound.dll and XRSound.dll first) -> i32 module (0: failed), str error,
	             // u32 exports (WbExport bits), i32 GetModuleVersion, str ModuleDate, str ModelName, str ModelDesc
	H_OVCINIT,   // i32 module, u32 vessel, i32 flightmodel -> i32 ok, i32 version, u32 overridden[2]
	H_OVCEXIT,   // i32 module, u32 vessel
	H_CLBK,      // u32 vessel, u32 callback, arguments -> results
	H_AIRFOIL,   // u32 function, u32 context, u32 vessel, i32 WbAirfoilKind, then f64 aoa, M, Re -> f64 cl, cm, cd; for the force and
	             // moment kind v3 airflow, f64 aoa, beta, gamma, M, Re -> f64 CA, CN, CY, Cl, Cm, Cn
	H_QUIT,
	H_LIFT,      // u32 function, f64 aoa -> f64 cl (SetLiftCoeffFunc)
	H_INPUTBOX,  // u32 function, u32 id, str text, u32 usrdata -> b ok (oapiOpenInputBox)
	H_FRAME,     // once a frame: the meshes the modules changed go back to Orbiter
	H_FREE,      // i32 module: its last open is freed (ExitModule, FreeLibrary)
	H_OPC,       // i32 module, i32 WbExport of an opc callback, its arguments (handles as ids; no window)
	H_MFDOPEN,   // u32 mode, u32 mfd, i32 w, i32 h, u32 vessel, u32 link -> u32 object (0: none), b MFD2
	H_MFDCLOSED, // u32 mode, u32 mfd: OAPI_MSG_MFD_CLOSED
	H_MFD,       // u32 object, i32 WbMfdCall, u32 cw, u32 ch, arguments -> b object there, results
	H_MODULE,    // u32 object, i32 WbModuleCall, arguments -> results
	H_EMFD,      // u32 object, i32 WbEmfdCall, the members, arguments -> b object there, the members
	H_HOOKS,     // -> u32 n, n x (u32 vessel, u32 mask[2]): bridged vessels whose class changed (overridden or hooked callbacks);
	             //    u32 m, m x (u32 class, u32 hooked[2]): Linux vessel classes whose hooked callbacks changed
	H_PROXY,     // u32 vessel -> b: the helper has its object for that Linux vessel (made now if not)
	H_VESSELGONE, // u32 vessel, u32 class (its last vessel: the class's vtable goes back; 0: not)
	H_CELBODY,    // i32 WbCbCall, its arguments -> its results: celestial body and atmosphere modules
	H_LPAD,       // u32 Launchpad item, i32 WbLpadCall -> its result (b: the item is there, then the result)
	H_CUSTOMCMD,  // u32 function, u32 context: a custom command
	H_ECC,        // u32 external camera control -> b polled, 6 f64 (x y z yaw pitch roll)
	H_ECCGONE,    // u32 external camera control: the camera that held it is destroyed
	H_MESHLOADED, // u32 function, u32 mesh, b first load: oapiLoadMeshGlobal's callback
	H_LUACALL,    // u32 function, u32 Lua state: a module's C function Lua calls on Linux's state -> i32 WbLuaEnd, i32 results (or yield's)
	H_LUAHOOK,    // u32 function, u32 Lua state, i32 event, currentline, i_ci: a module's hook -> i32 WbLuaEnd, i32 0
	H_LUAFREE,    // u32 n, n x u32: the blocks of a module's userdata whose Linux userdata Lua collected -> u32 m, m x u32: the blocks of
	              // a native module's userdata whose helper userdata the helper's Lua collected
	H_PUMP,       // the helper's window messages are dispatched (its dialogs)
	H_DIALOGS,    // i32 WbDlgWhat, u32 msg, u32 data (a vessel as its id): Orbiter's dialog manager
	H_LUA         // u32 helper Lua state, i32 WbLuaOp, its arguments -> its results: a native module's call on a helper state, as L_LUA's
};
enum WbDlgWhat { WD_DELETED, WD_MADE, WD_CLEARED, WD_MESSAGE }; // H_DIALOGS: dialogs may open while made; cleared and deleted close them

// L_LUA: lua.dll's function on a Linux state, as Lua's of that name (helper/luaremote.cpp, linux/Lua.cpp); the ones Lua may raise in
// reply b error first (Linux keeps the error and raises it again from the module function's closure)
enum WbLuaOp {
	LO_gettop, LO_settop, LO_pushvalue, LO_remove, LO_insert, LO_replace, LO_checkstack, LO_xmove,
	LO_isnumber, LO_isstring, LO_iscfunction, LO_isuserdata, LO_type, LO_equal, LO_rawequal, LO_lessthan,
	LO_tonumber, LO_toboolean, LO_tolstring, LO_objlen, LO_tocfunction, LO_touserdata, LO_tothread, LO_topointer,
	LO_pushnil, LO_pushnumber, LO_pushinteger, LO_pushlstring, LO_pushcclosure, LO_pushboolean, LO_pushlightuserdata, LO_pushthread,
	LO_gettable, LO_getfield, LO_rawget, LO_rawgeti, LO_createtable, LO_newuserdata, LO_getmetatable, LO_getfenv,
	LO_settable, LO_setfield, LO_rawset, LO_rawseti, LO_setmetatable, LO_setfenv,
	LO_call, LO_pcall, LO_cpcall, LO_load, LO_dump, LO_resume, LO_status, LO_gc, LO_error, LO_next, LO_concat, LO_newthread,
	LO_getstack, LO_getinfo, LO_getlocal, LO_setlocal, LO_getupvalue, LO_setupvalue, LO_sethook, LO_gethook, LO_gethookmask,
	LO_gethookcount, LO_atpanic,
	LO_callc,   // u32 Linux function: called by the module directly (a function lua_tocfunction gave) -> b error, i32 results
	LO_callhook // u32 Linux hook, i32 event, currentline, i_ci: called by the module directly -> b error
};
enum WbLuaEnd { LE_OK, LE_ERROR, LE_YIELD, LE_UNKNOWN, LE_GONE }; // returned, raised, yielded, no such function, no helper (Linux's)
enum WbLuaFn { LF_NONE, LF_MODULE, LF_LINUX }; // a C function (or hook) on the wire: none, a module's (its address), Linux's (an id)
enum WbLuaUd { LU_NONE, LU_LIGHT, LU_BLOCK, LU_VESSEL, LU_ID, LU_BYTES }; // lua_touserdata: a light one's value, the module's block,
	// a vessel's (u32 vessel, u32 userdata), a handle's (u32 id, u32 userdata), a copy (u32 userdata, str bytes)
// H_LUA is L_LUA the other way: the helper's state, LF_MODULE the helper's function, LF_LINUX a native module's (an id); a lightuserdata
// is u32 value, b Linux's (an id of the bridge's: lua_pushlightuserdata u32, b the same), LU_BLOCK a native module's block (an id)

// the module entry points Orbiter asks for by name (ModuleProc), in the order of WbExportName
enum WbExport {
	E_GetModuleVersion, E_ModuleDate, E_ovcInit, E_ovcExit,
	E_opcOpenRenderViewport, E_opcCloseRenderViewport, E_opcPreStep, E_opcPostStep, E_opcFocusChanged, E_opcTimeAccChanged,
	E_opcDeleteVessel, E_opcPause, E_opcSaveState, E_opcLoadState,
	E_InitInstance, E_ExitInstance, E_CreateAtmosphere, E_DeleteAtmosphere, E_ModelName, E_ModelDesc,
	E_COUNT
};
static const char *const WbExportName[E_COUNT] = {
	"GetModuleVersion", "ModuleDate", "ovcInit", "ovcExit",
	"opcOpenRenderViewport", "opcCloseRenderViewport", "opcPreStep", "opcPostStep", "opcFocusChanged", "opcTimeAccChanged",
	"opcDeleteVessel", "opcPause", "opcSaveState", "opcLoadState",
	"InitInstance", "ExitInstance", "CreateAtmosphere", "DeleteAtmosphere", "ModelName", "ModelDesc"};
// a VESSELSTATUS2 on the wire ("vs2"): u32 version, then for version 2: u32 flag, rbody, base, i32 port, status, v3 rpos rvel vrot arot,
// f64 surf_lng surf_lat surf_hdg, u32 xpdr, and the fuel, thruster and dock lists the flags name (u32 count, then the entries)

// calls the helper makes, written by hand; the generated ones (gen_calls.h) start at 1000
enum WbLinuxCall {
	L_LOG = 100,
	L_CreateAirfoil3, L_AddAnimationComponent, L_CreateThrusterGroup, L_AddExhaustStream2,
	L_BaseSaveState, L_BaseLoadStateEx, L_BaseSetStateEx, L_BaseLoadGenericCockpit, L_BaseGetRadiationForce,
	// strings Orbiter keeps
	L_GetName, L_GetClassName, L_GetAttachmentId, L_MFDButtonLabel,
	// other vessels
	L_VesselInfo, // u32 vessel -> b is one, i32 flight model, i32 interface version, u32 class (0: none)
	L_ProxyClbk,
	// vessel states and scenarios
	L_GetStatus, L_DefSetState, L_CreateVessel, L_GetStatusEx, L_DefSetStateEx, L_CreateVesselEx,
	L_ParseScenarioLineEx, L_ReadScenarioNextline,
	// memory Orbiter reads every frame (live items)
	L_AddBeacon, L_AddExhaustSpec, L_AddParticleStream, L_CreateVariableDragElement, L_AddReentryStream, L_AddExhaustStream1, L_DebugString,
	L_DelExhaust, L_DelExhaustStream,
	// functions Orbiter calls back
	L_CreateAirfoil, L_SetLiftCoeffFunc, L_OpenInputBox,
	// lights
	L_AddSpotLight, L_AddPointLight,
	// meshes
	L_MeshGroup, L_MeshGroupStore, L_MeshMaterial, L_MeshMaterialStore, L_EditMeshGroup, L_LoadMeshGlobal, L_LoadMesh,
	// drawing
	L_CreateFont, L_CreatePen, L_CreateBrush, L_ReleaseFont, L_ReleasePen, L_ReleaseBrush,
	L_GetSketchpad, L_ReleaseSketchpad, L_Skp, L_GetDC, L_ReleaseDC, L_CreateSurfaceBmp, L_RegisterPanelBackground,
	L_BaseDrawHUD2, L_BaseDrawHUD3, L_BaseRenderHUD,
	L_OsSoundID, L_OsLoad, L_OsSet3d, L_OsPlay, L_OsPlayRadio, L_OsStop, L_OsIsPlaying, L_OsIsPlayingRadio, L_OsOption, // OrbiterSound.dll
	L_OsSetFrequency, L_OsReplace, L_OsMfdID, L_OsMfdLoad, L_OsMfdPlay, L_OsMfdStop,
	L_WriteLog, L_WriteLogError, // a module's log line, formatted in the helper
	L_PlanetAtmConstants,        // u32 planet -> b has one, then the ATMCONST
	// MFD modes, MFDs, module objects, external MFDs (helper/mfd.cpp, module.cpp; linux/Mfd.cpp)
	L_RegisterMFDMode,    // u32 mode, str name, u32 key, u32 vessel (0: every vessel) -> i32 id, u32 key as Orbiter set it
	L_UnregisterMFDMode,  // i32 id, u32 vessel -> b
	L_MFDModeSpec,        // str name -> i32 id, then for one: str name, u32 key, b has msgproc, u32 mode (0: not the bridge's)
	L_Mfd,                // u32 link, i32 WbMfdBack, arguments -> results
	L_RegisterModule,     // u32 object, i32 module (0: none), u32 overridden (WbModuleCall bits), b 2010 layout
	L_ModuleGone,         // u32 object
	L_ModuleDefault,      // i32 module, i32 WbModuleCall, its arguments: Orbiter's default callback (the module's opc function)
	L_Emfd,               // i32 WbEmfdBack, u32 proxy, the members, arguments -> the members, results (Y_New: u32 object, MFDSPEC
	                      // -> u32 proxy, the members; Y_Gone, Y_Unregister: u32 proxy -> b)
	L_BroadcastMFDMessage, // i32 mode, i32 msg, u32 data -> i32
	L_RegisterPanelMFDGeometry, // u32 vessel, u32 panel, i32 mfd, i32 nmesh, i32 ngroup -> i32
	L_Celbody,            // i32 WbCbBack, its arguments -> its results
	// Launchpad items, custom commands, camera control and modes, help
	L_LpadRegister,       // u32 item, i32 module, u32 parent -> u32 handle, b the item's hItem set (it was added)
	L_LpadUnregister,     // u32 item -> b
	L_CustomCmd,          // str label, str description, u32 function, u32 context, i32 module -> u32 id
	L_CustomCmdGone,      // u32 id -> b
	L_EccRegister,        // u32 object (0: none), i32 module, live items (u32 id, u32 n, bytes) of datamode, cameramode, vcmode,
	                      // trkmode -> b
	L_EccUnregister,      // -> b: whatever control Orbiter's camera holds, as Orbiter's
	L_SetCameraMode,      // i32 mode, u32 target, f64 fov, the mode's fields (below) -> b
	L_OpenHelp,           // b Launchpad's, str helpfile, topic, toc, index -> b
	// meshes a module makes, HUD meshes, 2D panels, airfoils, nav data; a group: u32 MtrlIdx, TexIdx, zBias, nVtx, nIdx, the arrays
	L_CreateMesh,         // u32 n, n groups -> u32 mesh
	L_AddMeshGroup,       // u32 mesh, a group -> u32 index
	L_AddMeshGroupBlock,  // u32 mesh, u32 group, u32 nVtx, u32 nIdx, the arrays -> b
	L_DeleteMesh,         // u32 mesh
	L_MeshTexCount,       // u32 mesh -> u32 the entries oapiRenderHUD reads from its texture array
	L_RenderHUD,          // u32 mesh, b has an array, u32 n, n x u32 surface
	L_SetPanelBackground, // u32 vessel, u32 panel, u32 n, n x u32 surface, u32 mesh, u32 width, height, baseline, scrollflag -> i32
	L_SetPanelScaling,    // u32 vessel, u32 panel, f64 default, f64 extended -> i32
	L_RegisterPanelArea,  // u32 vessel, u32 panel, i32 id, i32 left top right bottom, i32 WbPanelArea, its fields (below) -> i32
	L_CreateAirfoil2,     // u32 vessel, i32 align, v3 ref, u32 function, f64 c, S, A -> u32 airfoil
	L_CreateAirfoil4,     // u32 vessel, v3 ref, u32 function, u32 context, f64 c, S, A -> u32 airfoil
	L_EditAirfoil,        // u32 vessel, u32 airfoil, u32 flag, v3 ref, u32 function, f64 c, S, A
	L_GetAirfoilParam,    // u32 vessel, u32 airfoil -> b, v3 ref, u32 function, u32 context (0, 0: not the bridge's), f64 c, S, A
	L_GetNavData,         // u32 nav -> u32 type, ch, f64 power, str descr, u32 handle, f64 a, b (vor: planet lng lat; vtol: base
	                      // npad; ils: base appdir; ids: vessel, u32 dock; xpdr: vessel)
	// lights: u32 vessel, u32 Orbiter's light first
	L_LightState,         // b active, u32 attached to, i32 visibility (-1: none), 3 colours, v3 own position, own direction, f64 own intensity,
	                      // position, direction, intensity each i32 0 own, 1 as now, 2 a live item; b point: f64 range, att[3]; b spot: umbra, penumbra
	L_GetLight,           // (u32 vessel, u32 index) -> u32 light, i32 type, visibility, u32 attached to, b active, 3 colours, v3 position,
	                      // direction, f64 intensity, range, att[3], umbra, penumbra
	L_DelLight,           // -> b
	L_ClearLights,        // (u32 vessel)
	// a vessel's data: u32 vessel first
	L_GetBeacon,          // u32 index -> u32 item (0: not the bridge's), b has, u32 shape, v3 pos, col, f64 size, falloff, period, duration, tofs, b active
	L_DelBeacon,          // u32 item -> b
	L_ClearBeacons,
	L_GetExhaustValues,   // u32 index -> b, f64 lscale, wscale, v3 pos, dir, u32 tex
	L_GetExhaustSpec,     // u32 index -> b, u32 th, f64 lsize, wsize, lofs, modulate, u32 tex, flags, id, then level, lpos, ldir each u32 item, b set, value
	L_GetMeshName,        // u32 index -> b, str
	L_GetSupervessel,     // -> u32
	L_SendBufferedKey,    // u32 key, b down, b has, 256 bytes -> i32
	L_ParseScenarioLine,  // str line, VESSELSTATUS -> VESSELSTATUS
	L_AddAnimComp,        // u32 sequence, u32 component, u32 n, n bytes: b list, u32 ngrp, ngrp x u32, f64 state0, state1, MESHGROUP_TRANSFORM -> b
	L_Watched,            // i32 kind (0 2006 component, 1 transform), u32 vessel, u32 id, u32 n, n bytes as it was added
	L_GetAnims,           // -> u32 n, u32 components; each animation: f64 defstate, state, u32 m; each component: f64 state0, state1, u32 id,
	                      // i32 parent (index), b transform (its fields as L_AddAnimationComponent's)
	L_GetAnimStates,      // -> u32 n, n x (f64 defstate, f64 state)
	L_SetAnimStates,      // u32 m, m x (u32 index, f64 state)
	// Orbiter's data a module asks for
	L_GetAtm,             // u32 vessel -> f64 T, p, rho, u32 the atmosphere's body
	L_GetWindVector,      // u32 planet, f64 lng, lat, alt, i32 frame -> v3
	L_GetCmdLine,         // -> b, str
	L_GetObjectFileName,  // u32 object -> b, str
	L_GetMeshFilename,    // u32 mesh -> b, str
	L_GetObjectParam,     // u32 object, u32 type -> u32 n (0: none), n bytes
	L_ElevationManager,   // u32 planet -> u32
	L_GetHUDMode,         // -> i32 mode, u32 reference body, u32 nav index
	L_SetHUDMode,         // i32 mode, b parameters, u32 reference body, u32 nav index -> b
	L_SetPanelBlink,      // b on, (on) 4 v3
	L_SimulateImmediateKey, // 256 bytes, b running only -> b
	L_ObjectVisuals,      // u32 n, n x u32 body -> u32 n, n x u32 visual
	L_EditMeshGroupT,     // as L_EditMeshGroup, for a template mesh
	L_GetMeshGroup,       // u32 device mesh, u32 group, u32 nVtx, b Vtx, b VtxPerm (perm), u32 nIdx, b Idx, b IdxPerm (perm)
	                      // -> i32, u32 nVtx, nIdx, MtrlIdx, TexIdx, b vertices, b indices
	L_MeshMaterialEx,     // u32 device mesh, u32 material, i32 property, b set (b value, FVECTOR4) -> i32 (get: FVECTOR4)
	L_LoadMeshGlobalClbk, // str name, u32 function -> u32 mesh
	L_OpenInputBoxEx,     // str title, u32 enter, u32 cancel, str buffer, i32 vislen, u32 usrdata, u32 flags
	L_CreateFontEx,       // i32 height, str face, i32 width, weight, style, f64 spacing -> u32 font
	L_LoadInterpreterLib, // -> i32 the helper's module number of LuaInline (Orbiter loads it as a plugin), 0: not loaded
	L_LUA,                // u32 Lua state, i32 WbLuaOp, its arguments -> its results
	L_XrsEngine,          // b vessel, then u32 vessel or str module name -> u32 engine (0: none): XRSound.dll's two functions
	L_Xrs,                // u32 engine, i32 WbXrsCall, its arguments -> its result
	L_LUACALL,            // u32 Linux function, u32 helper Lua state: a native module's C function on a helper state -> i32 WbLuaEnd, i32 results
	L_LUAHOOK,            // u32 Linux hook, u32 helper Lua state, i32 event, currentline, i_ci -> i32 WbLuaEnd, i32 0
	L_END
};

// XRSoundEngine30's methods in vtable order (L_Xrs). Arguments: i32 sound ID, then LoadWav str file, i32 type; PlayWav b loop, f64 volume;
// SetPaused, SetDefaultSoundEnabled b; SetDefaultSoundGroupFolder str; SetPan, SetPlaybackSpeed f64; SetPlayPosition u32.
// Results: b; GetVersion, GetPan, GetPlaybackSpeed f64; GetDefaultSoundGroupFolder, GetLogID b, str; GetEngineType, GetPlayPosition i32
enum WbXrsCall {
	XR_GetVersion, XR_LoadWav, XR_PlayWav, XR_StopWav, XR_IsWavPlaying, XR_SetPaused, XR_IsPaused,
	XR_SetDefaultSoundEnabled, XR_GetDefaultSoundEnabled, XR_SetDefaultSoundGroupFolder, XR_GetDefaultSoundGroupFolder,
	XR_IsDefaultSound, XR_IsDefaultSoundGroup, XR_IsDefaultSoundOrGroup,
	XR_GetEngineType, XR_GetLogID,
	XR_SetPan, XR_GetPan, XR_SetPlaybackSpeed, XR_GetPlaybackSpeed, XR_SetPlayPosition, XR_GetPlayPosition
};

// the Sketchpad methods (L_Skp: u32 sketchpad, i32 method, its arguments); a text is i32 length, str in UTF-8 (the whole string for
// a length of 0 or less)
enum WbSkpCall {
	S_SetFont, S_SetPen, S_SetBrush, S_SetTextAlign, S_SetTextColor, S_SetBackgroundColor, S_SetBackgroundMode,
	S_GetCharSize, S_GetTextWidth, S_SetOrigin, S_Text, S_TextBox, S_Pixel, S_MoveTo, S_LineTo, S_Line, S_Rectangle,
	S_Ellipse, S_Polygon, S_Polyline, S_PolyPolygon, S_PolyPolyline,
	S_GetOrigin // 2016
};

// an MFD object's virtual methods (H_MFD); the reply starts with b: the object is there
enum WbMfdCall {
	M_Update,    // u32 surface, u32 sketchpad -> b
	M_UpdateDC,  // the image (i32 w, h, rows) -> its changed part (b, then i32 x, y, w, h, rows)
	M_KeyBuffered, M_KeyImmediate, M_Button, M_Label, M_Menu, M_WriteStatus, M_ReadStatus, M_StoreStatus, M_RecallStatus,
	M_Delete
};
// Orbiter's side of an MFD object (L_Mfd)
enum WbMfdBack { B_InvalidateDisplay, B_InvalidateButtons, B_Title, B_Pen, B_Font, B_Colour };

// oapi::Module's callbacks (H_MODULE), in vtable order after the destructor; the last three came with 2016
enum WbModuleCall {
	O_SimulationStart, O_SimulationEnd, O_PreStep, O_PostStep, O_TimeJump, O_FocusChanged, O_TimeAccChanged, O_NewVessel,
	O_DeleteVessel, O_VesselJump, O_Pause, O_ProcessMouse, O_ProcessKeyboardImmediate, O_ProcessKeyboardBuffered,
	O_COUNT
};
#define WB_MODULE_2016 O_ProcessMouse

// an ExternMFD's virtual methods (H_EMFD) and Orbiter's object's methods (L_Emfd)
enum WbEmfdCall { X_SetVessel, X_Update, X_RefreshDisplay, X_RefreshButtons, X_FocusChanged, X_Delete };
enum WbEmfdBack {
	Y_New, Y_Gone, Y_Register, Y_Unregister, Y_Id, Y_Active, Y_GetVessel, Y_DisplaySurface, Y_ButtonLabel, Y_ProcessButton,
	Y_SendKey, Y_Resize, Y_SetMode, Y_OpenModeHelp,
	Y_SetVessel, Y_Update, Y_RefreshDisplay, Y_RefreshButtons, Y_FocusChanged // ExternMFD's own methods (the defaults)
};
// ExternMFD's members a module reads, the same on both sides, go with every call between them: u32 hVessel, i32 DW, DH, pmode,
// nbt1, nbt2, bty0, btdy, btpressed, b instr (Orbiter's instrument is there); an MFDSPEC: i32 left, top, right, bottom,
// nbt_left, nbt_right, bt_yofs, bt_ydist

// celestial body and atmosphere modules: the objects are the helper's, Orbiter holds proxies of them (H_CELBODY)
enum WbCbCall {
	K_Init,       // i32 module, u32 body, u32 proxy -> u32 object (0: none), i32 version, u32 overridden (WbCbVirt bits)
	K_Exit,       // i32 module, u32 object: its ExitInstance, else freed as Orbiter's delete did
	K_Clbk,       // u32 object, i32 WbCbVirt, its arguments -> its results
	K_AtmCreate,  // i32 module, u32 the body's object (0: a native body), u32 body -> u32 atmosphere (0: none), u32 overridden (WbAtmVirt bits)
	K_AtmDelete,  // i32 module (0: Orbiter's delete), u32 atmosphere: its DeleteAtmosphere, else freed as Orbiter's delete did
	K_Atm,        // u32 atmosphere, i32 WbAtmVirt, its arguments -> its results
	K_LegacyProc, // i32 module, str name -> u32 function (0: none): the 2006 interface's <body>_... functions
	K_Legacy      // i32 WbLegacy, u32 function, its arguments -> its results
};
// CELBODY's and CELBODY2's virtual methods: bEphemeris -> b; clbkInit: u32 cfg; clbkEphemeris, clbkFastEphemeris: f64 t, i32 req,
// 12 f64 -> i32 flags, 12 f64; clbkAtmParam: f64 alt, ATMPARAM -> b, ATMPARAM; LegacyAtmosphereInterface -> b
enum WbCbVirt { V_bEphemeris, V_Init, V_Ephemeris, V_FastEphemeris, V_AtmParam, V_Legacy, V_COUNT };
// ATMOSPHERE's: clbkName -> str; clbkConstants: ATMCONST -> b, ATMCONST; clbkParams: PRM_IN (6 f64, u32 flag), PRM_OUT -> b, PRM_OUT
enum WbAtmVirt { A_Name, A_Constants, A_Params, A_COUNT };
// the 2006 interface: SetPrecision: f64; Ephemeris, FastEphemeris: f64 t, 12 f64 -> i32 result, i32 format, 12 f64;
// AtmPrm: f64 alt, ATMPARAM -> ATMPARAM
enum WbLegacy { G_SetPrecision, G_Ephemeris, G_FastEphemeris, G_AtmPrm, G_COUNT };
// Orbiter's side (L_Celbody). An atmosphere Orbiter holds ("atm"): u32 the helper's object (a bridged one), u32 a native one's id,
// i32 the helper's number of its module (a Windows one), u32 a native module's id. One the helper gives ("atmref"): u32 its object,
// u32 overridden, u32 a native one's id
enum WbCbBack {
	Q_Init,          // u32 proxy, u32 cfg: CELBODY2's clbkInit on the proxy -> b, atm
	Q_SetAtm,        // u32 proxy, atmref -> b, atm
	Q_FreeAtm,       // u32 proxy -> b, atm
	Q_LoadAtm,       // u32 proxy, str name -> b, atm
	Q_FreeAtmModule, // u32 proxy -> b, atm
	Q_SyncAtm,       // u32 proxy, atmref: the module set its member itself
	Q_CbInfo,        // u32 body -> i32 version (0: no module), u32 its object (0: not a bridged one), atm
	Q_CbClbk,        // u32 body, i32 WbCbVirt, its arguments -> its results: a native body's object
	Q_LinuxAtm       // u32 native atmosphere, i32 WbAtmVirt, its arguments -> its results
};

// an airfoil's function: CreateAirfoil's and CreateAirfoil2's, CreateAirfoil3's (with the vessel and a context), CreateAirfoil4's
enum WbAirfoilKind { A_Old, A_Ex, A_Ex2 };
// RegisterPanelArea's forms: VESSEL3's texture rectangle (i32 rect[4], draw, mouse, bkmode), VESSEL3's surface and context (draw,
// mouse, u32 surface, u32 context), VESSEL4's texture index (i32 texidx, rect[4], draw, mouse, bkmode)
enum WbPanelArea { PA_TexRect, PA_Surface, PA_TexIdx };

// a Launchpad item's virtual methods (H_LPAD): Name, Description -> str; Open -> b; WriteConfig -> i32
enum WbLpadCall { P_Name, P_Description, P_Open, P_WriteConfig };
// a camera mode's fields after its mode (L_SetCameraMode): cockpit: i32 cockpit mode, i32 position, i32 leaning, b smooth; track:
// i32 track mode, f64 distance, phi, theta, u32 reference; ground: u32 reference, f64 longitude, latitude, altitude, phi, theta,
// b altitude above ground, b target lock

// the VESSEL2, VESSEL3 and VESSEL4 callbacks, in declaration order
enum WbClbk {
	C_SetClassCaps, C_SaveState, C_LoadStateEx, C_SetStateEx, C_PostCreation, C_FocusChanged, C_PreStep, C_PostStep,
	C_PlaybackEvent, C_VisualCreated, C_VisualDestroyed, C_DrawHUD2, C_RCSMode, C_ADCtrlMode, C_HUDMode, C_MFDMode,
	C_NavMode, C_DockEvent, C_Animate, C_ConsumeDirectKey, C_ConsumeBufferedKey, C_LoadGenericCockpit, C_LoadPanel,
	C_PanelMouseEvent2, C_PanelRedrawEvent2, C_LoadVC, C_VCMouseEvent, C_VCRedrawEvent,
	C_PanelMouseEvent3, C_PanelRedrawEvent3, C_Generic, C_LoadPanel2D, C_DrawHUD3, C_RenderHUD, C_GetRadiationForce,
	C_NavProcess, // VESSEL4
	C_COUNT
};
#define WB_V3_FIRST C_PanelMouseEvent3
#define WB_V4_FIRST C_NavProcess
// C_Generic: i32 message, i32 prm, u32 context (VMSG_LUAINTERPRETER, VMSG_LUAINSTANCE: a Linux Lua state) -> i32, b a Lua error was
// raised; a module's call of a Linux vessel's has b the context is a Linux Lua state after the context

static inline bool WbOverridden (const uint32_t *mask, int c) { return (mask[c >> 5] >> (c & 31)) & 1u; }

struct WbWriter {
	uint8_t *p, *end;
	bool over;
	WbWriter (uint8_t *b, uint32_t n) : p (b), end (b + n), over (false) {}
	void raw (const void *d, uint32_t n) { if ((uint32_t)(end - p) < n) { over = true; return; } memcpy (p, d, n); p += n; }
	void u32 (uint32_t v) { raw (&v, 4); }
	void i32 (int32_t v) { raw (&v, 4); }
	void f64 (double v) { raw (&v, 8); }
	void b (bool v) { uint8_t c = v ? 1 : 0; raw (&c, 1); }
	void v3 (const double *v) { raw (v, 24); }
	void str (const char *s) { uint32_t n = s ? (uint32_t)strlen (s) : 0xffffffffu; u32 (n); if (s) raw (s, n); }
};

struct WbReader {
	const uint8_t *p, *end;
	bool bad;
	WbReader () : p (0), end (0), bad (true) {}
	WbReader (const uint8_t *b, uint32_t n) : p (b), end (b + n), bad (false) {}
	void raw (void *d, uint32_t n) { if (bad || (uint32_t)(end - p) < n) { bad = true; memset (d, 0, n); return; } memcpy (d, p, n); p += n; }
	uint32_t u32 () { uint32_t v; raw (&v, 4); return v; }
	int32_t i32 () { int32_t v; raw (&v, 4); return v; }
	double f64 () { double v; raw (&v, 8); return v; }
	bool b () { uint8_t c; raw (&c, 1); return c != 0; }
	void v3 (double *v) { raw (v, 24); }
	// a string: its length (0xffffffff for none) and its bytes, which stay in the buffer until the next message
	const char *str (uint32_t *len) {
		uint32_t n = u32 ();
		*len = n;
		if (n == 0xffffffffu || bad) return 0;
		if ((uint32_t)(end - p) < n) { bad = true; return 0; }
		const char *s = (const char *)p; p += n; return s;
	}
};
