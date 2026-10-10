// not upstream: dmg3 area P unit tests: pieces, gates, glass, interior, re-apply, repair, debris spawn, caps, load rebuild, playback, idle (design-CA-dmg3-P 9)
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "CollBreakA.h"
#include "CollDamageA.h"
#include "CollShape.h"
#include "CollStore.h"
#include "CollAnimTest.h"

namespace {

struct MeshT { std::string name; std::vector<CollGroupData> grp; bool freed = false, copy = false; };

class BFake final : public CollSdk {
public:
	struct V { std::string name, cls; std::vector<MeshT *> slot; Vector ofs; bool playback = false, alive = true; int visual = 0; CollVesselRead rd {}; double empty = 500, size = 1; std::vector<Vector> dock; };
	BFake () : CollSdk (false) {}
	std::deque<V> ves; std::vector<V *> list; std::deque<MeshT> mesh;
	std::vector<std::string> log, calls; std::map<std::string, std::string> files;
	bool cfgDebris = true; double simt = 0;
	std::function<void (CollH)> onCreate;
	V *Add (const std::string &n, const std::string &c) { ves.emplace_back (); V *v = &ves.back (); v->name = n, v->cls = c; v->rd.R = IMatrix (); v->rd.m = 500; list.push_back (v); return v; }
	static V *X (CollH h) { return (V *)h; }
	int Calls (const std::string &c) const { int n = 0; for (auto &x : calls) if (x == c) n++; return n; }
	bool Logged (const std::string &s) const { for (auto &l : log) if (l.find (s) != std::string::npos) return true; return false; }
	int Logs (const std::string &s) const { int n = 0; for (auto &l : log) if (l.find (s) != std::string::npos) n++; return n; }
	uint32_t VesselCount () override { return (uint32_t)list.size (); }
	CollH Vessel (uint32_t i) override { return i < list.size () ? list[i] : nullptr; }
	bool IsVessel (CollH h) override { for (V *v : list) if (v == h) return true; return false; }
	int ObjType (CollH) override { return 10; }
	std::string Name (CollH h) override { return h ? X (h)->name : ""; }
	std::string ClassName (CollH h) override { return h ? X (h)->cls : ""; }
	double Size (CollH h) override { return X (h)->size; }
	void GlobalState (CollH h, Vector &p, Vector &v, Matrix &R) override { p = X (h)->rd.x; v = X (h)->rd.v; R = X (h)->rd.R; }
	uint32_t GbodyCount () override { return 0; }
	CollH Gbody (uint32_t) override { return nullptr; }
	uint32_t BaseCount (CollH) override { return 0; }
	CollH Base (CollH, uint32_t) override { return nullptr; }
	void BaseEquPos (CollH, double &, double &, double &) override {}
	double Elevation (CollH, double, double) override { return 0; }
	double PlanetPeriod (CollH) override { return 0; }
	double Mass (CollH h) override { return X (h)->rd.m; }
	double SimTime () override { return simt; }
	double SimMJD () override { return 0; }
	double SysTime () override { return simt; }
	double Warp () override { return 1; }
	void ReadVessel (CollH h, CollVesselRead &o, uint32_t) override { o = X (h)->rd; }
	double EmptyMass (CollH h) override { return X (h)->empty; }
	void SetEmptyMass (CollH h, double m) override { V *x = X (h); x->rd.m += m - x->empty; x->empty = m; } // total mass follows
	void SetPMI (CollH h, const Vector &p) override { X (h)->rd.pmi = p; }
	bool Recording (CollH) override { return false; }
	bool Playback (CollH h) override { return X (h)->playback; }
	int DamageModel (CollH) override { return 1; }
	uint32_t MeshCount (CollH h) override { return (uint32_t)X (h)->slot.size (); }
	CollH MeshTemplate (CollH h, uint32_t i) override { return i < X (h)->slot.size () ? X (h)->slot[i] : nullptr; }
	const char *MeshName (CollH h, uint32_t i) override { return X (h)->slot[i]->name.c_str (); }
	Vector MeshOffset (CollH h, uint32_t) override { return X (h)->ofs; }
	uint16_t MeshVisMode (CollH, uint32_t) override { return 1; }
	const char *TplName (CollH t) override { return ((MeshT *)t)->name.c_str (); }
	uint32_t TplGroups (CollH t) override { return (uint32_t)((MeshT *)t)->grp.size (); }
	bool TplGroup (CollH t, uint32_t g, CollTplGroup &o) override
	{
		auto *m = (MeshT *)t;
		if (g >= m->grp.size ()) return false;
		auto &d = m->grp[g];
		o = { d.vtx.data (), d.idx.data (), (uint32_t)d.vtx.size (), (uint32_t)d.idx.size (), d.usrflag };
		return true;
	}
	uint32_t Anims (CollH, const ANIMATION **a) override { *a = nullptr; return 0; }
	uint32_t DockCount (CollH h) override { return (uint32_t)X (h)->dock.size (); }
	bool Dock (CollH h, uint32_t i, CollPortInfo &p) override { if (i >= X (h)->dock.size ()) return false; p = CollPortInfo {}; p.pos = X (h)->dock[i]; return true; }
	uint32_t AttachCount (CollH, bool) override { return 0; }
	bool Attach (CollH, bool, uint32_t, CollAttInfo &) override { return false; }
	uint32_t ThrusterCount (CollH) override { return 0; }
	CollH Thruster (CollH, uint32_t) override { return nullptr; }
	CollH ThrusterTank (CollH, CollH) override { return nullptr; }
	uint32_t TankCount (CollH) override { return 0; }
	CollH Tank (CollH, uint32_t) override { return nullptr; }
	double TankMass (CollH, CollH) override { return 0; }
	double TankMaxMass (CollH, CollH) override { return 0; }
	CollH Visual (CollH h) override { return X (h)->visual ? (CollH)((char *)X (h) + X (h)->visual) : nullptr; }
	CollH DevMesh (CollH h, CollH, uint32_t i) override { return (CollH)((char *)X (h) + 1000 + i); }
	int ReadVtx (CollH, uint32_t, const uint16_t *, uint32_t, DentVtx *) override { return -1; }
	bool ClientCore () override { return false; }
	int ClientMatrix (int, CollH, uint32_t, uint32_t, float *) override { return -1; }
	std::string Resolve (const std::string &p) override { return p; }
	bool ReadText (const std::string &p, std::string &out) override { auto it = files.find (p); if (it == files.end ()) return false; out = it->second; return true; }
	std::vector<std::string> ListDir (const std::string &) override { return {}; }
	bool CfgString (const char *, int, const char *, std::string &) override { return false; }
	bool CfgInt (const char *, int, const char *, int &) override { return false; }
	bool CfgReal (const char *, int, const char *, double &) override { return false; }
	bool CfgBool (const char *, int, const char *, bool &) override { return false; }
	bool ScnLine (CollH, std::string &) override { return false; }
	void ScnWrite (CollH, const std::string &) override {}
	void Log (int, const char *m) override { log.push_back (m); }
	// dmg3 area P
	CollH MeshLoad (const char *name) override
	{
		for (auto &m : mesh) if (!m.copy && m.name == name) { mesh.push_back (m); mesh.back ().copy = true; calls.push_back ("MeshLoad"); return &mesh.back (); }
		return nullptr;
	}
	bool MeshEdit (CollH h, uint32_t g, uint32_t add, const DentVtx *vtx, uint32_t n) override
	{
		auto *m = (MeshT *)h;
		if (g >= m->grp.size ()) return false;
		calls.push_back ("MeshEdit");
		m->grp[g].usrflag |= add;
		for (uint32_t i = 0; vtx && i < n; i++) std::memcpy (&m->grp[g].vtx[i], &vtx[i], sizeof (CollVtx));
		return true;
	}
	void MeshFree (CollH h) override { ((MeshT *)h)->freed = true; calls.push_back ("MeshFree"); }
	bool DebrisClassExists () override { return cfgDebris; }
	std::map<const V *, DebrisCaps> caps; std::map<const V *, MeshT *> debrisMesh; std::vector<std::pair<uint32_t, bool>> flags; std::map<const V *, CollStateWrite> created;
protected:
	void DoSetState (CollH h, const CollStateWrite &st) override { states.push_back ({ X (h), st }); }
	void DoSetAttitude (CollH, const Matrix &) override {}
	void DoSetSpin (CollH h, const Vector &w) override { spins[X (h)] = w; }
	void DoAddForce (CollH h, const Vector &F, const Vector &r) override { forces.push_back ({ X (h), { F, r } }); }
public:
	std::vector<std::pair<const V *, CollStateWrite>> states; std::map<const V *, Vector> spins; std::vector<std::pair<const V *, std::pair<Vector, Vector>>> forces; // blast: SetSpin and AddForce calls
protected:
	void DoSetTank (CollH, CollH, CollH) override {}
	CollH DoCreateTank (CollH, double, double) override { return nullptr; }
	void DoDelTank (CollH, CollH) override {}
	void DoSetTankMass (CollH, CollH, double) override {}
	void DoSetWarp (double) override {}
	int DoWriteVtx (CollH, uint32_t, const uint16_t *, uint32_t, const DentVtx *) override { return 0; }
	int DoSetClientMatrix (int, CollH, uint32_t, uint32_t, const float *) override { return -1; }
	bool DoProbe (CollH h, uint32_t i) override { return i < X (h)->slot.size (); }
	bool DoNotify (CollH, int, void *, int &) override { return false; }
	void DoNotification (int, const char *, const char *) override {}
	void DoAnnotation (const char *) override {}
	int DoRegisterCmd (const char *, const char *, CollCmdFn, void *) override { return 1; }
	void DoUnregisterCmd (int) override {}
	bool DoOpenDialog (void *) override { return false; }
	int DoGroupFlag (CollH, uint32_t g, uint32_t, bool add) override { flags.push_back ({ g, add }); return 0; }
	CollH DoVesselCreate (const char *name, const char *cls, const CollStateWrite &s) override
	{
		V *v = Add (name, cls); created[v] = s; calls.push_back ("VesselCreate");
		if (onCreate) onCreate (v);
		return v;
	}
	bool DoDebrisSetup (CollH h, CollH m, const DebrisCaps &c) override { calls.push_back ("DebrisSetup"); caps[X (h)] = c; debrisMesh[X (h)] = (MeshT *)m; return true; }
	bool DoVesselDelete (CollH h) override
	{
		V *v = X (h); v->alive = false; calls.push_back ("VesselDelete");
		for (size_t i = 0; i < list.size (); i++) if (list[i] == v) { list.erase (list.begin () + (long)i); break; }
		return true;
	}
};

class BHost final : public CollDmgHost {
public:
	std::vector<CollH> ids; std::map<uint32_t, CollShape *> shape;
	CollH Vessel (uint32_t id) override { return id < ids.size () && ids[id] && BFake::X (ids[id])->alive ? ids[id] : nullptr; }
	uint32_t IdOf (CollH h) override { for (size_t i = 0; i < ids.size (); i++) if (ids[i] == h) return (uint32_t)i; ids.push_back (h); return (uint32_t)ids.size () - 1; }
	CollShape *Shape (uint32_t id) override { auto it = shape.find (id); return it == shape.end () ? nullptr : it->second; }
	uint32_t SlotCount (uint32_t) override { return 0; }
	bool Slot (uint32_t, uint32_t, CollDmgSlot &) override { return false; }
	bool SlotNow (uint32_t, uint32_t, uint32_t &) override { return false; }
	void ClientMeshRebuilt (uint32_t, uint32_t) override {}
	void WantSlots (uint32_t, bool) override {}
	double DestroyEnergy (uint32_t) override { return -1; }
	bool BaseObject (int, int, int, CollDmgBaseObj &) override { return false; }
	void Bases (std::vector<CollDmgBaseObj> &) override {}
};

void Quad (CollGroupData &g, double x0, double y0, double z, double w, int n) // n x n cells, w wide, at z, outward +z
{
	for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) g.vtx.push_back (CollVtx { (float)(x0 + w * i / n), (float)(y0 + w * j / n), (float)z, 0, 0, 1, 0, 0 });
	for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
		uint16_t a = (uint16_t)(j * (n + 1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + n + 1), d = (uint16_t)(c + 1);
		g.idx.insert (g.idx.end (), { a, b, c, b, d, c });
	}
}

// slot "ship": 0 body 10 x 10, 1 nose 1 x 1 at z 0.2 (x 5.5), 2 glass 1 x 1 (x 2), 3 interior (x -3, no collider)
MeshT Ship ()
{
	MeshT m; m.name = "ship"; m.grp.resize (4);
	Quad (m.grp[0], -5, -5, 0, 10, 10);
	Quad (m.grp[1], 5.5, -0.5, 0.2, 1, 4);
	Quad (m.grp[2], 2, -0.5, 0.3, 1, 4);
	Quad (m.grp[3], -3, -0.5, -0.3, 1, 4);
	return m;
}

const DentMaterial kMat { "test", 1e6, 0.01, 1, 0, 0, 0.5, false };

struct Rig {
	BFake sdk; BHost host; CollCfgValues cfg; std::unique_ptr<CollDmgSession> s;
	struct Body { BFake::V *v; uint32_t id; std::unique_ptr<CollShape> sh; CollAnim ca; CollMeshInfo mi; };
	std::deque<Body> body; CollTemplateCache cache;
	Rig () { cfg.blast = false; sdk.mesh.push_back (Ship ()); sdk.files["Meshes/ship.col"] = "COLLIDER-V1\nMAT glass GROUP 2\nEXCLUDE GROUP 3\n"; sdk.onCreate = [this] (CollH h) { host.IdOf (h); }; }
	CollDmgSession &S () { if (!s) s.reset (new CollDmgSession (sdk, host, cfg)); return *s; }
	CollBreakA &B () { return *(CollBreakA *)S ().brk.get (); }
	uint32_t Add (const std::string &name)
	{
		body.emplace_back ();
		Body &b = body.back ();
		b.v = sdk.Add (name, "ShuttlePB"); b.v->slot = { &sdk.mesh.front () }; b.v->visual = 1;
		b.id = host.IdOf (b.v);
		auto rm = std::make_shared<CollRestMesh> (); rm->name = "ship"; rm->grp = sdk.mesh.front ().grp;
		for (auto &g : rm->grp) rm->nvtx += (uint32_t)g.vtx.size ();
		auto sc = std::make_shared<CollSidecar> (); std::vector<std::string> w;
		const std::string &t = sdk.files["Meshes/ship.col"];
		REQUIRE (CollParseSidecar (t.data (), t.size (), "ship.col", *sc, w));
		b.mi.present = b.mi.collide = true; b.mi.serial = 1; b.mi.key = "ship"; b.mi.rest = rm; b.mi.side = sc;
		b.sh.reset (new CollShape ());
		b.sh->Update (&b.mi, 1, b.ca, nullptr, 0, cache);
		host.shape[b.id] = b.sh.get ();
		S ().brk->Shapes (b.id, b.sh.get ());
		return b.id;
	}
	CollDamageHit H (uint32_t id, int grp, double vn, double ratio, double depth = 0)
	{
		CollDamageHit h;
		h.id = id; h.h = host.Vessel (id); h.mesh = 0; h.grp = grp; h.rec = 0;
		const MeshT &m = sdk.mesh.front ();
		Vector c; for (auto &v : m.grp[grp].vtx) c += Vector (v.x, v.y, v.z); c /= (double)m.grp[grp].vtx.size ();
		h.c = c; h.n = Vector (0, 0, 1); h.R = 1; h.vn = vn; h.depth = depth; h.mat = &kMat; h.simt = sdk.simt;
		h.Esurplus = ratio * 0.5 * std::sqrt (0.5) * kMat.sigma_c * 1.0; // u = ratio * D_eff on a 1 m^2 piece of r 0.707
		return h;
	}
};

}

TEST_CASE ("P1 pieces: body fixed, nose part, glass and interior tiers", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	const CollSlotA *sl = r.B ().Slot (a, 0);
	REQUIRE (sl); REQUIRE (sl->ok); REQUIRE (sl->piece.size () == 4);
	auto pc = [&] (int g) { return sl->piece[sl->pieceOf[g]]; };
	CHECK (pc (0).fixed);
	CHECK (!pc (1).fixed); CHECK (pc (1).tier == CBRK_PART);
	CHECK (pc (2).tier == CBRK_GLASS); CHECK (!pc (2).fixed);
	CHECK (pc (3).tier == CBRK_INTERIOR);
}

TEST_CASE ("P2 crush ratio 0.55 keeps, 0.65 breaks; 15 m/s never breaks", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	r.B ().Hit (r.H (a, 1, 30, 0.55));
	CHECK (!r.B ().Hidden (a, 0, 1));
	r.B ().Hit (r.H (a, 1, 15, 0.9));
	CHECK (!r.B ().Hidden (a, 0, 1));
	r.B ().Hit (r.H (a, 1, 30, 0.65));
	CHECK (r.B ().Hidden (a, 0, 1));
	REQUIRE (r.S ().Damage (a));
	REQUIRE (r.S ().Damage (a)->d.torn.size () == 1);
	CHECK (r.S ().Damage (a)->d.torn[0].grp == std::vector<uint16_t> { 1 });
	CHECK (r.sdk.flags.empty ());                           // client flags only in the post-step pass
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	bool flagged = false; for (auto &f : r.sdk.flags) if (f.first == 1 && f.second) flagged = true;
	CHECK (flagged);
	CHECK (r.sdk.Logged ("Collision break 'A' kind=0"));
}

TEST_CASE ("P3 glass: 5 mm intact, 3 cm hidden, no debris", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	r.B ().Hit (r.H (a, 2, 5, 0, 0.005));
	CHECK (!r.B ().Hidden (a, 0, 2));
	r.B ().Hit (r.H (a, 2, 5, 0, 0.03));
	CHECK (r.B ().Hidden (a, 0, 2));
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	CHECK (r.sdk.Calls ("VesselCreate") == 0);
}

TEST_CASE ("P4 interior hidden on destructive hits only", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	CollDamageHit h = r.H (a, 3, 10, 0); h.Esurplus = 0;
	r.B ().Hit (h);
	CHECK (!r.B ().Hidden (a, 0, 3));
	h.vn = 30; h.grp = 0; h.Esurplus = 0; h.depth = 0.5;
	// no record: crush 0, so even 30 m/s keeps it; a surplus on the own group counts
	r.B ().Hit (h);
	CHECK (!r.B ().Hidden (a, 0, 3));
	h.grp = 3; h.Esurplus = 1e6;
	r.B ().Hit (h);
	CHECK (r.B ().Hidden (a, 0, 3));
}

TEST_CASE ("P5 re-assert on every post and pass, separate counter, new visual", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	r.B ().Hit (r.H (a, 1, 30, 0.9));
	uint64_t vtx = r.sdk.Count ().n[CSK_VTX], f0 = r.sdk.flagCalls;
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	CHECK (r.sdk.flagCalls > f0);
	uint64_t f1 = r.sdk.flagCalls;
	r.B ().Pass ();
	CHECK (r.sdk.flagCalls > f1);
	r.body.front ().v->visual = 2;
	uint64_t f2 = r.sdk.flagCalls;
	r.B ().PreStep (0.1, 0.01); r.B ().Post (0.1, 0.01);
	CHECK (r.sdk.flagCalls > f2);
	CHECK (r.sdk.Count ().n[CSK_VTX] == vtx);
	CollShape fresh; CollTemplateCache cache;
	fresh.Update (&r.body.front ().mi, 1, r.body.front ().ca, nullptr, 0, cache);
	r.B ().Shapes (a, &fresh);
	CHECK (r.B ().Hidden (a, 0, 1));
}

TEST_CASE ("P6 repair: delete only flags P added, collider unhidden", "[dmg3P]")
{
	Rig r;
	r.sdk.mesh.front ().grp[3].usrflag = 2;
	uint32_t a = r.Add ("A");
	r.B ().Hit (r.H (a, 1, 30, 0.9));
	CollDamageHit h = r.H (a, 3, 30, 0); h.Esurplus = 1e6;
	r.B ().Hit (h);
	CHECK (!r.B ().Hidden (a, 0, 3));                      // dmg3 tear: hidden at rest: never broken
	r.sdk.flags.clear ();
	r.B ().Repair (a);
	bool del1 = false, del3 = false;
	for (auto &f : r.sdk.flags) { if (f.first == 1 && !f.second) del1 = true; if (f.first == 3 && !f.second) del3 = true; }
	CHECK (del1); CHECK (!del3);
	CHECK (!r.B ().Hidden (a, 0, 3));
	CHECK (!r.B ().Hidden (a, 0, 1));
}

TEST_CASE ("P7 debris: spawn in post, template flags, vertices, kick, cap, mesh free after delete", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A"); uint32_t b = r.Add ("B");
	CollDamageHit h = r.H (a, 1, 70, 0.9); h.other = b; h.vt = 0;
	r.B ().Hit (h);
	CHECK (r.sdk.Calls ("VesselCreate") == 0);
	CHECK (r.B ().Pending () == 1);
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	REQUIRE (r.sdk.Calls ("VesselCreate") == 1);
	REQUIRE (r.B ().Debris ().size () == 1);
	const CollDebrisA d = r.B ().Debris ()[0];                        // a copy: the cap below erases it from the list
	BFake::V *dv = BFake::X (d.h);
	CHECK (dv->name == "A_D1"); CHECK (dv->cls == "CollDebris");
	MeshT *m = r.sdk.debrisMesh[dv];
	REQUIRE (m);
	CHECK ((m->grp[0].usrflag & 2)); CHECK ((m->grp[2].usrflag & 2)); CHECK ((m->grp[3].usrflag & 2)); CHECK (!(m->grp[1].usrflag & 2));
	std::vector<DentVtx> rest (r.sdk.mesh.front ().grp[1].vtx.size ());
	std::memcpy (rest.data (), r.sdk.mesh.front ().grp[1].vtx.data (), rest.size () * sizeof (DentVtx));
	std::vector<DentVtx> pv = CollBreakA::PieceVertices (rest, 1, d.row.pose[0], d.row.rec);
	CHECK (std::memcmp (pv.data (), m->grp[1].vtx.data (), pv.size () * sizeof (DentVtx)) == 0);
	CollStateWrite st = r.sdk.created[dv];
	CHECK ((st.rvel & Vector (0, 0, 1)) <= 1e-9);           // never toward the impactor
	CHECK (st.rvel.length () > 0.8 * BRK_KICK * 70 - 1e-9);
	CHECK (std::fabs (st.rpos.x - 6.0) < 1e-6);             // nose centroid
	CHECK (r.sdk.caps[dv].mass >= 1); CHECK (r.sdk.caps[dv].mass <= BRK_MASS_MAX * 500 + 1e-9);
	CHECK (r.S ().noPair.count ({ std::min (a, d.id), std::max (a, d.id) }));
	CHECK (r.S ().noPair.count ({ std::min (b, d.id), std::max (b, d.id) }));
	REQUIRE (r.S ().Damage (a)->d.debris.size () == 1);
	CHECK (r.S ().Damage (a)->d.torn[0].debris == "A_D1");
	// cap: debrisMax + 1 debris delete the oldest
	r.cfg.debrisMax = 2;
	std::vector<uint32_t> more;
	for (int i = 0; i < 2; i++) { uint32_t c = r.Add ("C" + std::to_string (i)); r.B ().Hit (r.H (c, 1, 70, 0.9)); }
	r.sdk.simt = 1; r.B ().PreStep (1, 0.01); r.B ().Post (1, 0.01);
	CHECK (r.B ().Debris ().size () == 2);
	CHECK (r.sdk.Calls ("VesselDelete") == 1);
	CHECK (!dv->alive);
	CHECK (!m->freed);
	r.B ().DropVessel (d.id, d.h);
	r.B ().PreStep (1.1, 0.01); r.B ().Post (1.1, 0.01);
	CHECK (m->freed);
	r.B ().End ();
	for (auto &x : r.sdk.mesh) if (x.copy) CHECK (x.freed);
}

TEST_CASE ("P8 missing CollDebris.cfg: hide only, one log line", "[dmg3P]")
{
	Rig r; r.sdk.cfgDebris = false; uint32_t a = r.Add ("A"), b = r.Add ("B");
	r.B ().Hit (r.H (a, 1, 70, 0.9)); r.B ().Hit (r.H (b, 1, 70, 0.9));
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	CHECK (r.B ().Hidden (a, 0, 1));
	CHECK (r.sdk.Calls ("VesselCreate") == 0);
	CHECK (r.sdk.Logs ("CollDebris.cfg missing") == 1);
}

TEST_CASE ("P9 load: torn rows adopted, debris rebuilt bitwise, orphans deleted", "[dmg3P]")
{
	std::vector<DentTorn> torn; std::vector<DentDebris> rows; uint32_t fnv = 0; std::vector<CollVtx> live;
	{
		Rig r; uint32_t a = r.Add ("A");
		r.B ().Hit (r.H (a, 1, 70, 0.9)); r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
		REQUIRE (r.B ().Debris ().size () == 1);
		fnv = r.B ().Debris ()[0].fnv;
		live = r.sdk.debrisMesh[BFake::X (r.B ().Debris ()[0].h)]->grp[1].vtx;
		torn = r.S ().Damage (a)->d.torn; rows = r.S ().Damage (a)->d.debris;
		r.B ().End ();
	}
	Rig r; uint32_t a = r.Add ("A");
	BFake::V *dv = r.sdk.Add ("A_D1", "CollDebris"); r.host.IdOf (dv);
	BFake::V *orphan = r.sdk.Add ("Z_D1", "CollDebris"); r.host.IdOf (orphan);
	for (auto &t : torn) r.S ().AddTorn (a, t);
	r.S ().SetDebris (a, rows);
	r.B ().PreStep (3, 0.01); r.B ().Post (3, 0.01);
	CHECK (r.B ().Hidden (a, 0, 1));
	REQUIRE (r.B ().Debris ().size () == 1);
	CHECK (r.B ().Debris ()[0].fnv == fnv);
	CHECK (std::memcmp (live.data (), r.sdk.debrisMesh[dv]->grp[1].vtx.data (), live.size () * sizeof (CollVtx)) == 0);
	CHECK (r.sdk.Logged ("Collision break restored 'A_D1'"));
	CHECK (!orphan->alive); CHECK (dv->alive);
	// missing mesh deletes the debris
	Rig q; uint32_t qa = q.Add ("A");
	q.sdk.mesh.front ().grp.pop_back ();
	BFake::V *qd = q.sdk.Add ("A_D1", "CollDebris"); q.host.IdOf (qd);
	q.S ().SetDebris (qa, rows);
	q.B ().PreStep (3, 0.01); q.B ().Post (3, 0.01);
	CHECK (!qd->alive);
	CHECK (q.B ().Debris ().empty ());
}

TEST_CASE ("P10 playback T: hide at t, debris spawned live, no parent write", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	r.body.front ().v->playback = true;
	CollDamageHit h = r.H (a, 1, 70, 0.9); h.playback = true;
	r.B ().Hit (h);
	CHECK (!r.B ().Hidden (a, 0, 1));
	DentTorn t; t.kind = CBRK_PART; t.slot = 0; t.key = DentMath::MeshKey ("ship"); t.ngrp = 4; t.debris = "A_D1"; t.grp = { 1 };
	uint64_t w0 = r.sdk.Count ().n[CSK_STATE];
	r.B ().Torn (a, t);
	CHECK (r.B ().Hidden (a, 0, 1));
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	CHECK (r.sdk.Calls ("VesselCreate") == 1);
	CHECK (r.sdk.Count ().n[CSK_STATE] == w0 + 2);      // create and setup of the debris only
}

TEST_CASE ("P11 idle: no hit, no P call", "[dmg3P]")
{
	Rig r; r.Add ("A"); r.Add ("B");
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01); r.B ().Pass (); r.B ().PreStep (1, 0.01); r.B ().Post (1, 0.01);
	CHECK (r.sdk.flagCalls == 0);
	CHECK (r.sdk.Count ().Writes () == 0);
	CHECK (r.sdk.calls.empty ());
}

TEST_CASE ("P12 a hit on a CollDebris vessel breaks nothing", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	r.body.front ().v->cls = "colldebris";
	r.B ().Hit (r.H (a, 1, 70, 0.9));
	CHECK (!r.B ().Hidden (a, 0, 1));
	CHECK (r.S ().Damage (a) == nullptr);
}

// dmg3 tear (design-CA-dmg3-tear 7, tests 8-13): a 10 m strip of ten 1 m groups along x, nose at x = 5
namespace {
MeshT Fuse ()
{
	MeshT m; m.name = "ship"; m.grp.resize (10);
	for (int k = 0; k < 10; k++) Quad (m.grp[k], k - 5.0, -0.5, 0, 1, 4);
	return m;
}
struct TearRig : Rig {
	uint32_t a = 0;
	explicit TearRig (bool fuse = true) { if (fuse) { sdk.mesh.front () = Fuse (); sdk.files["Meshes/ship.col"] = "COLLIDER-V1\n"; } }
	uint32_t Seed (const std::string &name, double P, double R)
	{
		a = Add (name); body.back ().v->size = 10;
		DentRecord c {};
		c.p.mode = DENTM_CRUSH, c.p.c = Vector (5, 0, 0), c.p.n = Vector (1, 0, 0), c.p.t = Vector (0, 1, 0), c.p.R = R, c.p.h = P, c.p.P = P;
		c.slot = 0, c.key = DentMath::MeshKey ("ship"), c.ngrp = (uint16_t)sdk.mesh.front ().grp.size ();
		for (auto &g : sdk.mesh.front ().grp) c.nvtx += (uint32_t)g.vtx.size ();
		REQUIRE (S ().AddCut (a, c, true));
		return a;
	}
	CollDamageHit T (double eSpec, double vn, double Es = 1)
	{
		CollDamageHit h;
		h.id = a; h.h = host.Vessel (a); h.mesh = 0; h.grp = 9; h.rec = 0; h.c = Vector (5, 0, 0); h.n = Vector (1, 0, 0); h.R = 0.5;
		h.vn = vn; h.eSpec = eSpec; h.E = eSpec * 500; h.Esurplus = Es; h.mat = &kMat; h.simt = sdk.simt; h.mode = DENTM_CRUSH;
		return h;
	}
	const DentRecord *Cut () { const VesselDamageA *v = S ().Damage (a); if (v) for (auto &r : v->d.rec) if (r.p.mode == DENTM_CUT) return &r; return nullptr; }
};
}

TEST_CASE ("tear 8: gate (555, 70) cuts, debris holds straddlers; (100, 30) and (25, 15) do not", "[dmg3P][tear]")
{
	DentParams cr {}; cr.mode = DENTM_CRUSH; cr.P = 0.1;
	CollDamageHit h; h.Esurplus = 1; h.vn = 70; h.eSpec = 555;
	CHECK (CollBreakA::TearGate (h, cr, 10));
	h.vn = 30; h.eSpec = 100; CHECK (!CollBreakA::TearGate (h, cr, 10));
	h.vn = 15; h.eSpec = 25; CHECK (!CollBreakA::TearGate (h, cr, 10));
	h.vn = 70; h.eSpec = 555; h.Esurplus = 0; CHECK (!CollBreakA::TearGate (h, cr, 10)); cr.P = 0.95 * DentMath::DmaxCrush (10); CHECK (CollBreakA::TearGate (h, cr, 10));
	TearRig r; r.Seed ("A", 0.6, 0.5);
	r.B ().Hit (r.T (555, 70));
	const DentRecord *c = r.Cut ();
	REQUIRE (c);
	CHECK (c->p.h == 0.0); CHECK (c->p.hz > 0); CHECK (std::fabs (c->p.P - 0.4 * c->p.hd) < 1e-6);
	CHECK (c->grp == std::vector<uint16_t> { 7 });                    // straddler x 2..3
	CHECK (r.B ().Hidden (r.a, 0, 8)); CHECK (r.B ().Hidden (r.a, 0, 9)); CHECK (!r.B ().Hidden (r.a, 0, 7));
	REQUIRE (r.B ().Pending () == 1);
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	REQUIRE (r.B ().Debris ().size () == 1);
	const DentDebris &d = r.B ().Debris ()[0].row;
	std::set<uint16_t> in; for (auto &ps : d.pose) in.insert (ps.grp.begin (), ps.grp.end ());
	CHECK (in == std::set<uint16_t> { 7, 8, 9 });
	REQUIRE (!d.rec.empty ()); CHECK (d.rec.back ().p.mode == DENTM_CUT); CHECK ((d.rec.back ().p.bits & DENTC_KEEP));
	CHECK (r.sdk.Logged ("Collision tear 'A'"));
	TearRig q; q.Seed ("B", 0.6, 0.5);
	q.B ().Hit (q.T (100, 30)); q.B ().Hit (q.T (25, 15));
	CHECK (!q.Cut ()); CHECK (!q.B ().Hidden (q.a, 0, 9));
}

TEST_CASE ("tear 9: B row mass reused on reload, MeshEdit before DebrisSetup, FNV equal", "[dmg3P][tear]")
{
	std::vector<DentDebris> rows; std::vector<DentTorn> torn; uint32_t fnv = 0; double mass = 0;
	{
		TearRig r; r.Seed ("A", 0.6, 0.5);
		r.B ().Hit (r.T (555, 70)); r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
		REQUIRE (r.B ().Debris ().size () == 1);
		fnv = r.B ().Debris ()[0].fnv;
		BFake::V *dv = BFake::X (r.B ().Debris ()[0].h);
		mass = r.sdk.caps[dv].mass;
		size_t e = 0, ds = 0;
		for (size_t i = 0; i < r.sdk.calls.size (); i++) { if (r.sdk.calls[i] == "MeshEdit" && !e) e = i + 1; if (r.sdk.calls[i] == "DebrisSetup") ds = i + 1; }
		CHECK (e > 0); CHECK (e < ds);
		rows = r.S ().Damage (r.a)->d.debris; torn = r.S ().Damage (r.a)->d.torn;
		REQUIRE (rows.size () == 1);
		CHECK (rows[0].mass == mass);
		std::vector<std::string> lines; DentMath::FormatDebris (rows[0], "", lines);
		DentVesselParser p; p.Line ("XDMG 1 0 0"); for (auto &l : lines) p.Line (l.c_str ());
		r.B ().End ();
	}
	CHECK (mass > 1); CHECK (mass <= BRK_TEAR_MMAX * 500 + 1e-9);
	TearRig r; r.Seed ("A", 0.6, 0.5);
	BFake::V *dv = r.sdk.Add ("A_D1", "CollDebris"); r.host.IdOf (dv);
	for (auto &t : torn) r.S ().AddTorn (r.a, t);
	r.body.front ().v->empty = 900; // a changed parent mass does not change the stored section mass
	r.S ().SetDebris (r.a, rows);
	r.B ().PreStep (3, 0.01); r.B ().Post (3, 0.01);
	REQUIRE (r.B ().Debris ().size () == 1);
	CHECK (r.B ().Debris ()[0].fnv == fnv);
	CHECK (r.sdk.caps[dv].mass == mass);
}

TEST_CASE ("tear 10: belly hit does not cut; snap moves the plane to a group's rear", "[dmg3P][tear]")
{
	TearRig r; r.Seed ("A", 0.6, 0.5);
	DentRecord b {};
	b.p.mode = DENTM_CRUSH, b.p.c = Vector (0, 0, 0), b.p.n = Vector (0, 0, 1), b.p.t = Vector (1, 0, 0), b.p.R = 0.5, b.p.h = b.p.P = 0.3;
	b.slot = 0, b.key = DentMath::MeshKey ("ship"), b.ngrp = 10; for (auto &g : r.sdk.mesh.front ().grp) b.nvtx += (uint32_t)g.vtx.size ();
	REQUIRE (r.S ().AddCut (r.a, b, true));
	CollDamageHit h = r.T (555, 70); h.rec = 1; h.grp = 5; h.c = Vector (0, 0, 0); h.n = Vector (0, 0, 1);
	r.B ().Hit (h);
	CHECK (!r.Cut ());
	TearRig q; q.Seed ("B", 1.45, 0.5);
	CollCutPlan pl = q.B ().PlanCut (q.a, *q.B ().Slot (q.a, 0), q.T (300, 70), q.S ().Damage (q.a)->d.rec[0].p, 10, false, 1);
	REQUIRE (pl.ok);
	CHECK (std::fabs (pl.d - 2.0) < 1e-9);
	const CollSlotA *qs = q.B ().Slot (q.a, 0);
	CHECK (qs->staticCls == qs->cls[qs->piece[qs->pieceOf[0]].grp[0]]);   // M2: the hull's pose class, not the hit group's                                        // 1.95 snapped back to the rear of group 8 (x = 3)
	CHECK (std::find (pl.front.begin (), pl.front.end (), 8) == pl.front.end ()); // its rear on the plane: straddles the jag band only
	CHECK (std::find (pl.straddle.begin (), pl.straddle.end (), 8) != pl.straddle.end ());
	CHECK (std::find (pl.front.begin (), pl.front.end (), 9) != pl.front.end ());
}

TEST_CASE ("tear 11: tip gate at 1.0 Mp, not 0.5; rest-hidden never debris; 0.4 m piece hidden only; dock pin released at 300 J/kg", "[dmg3P][tear]")
{
	DentParams hp {}; hp.mode = DENTM_HINGE; hp.P = DENT_HINGE_TMAX;
	CollDamageHit h; h.Mp = 1000; h.vn = 30; h.Esurplus = 1000;
	CHECK (CollBreakA::TipGate (h, hp));
	h.Esurplus = 500; CHECK (!CollBreakA::TipGate (h, hp));
	{
		TearRig r (true);
		r.sdk.mesh.front ().grp[8].usrflag = 2;
		r.Seed ("A", 0.6, 0.5);
		r.B ().Hit (r.T (555, 70)); r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
		REQUIRE (r.B ().Debris ().size () == 1);
		for (auto &ps : r.B ().Debris ()[0].row.pose) CHECK (std::find (ps.grp.begin (), ps.grp.end (), 8) == ps.grp.end ());
		bool row8 = false; for (auto &t : r.S ().Damage (r.a)->d.torn) for (uint16_t g : t.grp) if (g == 8) row8 = true;
		CHECK (row8);
	}
	{
		Rig r; r.sdk.mesh.front ().grp[1] = CollGroupData (); Quad (r.sdk.mesh.front ().grp[1], 5.5, -0.2, 0.2, 0.4, 4);
		uint32_t a = r.Add ("A");
		r.B ().Hit (r.H (a, 1, 70, 0.9)); r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
		CHECK (r.B ().Hidden (a, 0, 1));
		CHECK (r.sdk.Calls ("VesselCreate") == 0);
	}
	{
		Rig r2; uint32_t a2 = r2.Add ("A");
		r2.body.front ().v->dock.push_back (Vector (6, 0, 0.2));
		const CollSlotA *sl = r2.B ().Slot (a2, 0);
		REQUIRE (sl); CHECK ((sl->piece[sl->pieceOf[1]].functional & CBRK_FN_DOCK));
		CollDamageHit lo = r2.H (a2, 1, 30, 0.9); lo.eSpec = 100;
		r2.B ().Hit (lo); CHECK (!r2.B ().Hidden (a2, 0, 1));
		CollDamageHit hi = lo; hi.eSpec = 300;
		r2.B ().Hit (hi); CHECK (r2.B ().Hidden (a2, 0, 1));
	}
}

TEST_CASE ("tear 12: spawn velocity uses the post-impulse rd.v; kick along +-t / +-e only", "[dmg3P][tear]")
{
	TearRig r; r.Seed ("A", 0.6, 0.5);
	r.B ().Hit (r.T (555, 70));
	r.body.front ().v->rd.v = Vector (0, 0, 3);                         // PS4 wrote the post-impulse state before PO2
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	REQUIRE (r.B ().Debris ().size () == 1);
	BFake::V *dv = BFake::X (r.B ().Debris ()[0].h);
	CollStateWrite st = r.sdk.created[dv];
	const DentRecord *c = r.Cut ();
	REQUIRE (c);
	Vector kick = st.rvel - Vector (0, 0, 3);
	CHECK (std::fabs (kick & c->p.n) < 1e-9);
	Vector e = crossp (c->p.n, c->p.t);
	CHECK ((std::fabs (std::fabs (kick & c->p.t) - kick.length ()) < 1e-9 || std::fabs (std::fabs (kick & e) - kick.length ()) < 1e-9));
	CHECK (kick.length () >= 0.8 * BRK_TEAR_KICK * 70 - 1e-9); CHECK (kick.length () <= 1.2 * BRK_TEAR_KICK * 70 + 1e-9);
}

TEST_CASE ("tear 13: playback rebuilds the section debris from T + cut, no TearGate", "[dmg3P][tear]")
{
	DentRecord cut; DentTorn torn;
	{
		TearRig r; r.Seed ("A", 0.6, 0.5);
		r.B ().Hit (r.T (555, 70));
		REQUIRE (r.Cut ()); cut = *r.Cut ();
		for (auto &t : r.S ().Damage (r.a)->d.torn) if (t.kind == CBRK_SECTION) torn = t;
		REQUIRE (torn.kind == CBRK_SECTION);
	}
	TearRig r; r.Seed ("A", 0.6, 0.5);
	r.body.front ().v->playback = true;
	CollDamageHit h = r.T (555, 70); h.playback = true;
	r.B ().Hit (h);
	CHECK (!r.Cut ());                                                     // no gate math in playback
	REQUIRE (r.S ().AddCut (r.a, cut, true));
	REQUIRE (torn.kin);                                                    // M3: the live kick rides on the T row
	torn.dv = Vector (0, 1.5, 0), torn.dw = Vector (0, 0, 0.5), torn.mass = 777;
	r.B ().Torn (r.a, torn);
	CHECK (r.B ().Hidden (r.a, 0, 9));
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	REQUIRE (r.sdk.Calls ("VesselCreate") == 1);
	REQUIRE (r.B ().Debris ().size () == 1);
	CHECK (r.B ().Debris ()[0].row.rec.back ().p.bits & DENTC_KEEP);
	BFake::V *dv = BFake::X (r.B ().Debris ()[0].h);
	CHECK ((r.sdk.created[dv].rvel - Vector (0, 1.5, 0)).length () < 1e-9);
	CHECK (r.sdk.caps[dv].mass == 777);
}

// blast (design-CA-blast 4, 7): a 4 m box hull, one group per face, 6 x 6 quads each (432 triangles)
namespace {
MeshT BoxMesh ()
{
	MeshT m; m.name = "ship"; m.grp.resize (6);
	const double L = 4; const int n = 6;
	int k = 0;
	for (int ax = 0; ax < 3; ax++) for (int sg = -1; sg <= 1; sg += 2, k++) {
		CollGroupData &g = m.grp[k];
		auto P = [&] (double u, double v) { double c[3]; c[ax] = sg * L / 2; c[(ax + 1) % 3] = u; c[(ax + 2) % 3] = v; double nn[3] = { 0, 0, 0 }; nn[ax] = sg; return CollVtx { (float)c[0], (float)c[1], (float)c[2], (float)nn[0], (float)nn[1], (float)nn[2], 0, 0 }; };
		for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) g.vtx.push_back (P (-L / 2 + L * i / n, -L / 2 + L * j / n));
		for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
			uint16_t a = (uint16_t)(j * (n + 1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + n + 1), d = (uint16_t)(c + 1);
			g.idx.insert (g.idx.end (), { a, b, d, a, d, c });
		}
	}
	return m;
}
struct BlastRig : Rig {
	uint32_t a = 0;
	BlastRig () { sdk.mesh.front () = BoxMesh (); sdk.files["Meshes/ship.col"] = "COLLIDER-V1\n"; cfg.blast = true; }
	uint32_t Ship (const std::string &name) { a = Add (name); body.back ().v->size = 10; body.back ().v->empty = 5000; body.back ().v->rd.m = 5000; body.back ().v->rd.pmi = Vector (2.7, 2.7, 2.7); return a; }
	CollDamageHit K (double vn, double R = 1.5)
	{
		CollDamageHit h;
		h.id = a; h.h = host.Vessel (a); h.mesh = 0; h.grp = 5; h.rec = -1; h.c = Vector (0.3, 0.2, 2); h.n = Vector (0, 0, 1); h.tdir = Vector (1, 0, 0); h.R = R;
		h.vn = vn; h.eSpec = 0.5 * vn * vn; h.E = h.eSpec * 5000; h.Jn = 2500 * vn; h.dt = 0.02; h.depth = 0.004 * vn; h.mat = &kMat; h.simt = sdk.simt;
		return h;
	}
};
}

TEST_CASE ("blast P1: a 70 m/s hit separates cells; SpawnCells makes one debris per break with KEEP VCUT copies; momentum conserved", "[dmg3P][blast]")
{
	BlastRig r; uint32_t a = r.Ship ("A");
	r.B ().Hit (r.K (70));
	REQUIRE (r.B ().Blast (a, 0));
	CHECK (r.B ().Blast (a, 0)->site.size () == 64);
	const DentSites *ds = r.S ().Sites (a, 0);
	REQUIRE (ds); CHECK (ds->s.size () == 64); CHECK (ds->key == DentMath::MeshKey ("ship"));
	REQUIRE (r.B ().blastBreaks >= 1);
	CHECK (r.B ().Pending () >= 1);
	const std::vector<uint32_t> *kb = r.S ().BrokenBonds (a, 0);
	REQUIRE (kb); CHECK (!kb->empty ());
	CHECK (*kb == r.B ().Blast (a, 0)->BrokenPairs ());          // K rows: chunk key pairs
	for (uint32_t p : *kb) { CHECK (p / 65536 < p % 65536); CHECK (p % 65536 < 64); }
	CHECK (r.B ().Blast (a, 0)->BondsOfPairs (*kb) == r.B ().Blast (a, 0)->Broken ());
	CHECK (r.sdk.Logged ("Collision blast break 'A' slot=0 cells="));
	r.sdk.simt = 0.02;
	r.B ().PreStep (0.02, 0.02); r.B ().Post (0.02, 0.02);
	REQUIRE (!r.B ().Debris ().empty ());
	CHECK (r.sdk.Calls ("VesselCreate") == (int)r.B ().Debris ().size ());
	Vector Pm, Hm; double scale = 0;
	const BFake::V *pv = r.body.front ().v;
	size_t moved = 0, nv = 0;
	for (auto &d : r.B ().Debris ()) {
		CHECK (d.parent == a);
		size_t keep = 0;
		for (auto &rc : d.row.rec) if (rc.p.mode == DENTM_VCUT) {
			CHECK ((rc.p.bits & DENTC_KEEP) != 0);
			uint32_t c = (uint32_t)rc.p.P;
			CHECK (rc.p.P == (double)c); REQUIRE (c < 64); CHECK (rc.p.seed == 64);
			CHECK (rc.p.c.x == ds->s[c].x); CHECK (rc.p.c.y == ds->s[c].y); CHECK (rc.p.c.z == ds->s[c].z);
			CHECK (rc.grp == std::vector<uint16_t> { 0, 1, 2, 3, 4, 5 });
			keep++;
		}
		CHECK (keep >= 1);
		CHECK (d.row.mass > 2);
		REQUIRE (d.row.pose.size () == 1);
		const MeshT &tp = r.sdk.mesh.front ();
		std::set<uint32_t> kc;
		for (auto &rc : d.row.rec) if (rc.p.mode == DENTM_VCUT) kc.insert ((uint32_t)rc.p.P);
		std::vector<uint16_t> want;                                  // only the faces with a triangle in the kept cells
		for (uint16_t g = 0; g < 6; g++) {
			bool any = false;
			for (size_t t = 0; t + 2 < tp.grp[g].idx.size (); t += 3) {
				Vector x;
				for (int k = 0; k < 3; k++) { const auto &q = tp.grp[g].vtx[tp.grp[g].idx[t + k]]; x += Vector (q.x, q.y, q.z) / 3.0; }
				size_t bi = 0; for (size_t c = 1; c < ds->s.size (); c++) if ((x - ds->s[c]).length2 () < (x - ds->s[bi]).length2 ()) bi = c;
				if (kc.count ((uint32_t)bi)) any = true;
			}
			if (any) want.push_back (g);
		}
		CHECK (!want.empty ()); CHECK (want.size () < 6);
		CHECK (d.row.pose[0].grp == want);
		const MeshT *dm = r.sdk.debrisMesh[(const BFake::V *)d.h];      // VCUT KEEP evaluated with the parent's sites: far vertices fold onto the cell
		REQUIRE (dm);
		for (uint16_t g : want) for (size_t i = 0; i < tp.grp[g].vtx.size (); i++) {
			Vector x (tp.grp[g].vtx[i].x, tp.grp[g].vtx[i].y, tp.grp[g].vtx[i].z), y (dm->grp[g].vtx[i].x, dm->grp[g].vtx[i].y, dm->grp[g].vtx[i].z);
			Vector yr = y - d.row.pose[0].p;                        // rest frame: identity pose
			nv++; if ((yr - x).length () > 1e-3) moved++;
		}
		const BFake::V *dv = (const BFake::V *)d.h;
		REQUIRE (r.sdk.created.count (dv)); REQUIRE (r.sdk.caps.count (dv)); REQUIRE (r.sdk.spins.count (dv));
		const CollStateWrite &st = r.sdk.created[dv];
		const CollSdk::DebrisCaps &cp = r.sdk.caps[dv];
		double m = cp.mass;
		CHECK (m == d.row.mass);
		Vector v = st.rvel, cv = st.rpos, w = r.sdk.spins[dv];      // parent at rest at the origin: relative kick, centroid, spin
		CHECK (std::fabs (v.length () - 0.1 * 70) < 1e-9);
		CHECK (cv.z > 1.0);
		CHECK (std::fabs (w & v) <= 1e-9 * std::max (1.0, w.length () * v.length ())); // kick torque from our lever arm: spin normal to the kick
		Pm += v * m; Hm += crossp (v, cv) * m + Vector (cp.pmi.x * w.x, cp.pmi.y * w.y, cp.pmi.z * w.z) * m;
		scale = std::max (scale, m * v.length () * (1 + cv.length ()));
	}
	CHECK (moved > nv / 2);
	REQUIRE (r.B ().kicks.size () == r.B ().Debris ().size ());
	CHECK (r.sdk.forces.empty ());                                  // no AddForce over an unknown step
	size_t ns = 0;
	for (auto &x : r.sdk.states) if (x.first == pv) ns++;
	CHECK (ns == 1);                                                // one write per parent: the sum of its debris' kicks
	double cut = r.B ().MassCut (a), dsum = 0;
	for (auto &d : r.B ().Debris ()) dsum += d.row.mass;
	CHECK (cut > 0);
	CHECK (std::fabs (cut - dsum) <= 1e-6 * dsum);                  // the parent loses what flies off
	CHECK (pv->empty == 5000 - cut); CHECK (pv->rd.m == 5000 - cut);
	Vector pp = pv->rd.pmi;                                         // the parent's PMI without the debris inertia
	CHECK ((pp.x < 2.7 || pp.y < 2.7));
	for (auto &k : r.B ().kicks) {
		CHECK (k.M == 5000 - cut);
		Pm += k.dv * k.M;
		Hm += Vector (pp.x * k.dw.x, pp.y * k.dw.y, pp.z * k.dw.z) * k.M;
	}
	Vector vlast; for (auto &x : r.sdk.states) if (x.first == pv) vlast = x.second.rvel;
	Vector vsum; for (auto &k : r.B ().kicks) vsum += k.dv;
	CHECK ((vlast - vsum).length () < 1e-12);                       // rd.v (at rest) + the sum of the kicks
	CHECK (Pm.length () <= 1e-9 * std::max (1.0, scale));
	CHECK (Hm.length () <= 1e-9 * std::max (1.0, scale));
	printf ("blast P1: %zu debris, %llu breaks, parent+debris |P| %.3g |H| %.3g (scale %.3g), %zu/%zu debris vertices folded\n", r.B ().Debris ().size (), (unsigned long long)r.B ().blastBreaks, Pm.length (), Hm.length (), scale, moved, nv);
	std::set<uint32_t> dids;
	for (auto &d : r.B ().Debris ()) dids.insert (d.id);
	for (auto &d : r.B ().Debris ()) {                              // every debris: filtered against its parent and every other debris, for good
		CHECK (r.S ().noPair.count ({ std::min (a, d.id), std::max (a, d.id) }));
		for (uint32_t o : dids) if (o != d.id) CHECK (r.S ().noPair.count ({ std::min (o, d.id), std::max (o, d.id) }));
	}
	for (auto &pr : r.B ().Pairs ()) CHECK (((pr.a == a || pr.b == a) || (dids.count (pr.a) && dids.count (pr.b))));
	CHECK (!r.B ().Pairs ().empty ());                              // pair filter parent-debris
	for (auto &d : r.B ().Debris ()) {                              // blast debris rows save and parse with their KEEP VCUT records
		DentVesselText vt; vt.debris.push_back (d.row);
		std::vector<std::string> lines; DentMath::FormatVessel (vt, "", lines);
		DentVesselParser p; for (auto &l : lines) p.Line (l.c_str ());
		DentVesselText o; p.Finish (o);
		REQUIRE (o.debris.size () == 1);
		REQUIRE (o.debris[0].rec.size () == d.row.rec.size ());
		for (size_t i = 0; i < d.row.rec.size (); i++) {
			CHECK (o.debris[0].rec[i].p.mode == d.row.rec[i].p.mode); CHECK (o.debris[0].rec[i].p.P == d.row.rec[i].p.P);
			CHECK (o.debris[0].rec[i].p.bits == d.row.rec[i].p.bits); CHECK (o.debris[0].rec[i].p.R > 0);
		}
	}
}

TEST_CASE ("blast P2: a small hit breaks nothing; Blast runs only within 2 s of a hit", "[dmg3P][blast]")
{
	BlastRig r; uint32_t a = r.Ship ("A");
	r.B ().Hit (r.K (1, 0.5));
	REQUIRE (r.B ().Blast (a, 0));
	CHECK (r.B ().blastBreaks == 0);
	CHECK (r.B ().Pending () == 0);
	const std::vector<uint32_t> *kb = r.S ().BrokenBonds (a, 0);
	CHECK ((!kb || kb->empty ()));
	uint64_t n0 = r.B ().blastSteps;
	r.body.front ().v->rd.w = Vector (0.2, 0.1, 0);
	r.sdk.simt = 1.0; r.B ().PreStep (1.0, 0.02); r.B ().Post (1.0, 0.02);
	CHECK (r.B ().blastSteps == n0 + 1);
	r.sdk.simt = 1.9; r.B ().PreStep (1.9, 0.02); r.B ().Post (1.9, 0.02);
	CHECK (r.B ().blastSteps == n0 + 2);
	r.sdk.simt = 2.5; r.B ().PreStep (2.5, 0.02); r.B ().Post (2.5, 0.02);
	CHECK (r.B ().blastSteps == n0 + 2);                            // idle slots cost nothing
	CHECK (r.B ().blastBreaks == 0);
	CHECK (r.sdk.Calls ("VesselCreate") == 0);
}

TEST_CASE ("blast P3: load rebuilds the asset from sites, K bonds and VCUT cells: same actor partition, no new break", "[dmg3P][blast]")
{
	BlastRig r; uint32_t a = r.Ship ("A");
	r.B ().Hit (r.K (70));
	CollBlastA *x = r.B ().Blast (a, 0);
	REQUIRE (x);
	REQUIRE (r.B ().blastBreaks >= 1);
	auto part = x->Partition ();
	auto mainCh = x->MainChunks ();
	std::vector<uint32_t> cells;
	for (size_t c = 0; c < x->chunk.size (); c++) if (x->gone[c] && x->chunk[c].cell >= 0) cells.push_back ((uint32_t)x->chunk[c].cell);
	REQUIRE (!cells.empty ());
	DentSites ds = *r.S ().Sites (a, 0);
	std::vector<uint32_t> kb = *r.S ().BrokenBonds (a, 0);
	for (int variant = 0; variant < 2; variant++) {                 // 0: S + K + VCUT rows; 1: S + VCUT rows only
		INFO ("variant " << variant);
		BlastRig q; uint32_t b = q.Ship ("A");
		q.S ().SetSites (b, ds);
		if (variant == 0) q.S ().AddBrokenBonds (b, 0, kb);
		for (uint32_t c : cells) {
			DentRecord rc {};
			rc.slot = 0; rc.key = ds.key; rc.ngrp = 6; rc.nvtx = 6 * 49; rc.grp = { 0, 1, 2, 3, 4, 5 };
			rc.p.mode = DENTM_VCUT; rc.p.P = c; rc.p.seed = 64; rc.p.c = ds.s[c]; rc.p.n = Vector (0, 0, 1); rc.p.t = Vector (1, 0, 0);
			REQUIRE (q.S ().AddCut (b, rc, true));
		}
		q.B ().Hit (q.K (1, 0.5));                                   // first live hit builds and restores
		CollBlastA *y = q.B ().Blast (b, 0);
		REQUIRE (y);
		if (variant == 0) CHECK (y->Partition () == part);          // VCUT rows alone cannot tell how the removed cells were grouped
		CHECK (y->MainChunks () == mainCh);
		CHECK (q.B ().blastBreaks == 0);
		CHECK (q.B ().Pending () == 0);
		CHECK (q.sdk.Logged ("restored="));
	}
}

TEST_CASE ("blast P4: cfg.blast off keeps the part, tear and tip gates; on, parts and sections wait for Blast, glass stays", "[dmg3P][blast]")
{
	{
		Rig r; uint32_t a = r.Add ("A");                            // off (Rig default)
		r.B ().Hit (r.H (a, 1, 30, 0.65));
		CHECK (r.B ().Hidden (a, 0, 1));
		CHECK (r.B ().Blast (a, 0) == nullptr);
	}
	{
		Rig r; r.cfg.blast = true; uint32_t a = r.Add ("A");
		r.B ().Hit (r.H (a, 1, 30, 0.65));
		CHECK (!r.B ().Hidden (a, 0, 1));                           // part gate off
		CHECK (r.B ().Blast (a, 0) != nullptr);
		r.B ().Hit (r.H (a, 2, 5, 0, 0.03));
		CHECK (r.B ().Hidden (a, 0, 2));                            // glass stays
	}
	{
		TearRig r; r.Seed ("A", 0.6, 0.5);
		r.B ().Hit (r.T (555, 70));
		CHECK (r.Cut () != nullptr);                                // off: the tear gate cuts
	}
	{
		TearRig r; r.cfg.blast = true; r.Seed ("A", 0.6, 0.5);
		r.B ().Hit (r.T (555, 70));
		CHECK (r.Cut () == nullptr);                                // on: no section cut
		CHECK (r.B ().tears == 0);
	}
}

TEST_CASE ("blast P5: the contact force uses the contact time, not the frame: 30 and 60 fps break the same bonds", "[dmg3P][blast]")
{
	std::vector<uint32_t> k[2]; uint64_t n[2];
	for (int f = 0; f < 2; f++) {
		BlastRig r; uint32_t a = r.Ship ("A");
		CollDamageHit h = r.K (40);
		h.dt = f ? 1.0 / 60 : 1.0 / 30;
		r.B ().Hit (h);
		REQUIRE (r.B ().Blast (a, 0));
		k[f] = r.B ().Blast (a, 0)->BrokenPairs (); n[f] = r.B ().blastBreaks;
	}
	CHECK (k[0] == k[1]);
	CHECK (n[0] == n[1]);
	CHECK (!k[0].empty ());
}

TEST_CASE ("blast P6: the parent's empty mass drops by the broken cells, a reload cuts the same mass, repair restores it", "[dmg3P][blast]")
{
	BlastRig r; uint32_t a = r.Ship ("A");
	r.B ().Hit (r.K (70));
	CollBlastA *x = r.B ().Blast (a, 0);
	REQUIRE (x); REQUIRE (r.B ().blastBreaks >= 1);
	double cut = r.B ().MassCut (a), gone = 0;
	std::vector<uint32_t> cells;
	for (size_t c = 0; c < x->chunk.size (); c++) if (x->gone[c]) { gone += x->chunk[c].mass; if (x->chunk[c].cell >= 0) cells.push_back ((uint32_t)x->chunk[c].cell); }
	CHECK (std::fabs (cut - gone) <= 1e-9 * gone);
	CHECK (r.body.front ().v->empty == 5000 - cut);
	CHECK (r.sdk.Logged ("Collision blast 'A' empty mass 5000 -> "));
	DentSites ds = *r.S ().Sites (a, 0);
	BlastRig q; uint32_t b = q.Ship ("A");
	q.S ().SetSites (b, ds);
	for (uint32_t c : cells) {
		DentRecord rc {};
		rc.slot = 0; rc.key = ds.key; rc.ngrp = 6; rc.nvtx = 6 * 49; rc.grp = { 0, 1, 2, 3, 4, 5 };
		rc.p.mode = DENTM_VCUT; rc.p.P = c; rc.p.seed = 64; rc.p.c = ds.s[c]; rc.p.n = Vector (0, 0, 1); rc.p.t = Vector (1, 0, 0);
		REQUIRE (q.S ().AddCut (b, rc, true));
	}
	q.B ().PreStep (0, 0.02); q.B ().Post (0, 0.02);                                           // first post-step after load
	CHECK (std::fabs (q.B ().MassCut (b) - cut) <= 1e-9 * cut);
	CHECK (std::fabs (q.body.front ().v->empty - (5000 - cut)) <= 1e-9 * 5000);
	q.B ().PreStep (0.02, 0.02); q.B ().Post (0.02, 0.02);
	CHECK (std::fabs (q.B ().MassCut (b) - cut) <= 1e-9 * cut);   // once per load
	r.B ().Repair (a);
	CHECK (r.body.front ().v->empty == 5000); CHECK (r.body.front ().v->rd.m == 5000);
	for (double c : { r.body.front ().v->rd.pmi.x, r.body.front ().v->rd.pmi.y, r.body.front ().v->rd.pmi.z }) CHECK (std::fabs (c - 2.7) < 1e-9); // PMI back
	CHECK (r.B ().MassCut (a) == 0);
	BlastRig z; uint32_t e = z.Ship ("A");
	z.B ().PreStep (0, 0.02); z.B ().Post (0, 0.02);
	CHECK (z.B ().MassCut (e) == 0); CHECK (z.body.front ().v->empty == 5000); // no sites: no cut
}

TEST_CASE ("blast P7: bonds weakened without breaking are saved as W rows and a reload restores their health", "[dmg3P][blast]")
{
	for (double v : { 20.0, 70.0 }) {
		INFO ("v " << v);
		BlastRig r; uint32_t a = r.Ship ("A");
		r.B ().Hit (r.K (v));
		CollBlastA *x = r.B ().Blast (a, 0);
		REQUIRE (x);
		r.B ().PreStep (0.02, 0.02); r.B ().Post (0.02, 0.02);
		const std::vector<uint32_t> *wb = r.S ().WeakBonds (a, 0);
		REQUIRE (wb); REQUIRE (!wb->empty ());
		CHECK (*wb == x->WeakPairs ());
		CHECK (wb->size () % 2 == 0);
		for (size_t k = 1; k < wb->size (); k += 2) { CHECK ((*wb)[k] >= 1); CHECK ((*wb)[k] < 1000000u); }
		const DentVesselText &vt = r.S ().Damage (a)->d;
		std::vector<std::string> lines; DentMath::FormatVessel (vt, "", lines);
		for (auto &l : lines) CHECK (l.size () <= 200);
		DentVesselParser p; for (auto &l : lines) p.Line (l.c_str ());
		DentVesselText o; p.Finish (o);
		CHECK (o.weakBonds == vt.weakBonds); CHECK (o.brokenBonds == vt.brokenBonds); CHECK (o.sites.size () == vt.sites.size ());
		BlastRig q; uint32_t b = q.Ship ("A");
		q.S ().SetSites (b, o.sites[0]);
		for (auto &kb : o.brokenBonds) q.S ().AddBrokenBonds (b, kb.first, kb.second);
		for (auto &w : o.weakBonds) q.S ().SetWeakBonds (b, w.first, w.second);
		for (auto &rc : vt.rec) if (rc.p.mode == DENTM_VCUT) REQUIRE (q.S ().AddCut (b, rc, true));
		q.B ().PreStep (0, 0.02); q.B ().Post (0, 0.02);                                       // load: built and restored without a hit
		CollBlastA *y = q.B ().Blast (b, 0);
		REQUIRE (y);
		REQUIRE (y->bond.size () == x->bond.size ());
		size_t weak = 0;
		for (uint32_t i = 0; i < x->bond.size (); i++) {
			CHECK (std::fabs (y->Health (i) - x->Health (i)) <= 2e-6 * x->bond[i].area + 1e-9);
			if (x->Health (i) > 0 && (float)x->Health (i) < (float)x->bond[i].area) weak++;
		}
		CHECK (weak == wb->size () / 2);
		CHECK (y->WeakPairs () == *wb);
		CHECK (q.B ().blastBreaks == 0);
	}
}

TEST_CASE ("blast P8: pieces split off by Blast keep the dmg3 part gates: approach speed 20 m/s, dock pin below 300 J/kg", "[dmg3P][blast]")
{
	CollPieceA p; CollDamageHit h;
	h.vn = 15; h.eSpec = 26;  CHECK (CollPieceHeld (p, h));          // DG-DG 15 m/s: parts stay
	h.vn = 30; h.eSpec = 120; CHECK (!CollPieceHeld (p, h));
	p.functional = CBRK_FN_DOCK;
	CHECK (CollPieceHeld (p, h));                                     // the dock pin holds
	h.vn = 70; h.eSpec = 550; CHECK (!CollPieceHeld (p, h));          // and lets go above the tear threshold
}

TEST_CASE ("blast P9: skin crushed beyond the part ratio tears off as fragments at 70 m/s, not at 15 m/s; fragments keep no crush record", "[dmg3P][blast]")
{
	size_t brk[2] = { 0, 0 }, crushedDebris = 0;
	for (int k = 0; k < 2; k++) {
		double vn = k ? 70 : 15;
		INFO ("vn " << vn);
		BlastRig r; uint32_t a = r.Ship ("A");
		DentRecord rc {};
		rc.slot = 0; rc.key = DentMath::MeshKey ("ship"); rc.ngrp = 6; rc.nvtx = 6 * 49; rc.grp = { 0, 1, 2, 3, 4, 5 };
		rc.p.mode = DENTM_CRUSH; rc.p.c = Vector (0.3, 0.2, 2); rc.p.n = Vector (0, 0, 1); rc.p.t = Vector (1, 0, 0); rc.p.R = 2.5; rc.p.P = 1.5; rc.p.h = 1.5;
		REQUIRE (r.S ().AddCut (a, rc, true));
		CollDamageHit h = r.K (vn, 0.01);                            // tiny impact radius: the crush decides
		h.rec = (int)r.S ().Damage (a)->d.rec.size () - 1;
		r.B ().Hit (h);
		r.sdk.simt = 0.02;
		r.B ().PreStep (0.02, 0.02); r.B ().Post (0.02, 0.02);
		brk[k] = r.B ().blastBreaks;
		for (auto &d : r.B ().Debris ()) {
			bool crush = false;
			for (auto &x : d.row.rec) if (x.p.mode == DENTM_CRUSH) crush = true;
			if (!crush) crushedDebris++;
			CHECK (d.row.mass >= BRK_MIN_MASS);
		}
	}
	CHECK (brk[0] == 0);                                             // 15 m/s: below the part speed, the crush only dents
	CHECK (brk[1] >= 3);                                             // 70 m/s: the crushed face tears into fragments
	CHECK (crushedDebris >= 3);
}

TEST_CASE ("spawn queue: a save between the post-step that queued debris and the pre-step keeps its row; the load spawns it", "[dmg3P][blast]")
{
	std::vector<DentDebris> rows; std::vector<DentTorn> torn;
	{
		Rig r; uint32_t a = r.Add ("A"); uint32_t b = r.Add ("B");
		CollDamageHit h = r.H (a, 1, 70, 0.9); h.other = b; h.vt = 0;
		r.B ().Hit (h);
		REQUIRE (r.B ().Pending () == 1);
		CHECK (r.sdk.Calls ("VesselCreate") == 0);
		REQUIRE (r.S ().Damage (a));
		rows = r.S ().Damage (a)->d.debris; torn = r.S ().Damage (a)->d.torn; // what a save now writes
		REQUIRE (rows.size () == 1);
		CHECK (rows[0].name == "A_D1");
	}
	Rig q; uint32_t a = q.Add ("A");
	for (auto &t : torn) q.S ().AddTorn (a, t);
	q.S ().SetDebris (a, rows);
	q.B ().PreStep (0, 0.01); q.B ().Post (0, 0.01);
	CHECK (q.sdk.Calls ("VesselCreate") == 1);
	REQUIRE (q.B ().Debris ().size () == 1);
	CHECK (q.B ().Debris ()[0].row.name == "A_D1");
	CHECK (q.sdk.Logged ("without its vessel spawned again"));
	REQUIRE (q.S ().Damage (a)->d.debris.size () == 1);
}

// custom-fix B (design-custom-fix B1-B9, D1)
namespace {
typedef std::vector<std::pair<std::vector<DentVtx>, std::vector<uint16_t>>> GeoT;
void Rect (GeoT &geo, const Vector &o, const Vector &u, const Vector &v, int n, double bulge = 0) // n x n quads from o along u and v; bulge: a dish along u x v
{
	geo.emplace_back ();
	auto &G = geo.back ();
	Vector nn = crossp (u, v); nn = nn / nn.length ();
	for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) {
		double s = (double)i / n - 0.5, t = (double)j / n - 0.5;
		Vector p = o + u * ((double)i / n) + v * ((double)j / n) + nn * (bulge * (s * s + t * t));
		DentVtx x {}; x.x = (float)p.x, x.y = (float)p.y, x.z = (float)p.z;
		G.first.push_back (x);
	}
	for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
		uint16_t a = (uint16_t)(j * (n + 1) + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + n + 1), d = (uint16_t)(c + 1);
		G.second.insert (G.second.end (), { a, b, d, a, d, c });
	}
}
Vector OldPmi (const GeoT &geo) // the rule before B9: each triangle as a point at its centroid
{
	Vector J; double A = 0;
	for (auto &g : geo) for (size_t t = 0; t + 2 < g.second.size (); t += 3) {
		const DentVtx &x = g.first[g.second[t]], &y = g.first[g.second[t + 1]], &z = g.first[g.second[t + 2]];
		Vector pa (x.x, x.y, x.z), pb (y.x, y.y, y.z), pd (z.x, z.y, z.z);
		double ar = 0.5 * crossp (pb - pa, pd - pa).length ();
		Vector m = (pa + pb + pd) / 3.0;
		J += Vector (m.y * m.y + m.z * m.z, m.x * m.x + m.z * m.z, m.x * m.x + m.y * m.y) * ar; A += ar;
	}
	return A > 0 ? J / A : Vector ();
}
bool Rel (double a, double b, double tol) { return std::fabs (a - b) <= tol * std::max (1.0, std::fabs (b)); }
}

TEST_CASE ("B9 debris PMI: exact triangle second moments; square panel and box shell analytic; 2 or 32 triangles the same", "[dmg3P][caps]")
{
	for (int n : { 1, 4 }) { // 2 and 32 triangles of a 1 m square in z = 0, centred: (1/12, 1/12, 1/6)
		GeoT g; Rect (g, Vector (-0.5, -0.5, 0), Vector (1, 0, 0), Vector (0, 1, 0), n);
		CollSdk::DebrisCaps c = CollBreakA::Caps (g, 10, nullptr);
		INFO ("n " << n);
		CHECK (Rel (c.pmi.x, 1.0 / 12, 1e-12)); CHECK (Rel (c.pmi.y, 1.0 / 12, 1e-12)); CHECK (Rel (c.pmi.z, 1.0 / 6, 1e-12));
	}
	const double a = 2, b = 3, c = 5; // box shell a x b x c, centred
	GeoT box;
	Rect (box, Vector (-1, -1.5, -2.5), Vector (2, 0, 0), Vector (0, 3, 0), 2); Rect (box, Vector (-1, -1.5, 2.5), Vector (2, 0, 0), Vector (0, 3, 0), 2);
	Rect (box, Vector (-1, -1.5, -2.5), Vector (2, 0, 0), Vector (0, 0, 5), 2); Rect (box, Vector (-1, 1.5, -2.5), Vector (2, 0, 0), Vector (0, 0, 5), 2);
	Rect (box, Vector (-1, -1.5, -2.5), Vector (0, 3, 0), Vector (0, 0, 5), 2); Rect (box, Vector (1, -1.5, -2.5), Vector (0, 3, 0), Vector (0, 0, 5), 2);
	double Sxx = 2 * (b * a * a * a / 12 + c * a * a * a / 12 + b * c * a * a / 4), Syy = 2 * (a * b * b * b / 12 + c * b * b * b / 12 + a * c * b * b / 4);
	double Szz = 2 * (a * c * c * c / 12 + b * c * c * c / 12 + a * b * c * c / 4), A = 2 * (a * b + a * c + b * c);
	CollSdk::DebrisCaps bc = CollBreakA::Caps (box, 100, nullptr);
	CHECK (Rel (bc.pmi.x, (Syy + Szz) / A, 1e-12)); CHECK (Rel (bc.pmi.y, (Sxx + Szz) / A, 1e-12)); CHECK (Rel (bc.pmi.z, (Sxx + Syy) / A, 1e-12));
	GeoT p2, p32; // an off-centre tilted panel: triangulation invariant
	Rect (p2, Vector (0.25, -0.5, 0.75), Vector (0.5, 0.25, 0), Vector (0, 0.25, 0.5), 1);
	Rect (p32, Vector (0.25, -0.5, 0.75), Vector (0.5, 0.25, 0), Vector (0, 0.25, 0.5), 4);
	Vector q2 = CollBreakA::Caps (p2, 1, nullptr).pmi, q32 = CollBreakA::Caps (p32, 1, nullptr).pmi;
	CHECK (Rel (q2.x, q32.x, 1e-12)); CHECK (Rel (q2.y, q32.y, 1e-12)); CHECK (Rel (q2.z, q32.z, 1e-12));
	Vector o2 = OldPmi (p2), o32 = OldPmi (p32);
	CHECK (!Rel (o2.y, o32.y, 1e-3));                                // the old rule was not
	for (int n : { 1, 2, 4, 16 }) { // cell-like panel near HB_D2 (0.56 x 0.38 m, 0.03 m dish): old and new PMI
		GeoT g; Rect (g, Vector (-0.28, 0, -0.19), Vector (0.56, 0, 0), Vector (0, 0, 0.38), n, 0.12);
		Vector o = OldPmi (g), w = CollBreakA::Caps (g, 11.38, nullptr).pmi;
		printf ("B9 cell-like panel 0.56 x 0.38 m, %3zu triangles: old pmi (%.4g %.4g %.4g), new pmi (%.4g %.4g %.4g); HB_D2 measured (0.0149 0.0383 0.029)\n", g[0].second.size () / 3, o.x, o.y, o.z, w.x, w.y, w.z);
	}
}

TEST_CASE ("B9 blast cell debris: old and new PMI of the debris a 70 m/s hit makes", "[dmg3P][caps]")
{
	BlastRig r; r.Ship ("A");
	r.B ().Hit (r.K (70));
	r.sdk.simt = 0.02; r.B ().PreStep (0.02, 0.02); r.B ().Post (0.02, 0.02);
	REQUIRE (!r.B ().Debris ().empty ());
	for (auto &d : r.B ().Debris ()) {
		const BFake::V *dv = (const BFake::V *)d.h;
		const MeshT *dm = r.sdk.debrisMesh[dv];
		REQUIRE (dm);
		GeoT geo;
		for (auto &ps : d.row.pose) for (uint16_t g : ps.grp) {
			std::vector<DentVtx> v (dm->grp[g].vtx.size ());
			std::memcpy (v.data (), dm->grp[g].vtx.data (), v.size () * sizeof (DentVtx));
			geo.emplace_back (v, dm->grp[g].idx);
		}
		Vector o = OldPmi (geo), w = CollBreakA::Caps (geo, d.row.mass, nullptr).pmi, c = r.sdk.caps[dv].pmi;
		CHECK (c.x == w.x); CHECK (c.y == w.y); CHECK (c.z == w.z);
		printf ("B9 blast debris '%s' %.4g kg: old pmi (%.4g %.4g %.4g), new pmi (%.4g %.4g %.4g)\n", d.row.name.c_str (), d.row.mass, o.x, o.y, o.z, w.x, w.y, w.z);
	}
}

TEST_CASE ("B1 D1: a long debris name saved as #fnv8 finds its live debris on load; restored debris keep their pair filters, impactor too", "[dmg3P]")
{
	std::string pn (180, 'x'); pn[0] = 'A';
	const std::string dn = pn + "_D1";
	std::vector<DentDebris> rows; std::vector<DentTorn> torn;
	{
		Rig r; uint32_t a = r.Add (pn), b = r.Add ("B");
		CollDamageHit h = r.H (a, 1, 70, 0.9); h.other = b; h.vt = 0;
		r.B ().Hit (h); r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
		REQUIRE (r.B ().Debris ().size () == 1);
		const VesselDamageA *vd = r.S ().Damage (a);
		REQUIRE (vd->d.debris.size () == 1);
		CHECK (vd->d.debris[0].other == "B");                       // B1: the impactor rides on the B row
		std::vector<std::string> lines; DentMath::FormatVessel (vd->d, "  ", lines);
		for (auto &l : lines) CHECK (l.size () <= (size_t)DENT_LINE_MAX);
		DentVesselParser p; for (auto &l : lines) p.Line (l.c_str ());
		DentVesselText o; p.Finish (o);
		REQUIRE (o.debris.size () == 1);
		uint32_t hv = 0;
		CHECK (DentMath::NameHash (o.debris[0].name, hv));
		CHECK (hv == DentMath::Fnv1a (dn.data (), dn.size ()));
		CHECK (o.debris[0].other == "B");
		REQUIRE (o.torn.size () == 1);
		CHECK (DentMath::NameHash (o.torn[0].debris, hv));           // D2: the T row too
		rows = o.debris; torn = o.torn;
	}
	Rig q; uint32_t a = q.Add (pn), b = q.Add ("B");
	BFake::V *dv = q.sdk.Add (dn, "CollDebris"); uint32_t did = q.host.IdOf (dv);
	for (auto &t : torn) q.S ().AddTorn (a, t);
	q.S ().SetDebris (a, rows);
	q.B ().PreStep (3, 0.01); q.B ().Post (3, 0.01);
	REQUIRE (q.B ().Debris ().size () == 1);
	CHECK (q.B ().Debris ()[0].h == dv);                              // D1: the hash finds it
	CHECK (dv->alive);
	CHECK (q.sdk.Calls ("VesselCreate") == 0);
	CHECK (q.B ().Debris ()[0].row.name == dn);
	REQUIRE (q.S ().Damage (a)->d.debris.size () == 1);
	CHECK (q.S ().Damage (a)->d.debris[0].name == dn);
	CHECK (q.B ().Debris ()[0].other == b);
	CHECK (q.S ().noPair.count ({ std::min (a, did), std::max (a, did) }));   // B1: parent
	CHECK (q.S ().noPair.count ({ std::min (b, did), std::max (b, did) }));   // and impactor filters back
	q.sdk.simt = 14; q.B ().PreStep (14, 0.01); q.B ().Post (14, 0.01); // restored pairs count as apart: no stuck delete after load
	CHECK (dv->alive); CHECK (!q.sdk.Logged ("still overlapping"));
	Rig z; uint32_t za = z.Add (pn);                                 // no live debris for the hash: spawned again under a real name
	z.S ().SetDebris (za, rows);
	z.B ().PreStep (3, 0.01); z.B ().Post (3, 0.01);
	REQUIRE (z.sdk.Calls ("VesselCreate") == 1);
	REQUIRE (z.B ().Debris ().size () == 1);
	CHECK (z.B ().Debris ()[0].row.name == dn);
	CHECK (BFake::X (z.B ().Debris ()[0].h)->name == dn);
}

TEST_CASE ("B1: restored blast debris are filtered against their parent and each other again", "[dmg3P][blast]")
{
	std::vector<DentDebris> rows; DentSites ds;
	{
		BlastRig r; uint32_t a = r.Ship ("A");
		r.B ().Hit (r.K (70));
		r.sdk.simt = 0.02; r.B ().PreStep (0.02, 0.02); r.B ().Post (0.02, 0.02);
		REQUIRE (r.B ().Debris ().size () >= 2);
		rows = r.S ().Damage (a)->d.debris; ds = *r.S ().Sites (a, 0);
	}
	BlastRig q; uint32_t a = q.Ship ("A");
	q.S ().SetSites (a, ds);
	std::vector<uint32_t> ids;
	for (auto &row : rows) ids.push_back (q.host.IdOf (q.sdk.Add (row.name, "CollDebris")));
	q.S ().SetDebris (a, rows);
	q.B ().PreStep (0, 0.02); q.B ().Post (0, 0.02);
	REQUIRE (q.B ().Debris ().size () == rows.size ());
	CHECK (q.sdk.Calls ("VesselCreate") == 0);
	for (size_t i = 0; i < ids.size (); i++) {
		CHECK (q.S ().noPair.count ({ std::min (a, ids[i]), std::max (a, ids[i]) }));
		for (size_t j = i + 1; j < ids.size (); j++) CHECK (q.S ().noPair.count ({ std::min (ids[i], ids[j]), std::max (ids[i], ids[j]) }));
	}
	CHECK (q.B ().Pairs ().size () == ids.size () * (ids.size () + 1) / 2);
}

TEST_CASE ("B2: a spawn that fails drops its queued row", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	r.B ().Hit (r.H (a, 1, 70, 0.9));
	REQUIRE (r.B ().Pending () == 1);
	REQUIRE (r.S ().Damage (a)->d.debris.size () == 1);
	r.sdk.mesh.front ().name = "gone";                               // the slot's template has no file now
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	CHECK (r.sdk.Logged ("not loaded, no debris"));
	CHECK (r.sdk.Calls ("VesselCreate") == 0);
	CHECK (r.S ().Damage (a)->d.debris.empty ());
	CHECK (r.B ().Hidden (a, 0, 1));                                 // the part stays torn off
}

TEST_CASE ("B3: the mass cut scales the PMI with the total mass; repair restores it, also past the floor", "[dmg3P][blast]")
{
	BlastRig r; uint32_t a = r.Ship ("A");
	BFake::V *v = r.body.front ().v;
	v->empty = 10000; v->rd.m = 20000; v->rd.pmi = Vector (10, 10, 10); // 10 t of fuel
	Vector ic = Vector (0, 64, 64) * 1000;                           // 1 t at 8 m along x
	CHECK (r.B ().CutMass (a, v, 1000, ic) == 1000);
	CHECK (v->empty == 9000); CHECK (v->rd.m == 19000);
	CHECK (Rel (v->rd.pmi.x, 200000.0 / 19000, 1e-12));
	CHECK (Rel (v->rd.pmi.y, 136000.0 / 19000, 1e-12));              // 7.16, not the 4.0 of the empty mass
	CHECK (Rel (v->rd.pmi.z, 136000.0 / 19000, 1e-12));
	r.B ().Repair (a);
	CHECK (v->empty == 10000); CHECK (v->rd.m == 20000);
	for (double c : { v->rd.pmi.x, v->rd.pmi.y, v->rd.pmi.z }) CHECK (Rel (c, 10, 1e-12));
	CHECK (r.B ().CutMass (a, v, 1000, Vector (0, 1e7, 0)) == 1000); // past the 0.1 pmi0 floor
	CHECK (v->rd.pmi.y == 1.0);
	CHECK (r.sdk.Logged ("Collision blast 'A' PMI floor"));
	r.B ().Repair (a);
	for (double c : { v->rd.pmi.x, v->rd.pmi.y, v->rd.pmi.z }) CHECK (Rel (c, 10, 1e-12)); // the inertia really removed comes back
}

TEST_CASE ("B8: blast debris take the mass the parent lost; at the cap no debris and a mass cap line", "[dmg3P][blast]")
{
	for (double left : { 10.0, 1.0 }) {
		INFO ("left " << left);
		BlastRig r; uint32_t a = r.Ship ("A");
		BFake::V *v = r.body.front ().v;
		REQUIRE (r.B ().CutMass (a, v, BLAST_MASS_CUT * 5000 - left, Vector ()) > 0);
		r.B ().Hit (r.K (70));
		REQUIRE (r.B ().blastBreaks >= 1);
		CHECK (std::fabs (r.B ().MassCut (a) - BLAST_MASS_CUT * 5000) < 1e-9);
		r.sdk.simt = 0.02; r.B ().PreStep (0.02, 0.02); r.B ().Post (0.02, 0.02);
		CHECK (r.sdk.Logged ("Collision blast 'A' mass cap"));
		double m = 0;
		for (auto &d : r.B ().Debris ()) { m += d.row.mass; CHECK (d.row.mass == r.sdk.caps[(const BFake::V *)d.h].mass); }
		CHECK (m <= left + 1e-9);                                    // no mass made
		if (left == 10.0) { REQUIRE (r.B ().Debris ().size () == 1); CHECK (r.B ().Debris ()[0].row.mass == 10.0); }
		else CHECK (r.B ().Debris ().empty ());
	}
}

TEST_CASE ("B4: playback spawns blast cell debris from its recorded row: same vertices, the recorded kick and mass, no parent write", "[dmg3P][blast]")
{
	BlastRig r; uint32_t a = r.Ship ("A");
	r.B ().Hit (r.K (70));
	r.sdk.simt = 0.02; r.B ().PreStep (0.02, 0.02); r.B ().Post (0.02, 0.02);
	REQUIRE (!r.B ().Debris ().empty ());
	const CollDebrisA &d = r.B ().Debris ()[0];
	const BFake::V *dv = (const BFake::V *)d.h;
	DentTorn t;
	t.kind = CBRK_CELL; t.slot = 0; t.key = DentMath::MeshKey ("ship"); t.ngrp = 6; t.nvtx = 6 * 49; t.simt = 0; t.debris = d.row.name; t.kin = true;
	for (auto &rc : d.row.rec) if (rc.p.mode == DENTM_VCUT) t.cells.push_back ((uint32_t)rc.p.P);
	t.c = r.sdk.created[dv].rpos; t.dv = r.sdk.created[dv].rvel; t.dw = r.sdk.spins[dv]; t.mass = r.sdk.caps[dv].mass;
	std::vector<std::string> pay; DentMath::FormatTornEvent (t, pay);
	REQUIRE (pay.size () == 2);                                      // the K payload, then the cells
	std::vector<DentTorn> u (2); bool more = true;
	for (size_t i = 0; i < 2; i++) REQUIRE (DentMath::ParseTornEvent (pay[i].c_str (), u[i], more));
	CHECK (u[0].kin); CHECK (u[0].cells.empty ());
	CHECK (u[1].cells == t.cells);
	DentSites ds = *r.S ().Sites (a, 0);
	BlastRig q; uint32_t b = q.Ship ("A");
	q.body.front ().v->playback = true;
	q.S ().SetSites (b, ds);
	q.B ().Torn (b, u[0]);
	CHECK (q.B ().Pending () == 0);                                  // the kick alone waits for its cells
	q.B ().Torn (b, u[1]);
	CHECK (q.B ().Pending () == 1);
	for (uint32_t g = 0; g < 6; g++) CHECK (!q.B ().Hidden (b, 0, g));  // no hidden groups
	q.B ().PreStep (0, 0.02); q.B ().Post (0, 0.02);
	REQUIRE (q.sdk.Calls ("VesselCreate") == 1);
	REQUIRE (q.B ().Debris ().size () == 1);
	const CollDebrisA &e = q.B ().Debris ()[0];
	const BFake::V *ev = (const BFake::V *)e.h;
	REQUIRE (e.row.pose.size () == d.row.pose.size ());
	const MeshT *lm = r.sdk.debrisMesh[dv], *pm = q.sdk.debrisMesh[ev];
	for (auto &ps : d.row.pose) for (uint16_t g : ps.grp) CHECK (std::memcmp (lm->grp[g].vtx.data (), pm->grp[g].vtx.data (), lm->grp[g].vtx.size () * sizeof (CollVtx)) == 0);
	CHECK ((q.sdk.created[ev].rpos - t.c).length () < 1e-6);
	CHECK ((q.sdk.created[ev].rvel - t.dv).length () < 1e-6);
	CHECK ((q.sdk.spins[ev] - t.dw).length () < 1e-6);
	CHECK (q.sdk.caps[ev].mass == t.mass);
	for (auto &x : q.sdk.states) CHECK (x.first != q.body.front ().v); // the recording moves the parent
	CHECK (q.B ().kicks.empty ());
}

TEST_CASE ("B4: playback part rows use their recorded kick and mass; rows of one debris make one", "[dmg3P]")
{
	Rig r; uint32_t a = r.Add ("A");
	r.body.front ().v->playback = true;
	DentTorn t; t.kind = CBRK_PART; t.slot = 0; t.key = DentMath::MeshKey ("ship"); t.ngrp = 4; t.debris = "A_D1"; t.grp = { 1 };
	t.kin = true; t.dv = Vector (0.5, -2, 3); t.dw = Vector (0, 0.25, 0); t.mass = 17;
	r.B ().Torn (a, t);
	DentTorn t2 = t; t2.simt = 0.5;                                   // a second row of the same debris: one spawn
	r.B ().Torn (a, t2);
	r.B ().PreStep (0, 0.01); r.B ().Post (0, 0.01);
	REQUIRE (r.sdk.Calls ("VesselCreate") == 1);
	const BFake::V *dv = BFake::X (r.B ().Debris ()[0].h);
	CHECK ((r.sdk.created[dv].rvel - t.dv).length () < 1e-9);
	CHECK ((r.sdk.spins[dv] - t.dw).length () < 1e-9);
	CHECK (r.sdk.caps[dv].mass == 17);
}


namespace { // B7: the box hull with an animated 1.33 m panel (group 6) welded onto its +z face
struct HeldRig : BlastRig {
	TestVessel tv; TestModule mod; CollAnim ca;
	HeldRig ()
	{
		CollGroupData g;
		const double s = 4.0 / 6;
		for (int j = 0; j <= 2; j++) for (int i = 0; i <= 2; i++) g.vtx.push_back (CollVtx { (float)(s * i), (float)(s * j), 2.0f, 0, 0, 1, 0, 0 });
		for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) { uint16_t a = (uint16_t)(j * 3 + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + 3), d = (uint16_t)(c + 1); g.idx.insert (g.idx.end (), { a, b, d, a, d, c }); }
		sdk.mesh.front ().grp.push_back (g);
	}
	uint32_t ShipA (const std::string &name)
	{
		tv.coll = &ca; tv.meshGrp = { 7 };
		UINT an = tv.CreateAnimation (0);
		tv.AddAnimationComponent (an, 0, 1, mod.Rot (0, mod.Grp ({ 6 }), 1, _V (0, 0, 2), _V (1, 0, 0), (float)(PI / 2)));
		tv.Step ();
		uint32_t a = Ship (name);
		Body &b = body.back ();
		b.sh.reset (new CollShape ());
		b.sh->Update (&b.mi, 1, ca, tv.anim, tv.nanim, cache);
		host.shape[b.id] = b.sh.get ();
		S ().brk->Shapes (b.id, b.sh.get ());
		return a;
	}
	CollDamageHit P (double vn, double eSpec, double R = 1.2) { CollDamageHit h = K (vn, R); h.c = Vector (0.67, 0.67, 2); h.grp = 6; h.eSpec = eSpec; h.Jn = 0; return h; }
};
}

TEST_CASE ("B5: a split under spin loads only gets no kick: the debris keeps the parent's rotation velocity at its centroid", "[dmg3P][blast]")
{
	static const DentMaterial weak { "weak", 1e4, 0.01, 1, 0, 0, 0.5, false };
	BlastRig r; r.Ship ("A");
	CollDamageHit h = r.K (5, 0.5); h.mat = &weak;
	r.B ().Hit (h);
	r.sdk.simt = 0.02; r.B ().PreStep (0.02, 0.02); r.B ().Post (0.02, 0.02); // the hit's own debris, if any
	size_t n0 = r.B ().Debris ().size ();
	uint64_t b0 = r.B ().blastBreaks;
	const Vector w (0, 0, 60);
	r.body.front ().v->rd.w = w;
	r.sdk.simt = 0.5; r.B ().PreStep (0.5, 0.02); r.B ().Post (0.5, 0.02); // no hit this frame: spin loads only
	REQUIRE (r.B ().blastBreaks > b0);
	r.sdk.simt = 0.52; r.B ().PreStep (0.52, 0.02);
	REQUIRE (r.B ().Debris ().size () > n0);
	for (size_t i = n0; i < r.B ().Debris ().size (); i++) {
		const BFake::V *dv = (const BFake::V *)r.B ().Debris ()[i].h;
		Vector cv = r.sdk.created[dv].rpos, vr = crossp (cv, w);
		CHECK ((r.sdk.created[dv].rvel - vr).length () <= 1e-12 * std::max (1.0, vr.length ())); // no kick from the old hit
		CHECK ((r.sdk.spins[dv] - w).length () <= 1e-12 * w.length ());
	}
}

TEST_CASE ("cfix4 X1: a bump holds the panel, a tumble after it breaks it off without a Reset every frame", "[dmg3P][blast]")
{
	HeldRig r; r.ShipA ("A");
	r.B ().Hit (r.P (5, 300, 0.5));                                  // a 5 m/s bump splits the panel: held below the part speed
	REQUIRE (r.B ().blastRebuilds == 1);
	uint64_t b0 = r.B ().blastBreaks;
	r.body.front ().v->rd.w = Vector (60, 0, 0);                     // a tumble inside BLAST_LIVE that breaks the panel at the 1 % floor: spin loads only
	for (int k = 1; k <= 10; k++) { r.sdk.simt = 0.02 * k; r.B ().PreStep (r.sdk.simt, 0.02); r.B ().Post (r.sdk.simt, 0.02); }
	CHECK (r.B ().blastRebuilds <= 2);
	CHECK (r.B ().blastBreaks > b0);                                 // the spin breaks the panel off
}

TEST_CASE ("B7: a held piece keeps its bonds' health from before the step as W rows; the same held set again is rebuilt the same", "[dmg3P][blast]")
{
	HeldRig r; uint32_t a = r.ShipA ("A");
	r.B ().Hit (r.P (15, 100, 0.3));                                 // weakens the panel's bonds
	for (int k = 0; k < 4; k++) { r.sdk.simt += 0.1; CollDamageHit h = r.P (15, 0, 0.3); h.Jn = 1e5; r.B ().Hit (h); } // pushes break some
	CollBlastA *x = r.B ().Blast (a, 0);
	REQUIRE (x);
	int pc = x->ChunkOfPiece (1);
	REQUIRE (pc >= 0);
	uint32_t key = x->ChunkKey ((uint32_t)pc);
	std::map<uint32_t, double> pre;                                  // the panel's intact bonds: pair -> health share
	for (uint32_t i = 0; i < x->bond.size (); i++) if ((x->bond[i].a == (uint32_t)pc || x->bond[i].b == (uint32_t)pc) && x->Health (i) > 0) {
		uint32_t u = x->ChunkKey (x->bond[i].a), v = x->ChunkKey (x->bond[i].b);
		pre[std::min (u, v) * 65536u + std::max (u, v)] = x->Health (i) / x->bond[i].area;
	}
	REQUIRE (pre.size () >= 2);
	for (auto &p : pre) REQUIRE (p.second < 0.999);                 // weakened: W rows
	CHECK (r.B ().blastBreaks == 0);
	CHECK (r.sdk.Logs ("piece chunks held") == 0);
	r.sdk.simt += 0.1;
	r.B ().Hit (r.P (15, 300, 0.5));                                 // the panel splits off at 15 m/s: held
	CHECK (r.B ().blastBreaks == 0);
	CHECK (r.sdk.Logs ("piece chunks held") == 1);
	CHECK (r.B ().blastRebuilds == 1);
	const std::vector<uint32_t> *wb = r.S ().WeakBonds (a, 0), *kb = r.S ().BrokenBonds (a, 0);
	REQUIRE (wb); REQUIRE (kb);
	std::map<uint32_t, uint32_t> W;
	for (size_t k = 0; k + 1 < wb->size (); k += 2) W[(*wb)[k]] = (*wb)[k + 1];
	for (auto &p : pre) {
		INFO ("pair " << p.first / 65536 << "/" << p.first % 65536);
		REQUIRE (W.count (p.first));                                 // kept as a W row
		CHECK (std::fabs (W[p.first] * 1e-6 - p.second) < 2e-6);     // at its health before the step
		CHECK (!std::binary_search (kb->begin (), kb->end (), p.first)); // never a K row
	}
	x = r.B ().Blast (a, 0);                                         // rebuilt: the panel back on at that health, not new
	REQUIRE (x);
	pc = x->ChunkOfPiece (1);
	for (uint32_t i = 0; i < x->bond.size (); i++) if (x->bond[i].a == (uint32_t)pc || x->bond[i].b == (uint32_t)pc) {
		uint32_t u = x->ChunkKey (x->bond[i].a), v = x->ChunkKey (x->bond[i].b), p = std::min (u, v) * 65536u + std::max (u, v);
		if (pre.count (p)) CHECK (std::fabs (x->Health (i) / x->bond[i].area - pre[p]) < 2e-6);
	}
	CHECK (key == x->ChunkKey ((uint32_t)pc));
	CHECK (!x->gone[pc]);
	auto held = [&] () { std::map<uint32_t, uint32_t> m; const std::vector<uint32_t> *w = r.S ().WeakBonds (a, 0); if (w) for (size_t k = 0; k + 1 < w->size (); k += 2) if (pre.count ((*w)[k])) m[(*w)[k]] = (*w)[k + 1]; return m; };
	std::map<uint32_t, uint32_t> w1 = held ();
	CHECK (w1.size () == pre.size ());
	r.sdk.simt += 0.1;
	r.B ().Hit (r.P (15, 300, 0.5));                                 // the same held set again: rebuilt again, one log line, its W rows stay
	CHECK (r.B ().blastRebuilds == 2);
	CHECK (r.sdk.Logs ("piece chunks held") == 1);
	CHECK (held () == w1);
	CHECK (!r.B ().Blast (a, 0)->gone[pc]);
	r.sdk.simt += 0.1;
	r.B ().PreStep (r.sdk.simt, 0.02); r.B ().Post (r.sdk.simt, 0.02); // and through the next steps
	CHECK (held () == w1);
}

TEST_CASE ("custom-fix2 B7: two 15 m/s hits leave the panel in the structure at the 1 % floor, a load restores the same bonds, a 40 m/s hit tears it off", "[dmg3P][blast]")
{
	HeldRig r; uint32_t a = r.ShipA ("A");
	auto panelPairs = [] (const CollBlastA *x) { std::set<uint32_t> s; int pc = x->ChunkOfPiece (1); for (auto &b : x->bond) if (b.a == (uint32_t)pc || b.b == (uint32_t)pc) { uint32_t u = x->ChunkKey (b.a), v = x->ChunkKey (b.b); s.insert (std::min (u, v) * 65536u + std::max (u, v)); } return s; };
	for (int k = 0; k < 2; k++) {
		INFO ("hit " << k);
		r.sdk.simt += 0.1;
		r.B ().Hit (r.P (15, 300, 0.5));                             // the panel splits off at 15 m/s: held, from full health in one step
		CHECK (r.B ().blastRebuilds == (uint64_t)k + 1);              // every held split: the panel goes back into the structure
		CollBlastA *x = r.B ().Blast (a, 0);
		REQUIRE (x);
		int pc = x->ChunkOfPiece (1);
		REQUIRE (pc >= 0);
		CHECK (!x->gone[pc]);
		std::vector<uint32_t> mc = x->MainChunks ();
		CHECK (std::binary_search (mc.begin (), mc.end (), (uint32_t)pc));
		const std::vector<uint32_t> *wb = r.S ().WeakBonds (a, 0), *kb = r.S ().BrokenBonds (a, 0);
		REQUIRE (wb);
		std::map<uint32_t, uint32_t> W;
		for (size_t j = 0; j + 1 < wb->size (); j += 2) W[(*wb)[j]] = (*wb)[j + 1];
		size_t floor = 0;
		for (uint32_t p : panelPairs (x)) {
			if (kb) CHECK (!std::binary_search (kb->begin (), kb->end (), p)); // never a K row
			if (W.count (p) && W[p] == 10000u) floor++;
		}
		CHECK (floor >= 1);                                          // broken from full health: stored at 1 % of the area
		for (uint32_t i = 0; i < x->bond.size (); i++) if ((x->bond[i].a == (uint32_t)pc || x->bond[i].b == (uint32_t)pc)) CHECK (x->Health (i) > 0); // and live: weak, not broken
	}
	CHECK (r.B ().blastBreaks == 0);
	CHECK (r.sdk.Logs ("piece chunks held") == 1);
	CollBlastA *x = r.B ().Blast (a, 0);
	const DentVesselText &vt = r.S ().Damage (a)->d;                 // save and load in between: the same bonds
	std::vector<std::string> lines; DentMath::FormatVessel (vt, "", lines);
	DentVesselParser p; for (auto &l : lines) p.Line (l.c_str ());
	DentVesselText o; p.Finish (o);
	REQUIRE (o.sites.size () == 1);
	HeldRig q; uint32_t b = q.ShipA ("A");
	q.S ().SetSites (b, o.sites[0]);
	for (auto &k : o.brokenBonds) q.S ().AddBrokenBonds (b, k.first, k.second);
	for (auto &w : o.weakBonds) q.S ().SetWeakBonds (b, w.first, w.second);
	for (auto &rc : o.rec) if (rc.p.mode == DENTM_VCUT) REQUIRE (q.S ().AddCut (b, rc, true));
	q.B ().PreStep (0, 0.02); q.B ().Post (0, 0.02);
	CollBlastA *y = q.B ().Blast (b, 0);
	REQUIRE (y);
	REQUIRE (y->bond.size () == x->bond.size ());
	for (uint32_t i = 0; i < x->bond.size (); i++) CHECK (std::fabs (y->Health (i) - x->Health (i)) <= 2e-6 * x->bond[i].area + 1e-9);
	CHECK (y->Partition () == x->Partition ());
	CHECK (y->BrokenPairs () == x->BrokenPairs ());
	CHECK (y->WeakPairs () == x->WeakPairs ());
	CHECK (y->MainChunks () == x->MainChunks ());
	for (HeldRig *z : { &r, &q }) {                                  // live and loaded: a 40 m/s hit tears the panel off
		uint32_t id = z == &r ? a : b;
		z->sdk.simt += 0.1;
		CollDamageHit h = z->P (40, 800, 0.5); h.id = id; h.h = z->host.Vessel (id);
		z->B ().Hit (h);
		CHECK (z->B ().Hidden (id, 0, 6));
		CollBlastA *w = z->B ().Blast (id, 0);
		REQUIRE (w);
		CHECK (w->gone[w->ChunkOfPiece (1)]);
		const std::vector<uint32_t> *kb = z->S ().BrokenBonds (id, 0);
		REQUIRE (kb);
		size_t k = 0;
		for (uint32_t pp : panelPairs (w)) if (std::binary_search (kb->begin (), kb->end (), pp)) k++;
		CHECK (k >= 1);                                              // now K rows
		CHECK (z->B ().Pending () >= 1);
	}
}

TEST_CASE ("custom-fix2 B4 D2: a 3-cell row with a normal kick and a long parent name round-trips through the side file; its K and cell payloads and the part row play back one debris", "[dmg3P][blast]")
{
	for (int variant = 0; variant < 2; variant++) {                  // 0: every row keeps the name; 1: longer, the K and part rows write its #fnv8
		const std::string parent = std::string (variant ? "A-very-long-parent-vessel-name-for-the-recorder-rows-of-blast-debris-1234567" : "A-very-long-parent-vessel-name-for-the-recorder-rows-of-blast-debris");
		INFO ("variant " << variant << " parent " << parent.size () << " chars");
		DentSites ds;
		{
			HeldRig r; r.ShipA (parent);
			r.B ().Hit (r.P (1, 0, 0.1));                            // builds the slot: its sites
			REQUIRE (r.S ().Sites (r.a, 0));
			ds = *r.S ().Sites (r.a, 0);
		}
		REQUIRE (ds.s.size () >= 3);
		DentTorn t;                                                  // as SpawnCells records it: the cell row, then the piece's part row
		t.kind = CBRK_CELL; t.slot = 0; t.key = DentMath::MeshKey ("ship"); t.ngrp = 7; t.nvtx = 6 * 49 + 9; t.simt = 0.30000000000000004; t.debris = parent + "_D1";
		t.kin = true; t.dv = Vector (0.812345678, -2.50000001, 3.75); t.dw = Vector (0.0123456789, -0.25, 1.5); t.mass = 91.2345678;
		std::vector<uint32_t> near;
		for (uint32_t c = 0; c < ds.s.size (); c++) near.push_back (c);
		std::sort (near.begin (), near.end (), [&] (uint32_t i, uint32_t j) { return (ds.s[i] - Vector (0.67, 0.67, 2)).length2 () < (ds.s[j] - Vector (0.67, 0.67, 2)).length2 (); });
		t.cells = { near[0], near[1], near[2] }; std::sort (t.cells.begin (), t.cells.end ());
		t.pieces = { 1 }; t.c = Vector (0.712345678, 0.698765432, 2.0123456); t.crushed = false;
		DentTorn u = t;
		u.kind = CBRK_PART; u.grp = { 6 }; u.cells.clear (); u.pieces.clear (); u.c = Vector ();
		std::vector<std::string> l;
		CollSide::Torn (0.3, 0, t, l);
		CollSide::Torn (0.3, 0, u, l);
		CHECK (l.size () >= 3);                                      // K payload, cell payload, part row
		std::string text = CollSide::Header ("X") + "\n" + CollSide::Vdef (0, 0, parent, "ShuttlePB") + "\n";
		for (auto &x : l) {
			INFO (x);
			size_t p3 = 0; for (int k = 0; k < 3; k++) p3 = x.find (' ', p3) + 1; // payload after "<t> T <alias> "
			CHECK (x.size () - p3 <= (size_t)DENT_EVENT_MAX);
			text += x + "\n";
		}
		CollSideFile f;
		REQUIRE (CollSide::Parse (text, f));
		REQUIRE (f.ev.size () == l.size ());
		size_t hashed = 0, plain = 0, kin = 0;
		DentTorn cells;
		for (auto &e : f.ev) {
			REQUIRE (e.kind == 'T');
			uint32_t h = 0;
			if (DentMath::NameHash (e.torn.debris, h)) { CHECK (h == DentMath::Fnv1a (t.debris.data (), t.debris.size ())); hashed++; } else { CHECK (e.torn.debris == t.debris); plain++; }
			if (e.torn.kind == CBRK_CELL && e.torn.kin) { kin++; CHECK (e.torn.cells.empty ()); CHECK (e.torn.mass == t.mass); CHECK (e.torn.dv.x == t.dv.x); CHECK (e.torn.dw.z == t.dw.z); }
			if (e.torn.kind == CBRK_CELL && !e.torn.kin) { cells.cells.insert (cells.cells.end (), e.torn.cells.begin (), e.torn.cells.end ()); cells.pieces.insert (cells.pieces.end (), e.torn.pieces.begin (), e.torn.pieces.end ()); cells.c = e.torn.c; }
		}
		CHECK (kin == 1);
		CHECK (cells.cells == t.cells); CHECK (cells.pieces == t.pieces); // round trip
		CHECK (cells.c.x == t.c.x); CHECK (cells.c.y == t.c.y); CHECK (cells.c.z == t.c.z);
		if (variant == 0) CHECK (hashed == 0);                       // the head keeps its room
		else { CHECK (hashed >= 1); CHECK (plain >= 1); }            // rows of one debris with both name forms
		HeldRig q; uint32_t b = q.ShipA (parent);
		q.body.front ().v->playback = true;
		q.S ().SetSites (b, ds);
		for (auto &e : f.ev) q.B ().Torn (b, e.torn);
		CHECK (q.B ().Hidden (b, 0, 6));
		q.sdk.simt = 0.3; q.B ().PreStep (0.3, 0.02); q.B ().Post (0.3, 0.02);
		REQUIRE (q.sdk.Calls ("VesselCreate") == 1);                 // one debris
		REQUIRE (q.B ().Debris ().size () == 1);
		const CollDebrisA &d = q.B ().Debris ()[0];
		const BFake::V *dv = (const BFake::V *)d.h;
		CHECK (q.sdk.caps[dv].mass == t.mass);
		CHECK ((q.sdk.created[dv].rvel - t.dv).length () < 1e-6);
		size_t keep = 0; bool panel = false;
		for (auto &rc : d.row.rec) if (rc.p.mode == DENTM_VCUT && (rc.p.bits & DENTC_KEEP)) keep++;
		for (auto &ps : d.row.pose) for (uint16_t g : ps.grp) if (g == 6) panel = true;
		CHECK (keep == 3); CHECK (panel);                             // the cells and the panel in one
	}
}

TEST_CASE ("custom-fix3 B4: a cell debris whose cell payloads are lost still spawns from its part row", "[dmg3P][blast]")
{
	const std::string parent = "ShipWithLostCells";
	DentSites ds;
	{
		HeldRig r; r.ShipA (parent);
		r.B ().Hit (r.P (1, 0, 0.1));
		REQUIRE (r.S ().Sites (r.a, 0));
		ds = *r.S ().Sites (r.a, 0);
	}
	DentTorn t;                                                  // the K payload only: kick and mass, no cells
	t.kind = CBRK_CELL; t.slot = 0; t.key = DentMath::MeshKey ("ship"); t.ngrp = 7; t.nvtx = 6 * 49 + 9; t.simt = 0.3; t.debris = parent + "_D1";
	t.kin = true; t.dv = Vector (0.5, -2.0, 3.0); t.dw = Vector (0.01, -0.2, 1.0); t.mass = 50.0;
	DentTorn u = t;                                              // the piece's part row of the same debris
	u.kind = CBRK_PART; u.grp = { 6 };
	HeldRig q; uint32_t b = q.ShipA (parent);
	q.body.front ().v->playback = true;
	q.S ().SetSites (b, ds);
	q.B ().Torn (b, t);
	q.B ().Torn (b, u);
	CHECK (q.B ().Hidden (b, 0, 6));
	q.sdk.simt = 0.3; q.B ().PreStep (0.3, 0.02); q.B ().Post (0.3, 0.02);
	REQUIRE (q.sdk.Calls ("VesselCreate") == 1);                 // the part row spawned the piece
	REQUIRE (q.B ().Debris ().size () == 1);
	bool panel = false;
	for (auto &ps : q.B ().Debris ()[0].row.pose) for (uint16_t g : ps.grp) if (g == 6) panel = true;
	CHECK (panel);
}
