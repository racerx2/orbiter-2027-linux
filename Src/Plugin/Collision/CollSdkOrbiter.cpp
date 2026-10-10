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

static_assert (sizeof (NTVERTEX) == 32, "NTVERTEX must keep the CollVtx layout");

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
	CollH MeshLoad (const char *name) override
	{
		std::string n (name ? name : "");
		if (n.empty ()) return nullptr;
		MESHHANDLE m = oapiLoadMesh (n.data ());
		if (m && !oapiMeshGroupCount (m)) { oapiDeleteMesh (m); m = nullptr; } // a missing file loads as an empty mesh
		return (CollH)m;
	}
	bool MeshEdit (CollH mesh, uint32_t g, uint32_t addFlag, const DentVtx *vtx, uint32_t n) override
	{
		if (!mesh) return false;
		GROUPEDITSPEC ges; memset (&ges, 0, sizeof ges);
		if (addFlag) ges.flags |= GRPEDIT_ADDUSERFLAG, ges.UsrFlag = addFlag;
		if (vtx && n) ges.flags |= GRPEDIT_VTXCRD | GRPEDIT_VTXNML, ges.Vtx = (NTVERTEX *)vtx, ges.nVtx = n;
		return oapiEditMeshGroup ((MESHHANDLE)mesh, g, &ges) == 0;
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
};
} // namespace

std::unique_ptr<CollSdk> CollSdkOrbiterCreate (bool processMode) { return std::make_unique<CollSdkOrbiter> (processMode); }

void CollSdkOrbiterBindClient (CollSdk &sdk)
{
	auto *s = dynamic_cast<CollSdkOrbiter *> (&sdk);
	if (!s || s->process) return;
	s->core = false; // gcCore needs gcCoreAPI.h, which the Coll_ object libraries do not see yet: ClientCheck stays off
}
