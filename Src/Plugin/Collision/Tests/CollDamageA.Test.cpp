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
	Rig () { cfg.logLevel = 1; cfg.dentModes = cfg.dentNoise = cfg.dentFacetNormals = false; } // dmg3: existing tests run the bowl path
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
	uint32_t AddNosed (const std::string &name, bool cabin = false) // body plate (group 0), small animated nose plate 0.2 m in front (group 1); cabin: group 2 behind the nose, no collider
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
		if (cabin) {
			m->grp.push_back (g);
			for (CollVtx &x : m->grp[2].vtx) x.z = 0.1f;
			auto sc = std::make_shared<CollSidecar> ();
			std::vector<std::string> w;
			const char *txt = "COLLIDER-V1\nEXCLUDE GROUP 2\n";
			REQUIRE (CollParseSidecar (txt, std::strlen (txt), "t.col", *sc, w));
			b.mi.side = sc;
		}
		m->nvtx = 0;
		for (auto &x : m->grp) m->nvtx += (uint32_t)x.vtx.size ();
		b.mi.key = "nosed", b.mi.rest = m;
		b.tv.reset (new TestVessel ()), b.mod.reset (new TestModule ());
		b.tv->coll = &b.ca;
		b.tv->meshGrp = { 2 };
		UINT an = b.tv->CreateAnimation (0);
		b.tv->AddAnimationComponent (an, 0, 1, b.mod->Lin (0, b.mod->Grp ({1}), 1, _V(0,0,1)));
		host.slots[id] = { CollDmgSlot { true, DentMath::MeshKey ("nosed"), (uint16_t)m->grp.size (), m->nvtx, m, "nosed", 1 } };
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
	if (v) for (const DentRecord &x : v->d.rec) {
		std::vector<uint32_t> g (x.grp.begin (), x.grp.end ());
		if (x.p.mode == DENTM_CUT) fresh.ApplyMap (0, g.data (), g.size (), DentMath::MapLow, &x); // dmg3 tear
		else fresh.ApplyDent (0, g.data (), g.size (), DentMath::Field, &x);
	}
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

TEST_CASE ("dent2 D7 groups without a collider get the dent")
{
	Rig r;
	uint32_t a = r.AddNosed ("DG-A", true), b = r.Add ("PB-B");
	r.Begin ();
	r.Frame ();
	REQUIRE (r.B (a).sh->PartOf (0, 2) < 0);
	CollImpactEvent e = Hit (a, -1, b, 30.0, 4.0e6);
	e.s[0].grp = 1, e.s[0].c = Vector (0.1, 0.1, 0.2);
	r.Frame ({ e });
	const VesselDamageA *v = r.s.Damage (a);
	REQUIRE (v);
	bool has2 = false;
	for (const DentRecord &x : v->d.rec) for (uint16_t g : x.grp) has2 = has2 || g == 2;
	CHECK (has2);
}

// fix1 area D (design-CA-fix1)

namespace {

// group 0 the plate, group 1 from grid (nx x ny points, origin o, steps ux, uy, normal nm); exclude: group 1 has no collider; animate: group 1 is its own pose class
uint32_t AddTwo (Rig &r, const std::string &name, const std::string &key, Vector o, Vector ux, Vector uy, int nx, int ny, Vector nm, bool exclude, bool animate)
{
	uint32_t id = r.Add (name);
	Rig::Body &b = r.body.back ();
	auto m = std::make_shared<CollRestMesh> (*r.plate);
	m->name = key;
	m->grp.resize (2);
	CollGroupData &g = m->grp[1];
	for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) {
		Vector p = o + ux * i + uy * j;
		g.vtx.push_back (CollVtx { (float)p.x, (float)p.y, (float)p.z, (float)nm.x, (float)nm.y, (float)nm.z, 0, 0 });
	}
	for (int j = 0; j + 1 < ny; j++) for (int i = 0; i + 1 < nx; i++) {
		uint16_t a = (uint16_t)(j * nx + i), c1 = (uint16_t)(a + 1), c = (uint16_t)(a + nx), d = (uint16_t)(c + 1);
		g.idx.insert (g.idx.end (), { a, c1, c, c1, d, c });
	}
	m->nvtx = (uint32_t)(m->grp[0].vtx.size () + g.vtx.size ());
	if (exclude) {
		auto sc = std::make_shared<CollSidecar> ();
		std::vector<std::string> w;
		const char *txt = "COLLIDER-V1\nEXCLUDE GROUP 1\n";
		REQUIRE (CollParseSidecar (txt, std::strlen (txt), "t.col", *sc, w));
		b.mi.side = sc;
	}
	b.mi.key = key, b.mi.rest = m;
	if (animate) {
		b.tv.reset (new TestVessel ()), b.mod.reset (new TestModule ());
		b.tv->coll = &b.ca;
		b.tv->meshGrp = { 2 };
		UINT an = b.tv->CreateAnimation (0);
		b.tv->AddAnimationComponent (an, 0, 1, b.mod->Lin (0, b.mod->Grp ({1}), 1, _V(0,0,1)));
	}
	r.host.slots[id] = { CollDmgSlot { true, DentMath::MeshKey (key.c_str ()), 2, m->nvtx, m, key, 1 } };
	return id;
}

CollDmgBaseObj Block () { return CollDmgBaseObj { "Moon", "Brighton Beach", "BLOCK", 0, 0, 1, DENTB_BLOCK, Vector (10, 10, 10), -60.6, -35, 0, nullptr, (CollH)0x77 }; }

// the mirror equals a fresh full build from the same records, bitwise
bool MirrorExact (Rig &r, uint32_t id, uint32_t slot)
{
	const VesselDamageA *v = r.s.Damage (id);
	const DentMeshCopyA *c = r.s.vis.Copy (id, slot);
	if (!v || !c) return false;
	CollVisualA fresh (r.sdk, r.host, r.cfg);
	CollDmgSlot s;
	if (!r.host.Slot (id, slot, s)) return false;
	std::vector<const DentRecord *> rs;
	for (size_t k = 0; k < v->d.rec.size (); k++) if (v->match[k] == (int)slot) rs.push_back (&v->d.rec[k]);
	fresh.SetRecords (id, v->name, s, slot, rs);
	const DentMeshCopyA *f = fresh.Copy (id, slot);
	if (!f || f->cur.size () != c->cur.size ()) return false;
	for (size_t g = 0; g < f->cur.size (); g++) {
		if (f->cur[g].size () != c->cur[g].size () || f->g[g].edit != c->g[g].edit) return false;
		if (std::memcmp (f->cur[g].data (), c->cur[g].data (), f->cur[g].size () * sizeof (DentVtx))) return false;
	}
	return true;
}

}

TEST_CASE ("fix1 M8 playback D events past DENT_MAX_VESSEL are rejected")
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
	std::string side = CollSide::Header ("FLOOD") + "\n" + CollSide::Vdef (0, 0, "PB-A", "ShuttlePB") + "\n";
	std::vector<std::string> l;
	for (uint32_t k = 0; k < 600; k++) CollSide::Dent (0, 0, k, rec, l);
	for (auto &x : l) side += x + "\n";
	Rig p;
	p.s.sideDir = "side";
	p.sdk.files["side/FLOOD.txt"] = side;
	uint32_t a = p.Add ("PB-A");
	p.B (a).v->playback = true;
	CollStoreBlock blk;
	blk.recId = "FLOOD";
	p.Begin (std::move (blk));
	p.Frame ();
	REQUIRE (p.s.Damage (a));
	CHECK (p.s.Damage (a)->d.rec.size () == DENT_MAX_VESSEL);
	CHECK (p.sdk.Logged ("dent 512 beyond 512 records, skipped"));
	CHECK (ColliderExact (p, a));
}

TEST_CASE ("fix1 M9 one collider replay and one mirror build per slot per frame")
{
	Rig r;
	uint32_t a = r.Add ("PB-A");
	r.host.bases.push_back (Block ()); // the other side takes energy only
	r.Begin ();
	r.Frame ();
	auto at = [&] (double x, double y) { CollImpactEvent e = Hit (a, 0, 1, 10.0, 3.0e4); e.s[0].c = Vector (x, y, 0); return e; };
	r.Frame ({ at (-2, -2) });
	REQUIRE (r.s.Damage (a)->d.rec.size () == 1);
	double h0 = r.s.Damage (a)->d.rec[0].p.h;
	CollVisCounters c0 = r.s.vis.n;
	uint64_t rp0 = r.s.n.replays;
	r.Frame ({ at (-2, -2), at (2, 2), at (-2, -2) }); // grow, new, grow
	REQUIRE (r.s.Damage (a)->d.rec.size () == 2);
	CHECK (r.s.Damage (a)->d.rec[0].p.h > h0);
	CHECK (r.s.vis.n.builds + r.s.vis.n.incr == c0.builds + c0.incr + 1);
	CHECK (r.s.n.replays == rp0 + 2); // fix2: the new hit replays the grown slot before it reads the collider
	CHECK (ColliderExact (r, a));
	CHECK (MirrorExact (r, a, 0));
	c0 = r.s.vis.n, rp0 = r.s.n.replays;
	r.Frame ({ at (2, -2), at (-2, 2), at (0, 3) }); // three new records: incremental
	REQUIRE (r.s.Damage (a)->d.rec.size () == 5);
	CHECK (r.s.vis.n.incr == c0.incr + 1);
	CHECK (r.s.vis.n.builds == c0.builds);
	CHECK (r.s.n.replays == rp0);
	CHECK (ColliderExact (r, a));
	CHECK (MirrorExact (r, a, 0));
}

TEST_CASE ("fix1 M10 a stale record no longer blocks new dents on a present slot")
{
	std::vector<std::string> saved;
	{
		Rig r;
		uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
		r.Begin ();
		r.Frame ();
		r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
		REQUIRE (r.s.Damage (a)->d.rec.size () == 1);
		r.s.SaveLines (saved);
	}
	for (bool rest : { true, false }) {
		Rig q;
		uint32_t qa = q.Add ("PB-A"), qb = q.Add ("PB-B");
		q.host.slots[qa][0] = CollDmgSlot { true, DentMath::MeshKey ("other"), 1, q.plate->nvtx, rest ? q.plate : nullptr, "other", 1 };
		CollStoreBlock blk;
		size_t pos = 0;
		REQUIRE (CollStore::Parse ([&] (std::string &l) { if (pos >= saved.size ()) return false; l = saved[pos++]; return true; }, blk));
		q.Begin (std::move (blk));
		q.Frame ();
		q.Frame ({ Hit (qa, -1, qb, 10.0, 3.0e4) });
		const VesselDamageA *d = q.s.Damage (qa);
		REQUIRE (d);
		CHECK (d->match[0] < 0);
		if (!rest) { CHECK (d->d.rec.size () == 1); continue; } // no rest mesh yet: still waits
		REQUIRE (d->d.rec.size () == 2);
		CHECK (d->match[1] == 0);
		CHECK (ColliderExact (q, qa) == false); // the stale record is not on the collider
		std::vector<std::string> out;
		q.s.SaveLines (out);
		int lines = 0;
		bool inA = false;
		for (auto &l : out) { inA = (inA || l == "VESSEL 0 PB-A ShuttlePB") && l != "END_VESSEL"; lines += inA && l.find ("XDMGD") != std::string::npos; }
		CHECK (lines == 2); // dormant record kept and saved
	}
}

TEST_CASE ("fix1 groups over 65536 vertices get no visual dent")
{
	Rig r;
	uint32_t a = r.Add ("PB-A");
	auto m = std::make_shared<CollRestMesh> ();
	m->name = "big";
	m->grp.resize (2);
	for (int j = 0; j < 250; j++) for (int i = 0; i < 280; i++) m->grp[0].vtx.push_back (CollVtx { 0.04f * i, 0.04f * j, 0, 0, 0, 1, 0, 0 });
	m->grp[1] = r.plate->grp[0];
	m->nvtx = (uint32_t)(m->grp[0].vtx.size () + m->grp[1].vtx.size ());
	CollDmgSlot s { true, DentMath::MeshKey ("big"), 2, m->nvtx, m, "big", 1 };
	r.host.slots[a] = { s };
	DentRecord rec {};
	const CollVtx &x = m->grp[0].vtx[66000];
	rec.p.c = Vector (x.x, x.y, 0), rec.p.n = Vector (0, 0, 1), rec.p.R = 0.5, rec.p.h = 0.1, rec.p.T = 0;
	r.s.vis.SetRecords (a, "PB-A", s, 0, { &rec });
	const DentMeshCopyA *c = r.s.vis.Copy (a, 0);
	REQUIRE (c);
	CHECK (c->g[0].big);
	CHECK (c->g[0].edit.empty ());
	CHECK (r.sdk.Logged ("grp=0 has 70000 vertices"));
	DFake::V *v = r.B (a).v;
	v->visual = 1;
	v->dev[0] = ClientRest (*m);
	r.s.vis.Pass (CollVisualA::PASS_ALL);
	CHECK (std::memcmp (v->dev[0][0].data (), ClientRest (*m)[0].data (), 70000 * sizeof (DentVtx)) == 0); // nothing written into a wrapped index
}

TEST_CASE ("fix1 zero rest normals: the client's NaN normal counts as rest")
{
	Rig r;
	uint32_t a = r.Add ("PB-A");
	r.host.bases.push_back (Block ());
	auto m = std::make_shared<CollRestMesh> (*r.plate);
	for (CollVtx &x : m->grp[0].vtx) x.nx = x.ny = x.nz = 0;
	r.B (a).mi.rest = m, r.host.slots[a][0].rest = m;
	DFake::V *v = r.B (a).v;
	v->visual = 1;
	v->dev[0] = ClientRest (*m);
	for (DentVtx &x : v->dev[0][0]) x.nx = x.ny = x.nz = std::nanf (""); // the client's 0 * inf
	r.Begin ();
	r.Frame ();
	r.Frame ({ Hit (a, 0, 1, 10.0, 3.0e4) });
	const DentMeshCopyA *c = r.s.vis.Copy (a, 0);
	REQUIRE (c);
	REQUIRE_FALSE (c->g[0].edit.empty ());
	CHECK_FALSE (c->g[0].module);
	CHECK (r.s.vis.ModuleGroups (a) == 0);
	uint16_t u = c->g[0].edit[0];
	CHECK (std::memcmp (&v->dev[0][0][u], &c->cur[0][u], 12) == 0);
}

TEST_CASE ("fix1 D7 groups are in the depth cap")
{
	Rig r;
	uint32_t a = AddTwo (r, "PB-A", "cabin", Vector (0.15, 0.15, -0.05), Vector (0.1, 0, 0), Vector (0, 0.1, 0), 3, 3, Vector (0, 0, 1), true, false);
	r.B (a).v->size = 1.0;
	r.host.bases.push_back (Block ());
	r.Begin ();
	r.Frame ();
	REQUIRE (r.B (a).sh->PartOf (0, 1) < 0);
	CollImpactEvent e = Hit (a, 0, 1, 30.0, 4.0e6);
	e.s[0].c = Vector (0.25, 0.25, 0);
	r.Frame ({ e });
	r.Frame ({ e });
	const VesselDamageA *v = r.s.Damage (a);
	REQUIRE (v);
	const CollRestMesh &m = *r.host.slots[a][0].rest;
	double worst = 0, cap = 1e300;
	bool listed = false;
	for (const CollVtx &x : m.grp[1].vtx) {
		Vector p (x.x, x.y, x.z);
		double u = 0;
		for (const DentRecord &d : v->d.rec)
			if (std::find (d.grp.begin (), d.grp.end (), 1) != d.grp.end ()) { listed = true; u += -dotp (DentMath::Displace (d.p, p), d.p.n); cap = std::min (cap, DentMath::DmaxVessel (d.p.T, d.p.R, 1.0)); }
		worst = std::max (worst, u);
	}
	REQUIRE (listed);
	CHECK (worst > 0.0);
	CHECK (worst <= cap + 1e-9);
}

TEST_CASE ("fix1 weld map within one pose class: the seam keeps its normals")
{
	Rig r;
	uint32_t a = AddTwo (r, "PB-A", "walled", Vector (-1, 0, 0), Vector (0.5, 0, 0), Vector (0, 0, 0.5), 5, 3, Vector (0, -1, 0), false, true);
	r.Begin ();
	r.Frame ();
	REQUIRE (r.B (a).sh->PartOf (0, 0) != r.B (a).sh->PartOf (0, 1));
	CollDmgSlot s;
	REQUIRE (r.host.Slot (a, 0, s));
	DentRecord rec {};
	rec.p.c = Vector (0, 0.5, 0), rec.p.n = Vector (0, 0, 1), rec.p.R = 1.0, rec.p.h = 0.2, rec.p.T = 0, rec.grp = { 0 };
	r.s.vis.SetRecords (a, "PB-A", s, 0, { &rec });
	const DentMeshCopyA *c = r.s.vis.Copy (a, 0);
	REQUIRE (c);
	CHECK_FALSE (c->g[0].edit.empty ());
	CHECK (c->g[1].edit.empty ());
	CHECK (std::memcmp (c->cur[1].data (), c->rp[1].data (), c->rp[1].size () * sizeof (DentVtx)) == 0);
}

TEST_CASE ("fix1 thrust cut: deleted thrusters leave the wish list")
{
	Rig r;
	uint32_t a = r.Add ("GL", "DeltaGlider");
	DFake::V *v = r.B (a).v;
	DFake::Tk *main = r.sdk.AddTank (v, 100, 100);
	for (int k = 0; k < 4; k++) r.sdk.AddThruster (v, main);
	CollStoreBlock blk;
	REQUIRE (CollStore::Parse ([lines = std::vector<std::string> { "COLLA 1", "VESSEL 0 GL DeltaGlider", "XDMG 1 600000 1", "END_VESSEL" }, i = size_t (0)] (std::string &l) mutable {
		if (i >= lines.size ()) return false; l = lines[i++]; return true; }, blk));
	r.Begin (std::move (blk));
	r.Frame ();
	REQUIRE (r.s.Damage (a)->cut.wish.size () == 4);
	v->thList.erase (v->thList.begin () + 1); // the module deletes a thruster
	r.Frame ();
	CHECK (r.s.Damage (a)->cut.wish.size () == 3);
	for (int k = 0; k < 5; k++) { r.sdk.AddThruster (v, main); v->thList.erase (v->thList.begin ()); r.Frame (); } // churn stays bounded
	CHECK (r.s.Damage (a)->cut.wish.size () == 3);
}

TEST_CASE ("fix1 merge key: a D7 group's frame does not stop coalescing")
{
	Rig r;
	uint32_t a = r.AddNosed ("DG-A", true);
	Rig::Body &b = r.body.back ();
	b.tv->meshGrp = { 3 };
	UINT an2 = b.tv->CreateAnimation (0);
	b.tv->AddAnimationComponent (an2, 0, 1, b.mod->Lin (0, b.mod->Grp ({2}), 1, _V(0,0,1))); // the cabin moves on its own
	r.host.bases.push_back (Block ());
	r.Begin ();
	r.Frame ();
	REQUIRE (r.B (a).sh->PartOf (0, 2) < 0);
	auto hit = [&] (double dKE) { CollImpactEvent e = Hit (a, 0, 1, 30.0, dKE); e.s[0].c = Vector (2, 2, 0); return e; };
	r.Frame ({ hit (4.0e6) });
	const VesselDamageA *v = r.s.Damage (a);
	REQUIRE (v);
	size_t n1 = v->d.rec.size ();
	REQUIRE (std::find (v->d.rec[0].grp.begin (), v->d.rec[0].grp.end (), 2) != v->d.rec[0].grp.end ()); // the cabin joins the body record at state 0
	double h1 = v->d.rec[0].p.h;
	b.tv->SetAnimation (an2, 1.0);
	r.Frame ();
	r.Frame ({ hit (1.0e6) });
	CHECK (r.s.n.coalesced >= 1);
	CHECK (v->d.rec[0].p.h > h1);
	CHECK (v->d.rec.size () == n1);
}

TEST_CASE ("fix2 M8 the playback flag follows the vessel, not an earlier playback")
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
	std::string side = CollSide::Header ("TAKE") + "\n" + CollSide::Vdef (0, 0, "PB-A", "ShuttlePB") + "\n";
	std::vector<std::string> l;
	CollSide::Dent (0, 0, 0, rec, l);
	for (auto &x : l) side += x + "\n";
	Rig p;
	p.s.sideDir = "side";
	p.sdk.files["side/TAKE.txt"] = side;
	uint32_t a = p.Add ("PB-A"), b = p.Add ("PB-B");
	p.B (a).v->playback = true;
	CollStoreBlock blk;
	blk.recId = "TAKE";
	p.Begin (std::move (blk));
	p.Frame ();
	REQUIRE (p.s.Damage (a));
	REQUIRE (p.s.Damage (a)->d.rec.size () == 1);
	COLLA_DAMAGEINFO info;
	std::memset (&info, 0, sizeof info);
	info.hdr.size = sizeof info;
	REQUIRE (p.s.GetVesselDamage (p.B (a).v, &info) == 1);
	CHECK ((info.flags & COLLA_DMG_PLAYBACK));
	p.B (a).v->playback = false; // the user takes over
	int dents = 0;
	uint32_t fl = 0;
	p.sdk.reply = [&] (CollH h, int prm, void *x) { if (h == p.B (a).v && prm == COLLA_KIND_DENT) dents++, fl |= ((COLLA_DAMAGEINFO *)x)->flags; return 0; };
	auto hit = Hit (a, -1, b, 10.0, 3.0e4);
	hit.s[0].c = Vector (-3, -3, 0);
	p.Frame ({ hit });
	REQUIRE (dents >= 1);
	CHECK (!(fl & COLLA_DMG_PLAYBACK));
	std::memset (&info, 0, sizeof info);
	info.hdr.size = sizeof info;
	REQUIRE (p.s.GetVesselDamage (p.B (a).v, &info) == 1);
	CHECK (!(info.flags & COLLA_DMG_PLAYBACK));
}

TEST_CASE ("fix2 a version change before the rebuilt event keeps what the client holds")
{
	Rig r;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	DFake::V *v = r.B (a).v;
	v->visual = 1;
	v->dev[0] = ClientRest (*r.plate);
	r.Begin ();
	r.Frame ();
	r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
	REQUIRE (r.s.vis.Copy (a, 0));
	r.host.slots[a][0].serial = 2; // E2 sees a new version; its rebuilt event comes later
	auto hit = Hit (a, -1, b, 10.0, 3.0e4);
	hit.s[0].c = Vector (-3, -3, 0);
	r.Frame ({ hit });
	const DentMeshCopyA *c = r.s.vis.Copy (a, 0);
	REQUIRE (c);
	CHECK (c->serial == 2);
	CHECK (!c->g[0].module);
	CHECK (r.s.vis.ModuleGroups (a) == 0);
	bool holds = true;
	for (size_t i = 0; i < c->cur[0].size (); i++) holds = holds && !std::memcmp (&v->dev[0][0][i], &c->cur[0][i], 24);
	CHECK (holds);
}

TEST_CASE ("custom-fix D4: a repair after a version change restores what the client holds, then frees the copy")
{
	Rig r;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	DFake::V *v = r.B (a).v;
	v->visual = 1;
	v->dev[0] = ClientRest (*r.plate);
	r.Begin ();
	r.Frame ();
	r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
	REQUIRE (r.s.vis.Copy (a, 0));
	auto rest = ClientRest (*r.plate);
	auto atRest = [&] { for (size_t i = 0; i < rest[0].size (); i++) if (std::memcmp (&v->dev[0][0][i], &rest[0][i], 12)) return false; return true; };
	REQUIRE_FALSE (atRest ());
	r.host.slots[a][0].serial = 2; // E2 sees a new version; its rebuilt event comes later
	r.s.RepairVessel (v);
	r.Frame ();
	CHECK (atRest ());
	CHECK (r.s.vis.Copy (a, 0) == nullptr);
	CHECK (r.s.vis.ModuleGroups (a) == 0);
}

TEST_CASE ("custom-fix D5: a repair drops unknown-version rows and logs the record cap again")
{
	Rig r;
	uint32_t a = r.Add ("PB-A");
	r.sdk.scnIn = { "COLLA 1", "VESSEL 0 PB-A ShuttlePB", "XDMG 1 7 0", "XDMG 9 1 2", "XDMGD 9 future", "END_VESSEL", "END" };
	CollStoreBlock pending;
	REQUIRE (CollDmgSession::Parse (r.sdk, nullptr, pending));
	r.Begin (std::move (pending));
	r.Frame ();
	REQUIRE (r.s.Damage (a));
	CHECK (r.s.Damage (a)->d.verbatim.size () == 2);
	const_cast<VesselDamageA *> (r.s.Damage (a))->loggedCap = true; // the cap message was logged before
	r.s.RepairVessel (r.B (a).v);
	r.Frame ();
	REQUIRE (r.s.Damage (a));
	CHECK (r.s.Damage (a)->d.verbatim.empty ());
	CHECK_FALSE (r.s.Damage (a)->loggedCap);
	std::vector<std::string> saved;
	r.s.SaveLines (saved);
	for (const std::string &l : saved) CHECK (l.find ("XDMG") == std::string::npos); // a newer build loading the save sees no repaired damage
}

TEST_CASE ("fix2 a hit after a growth in the same commit reads the grown collider")
{
	auto at = [] (uint32_t a, double x, double y, double dKE) { CollImpactEvent e = Hit (a, 0, 1, 10.0, dKE); e.s[0].c = Vector (x, y, 0); return e; };
	auto run = [&] (bool split) {
		Rig r;
		uint32_t a = r.Add ("PB-A");
		r.host.bases.push_back (Block ());
		r.Begin ();
		r.Frame ();
		r.Frame ({ at (a, -2, -2, 3.0e5) });
		if (split) for (int k = 0; k < 3; k++) r.Frame ({ at (a, -2, -2, 1.0e5) }); // grow up to the depth cap
		else r.Frame ({ at (a, -2, -2, 1.0e5), at (a, -2, -2, 1.0e5), at (a, -2, -2, 1.0e5) });
		CHECK (ColliderExact (r, a));
		return r.s.Damage (a)->d.rec;
	};
	std::vector<DentRecord> one = run (false), two = run (true);
	REQUIRE (one.size () == two.size ());
	for (size_t i = 0; i < one.size (); i++) CHECK (std::memcmp (&one[i].p, &two[i].p, sizeof (DentParams)) == 0);
}

namespace {
struct SinkLog : CollDmgSink {
	std::string tag; std::vector<std::string> *log; std::vector<CollDamageHit> hits; std::vector<DentTorn> torn; int post = 0, pass = 0, repair = 0, drop = 0, end = 0, destroyed = 0, shapes = 0; double lastDt = -1;
	SinkLog (const char *t, std::vector<std::string> *l) : tag (t), log (l) {}
	void Hit (const CollDamageHit &h) override { hits.push_back (h); log->push_back (tag + " hit"); }
	void Post (double, double dt) override { post++, lastDt = dt; }
	void Pass () override { pass++; }
	void Torn (uint32_t, const DentTorn &t) override { torn.push_back (t); }
	void Repair (uint32_t) override { repair++; }
	void DropVessel (uint32_t, CollH) override { drop++; }
	void Destroyed (uint32_t) override { destroyed++; }
	void Shapes (uint32_t, CollShape *) override { shapes++; }
	void End () override { end++; }
};

bool ColliderExactLow (Rig &r, uint32_t id)
{
	auto &b = r.B (id);
	CollShape fresh;
	CollMeshInfo mi = b.mi;
	CollAnim ca;
	CollTemplateCache cache;
	fresh.Update (&mi, 1, ca, nullptr, 0, cache);
	const VesselDamageA *v = r.s.Damage (id);
	if (v) for (const DentRecord &x : v->d.rec) { std::vector<uint32_t> g (x.grp.begin (), x.grp.end ()); fresh.ApplyDent (0, g.data (), g.size (), DentMath::FieldLow, &x); }
	for (uint32_t p = 0; p < fresh.nPart (); p++) {
		const CollGeom &A = fresh.Part (p).Geom (), &B = b.sh->Part (p).Geom ();
		if (A.vtx.size () != B.vtx.size ()) return false;
		for (size_t i = 0; i < A.vtx.size (); i++) if (std::memcmp (&A.vtx[i], &B.vtx[i], sizeof (Vector))) return false;
	}
	return true;
}
}

TEST_CASE ("dmg3 hooks: hit to parts then effects, rec and Esurplus, X and T recorded and played back")
{
	auto dir = std::filesystem::temp_directory_path () / "collD_dmg3";
	std::filesystem::remove_all (dir);
	std::vector<std::string> playScn, order;
	std::vector<DentRecord> recorded;
	DentTorn tr;
	tr.kind = 0, tr.slot = 0, tr.key = DentMath::MeshKey ("plate"), tr.ngrp = 1, tr.nvtx = 441, tr.simt = 1.5, tr.grp = { 0 };
	{
		Rig r;
		r.cfg.dentModes = r.cfg.dentNoise = r.cfg.dentFacetNormals = true;
		r.s.sideDir = dir.string ();
		r.cfg.testRecId = "DMG3X";
		auto *bk = new SinkLog ("brk", &order), *fx = new SinkLog ("fx", &order);
		r.s.brk.reset (bk), r.s.fx.reset (fx);
		uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
		r.Begin ();
		r.Frame ();
		r.B (a).v->recording = r.B (b).v->recording = true;
		r.s.SaveLines (playScn);
		CollImpactEvent e = Hit (a, -1, b, 10.0, 3.0e4);
		e.vt = 2.5;
		r.Frame ({ e });
		REQUIRE (bk->hits.size () == 2);
		REQUIRE (fx->hits.size () == 2);
		REQUIRE (order.size () >= 2);
		CHECK (order[0] == "brk hit");
		CHECK (order[1] == "fx hit");
		const CollDamageHit &h = bk->hits[0];
		CHECK (h.id == a);
		CHECK (h.other == b);
		CHECK (h.rec == 0);
		CHECK (h.vn == 10.0);
		CHECK (h.vt == 2.5);
		CHECK (h.E > 0);
		CHECK (h.Esurplus >= 0);
		CHECK (h.Esurplus <= h.E);
		CHECK (h.depth > 0);
		CHECK (h.eSpec == h.E / 500.0);
		CHECK (h.mat);
		CHECK_FALSE (h.playback);
		CHECK (bk->post >= 2);
		CHECK (fx->post >= 2);
		CHECK (bk->shapes >= 2);
		recorded = r.s.Damage (a)->d.rec;
		REQUIRE (recorded.size () >= 1);
		CHECK (recorded[0].p.seed != 0); // new build: noise seed
		CHECK_FALSE (DentMath::Legacy (recorded[0].p));
		CHECK (ColliderExactLow (r, a));
		r.s.AddTorn (a, tr);
		CHECK (r.s.Damage (a)->d.torn.size () == 1);
		std::vector<std::string> q;
		r.s.SaveLines (q);
		bool x2 = false, tt = false;
		for (auto &l : q) { x2 = x2 || l.find ("XDMG 2 ") != std::string::npos; tt = tt || l.find ("XDMGM T ") != std::string::npos; CHECK (l.size () <= 200); }
		CHECK (x2);
		CHECK (tt);
		r.s.End ();
		CHECK (bk->end == 1);
		CHECK (fx->end == 1);
	}
	REQUIRE (std::filesystem::exists (dir / "DMG3X.txt"));
	{
		std::ifstream f (dir / "DMG3X.txt");
		std::stringstream ss;
		ss << f.rdbuf ();
		std::string txt = ss.str ();
		size_t d = txt.find (" D "), x = txt.find (" X "), t = txt.find (" T ");
		CHECK (d != std::string::npos);
		CHECK (x != std::string::npos);
		CHECK (x > d); // X right after the record's D lines
		CHECK (t != std::string::npos);
	}
	Rig p;
	p.cfg.dentModes = p.cfg.dentNoise = p.cfg.dentFacetNormals = true;
	std::vector<std::string> order2;
	auto *bk = new SinkLog ("brk", &order2);
	p.s.brk.reset (bk);
	p.s.sideDir = dir.string ();
	uint32_t a = p.Add ("PB-A"), b = p.Add ("PB-B");
	p.B (a).v->playback = p.B (b).v->playback = true;
	CollStoreBlock blk;
	size_t pos = 0;
	REQUIRE (CollStore::Parse ([&] (std::string &l) { if (pos >= playScn.size ()) return false; l = playScn[pos++]; return true; }, blk));
	p.Begin (std::move (blk));
	for (int k = 0; k < 6; k++) p.Frame ();
	REQUIRE (p.s.Damage (a));
	REQUIRE (p.s.Damage (a)->d.rec.size () == recorded.size ());
	for (size_t i = 0; i < recorded.size (); i++) CHECK (std::memcmp (&p.s.Damage (a)->d.rec[i].p, &recorded[i].p, sizeof (DentParams)) == 0);
	CHECK (ColliderExactLow (p, a));
	bool pb = false;
	for (const CollDamageHit &h : bk->hits) if (h.playback && h.id == a) { pb = true; CHECK (h.vn == 10.0); CHECK (h.vt == 2.5); CHECK (h.E > 0); CHECK (h.Esurplus <= h.E); }
	CHECK (pb);
	REQUIRE (bk->torn.size () == 1);
	CHECK (bk->torn[0].grp == tr.grp);
	CHECK (p.s.Damage (a)->d.torn.size () == 1);
	std::filesystem::remove_all (dir);
}

TEST_CASE ("dmg3 CollDebris vessels take energy only; legacy rig writes no extension")
{
	Rig r;
	r.cfg.dentModes = r.cfg.dentNoise = true;
	std::vector<std::string> order;
	auto *bk = new SinkLog ("brk", &order);
	r.s.brk.reset (bk);
	uint32_t a = r.Add ("Deb-1", "colldebris"), b = r.Add ("PB-B");
	r.Begin ();
	r.Frame ();
	r.Frame ({ Hit (a, -1, b, 10.0, 3.0e4) });
	REQUIRE (r.s.Damage (a));
	CHECK (r.s.Damage (a)->d.eabs > 0);
	CHECK (r.s.Damage (a)->d.rec.empty ());
	for (const CollDamageHit &h : bk->hits) CHECK (h.id != a);
	Rig o; // modes off: records stay legacy, the save has no version-2 lines
	uint32_t c = o.Add ("PB-C"), d = o.Add ("PB-D");
	o.Begin ();
	o.Frame ();
	o.Frame ({ Hit (c, -1, d, 10.0, 3.0e4) });
	REQUIRE (o.s.Damage (c));
	REQUIRE (!o.s.Damage (c)->d.rec.empty ());
	CHECK (DentMath::Legacy (o.s.Damage (c)->d.rec[0].p));
	std::vector<std::string> q;
	o.s.SaveLines (q);
	for (auto &l : q) CHECK (l.find ("XDMG 2") == std::string::npos);
}

TEST_CASE ("dmg3 corner crush: mode 1, the next hit inherits it and grows P; Esurplus in [0, E]")
{
	Rig r;
	r.cfg.dentModes = r.cfg.dentNoise = r.cfg.dentFacetNormals = true;
	std::vector<std::string> order;
	auto *bk = new SinkLog ("brk", &order);
	r.s.brk.reset (bk);
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	r.Begin ();
	r.Frame ();
	CollImpactEvent e = Hit (a, -1, b, 20.0, 2.0e5);
	e.s[0].c = Vector (4.95, 4.95, 0);
	r.Frame ({ e });
	REQUIRE (r.s.Damage (a));
	const std::vector<DentRecord> &rec = r.s.Damage (a)->d.rec;
	REQUIRE (!rec.empty ());
	CHECK (rec[0].p.mode == DENTM_CRUSH);
	CHECK (rec[0].p.P > 0);
	CHECK (rec[0].p.h <= rec[0].p.P);
	size_t n0 = rec.size ();
	double P0 = rec[0].p.P;
	REQUIRE (!bk->hits.empty ());
	CHECK (bk->hits[0].mode == DENTM_CRUSH);
	CHECK (bk->hits[0].Esurplus >= 0);
	CHECK (bk->hits[0].Esurplus <= bk->hits[0].E);
	e.s[0].c = Vector (4.9, 4.9, -rec[0].p.h); // on the crushed face: mapped back to rest
	r.Frame ({ e });
	CHECK (r.s.Damage (a)->d.rec.size () <= n0 + 1);
	for (const DentRecord &x : r.s.Damage (a)->d.rec) CHECK (x.p.mode == DENTM_CRUSH); // the partner's mode is inherited
	CHECK (r.s.Damage (a)->d.rec[0].p.P >= P0);
	CHECK (ColliderExactLow (r, a));
}

TEST_CASE ("dmg3 CollVisualA crease normals: a low-valence seam vertex under a crush takes the most-turned face; welded copies equal; legacy normals bitwise")
{
	auto half = [] (CollGroupData &g, float x0) { // 11 x 11 grid over [x0, x0 + 5] x [-5, 0] in z = 0
		for (int j = 0; j <= 10; j++) for (int i = 0; i <= 10; i++) g.vtx.push_back (CollVtx { x0 + 0.5f * i, -5.0f + 0.5f * j, 0, 0, 0, 1, 0, 0 });
		for (int j = 0; j < 10; j++) for (int i = 0; i < 10; i++) {
			uint16_t a = (uint16_t)(j * 11 + i), b = (uint16_t)(a + 1), c = (uint16_t)(a + 11), d = (uint16_t)(c + 1);
			g.idx.insert (g.idx.end (), { a, b, c, b, d, c });
		}
	};
	auto m = std::make_shared<CollRestMesh> ();
	m->name = "seam";
	m->grp.resize (2);
	half (m->grp[0], -5.0f), half (m->grp[1], 0.0f);
	m->nvtx = (uint32_t)(m->grp[0].vtx.size () + m->grp[1].vtx.size ());
	CollDmgSlot s { true, DentMath::MeshKey ("seam"), 2, m->nvtx, m, "seam", 1 };
	DentRecord crush {}, bowl {};
	crush.p.c = Vector (0, -3.8, 0), crush.p.n = Vector (0, 0, 1), crush.p.R = 1.5, crush.p.h = 1.0, crush.p.T = 0;
	crush.p.mode = DENTM_CRUSH, crush.p.P = 1.0, crush.p.t = Vector (1, 0, 0);
	DentMath::Quantise (crush.p);
	bowl.p.c = Vector (0, -2.5, 0), bowl.p.n = Vector (0, 0, 1), bowl.p.R = 1.5, bowl.p.h = 0.3, bowl.p.T = 0;
	auto build = [&] (bool facet, const std::vector<const DentRecord *> &rec) {
		auto r = std::make_unique<Rig> ();
		r->cfg.dentModes = facet, r->cfg.dentFacetNormals = facet;
		uint32_t a = r->Add ("PB-A");
		r->host.slots[a] = { s };
		r->s.vis.SetRecords (a, "PB-A", s, 0, rec);
		const DentMeshCopyA *c = r->s.vis.Copy (a, 0);
		REQUIRE (c);
		return std::make_pair (c->cur, std::move (r));
	};
	auto on = build (true, { &crush });
	const auto &cur = on.first;
	const DentVtx &v0 = cur[0][10], &v1 = cur[1][0]; // corner (0, -5): one triangle in group 1, two in group 0
	REQUIRE (std::memcmp (&v0, &m->grp[0].vtx[10], 12) != 0);
	CHECK (std::memcmp (&v0, &v1, sizeof (DentVtx)) == 0); // welded copies equal
	auto P = [] (const DentVtx &d) { return Vector (d.x, d.y, d.z); };
	Vector best; double bd = 2;
	for (int g = 0; g < 2; g++) {
		const std::vector<uint16_t> &ix = m->grp[g].idx;
		uint16_t corner = g == 0 ? 10 : 0;
		for (size_t t = 0; t + 2 < ix.size (); t += 3) {
			if (ix[t] != corner && ix[t + 1] != corner && ix[t + 2] != corner) continue;
			Vector fn = crossp (P (cur[g][ix[t + 1]]) - P (cur[g][ix[t]]), P (cur[g][ix[t + 2]]) - P (cur[g][ix[t]]));
			fn = fn / fn.length ();
			if (fn.z < bd) bd = fn.z, best = fn;
		}
	}
	REQUIRE (bd < 0.999);
	CHECK (std::fabs (v0.nx - best.x) < 1e-5);
	CHECK (std::fabs (v0.ny - best.y) < 1e-5);
	CHECK (std::fabs (v0.nz - best.z) < 1e-5);
	auto off = build (false, { &crush });
	CHECK (std::memcmp (&off.first[0][10], &v0, 12) == 0); // same positions
	CHECK (std::memcmp (&off.first[0][10].nx, &v0.nx, 12) != 0); // the smooth rule differs
	auto l1 = build (true, { &bowl }), l0 = build (false, { &bowl }); // mode 0 seed 0: today's normals
	for (int g = 0; g < 2; g++) CHECK (std::memcmp (l1.first[g].data (), l0.first[g].data (), l0.first[g].size () * sizeof (DentVtx)) == 0);
	CHECK (std::memcmp (l1.first[0].data (), m->grp[0].vtx.data (), 12) == 0);
}

TEST_CASE ("dmg3 hinge copy to the aileron: the flap reaches a second part, both records fold it the same, no gap")
{
	Rig r;
	r.cfg.dentModes = true;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	Rig::Body &w = r.body[0];
	auto sheet = [] (CollGroupData &g, float x0, float x1, float y0, float y1, float z, float nz, int nx, int ny) { // one skin, normal (0, 0, nz)
		uint16_t base = (uint16_t)g.vtx.size ();
		for (int j = 0; j <= ny; j++) for (int i = 0; i <= nx; i++) g.vtx.push_back (CollVtx { x0 + (x1 - x0) * i / nx, y0 + (y1 - y0) * j / ny, z, 0, 0, nz, 0, 0 });
		for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) {
			uint16_t p = (uint16_t)(base + j * (nx + 1) + i), q = (uint16_t)(p + 1), s = (uint16_t)(p + nx + 1), t = (uint16_t)(s + 1);
			if (nz > 0) g.idx.insert (g.idx.end (), { p, q, s, q, t, s });
			else g.idx.insert (g.idx.end (), { p, s, q, q, s, t });
		}
	};
	auto m = std::make_shared<CollRestMesh> ();
	m->name = "wing";
	m->grp.resize (2);
	sheet (m->grp[0], -8, 2, -6, 6, 0, 1, 5, 6), sheet (m->grp[0], -8, 2, -6, 6, -0.64f, -1, 5, 6);
	sheet (m->grp[1], 2.6f, 3.6f, -0.5f, 0.5f, 0, 1, 1, 1), sheet (m->grp[1], 2.6f, 3.6f, -0.5f, 0.5f, -0.64f, -1, 1, 1);
	m->nvtx = (uint32_t)(m->grp[0].vtx.size () + m->grp[1].vtx.size ());
	w.mi.key = "wing", w.mi.rest = m;
	w.tv.reset (new TestVessel ()), w.mod.reset (new TestModule ());
	w.tv->coll = &w.ca;
	w.tv->meshGrp = { 2 };
	UINT an = w.tv->CreateAnimation (0);
	w.tv->AddAnimationComponent (an, 0, 1, w.mod->Lin (0, w.mod->Grp ({1}), 1, _V(0,0,1)));
	r.host.slots[a] = { CollDmgSlot { true, DentMath::MeshKey ("wing"), 2, m->nvtx, m, "wing", 1 } };
	r.Begin ();
	r.Frame ();
	REQUIRE (r.B (a).sh->PartOf (0, 0) != r.B (a).sh->PartOf (0, 1));
	CollImpactEvent e = Hit (a, -1, b, 30.0, 2.46e6); // 1.23 MJ on the wing side
	e.s[0].c = Vector (1.9, 0, 0), e.s[0].a = 0.3;
	r.Frame ({ e });
	REQUIRE (r.s.Damage (a));
	const std::vector<DentRecord> &rec = r.s.Damage (a)->d.rec;
	std::vector<const DentRecord *> hg;
	for (const DentRecord &x : rec) if (x.p.mode == DENTM_HINGE) hg.push_back (&x);
	std::string lg;
	for (auto &l : r.sdk.log) if (l.find ("Collision dent") != std::string::npos) lg += l + " | ";
	INFO ("records " << rec.size () << " hinge " << hg.size () << " " << lg);
	REQUIRE (hg.size () == 2);
	CHECK (hg[0]->grp != hg[1]->grp);
	CHECK (hg[0]->p.P > 0);
	CHECK (hg[0]->p.P == hg[1]->p.P);
	for (const CollVtx &x : m->grp[1].vtx) { // the aileron under either record moves the same: no gap at the split
		Vector p (x.x, x.y, x.z);
		Vector d0 = DentMath::Displace (hg[0]->p, p), d1 = DentMath::Displace (hg[1]->p, p);
		CHECK ((d0 - d1).length () < 1e-6);
	}
	bool moved = false;
	for (const CollVtx &x : m->grp[1].vtx) moved |= DentMath::Displace (hg[1]->p, Vector (x.x, x.y, x.z)).length () > 0.05;
	CHECK (moved);
}

// dmg3 cr3: wing plate with a separate aileron part from x0 to x1 (animated group 1)
static std::shared_ptr<CollRestMesh> Cr3Wing (Rig &r, uint32_t a, float x0, float x1)
{
	Rig::Body &w = r.body[0];
	auto sheet = [] (CollGroupData &g, float xa, float xb, float y0, float y1, float z, float nz, int nx, int ny) {
		uint16_t base = (uint16_t)g.vtx.size ();
		for (int j = 0; j <= ny; j++) for (int i = 0; i <= nx; i++) g.vtx.push_back (CollVtx { xa + (xb - xa) * i / nx, y0 + (y1 - y0) * j / ny, z, 0, 0, nz, 0, 0 });
		for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) {
			uint16_t p = (uint16_t)(base + j * (nx + 1) + i), q = (uint16_t)(p + 1), s = (uint16_t)(p + nx + 1), t = (uint16_t)(s + 1);
			if (nz > 0) g.idx.insert (g.idx.end (), { p, q, s, q, t, s });
			else g.idx.insert (g.idx.end (), { p, s, q, q, s, t });
		}
	};
	auto m = std::make_shared<CollRestMesh> ();
	m->name = "wing";
	m->grp.resize (2);
	sheet (m->grp[0], -8, 2, -6, 6, 0, 1, 5, 6), sheet (m->grp[0], -8, 2, -6, 6, -0.64f, -1, 5, 6);
	sheet (m->grp[1], x0, x1, -0.5f, 0.5f, 0, 1, 1, 1), sheet (m->grp[1], x0, x1, -0.5f, 0.5f, -0.64f, -1, 1, 1);
	m->nvtx = (uint32_t)(m->grp[0].vtx.size () + m->grp[1].vtx.size ());
	w.mi.key = "wing", w.mi.rest = m;
	w.tv.reset (new TestVessel ()), w.mod.reset (new TestModule ());
	w.tv->coll = &w.ca;
	w.tv->meshGrp = { 2 };
	UINT an = w.tv->CreateAnimation (0);
	w.tv->AddAnimationComponent (an, 0, 1, w.mod->Lin (0, w.mod->Grp ({1}), 1, _V(0,0,1)));
	r.host.slots[a] = { CollDmgSlot { true, DentMath::MeshKey ("wing"), 2, m->nvtx, m, "wing", 1 } };
	return m;
}

static std::vector<const DentRecord *> Cr3Hinges (Rig &r, uint32_t a)
{
	std::vector<const DentRecord *> hg;
	for (const DentRecord &x : r.s.Damage (a)->d.rec) if (x.p.mode == DENTM_HINGE) hg.push_back (&x);
	return hg;
}

TEST_CASE ("dmg3 cr3 M2: the hinge reaches an aileron beyond the bowl radius")
{
	Rig r;
	r.cfg.dentModes = true;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	auto m = Cr3Wing (r, a, 6.5f, 7.5f);
	r.Begin ();
	r.Frame ();
	CollImpactEvent e = Hit (a, -1, b, 30.0, 2.46e6);
	e.s[0].c = Vector (1.9, 0, 0), e.s[0].a = 0.3;
	r.Frame ({ e });
	REQUIRE (r.s.Damage (a));
	std::vector<const DentRecord *> hg = Cr3Hinges (r, a);
	REQUIRE (!hg.empty ());
	INFO ("R " << hg[0]->p.R << " hinges " << hg.size ());
	REQUIRE (hg[0]->p.R + 0.8 < 7.0 - 1.9); // the aileron lies outside the bowl reach
	REQUIRE (hg.size () == 2);
	CHECK (hg[0]->grp != hg[1]->grp);
	for (const CollVtx &x : m->grp[1].vtx) CHECK ((DentMath::Displace (hg[0]->p, Vector (x.x, x.y, x.z)) - DentMath::Displace (hg[1]->p, Vector (x.x, x.y, x.z))).length () < 1e-6);
}


// dmg3 tear (design-CA-dmg3-tear 7, tests 5-7)
static DentRecord PlateCut (const Rig &r)
{
	DentRecord c {};
	c.p.mode = DENTM_CUT, c.p.c = Vector (0, 0, -0.2), c.p.n = Vector (0, 0, 1), c.p.t = Vector (1, 0, 0), c.p.R = 2, c.p.h = 0;
	c.p.P = 0.05, c.p.hd = 0.3, c.p.hz = 0.1, c.p.seed = 0x51u;
	c.slot = 0, c.key = DentMath::MeshKey ("plate"), c.ngrp = 1, c.nvtx = r.plate->nvtx;
	return c;
}

TEST_CASE ("dmg3 tear 5: no growth below a cut; anyMode never forces a cut", "[dmg3][tear]")
{
	Rig r;
	DentRecord b {};
	b.p.c = Vector (0.1, 0.1, 0), b.p.n = Vector (0, 0, 1), b.p.R = 1, b.p.h = 0.05;
	b.slot = 0, b.key = DentMath::MeshKey ("plate"), b.ngrp = 1, b.nvtx = r.plate->nvtx;
	DentRecord c = PlateCut (r);
	std::vector<DentRecord> l { b };
	CHECK (DentMath::FindCoalesce (l, b) == 0);
	l.push_back (c);
	CHECK (DentMath::FindCoalesce (l, b) == -1);                          // record 0 is below the cut
	CHECK (DentMath::FindCoalesce (l, b, nullptr, true) == -1);
	DentRecord nc = c; nc.p.c = Vector (0, 0, -0.19);
	std::vector<DentRecord> lc { c };
	CHECK (DentMath::FindCoalesce (lc, nc, nullptr, true) == -1);        // a cut never grows
	DentRecord probe = b; probe.p.mode = DENTM_BOWL;
	CHECK (DentMath::FindCoalesce (lc, probe, nullptr, true) == -1);       // anyMode: no cut partner, so no force = 3
}

TEST_CASE ("dmg3 tear 6: a dent on the stump starts from the post-cut rest", "[dmg3][tear]")
{
	Rig r;
	uint32_t a = r.Add ("PB-A"), b = r.Add ("PB-B");
	r.Begin ();
	r.Frame ();
	REQUIRE (r.s.AddCut (a, PlateCut (r), true));
	REQUIRE (ColliderExact (r, a));
	CollImpactEvent e = Hit (a, -1, b, 10.0, 3.0e4);
	double zc = -0.2 - 0.1 * (0.2 / 0.3);
	e.s[0].c = Vector (0.1, 0.1, zc);
	r.Frame ({ e });
	const VesselDamageA *v = r.s.Damage (a);
	REQUIRE (v);
	REQUIRE (v->d.rec.size () >= 2);
	CHECK (v->d.rec[0].p.mode == DENTM_CUT);
	const DentRecord &d = v->d.rec.back ();
	CHECK (d.p.mode != DENTM_CUT);
	CHECK (std::fabs (d.p.c.z - zc) < 1e-6);                               // on the stump face, not mapped back through the cut
	CHECK (d.p.h > 0);
	CHECK (ColliderExact (r, a));
	CHECK (DentMath::DisplaceLow (v->d.rec[0].p, Vector (0.1, 0.1, 0)).length () == 0.0); // the cap view sees no bowl from the cut
}

TEST_CASE ("dmg3 tear 7: AddCut at DENT_MAX_VESSEL is refused; inside Dent it is deferred", "[dmg3][tear]")
{
	Rig r;
	uint32_t a = r.Add ("PB-A");
	r.Begin ();
	r.Frame ();
	DentRecord c = PlateCut (r);
	for (uint32_t k = 0; k < DENT_MAX_VESSEL; k++) { DentRecord x = c; x.p.mode = DENTM_BOWL; x.p.seed = 0; x.p.P = x.p.hd = x.p.hz = 0; x.p.t = Vector (); x.p.h = 0.001; x.p.c = Vector (-4 + 0.01 * k, 0, 0); REQUIRE (r.s.AddCut (a, x, true)); }
	CHECK (!r.s.AddCut (a, c, true));
	CHECK (r.s.Damage (a)->d.rec.size () == DENT_MAX_VESSEL);
	Rig q;
	uint32_t qa = q.Add ("PB-A"), qb = q.Add ("PB-B");
	q.cfg.brk = false;
	q.Begin ();
	q.Frame ();
	struct Sink : CollDmgSink {
		CollDmgSession *s = nullptr; DentRecord c; size_t during = 0, before = 0; bool called = false, deferred = false;
		void Hit (const CollDamageHit &h) override { if (called) return; called = true; before = s->Damage (h.id)->d.rec.size (); deferred = s->AddCut (h.id, c, false) && s->PendingCuts () == 1; during = s->Damage (h.id)->d.rec.size (); }
	};
	auto *sk = new Sink (); sk->s = &q.s; sk->c = PlateCut (q);
	q.s.brk.reset (sk);
	q.Frame ({ Hit (qa, -1, qb, 10.0, 3.0e4) });
	REQUIRE (sk->called);
	CHECK (sk->deferred);
	CHECK (sk->during == sk->before);                                      // not appended inside Dent
	CHECK (q.s.PendingCuts () == 0);
	bool cut = false; for (auto &x : q.s.Damage (qa)->d.rec) if (x.p.mode == DENTM_CUT) cut = true;
	CHECK (cut);                                                           // applied after the solve returned
	CHECK (ColliderExact (q, qa));
}

namespace { // blast: four cells over the plate
DentSites PlateSites () { DentSites s; s.slot = 0, s.key = DentMath::MeshKey ("plate"); s.s = { Vector (-2.5, -2.5, 0.5), Vector (2.5, -2.5, 0.5), Vector (-2.5, 2.5, 0.5), Vector (2.5, 2.5, 0.5) }; return s; }
bool ColliderVCut (Rig &r, uint32_t id) // fresh collider: records in order, each VCUT set once at its first record
{
	auto &b = r.B (id);
	CollShape fresh;
	CollMeshInfo mi = b.mi;
	CollAnim ca;
	CollTemplateCache cache;
	fresh.Update (&mi, 1, ca, nullptr, 0, cache);
	const VesselDamageA *v = r.s.Damage (id);
	std::vector<const DentRecord *> l;
	if (v) for (const DentRecord &x : v->d.rec) l.push_back (&x);
	DentVCut vc;
	DentMath::VCutSet (l, r.s.Sites (id, 0), vc);
	bool rm = false, kp = false;
	for (const DentRecord *x : l) {
		std::vector<uint32_t> g (x->grp.begin (), x->grp.end ());
		if (x->p.mode == DENTM_VCUT) {
			bool k = (x->p.bits & DENTC_KEEP) != 0;
			if (k ? kp : rm) continue;
			(k ? kp : rm) = true;
			DentVCut c = vc; c.on = k;
			fresh.ApplyMap (0, g.data (), g.size (), DentMath::MapVCut, &c);
		} else if (x->p.mode == DENTM_CUT) fresh.ApplyMap (0, g.data (), g.size (), DentMath::MapLow, x);
		else fresh.ApplyDent (0, g.data (), g.size (), DentMath::FieldLow, x);
	}
	if (fresh.nPart () != b.sh->nPart ()) return false;
	for (uint32_t p = 0; p < fresh.nPart (); p++) {
		const CollGeom &A = fresh.Part (p).Geom (), &B = b.sh->Part (p).Geom ();
		if (A.vtx.size () != B.vtx.size ()) return false;
		for (size_t i = 0; i < A.vtx.size (); i++) if (std::memcmp (&A.vtx[i], &B.vtx[i], sizeof (Vector))) return false;
	}
	return true;
}
size_t MovedCollider (Rig &r, uint32_t id)
{
	auto &b = r.B (id);
	size_t n = 0;
	for (uint32_t p = 0; p < b.sh->nPart (); p++) { const CollGeom &A = b.sh->Part (p).Geom (); for (uint32_t i = 0; i < A.vtx.size (); i++) { Vector x = A.RestPos (i), y = A.Pos (i); if (x.x != y.x || x.y != y.y || x.z != y.z) n++; } }
	return n;
}
bool MirrorVCut (Rig &r, uint32_t id) // the client copy equals rest plus Fold with the sites
{
	const DentMeshCopyA *c = r.s.vis.Copy (id, 0);
	const VesselDamageA *v = r.s.Damage (id);
	if (!c || !v) return false;
	std::vector<const DentParams *> l;
	for (const DentRecord &x : v->d.rec) l.push_back (&x.p);
	for (size_t i = 0; i < c->rp[0].size (); i++) {
		Vector x (c->rp[0][i].x, c->rp[0][i].y, c->rp[0][i].z), y = x + DentMath::Fold (l.data (), l.size (), x, true, nullptr, r.s.Sites (id, 0));
		if (c->cur[0][i].x != (float)y.x || c->cur[0][i].y != (float)y.y || c->cur[0][i].z != (float)y.z) return false;
	}
	return true;
}
}

TEST_CASE ("blast AddCellCuts: one VCUT record per cell, X events, collider and mirror flushed, deferred inside Dent; V K played back", "[blast]")
{
	auto dir = std::filesystem::temp_directory_path () / "collD_blast";
	std::filesystem::remove_all (dir);
	std::vector<std::string> playScn, saved;
	std::vector<DentRecord> recorded;
	DentSites st = PlateSites ();
	{
		Rig r;
		r.s.sideDir = dir.string ();
		r.cfg.testRecId = "BLAST1";
		uint32_t a = r.Add ("PB-A");
		r.Begin ();
		r.Frame ();
		r.B (a).v->recording = true;
		r.s.SaveLines (playScn);
		r.s.AddCellCuts (a, 0, { 1 });                                      // no sites yet: nothing
		CHECK (r.s.Damage (a)->d.rec.empty ());
		r.s.SetSites (a, st);
		REQUIRE (r.s.Sites (a, 0));
		r.s.AddBrokenBonds (a, 0, { 7, 3, 7 });
		r.s.AddCellCuts (a, 0, { 1, 1, 9 });                                // a duplicate and a cell past the site count
		const VesselDamageA *v = r.s.Damage (a);
		REQUIRE (v->d.rec.size () == 1);
		const DentRecord &x = v->d.rec[0];
		CHECK (x.p.mode == DENTM_VCUT); CHECK (x.p.P == 1.0); CHECK (x.p.seed == 4); CHECK (x.p.h == 0.0); CHECK (x.p.bits == 0u);
		CHECK (std::memcmp (&x.p.c, &st.s[1], sizeof (Vector)) == 0);
		CHECK (x.grp.empty ());                                            // one pose class: all groups
		CHECK (x.p.hz > 0); CHECK (x.p.hd > 0);
		CHECK (v->match[0] == 0);
		size_t m1 = MovedCollider (r, a);
		CHECK (m1 > 0);
		CHECK (ColliderVCut (r, a));
		CHECK (MirrorVCut (r, a));
		r.s.AddCellCuts (a, 0, { 3, 1 });                                   // 1 is cut already
		REQUIRE (v->d.rec.size () == 2);
		CHECK (v->d.rec[1].p.P == 3.0);
		CHECK (MovedCollider (r, a) > m1);
		CHECK (ColliderVCut (r, a));                                       // a second VCUT replays the slot: one union map
		CHECK (MirrorVCut (r, a));
		r.Frame ();
		CHECK (ColliderVCut (r, a));
		r.s.SaveLines (saved);
		bool S = false, K = false, x2 = false;
		for (auto &l : saved) { CHECK (l.size () <= 200); S = S || l.find ("XDMGM S 0 ") != std::string::npos; K = K || l.find ("XDMGM K 0 3,7") != std::string::npos; x2 = x2 || l.find ("XDMG 2 ") != std::string::npos; }
		CHECK (S); CHECK (K); CHECK (x2);
		recorded = v->d.rec;
		r.s.End ();
	}
	REQUIRE (std::filesystem::exists (dir / "BLAST1.txt"));
	{
		std::ifstream f (dir / "BLAST1.txt");
		std::stringstream ss;
		ss << f.rdbuf ();
		std::string txt = ss.str ();
		size_t vv = txt.find (" V "), k = txt.find (" K "), d = txt.find (" D "), x = txt.find (" X ");
		CHECK (vv != std::string::npos); CHECK (k != std::string::npos);
		CHECK (d != std::string::npos); CHECK (x != std::string::npos);
		CHECK (vv < d); CHECK (d < x);
	}
	Rig p;
	p.s.sideDir = dir.string ();
	uint32_t a = p.Add ("PB-A");
	p.B (a).v->playback = true;
	CollStoreBlock blk;
	size_t pos = 0;
	REQUIRE (CollStore::Parse ([&] (std::string &l) { if (pos >= playScn.size ()) return false; l = playScn[pos++]; return true; }, blk));
	p.Begin (std::move (blk));
	for (int k = 0; k < 6; k++) p.Frame ();
	REQUIRE (p.s.Damage (a));
	REQUIRE (p.s.Damage (a)->d.rec.size () == recorded.size ());
	for (size_t i = 0; i < recorded.size (); i++) CHECK (std::memcmp (&p.s.Damage (a)->d.rec[i].p, &recorded[i].p, sizeof (DentParams)) == 0);
	REQUIRE (p.s.Sites (a, 0));
	CHECK (p.s.Sites (a, 0)->s.size () == 4);
	REQUIRE (p.s.BrokenBonds (a, 0));
	CHECK (*p.s.BrokenBonds (a, 0) == std::vector<uint32_t> { 3, 7 });
	CHECK (ColliderVCut (p, a));
	CHECK (MirrorVCut (p, a));
	std::filesystem::remove_all (dir);
	Rig q; // reload: sites, bonds and records from the saved block
	uint32_t qa = q.Add ("PB-A");
	CollStoreBlock qb;
	pos = 0;
	REQUIRE (CollStore::Parse ([&] (std::string &l) { if (pos >= saved.size ()) return false; l = saved[pos++]; return true; }, qb));
	q.Begin (std::move (qb));
	q.Frame ();
	REQUIRE (q.s.Damage (qa));
	CHECK (q.s.Damage (qa)->d.rec.size () == 2);
	CHECK (q.s.Sites (qa, 0));
	CHECK (ColliderVCut (q, qa));
	CHECK (MirrorVCut (q, qa));
	Rig w; // inside Dent: deferred until the solve returns
	uint32_t wa = w.Add ("PB-A"), wb = w.Add ("PB-B");
	w.cfg.brk = false;
	w.Begin ();
	w.Frame ();
	w.s.SetSites (wa, st);
	struct Sink : CollDmgSink {
		CollDmgSession *s = nullptr; size_t during = 0, before = 0; bool called = false, deferred = false;
		void Hit (const CollDamageHit &h) override { if (called) return; called = true; before = s->Damage (h.id)->d.rec.size (); s->AddCellCuts (h.id, 0, { 2 }); deferred = s->PendingCuts () == 1; during = s->Damage (h.id)->d.rec.size (); }
	};
	auto *sk = new Sink (); sk->s = &w.s;
	w.s.brk.reset (sk);
	w.Frame ({ Hit (wa, -1, wb, 10.0, 3.0e4) });
	REQUIRE (sk->called);
	CHECK (sk->deferred);
	CHECK (sk->during == sk->before);
	CHECK (w.s.PendingCuts () == 0);
	bool vc = false; for (auto &x : w.s.Damage (wa)->d.rec) if (x.p.mode == DENTM_VCUT && x.p.P == 2.0) vc = true;
	CHECK (vc);
	CHECK (ColliderVCut (w, wa));
}

TEST_CASE ("ground: a ground event dents the vessel side with the block material, the ground side is skipped", "[ground]")
{
	Rig r;
	uint32_t a = r.Add ("PB-A");
	r.host.bases.push_back (CollDmgBaseObj { "Earth", "Habana", "BLOCK", 0, 0, 1, DENTB_BLOCK, Vector (10, 10, 10), 0, 0, 0, nullptr, (CollH)0x77 });
	r.Begin ();
	r.Frame ();
	CollImpactEvent e = Hit (a, 0, 0, 10.0, 3.0e4);
	e.s[1].owner = CollOwnerRef { 0, 0, -1, -1, -1 };
	e.s[1].mesh = e.s[1].grp = e.s[1].tri = -1;
	r.Frame ({ e });
	const VesselDamageA *v = r.s.Damage (a);
	REQUIRE (v);
	double E[2], ea[2], Ev[2], eav[2];
	DentMath::SplitEnergy (3.0e4, 0, 10.0, true, DentMath::DefaultMaterial (-1), DentMath::DefaultMaterial (DENTB_BLOCK), E, ea);
	DentMath::SplitEnergy (3.0e4, 0, 10.0, true, DentMath::DefaultMaterial (-1), DentMath::DefaultMaterial (-1), Ev, eav);
	CHECK (ea[0] != eav[0]);
	CHECK (v->d.eabs == ea[0]);
	CHECK (v->d.rec.size () >= 1);
	CHECK (r.s.Buildings ().empty ());
	CHECK (ColliderExact (r, a));
	CHECK (r.sdk.Logged ("Collision dent t="));
	CHECK_FALSE (r.sdk.Logged ("Collision building"));
}

TEST_CASE ("custom-fix B4: blast cell debris rows are recorded only; playback hands them to P and never saves them", "[blast]")
{
	auto dir = std::filesystem::temp_directory_path () / "collD_cellrow";
	std::filesystem::remove_all (dir);
	std::vector<std::string> playScn, saved;
	DentTorn tr;
	tr.kind = CBRK_CELL, tr.slot = 0, tr.key = DentMath::MeshKey ("plate"), tr.ngrp = 1, tr.nvtx = 441, tr.simt = 1.5, tr.debris = "PB-A_D1";
	tr.kin = true, tr.dv = Vector (0.5, 0, -1), tr.dw = Vector (0, 0.25, 0), tr.mass = 12.5, tr.cells = { 3, 7 }, tr.pieces = {}, tr.c = Vector (0.5, -0.25, 1), tr.crushed = true;
	{
		Rig r;
		r.s.sideDir = dir.string ();
		r.cfg.testRecId = "CELL1";
		std::vector<std::string> order;
		r.s.brk.reset (new SinkLog ("brk", &order));
		uint32_t a = r.Add ("PB-A");
		r.Begin ();
		r.Frame ();
		r.B (a).v->recording = true;
		r.s.SaveLines (playScn);
		r.s.RecordTorn (a, tr);
		CHECK ((!r.s.Damage (a) || r.s.Damage (a)->d.torn.empty ()));  // not a saved row
		r.s.SaveLines (saved);
		for (auto &l : saved) CHECK (l.find ("XDMGM T ") == std::string::npos);
		r.s.End ();
	}
	REQUIRE (std::filesystem::exists (dir / "CELL1.txt"));
	{
		std::ifstream f (dir / "CELL1.txt");
		std::stringstream ss;
		ss << f.rdbuf ();
		CHECK (ss.str ().find (" T ") != std::string::npos);
		CHECK (ss.str ().find (" C=3,7 ") != std::string::npos);
	}
	Rig p;
	std::vector<std::string> order2;
	auto *bk = new SinkLog ("brk", &order2);
	p.s.brk.reset (bk);
	p.s.sideDir = dir.string ();
	uint32_t a = p.Add ("PB-A");
	p.B (a).v->playback = true;
	CollStoreBlock blk;
	size_t pos = 0;
	REQUIRE (CollStore::Parse ([&] (std::string &l) { if (pos >= playScn.size ()) return false; l = playScn[pos++]; return true; }, blk));
	p.Begin (std::move (blk));
	for (int k = 0; k < 6; k++) p.Frame ();
	REQUIRE (bk->torn.size () == 1);                                    // to P
	const DentTorn &o = bk->torn[0];
	CHECK (o.kind == CBRK_CELL); CHECK (o.debris == "PB-A_D1"); CHECK (o.kin); CHECK (o.mass == 12.5);
	CHECK (o.cells == tr.cells); CHECK (o.crushed); CHECK (o.c.x == 0.5); CHECK (o.c.y == -0.25); CHECK (o.c.z == 1);
	CHECK ((!p.s.Damage (a) || p.s.Damage (a)->d.torn.empty ()));      // never a saved row
	std::filesystem::remove_all (dir);
}
