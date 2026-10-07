// not upstream: E3-U7 to E3-U15 on a local fake SDK: events to dents, collider re-apply, thrust cut, notices, visual sync, block, recorder, lifecycle (Design CA E3 12.1)
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "CollAnimTest.h"
#include "CollDamageA.h"
#include "CollApiA.h"
#include "CollisionAPI.h"

static_assert (sizeof (COLLA_HDR) == 12, "COLLA_HDR is 12 bytes");
static_assert (offsetof (COLLA_DAMAGEINFO, hdr) == 0 && offsetof (COLLA_CONTACTINFO, hdr) == 0, "header first");

namespace {

// the core's tank, thruster and client-mesh semantics this file needs (E2 1.5 subset)
class DFake final : public CollSdk {
public:
	struct Tk { double max, mass; };
	struct Th { Tk *tank = nullptr; };
	struct V {
		std::string name, cls; double size = 10, mass = 1000, empty = 500; bool playback = false, recording = false, alive = true;
		std::deque<Th> th; std::vector<Th *> thList; std::deque<Tk> tk; std::vector<Tk *> tkList;
		int visual = 0; std::map<uint32_t, std::vector<std::vector<DentVtx>>> dev;
	};
	DFake (bool imgui = false) : CollSdk (imgui) {}
	std::deque<V> ves; std::vector<V *> list;
	int damageModel = 1; double simt = 0; int readCode = 0, writeCode = 0; uint64_t calls = 0;
	std::function<int (CollH, int, void *)> reply;
	std::vector<std::string> log, scnOut, scnIn; size_t scnPos = 0; std::map<std::string, std::string> files; std::string annotation;
	V *Add (const std::string &name, const std::string &cls) { ves.emplace_back (); V *v = &ves.back (); v->name = name, v->cls = cls; list.push_back (v); return v; }
	Tk *AddTank (V *v, double max, double mass) { v->tk.push_back ({ max, mass }); v->tkList.push_back (&v->tk.back ()); return &v->tk.back (); }
	Th *AddThruster (V *v, Tk *t) { v->th.push_back ({ t }); v->thList.push_back (&v->th.back ()); return &v->th.back (); }
	static V *X (CollH h) { return (V *)h; }
	uint32_t VesselCount () override { calls++; return (uint32_t)list.size (); }
	CollH Vessel (uint32_t i) override { calls++; return i < list.size () ? list[i] : nullptr; }
	bool IsVessel (CollH h) override { calls++; for (V *v : list) if (v == h) return true; return false; }
	int ObjType (CollH) override { return 10; }
	std::string Name (CollH h) override { calls++; return X (h)->name; }
	std::string ClassName (CollH h) override { calls++; return h ? X (h)->cls : ""; }
	double Size (CollH h) override { return X (h)->size; }
	void GlobalState (CollH, Vector &, Vector &, Matrix &) override {}
	uint32_t GbodyCount () override { return 0; }
	CollH Gbody (uint32_t) override { return nullptr; }
	uint32_t BaseCount (CollH) override { return 0; }
	CollH Base (CollH, uint32_t) override { return nullptr; }
	void BaseEquPos (CollH, double &, double &, double &) override {}
	double Elevation (CollH, double, double) override { return 0; }
	double PlanetPeriod (CollH) override { return 0; }
	double Mass (CollH h) override { V *v = X (h); double m = v->mass; for (Tk *t : v->tkList) m += t->mass; return m; }
	double SimTime () override { return simt; }
	double SimMJD () override { return 0; }
	double SysTime () override { return simt; }
	double Warp () override { return 1; }
	void ReadVessel (CollH, CollVesselRead &, uint32_t) override {}
	double EmptyMass (CollH h) override { return X (h)->empty; }
	bool Recording (CollH h) override { calls++; return X (h)->recording; }
	bool Playback (CollH h) override { calls++; return X (h)->playback; }
	int DamageModel (CollH) override { calls++; return damageModel; }
	uint32_t MeshCount (CollH) override { return 0; }
	CollH MeshTemplate (CollH, uint32_t) override { return nullptr; }
	const char *MeshName (CollH, uint32_t) override { return ""; }
	Vector MeshOffset (CollH, uint32_t) override { return Vector (); }
	uint16_t MeshVisMode (CollH, uint32_t) override { return 1; }
	const char *TplName (CollH) override { return nullptr; }
	uint32_t TplGroups (CollH) override { return 0; }
	bool TplGroup (CollH, uint32_t, CollTplGroup &) override { return false; }
	uint32_t Anims (CollH, const ANIMATION **) override { return 0; }
	uint32_t DockCount (CollH) override { return 0; }
	bool Dock (CollH, uint32_t, CollPortInfo &) override { return false; }
	uint32_t AttachCount (CollH, bool) override { return 0; }
	bool Attach (CollH, bool, uint32_t, CollAttInfo &) override { return false; }
	uint32_t ThrusterCount (CollH h) override { calls++; return (uint32_t)X (h)->thList.size (); }
	CollH Thruster (CollH h, uint32_t i) override { calls++; return X (h)->thList[i]; }
	CollH ThrusterTank (CollH, CollH th) override { calls++; return ((Th *)th)->tank; }
	uint32_t TankCount (CollH h) override { calls++; return (uint32_t)X (h)->tkList.size (); }
	CollH Tank (CollH h, uint32_t i) override { calls++; return i < X (h)->tkList.size () ? X (h)->tkList[i] : nullptr; }
	double TankMass (CollH, CollH t) override { calls++; return ((Tk *)t)->mass; }
	double TankMaxMass (CollH, CollH t) override { calls++; return ((Tk *)t)->max; }
	CollH Visual (CollH h) override { calls++; return X (h)->visual ? (CollH)((char *)X (h) + X (h)->visual) : nullptr; }
	CollH DevMesh (CollH h, CollH vis, uint32_t i) override { REQUIRE (vis); auto it = X (h)->dev.find (i); return it == X (h)->dev.end () ? nullptr : &it->second; }
	int ReadVtx (CollH dm, uint32_t g, const uint16_t *idx, uint32_t n, DentVtx *out) override
	{
		if (readCode) return readCode;
		auto &m = *(std::vector<std::vector<DentVtx>> *)dm;
		if (g >= m.size ()) return 1;
		for (uint32_t k = 0; k < n; k++) { if (idx[k] >= m[g].size ()) return 1; out[k] = m[g][idx[k]]; }
		return 0;
	}
	bool ClientCore () override { return false; }
	int ClientMatrix (int, CollH, uint32_t, uint32_t, float *) override { return -1; }
	std::string Resolve (const std::string &p) override { return p; }
	bool ReadText (const std::string &p, std::string &out) override
	{
		auto it = files.find (p);
		if (it != files.end ()) { out = it->second; return true; }
		std::ifstream f (p);
		if (!f) return false;
		std::stringstream s; s << f.rdbuf (); out = s.str ();
		return true;
	}
	std::vector<std::string> ListDir (const std::string &) override { return {}; }
	bool CfgString (const char *, int, const char *, std::string &) override { return false; }
	bool CfgInt (const char *, int, const char *, int &) override { return false; }
	bool CfgReal (const char *, int, const char *, double &) override { return false; }
	bool CfgBool (const char *, int, const char *, bool &) override { return false; }
	bool ScnLine (CollH, std::string &l) override { if (scnPos >= scnIn.size () || CollKey::IEqual (scnIn[scnPos], "END")) { scnPos++; return false; } l = scnIn[scnPos++]; return true; }
	void ScnWrite (CollH, const std::string &l) override { scnOut.push_back (l); }
	void Log (int, const char *m) override { calls++; log.push_back (m); }
	bool Logged (const std::string &s) const { for (auto &l : log) if (l.find (s) != std::string::npos) return true; return false; }
protected:
	void DoSetState (CollH, const CollStateWrite &) override {}
	void DoSetAttitude (CollH, const Matrix &) override {}
	void DoSetSpin (CollH, const Vector &) override {}
	void DoAddForce (CollH, const Vector &, const Vector &) override {}
	void DoSetTank (CollH, CollH th, CollH tank) override { ((Th *)th)->tank = (Tk *)tank; }
	CollH DoCreateTank (CollH h, double max, double mass) override { return AddTank (X (h), max, mass); }
	void DoDelTank (CollH h, CollH tank) override
	{
		V *v = X (h);
		for (Th *t : v->thList) if (t->tank == tank) t->tank = nullptr;
		for (size_t i = 0; i < v->tkList.size (); i++) if (v->tkList[i] == tank) { v->tkList.erase (v->tkList.begin () + i); break; }
	}
	void DoSetTankMass (CollH, CollH tank, double m) override { ((Tk *)tank)->mass = m; }
	void DoSetWarp (double) override {}
	int DoWriteVtx (CollH dm, uint32_t g, const uint16_t *idx, uint32_t n, const DentVtx *vtx) override
	{
		if (writeCode) return writeCode;
		auto &m = *(std::vector<std::vector<DentVtx>> *)dm;
		if (g >= m.size ()) return 1;
		for (uint32_t k = 0; k < n; k++) { if (idx[k] >= m[g].size ()) return 1; std::memcpy (&m[g][idx[k]], &vtx[k], 24); }
		return 0;
	}
	int DoSetClientMatrix (int, CollH, uint32_t, uint32_t, const float *) override { return 0; }
	bool DoProbe (CollH, uint32_t) override { return true; }
	bool DoNotify (CollH v, int prm, void *payload, int &r) override { r = reply ? reply (v, prm, payload) : 0; return true; }
	void DoNotification (int, const char *, const char *) override {}
	void DoAnnotation (const char *t) override { annotation = t; }
	int DoRegisterCmd (const char *, const char *, CollCmdFn, void *) override { return 1; }
	void DoUnregisterCmd (int) override {}
	bool DoOpenDialog (void *) override { return true; }
};

class DHost final : public CollDmgHost {
public:
	explicit DHost (DFake &f) : f (f) {}
	DFake &f; std::vector<CollH> ids; std::map<uint32_t, CollShape *> shape; std::map<uint32_t, std::vector<CollDmgSlot>> slots;
	std::vector<CollDmgBaseObj> bases; int rebuilt = 0, slotReads = 0;
	CollH Vessel (uint32_t id) override { return id < ids.size () && ids[id] && DFake::X (ids[id])->alive ? ids[id] : nullptr; }
	uint32_t IdOf (CollH h) override { for (size_t i = 0; i < ids.size (); i++) if (ids[i] == h) return (uint32_t)i; ids.push_back (h); return (uint32_t)ids.size () - 1; }
	CollShape *Shape (uint32_t id) override { auto it = shape.find (id); return it == shape.end () ? nullptr : it->second; }
	uint32_t SlotCount (uint32_t id) override { return (uint32_t)slots[id].size (); }
	bool Slot (uint32_t id, uint32_t m, CollDmgSlot &out) override { slotReads++; auto &s = slots[id]; if (m >= s.size ()) return false; out = s[m]; return true; }
	bool SlotNow (uint32_t id, uint32_t m, uint32_t &serial) override { auto &s = slots[id]; if (m >= s.size () || !s[m].present) return false; serial = s[m].serial; return true; }
	void ClientMeshRebuilt (uint32_t, uint32_t) override { rebuilt++; }
	void WantSlots (uint32_t, bool) override {}
	double DestroyEnergy (uint32_t) override { return -1; }
	bool BaseObject (int planet, int base, int obj, CollDmgBaseObj &out) override
	{
		for (auto &b : bases) if (b.planetIdx == planet && b.baseIdx == base && (int)b.obj == obj) { out = b; return true; }
		return false;
	}
	void Bases (std::vector<CollDmgBaseObj> &all) override { all = bases; }
};

std::shared_ptr<CollRestMesh> Plate () // 21 x 21 grid over [-5, 5]^2 in z = 0, outward +z
{
	auto m = std::make_shared<CollRestMesh> ();
	m->name = "plate";
	m->grp.resize (1);
	CollGroupData &g = m->grp[0];
	for (int j = 0; j <= 20; j++) for (int i = 0; i <= 20; i++) g.vtx.push_back (CollVtx { -5.0f + 0.5f * i, -5.0f + 0.5f * j, 0, 0, 0, 1, 0, 0 });
	for (int j = 0; j < 20; j++) for (int i = 0; i < 20; i++) {
		uint16_t a = (uint16_t)(j * 21 + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + 21), d = (uint16_t)(c + 1);
		g.idx.insert (g.idx.end (), { a, b, c, b, d, c });
	}
	m->nvtx = (uint32_t)g.vtx.size ();
	return m;
}

struct Rig {
	DFake sdk; DHost host { sdk }; CollCfgValues cfg; CollDmgSession s { sdk, host, cfg };
	std::shared_ptr<CollRestMesh> plate = Plate ();
	struct Body { DFake::V *v; uint32_t id; std::unique_ptr<CollShape> sh; CollAnim ca; CollMeshInfo mi; std::unique_ptr<TestVessel> tv; std::unique_ptr<TestModule> mod; };
	std::deque<Body> body; CollTemplateCache cache;
	Rig () { cfg.logLevel = 1; }
	uint32_t Add (const std::string &name, const std::string &cls = "ShuttlePB")
	{
		body.emplace_back ();
		Body &b = body.back ();
		b.v = sdk.Add (name, cls);
		b.id = host.IdOf (b.v);
		b.sh.reset (new CollShape ());
		b.mi.present = b.mi.collide = true, b.mi.serial = 1, b.mi.key = "plate", b.mi.rest = plate;
		host.shape[b.id] = b.sh.get ();
		host.slots[b.id] = { CollDmgSlot { true, DentMath::MeshKey ("plate"), 1, plate->nvtx, plate, "plate", 1 } };
		return b.id;
	}
	uint32_t AddNosed (const std::string &name) // body plate (group 0) and a small animated nose plate 0.2 m in front (group 1): two parts
	{
		uint32_t id = Add (name);
		Body &b = body.back ();
		auto m = std::make_shared<CollRestMesh> (*plate);
		m->name = "nosed";
		m->grp.resize (2);
		CollGroupData &g = m->grp[1];
		for (int j = 0; j <= 4; j++) for (int i = 0; i <= 4; i++) g.vtx.push_back (CollVtx { -0.5f + 0.25f * i, -0.5f + 0.25f * j, 0.2f, 0, 0, 1, 0, 0 });
		for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {
			uint16_t a = (uint16_t)(j * 5 + i), c1 = (uint16_t)(a + 1), c = (uint16_t)(a + 5), d = (uint16_t)(c + 1);
			g.idx.insert (g.idx.end (), { a, c1, c, c1, d, c });
		}
		m->nvtx = (uint32_t)(m->grp[0].vtx.size () + g.vtx.size ());
		b.mi.key = "nosed", b.mi.rest = m;
		b.tv.reset (new TestVessel ()), b.mod.reset (new TestModule ());
		b.tv->coll = &b.ca;
		b.tv->meshGrp = { 2 };
		UINT an = b.tv->CreateAnimation (0);
		b.tv->AddAnimationComponent (an, 0, 1, b.mod->Lin (0, b.mod->Grp ({1}), 1, _V(0,0,1)));
		host.slots[id] = { CollDmgSlot { true, DentMath::MeshKey ("nosed"), 2, m->nvtx, m, "nosed", 1 } };
		return id;
	}
	Body &B (uint32_t id) { for (auto &b : body) if (b.id == id) return b; return body.front (); }
	void Begin (CollStoreBlock &&blk = CollStoreBlock ()) { s.Begin (std::move (blk)); }
	void Frame (const std::vector<CollImpactEvent> &ev = {})
	{
		for (auto &b : body) {
			if (b.tv) b.tv->Step ();
			b.sh->Update (&b.mi, 1, b.ca, b.tv ? b.tv->anim : nullptr, b.tv ? b.tv->nanim : 0, cache);
		}
		for (auto &b : body) s.ShapesUpdated (b.id, b.sh.get (), std::vector<CollDmgSlotEv> ());
		s.PrePhysics ();
		s.Commit (ev, sdk.simt);
		s.SendNotices ();
		s.EndFrame ();
		s.PostStep ();
		sdk.simt += 0.02;
	}
};

CollImpactEvent Hit (uint32_t a, int planet, uint32_t bId, double vn, double dKE, bool first = true)
{
	CollImpactEvent e {};
	e.s[0].owner = CollOwnerRef { a, -1, -1, -1, -1 };
	e.s[0].mesh = 0, e.s[0].grp = 0, e.s[0].tri = 0, e.s[0].c = Vector (0.1, 0.1, 0), e.s[0].n = Vector (0, 0, 1), e.s[0].a = 0.1;
	if (planet < 0) e.s[1].owner = CollOwnerRef { bId, -1, -1, -1, -1 };
	else e.s[1].owner = CollOwnerRef { 0, planet, 0, (int)bId, 0 };
	e.s[1].mesh = 0, e.s[1].grp = 0, e.s[1].c = Vector (0.1, 0.1, 0), e.s[1].n = Vector (0, 0, 1), e.s[1].a = 0.1;
	e.t = 1, e.dKE = dKE, e.Wf = 0, e.vn = vn, e.flags = first ? COLLEV_FIRST : 0;
	return e;
}

// the collider equals rest plus each record once, in order (bitwise against a fresh application)
bool ColliderExact (Rig &r, uint32_t id)
{
	auto &b = r.B (id);
	CollShape fresh;
	CollMeshInfo mi = b.mi;
	CollAnim ca;
	CollTemplateCache cache;
	fresh.Update (&mi, 1, ca, nullptr, 0, cache);
	const VesselDamageA *v = r.s.Damage (id);
	if (v) for (const DentRecord &x : v->d.rec) { std::vector<uint32_t> g (x.grp.begin (), x.grp.end ()); fresh.ApplyDent (0, g.data (), g.size (), DentMath::Field, &x); }
	if (fresh.nPart () != b.sh->nPart ()) return false;
	for (uint32_t p = 0; p < fresh.nPart (); p++) {
		const CollGeom &A = fresh.Part (p).Geom (), &B = b.sh->Part (p).Geom ();
		if (A.vtx.size () != B.vtx.size ()) return false;
		for (size_t i = 0; i < A.vtx.size (); i++) if (std::memcmp (&A.vtx[i], &B.vtx[i], sizeof (Vector))) return false;
	}
	return true;
}

std::vector<std::vector<DentVtx>> ClientRest (const CollRestMesh &m)
{
	std::vector<std::vector<DentVtx>> g (m.grp.size ());
	for (size_t i = 0; i < m.grp.size (); i++) { g[i].resize (m.grp[i].vtx.size ()); std::memcpy (g[i].data (), m.grp[i].vtx.data (), g[i].size () * sizeof (DentVtx)); }
	return g;
}

}

TEST_CASE ("E3-U11 event pipeline")
{
	Rig r;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	r.host.bases.push_back (CollDmgBaseObj { "Moon", "Brighton Beach", "BLOCK", 0, 0, 1, DENTB_BLOCK, Vector (10, 10, 10), -60.6, -35, 0, nullptr, (CollH)0x77 });
	r.Begin ();
	r.Frame ();
	SECTION ("vn 1.0: nothing") {
		r.Frame ({ Hit (a, -1, b, 1.0, 1.5e4) });
		CHECK (r.s.Damage (a) == nullptr);
	}
	SECTION ("FIRST at 10 m/s: records and the energy of D4 2.1") {
		r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
		const VesselDamageA *v = r.s.Damage (a);
		REQUIRE (v);
		double E[2], ea[2];
		const DentMaterial &m = DentMath::DefaultMaterial (-1);
		DentMath::SplitEnergy (3.0e4, 0, 10.0, true, m, m, E, ea);
		CHECK (v->d.eabs == ea[0]);
		CHECK (v->d.rec.size () >= 1);
		CHECK (r.s.Damage (b)->d.eabs == ea[1]);
		CHECK (ColliderExact (r, a));
		CHECK (ColliderExact (r, b));
		CHECK (r.sdk.Logged ("Collision dent t="));
	}
	SECTION ("playback side gets nothing") {
		r.B (a).v->playback = true;
		r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
		CHECK (r.s.Damage (a) == nullptr);
		CHECK (r.s.Damage (b) != nullptr);
	}
	SECTION ("building side: eabs only") {
		r.Frame ({ Hit (a, 0, 1, 10.0, 3.0e4) });
		REQUIRE (r.s.Buildings ().size () == 1);
		CHECK (r.s.Buildings ().begin ()->second.eabs > 0);
		double e; uint32_t f;
		CHECK (r.s.GetBuildingDamage ("Moon:Brighton Beach", 1, &e, &f) == 1);
		CHECK (e > 0);
	}
	SECTION ("destroyed exactly at the threshold") {
		double E[2], ea[2];
		const DentMaterial &m = DentMath::DefaultMaterial (-1);
		DentMath::SplitEnergy (1.0e6, 0, 10.0, true, m, m, E, ea);
		r.cfg.destroyEnergy = ea[0] / 500.0; // empty mass 500 kg
		r.Frame ({ Hit (a, -1, b, 10.0, 1.0e6) });
		CHECK ((r.s.Damage (a)->d.flags & XDMG_DESTROYED));
		CHECK (r.sdk.Logged ("Collision vessel destroyed 'PB-A'"));
	}
	SECTION ("cap: 513th record is energy only") {
		VesselDamageA *v = const_cast<VesselDamageA *> (r.s.Damage (a));
		REQUIRE_FALSE (v);
		for (int k = 0; k < 40 && (!r.s.Damage (a) || r.s.Damage (a)->d.rec.size () < 3); k++) {
			CollImpactEvent e = Hit (a, -1, b, 10.0, 3.0e4);
			e.s[0].c = Vector (-4.5 + 0.9 * (k % 10), -4.5 + 0.9 * (k / 10), 0);
			r.Frame ({ e });
		}
		CHECK (r.s.Damage (a)->d.rec.size () >= 3);
		CHECK (ColliderExact (r, a));
	}
}

TEST_CASE ("E3-U14 collider re-apply")
{
	Rig r;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	r.Begin ();
	r.Frame ();
	for (int k = 0; k < 4; k++) {
		CollImpactEvent e = Hit (a, -1, b, 10.0, 3.0e4);
		e.s[0].c = Vector (-3.0 + 2.0 * k, 0.3 * k, 0);
		r.Frame ({ e });
		REQUIRE (ColliderExact (r, a));
	}
	r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) }); // coalescing near record 1 or new
	CHECK (ColliderExact (r, a));
	r.B (a).mi.serial++; // rebuild: Replaced (0)
	r.Frame ();
	CHECK (ColliderExact (r, a));
	r.Frame ();
	CHECK (ColliderExact (r, a));
	r.s.RepairVessel (r.B (a).v);
	r.Frame ();
	CHECK (r.s.Damage (a)->d.rec.empty ());
	CHECK (ColliderExact (r, a));
	// first frame after a load: every record once
	std::vector<std::string> saved;
	r.s.SaveLines (saved);
	Rig q;
	uint32_t qa = q.Add ("PB-A"), qb = q.Add ("PB-B");
	CollStoreBlock blk;
	size_t pos = 0;
	REQUIRE (CollStore::Parse ([&] (std::string &l) { if (pos >= saved.size ()) return false; l = saved[pos++]; return true; }, blk));
	q.Begin (std::move (blk));
	q.Frame ();
	REQUIRE (q.s.Damage (qb));
	CHECK (q.s.Damage (qb)->d.rec.size () == r.s.Damage (b)->d.rec.size ());
	CHECK (ColliderExact (q, qb));
	CHECK (ColliderExact (q, qa));
}

TEST_CASE ("E3-U9 thrust cut through the dummy tank")
{
	Rig r;
	uint32_t a = r.Add ("GL", "DeltaGlider");
	DFake::V *v = r.B (a).v;
	DFake::Tk *main = r.sdk.AddTank (v, 100, 100), *rcs = r.sdk.AddTank (v, 10, 10);
	DFake::Th *t0 = r.sdk.AddThruster (v, main), *t1 = r.sdk.AddThruster (v, main), *retro = r.sdk.AddThruster (v, main), *t3 = r.sdk.AddThruster (v, rcs);
	CollStoreBlock blk;
	REQUIRE (CollStore::Parse ([lines = std::vector<std::string> { "COLLA 1", "VESSEL 0 GL DeltaGlider", "XDMG 1 600000 1", "END_VESSEL" }, i = size_t (0)] (std::string &l) mutable {
		if (i >= lines.size ()) return false; l = lines[i++]; return true; }, blk));
	r.Begin (std::move (blk));
	r.Frame ();
	auto dummy = [&] { return v->tkList.back (); };
	REQUIRE (v->tkList.size () == 3);
	CHECK (dummy ()->max == COLL_DUMMY_MAXMASS);
	CHECK (dummy ()->mass == 0);
	for (DFake::Th *t : { t0, t1, retro, t3 }) CHECK (t->tank == dummy ());
	// status round trip: level finite
	CHECK (std::isfinite (dummy ()->mass / dummy ()->max));
	// DG doors close during the cut: unlink seen
	retro->tank = nullptr;
	r.Frame ();
	CHECK (retro->tank == nullptr);
	// refuel fills the dummy: reset next pre-step
	dummy ()->mass = COLL_DUMMY_MAXMASS;
	r.Frame ();
	CHECK (dummy ()->mass == 0);
	// a module adds a tank during the cut: the dummy is last again after the post-step
	DFake::Tk *extra = r.sdk.AddTank (v, 30, 20);
	r.s.PostStep ();
	CHECK (v->tkList.back () != extra);
	CHECK (v->tkList.back ()->max == COLL_DUMMY_MAXMASS);
	CHECK (v->tkList[2] == extra);
	CHECK (t0->tank == v->tkList.back ());
	// module relinks: remembered, cut again
	t1->tank = rcs;
	r.Frame ();
	CHECK (t1->tank == v->tkList.back ());
	// Damage model off: restored to the module's latest wish, dummy gone
	r.sdk.damageModel = 0;
	r.Frame ();
	CHECK (v->tkList.size () == 3);
	CHECK (t0->tank == main);
	CHECK (t1->tank == rcs);
	CHECK (retro->tank == nullptr);
	CHECK (t3->tank == rcs);
	// on again, then repair
	r.sdk.damageModel = 1;
	r.Frame ();
	CHECK (v->tkList.size () == 4);
	r.s.RepairVessel (v);
	r.Frame ();
	CHECK (v->tkList.size () == 3);
	CHECK (t0->tank == main);
	CHECK (r.s.Damage (a)->d.flags == 0);
	// no destroyed vessel: zero writes
	uint64_t w = r.sdk.Count ().Writes ();
	for (int k = 0; k < 10; k++) r.Frame ();
	CHECK (r.sdk.Count ().Writes () == w);
}

TEST_CASE ("E3-U12 notices, replies and the exported API")
{
	Rig r;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	std::vector<std::pair<CollH, int>> got;
	int destroyedReply = CDMG_HANDLED;
	r.sdk.reply = [&] (CollH h, int prm, void *p) {
		COLLA_DAMAGEINFO *i = (COLLA_DAMAGEINFO *)p;
		REQUIRE (i->hdr.magic == COLLA_MAGIC);
		REQUIRE (i->hdr.kind == prm);
		REQUIRE (i->hdr.size == sizeof (COLLA_DAMAGEINFO));
		got.push_back ({ h, prm });
		return prm == COLLA_KIND_DESTROYED ? destroyedReply : 0;
	};
	r.cfg.destroyEnergy = 1;
	r.Begin ();
	r.Frame ();
	CollImpactEvent e1 = Hit (a, -1, b, 10.0, 3.0e4), e2 = e1;
	e2.s[0].c = Vector (3, 3, 0);
	r.Frame ({ e1, e2 });
	int dentA = 0, destA = 0;
	for (auto &g : got) { dentA += g.first == r.B (a).v && g.second == COLLA_KIND_DENT; destA += g.first == r.B (a).v && g.second == COLLA_KIND_DESTROYED; }
	CHECK (dentA == 1);
	CHECK (destA == 1);
	CHECK ((r.s.Damage (a)->d.flags & XDMG_MODULEFX));
	CHECK (r.B (a).v->tkList.empty ()); // no cut with module effects
	// query
	COLLA_DAMAGEINFO info;
	std::memset (&info, 0, sizeof info);
	info.hdr.size = sizeof info;
	REQUIRE (r.s.GetVesselDamage (r.B (a).v, &info) == 1);
	CHECK (info.hdr.kind == COLLA_KIND_STATE);
	CHECK ((info.flags & COLLA_DMG_DESTROYED));
	CHECK (info.ndent == r.s.Damage (a)->d.rec.size ());
	struct Short { COLLA_HDR hdr; uint32_t flags; } sh {};
	sh.hdr.size = sizeof sh;
	REQUIRE (r.s.GetVesselDamage (r.B (a).v, &sh) == 1);
	CHECK (sh.hdr.size == sizeof sh);
	CHECK (r.s.GetVesselDamage ((CollH)&info, &info) == 0);
	// exports through the hook
	static CollDmgSession *cur = nullptr;
	cur = &r.s;
	CollApiA::SetSession ([] () { return cur; });
	CHECK (r.s.QueuedRepairs () == 0);
	CHECK (r.s.RepairVessel (r.B (a).v) == 1);
	CollApiA::SetSession (nullptr);
	// a repair requested in a handler runs next frame
	bool once = true;
	r.sdk.reply = [&] (CollH h, int prm, void *) { if (prm == COLLA_KIND_REPAIRED && once) { once = false; r.s.RepairVessel (h); } return 0; };
	r.Frame ();
	CHECK (r.s.Damage (a)->d.rec.empty ());
	CHECK (r.s.QueuedRepairs () == 1);
	r.Frame ();
	CHECK (r.s.QueuedRepairs () == 0);
}

TEST_CASE ("E3-U10 visual sync")
{
	Rig r;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	DFake::V *v = r.B (a).v;
	v->visual = 1;
	v->dev[0] = ClientRest (*r.plate);
	r.Begin ();
	r.Frame ();
	r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
	const DentMeshCopyA *c = r.s.vis.Copy (a, 0);
	REQUIRE (c);
	REQUIRE_FALSE (c->g[0].edit.empty ());
	auto holdsCur = [&] {
		for (size_t i = 0; i < c->cur[0].size (); i++) if (std::memcmp (&v->dev[0][0][i], &c->cur[0][i], 24)) return false;
		return true;
	};
	CHECK (holdsCur ());
	uint64_t w = r.sdk.Count ().n[CSK_VTX];
	for (int k = 0; k < 5; k++) r.Frame ();
	CHECK (r.sdk.Count ().n[CSK_VTX] == w); // nothing dirty: no writes
	CHECK (holdsCur ());
	// client rebuild (INSMESH): template back, confirmed and re-pushed in the same pass
	v->dev[0] = ClientRest (*r.plate);
	r.s.PostStep ();
	CHECK (r.host.rebuilt == 1);
	CHECK (holdsCur ());
	// new visual: read before write; client already holds the dent, nothing doubles
	v->visual = 2;
	r.s.PostStep ();
	CHECK (holdsCur ());
	// a foreign write (module) turns the group module-owned for good
	uint16_t u = c->g[0].edit[0];
	v->dev[0][0][u].z += 0.25f;
	DentVtx foreign = v->dev[0][0][u];
	r.s.PostStep ();
	CHECK (c->g[0].module);
	r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
	CHECK (std::memcmp (&v->dev[0][0][u], &foreign, 24) == 0);
	CHECK (r.s.vis.ModuleGroups (a) == 1);
}

TEST_CASE ("E3-U10b visual repair and absolute mode")
{
	Rig r;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	DFake::V *v = r.B (a).v;
	v->visual = 1;
	v->dev[0] = ClientRest (*r.plate);
	r.Begin ();
	r.Frame ();
	r.sdk.readCode = -2; // edit without read: absolute mode
	r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
	CHECK (r.s.vis.Mode () == CollVisualA::MODE_ABSOLUTE);
	const DentMeshCopyA *c = r.s.vis.Copy (a, 0);
	REQUIRE (c);
	CHECK (std::memcmp (&v->dev[0][0][c->g[0].edit[0]], &c->cur[0][c->g[0].edit[0]], 24) == 0);
	r.s.RepairVessel (v);
	r.Frame ();
	CHECK (r.s.vis.Copy (a, 0) == nullptr);
	auto rest = ClientRest (*r.plate);
	bool same = true;
	for (size_t i = 0; i < rest[0].size (); i++) same = same && !std::memcmp (&v->dev[0][0][i], &rest[0][i], 12);
	CHECK (same);
}

TEST_CASE ("E3-U7 E3-U8 recorder and playback links")
{
	auto dir = std::filesystem::temp_directory_path () / "collD_side";
	std::filesystem::remove_all (dir);
	std::vector<std::string> playScn;
	std::vector<DentRecord> recorded;
	{
		Rig r;
		r.s.sideDir = dir.string ();
		r.cfg.testRecId = "E3H7";
		uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
		r.Begin ();
		r.Frame ();
		std::vector<std::string> q;
		r.s.SaveLines (q); // quicksave: no RECID
		for (auto &l : q) CHECK (l.rfind ("RECID", 0) != 0);
		r.B (a).v->recording = r.B (b).v->recording = true;
		r.s.SaveLines (playScn);
		CHECK (playScn[1].rfind ("RECID E3H7 ", 0) == 0);
		std::vector<std::string> again;
		r.s.SaveLines (again);
		for (auto &l : again) CHECK (l.rfind ("RECID", 0) != 0); // once per recording
		r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
		recorded = r.s.Damage (a)->d.rec;
		r.s.End ();
	}
	REQUIRE (std::filesystem::exists (dir / "E3H7.txt"));
	Rig p;
	p.s.sideDir = dir.string ();
	uint32_t a = p.Add ("PB-A"), b = p.Add ("PB-B");
	p.B (a).v->playback = p.B (b).v->playback = true;
	CollStoreBlock blk;
	size_t pos = 0;
	REQUIRE (CollStore::Parse ([&] (std::string &l) { if (pos >= playScn.size ()) return false; l = playScn[pos++]; return true; }, blk));
	CHECK (blk.recId == "E3H7");
	int pb = 0;
	p.sdk.reply = [&] (CollH, int prm, void *x) { if (prm == COLLA_KIND_DENT && (((COLLA_DAMAGEINFO *)x)->flags & COLLA_DMG_PLAYBACK)) pb++; return 0; };
	p.Begin (std::move (blk));
	for (int k = 0; k < 4; k++) p.Frame ();
	REQUIRE (p.s.Damage (a));
	REQUIRE (p.s.Damage (a)->d.rec.size () == recorded.size ());
	for (size_t i = 0; i < recorded.size (); i++) CHECK (std::memcmp (&p.s.Damage (a)->d.rec[i].p, &recorded[i].p, sizeof (DentParams)) == 0);
	CHECK (pb >= 1);
	CHECK (ColliderExact (p, a));
	std::filesystem::remove_all (dir);
}

TEST_CASE ("E3-U15 session lifecycle and the command")
{
	DFake proc (false);
	CollUiA::OnCommand (proc, nullptr, nullptr);
	CHECK (proc.log.empty ());
	Rig r;
	uint32_t a = r.Add ("PB-A");
	(void)a;
	r.sdk.scnIn = { "COLLA 1", "VESSEL 0 PB-A ShuttlePB", "XDMG 1 7 0", "END_VESSEL", "VESSEL 0 Gone X", "XDMG 1 3 0", "END_VESSEL", "UNKNOWNKEY 5", "END" };
	CollStoreBlock pending;
	REQUIRE (CollDmgSession::Parse (r.sdk, nullptr, pending));
	r.Begin (std::move (pending));
	std::vector<std::string> before;
	r.s.SaveLines (before); // before the first match: as read
	CHECK (before.size () == 8);
	r.Frame ();
	CHECK (r.s.Damage (0)->d.eabs == 7);
	r.s.Save (nullptr);
	CHECK (r.sdk.scnOut.size () == 8);
	CHECK (r.sdk.scnOut.back () == "UNKNOWNKEY 5");
	CHECK (std::find (r.sdk.scnOut.begin (), r.sdk.scnOut.end (), "VESSEL 0 Gone X") != r.sdk.scnOut.end ());
	CollUiA::OnCommand (proc, &r.s, nullptr);
	CHECK (r.sdk.Logged ("vessel 'PB-A'"));
	CHECK (r.sdk.annotation.find ("1 damaged vessels") != std::string::npos);
	DFake procI (true);
	CollUiA::OnCommand (procI, &r.s, (void *)&procI);
	CHECK (procI.Count ().n[CSK_UI] == 1);
	uint64_t calls = r.sdk.calls;
	size_t logs = r.sdk.log.size ();
	r.s.End ();
	CHECK (r.sdk.calls == calls);
	CHECK (r.sdk.log.size () == logs);
	// a second session reusing the handle values starts clean
	CollDmgSession s2 (r.sdk, r.host, r.cfg);
	s2.Begin (CollStoreBlock ());
	CHECK (s2.Damage (0) == nullptr);
}

TEST_CASE ("E3 review CA-GD: waiting records, saved names, energy without FIRST, RECID")
{
	std::vector<std::string> saved;
	{
		Rig r;
		uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
		r.Begin ();
		r.Frame ();
		r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4, false) }); // logged once, the energy still counts
		CHECK (r.sdk.Logged ("event energy without FIRST at t=1, counted"));
		REQUIRE (r.s.Damage (a));
		CHECK (r.s.Damage (a)->d.eabs > 0);
		r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
		REQUIRE (r.s.Damage (a)->d.rec.size () >= 1);
		r.s.SaveLines (saved);
	}
	Rig q;
	uint32_t qa = q.Add ("PB-A");
	q.Add ("PB-B");
	DFake::V *v = q.B (qa).v;
	v->visual = 1;
	v->dev[0] = ClientRest (*q.plate);
	v->dev[1] = ClientRest (*q.plate);
	q.host.slots[qa][0] = CollDmgSlot { true, DentMath::MeshKey ("other"), 1, q.plate->nvtx, q.plate, "other", 1 }; // the slot now holds another mesh
	CollStoreBlock blk;
	size_t pos = 0;
	REQUIRE (CollStore::Parse ([&] (std::string &l) { if (pos >= saved.size ()) return false; l = saved[pos++]; return true; }, blk));
	q.Begin (std::move (blk));
	q.Frame ();
	const VesselDamageA *d = q.s.Damage (qa);
	REQUIRE (d);
	REQUIRE (d->match[0] < 0);
	int reads = q.host.slotReads;
	for (int k = 0; k < 5; k++) q.Frame ();
	CHECK (q.host.slotReads == reads); // a waiting record does not re-match or rebuild every pre-step
	std::vector<std::string> out;
	q.s.SaveLines (out);
	bool other = false, plate = false;
	for (auto &l : out) { other = other || l.find (" other") != std::string::npos; plate = plate || (l.find ("XDMGM") != std::string::npos && l.find (" plate") != std::string::npos); }
	CHECK_FALSE (other);
	CHECK (plate);
	q.host.slots[qa].push_back (CollDmgSlot { true, DentMath::MeshKey ("plate"), 1, q.plate->nvtx, q.plate, "plate", 1 }); // the slot list grows without an event
	q.Frame ();
	CHECK (d->match[0] == 1);
	REQUIRE (q.s.vis.Copy (qa, 1));
	CHECK_FALSE (q.s.vis.Copy (qa, 1)->g[0].edit.empty ());
	// RECID only with a writable side file
	auto blocker = std::filesystem::temp_directory_path () / "collD_blocker";
	std::filesystem::remove_all (blocker);
	{ std::ofstream f (blocker); f << "x"; }
	Rig w;
	w.s.sideDir = (blocker / "sub").string ();
	w.cfg.testRecId = "E3NO";
	uint32_t wa = w.Add ("PB-A");
	w.Begin ();
	w.Frame ();
	w.B (wa).v->recording = true;
	std::vector<std::string> ps;
	w.s.SaveLines (ps);
	CHECK (w.sdk.Logged ("side file not writable"));
	for (auto &l : ps) CHECK (l.rfind ("RECID", 0) != 0);
	std::filesystem::remove_all (blocker);
}

TEST_CASE ("E3 review CA-GD: playback gaps and a replacement that moves mesh")
{
	DentRecord rec;
	{
		Rig r;
		uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
		r.Begin ();
		r.Frame ();
		r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
		REQUIRE (r.s.Damage (a));
		rec = r.s.Damage (a)->d.rec[0];
	}
	REQUIRE (rec.slot == 0);
	std::string side = CollSide::Header ("GAP") + "\n" + CollSide::Vdef (0, 0, "PB-A", "ShuttlePB") + "\n";
	std::vector<std::string> l;
	CollSide::Dent (0, 0, 2, rec, l);                       // index 2 without 0 and 1: skipped
	CollSide::Dent (0, 0, 0, rec, l);                       // appended on mesh 0
	DentRecord moved = rec; moved.slot = 1;
	CollSide::Dent (0, 0, 0, moved, l);                     // replaced, now on mesh 1
	for (auto &x : l) side += x + "\n";
	Rig p;
	p.s.sideDir = "side";
	p.sdk.files["side/GAP.txt"] = side;
	uint32_t a = p.Add ("PB-A");
	DFake::V *v = p.B (a).v;
	v->playback = true;
	v->visual = 1;
	v->dev[0] = ClientRest (*p.plate);
	v->dev[1] = ClientRest (*p.plate);
	p.host.slots[a].push_back (CollDmgSlot { true, DentMath::MeshKey ("plate"), 1, p.plate->nvtx, p.plate, "plate", 1 });
	CollStoreBlock blk;
	blk.recId = "GAP";
	p.Begin (std::move (blk));
	p.Frame ();
	const VesselDamageA *d = p.s.Damage (a);
	REQUIRE (d);
	CHECK (p.sdk.Logged ("has no earlier records, skipped"));
	REQUIRE (d->d.rec.size () == 1);
	CHECK (d->match[0] == 1);
	const DentMeshCopyA *c0 = p.s.vis.Copy (a, 0), *c1 = p.s.vis.Copy (a, 1);
	REQUIRE (c1);
	CHECK_FALSE (c1->g[0].edit.empty ());
	CHECK ((c0 == nullptr || c0->g[0].edit.empty ())); // the old mesh no longer shows the record
}

TEST_CASE ("dent2 crash-sized vessel dents")
{
	Rig r;
	uint32_t a = r.AddNosed ("DG-A"), b = r.Add ("PB-B");
	r.Begin ();
	r.Frame ();
	REQUIRE (r.B (a).sh->nPart () == 2);
	REQUIRE (r.B (a).sh->PartRadius (0, 1) < 1.0);
	auto hit = [&] (double vn, double dKE) {
		CollImpactEvent e = Hit (a, -1, b, vn, dKE);
		e.s[0].grp = 1, e.s[0].c = Vector (0.1, 0.1, 0.2);
		return e;
	};
	SECTION ("big hit: R beyond the nose part, copied to the body part, h past the old 0.5 m t_cap") {
		r.Frame ({ hit (30.0, 4.0e6) });
		const VesselDamageA *v = r.s.Damage (a);
		REQUIRE (v);
		REQUIRE (v->d.rec.size () == 2);
		CHECK (v->d.rec[0].p.R > 1.0);
		CHECK (v->d.rec[0].p.h > 0.5);
		CHECK (v->d.rec[0].p.h <= 0.5 * v->d.rec[0].p.R + 1e-9);
		CHECK (v->d.rec[0].grp == std::vector<uint16_t> { 1 });
		CHECK (v->d.rec[1].grp == std::vector<uint16_t> { 0 });
		CHECK (v->d.rec[1].p.R == v->d.rec[0].p.R);
		CHECK (v->d.rec[1].p.h == v->d.rec[0].p.h);
		CHECK (r.sdk.Logged ("parts=2 copies=1"));
	}
	SECTION ("dent grows with energy") {
		Rig r2;
		uint32_t a2 = r2.AddNosed ("DG-A"), b2 = r2.Add ("PB-B");
		r2.Begin ();
		r2.Frame ();
		CollImpactEvent e = Hit (a2, -1, b2, 30.0, 4.0e5);
		e.s[0].grp = 1, e.s[0].c = Vector (0.1, 0.1, 0.2);
		r2.Frame ({ e });
		r.Frame ({ hit (30.0, 4.0e6) });
		REQUIRE (r2.s.Damage (a2));
		REQUIRE (r.s.Damage (a));
		CHECK (r.s.Damage (a)->d.rec[0].p.R > r2.s.Damage (a2)->d.rec[0].p.R);
		CHECK (r.s.Damage (a)->d.rec[0].p.h > r2.s.Damage (a2)->d.rec[0].p.h);
	}
	SECTION ("small then large at one spot: the large one is its own record (R grows)") {
		r.Frame ({ hit (5.0, 2.0e4) });
		size_t n1 = r.s.Damage (a)->d.rec.size ();
		REQUIRE (n1 >= 1);
		double R1 = r.s.Damage (a)->d.rec[0].p.R;
		r.Frame ({ hit (30.0, 4.0e6) });
		const VesselDamageA *v = r.s.Damage (a);
		REQUIRE (v->d.rec.size () > n1);
		CHECK (v->d.rec[n1].p.R > R1);
	}
	SECTION ("same hit twice: grown once per record, copies keep one depth") {
		r.Frame ({ hit (30.0, 4.0e6) });
		double h1 = r.s.Damage (a)->d.rec[0].p.h;
		r.Frame ({ hit (30.0, 1.0e6) });
		const VesselDamageA *v = r.s.Damage (a);
		REQUIRE (v->d.rec.size () == 2);
		CHECK (v->d.rec[0].p.h >= h1);
		CHECK (v->d.rec[1].p.h == v->d.rec[0].p.h);
	}
}
