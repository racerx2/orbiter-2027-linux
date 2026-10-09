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
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
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
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
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
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
	CHECK (r.sdk.flagCalls > f0);
	uint64_t f1 = r.sdk.flagCalls;
	r.B ().Pass ();
	CHECK (r.sdk.flagCalls > f1);
	r.body.front ().v->visual = 2;
	uint64_t f2 = r.sdk.flagCalls;
	r.B ().PreStep (0.1); r.B ().Post (0.1, 0.01);
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
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
	REQUIRE (r.sdk.Calls ("VesselCreate") == 1);
	REQUIRE (r.B ().Debris ().size () == 1);
	const CollDebrisA &d = r.B ().Debris ()[0];
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
	r.sdk.simt = 1; r.B ().PreStep (1); r.B ().Post (1, 0.01);
	CHECK (r.B ().Debris ().size () == 2);
	CHECK (r.sdk.Calls ("VesselDelete") == 1);
	CHECK (!dv->alive);
	CHECK (!m->freed);
	r.B ().DropVessel (d.id, d.h);
	r.B ().PreStep (1.1); r.B ().Post (1.1, 0.01);
	CHECK (m->freed);
	r.B ().End ();
	for (auto &x : r.sdk.mesh) if (x.copy) CHECK (x.freed);
}

TEST_CASE ("P8 missing CollDebris.cfg: hide only, one log line", "[dmg3P]")
{
	Rig r; r.sdk.cfgDebris = false; uint32_t a = r.Add ("A"), b = r.Add ("B");
	r.B ().Hit (r.H (a, 1, 70, 0.9)); r.B ().Hit (r.H (b, 1, 70, 0.9));
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
	CHECK (r.B ().Hidden (a, 0, 1));
	CHECK (r.sdk.Calls ("VesselCreate") == 0);
	CHECK (r.sdk.Logs ("CollDebris.cfg missing") == 1);
}

TEST_CASE ("P9 load: torn rows adopted, debris rebuilt bitwise, orphans deleted", "[dmg3P]")
{
	std::vector<DentTorn> torn; std::vector<DentDebris> rows; uint32_t fnv = 0; std::vector<CollVtx> live;
	{
		Rig r; uint32_t a = r.Add ("A");
		r.B ().Hit (r.H (a, 1, 70, 0.9)); r.B ().PreStep (0); r.B ().Post (0, 0.01);
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
	r.B ().PreStep (3); r.B ().Post (3, 0.01);
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
	q.B ().PreStep (3); q.B ().Post (3, 0.01);
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
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
	CHECK (r.sdk.Calls ("VesselCreate") == 1);
	CHECK (r.sdk.Count ().n[CSK_STATE] == w0 + 2);      // create and setup of the debris only
}

TEST_CASE ("P11 idle: no hit, no P call", "[dmg3P]")
{
	Rig r; r.Add ("A"); r.Add ("B");
	r.B ().PreStep (0); r.B ().Post (0, 0.01); r.B ().Pass (); r.B ().PreStep (1); r.B ().Post (1, 0.01);
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
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
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
		r.B ().Hit (r.T (555, 70)); r.B ().PreStep (0); r.B ().Post (0, 0.01);
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
	r.B ().PreStep (3); r.B ().Post (3, 0.01);
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
		r.B ().Hit (r.T (555, 70)); r.B ().PreStep (0); r.B ().Post (0, 0.01);
		REQUIRE (r.B ().Debris ().size () == 1);
		for (auto &ps : r.B ().Debris ()[0].row.pose) CHECK (std::find (ps.grp.begin (), ps.grp.end (), 8) == ps.grp.end ());
		bool row8 = false; for (auto &t : r.S ().Damage (r.a)->d.torn) for (uint16_t g : t.grp) if (g == 8) row8 = true;
		CHECK (row8);
	}
	{
		Rig r; r.sdk.mesh.front ().grp[1] = CollGroupData (); Quad (r.sdk.mesh.front ().grp[1], 5.5, -0.2, 0.2, 0.4, 4);
		uint32_t a = r.Add ("A");
		r.B ().Hit (r.H (a, 1, 70, 0.9)); r.B ().PreStep (0); r.B ().Post (0, 0.01);
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
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
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
	r.B ().PreStep (0); r.B ().Post (0, 0.01);
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
	r.B ().PreStep (0.02); r.B ().Post (0.02, 0.02);
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
	CHECK (ns == r.B ().kicks.size ());
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
	CHECK ((vlast - r.B ().kicks.back ().dv).length () < 1e-12);   // each write: rd.v (unchanged in the fake) + its kick
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
	r.sdk.simt = 1.0; r.B ().PreStep (1.0); r.B ().Post (1.0, 0.02);
	CHECK (r.B ().blastSteps == n0 + 1);
	r.sdk.simt = 1.9; r.B ().PreStep (1.9); r.B ().Post (1.9, 0.02);
	CHECK (r.B ().blastSteps == n0 + 2);
	r.sdk.simt = 2.5; r.B ().PreStep (2.5); r.B ().Post (2.5, 0.02);
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
	q.B ().PreStep (0); q.B ().Post (0, 0.02);                                           // first post-step after load
	CHECK (std::fabs (q.B ().MassCut (b) - cut) <= 1e-9 * cut);
	CHECK (std::fabs (q.body.front ().v->empty - (5000 - cut)) <= 1e-9 * 5000);
	q.B ().PreStep (0.02); q.B ().Post (0.02, 0.02);
	CHECK (std::fabs (q.B ().MassCut (b) - cut) <= 1e-9 * cut);   // once per load
	r.B ().Repair (a);
	CHECK (r.body.front ().v->empty == 5000); CHECK (r.body.front ().v->rd.m == 5000);
	for (double c : { r.body.front ().v->rd.pmi.x, r.body.front ().v->rd.pmi.y, r.body.front ().v->rd.pmi.z }) CHECK (std::fabs (c - 2.7) < 1e-9); // PMI back
	CHECK (r.B ().MassCut (a) == 0);
	BlastRig z; uint32_t e = z.Ship ("A");
	z.B ().PreStep (0); z.B ().Post (0, 0.02);
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
		r.B ().PreStep (0.02); r.B ().Post (0.02, 0.02);
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
		q.B ().PreStep (0); q.B ().Post (0, 0.02);                                       // load: built and restored without a hit
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
		r.B ().PreStep (0.02); r.B ().Post (0.02, 0.02);
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
