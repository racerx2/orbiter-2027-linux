// not upstream: collision addon, the one fake CollSdk for unit tests: scripted world, files in memory, call counts (design E2 1.5)
#ifndef __COLLFAKESDK_H
#define __COLLFAKESDK_H
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "CollSdk.h"
#include "CollGeom.h"
#include "CollAnimTest.h"

class CollFakeSdk final : public CollSdk {
public:
	enum { HOLE = 0, TPL = 1, NAME = 2 };
	struct Tpl { std::string name; bool anon = false; std::vector<CollGroupData> grp; };
	struct Slot { int kind = HOLE; const Tpl *tpl = nullptr; std::string name; Vector ofs; uint16_t mode = 1; };
	struct Tank { double max = 1, mass = 1; bool alive = true; };
	struct Ves {
		std::string name, cls; std::vector<Slot> slot; TestVessel anim; const void *visual = nullptr;
		std::vector<CollPortInfo> dock; std::vector<CollAttInfo> att[2]; const void *sv = nullptr;
		std::deque<Tank> tank; std::vector<Tank *> tankList; bool alive = true;
		CollVesselRead rd {};
	};
	struct Body { std::string name; int type = 4; double size = 6.371e6, mass = 5.97e24; std::vector<const Body *> base; double lng = 0, lat = 0;
		Vector pos; Matrix R = IMatrix (); double elev = 0; };
	explicit CollFakeSdk (bool imgui = false): CollSdk (imgui) {}
	// script
	std::deque<Ves> ves; std::vector<Ves *> list;
	std::deque<Tpl> tpls;
	std::deque<Body> bodies; std::vector<Body *> gbody;
	std::map<std::string, std::string> files;
	std::map<std::string, std::vector<std::string>> dirs;
	std::vector<std::string> log, calls;
	std::string annotation;
	int misuse = 0, noteCalls = 0, dialogs = 0, cmdNext = 1, annotationCalls = 0, forgets = 0;
	struct Wr { char op; CollH h; Vector a, b; Matrix R; CollStateWrite s; }; // op: S state, A attitude, W spin, F force
	std::vector<Wr> wr;                     // state-changing vessel calls in order
	bool applyWrites = false;               // apply them to rd; a vessel with sv and svcg moves as a stack component (SuperVessel.cpp:309-388)
	double simT = 0, mjd = 51544.5, sysT = 0, warp = 1;
	bool clientCore = false, ended = false;
	std::map<std::string, int> cfgInt;
	Ves *AddVessel (const std::string &name, const std::string &cls = "")
	{
		ves.emplace_back (); Ves *v = &ves.back (); v->name = name; v->cls = cls; list.push_back (v); return v;
	}
	void DelVessel (Ves *v) { v->alive = false; for (size_t i = 0; i < list.size (); i++) if (list[i] == v) { list.erase (list.begin () + i); break; } }
	const Tpl *AddTpl (const std::string &name, std::vector<CollGroupData> g) { tpls.push_back ({ name, name.empty (), std::move (g) }); return &tpls.back (); }
	static std::string Norm (const std::string &p)
	{
		std::string r;
		for (char c : p) r += (c == '\\') ? '/' : (char)tolower ((unsigned char)c);
		while (r.compare (0, 2, "./") == 0) r.erase (0, 2);
		std::string o;
		for (size_t i = 0; i < r.size (); i++) if (!(r[i] == '/' && i + 1 < r.size () && r[i+1] == '/')) o += r[i];
		while (o.size () > 1 && o.back () == '/') o.pop_back ();
		return o;
	}
	void File (const std::string &path, const std::string &text) { files[Norm (path)] = text; }
	void Bad (const char *what) { misuse++; log.push_back (std::string ("FAKE MISUSE ") + what); }
	Ves *V (CollH h) { Ves *v = (Ves *)h; if (!v || !v->alive || ended) { Bad ("vessel handle"); return nullptr; } return v; }
	// world and bodies
	uint32_t VesselCount () override { return (uint32_t)list.size (); }
	CollH Vessel (uint32_t i) override { return i < list.size () ? list[i] : nullptr; }
	bool IsVessel (CollH h) override { for (auto *v : list) if (v == h) return true; return false; }
	int ObjType (CollH h) override { if (IsVessel (h)) return 10; for (auto &b : bodies) if (&b == h) return b.type; return 0; }
	std::string Name (CollH h) override { if (IsVessel (h)) return ((Ves *)h)->name; for (auto &b : bodies) if (&b == h) return b.name; for (auto &v : ves) if (&v == h) return v.name; return ""; }
	std::string ClassName (CollH v) override { Ves *x = V (v); return x ? x->cls : ""; }
	double Size (CollH h) override { for (auto &b : bodies) if (&b == h) return b.size; return 10; }
	void GlobalState (CollH h, Vector &pos, Vector &vel, Matrix &R) override
	{
		vel = Vector ();
		for (auto &b : bodies) if (&b == h) { pos = b.pos; R = b.R; return; }
		if (Ves *v = V (h)) { pos = v->rd.x; R = v->rd.R; auto it = velE.find (v); if (it != velE.end ()) vel = it->second; }
	}
	uint32_t GbodyCount () override { return (uint32_t)gbody.size (); }
	CollH Gbody (uint32_t i) override { return i < gbody.size () ? gbody[i] : nullptr; }
	uint32_t BaseCount (CollH p) override { return (uint32_t)((const Body *)p)->base.size (); }
	CollH Base (CollH p, uint32_t i) override { auto *b = (const Body *)p; return i < b->base.size () ? b->base[i] : nullptr; }
	void BaseEquPos (CollH base, double &lng, double &lat, double &rad) override { auto *b = (const Body *)base; lng = b->lng; lat = b->lat; rad = 0; }
	int elevCalls = 0;
	double Mass (CollH h) override { for (auto &b : bodies) if (&b == h) return b.mass; Ves *v = V (h); return v ? v->rd.m : 0; }
	double SimTime () override { return simT; }
	double SimMJD () override { return mjd; }
	double SysTime () override { return sysT; }
	double Warp () override { return warp; }
	// vessel reads
	void ReadVessel (CollH v, CollVesselRead &out, uint32_t) override { if (Ves *x = V (v)) { out = x->rd; out.sv = x->sv; } }
	double EmptyMass (CollH v) override { Ves *x = V (v); if (!x) return 0; auto it = emptyMassE.find (x); return it != emptyMassE.end () ? it->second : x->rd.m; }
	bool Recording (CollH) override { return false; }
	bool Playback (CollH) override { return false; }
	int DamageModel (CollH) override { return damageModelE; }
	// meshes and animations
	uint32_t MeshCount (CollH v) override { Ves *x = V (v); calls.push_back ("MeshCount"); return x ? (uint32_t)x->slot.size () : 0; }
	CollH MeshTemplate (CollH v, uint32_t i) override { Ves *x = V (v); return (x && i < x->slot.size () && x->slot[i].kind == TPL) ? x->slot[i].tpl : nullptr; }
	const char *MeshName (CollH v, uint32_t i) override
	{
		Ves *x = V (v);
		if (!x || i >= x->slot.size () || x->slot[i].kind == HOLE) { Bad ("MeshName on a hole"); return ""; }
		return x->slot[i].kind == NAME ? x->slot[i].name.c_str () : x->slot[i].tpl->name.c_str ();
	}
	Vector MeshOffset (CollH v, uint32_t i) override { Ves *x = V (v); if (!x || i >= x->slot.size () || x->slot[i].kind == HOLE) { Bad ("MeshOffset on a hole"); return Vector (); } return x->slot[i].ofs; }
	uint16_t MeshVisMode (CollH v, uint32_t i) override { Ves *x = V (v); if (!x || i >= x->slot.size () || x->slot[i].kind == HOLE) { Bad ("MeshVisMode on a hole"); return 0; } return x->slot[i].mode; }
	const char *TplName (CollH tpl) override { auto *t = (const Tpl *)tpl; return t->anon ? nullptr : t->name.c_str (); }
	uint32_t TplGroups (CollH tpl) override { return (uint32_t)((const Tpl *)tpl)->grp.size (); }
	bool TplGroup (CollH tpl, uint32_t g, CollTplGroup &out) override
	{
		if (!tpl) { Bad ("TplGroup NULL"); return false; }
		auto *t = (const Tpl *)tpl;
		if (g >= t->grp.size ()) return false;
		const CollGroupData &d = t->grp[g];
		out = { d.vtx.data (), d.idx.data (), (uint32_t)d.vtx.size (), (uint32_t)d.idx.size (), d.usrflag };
		return true;
	}
	uint32_t Anims (CollH v, const ANIMATION **a) override { Ves *x = V (v); if (!x) { *a = nullptr; return 0; } *a = x->anim.anim; return x->anim.nanim; }
	// docks and attachments
	uint32_t DockCount (CollH v) override { Ves *x = V (v); return x ? (uint32_t)x->dock.size () : 0; }
	bool Dock (CollH v, uint32_t i, CollPortInfo &out) override { Ves *x = V (v); if (!x || i >= x->dock.size ()) return false; out = x->dock[i]; return true; }
	uint32_t AttachCount (CollH v, bool tp) override { Ves *x = V (v); return x ? (uint32_t)x->att[tp].size () : 0; }
	bool Attach (CollH v, bool tp, uint32_t i, CollAttInfo &out) override { Ves *x = V (v); if (!x || i >= x->att[tp].size ()) return false; out = x->att[tp][i]; return true; }
	// thrusters and tanks
	uint32_t ThrusterCount (CollH) override { return 0; }
	CollH Thruster (CollH, uint32_t) override { return nullptr; }
	CollH ThrusterTank (CollH, CollH) override { return nullptr; }
	uint32_t TankCount (CollH v) override { Ves *x = V (v); return x ? (uint32_t)x->tankList.size () : 0; }
	CollH Tank (CollH v, uint32_t i) override { Ves *x = V (v); return (x && i < x->tankList.size ()) ? x->tankList[i] : nullptr; }
	double TankMass (CollH, CollH tk) override { return TankDead (tk) ? 0 : ((const struct Tank *)tk)->mass; }
	double TankMaxMass (CollH, CollH tk) override { return TankDead (tk) ? 0 : ((const struct Tank *)tk)->max; }
	// visuals and the client
	CollH Visual (CollH v) override { Ves *x = V (v); return x ? x->visual : nullptr; }
	CollH DevMesh (CollH, CollH vis, uint32_t) override { if (!vis) Bad ("DevMesh NULL visual"); return nullptr; }
	int ReadVtx (CollH, uint32_t, const uint16_t *, uint32_t, DentVtx *) override { return -1; }
	bool ClientCore () override { return clientCore; }
	int ClientMatrix (int, CollH, uint32_t, uint32_t, float m[16]) override { for (int i = 0; i < 16; i++) m[i] = (i % 5) ? 0.0f : 1.0f; return clientCore ? 0 : -1; }
	// files, config, scenario, log
	std::string Resolve (const std::string &path) override { return path; }
	bool ReadText (const std::string &path, std::string &out) override { auto f = files.find (Norm (path)); if (f == files.end ()) return false; out = f->second; return true; }
	std::vector<std::string> ListDir (const std::string &dir) override { auto f = dirs.find (Norm (dir)); return f == dirs.end () ? std::vector<std::string> () : f->second; }
	bool CfgString (const char *, int, const char *, std::string &) override { return false; }
	bool CfgInt (const char *file, int, const char *item, int &val) override { auto f = cfgInt.find (std::string (file) + ":" + item); if (f == cfgInt.end ()) return false; val = f->second; return true; }
	bool CfgReal (const char *, int, const char *, double &) override { return false; }
	bool CfgBool (const char *, int, const char *, bool &) override { return false; }
	bool ScnLine (CollH, std::string &) override { return false; }
	void ScnWrite (CollH, const std::string &) override {}
	void Log (int, const char *msg) override { if (ended) Bad ("Log after EndSession"); log.push_back (msg); }
	int LogCount (const char *needle) const { int n = 0; for (auto &l : log) if (l.find (needle) != std::string::npos) n++; return n; }
protected:
	void DoSetState (CollH v, const CollStateWrite &st) override
	{
		wr.push_back ({ 'S', v, Vector (), Vector (), Matrix (), st });
		Ves *x = V (v);
		if (!x || !applyWrites) return;
		Vector xr, vr; Matrix Rr;
		if (st.rbody) GlobalState (st.rbody, xr, vr, Rr);
		Matrix R; R.Set (st.arot);
		Vector cg = xr + st.rpos + mul (x->rd.R, x->rd.svcg); // RPlace with the current rotation, then the orientation about the CG
		x->rd.x = cg - mul (R, x->rd.svcg); x->rd.R = R; x->rd.v = vr + st.rvel; x->rd.w = st.vrot;
	}
	void DoSetAttitude (CollH v, const Matrix &R) override
	{
		wr.push_back ({ 'A', v, Vector (), Vector (), R, CollStateWrite {} });
		Ves *x = V (v);
		if (!x || !applyWrites) return;
		Vector cg = x->rd.x + mul (x->rd.R, x->rd.svcg);
		x->rd.x = cg - mul (R, x->rd.svcg); x->rd.R = R;
	}
	void DoSetSpin (CollH v, const Vector &w) override
	{
		wr.push_back ({ 'W', v, w, Vector (), Matrix (), CollStateWrite {} });
		Ves *x = V (v);
		if (x && applyWrites) x->rd.w = w;
	}
	void DoAddForce (CollH v, const Vector &F, const Vector &r) override { wr.push_back ({ 'F', v, F, r, Matrix (), CollStateWrite {} }); }
	void DoSetTank (CollH, CollH, CollH tank) override { if (tank) TankDead (tank); }
	CollH DoCreateTank (CollH v, double maxMass, double mass) override { Ves *x = V (v); if (!x) return nullptr; x->tank.push_back ({ maxMass, mass, true }); x->tankList.push_back (&x->tank.back ()); return &x->tank.back (); }
	void DoDelTank (CollH v, CollH tank) override { Ves *x = V (v); if (!x || TankDead (tank)) return; for (size_t i = 0; i < x->tankList.size (); i++) if (x->tankList[i] == tank) { x->tankList.erase (x->tankList.begin () + i); break; } ((struct Tank *)tank)->alive = false; }
	void DoSetTankMass (CollH, CollH tank, double m) override { if (!TankDead (tank)) ((struct Tank *)tank)->mass = m; }
	void DoSetWarp (double w) override { warp = w; }
	int DoWriteVtx (CollH, uint32_t, const uint16_t *, uint32_t, const DentVtx *) override { return -1; }
	int DoSetClientMatrix (int, CollH, uint32_t, uint32_t, const float *) override { return -1; }
	bool DoProbe (CollH v, uint32_t i) override
	{
		Ves *x = V (v);
		if (!x || i >= x->slot.size () || x->slot[i].kind == HOLE) return false;
		x->slot[i].ofs += Vector (-0.0, -0.0, -0.0);
		return true;
	}
	bool DoNotify (CollH, int, void *, int &reply) override { reply = 0; return false; }
	void DoNotification (int, const char *, const char *) override { noteCalls++; }
	void DoAnnotation (const char *text) override { annotationCalls++; annotation = text; }
	void DoForgetAnnotation () override { forgets++; }
	int DoRegisterCmd (const char *, const char *, CollCmdFn, void *) override { return cmdNext++; }
	void DoUnregisterCmd (int) override {}
	bool DoOpenDialog (void *) override { dialogs++; return true; }
public: // fix2 area E
	std::map<const Ves *, double> emptyMassE;  // EmptyMass per vessel; absent: rd.m, as before
	std::map<const Ves *, Vector> velE;        // GlobalState velocity per vessel; absent: zero, as before
	int damageModelE = 0;                      // DamageModel for every vessel
	bool TankDead (CollH tk) { auto *t = (const struct Tank *)tk; if (t && t->alive) return false; Bad ("deleted tank"); return true; } // a deleted tank's handle is dead
public: // dmg3 area P
	struct MeshP { std::vector<CollGroupData> grp; bool freed = false; }; // private mesh copies of MeshLoad
	std::deque<MeshP> meshP; std::vector<std::string> callsP; std::map<const Ves *, DebrisCaps> debrisP;
	bool debrisCfgP = true;
	void SetEmptyMass (CollH v, double m) override { Ves *x = V (v); if (!x) return; double o = EmptyMass (v); emptyMassE[x] = m; x->rd.m += m - o; callsP.push_back ("SetEmptyMass"); } // total mass follows
	void SetPMI (CollH v, const Vector &p) override { Ves *x = V (v); if (x) x->rd.pmi = p; }
	CollH MeshLoad (const char *name) override
	{
		for (auto &t : tpls) if (name && t.name == name) { meshP.push_back ({ t.grp, false }); callsP.push_back ("MeshLoad"); return &meshP.back (); }
		return nullptr;
	}
	bool MeshEdit (CollH m, uint32_t g, uint32_t addFlag, const DentVtx *vtx, uint32_t n) override
	{
		auto *x = (MeshP *)m;
		if (!x || g >= x->grp.size ()) return false;
		x->grp[g].usrflag |= addFlag;
		for (uint32_t i = 0; vtx && i < n && i < x->grp[g].vtx.size (); i++) std::memcpy (&x->grp[g].vtx[i], (const char *)vtx + sizeof (CollVtx) * i, sizeof (CollVtx)); // DentVtx is incomplete here, same layout
		return true;
	}
	void MeshFree (CollH m) override { if (m) ((MeshP *)m)->freed = true; callsP.push_back ("MeshFree"); }
	bool DebrisClassExists () override { return debrisCfgP; }
protected:
	int DoGroupFlag (CollH, uint32_t g, uint32_t flag, bool add) override { callsP.push_back (std::string (add ? "Flag+" : "Flag-") + std::to_string (g) + ":" + std::to_string (flag)); return 0; }
	CollH DoVesselCreate (const char *name, const char *cls, const CollStateWrite &s) override { Ves *v = AddVessel (name, cls); v->rd.x = s.rpos; v->rd.v = s.rvel; v->rd.R = IMatrix (); callsP.push_back ("VesselCreate"); return v; }
	bool DoDebrisSetup (CollH v, CollH, const DebrisCaps &c) override { Ves *x = V (v); if (!x) return false; debrisP[x] = c; return true; }
	bool DoVesselDelete (CollH v) override { Ves *x = V (v); if (!x) return false; DelVessel (x); callsP.push_back ("VesselDelete"); return true; }
public:
public: // dmg3 area F
	struct FxS { CollH v; FxSpec s; Vector pos, dir; double *lvl; bool alive, detached; };
	struct GroundF { bool on = false; Vector vLoc, up = Vector (0, 1, 0); double alt = 0; };
	std::deque<FxS> fx;                        // every stream created, in order; handles are addresses into it
	bool fxNull = false;                       // FxAdd returns NULL (headless, EnableParticleStreams off)
	std::map<const Ves *, double> atmF;        // FxAtm per vessel; absent: 0
	std::map<const Ves *, GroundF> groundF;    // FxGround per vessel; absent: no contact
	int fxAdds = 0, fxDels = 0, fxReads = 0;
	CollH FxAdd (CollH v, const FxSpec &s, const Vector &pos, const Vector &dir, double *lvl) override
	{
		fxAdds++;
		if (!V (v) || fxNull) return nullptr;
		fx.push_back ({ v, s, pos, dir, lvl, true, false });
		return &fx.back ();
	}
	bool FxDel (CollH v, CollH ps) override
	{
		fxDels++;
		if (!V (v)) return false;
		for (auto &f : fx) if (&f == ps) {
			if (f.detached) return false;  // the core compares pointers first: safe, no misuse
			if (!f.alive || f.v != v) { Bad ("FxDel dead or foreign stream"); return false; }
			f.alive = false; f.lvl = nullptr; return true;
		}
		Bad ("FxDel unknown stream"); return false;
	}
	double FxAtm (CollH v) override { fxReads++; auto it = atmF.find ((const Ves *)v); return it == atmF.end () ? 0 : it->second; }
	bool FxGround (CollH v, Vector &vLoc, Vector &upLoc, double &alt) override
	{
		fxReads++;
		auto it = groundF.find ((const Ves *)v);
		GroundF g = it == groundF.end () ? GroundF () : it->second;
		vLoc = g.vLoc; upLoc = g.up; alt = g.alt; return g.on;
	}
	void FxDetach (CollH v) { for (auto &f : fx) if (f.v == v && f.alive) { f.alive = false; f.detached = true; f.lvl = nullptr; } } // vessel destroyed or ClearThrusterDefinitions
	size_t FxLive () const { size_t n = 0; for (auto &f : fx) n += f.alive; return n; }
public: // ground area G
	std::function<double (double lng, double lat)> elevG; // terrain by position; empty: the body's elev
	double periodG = 86164;                    // PlanetPeriod of every body; 0: not rotating
	double Elevation (CollH planet, double lng, double lat) override { elevCalls++; return elevG ? elevG (lng, lat) : ((const Body *)planet)->elev; }
	double PlanetPeriod (CollH) override { return periodG; }
};
#endif
