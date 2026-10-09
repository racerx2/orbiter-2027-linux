// not upstream: collision addon, the one thin SDK interface of CollPlugin, E1, E2, E3; CollSdkOrbiter.cpp implements it, Tests/CollFakeSdk.h fakes it
#ifndef __COLLSDK_H
#define __COLLSDK_H
#include <cstdint>
#include <exception>
#include <string>
#include <vector>
#include "Vecmat.h"                          // collvm copy (E4 3.2)
struct ANIMATION; struct CollVtx; struct DentVtx;
typedef const void *CollH;                   // OBJHANDLE, MESHHANDLE, DEVMESHHANDLE, VISHANDLE, THRUSTER_HANDLE, PROPELLANT_HANDLE, SUPERVESSELHANDLE, FILEHANDLE
typedef void (*CollCmdFn) (void *ctx);

enum CollSdkKind : uint8_t {
	CSK_STATE, CSK_ATTITUDE, CSK_SPIN, CSK_FORCE, CSK_TANK, CSK_WARP, // state writes, "writes=" of the summary
	CSK_VTX, CSK_MATRIX,                     // client-side visual edits
	CSK_PROBE, CSK_NOTICE, CSK_UI, CSK_N     // -0.0 probe, clbkGeneric, notification, annotation, command and dialog calls
};
enum : int { COLLN_SUCCESS = 0, COLLN_WARNING = 1, COLLN_ERROR = 2, COLLN_INFO = 3 }; // notification types, equal to OAPINOTIF_* (OrbiterAPI.h:354-357)
struct CollSdkCount {
	uint64_t n[CSK_N] = {};
	int64_t cmds = 0;                        // registered minus unregistered custom commands
	uint64_t Writes () const { uint64_t s = 0; for (int k = CSK_STATE; k <= CSK_WARP; k++) s += n[k]; return s; }
};

struct CollVesselRead {                      // E1 1.2: one vessel at t0, s0 copies
	Vector x, v, w, arot, aTot, W, svcg;     // GetGlobalPos, GetGlobalVel, GetAngularVel, GetAngularAcc, GetForceVector, GetWeightVector, GetSuperstructureCG
	Matrix R; double m; Vector pmi;          // GetRotationMatrix, GetMass, GetPMI
	uint32_t status;                         // GetFlightStatus
	bool playback, recording, ground, thrust; // Playback, Recording, GroundContact, GetThrustVector return value
	CollH gref, sv, sref;                    // GetGravityRef, GetSupervessel, GetSurfaceRef
};
enum : uint32_t { CVR_NOWEIGHT = 1 };        // skip GetWeightVector, which primes weight_valid (review CA-3 10)
struct CollStateWrite { CollH rbody; Vector rpos, rvel, vrot, arot; }; // DefSetStateEx with version 2, flag 0, status 0 (E1 6.2)
struct CollTplGroup { const CollVtx *vtx; const uint16_t *idx; uint32_t nvtx, nidx, usrflag; }; // MESHGROUPEX view, no copy
struct CollPortInfo { Vector pos, dir, rot; CollH mate; };          // vessel frame
struct CollAttInfo  { Vector pos, dir, rot; CollH mate; char id[9]; };

typedef void (*CollFailFn) (const char *where, const char *what);
inline CollFailFn g_collFail = nullptr;      // E4: logs once and turns the session off; NULL: the error is dropped

template <class R, class F> R CollGuard (const char *where, R onFail, F &&f) noexcept // no exception leaves a module callback or a colla* export
{
	try { return f (); }
	catch (const std::exception &e) { if (g_collFail) g_collFail (where, e.what ()); }
	catch (...) { if (g_collFail) g_collFail (where, "unknown exception"); }
	return onFail;
}

template <class F> void CollGuard (const char *where, F &&f) noexcept
{
	try { f (); }
	catch (const std::exception &e) { if (g_collFail) g_collFail (where, e.what ()); }
	catch (...) { if (g_collFail) g_collFail (where, "unknown exception"); }
}

class CollSdk {
public:
	explicit CollSdk (bool imgui): imgui (imgui) {}            // imgui = COLL_HAVE_IMGUI of the build (E3 9.4); the fake sets either value
	virtual ~CollSdk () = default;
	// world and bodies
	virtual uint32_t VesselCount () = 0;
	virtual CollH    Vessel (uint32_t i) = 0;
	virtual bool     IsVessel (CollH h) = 0;
	virtual int      ObjType (CollH h) = 0;
	virtual std::string Name (CollH h) = 0;                    // vessels: GetName, uncut; other bodies: oapiGetObjectName (256)
	virtual std::string ClassName (CollH v) = 0;               // "" if NULL
	virtual double   Size (CollH h) = 0;
	virtual void     GlobalState (CollH h, Vector &pos, Vector &vel, Matrix &R) = 0;
	virtual void     RelState (CollH v, CollH ref, Vector &rpos, Vector &rvel) // vessel state relative to ref as the core keeps it (GetRelativePos/Vel): a write in the post-step lands where it was read
	{
		CollVesselRead rd {};
		ReadVessel (v, rd, CVR_NOWEIGHT);
		Vector x, w; Matrix R;
		if (ref) GlobalState (ref, x, w, R);
		rpos = rd.x - x; rvel = rd.v - w;
	}
	virtual uint32_t GbodyCount () = 0;
	virtual CollH    Gbody (uint32_t i) = 0;
	virtual uint32_t BaseCount (CollH planet) = 0;
	virtual CollH    Base (CollH planet, uint32_t i) = 0;
	virtual void     BaseEquPos (CollH base, double &lng, double &lat, double &rad) = 0;
	virtual double   Elevation (CollH planet, double lng, double lat) = 0;
	virtual double   PlanetPeriod (CollH planet) = 0;
	virtual double   Mass (CollH h) = 0;                        // oapiGetMass, any object (E1: the gravity reference, E1 6.3); for a vessel the value of GetMass
	virtual double   SimTime () = 0;
	virtual double   SimMJD () = 0;
	virtual double   SysTime () = 0;                            // annotation timeout, message rate limits
	virtual double   Warp () = 0;
	// vessel reads (E1, E3)
	virtual void     ReadVessel (CollH v, CollVesselRead &out, uint32_t flags) = 0;
	virtual double   EmptyMass (CollH v) = 0;
	virtual bool     Recording (CollH v) = 0;
	virtual bool     Playback (CollH v) = 0;
	virtual int      DamageModel (CollH v) = 0;                 // E3 passes Vessel (0); the value is the session setting
	// meshes and animations (E2)
	virtual uint32_t MeshCount (CollH v) = 0;
	virtual CollH    MeshTemplate (CollH v, uint32_t i) = 0;   // NULL for name slots and holes
	virtual const char *MeshName (CollH v, uint32_t i) = 0;    // live slots only
	virtual Vector   MeshOffset (CollH v, uint32_t i) = 0;     // live slots only
	virtual uint16_t MeshVisMode (CollH v, uint32_t i) = 0;    // live slots only
	virtual const char *TplName (CollH tpl) = 0;               // NULL if anonymous
	virtual uint32_t TplGroups (CollH tpl) = 0;
	virtual bool     TplGroup (CollH tpl, uint32_t g, CollTplGroup &out) = 0; // tpl never NULL
	virtual uint32_t Anims (CollH v, const ANIMATION **a) = 0;
	// docks and attachments (E1, E2)
	virtual uint32_t DockCount (CollH v) = 0;
	virtual bool     Dock (CollH v, uint32_t i, CollPortInfo &out) = 0;
	virtual uint32_t AttachCount (CollH v, bool toparent) = 0;
	virtual bool     Attach (CollH v, bool toparent, uint32_t i, CollAttInfo &out) = 0;
	// thrusters and tanks (E3)
	virtual uint32_t ThrusterCount (CollH v) = 0;
	virtual CollH    Thruster (CollH v, uint32_t i) = 0;
	virtual CollH    ThrusterTank (CollH v, CollH th) = 0;
	virtual uint32_t TankCount (CollH v) = 0;
	virtual CollH    Tank (CollH v, uint32_t i) = 0;            // NULL if i >= TankCount
	virtual double   TankMass (CollH v, CollH tk) = 0;
	virtual double   TankMaxMass (CollH v, CollH tk) = 0;
	// visuals and the client (E2, E3)
	virtual CollH    Visual (CollH v) = 0;                     // *oapiObjectVisualPtr (v); read in every pass, never cached
	virtual CollH    DevMesh (CollH v, CollH vis, uint32_t i) = 0; // vis never NULL
	virtual int      ReadVtx (CollH dm, uint32_t g, const uint16_t *idx, uint32_t n, DentVtx *out) = 0; // 0 ok, 1 bad group, -1 no client, -2 unsupported
	virtual bool     ClientCore () = 0;                        // gcCore bound for this session (1.6)
	virtual int      ClientMatrix (int id, CollH v, uint32_t mesh, uint32_t grp, float m[16]) = 0; // gcCore GetMatrix; client codes
	// files, config, scenario, log
	virtual std::string Resolve (const std::string &path) = 0;
	virtual bool     ReadText (const std::string &path, std::string &out) = 0; // text-mode ifstream, as the core
	virtual std::vector<std::string> ListDir (const std::string &dir) = 0;
	virtual bool     CfgString (const char *file, int root, const char *item, std::string &val) = 0;
	virtual bool     CfgInt (const char *file, int root, const char *item, int &val) = 0;
	virtual bool     CfgReal (const char *file, int root, const char *item, double &val) = 0;
	virtual bool     CfgBool (const char *file, int root, const char *item, bool &val) = 0;
	virtual bool     ScnLine (CollH scn, std::string &line) = 0;
	virtual void     ScnWrite (CollH scn, const std::string &line) = 0;
	virtual void     Log (int level, const char *msg) = 0;
	// counted calls: non-virtual, count + log line + forward (CollSdk.cpp)
	void  SetState (CollH v, const CollStateWrite &s);
	void  SetAttitude (CollH v, const Matrix &R);
	void  SetSpin (CollH v, const Vector &w);
	void  AddForce (CollH v, const Vector &F, const Vector &r);
	void  SetTank (CollH v, CollH th, CollH tank);              // SetThrusterResource; tank NULL also zeroes the level
	CollH CreateTank (CollH v, double maxMass, double mass);    // CreatePropellantResource; appended as the last tank; pass mass >= 0
	void  DelTank (CollH v, CollH tank);                        // DelPropellantResource; unlinks its thrusters
	void  SetTankMass (CollH v, CollH tank, double m);          // SetPropellantMass (SDK form: no clamp to the max mass)
	void  SetWarp (double w);
	int   WriteVtx (CollH v, CollH dm, uint32_t g, const uint16_t *idx, uint32_t n, const DentVtx *vtx); // codes as ReadVtx
	int   SetClientMatrix (int id, CollH v, uint32_t mesh, uint32_t grp, const float m[16]);
	bool  ProbeSlot (CollH v, uint32_t i);                      // ShiftMesh (i, {-0.0,-0.0,-0.0}); false: hole or out of range
	bool  Notify (CollH v, int prm, void *payload, int &reply); // false: gone or Version () < 2
	void  Notification (int type, const char *title, const char *text); // COLLN_*; without imgui: log line + Annotation (8 s)
	void  Annotation (const char *text, double holdSys);        // the session's screen line; "" clears; cleared after holdSys s of system time
	void  UiTick ();                                            // clears an expired annotation; E4's post-step stage PO4 calls it
	int   RegisterCmd (const char *label, const char *desc, CollCmdFn fn, void *ctx); // E4, on its process instance (W1, 1.6)
	void  UnregisterCmd (int id);                               // E4, on its process instance
	bool  OpenDialog (void *imguiDialog);                       // E3's command handler, on the process instance; false without imgui
	const CollSdkCount &Count () const { return cnt; }
	const bool imgui;                                           // ImGuiDialog and oapiAddNotification present (one macro, E3 9.4)
	int logLevel = 1;                                           // CollisionLog
protected:
	virtual void  DoSetState (CollH v, const CollStateWrite &s) = 0;
	virtual void  DoSetAttitude (CollH v, const Matrix &R) = 0;
	virtual void  DoSetSpin (CollH v, const Vector &w) = 0;
	virtual void  DoAddForce (CollH v, const Vector &F, const Vector &r) = 0;
	virtual void  DoSetTank (CollH v, CollH th, CollH tank) = 0;
	virtual CollH DoCreateTank (CollH v, double maxMass, double mass) = 0;
	virtual void  DoDelTank (CollH v, CollH tank) = 0;
	virtual void  DoSetTankMass (CollH v, CollH tank, double m) = 0;
	virtual void  DoSetWarp (double w) = 0;
	virtual int   DoWriteVtx (CollH dm, uint32_t g, const uint16_t *idx, uint32_t n, const DentVtx *vtx) = 0;
	virtual int   DoSetClientMatrix (int id, CollH v, uint32_t mesh, uint32_t grp, const float m[16]) = 0;
	virtual bool  DoProbe (CollH v, uint32_t i) = 0;
	virtual bool  DoNotify (CollH v, int prm, void *payload, int &reply) = 0;
	virtual void  DoNotification (int type, const char *title, const char *text) = 0; // only called with imgui
	virtual void  DoAnnotation (const char *text) = 0;          // creates the note lazily; nothing without a render window
	virtual int   DoRegisterCmd (const char *label, const char *desc, CollCmdFn fn, void *ctx) = 0;
	virtual void  DoUnregisterCmd (int id) = 0;
	virtual bool  DoOpenDialog (void *imguiDialog) = 0;         // only called with imgui
public: // dmg3 area P
	struct DebrisCaps { double size = 1, mass = 10; Vector pmi, cs; std::vector<Vector> td; double tdK = 0, tdD = 0, mu = 0.5; }; // CollDebris vessel setup
	uint64_t flagCalls = 0;                                     // GroupFlag calls, separate from CSK_VTX (re-asserted every pass)
	int   GroupFlag (CollH v, CollH dm, uint32_t g, uint32_t flag, bool add) { (void)v; flagCalls++; return DoGroupFlag (dm, g, flag, add); } // client UsrFlag add or delete
	CollH VesselCreate (const char *name, const char *cls, const CollStateWrite &s) { cnt.n[CSK_STATE]++; LogCall (CSK_STATE, nullptr); return DoVesselCreate (name, cls, s); } // VESSELSTATUS2 v2, status 0
	bool  DebrisSetup (CollH v, CollH mesh, const DebrisCaps &c) { cnt.n[CSK_STATE]++; LogCall (CSK_STATE, v); return DoDebrisSetup (v, mesh, c); } // AddMesh, size, mass, PMI, cross sections, touchdown points
	bool  VesselDelete (CollH v) { cnt.n[CSK_STATE]++; LogCall (CSK_STATE, v); return DoVesselDelete (v); }
	virtual CollH MeshLoad (const char *) { return nullptr; }  // oapiLoadMesh: private copy, NULL if missing
	virtual bool  MeshEdit (CollH, uint32_t, uint32_t, const DentVtx *, uint32_t) { return false; } // template group: add UsrFlag (if nonzero), first n vertices (if vtx)
	virtual void  MeshFree (CollH) {}                          // oapiDeleteMesh
	virtual bool  DebrisClassExists () { std::string t; return ReadText (Resolve ("Config/Vessels/CollDebris.cfg"), t); }
	virtual uint32_t TouchdownCount (CollH) { return 0; }
	virtual bool  Touchdown (CollH, uint32_t, Vector &) { return false; }   // vessel frame
	virtual void  SetEmptyMass (CollH, double) {}               // blast: the parent loses the mass of its broken cells
	virtual void  SetPMI (CollH, const Vector &) {}             // blast: and their inertia
	virtual bool  ThrusterPos (CollH, CollH, Vector &) { return false; }    // vessel frame
protected:
	virtual int   DoGroupFlag (CollH, uint32_t, uint32_t, bool) { return -1; }
	virtual CollH DoVesselCreate (const char *, const char *, const CollStateWrite &) { return nullptr; }
	virtual bool  DoDebrisSetup (CollH, CollH, const DebrisCaps &) { return false; }
	virtual bool  DoVesselDelete (CollH) { return false; }
public: // dmg3 area F
	enum : uint8_t { FX_TEX_FLAKE = 0, FX_TEX_SPARK = 1, FX_TEX_VENT = 2, FX_TEX_DUST = 3 }; // Contrail1a, Exhaust, Contrail1, Contrail4
	enum : uint8_t { FX_EMISSIVE = 0, FX_DIFFUSE = 1 };                // PARTICLESTREAMSPEC::LTYPE
	enum : uint8_t { FX_LVL_FLAT = 0, FX_LVL_LIN = 1 };                // PARTICLESTREAMSPEC::LEVELMAP
	struct FxSpec {                                                   // PARTICLESTREAMSPEC with ATM_FLAT; the client copies it
		double size = 0, rate = 0, v0 = 0, spread = 0, life = 0, grow = 0, slow = 0;
		uint8_t ltype = FX_DIFFUSE, lmap = FX_LVL_LIN, tex = FX_TEX_FLAKE;
		double lmin = 0, lmax = 1, amin = 1, amax = 1;
	};
	virtual CollH  FxAdd (CollH, const FxSpec &, const Vector &, const Vector &, double *) { return nullptr; } // AddParticleStream (v, spec, pos, dir, lvl); NULL without a client or with streams off
	virtual bool   FxDel (CollH, CollH) { return false; }            // DelExhaustStream (v, ps); false: unknown or detached handle
	virtual double FxAtm (CollH) { return 0; }                       // GetAtmDensity [kg/m^3]
	virtual bool   FxGround (CollH, Vector &vLoc, Vector &upLoc, double &alt) { vLoc = upLoc = Vector (); alt = 0; return false; } // GroundContact; ground speed and up in the vessel frame, altitude over ground
private:
	CollSdkCount cnt;
	double noteUntil = -1;                                      // system time the annotation expires, -1 none
	void LogCall (CollSdkKind k, CollH v);
};
#endif
