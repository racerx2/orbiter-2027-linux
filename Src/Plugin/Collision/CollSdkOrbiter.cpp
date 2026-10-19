// not upstream: collision addon, the only file that calls the Orbiter SDK for CollSdk (design E2 1.6)
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include "OrbiterAPI.h"
#include "ModuleAPI.h"
#include "VesselAPI.h"
#include "CollisionAPI.h"
#include "CollSdkOrbiter.h"
#include "CollPlatform.h"
#include "DentMath.h"

static_assert (sizeof (NTVERTEX) == 32, "NTVERTEX must keep the CollVtx layout");
#if defined (OAPI_VESSEL_CONTROL_LOCK) && !defined (_WIN32)
extern void oapiSetVesselControlLock (OBJHANDLE hVessel, bool lock) __attribute__ ((weak)); // not upstream: weak, so a host without it still loads the plugin
#endif
#if defined (OAPI_COPY_MESH) && !defined (_WIN32)
extern MESHHANDLE oapiCopyMesh (MESHHANDLE hMesh, DWORD dropFlag) __attribute__ ((weak)); // not upstream: weak, a host without it loads every debris mesh from file
#define COLL_COPY_MESH 1
#endif
#if defined (OAPI_BASE_CLEARZONES) && !defined (_WIN32)
extern DWORD oapiGetBaseClearZones (OBJHANDLE hBase, BASECLEARZONE *zone, DWORD nmax) __attribute__ ((weak)); // not upstream: weak, a host without it has no clear zones
#define COLL_BASE_CLEARZONES 1
#endif
#if defined (OAPI_BASE_DENT) && !defined (_WIN32)
extern bool oapiBaseSetDent (OBJHANDLE hBase, const BASEDENTSPEC *d) __attribute__ ((weak)); // not upstream: weak, a host without it draws no building damage
extern void oapiBaseClearDents (OBJHANDLE hBase, DWORD obj) __attribute__ ((weak));
#define COLL_BASE_DENT 1
#endif
#if defined (OAPI_GLTF_DROPIN_MESH) && !defined (_WIN32)
extern bool oapiHasGltfDropInMesh (const char *fname) __attribute__ ((weak));
#endif
#if defined (OAPI_MESHGROUP_FLAGS) && !defined (_WIN32)
extern int oapiGetMeshGroupFlags (DEVMESHHANDLE hMesh, DWORD grpidx, DWORD *usrFlag) __attribute__ ((weak)); // not upstream: weak, a host without it leaves hidden groups solid
#define COLL_MESHGROUP_FLAGS 1
#endif

namespace {
inline Vector V (const VECTOR3 &v) { return Vector (v.x, v.y, v.z); }
inline VECTOR3 O (const Vector &v) { return _V (v.x, v.y, v.z); }
inline Matrix M (const MATRIX3 &m) { return Matrix (m.m11, m.m12, m.m13, m.m21, m.m22, m.m23, m.m31, m.m32, m.m33); }
inline MATRIX3 O (const Matrix &m) { return _M (m.m11, m.m12, m.m13, m.m21, m.m22, m.m23, m.m31, m.m32, m.m33); }
inline OBJHANDLE H (CollH h) { return (OBJHANDLE)h; }
inline VESSEL *Ves (CollH h) { return h ? oapiGetVesselInterface (H (h)) : nullptr; }

class CollSdkOrbiter final : public CollSdk {
public:
	explicit CollSdkOrbiter (bool process): CollSdk (COLL_HAVE_IMGUI != 0), process (process) {}
	bool process;
	NOTEHANDLE note = nullptr;
	bool core = false;
	uint32_t VesselCount () override { return (uint32_t)oapiGetVesselCount (); }
	CollH Vessel (uint32_t i) override { return oapiGetVesselByIndex ((int)i); }
	bool IsVessel (CollH h) override { return oapiIsVessel (H (h)); }
	int ObjType (CollH h) override { return oapiGetObjectType (H (h)); }
	std::string Name (CollH h) override
	{
		if (oapiIsVessel (H (h))) { VESSEL *v = Ves (h); return v && v->GetName () ? v->GetName () : ""; }
		char buf[256] = "";
		oapiGetObjectName (H (h), buf, 256);
		return buf;
	}
	std::string ClassName (CollH v) override { VESSEL *x = Ves (v); const char *c = x ? x->GetClassName () : nullptr; return c ? c : ""; }
	double Size (CollH h) override { return oapiGetSize (H (h)); }
	void RelState (CollH v, CollH ref, Vector &rpos, Vector &rvel) override
	{
		if (!ref) { CollSdk::RelState (v, ref, rpos, rvel); return; }
		VECTOR3 a, b;
		Ves (v)->GetRelativePos (H (ref), a); Ves (v)->GetRelativeVel (H (ref), b);
		rpos = V (a); rvel = V (b);
	}
	void GlobalState (CollH h, Vector &pos, Vector &vel, Matrix &R) override
	{
		VECTOR3 p, v; MATRIX3 r;
		oapiGetGlobalPos (H (h), &p); oapiGetGlobalVel (H (h), &v); oapiGetRotationMatrix (H (h), &r);
		pos = V (p); vel = V (v); R = M (r);
	}
	uint32_t GbodyCount () override { return (uint32_t)oapiGetGbodyCount (); }
	CollH Gbody (uint32_t i) override { return oapiGetGbodyByIndex ((int)i); }
	uint32_t BaseCount (CollH planet) override { return (uint32_t)oapiGetBaseCount (H (planet)); }
	CollH Base (CollH planet, uint32_t i) override { return oapiGetBaseByIndex (H (planet), (int)i); }
	void BaseEquPos (CollH base, double &lng, double &lat, double &rad) override { oapiGetBaseEquPos (H (base), &lng, &lat, &rad); }
	double Elevation (CollH planet, double lng, double lat) override { return oapiSurfaceElevation (H (planet), lng, lat); }
	double PlanetPeriod (CollH planet) override { return oapiGetPlanetPeriod (H (planet)); }
	double Mass (CollH h) override { return oapiGetMass (H (h)); }
	double SimTime () override { return oapiGetSimTime (); }
	double SimMJD () override { return oapiGetSimMJD (); }
	double RefMJD () override { return oapiTime2MJD (0.0); }
	double SysTime () override { return oapiGetSysTime (); }
	double Warp () override { return oapiGetTimeAcceleration (); }
	void ReadVessel (CollH h, CollVesselRead &o, uint32_t flags) override
	{
		VESSEL *v = Ves (h);
		VECTOR3 a;
		v->GetGlobalPos (a); o.x = V (a);
		v->GetGlobalVel (a); o.v = V (a);
		v->GetAngularVel (a); o.w = V (a);
		v->GetAngularAcc (a); o.arot = V (a);
		v->GetForceVector (a); o.aTot = V (a);
		if (!(flags & CVR_NOWEIGHT)) { v->GetWeightVector (a); o.W = V (a); } else o.W = Vector ();
		a = _V (0, 0, 0); v->GetSuperstructureCG (a); o.svcg = V (a);
		MATRIX3 R; v->GetRotationMatrix (R); o.R = M (R);
		o.m = v->GetMass ();
		v->GetPMI (a); o.pmi = V (a);
		o.status = v->GetFlightStatus ();
		o.playback = v->Playback (); o.recording = v->Recording (); o.ground = v->GroundContact ();
		o.thrust = v->GetThrustVector (a);
		o.gref = v->GetGravityRef (); o.sv = v->GetSupervessel (); o.sref = v->GetSurfaceRef ();
	}
	double EmptyMass (CollH v) override { return Ves (v)->GetEmptyMass (); }
	bool Recording (CollH v) override { return Ves (v)->Recording (); }
	bool Playback (CollH v) override { return Ves (v)->Playback (); }
	int DamageModel (CollH v) override { VESSEL *x = Ves (v); return x ? x->GetDamageModel () : -1; }
	bool ControlLock (CollH v, bool lock) override
	{
#if defined (OAPI_VESSEL_CONTROL_LOCK) && !defined (_WIN32)
		if (!oapiSetVesselControlLock || !v || !oapiIsVessel (H (v))) return false; // weak: a host without it (tests, an older core) leaves the plugin loadable
		oapiSetVesselControlLock (H (v), lock);
		return true;
#else
		(void)v; (void)lock; return false; // an upstream core: the thrust cut and the control surfaces only
#endif
	}
	int ADCtrlMode (CollH v) override { VESSEL *x = Ves (v); return x ? (int)x->GetADCtrlMode () : -1; }
	void SetADCtrlMode (CollH v, int mode) override { VESSEL *x = Ves (v); if (x && mode >= 0) x->SetADCtrlMode ((DWORD)mode); }
bool MeshHasGltfDropIn (const char *name) override
	{
#if defined (OAPI_GLTF_DROPIN_MESH) && !defined (_WIN32)
		return oapiHasGltfDropInMesh && oapiHasGltfDropInMesh (name);
#else
		(void)name;
		return false;
#endif
	}
	uint32_t MeshCount (CollH v) override { return Ves (v)->GetMeshCount (); }
	CollH MeshTemplate (CollH v, uint32_t i) override { return Ves (v)->GetMeshTemplate (i); }
	const char *MeshName (CollH v, uint32_t i) override { return Ves (v)->GetMeshName (i); }
	Vector MeshOffset (CollH v, uint32_t i) override { VECTOR3 o = _V (0, 0, 0); Ves (v)->GetMeshOffset (i, o); return V (o); }
	uint16_t MeshVisMode (CollH v, uint32_t i) override { return Ves (v)->GetMeshVisibilityMode (i); }
	const char *TplName (CollH tpl) override { return oapiGetMeshFilename ((MESHHANDLE)tpl); }
	uint32_t TplGroups (CollH tpl) override { return oapiMeshGroupCount ((MESHHANDLE)tpl); }
	bool TplGroup (CollH tpl, uint32_t g, CollTplGroup &out) override
	{
		assert (tpl && g < TplGroups (tpl));
		if (!tpl || g >= TplGroups (tpl)) return false;
		MESHGROUPEX *mg = oapiMeshGroupEx ((MESHHANDLE)tpl, g);
		if (!mg) return false;
		out = { (const CollVtx *)mg->Vtx, (const uint16_t *)mg->Idx, (uint32_t)mg->nVtx, (uint32_t)mg->nIdx, (uint32_t)mg->UsrFlag };
		return true;
	}
	uint32_t Anims (CollH v, const ANIMATION **a) override { ANIMATION *p = nullptr; UINT n = Ves (v)->GetAnimPtr (&p); *a = p; return n; }
	uint32_t DockCount (CollH v) override { return Ves (v)->DockCount (); }
	bool Dock (CollH v, uint32_t i, CollPortInfo &out) override
	{
		VESSEL *x = Ves (v);
		DOCKHANDLE d = x->GetDockHandle (i);
		if (!d) return false;
		VECTOR3 p, dr, r;
		x->GetDockParams (d, p, dr, r);
		out = { V (p), V (dr), V (r), x->GetDockStatus (d) };
		return true;
	}
	uint32_t AttachCount (CollH v, bool tp) override { return Ves (v)->AttachmentCount (tp); }
	bool Attach (CollH v, bool tp, uint32_t i, CollAttInfo &out) override
	{
		VESSEL *x = Ves (v);
		ATTACHMENTHANDLE a = x->GetAttachmentHandle (tp, i);
		if (!a) return false;
		VECTOR3 p, dr, r;
		x->GetAttachmentParams (a, p, dr, r);
		out.pos = V (p); out.dir = V (dr); out.rot = V (r); out.mate = x->GetAttachmentStatus (a);
		const char *id = x->GetAttachmentId (a);
		memset (out.id, 0, sizeof out.id);
		if (id) strncpy (out.id, id, 8);
		return true;
	}
	uint32_t ThrusterCount (CollH v) override { return Ves (v)->GetThrusterCount (); }
	CollH Thruster (CollH v, uint32_t i) override { return Ves (v)->GetThrusterHandleByIndex (i); }
	CollH ThrusterTank (CollH v, CollH th) override { return Ves (v)->GetThrusterResource ((THRUSTER_HANDLE)th); }
	uint32_t TankCount (CollH v) override { return Ves (v)->GetPropellantCount (); }
	CollH Tank (CollH v, uint32_t i) override { return i < TankCount (v) ? Ves (v)->GetPropellantHandleByIndex (i) : nullptr; }
	double TankMass (CollH v, CollH tk) override { return Ves (v)->GetPropellantMass ((PROPELLANT_HANDLE)tk); }
	double TankMaxMass (CollH v, CollH tk) override { return Ves (v)->GetPropellantMaxMass ((PROPELLANT_HANDLE)tk); }
	CollH Visual (CollH v) override { VISHANDLE *p = oapiObjectVisualPtr (H (v)); return p ? *p : nullptr; }
	CollH DevMesh (CollH v, CollH vis, uint32_t i) override { assert (vis); return Ves (v)->GetDevMesh ((VISHANDLE)vis, i); }
	int DevGroupFlags (CollH dm, uint32_t g, uint32_t &flags) override
	{
#ifdef COLL_MESHGROUP_FLAGS
		DWORD f = 0;
		if (!oapiGetMeshGroupFlags || !dm) return -2;
		int r = oapiGetMeshGroupFlags ((DEVMESHHANDLE)dm, g, &f);
		flags = (uint32_t)f;
		return r;
#else
		(void)dm; (void)g; (void)flags;
		return -2;
#endif
	}
	int ReadVtx (CollH dm, uint32_t g, const uint16_t *idx, uint32_t n, DentVtx *out) override
	{
		if (!dm) return -1;
		GROUPREQUESTSPEC grs; memset (&grs, 0, sizeof grs);
		grs.Vtx = (NTVERTEX *)out; grs.nVtx = n; grs.VtxPerm = (WORD *)idx;
		return oapiGetMeshGroup ((DEVMESHHANDLE)dm, g, &grs);
	}
	bool ClientCore () override { return core; }
	int ClientMatrix (int, CollH, uint32_t, uint32_t, float[16]) override { return -1; }
	std::string Resolve (const std::string &path) override
	{
#ifdef _WIN32
		return path;
#else
		return oapiResolvePath (path.c_str ());
#endif
	}
	bool ReadText (const std::string &path, std::string &out) override
	{
		std::ifstream f (path);
		if (!f) return false;
		std::ostringstream s; s << f.rdbuf ();
		out = s.str ();
		return true;
	}
	std::vector<std::string> ListDir (const std::string &dir) override
	{
		std::vector<std::string> r;
		std::error_code ec;
		std::filesystem::directory_iterator it (std::filesystem::path (dir), ec), end; // no throwing overloads: an error ends the listing
		for (; !ec && it != end; it.increment (ec)) {
			std::u8string n = it->path ().filename ().u8string (); // string () throws on Windows for names outside the code page
			r.emplace_back (n.begin (), n.end ());
		}
		std::sort (r.begin (), r.end ()); // NTFS order
		return r;
	}
	bool CfgString (const char *file, int root, const char *item, std::string &val) override
	{
		FILEHANDLE f = oapiOpenFile (file, FILE_IN_ZEROONFAIL, (PathRoot)root);
		if (!f) return false;
		std::string it (item);
		char buf[1024] = "";
		bool ok = oapiReadItem_string (f, it.data (), buf);
		oapiCloseFile (f, FILE_IN_ZEROONFAIL);
		if (ok) val = buf;
		return ok;
	}
	bool CfgInt (const char *file, int root, const char *item, int &val) override
	{
		FILEHANDLE f = oapiOpenFile (file, FILE_IN_ZEROONFAIL, (PathRoot)root);
		if (!f) return false;
		std::string it (item);
		bool ok = oapiReadItem_int (f, it.data (), val);
		oapiCloseFile (f, FILE_IN_ZEROONFAIL);
		return ok;
	}
	bool CfgReal (const char *file, int root, const char *item, double &val) override
	{
		FILEHANDLE f = oapiOpenFile (file, FILE_IN_ZEROONFAIL, (PathRoot)root);
		if (!f) return false;
		std::string it (item);
		bool ok = oapiReadItem_float (f, it.data (), val);
		oapiCloseFile (f, FILE_IN_ZEROONFAIL);
		return ok;
	}
	bool CfgBool (const char *file, int root, const char *item, bool &val) override
	{
		FILEHANDLE f = oapiOpenFile (file, FILE_IN_ZEROONFAIL, (PathRoot)root);
		if (!f) return false;
		std::string it (item);
		bool ok = oapiReadItem_bool (f, it.data (), val);
		oapiCloseFile (f, FILE_IN_ZEROONFAIL);
		return ok;
	}
	bool ScnLine (CollH scn, std::string &line) override
	{
		char *l = nullptr;
		if (!oapiReadScenario_nextline ((FILEHANDLE)scn, l) || !l) return false;
		line = l;
		return true;
	}
	void ScnWrite (CollH scn, const std::string &line) override { std::string b (line); oapiWriteLine ((FILEHANDLE)scn, b.data ()); }
	void Log (int, const char *msg) override { oapiWriteLogV ("%s", msg); }
protected:
	void DoSetState (CollH v, const CollStateWrite &s) override
	{
		VESSELSTATUS2 vs; memset (&vs, 0, sizeof vs);
		vs.version = 2; vs.flag = 0;
		vs.rbody = H (s.rbody); vs.rpos = O (s.rpos); vs.rvel = O (s.rvel); vs.vrot = O (s.vrot); vs.arot = O (s.arot);
		vs.status = 0;
		Ves (v)->DefSetStateEx (&vs);
	}
	void DoSetLanded (CollH v, const CollLandWrite &s) override // Vesselstatus.cpp: status 1 with arot.x <= 4 takes arot as R_planet^T R_vessel and vrot.x as the CG height over the terrain
	{
		VESSELSTATUS2 vs; memset (&vs, 0, sizeof vs);
		vs.version = 2; vs.flag = 0;
		vs.rbody = H (s.rbody); vs.base = nullptr; vs.port = -1;
		vs.status = 1;
		vs.surf_lng = s.lng; vs.surf_lat = s.lat; vs.surf_hdg = s.hdg;
		vs.arot = O (s.arot); vs.vrot = _V (s.alt, 0, 0);
		Ves (v)->DefSetStateEx (&vs);
	}
	void DoSetAttitude (CollH v, const Matrix &R) override { Ves (v)->SetRotationMatrix (O (R)); }
	void DoSetSpin (CollH v, const Vector &w) override { Ves (v)->SetAngularVel (O (w)); }
	void DoAddForce (CollH v, const Vector &F, const Vector &r) override { Ves (v)->AddForce (O (F), O (r)); }
	void DoSetTank (CollH v, CollH th, CollH tank) override { Ves (v)->SetThrusterResource ((THRUSTER_HANDLE)th, (PROPELLANT_HANDLE)tank); }
	CollH DoCreateTank (CollH v, double maxMass, double mass) override { assert (mass >= 0); return Ves (v)->CreatePropellantResource (maxMass, mass); }
	void DoDelTank (CollH v, CollH tank) override { PROPELLANT_HANDLE ph = (PROPELLANT_HANDLE)tank; Ves (v)->DelPropellantResource (ph); }
	void DoSetTankMass (CollH v, CollH tank, double m) override { Ves (v)->SetPropellantMass ((PROPELLANT_HANDLE)tank, m); }
	void DoSetWarp (double w) override { oapiSetTimeAcceleration (w); }
	int DoWriteVtx (CollH dm, uint32_t g, const uint16_t *idx, uint32_t n, const DentVtx *vtx) override
	{
		if (!dm) return -1;
		GROUPEDITSPEC ges; memset (&ges, 0, sizeof ges);
		ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML;
		ges.Vtx = (NTVERTEX *)vtx; ges.nVtx = n; ges.vIdx = (WORD *)idx;
		return oapiEditMeshGroup ((DEVMESHHANDLE)dm, g, &ges);
	}
	int DoSetClientMatrix (int, CollH, uint32_t, uint32_t, const float[16]) override { return -1; }
	bool DoProbe (CollH v, uint32_t i) override
	{
		VESSEL *x = Ves (v);
		if (!x || i >= x->GetMeshCount ()) return false;
		return x->ShiftMesh (i, _V (-0.0, -0.0, -0.0));
	}
	bool DoNotify (CollH h, int prm, void *payload, int &reply) override
	{
		if (!oapiIsVessel (H (h))) return false;
		VESSEL *v = Ves (h);
		if (!v || v->Version () < 2) return false;
		reply = static_cast<VESSEL3 *> (v)->clbkGeneric (COLLA_VMSG, prm, payload);
		return true;
	}
	void DoNotification (int type, const char *title, const char *text) override
	{
#if COLL_HAVE_IMGUI
		oapiAddNotification (type, title, text);
#else
		(void)type; (void)title; (void)text;
#endif
	}
	void DoAnnotation (const char *text) override
	{
		if (!note) {
			if (!*text) return;
			note = oapiCreateAnnotation (true, 0.8, _V (1, 0.8, 0.3));
			if (!note) return;
			oapiAnnotationSetPos (note, 0.05, 0.12, 0.7, 0.3);
		}
		std::string b (text);
		oapiAnnotationSetText (note, b.data ());
	}
	void DoForgetAnnotation () override { note = nullptr; } // deleted by the core (Orbiter.cpp:935-940)
	std::map<int, std::unique_ptr<char[]>> cmdDesc; // writable desc copies by command id: 2024 takes char* and keeps desc, copies the label
	int DoRegisterCmd (const char *label, const char *desc, CollCmdFn fn, void *ctx) override
	{
		std::string l (label ? label : "");
		size_t n = strlen (desc ? desc : "") + 1;
		std::unique_ptr<char[]> d (new char[n]);
		memcpy (d.get (), desc ? desc : "", n);
		int id = (int)oapiRegisterCustomCmd (l.data (), d.get (), (CustomFunc)fn, ctx);
		if (id) try { cmdDesc[id] = std::move (d); } catch (...) { oapiUnregisterCustomCmd (id); throw; } // no command left behind
		return id;
	}
	void DoUnregisterCmd (int id) override { oapiUnregisterCustomCmd (id); cmdDesc.erase (id); }
	bool DoOpenDialog (void *d) override
	{
#if COLL_HAVE_IMGUI
		oapiOpenDialog ((ImGuiDialog *)d);
		return true;
#else
		(void)d; return false;
#endif
	}
public: // dmg3 area P
	void SetEmptyMass (CollH v, double m) override { if (m > 0) Ves (v)->SetEmptyMass (m); }
	void SetPMI (CollH v, const Vector &p) override { if (p.x > 0 && p.y > 0 && p.z > 0) Ves (v)->SetPMI (O (p)); }
	std::map<std::string, MESHHANDLE> meshMaster; // one file read per mesh a session; debris take copies (a breakup spawns dozens)
	~CollSdkOrbiter () override { for (auto &kv : meshMaster) if (kv.second) oapiDeleteMesh (kv.second); }
	CollH MeshLoad (const char *name) override
	{
		std::string n (name ? name : "");
		if (n.empty ()) return nullptr;
		auto load = [&] () { MESHHANDLE m = oapiLoadMesh (n.data ()); if (m && !oapiMeshGroupCount (m)) { oapiDeleteMesh (m); m = nullptr; } return m; }; // a missing file loads as an empty mesh
#ifdef COLL_COPY_MESH
		if (oapiCopyMesh) {
			auto it = meshMaster.find (n);
			if (it == meshMaster.end ()) it = meshMaster.emplace (n, load ()).first;
			return it->second ? (CollH)oapiCopyMesh (it->second, 0) : nullptr;
		}
#endif
		return (CollH)load ();
	}
	CollH MeshCompact (CollH mesh) override
	{
#ifdef COLL_COPY_MESH
		if (mesh && oapiCopyMesh) { // the hidden groups left out: a debris piece uploads its own triangles only, not the whole hull
			MESHHANDLE m = oapiCopyMesh ((MESHHANDLE)mesh, 2);
			if (m && oapiMeshGroupCount (m)) { oapiDeleteMesh ((MESHHANDLE)mesh); return (CollH)m; }
			if (m) oapiDeleteMesh (m);
		}
#endif
		return mesh;
	}
	bool MeshEdit (CollH mesh, uint32_t g, uint32_t addFlag, const DentVtx *vtx, uint32_t n) override
	{
		if (!mesh) return false;
		GROUPEDITSPEC ges; memset (&ges, 0, sizeof ges);
		if (addFlag) ges.flags |= GRPEDIT_ADDUSERFLAG, ges.UsrFlag = addFlag;
		if (vtx && n) ges.flags |= GRPEDIT_VTXCRD | GRPEDIT_VTXNML, ges.Vtx = (NTVERTEX *)vtx, ges.nVtx = n;
		return oapiEditMeshGroup ((MESHHANDLE)mesh, g, &ges) == 0;
	}
	int MeshAddGroup (CollH mesh, uint32_t from, const DentVtx *vtx, uint32_t nv, const uint16_t *idx, uint32_t ni) override
	{
		if (!mesh || !vtx || !idx || !nv || !ni || nv > 65535) return -1;
		MESHGROUPEX *src = from < oapiMeshGroupCount ((MESHHANDLE)mesh) ? oapiMeshGroupEx ((MESHHANDLE)mesh, from) : nullptr;
		if (!src) return -1;
		MESHGROUP g; memset (&g, 0, sizeof g);
		g.Vtx = (NTVERTEX *)vtx; g.nVtx = nv; g.Idx = (WORD *)idx; g.nIdx = ni;
		g.MtrlIdx = src->MtrlIdx; g.TexIdx = src->TexIdx; g.zBias = src->zBias;
		DWORD flag = src->UsrFlag & ~2u; // read before the add reallocates the groups
		DWORD k = oapiAddMeshGroup ((MESHHANDLE)mesh, &g); // deep copy (Mesh::AddGroup)
		MESHGROUPEX *dst = oapiMeshGroupEx ((MESHHANDLE)mesh, k);
		src = oapiMeshGroupEx ((MESHHANDLE)mesh, from); // again: the add moved the groups
		if (dst && src) { memcpy (dst->TexIdxEx, src->TexIdxEx, sizeof dst->TexIdxEx); memcpy (dst->TexMixEx, src->TexMixEx, sizeof dst->TexMixEx); } // the extra textures (night lights) as the source group
		GROUPEDITSPEC ges; memset (&ges, 0, sizeof ges);
		ges.flags = GRPEDIT_SETUSERFLAG; ges.UsrFlag = flag;
		oapiEditMeshGroup ((MESHHANDLE)mesh, k, &ges);
		return (int)k;
	}
	void MeshFree (CollH mesh) override { if (mesh) oapiDeleteMesh ((MESHHANDLE)mesh); }
	bool DebrisClassExists () override
	{
		std::string f ("Vessels/CollDebris.cfg"), v;
		return CfgString (f.c_str (), (int)CONFIG, "Size", v);
	}
	uint32_t TouchdownCount (CollH v) override { VESSEL *x = Ves (v); return x ? (uint32_t)x->GetTouchdownPointCount () : 0; }
	bool Touchdown (CollH v, uint32_t i, Vector &pos) override
	{
		VESSEL *x = Ves (v);
		TOUCHDOWNVTX t;
		if (!x || i >= x->GetTouchdownPointCount () || !x->GetTouchdownPoint (t, i)) return false;
		pos = V (t.pos);
		return true;
	}
	bool ThrusterPos (CollH v, CollH th, Vector &pos) override
	{
		VESSEL *x = Ves (v);
		if (!x || !th) return false;
		VECTOR3 p; x->GetThrusterRef ((THRUSTER_HANDLE)th, p); pos = V (p);
		return true;
	}
protected:
	int DoGroupFlag (CollH dm, uint32_t g, uint32_t flag, bool add) override
	{
		if (!dm) return -1;
		GROUPEDITSPEC ges; memset (&ges, 0, sizeof ges);
		ges.flags = add ? GRPEDIT_ADDUSERFLAG : GRPEDIT_DELUSERFLAG; ges.UsrFlag = flag;
		return oapiEditMeshGroup ((DEVMESHHANDLE)dm, g, &ges);
	}
	CollH DoVesselCreate (const char *name, const char *cls, const CollStateWrite &s) override
	{
		VESSELSTATUS2 vs; memset (&vs, 0, sizeof vs);
		vs.version = 2; vs.flag = 0;
		vs.rbody = H (s.rbody); vs.rpos = O (s.rpos); vs.rvel = O (s.rvel); vs.vrot = O (s.vrot); vs.arot = O (s.arot);
		vs.status = 0;
		std::string n (name ? name : ""), c (cls ? cls : "");
		return oapiCreateVesselEx (n.data (), c.data (), &vs);
	}
	bool DoDebrisSetup (CollH v, CollH mesh, const DebrisCaps &c) override
	{
		VESSEL *x = Ves (v);
		if (!x) return false;
		if (mesh) x->AddMesh ((MESHHANDLE)mesh);
		x->SetSize (c.size); x->SetEmptyMass (c.mass); x->SetPMI (O (c.pmi)); x->SetCrossSections (O (c.cs));
		if (c.td.size () >= 3) {
			std::vector<TOUCHDOWNVTX> td (c.td.size ());
			for (size_t i = 0; i < td.size (); i++) td[i].pos = O (c.td[i]), td[i].stiffness = c.tdK, td[i].damping = c.tdD, td[i].mu = c.mu, td[i].mu_lng = c.mu;
			x->SetTouchdownPoints (td.data (), (DWORD)td.size ());
		}
		return true;
	}
	bool DoVesselDelete (CollH v) override { return v && oapiIsVessel (H (v)) && oapiDeleteVessel (H (v)); }
public:
public: // dmg3 area F
	CollH FxAdd (CollH h, const FxSpec &s, const Vector &pos, const Vector &dir, double *lvl) override
	{
		VESSEL *v = Ves (h);
		if (!v) return nullptr;
		static char tFlake[] = "Contrail1a", tSpark[] = "Exhaust", tVent[] = "Contrail1", tDust[] = "Contrail4"; // 2024 takes char*
		char *tn = s.tex == FX_TEX_SPARK ? tSpark : s.tex == FX_TEX_VENT ? tVent : s.tex == FX_TEX_DUST ? tDust : tFlake;
		PARTICLESTREAMSPEC p;
		memset (&p, 0, sizeof p);
		p.flags = 0; p.srcsize = s.size; p.srcrate = s.rate; p.v0 = s.v0; p.srcspread = s.spread;
		p.lifetime = s.life; p.growthrate = s.grow; p.atmslowdown = s.slow;
		p.ltype = s.ltype == FX_EMISSIVE ? PARTICLESTREAMSPEC::EMISSIVE : PARTICLESTREAMSPEC::DIFFUSE;
		p.levelmap = s.lmap == FX_LVL_FLAT ? PARTICLESTREAMSPEC::LVL_FLAT : PARTICLESTREAMSPEC::LVL_LIN;
		p.lmin = s.lmin; p.lmax = s.lmax;
		p.atmsmap = PARTICLESTREAMSPEC::ATM_FLAT; p.amin = s.amin; p.amax = s.amax;
		p.tex = oapiRegisterParticleTexture (tn);
		return (CollH)v->AddParticleStream (&p, O (pos), O (dir), lvl);
	}
	bool FxDel (CollH h, CollH ps) override { VESSEL *v = Ves (h); return v && ps && v->DelExhaustStream ((PSTREAM_HANDLE)ps); }
	double FxAtm (CollH h) override { VESSEL *v = Ves (h); return v ? v->GetAtmDensity () : 0; }
	bool FxGround (CollH h, Vector &vLoc, Vector &upLoc, double &alt) override
	{
		vLoc = upLoc = Vector (); alt = 0;
		VESSEL *v = Ves (h);
		if (!v) return false;
		VECTOR3 gs, r;
		if (v->GetGroundspeedVector (FRAME_HORIZON, gs)) { v->HorizonInvRot (gs, r); vLoc = V (r); }
		v->HorizonInvRot (_V (0, 1, 0), r); upLoc = V (r);
		alt = v->GetAltitude (ALTMODE_GROUND);
		return v->GroundContact ();
	}
public: // bldg: streamed 3D buildings, only with a core that has them (as COLL_HAVE_IMGUI keys on OAPINOTIF_WARNING)
#ifdef OAPI_TILESTREAM_BLDG
	int BldgWant (CollH planet, int x, int y) override { return oapiTileStreamBldgWant (H (planet), x, y); }
	bool BldgStream (CollH planet) override { return oapiTileStreamEnabled (H (planet)); }
	bool BldgLoad (CollH planet, int x, int y, CollBldgTile &out) override
	{
		static_assert (sizeof (DWORD) == sizeof (uint32_t), "TSBLDGMESH indices are 32 bit");
		out = CollBldgTile ();
		TSBLDGMESH *m = oapiTileStreamBldgLoad (H (planet), x, y);
		if (!m) return false;
		try {
			out.b.resize (m->nb);
			for (DWORD i = 0; i < m->nb; i++) {
				const TSBLDG &s = m->b[i];
				out.b[i] = { s.lat, s.lng, s.hmin, s.hmax, (uint32_t)s.v0, (uint32_t)s.nv, (uint32_t)s.i0, (uint32_t)s.ni };
			}
		} catch (...) { oapiTileStreamBldgFree (m); out = CollBldgTile (); throw; }
		out.x = x; out.y = y; out.nv = (uint32_t)m->nv; out.ni = (uint32_t)m->ni;
		out.vtx = m->vtx; out.idx = (const uint32_t *)m->idx; out.handle = m;
		return true;
	}
	void BldgFree (CollBldgTile &t) override
	{
		if (t.handle) oapiTileStreamBldgFree ((TSBLDGMESH *)t.handle);
		t = CollBldgTile ();
	}
#endif
#ifdef COLL_BASE_DENT
	bool BaseDentOn () override { return oapiBaseSetDent && oapiBaseClearDents; }
	bool BaseSetDent (CollH base, uint32_t obj, uint32_t k, const DentRecord &r) override
	{
		if (!oapiBaseSetDent || !base) return false;
		BASEDENTSPEC d; memset (&d, 0, sizeof d);
		d.size = sizeof d; d.obj = obj; d.k = k; d.mode = r.p.mode; d.seed = r.p.seed; d.bits = r.p.bits; d.flags = r.flags;
		d.c = O (r.p.c); d.n = O (r.p.n); d.t = O (r.p.t);
		d.R = r.p.R; d.h = r.p.h; d.T = r.p.T; d.P = r.p.P; d.hd = r.p.hd; d.hz = r.p.hz;
		return oapiBaseSetDent (H (base), &d);
	}
	void BaseClearDents (CollH base, uint32_t obj) override { if (oapiBaseClearDents && base) oapiBaseClearDents (H (base), obj == ~0u ? (DWORD)-1 : (DWORD)obj); }
#endif
#ifdef COLL_BASE_CLEARZONES
	void BaseClearZones (CollH base, std::vector<CollClearZone> &out) override
	{
		out.clear ();
		if (!oapiGetBaseClearZones || !base) return;
		DWORD n = oapiGetBaseClearZones (H (base), nullptr, 0);
		if (!n) return;
		std::vector<BASECLEARZONE> z (n);
		n = std::min (n, oapiGetBaseClearZones (H (base), z.data (), n));
		for (DWORD i = 0; i < n; i++) out.push_back ({ z[i].lng, z[i].lat, z[i].rad });
	}
#endif
};
} // namespace

std::unique_ptr<CollSdk> CollSdkOrbiterCreate (bool processMode) { return std::make_unique<CollSdkOrbiter> (processMode); }

void CollSdkOrbiterBindClient (CollSdk &sdk)
{
	auto *s = dynamic_cast<CollSdkOrbiter *> (&sdk);
	if (!s || s->process) return;
	s->core = false; // gcCore needs gcCoreAPI.h, which the Coll_ object libraries do not see yet: ClientCheck stays off
}
