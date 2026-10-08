// not upstream: collision addon, unit tests of E2's session sources on the fake SDK (design E2-U3 ... E2-U7, E2-U11, E2-U12)
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include "CollBaseA.h"
#include "CollFakeSdk.h"
#include "CollSourceA.h"

namespace {
CollGroupData Box (float s)
{
	CollGroupData g;
	float c[8][3] = { {-s,-s,-s},{s,-s,-s},{s,s,-s},{-s,s,-s},{-s,-s,s},{s,-s,s},{s,s,s},{-s,s,s} };
	for (auto &p : c) g.vtx.push_back (CollVtx { p[0], p[1], p[2], 0, 0, 1, 0, 0 });
	uint16_t f[36] = { 0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1, 3,2,6, 3,6,7, 0,3,7, 0,7,4, 1,5,6, 1,6,2 };
	g.idx.assign (f, f + 36);
	return g;
}
std::string BoxMsh ()
{
	std::string s = "MSHX1\nGROUPS 1\nGEOM 8 12\n";
	float c[8][3] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} };
	char b[128];
	for (auto &p : c) { snprintf (b, sizeof b, "%g %g %g 0 0 1 0 0\n", p[0], p[1], p[2]); s += b; }
	s += "0 1 2\n0 2 3\n4 6 5\n4 7 6\n0 4 5\n0 5 1\n3 2 6\n3 6 7\n0 3 7\n0 7 4\n1 5 6\n1 6 2\n";
	return s;
}
struct Sink : CollShapeSink {
	std::vector<std::pair<uint32_t, std::vector<CollSlotEvent>>> got;
	void ShapesUpdated (uint32_t id, CollShape *, const std::vector<CollSlotEvent> &ev) override { got.push_back ({ id, ev }); }
};
CollCfgValues Cfg () { CollCfgValues c; c.model = 1; return c; }
}

TEST_CASE ("E2-U3 slot poll, holes, serials, probe bits", "[CollSourceA]")
{
	CollFakeSdk s;
	s.File (".\\Meshes\\box.msh", BoxMsh ());
	auto *v = s.AddVessel ("V1", "Test");
	const auto *T = s.AddTpl ("tplbox", { Box (2) });
	v->slot.resize (3);
	v->slot[0].kind = CollFakeSdk::NAME; v->slot[0].name = "box"; v->slot[0].ofs = Vector (-0.0, 1.5, 1e-310);
	v->slot[2].kind = CollFakeSdk::TPL; v->slot[2].tpl = T;
	CollGeomSession g (s, Cfg ());
	g.ReadOrbiterCfg ();
	g.SimulationStart (0, false);
	Sink k;
	g.BeginFrame (0, 0.1); g.Deliver (k);
	REQUIRE (s.misuse == 0);
	REQUIRE (k.got.size () == 1);
	REQUIRE (k.got[0].second.size () == 2);
	REQUIRE (g.Slots (0)[0].present);
	REQUIRE (g.Slots (0)[0].nvtx == 8);
	REQUIRE (g.Slots (0)[2].kind == SLOT_TPL);
	REQUIRE (g.Geom (0)->shape != nullptr);
	uint32_t ser0 = g.Slots (0)[0].serial, ser = 0;
	REQUIRE (g.SlotNow (0, 0, ser)); REQUIRE (ser == ser0);
	REQUIRE (!g.SlotNow (0, 1, ser));
	for (int i = 0; i < 1000; i++) { g.BeginFrame (0, 0.1); g.Deliver (k); }
	REQUIRE (std::signbit (v->slot[0].ofs.x));
	REQUIRE (v->slot[0].ofs.z == 1e-310);
	uint64_t probes = s.Count ().n[CSK_PROBE];
	REQUIRE (probes >= 1000);
	REQUIRE (s.Count ().Writes () == 0);
	// delete, hole reuse by a template, ClearMeshes
	k.got.clear ();
	v->slot[0].kind = CollFakeSdk::HOLE;
	REQUIRE (!g.SlotNow (0, 0, ser));
	g.BeginFrame (0, 0.1); g.Deliver (k);
	REQUIRE (k.got[0].second.size () == 1);
	REQUIRE (k.got[0].second[0].what == SLOTEV_GONE);
	v->slot[0].kind = CollFakeSdk::TPL; v->slot[0].tpl = T;
	g.BeginFrame (0, 0.1); g.Deliver (k);
	REQUIRE (k.got[1].second.size () == 1);
	REQUIRE (k.got[1].second[0].what == SLOTEV_REPLACED);
	REQUIRE (g.Slots (0)[0].serial == ser0 + 1);
	v->slot.clear ();
	g.BeginFrame (0, 0.1); g.Deliver (k);
	REQUIRE (k.got[2].second.size () == 2);
	REQUIRE (s.misuse == 0);
}

TEST_CASE ("E2-U3 MeshProbe ONCE", "[CollSourceA]")
{
	CollFakeSdk s;
	s.File (".\\Meshes\\box.msh", BoxMsh ());
	auto *v = s.AddVessel ("V1", "Once");
	v->slot.resize (1);
	v->slot[0].kind = CollFakeSdk::NAME; v->slot[0].name = "box";
	CollCfgValues c = Cfg (); c.meshProbeOnce = { "Once" };
	CollGeomSession g (s, c);
	g.SimulationStart (0, false);
	for (int i = 0; i < 10; i++) g.BeginFrame (0, 0.1);
	REQUIRE (s.Count ().n[CSK_PROBE] == 1);
}

TEST_CASE ("E2-U3 MeshProbe ONCE, DelMesh leaves a hole without a count change", "[CollSourceA]")
{
	CollFakeSdk s;
	s.File (".\\Meshes\\box.msh", BoxMsh ());
	auto *v = s.AddVessel ("V1", "Once");
	v->slot.resize (2);
	v->slot[0].kind = CollFakeSdk::NAME; v->slot[0].name = "box";
	v->slot[1].kind = CollFakeSdk::NAME; v->slot[1].name = "box";
	CollCfgValues c = Cfg (); c.meshProbeOnce = { "Once" };
	CollGeomSession g (s, c);
	g.SimulationStart (0, false);
	g.BeginFrame (0, 0.1);
	REQUIRE (s.misuse == 0);
	v->slot[0].kind = CollFakeSdk::HOLE;
	for (int i = 0; i < 5; i++) g.BeginFrame (0, 0.1);
	REQUIRE (s.misuse == 0);
	REQUIRE (g.Slots (0)[0].present);
	v->slot.resize (3); // a count change probes again and sees the hole
	g.BeginFrame (0, 0.1);
	REQUIRE (s.misuse == 0);
	REQUIRE (!g.Slots (0)[0].present);
}

TEST_CASE ("E2-U11 class keys of vessels without a class name", "[CollSourceA]")
{
	CollFakeSdk s;
	s.File (".\\Config\\Vessels\\A.cfg", "EnableCollider = FALSE\nDockZoneRadius = 3\n");
	s.File (".\\Config\\Vessels\\B.cfg", "DockZoneRadius = 4\n");
	s.AddVessel ("A"); s.AddVessel ("B"); s.AddVessel ("C");
	CollGeomSession g (s, Cfg ());
	g.SimulationStart (0, false);
	g.BeginFrame (0, 0.1);
	REQUIRE (!g.Keys (0).enableCollider);
	REQUIRE (g.Keys (0).dockZoneRadius == 3);
	REQUIRE (g.Keys (1).enableCollider);
	REQUIRE (g.Keys (1).dockZoneRadius == 4);
	REQUIRE (g.Keys (2).dockZoneRadius == 1.5);
}

TEST_CASE ("E2-U4 animation diff and E2-U6 prediction", "[CollSourceA]")
{
	CollFakeSdk s;
	auto *v = s.AddVessel ("V1");
	const auto *T = s.AddTpl ("door", { Box (1), Box (0.5f) });
	v->slot.resize (1); v->slot[0].kind = CollFakeSdk::TPL; v->slot[0].tpl = T;
	TestModule mod;
	UINT an = v->anim.CreateAnimation (0);
	auto *rot = mod.Rot (0, mod.Grp ({ 1 }), 1, _V (0, 0, 0), _V (0, 1, 0), (float)1.0);
	v->anim.AddAnimationComponent (an, 0, 1, rot);
	CollGeomSession g (s, Cfg ());
	g.SimulationStart (0, false);
	Sink k;
	g.BeginFrame (0, 0.1); g.Deliver (k);
	REQUIRE (g.GroupAnimated (0, 0, 1));
	REQUIRE (!g.GroupAnimated (0, 0, 0));
	double st = 0;
	std::vector<CollAffine> pred;
	for (int f = 0; f < 5; f++) {
		st += 0.05; v->anim.SetAnimation (an, st);
		g.BeginFrame (0, 0.1); g.Deliver (k);
		const CollVesselGeom *G = g.Geom (0);
		REQUIRE (G->shape);
		if (f >= 2) {
			for (uint32_t p = 0; p < G->shape->nPart (); p++) {
				const CollAffine &a = pred[p], &b = G->shape->Part (p).pose[1];
				for (int e = 0; e < 9; e++) REQUIRE (std::fabs (a.A.data[e] - b.A.data[e]) < 1e-12);
			}
		}
		pred = G->next;
	}
	// a module edit of the axis in HEADLESS is a re-snapshot (Version bump)
	uint64_t ver = v->anim.anim[0].comp[0]->trans ? 0 : 1;
	(void)ver;
	rot->axis = _V (1, 0, 0);
	g.BeginFrame (0, 0.1);
	g.TimeJump ();
	g.BeginFrame (0, 0.1);
	REQUIRE (s.misuse == 0);
}

TEST_CASE ("E2-U7 assemblies", "[CollSourceA]")
{
	CollFakeSdk s;
	auto *a = s.AddVessel ("A"); auto *b = s.AddVessel ("B"); auto *c = s.AddVessel ("C"); s.AddVessel ("D");
	int svx = 0;
	a->sv = b->sv = &svx;
	a->dock.push_back ({ Vector (0, 0, 1), Vector (0, 0, 1), Vector (0, 1, 0), b });
	b->dock.push_back ({ Vector (0, 0, -1), Vector (0, 0, -1), Vector (0, 1, 0), a });
	CollAttInfo ci {}; ci.mate = a; strcpy (ci.id, "XS");
	c->att[1].push_back (ci);
	CollAttInfo pi {}; pi.mate = c; strcpy (pi.id, "XS");
	a->att[0].push_back (pi);
	CollGeomSession g (s, Cfg ());
	g.SimulationStart (0, false);
	g.BeginFrame (0, 0.1);
	REQUIRE (g.Assemblies ().size () == 2);
	const CollAssembly *big = nullptr;
	for (auto &x : g.Assemblies ()) if (x.member.size () == 3) big = &x;
	REQUIRE (big);
	REQUIRE ((big->flags & ASM_STACK));
	REQUIRE ((big->flags & ASM_CHANGED));
	REQUIRE (g.Ports ().size () == 2);
	REQUIRE (g.Ports ()[0].matePort == 0);
	REQUIRE (g.AttachPoints ().size () == 2);
	g.BeginFrame (0, 0.1);
	for (auto &x : g.Assemblies ()) REQUIRE (!(x.flags & ASM_CHANGED));
	b->sv = nullptr; a->sv = nullptr; a->dock[0].mate = nullptr; b->dock[0].mate = nullptr;
	g.BeginFrame (0, 0.1);
	REQUIRE (g.Assemblies ().size () == 3);
}

TEST_CASE ("E2-U11 sessions, pending records, MeshDir", "[CollSourceA]")
{
	CollFakeSdk s;
	s.File ("Orbiter.cfg", "MeshDir = Other\nConfigDir = Cfg\n");
	s.File ("Other\\box.msh", BoxMsh ());
	s.File ("Cfg\\Vessels\\Tst.cfg", "ClassName = Tst\nEnableCollider = FALSE\nDockZoneRadius = 3\n");
	auto *v = s.AddVessel ("V1");
	v->slot.resize (1); v->slot[0].kind = CollFakeSdk::NAME; v->slot[0].name = "box";
	auto *w = s.AddVessel ("W", "Tst");
	{
		CollGeomSession g (s, Cfg ());
		g.ReadOrbiterCfg ();
		g.NewVessel (0, v);          // before the start
		REQUIRE (g.Slots (0)[0].present);
		g.SimulationStart (0, false);
		g.BeginFrame (0, 0.1);
		REQUIRE (g.Keys (1).dockZoneRadius == 3);
		REQUIRE (!g.Keys (1).enableCollider);
		REQUIRE (g.Geom (1)->shape == nullptr);
		g.NewVessel (7, w); g.DeleteVessel (7);
		s.log.clear ();
		size_t nc = s.calls.size ();
		g.EndSession ();
		REQUIRE (s.calls.size () == nc);
	}
	CollGeomSession g2 (s, Cfg ());
	g2.SimulationStart (0, false);
	g2.BeginFrame (0, 0.1);
	REQUIRE (!g2.Slots (0)[0].present); // default MeshDir: no file
	REQUIRE (s.misuse == 0);
}

TEST_CASE ("E2-U12 ShapesUpdated once per vessel, ClientMeshRebuilt applied once", "[CollSourceA]")
{
	CollFakeSdk s;
	auto *v = s.AddVessel ("V1");
	const auto *T = s.AddTpl ("box", { Box (1) });
	v->slot.resize (1); v->slot[0].kind = CollFakeSdk::TPL; v->slot[0].tpl = T;
	s.AddVessel ("V2");
	CollGeomSession g (s, Cfg ());
	g.SimulationStart (0, false);
	Sink k;
	g.BeginFrame (0, 0.1); g.Deliver (k);
	REQUIRE (k.got.size () == 2);
	REQUIRE (k.got[1].second.empty ());
	uint32_t ser = g.Slots (0)[0].serial;
	g.ClientMeshRebuilt (0, 0, E3_SENTINEL);
	g.ClientMeshRebuilt (0, 0, E3_SENTINEL);
	k.got.clear ();
	g.BeginFrame (0, 0.1); g.Deliver (k);
	REQUIRE (k.got[0].second.size () == 1);
	REQUIRE (k.got[0].second[0].what == SLOTEV_REBUILT);
	REQUIRE (g.Slots (0)[0].serial == ser + 1);
	CollCfgValues c0; c0.model = 0;
	CollGeomSession z (s, c0);
	z.SimulationStart (0, false);
	z.BeginFrame (0, 0.1);
	REQUIRE (z.Geom (0)->shape == nullptr);
	REQUIRE (z.Slots (0).empty ());
}

TEST_CASE ("E2-U5 rewind returns the replica to defstate", "[CollSourceA]")
{
	TestVessel tv; TestModule mod; CollAnim ca;
	UINT an = tv.CreateAnimation (0);
	auto *rot = mod.Rot (0, mod.Grp ({ 0 }), 1, _V (1, 0, 0), _V (0, 0, 1), (float)2.0);
	tv.AddAnimationComponent (an, 0, 1, rot);
	tv.SetAnimation (an, 0.7);
	ca.OnAdd (tv.anim[0].comp[0]);
	std::vector<uint8_t> pr (1, 1);
	ca.Step (tv.anim, tv.nanim, pr.data (), 1);
	CollAffine F1; ca.GroupTransform (0, 0, F1);
	CollGeomSession::Rewind (ca, tv.anim, tv.nanim, pr.data (), 1);
	ca.Step (tv.anim, tv.nanim, pr.data (), 1);
	CollAffine F2; ca.GroupTransform (0, 0, F2);
	for (int e = 0; e < 9; e++) REQUIRE (std::fabs (F1.A.data[e] - F2.A.data[e]) < 1e-12);
	REQUIRE ((F1.t - F2.t).length () < 1e-12);
}

namespace {
std::string BoxesMsh (const std::vector<std::pair<std::string, float>> &g) // one unit box per group at x = offset, LABEL if named
{
	std::string s = "MSHX1\nGROUPS " + std::to_string (g.size ()) + "\n";
	float c[8][3] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} };
	char b[128];
	for (auto &q : g) {
		if (!q.first.empty ()) s += "LABEL " + q.first + "\n";
		s += "GEOM 8 12\n";
		for (auto &p : c) { snprintf (b, sizeof b, "%g %g %g 0 0 1 0 0\n", p[0] + q.second, p[1], p[2]); s += b; }
		s += "0 1 2\n0 2 3\n4 6 5\n4 7 6\n0 4 5\n0 5 1\n3 2 6\n3 6 7\n0 3 7\n0 7 4\n1 5 6\n1 6 2\n";
	}
	return s;
}
}

TEST_CASE ("fix1 M7: name selectors of a MESH sidecar pick the collision mesh's groups", "[CollSourceA]")
{
	CollFakeSdk s;
	s.File (".\\Meshes\\vis.msh", BoxesMsh ({ { "lid", 0 }, { "body", 4 } }));
	s.File (".\\Meshes\\hull.msh", BoxesMsh ({ { "body", 0 }, { "lid", 4 }, { "", 8 } }));
	s.File (".\\Meshes\\vis.col", "COLLIDER-V1\nMESH hull\nEXCLUDE LABEL lid\n");
	auto *v = s.AddVessel ("V1");
	v->slot.resize (1); v->slot[0].kind = CollFakeSdk::NAME; v->slot[0].name = "vis";
	CollGeomSession g (s, Cfg ());
	g.SimulationStart (0, false);
	g.BeginFrame (0, 0.1);
	const CollShape *sh = g.Geom (0)->shape;
	REQUIRE (sh);
	REQUIRE (sh->CollMesh (0));
	CHECK (sh->PartOf (0, 0) >= 0);
	CHECK (sh->PartOf (0, 1) == -1);
	CHECK (sh->PartOf (0, 2) >= 0);
	CHECK (s.LogCount ("name selectors ignored") == 0);
	CHECK (s.misuse == 0);
}

TEST_CASE ("fix1 M6: a static MESH sidecar part is predicted static while a visual group animates", "[CollSourceA]")
{
	CollFakeSdk s;
	s.File (".\\Meshes\\vis.msh", BoxesMsh ({ { "", 0 }, { "", 4 } }));
	s.File (".\\Meshes\\hull.msh", BoxesMsh ({ { "", 0 } }));
	s.File (".\\Meshes\\vis.col", "COLLIDER-V1\nMESH hull\n");
	auto *v = s.AddVessel ("V1");
	v->slot.resize (1); v->slot[0].kind = CollFakeSdk::NAME; v->slot[0].name = "vis"; v->slot[0].ofs = Vector (0, 0, 3);
	TestModule mod;
	UINT an = v->anim.CreateAnimation (0);
	v->anim.AddAnimationComponent (an, 0, 1, mod.Rot (0, mod.Grp ({ 0 }), 1, _V (0, 0, 0), _V (0, 1, 0), (float)1.0)); // the visual door is group 0
	CollGeomSession g (s, Cfg ());
	g.SimulationStart (0, false);
	g.BeginFrame (0, 0.1);
	double st = 0;
	bool anim = false;
	for (int f = 0; f < 4; f++) {
		st += 0.1; v->anim.SetAnimation (an, st);
		g.BeginFrame (0, 0.1);
		const CollVesselGeom *G = g.Geom (0);
		REQUIRE (G->shape);
		REQUIRE (G->shape->nPart () == 1);
		anim = anim || G->animating;
		const CollAffine &a = G->next[0], &b = G->shape->Part (0).pose[1];
		for (int e = 0; e < 9; e++) CHECK (a.A.data[e] == b.A.data[e]);
		CHECK ((a.t - b.t).length () == 0);
		CHECK (G->motionNext[0] == 0);
	}
	CHECK (anim);
	CHECK (s.misuse == 0);
}
