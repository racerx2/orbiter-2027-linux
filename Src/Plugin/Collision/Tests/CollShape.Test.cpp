// not upstream: unit tests for Src/Orbiter/CollShape (D1 9.3): sidecar, tags, parts, damage
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include "CollShape.h"
#include "CollTestMsh.h"
#include "CollAnimTest.h"

static std::string ReadFile (const std::string &path)
{
	std::ifstream f (path, std::ios::binary);
	std::ostringstream ss;
	ss << f.rdbuf ();
	return ss.str ();
}

static bool Parse (const std::string &text, CollSidecar &sc, std::vector<std::string> &warn)
{
	warn.clear ();
	return CollParseSidecar (text.data(), text.size(), "test.col", sc, warn);
}

static bool HasWarn (const std::vector<std::string> &warn, const char *part)
{
	for (const std::string &w : warn) if (w.find (part) != std::string::npos) return true;
	return false;
}

// sidecar parser (D1 4.2, 4.4)

TEST_CASE("Sidecar: syntax, CRLF, BOM, comments, case", "[collshape]")
{
	std::string t = "\xEF\xBB\xBF; stock sidecar\r\n\r\ncollider-v1\r\n"
		"EXCLUDE\tMATERIAL dgint* Visor ; cabin\r\n"
		"mat Glass material cockpitglass\r\n"
		"MAT gear GROUP 95-97 99-101\r\n"
		"include group 3\r\n"
		"SKIN 0.05\r\nWELD 0.002\r\n"
		"EXCLUDE ALL";  // EOF without newline
	CollSidecar sc;
	std::vector<std::string> w;
	REQUIRE (Parse (t, sc, w));
	CHECK (w.empty ());
	REQUIRE (sc.rule.size() == 5);
	CHECK (sc.rule[0].op == CollSideRule::EXCLUDE);
	CHECK (sc.rule[0].sel.kind == CollSelector::MATERIAL);
	CHECK (sc.rule[0].sel.pat == std::vector<std::string> { "dgint*", "visor" });
	CHECK (sc.rule[0].line == 4);
	CHECK (sc.rule[1].op == CollSideRule::MAT);
	CHECK (sc.rule[1].mat == CollInternMaterial ("glass"));
	CHECK (std::string (CollMaterialName (sc.rule[1].mat)) == "glass");
	CHECK (sc.rule[2].sel.kind == CollSelector::GROUP);
	CHECK (sc.rule[2].sel.range == std::vector<std::pair<uint32_t,uint32_t>> { { 95, 97 }, { 99, 101 } });
	CHECK (sc.rule[2].mat == CollInternMaterial ("GEAR"));
	CHECK (sc.rule[3].op == CollSideRule::INCLUDE);
	CHECK (sc.rule[4].sel.kind == CollSelector::ALL);
	CHECK (sc.skin == 0.05);
	CHECK (sc.weld == 0.002);
	CHECK (sc.needNames);
	CHECK (CollInternMaterial ("") == 0);
	CHECK (std::string (CollMaterialName (0)) == "");
	CHECK (std::string (CollMaterialName (60000)) == "");
}

TEST_CASE("Sidecar: errors warn and skip the line, never fatal", "[collshape]")
{
	CollSidecar sc;
	std::vector<std::string> w;
	CHECK_FALSE (Parse ("EXCLUDE ALL\n", sc, w));                 // missing header
	CHECK (HasWarn (w, "test.col:1:"));
	CHECK (sc.rule.empty ());
	CHECK_FALSE (Parse ("", sc, w));
	CHECK_FALSE (Parse ("; only a comment\n\n", sc, w));

	std::string t = "COLLIDER-V1\n"
		"SKIN 0.005\n"                // 2: below 0.01
		"SKIN abc\n"                  // 3: bad number
		"WELD 0.02\n"                 // 4: above 0.01
		"FROB 1\n"                    // 5: unknown keyword
		"EXCLUDE GROUP 27-0\n"        // 6: reversed, swapped with a warning
		"EXCLUDE GROUP 0-70000\n"     // 7: over 65,536 entries
		"EXCLUDE GROUP 0-65535\n"     // 8: exactly 65,536
		"EXCLUDE GROUP 1x\n"          // 9: bad index
		"EXCLUDE GROUP -3\n"          // 10: bad index
		"EXCLUDE LABEL a*b\n"         // 11: star inside
		"EXCLUDE ALL GROUP 1\n"       // 12: tokens after ALL
		"MAT glass\n"                 // 13: no selector
		"EXCLUDE\n"                   // 14: no selector
		"EXCLUDE WHAT 1\n"            // 15: unknown selector
		"FOLLOW 1 2\n"                // 16: FOLLOW without MESH (end of file warning)
		"SKIN 0.5\n"                  // 17: upper limit, valid
		"WELD 0\n";                   // 18: valid
	REQUIRE (Parse (t, sc, w));
	for (int line : { 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15 })
		CHECK (HasWarn (w, ("test.col:" + std::to_string (line) + ":").c_str()));
	CHECK (HasWarn (w, "FOLLOW without MESH"));
	REQUIRE (sc.rule.size() == 2);
	CHECK (sc.rule[0].sel.range[0] == std::make_pair (0u, 27u));
	CHECK (sc.rule[1].sel.range[0] == std::make_pair (0u, 65535u));
	CHECK (sc.skin == 0.5);
	CHECK (sc.weld == 0.0);
	CHECK (sc.follow.empty ());

	// MESH takes the rest of the line; FOLLOW maps collision groups to one visual group
	REQUIRE (Parse ("COLLIDER-V1\nMESH  My Folder\\hull_lo  \nFOLLOW 0-2 5 7\n", sc, w));
	CHECK (sc.mesh == "My Folder\\hull_lo");
	REQUIRE (sc.follow.size() == 1);
	CHECK (sc.follow[0].first == std::vector<uint32_t> { 0, 1, 2, 5 });
	CHECK (sc.follow[0].second == 7);

	// 10 kB line, and a file over 1 MB
	std::string big = "COLLIDER-V1\nEXCLUDE GROUP";
	for (int i = 0; i < 2000; i++) big += " " + std::to_string (i * 2);
	REQUIRE (Parse (big, sc, w));
	REQUIRE (sc.rule.size() == 1);
	CHECK (sc.rule[0].sel.range.size() == 2000);
	std::string huge (COLL_SIDECAR_MAX + 1, ' ');
	CHECK_FALSE (Parse ("COLLIDER-V1\n" + huge, sc, w));
	CHECK (HasWarn (w, "1 MB"));
}

TEST_CASE("Sidecar: random bytes never crash", "[collshape]")
{
	TestRng r (77);
	static const char *words[] = { "EXCLUDE", "INCLUDE", "MAT", "SKIN", "WELD", "MESH", "FOLLOW", "ALL", "GROUP", "LABEL",
		"MATERIAL", "TEXTURE", "0", "12-3", "-", "*", "a*", ";", "\r", "\t", "0.02", "1e400", "nan", "\xEF\xBB\xBF" };
	for (int k = 0; k < 2000; k++) {
		std::string t = "COLLIDER-V1\n";
		int n = (int)r.I (60);
		for (int i = 0; i < n; i++) {
			if (r.P (0.5)) t += words[r.I (sizeof (words) / sizeof (words[0]))];
			else t += (char)r.I (256);
			t += r.P (0.2) ? '\n' : ' ';
		}
		CollSidecar sc;
		std::vector<std::string> w;
		CollParseSidecar (t.data(), t.size(), nullptr, sc, w);
		CollMeshTags tags;
		tags.label.assign (8, "x"); tags.material.assign (8, "m"); tags.texture.assign (8, "t");
		CollResolveNames (sc, &tags, 8, w);
		CollRestMesh rm;
		rm.grp.resize (8);
		std::vector<uint8_t> on;
		std::vector<uint16_t> mat;
		CollGroupRules (&sc, rm, on, mat);
		REQUIRE (on.size() == 8);
	}
}

// tag scanner and name resolution (D1 4.3)

TEST_CASE("Tag scanner: DG ns and ShuttleA against R6", "[collshape]")
{
	std::string t = ReadFile (CollTestPath ("Src/Vessel/DeltaGlider/Meshes/deltaglider_ns.msh"));
	REQUIRE (!t.empty ());
	CollMeshTags tg;
	std::vector<std::string> w;
	REQUIRE (CollScanMeshTags (t.data(), t.size(), tg, w));
	REQUIRE (tg.label.size() == 122);
	// r6_groups.csv rows 0-27, 111-121
	static const char *mat[28] = { "dgint1_1", "dgint1_1_A", "dgint1_1_B", "dgint1_1_M", "dgint1_1_M", "dgint1_3", "dgint1_3", "dgint1_3",
		"dgint1_3", "dgint1_3", "dgint1_3", "dgint1_3", "dgint1_3", "dgint1_3", "dgint1_3_A", "dgint1_3_A", "dgint1_3_A", "dgint1_3_A",
		"dgint1_3_A", "dgint1_3_A", "dgint1_3_A", "dgint1_3_A", "dgint1_3_L", "dgint1_3_M", "dgint1_3_M", "dgint1_3_M", "dgint1_3_MU", "dgpilot1_1" };
	for (int g = 0; g < 28; g++) { INFO ("group " << g); CHECK (tg.material[g] == mat[g]); CHECK (tg.label[g] == ""); }
	static const char *lab2[11] = { "Psngr1", "Psngr2", "Psngr3", "Psngr4", "VisorPilot", "Visor1", "Visor2", "Visor3", "Visor4", "", "" };
	static const char *mat2[11] = { "pnsgr1", "psng4", "psngr2", "psngr3", "visor", "visor", "visor", "visor", "visor", "HUD_glass", "cockpitglass" };
	for (int g = 111; g < 122; g++) { INFO ("group " << g); CHECK (tg.label[g] == lab2[g-111]); CHECK (tg.material[g] == mat2[g-111]); }
	CHECK (tg.texture[0] == "DG\\DGMK4_3.dds");
	CHECK (tg.texture[27] == "DG\\DGPILOT1.dds");

	t = ReadFile (CollTestPath ("Src/Vessel/ShuttleA/Meshes/ShuttleA.msh"));
	REQUIRE (CollScanMeshTags (t.data(), t.size(), tg, w));
	static const char *sa[67] = { "Aft_Shelve", "Aft_Structure", "Airlock", "Airlock_Hatch", "Aux_Engine_Left", "Aux_Engine_Pod_Cap_Left",
		"Aux_Engine_Pod_Cap_Right", "Aux_Engine_Pod_Left", "Aux_Engine_Pod_Right", "Aux_Engine_Pod_Shield_Left", "Aux_Engine_Pod_Shield_Right",
		"Aux_Engine_Right", "Bottom_Structure", "Crew_Module_Bottom", "Crew_Module_Forward", "Crew_Module_Sides", "Crew_Module_Top", "Docking_Bay",
		"Docking_Bay_Cover_Lower", "Docking_Bay_Cover_Upper", "Docking_Hatch", "Docking_Ring", "External_Tank", "Gear_Actuator_AL", "Gear_Actuator_AR",
		"Gear_Actuator_FL", "Gear_Actuator_FR", "Gear_Actuator_ML", "Gear_Actuator_MR", "Gear_Hinge_Box", "Gear_Pad_AL", "Gear_Pad_AR", "Gear_Pad_FL",
		"Gear_Pad_FR", "Gear_Pad_ML", "Gear_Pad_MR", "Gear_Strut_AL", "Gear_Strut_AR", "Gear_Strut_FL", "Gear_Strut_FR", "Gear_Strut_ML",
		"Gear_Strut_MR", "Hover_Engine", "Internal_Tank", "Main_Engine_Gimbal_L", "Main_Engine_Gimbal_R", "Main_Engine_L", "Main_Engine_R",
		"Mid_Structure", "ORBGroup15", "ORBGroup23", "ORBGroup24", "ORBGroup25", "ORBGroup26", "ORBGroup40", "ORBGroup47", "Pilots", "Pod_Structure",
		"RCS", "RCS_Tank", "RCS_Tank_Support", "RCS_pod", "VC_Aft", "VC_Forward", "VC_Windows", "beams", "frames" };
	REQUIRE (tg.label.size() == 67);
	for (int g = 0; g < 67; g++) { INFO ("group " << g); CHECK (tg.label[g] == sa[g]); }
}

TEST_CASE("Tag scanner: inherit rule, empty groups, long lines", "[collshape]")
{
	std::string t =
		"MSHX1\r\nGROUPS 5\r\n"
		"LABEL first\r\nTEXTURE 2\r\nGEOM 3 1\r\n0 0 0\r\n1 0 0\r\n0 1 0\r\n0 1 2\r\n"          // g0: no MATERIAL -> default, texture b
		"MATERIAL 2\r\nLABEL empty\r\nGEOM 0 0\r\n"                                         // file group 1: no geometry, dropped
		"LABEL second\r\nGEOM 3 1\r\n0 0 0\r\n1 0 0\r\n0 1 0\r\n0 1 2\r\n"                // core g1: inherits default (not the dropped group's 2)
		"MATERIAL 1\r\nTEXTURE 0\r\nGEOM 3 1\r\n0 0 0\r\n1 0 0\r\n0 1 0\r\n0 1 2\r\n"     // core g2: m1, no texture
		"label  fourth  extra\r\nGEOM 3 1\r\n0 0 0\r\n1 0 0\r\n0 1 0\r\n0 1 2\r\n"        // core g3: inherits m1 and no texture
		"MATERIALS 2\r\nm1\r\nm2\r\n"
		"MATERIAL m1\r\n1 1 1 1\r\n1 1 1 1\r\n1 1 1 1\r\n0 0 0 1\r\n"
		"MATERIAL m2\r\n1 1 1 1\r\n1 1 1 1\r\n1 1 1 1\r\n0 0 0 1\r\n"
		"TEXTURES 2\r\nta.dds\r\ntb.dds D\r\n";
	CollMeshTags tg;
	std::vector<std::string> w;
	REQUIRE (CollScanMeshTags (t.data(), t.size(), tg, w));
	REQUIRE (tg.label.size() == 4);
	CHECK (tg.label == std::vector<std::string> { "first", "second", "", "fourth" });
	CHECK (tg.material == std::vector<std::string> { "default", "default", "m1", "m1" });
	CHECK (tg.texture == std::vector<std::string> { "tb.dds", "tb.dds", "default", "default" });

	// a line over 255 characters stops the core parser: the group count then differs
	std::string lng = "MSHX1\nGROUPS 2\nLABEL a\nGEOM 3 1\n0 0 0" + std::string (300, ' ') + "\n1 0 0\n0 1 0\n0 1 2\nGEOM 3 1\n0 0 0\n1 0 0\n0 1 0\n0 1 2\n";
	REQUIRE (CollScanMeshTags (lng.data(), lng.size(), tg, w));
	CHECK (tg.label.empty ());
	CHECK_FALSE (CollScanMeshTags ("MSH\n", 4, tg, w));

	// name resolution: patterns to GROUP ranges, '*' suffix, no case; count mismatch drops name rules
	CollSidecar sc;
	REQUIRE (Parse ("COLLIDER-V1\nEXCLUDE LABEL FIRST fou*\nMAT x MATERIAL M1\nEXCLUDE GROUP 9\nINCLUDE TEXTURE nothing\n", sc, w));
	CollSidecar sc2 = sc;
	w.clear ();
	REQUIRE (CollResolveNames (sc, &tg, 4, w) == false); // tg is empty now: count mismatch
	CHECK (sc.rule.size() == 1);
	CHECK (HasWarn (w, "name selectors ignored"));
	REQUIRE (CollScanMeshTags (t.data(), t.size(), tg, w));
	w.clear ();
	REQUIRE (CollResolveNames (sc2, &tg, 4, w));
	REQUIRE (sc2.rule.size() == 4);
	CHECK (sc2.rule[0].sel.kind == CollSelector::GROUP);
	CHECK (sc2.rule[0].sel.range == std::vector<std::pair<uint32_t,uint32_t>> { { 0, 0 }, { 3, 3 } });
	CHECK (sc2.rule[1].sel.range == std::vector<std::pair<uint32_t,uint32_t>> { { 2, 3 } });
	CHECK (HasWarn (w, "line 4: group 9 beyond"));
	CHECK (HasWarn (w, "line 5: no group matches"));
	CHECK_FALSE (sc2.needNames);
}

// stock sidecars (D1 4.6, texts as literals until the files exist in Phase B)

static const char *SC_DG = "COLLIDER-V1\nEXCLUDE MATERIAL dgint* dgpilot* pnsgr* psng* visor HUD_glass\nMAT glass MATERIAL cockpitglass\nMAT gear GROUP 95-97 99-101\n";
static const char *SC_SA = "COLLIDER-V1\nEXCLUDE LABEL Pilots VC_Aft VC_Forward\nMAT glass LABEL VC_Windows\nMAT gear LABEL Gear_*\n";
static const char *SC_ATL = "COLLIDER-V1\nEXCLUDE MATERIAL HUD\nMAT glass MATERIAL glass\nMAT gear LABEL nosewheel nosegear wheelR wheelL gearR gearL\n";
static const char *SC_ALL = "COLLIDER-V1\nEXCLUDE ALL\n";

static std::map<std::string, std::shared_ptr<const CollRestMesh>> g_rest;

static std::shared_ptr<const CollRestMesh> Rest (const char *rel)
{
	auto it = g_rest.find (rel);
	if (it != g_rest.end()) return it->second;
	auto m = std::make_shared<CollRestMesh> ();
	std::string err;
	bool ok = CollTestLoadMsh (CollTestPath (rel).c_str(), *m, &err);
	INFO (err);
	REQUIRE (ok);
	g_rest[rel] = m;
	return m;
}

// parse, scan and resolve like CollSource will
static std::shared_ptr<const CollSidecar> Side (const char *text, const char *rel, uint32_t ngrp)
{
	auto sc = std::make_shared<CollSidecar> ();
	std::vector<std::string> w;
	REQUIRE (CollParseSidecar (text, strlen (text), rel, *sc, w));
	CollMeshTags tg;
	if (sc->needNames) {
		std::string t = ReadFile (CollTestPath (rel));
		REQUIRE (CollScanMeshTags (t.data(), t.size(), tg, w));
	}
	REQUIRE (CollResolveNames (*sc, sc->needNames ? &tg : nullptr, ngrp, w));
	return sc;
}

static uint32_t Kept (const CollRestMesh &rm, const CollSidecar &sc, std::vector<uint16_t> &mat)
{
	std::vector<uint8_t> on;
	CollGroupRules (&sc, rm, on, mat);
	uint32_t n = 0;
	for (size_t g = 0; g < rm.grp.size(); g++) if (on[g]) n += (uint32_t)rm.grp[g].idx.size() / 3;
	return n;
}

TEST_CASE("Stock sidecars: included triangles before filters", "[collshape][stock]")
{
	uint16_t gear = CollInternMaterial ("gear"), glass = CollInternMaterial ("glass");
	std::vector<uint16_t> mat;
	struct Row { const char *rel; const char *text; uint32_t kept; } rows[] = {
		{ "Src/Vessel/DeltaGlider/Meshes/deltaglider_ns.msh", SC_DG, 4003 },
		{ "Src/Vessel/DeltaGlider/Meshes/deltaglider.msh", SC_DG, 4093 },
		{ "Src/Vessel/ShuttleA/Meshes/ShuttleA.msh", SC_SA, 7490 },
		{ "Src/Vessel/Atlantis/Atlantis/Meshes/Atlantis.msh", SC_ATL, 13907 },
		{ "Src/Vessel/Atlantis/Atlantis/Meshes/AtlantisCockpit.msh", SC_ALL, 0 },
		{ "Src/Vessel/ShuttleA/ShuttleA_PL/Meshes/ShuttleA_chpr.msh", SC_ALL, 0 },
		{ "Src/Vessel/ShuttleA/ShuttleA_PL/Meshes/ShuttleA_chmain.msh", SC_ALL, 0 },
	};
	for (const Row &r : rows) {
		INFO (r.rel);
		auto rm = Rest (r.rel);
		auto sc = Side (r.text, r.rel, (uint32_t)rm->grp.size());
		CHECK (Kept (*rm, *sc, mat) == r.kept);
		std::string rel = r.rel;
		if (rel.find ("deltaglider") != std::string::npos) {
			for (uint32_t g : { 95u, 96u, 97u, 99u, 100u, 101u }) CHECK (mat[g] == gear);
			CHECK (mat[98] == 0);
			CHECK (mat[121] == glass);
		}
		if (rel.find ("ShuttleA.msh") != std::string::npos) {
			for (uint32_t g = 23; g <= 41; g++) CHECK (mat[g] == gear);
			CHECK (mat[22] == 0); CHECK (mat[42] == 0);
			CHECK (mat[64] == glass);
		}
		if (rel.find ("Atlantis.msh") != std::string::npos) {
			for (uint32_t g = 19; g <= 24; g++) CHECK (mat[g] == gear);
			CHECK (mat[56] == glass);
		}
	}
}

TEST_CASE("Defaults: FLAG 0x02 groups are excluded, INCLUDE restores them", "[collshape]")
{
	CollRestMesh rm;
	rm.grp.resize (3);
	rm.grp[1].usrflag = 0x02;
	rm.grp[2].usrflag = 0x04;
	std::vector<uint8_t> on;
	std::vector<uint16_t> mat;
	CollGroupRules (nullptr, rm, on, mat);
	CHECK (on == std::vector<uint8_t> { 1, 0, 1 });
	CollSidecar sc;
	std::vector<std::string> w;
	REQUIRE (Parse ("COLLIDER-V1\nEXCLUDE ALL\nINCLUDE GROUP 1\nMAT steel GROUP 0-1\nINCLUDE GROUP 0\nEXCLUDE GROUP 0\n", sc, w));
	CollGroupRules (&sc, rm, on, mat);
	CHECK (on == std::vector<uint8_t> { 0, 1, 0 });  // last EXCLUDE/INCLUDE wins
	CHECK (mat[0] == CollInternMaterial ("steel"));
	CHECK (mat[1] == mat[0]);
	CHECK (mat[2] == 0);
}

// vessel colliders

// one Atlantis: meshes 0 cockpit (EXCLUDE ALL: no collider), 1 orbiter, 2 VC (not external)
struct AtlRig {
	TestVessel v; TestModule mod; AtlantisAnims a; CollAnim ca; CollShape sh;
	std::vector<CollMeshInfo> mi;
	explicit AtlRig (const std::shared_ptr<const CollSidecar> &side = nullptr)
	{
		v.coll = &ca;
		v.meshGrp = { 1, ATL_NGRP, 1 };
		DefineAtlantis (v, mod, a);
		mi.resize (3);
		for (uint32_t m = 0; m < 3; m++) { mi[m].present = true; mi[m].collide = false; mi[m].serial = 1; }
		mi[1].collide = true;
		mi[1].key = "F:atlantis\\atlantis";
		mi[1].rest = Rest ("Src/Vessel/Atlantis/Atlantis/Meshes/Atlantis.msh");
		mi[1].side = side;
	}
	uint32_t Update (CollTemplateCache &cache) { v.Step (); return sh.Update (mi.data(), (uint32_t)mi.size(), ca, v.anim, v.nanim, cache); }
};

static const std::vector<std::vector<uint32_t>> ATL_SETS = {
	{ ATL_cargodooroutR, ATL_cargodoorinR, ATL_radiatorBR }, { ATL_radiatorFR }, { ATL_cargodooroutL, ATL_cargodoorinL, ATL_radiatorBL }, { ATL_radiatorFL },
	{ ATL_nosedoorL }, { ATL_nosedoorR }, { ATL_nosewheel, ATL_nosegear }, { ATL_geardoorR }, { ATL_geardoorL }, { ATL_wheelR, ATL_gearR, ATL_wheelL, ATL_gearL },
	{ ATL_startrackers }, { ATL_KUband1 }, { ATL_KUband2 }, { ATL_flapR, ATL_aileronR }, { ATL_flapL, ATL_aileronL }, { ATL_rudderR }, { ATL_rudderL },
	{ ATL_Shoulder }, { ATL_Humerus }, { ATL_radii, ATL_RMScamera, ATL_RMScamera_pivot }, { ATL_wrist }, { ATL_endeffecter },
	{ ATL_SSMEL }, { ATL_SSMER }, { ATL_SSMET } };

TEST_CASE("Partition: Atlantis mesh 1 has the 26 parts of D1 3.3", "[collshape][stock]")
{
	CollTemplateCache cache;
	AtlRig r;
	uint32_t f = r.Update (cache);
	CHECK ((f & COLLSH_BUILT));
	CHECK_FALSE ((f & COLLSH_NONE));
	REQUIRE (r.sh.nPart () == 26);
	std::set<int> used;
	std::vector<uint8_t> inSet (ATL_NGRP, 0);
	for (const auto &s : ATL_SETS) {
		int p = r.sh.PartOf (1, s[0]);
		REQUIRE (p >= 0);
		for (uint32_t g : s) { CHECK (r.sh.PartOf (1, g) == p); inSet[g] = 1; }
		CHECK (used.insert (p).second);
	}
	int stat = r.sh.PartOf (1, 0);
	CHECK (used.count (stat) == 0);
	for (uint32_t g = 0; g < ATL_NGRP; g++) if (!inSet[g]) CHECK (r.sh.PartOf (1, g) == stat);
	CHECK (r.sh.PartOf (0, 0) == -1);   // cockpit mesh has no collider
	for (uint32_t i = 0; i < r.sh.nPart (); i++) {
		const CollPart &P = r.sh.Part (i);
		CHECK (P.mesh == 1);
		CHECK (r.sh.PartOf (1, P.rep) == (int)i);
		CHECK (P.motion == 0);
	}

	// HST: 8 parts (D1 3.3)
	TestVessel v; TestModule mod; CollAnim ca; CollShape sh;
	UINT ant, hatch, arr;
	v.coll = &ca;
	v.meshGrp = { 104 };
	DefineHST (v, mod, ant, hatch, arr);
	CollMeshInfo mi;
	mi.present = mi.collide = true; mi.serial = 1; mi.key = "F:hst"; mi.rest = Rest ("Src/Vessel/HST/Meshes/HST_STS-109.msh");
	REQUIRE (mi.rest->grp.size() == 104);
	v.Step ();
	sh.Update (&mi, 1, ca, v.anim, v.nanim, cache);
	CHECK (sh.nPart () == 8);
}

TEST_CASE("Partition after ClearAnimations: posed doors stay separate parts", "[collshape][stock]")
{
	CollTemplateCache cache;
	AtlRig r;
	r.Update (cache);
	for (int k = 1; k <= 10; k++) { r.v.SetAnimation (r.a.anim_door, k * 0.1); r.Update (cache); }
	int pr = r.sh.PartOf (1, ATL_cargodooroutR), pl = r.sh.PartOf (1, ATL_cargodooroutL), ps = r.sh.PartOf (1, 0);
	CollAffine before = r.sh.Part (pr).anim[1];
	r.v.ClearAnimations ();
	uint32_t f = r.Update (cache);
	CHECK ((f & COLLSH_PARTITION));
	pr = r.sh.PartOf (1, ATL_cargodooroutR); pl = r.sh.PartOf (1, ATL_cargodooroutL); ps = r.sh.PartOf (1, 0);
	CHECK (r.sh.nPart () == 3);
	CHECK (pr != ps); CHECK (pl != ps); CHECK (pr != pl);
	CHECK (r.sh.PartOf (1, ATL_cargodoorinR) == pr);
	CHECK (r.sh.PartOf (1, ATL_radiatorFR) == pr);
	CHECK (r.sh.PartOf (1, ATL_nosewheel) == ps);
	// new parts inherit anim[0] from the old part of their representative group: no motion
	const CollPart &P = r.sh.Part (pr);
	for (int k = 0; k < 9; k++) CHECK (P.anim[0].A.data[k] == before.A.data[k]);
	CHECK (P.motion == 0);
	CollAffine F;
	REQUIRE (r.sh.GroupPose (1, ATL_cargodooroutR, F));
	for (int k = 0; k < 9; k++) CHECK (F.A.data[k] == before.A.data[k]);
}

TEST_CASE("Bay with animation: inner doors at state 0, no hit once open", "[collshape][stock]")
{
	CollTemplateCache cache;
	AtlRig r;
	r.Update (cache);
	CollRayHit h;
	for (double z : { -6.0, 0.0, 8.0 }) {
		INFO ("z " << z);
		REQUIRE (r.sh.RayRest (1, 0, Vector (0, 1, z), Vector (0, 1, 0), 0, 20, h));
		CHECK (fabs (1 + h.t - 3.43) < 0.02);
	}
	for (int k = 1; k <= 60; k++) { r.v.SetAnimation (r.a.anim_door, k / 60.0); r.Update (cache); }
	for (double z : { -6.0, 0.0, 8.0 }) {
		INFO ("z " << z);
		CHECK_FALSE (r.sh.RayRest (1, 0, Vector (0, 1, z), Vector (0, 1, 0), 0, 20, h));
	}
}

TEST_CASE("Poses t0/t1, offsets, MarkJump, rebuild", "[collshape][stock]")
{
	CollTemplateCache cache;
	AtlRig r;
	r.Update (cache);
	int door = r.sh.PartOf (1, ATL_cargodooroutR), stat = r.sh.PartOf (1, 0);
	uint32_t vdoor = r.sh.Part (door).version;

	// door moving: anim[0] = previous anim[1]
	r.v.SetAnimation (r.a.anim_door, 0.1);
	r.Update (cache);
	CollAffine prev = r.sh.Part (door).anim[1];
	r.v.SetAnimation (r.a.anim_door, 0.2);
	r.mi[1].ofs = Vector (1, 2, 3);
	r.mi[0].ofs = Vector (1, 2, 3);
	r.mi[2].ofs = Vector (1, 2, 3);
	uint32_t f = r.Update (cache);
	CHECK ((f & COLLSH_MOVED));
	CHECK ((f & COLLSH_OFFSET));
	CHECK_FALSE ((f & (COLLSH_JUMP | COLLSH_REBUILT | COLLSH_PARTITION)));
	const CollPart &D = r.sh.Part (door);
	for (int k = 0; k < 9; k++) CHECK (D.anim[0].A.data[k] == prev.A.data[k]);
	CHECK (D.motion > 0.01);
	CHECK (D.version == vdoor);
	CHECK (r.sh.Part (stat).motion == 0);
	for (int k = 0; k < 2; k++) {
		CHECK ((D.pose[k].t - D.anim[k].t - Vector (1, 2, 3)).length () < 1e-12);  // both poses on the current offset
		for (int j = 0; j < 9; j++) CHECK (D.pose[k].A.data[j] == D.anim[k].A.data[j]);
	}

	// ShiftCG-like: every offset by -s; no part motion, no jump
	Vector s (0.5, -0.25, 2);
	for (auto &m : r.mi) m.ofs -= s;
	f = r.Update (cache);
	CHECK ((f & COLLSH_OFFSET));
	CHECK_FALSE ((f & COLLSH_JUMP));
	CHECK_FALSE ((f & COLLSH_MOVED));
	for (uint32_t m = 0; m < 3; m++) CHECK ((r.sh.OffsetChange (m) + s).length () < 1e-12);
	for (uint32_t i = 0; i < r.sh.nPart (); i++) CHECK (r.sh.Part (i).motion == 0);

	// ShiftMesh-like: one offset changes
	r.mi[1].ofs += Vector (0, 0, 1);
	f = r.Update (cache);
	CHECK ((f & COLLSH_OFFSET));
	CHECK ((r.sh.OffsetChange (1) - Vector (0, 0, 1)).length () < 1e-12);
	CHECK (r.sh.OffsetChange (0).length () == 0);
	CHECK (r.sh.OffsetChange (2).length () == 0);
	CHECK (r.sh.OffsetChange (7).length () == 0);
	for (uint32_t i = 0; i < r.sh.nPart (); i++) CHECK (r.sh.Part (i).motion == 0);
	f = r.Update (cache);
	CHECK_FALSE ((f & COLLSH_OFFSET));

	// time jump: no part motion that step
	r.v.SetAnimation (r.a.anim_door, 0.4);
	r.sh.MarkJump ();
	f = r.Update (cache);
	CHECK ((f & COLLSH_JUMP));
	CHECK (r.sh.Part (door).motion == 0);
	r.v.SetAnimation (r.a.anim_door, 0.5);
	CHECK ((r.Update (cache) & (COLLSH_JUMP | COLLSH_MOVED)) == COLLSH_MOVED);

	// INSMESH of the slot: entry rebuilt, template hit, new versions, anim[0] = anim[1]
	r.mi[1].serial++;
	r.ca.OnMeshInsert (1);
	r.v.SetAnimation (r.a.anim_door, 0.6);
	f = r.Update (cache);
	CHECK ((f & COLLSH_REBUILT));
	door = r.sh.PartOf (1, ATL_cargodooroutR);
	CHECK (r.sh.Part (door).version != vdoor);
	for (uint32_t i = 0; i < r.sh.nPart (); i++) CHECK (r.sh.Part (i).motion == 0);

	// bounds: every part sphere inside the vessel bound; centre at the vessel origin
	Vector c; double rb;
	r.sh.Bound (1, c, rb);
	CHECK (c.length () == 0);
	for (uint32_t i = 0; i < r.sh.nPart (); i++) CHECK (r.sh.Part (i).sc[1].length () + r.sh.Part (i).sr[1] <= rb);
	CHECK (rb > 15);
}

TEST_CASE("Templates: shared between vessels, copy on write, freed with the last vessel", "[collshape][stock]")
{
	CollTemplateCache cache;
	const CollGeom *shared;
	{
		AtlRig a, b;
		a.Update (cache);
		b.Update (cache);
		REQUIRE (a.sh.nPart () == b.sh.nPart ());
		for (uint32_t i = 0; i < a.sh.nPart (); i++) CHECK (a.sh.Part (i).tpl.get() == b.sh.Part (i).tpl.get());
		CHECK (cache.tpl.size() == 1);
		int p = a.sh.PartOf (1, ATL_cargodooroutR);
		shared = b.sh.Part (p).tpl.get();
		uint32_t g = ATL_cargodooroutR;
		auto field = [] (const void *, const Vector &x) { return Vector (0, x.z > 0 ? 0.05 : 0.0, 0); };
		size_t n = a.sh.ApplyDent (1, &g, 1, field, nullptr);
		CHECK (n > 0);
		CHECK (a.sh.Part (p).own != nullptr);
		CHECK (b.sh.Part (p).own == nullptr);
		CHECK (&b.sh.Part (p).Geom () == shared);
		for (uint32_t v = 0; v < shared->vtx.size(); v++) CHECK (shared->Pos (v).y == shared->RestPos (v).y);
		CHECK (a.sh.Part (p).Geom ().tri.size() == shared->tri.size());
	}
	REQUIRE (cache.tpl.size() == 1);
	CHECK (cache.tpl.begin()->second.expired ());
}

// RayRest vs brute force over every posed part (Moller-Trumbore in the frame of the group's part)
static bool BruteRay (const CollShape &sh, const CollAffine &P, const Vector &o, const Vector &d, double tmax, double &tbest)
{
	CollAffine Pi = CollInverse (P);
	bool any = false;
	tbest = tmax;
	for (uint32_t i = 0; i < sh.nPart (); i++) {
		const CollGeom &G = sh.Part (i).Geom ();
		CollAffine X = CollCompose (Pi, sh.Part (i).pose[1]);
		for (const CollTri &t : G.tri) {
			Vector a = CollApply (X, G.Pos (t.v[0])), b = CollApply (X, G.Pos (t.v[1])), c = CollApply (X, G.Pos (t.v[2]));
			Vector e1 = b - a, e2 = c - a, p = crossp (d, e2);
			double det = dotp (e1, p);
			if (fabs (det) < 1e-15) continue;
			Vector s = o - a;
			double u = dotp (s, p) / det;
			Vector q = crossp (s, e1);
			double w = dotp (d, q) / det, tt = dotp (e2, q) / det;
			if (u < 0 || w < 0 || u + w > 1 || tt < 0 || tt > tbest) continue;
			tbest = tt; any = true;
		}
	}
	return any;
}

TEST_CASE("Damage API: ApplyDent, RayRest, RenderFeature, ResetDents, Material, PartRadius", "[collshape][stock]")
{
	CollTemplateCache cache;
	AtlRig r (Side (SC_ATL, "Src/Vessel/Atlantis/Atlantis/Meshes/Atlantis.msh", ATL_NGRP));
	for (int k = 0; k <= 5; k++) { r.v.SetAnimation (r.a.anim_door, k * 0.05); r.v.SetAnimation (r.a.anim_gear, k * 0.2); r.Update (cache); }
	const CollRestMesh &rm = *r.mi[1].rest;

	// RenderFeature: every collider triangle maps back to its render triangle (within weld tolerance)
	for (uint32_t i = 0; i < r.sh.nPart (); i++) {
		const CollGeom &G = r.sh.Part (i).Geom ();
		double worst = 0;
		for (uint32_t t = 0; t < G.tri.size(); t++) {
			uint32_t m, g, ot;
			REQUIRE (r.sh.RenderFeature (i, t, m, g, ot));
			REQUIRE (m == 1);
			REQUIRE (r.sh.PartOf (m, g) == (int)i);
			for (int k = 0; k < 3; k++) {
				const CollVtx &v = rm.grp[g].vtx[rm.grp[g].idx[ot*3+k]];
				worst = std::max (worst, (G.RestPos (G.tri[t].v[k]) - Vector (v.x, v.y, v.z)).length ());
			}
		}
		CHECK (worst <= COLL_WELD_DEFAULT * 1.0001);
	}
	uint32_t m, g, ot;
	CHECK_FALSE (r.sh.RenderFeature (r.sh.nPart (), 0, m, g, ot));

	// Material and PartRadius from the sidecar
	CHECK (r.sh.Material (1, ATL_nosewheel) == CollInternMaterial ("gear"));
	CHECK (r.sh.Material (1, 56) == CollInternMaterial ("glass"));
	CHECK (r.sh.Material (1, 0) == 0);
	CHECK (r.sh.PartOf (1, 54) == -1);         // HUD excluded
	CHECK (r.sh.PartRadius (1, 54) == -1);
	CHECK (r.sh.PartRadius (1, ATL_nosewheel) > 0.3);

	// GroupPose: the client's matrix of the group at the last Update, also for excluded groups
	CollAffine F, Fa;
	REQUIRE (r.sh.GroupPose (1, ATL_geardoorR, F));
	r.ca.GroupTransform (1, ATL_geardoorR, Fa);
	for (int k = 0; k < 9; k++) CHECK (F.A.data[k] == Fa.A.data[k]);
	CHECK (r.sh.GroupPose (1, 54, F));
	CHECK_FALSE (r.sh.GroupPose (0, 0, F));

	// RayRest equals brute force over posed parts, from a moving and a static part's frame
	TestRng rng (5);
	for (int k = 0; k < 40; k++) {
		uint32_t grp = (k % 2) ? (uint32_t)ATL_geardoorR : 0u;
		Vector o (rng.U (-3, 3), rng.U (-2, 3), rng.U (-12, 15)), d (rng.N (), rng.N (), rng.N ());
		d /= d.length ();
		CollRayHit h;
		double tb;
		bool hit = r.sh.RayRest (1, grp, o, d, 0, 30, h);
		bool bh = BruteRay (r.sh, r.sh.Part (r.sh.PartOf (1, grp)).pose[1], o, d, 30, tb);
		INFO ("ray " << k);
		REQUIRE (hit == bh);
		if (hit) CHECK (fabs (h.t - tb) < 1e-9);
	}

	// ApplyDent: field on rest positions, only listed groups move, triangle ids unchanged
	int p = r.sh.PartOf (1, ATL_cargodooroutR);
	std::vector<CollTri> tri0 = r.sh.Part (p).Geom ().tri;
	std::vector<Vector> rest0 = r.sh.Part (p).Geom ().vtx;
	auto field = [] (const void *ctx, const Vector &x) { double s = *(const double*)ctx; return Vector (0, x.z > 2 ? s : 0.0, 0); };
	double amp = 0.03;
	uint32_t lg = ATL_cargodooroutR;
	size_t n = r.sh.ApplyDent (1, &lg, 1, field, &amp);
	REQUIRE (n > 0);
	const CollGeom &W = r.sh.Part (p).Geom ();
	REQUIRE (W.tri.size() == tri0.size());
	for (size_t t = 0; t < tri0.size(); t++) CHECK (memcmp (&W.tri[t], &tri0[t], sizeof (CollTri)) == 0);
	size_t moved = 0;
	for (uint32_t v = 0; v < W.vtx.size(); v++) {
		bool listed = false;
		for (uint32_t q = W.refOfs[v]; q < W.refOfs[v+1]; q++) listed = listed || W.srcTab[W.ref[q].src].grp == ATL_cargodooroutR;
		CHECK (W.RestPos (v).y == rest0[v].y);
		Vector want = rest0[v] + (listed ? field (&amp, rest0[v]) : Vector ());
		CHECK ((W.Pos (v) - want).length () == 0);
		if ((W.Pos (v) - rest0[v]).length () > 0) moved++;
	}
	CHECK (moved == n);
	// a second record adds to the first
	r.sh.ApplyDent (1, &lg, 1, field, &amp);
	for (uint32_t v = 0; v < W.vtx.size(); v++) if (W.Pos (v).y != W.RestPos (v).y) { CHECK (fabs (W.Pos (v).y - W.RestPos (v).y - 2 * amp) < 1e-12); break; }
	CHECK (r.sh.ApplyDent (1, &lg, 1, [] (const void *, const Vector &) { return Vector (); }, nullptr) == 0);

	// ResetDents restores the template
	r.sh.ResetDents (1);
	CHECK (r.sh.Part (p).own == nullptr);
	for (uint32_t v = 0; v < rest0.size(); v++) CHECK ((r.sh.Part (p).Geom ().Pos (v) - rest0[v]).length () == 0);
}

// axis-aligned box as a mesh group with per-face vertices (24 vertices, 12 triangles)
static CollGroupData Box (const Vector &c, const Vector &h)
{
	CollGroupData g;
	static const int f[6][4][3] = {
		{ {-1,-1,-1}, {-1,1,-1}, {1,1,-1}, {1,-1,-1} }, { {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} },
		{ {-1,-1,-1}, {1,-1,-1}, {1,-1,1}, {-1,-1,1} }, { {-1,1,-1}, {-1,1,1}, {1,1,1}, {1,1,-1} },
		{ {-1,-1,-1}, {-1,-1,1}, {-1,1,1}, {-1,1,-1} }, { {1,-1,-1}, {1,1,-1}, {1,1,1}, {1,-1,1} } };
	for (int s = 0; s < 6; s++) {
		uint16_t b = (uint16_t)g.vtx.size();
		for (int k = 0; k < 4; k++)
			g.vtx.push_back (CollVtx { (float)(c.x + h.x*f[s][k][0]), (float)(c.y + h.y*f[s][k][1]), (float)(c.z + h.z*f[s][k][2]), 0, 0, 0, 0, 0 });
		for (uint16_t i : { 0, 1, 2, 0, 2, 3 }) g.idx.push_back ((uint16_t)(b + i));
	}
	return g;
}

TEST_CASE("Synthetic vessel: MESH replacement with FOLLOW, hidden groups, NONE", "[collshape]")
{
	// visual mesh: group 0 static, group 1 animated; collision mesh: 3 boxes, groups 1-2 follow group 1
	auto vis = std::make_shared<CollRestMesh> ();
	vis->grp.push_back (Box (Vector (0, 0, 0), Vector (1, 1, 1)));
	vis->grp.push_back (Box (Vector (4, 0, 0), Vector (1, 1, 1)));
	auto col = std::make_shared<CollRestMesh> ();
	col->name = "hull_lo";
	col->grp.push_back (Box (Vector (0, 0, 0), Vector (1.1, 1.1, 1.1)));
	col->grp.push_back (Box (Vector (4, 0, 0), Vector (1.1, 1.1, 1.1)));
	col->grp.push_back (Box (Vector (4, 3, 0), Vector (0.5, 0.5, 0.5)));
	auto side = std::make_shared<CollSidecar> ();
	std::vector<std::string> w;
	std::string txt = "COLLIDER-V1\nMESH hull_lo\nFOLLOW 1-2 1\nMAT steel GROUP 2\n";
	REQUIRE (CollParseSidecar (txt.data(), txt.size(), "x.col", *side, w));

	TestVessel v; TestModule mod; CollAnim ca; CollShape sh; CollTemplateCache cache;
	v.coll = &ca;
	v.meshGrp = { 2 };
	UINT an = v.CreateAnimation (0);
	v.AddAnimationComponent (an, 0, 1, mod.Rot (0, mod.Grp ({1}), 1, _V(3,0,0), _V(0,0,1), (float)(PI/2)));
	CollMeshInfo mi;
	mi.present = mi.collide = true; mi.serial = 1; mi.key = "F:box"; mi.rest = vis; mi.coll = col; mi.side = side;
	auto upd = [&] () { v.Step (); return sh.Update (&mi, 1, ca, v.anim, v.nanim, cache); };
	upd ();
	REQUIRE (sh.nPart () == 2);
	int ps = sh.PartOf (0, 0), pf = sh.PartOf (0, 1);
	CHECK (ps >= 0); CHECK (pf >= 0); CHECK (ps != pf);
	CHECK (sh.PartOf (0, 2) == pf);
	uint32_t m, g, ot;
	REQUIRE (sh.RenderFeature (pf, 0, m, g, ot));
	CHECK (g == 1); CHECK (ot == ~0u);
	REQUIRE (sh.RenderFeature (ps, 0, m, g, ot));
	CHECK (g == ~0u);
	CHECK (sh.Material (0, 2) == CollInternMaterial ("steel"));

	v.SetAnimation (an, 1.0);
	upd ();
	CollAffine F;
	ca.GroupTransform (0, 1, F);
	for (int k = 0; k < 9; k++) CHECK (sh.Part (pf).anim[1].A.data[k] == F.A.data[k]);
	CHECK (sh.Part (ps).motion == 0);
	CHECK (sh.Part (pf).motion > 1);

	// hidden groups: mask by srcTab index of the part geometry
	CHECK (sh.GroupMask (pf) == nullptr);
	sh.SetGroupHidden (0, 2, true);
	const uint8_t *mk = sh.GroupMask (pf);
	REQUIRE (mk != nullptr);
	const CollGeom &G = sh.Part (pf).Geom ();
	for (size_t k = 0; k < G.srcTab.size(); k++) CHECK ((mk[k] != 0) == (G.srcTab[k].grp == 2));
	CHECK (sh.GroupMask (ps) == nullptr);
	sh.SetGroupHidden (0, 2, false);
	CHECK (sh.GroupMask (pf) == nullptr);

	// no colliding mesh: NONE
	mi.collide = false;
	uint32_t f = upd ();
	CHECK ((f & COLLSH_NONE));
	CHECK ((f & COLLSH_REBUILT));
	CHECK (sh.nPart () == 0);
	CHECK (sh.PartOf (0, 0) == -1);
}

TEST_CASE("Synthetic vessel: a mesh inserted later joins the propagation", "[collshape]")
{
	// a mesh 1 component parents one on mesh 0: while mesh 1 is missing, its transform skips mesh 0
	auto box = std::make_shared<CollRestMesh> ();
	box->grp.push_back (Box (Vector (0, 0, 0), Vector (1, 1, 1)));
	box->grp.push_back (Box (Vector (3, 0, 0), Vector (1, 1, 1)));
	TestVessel v; TestModule mod; CollAnim ca; CollShape sh; CollTemplateCache cache;
	v.coll = &ca;
	v.meshGrp = { 2, 0 };
	UINT a0 = v.CreateAnimation (0), a1 = v.CreateAnimation (0);
	ANIMATIONCOMP *p = v.AddAnimationComponent (a0, 0, 1, mod.Lin (1, mod.Grp ({0}), 1, _V(0,0,1)));
	v.AddAnimationComponent (a1, 0, 1, mod.Lin (0, mod.Grp ({1}), 1, _V(1,0,0)), p);
	CollMeshInfo mi[2];
	mi[0].present = mi[0].collide = true; mi[0].serial = 1; mi[0].rest = box;
	mi[1].present = false; mi[1].collide = false; mi[1].serial = 0;
	auto upd = [&] () { v.Step (); return sh.Update (mi, 2, ca, v.anim, v.nanim, cache); };
	upd ();
	CHECK (sh.nPart () == 2);   // group 1 has a component of its own
	v.meshGrp[1] = 1; mi[1].present = true; mi[1].serial = 1; ca.OnMeshInsert (1);
	uint32_t f = upd ();
	CHECK_FALSE ((f & COLLSH_PARTITION));  // still two parts: the signature changed, the grouping did not
	v.SetAnimation (a0, 1.0);
	upd ();
	CollAffine F;
	REQUIRE (sh.GroupPose (0, 1, F));
	CHECK (F.t.z == 1.0);     // the parent's translation now reaches mesh 0
	REQUIRE (sh.GroupPose (0, 0, F));
	CHECK (F.t.length () == 0);
}

// building collider (D1 5.3-5.5)

static void BaseBrute (const std::vector<std::vector<CollGroupData>> &objs, const Vector &rpos, const Matrix &rrot, double R, double &rmax, double &htop)
{
	rmax = 0; htop = -1e300;
	for (const auto &o : objs)
		for (const CollGroupData &g : o) {
			if (g.undersh) continue;
			for (uint16_t i : g.idx) {
				Vector p (g.vtx[i].x, g.vtx[i].y, g.vtx[i].z);
				rmax = std::max (rmax, p.length ());
				htop = std::max (htop, (rpos + mul (rrot, p)).length () - R);
			}
		}
}

TEST_CASE("Base collider: SetObject, Refit on dents, rebuild on topology, rmax and htop", "[collshape]")
{
	// BLOCK-like object: walls and roof; HANGAR-like: two boxes, an undershadow group, an empty group
	std::vector<std::vector<CollGroupData>> objs (2);
	objs[0].push_back (Box (Vector (100, 5, -40), Vector (10, 5, 20)));
	objs[0].push_back (Box (Vector (100, 10.5, -40), Vector (10, 0.5, 20)));
	objs[1].push_back (Box (Vector (-300, 8, 900), Vector (15, 8, 30)));
	CollGroupData shadow = Box (Vector (-300, 0, 900), Vector (20, 0.01, 40));
	shadow.undersh = 1;
	objs[1].push_back (shadow);
	objs[1].push_back (CollGroupData ());
	objs[1].push_back (Box (Vector (-280, 18, 900), Vector (2, 2, 2)));
	uint16_t conc = CollInternMaterial ("concrete");

	CollBaseShape bs;
	REQUIRE (bs.SetObject (0, 17, objs[0].data(), objs[0].size(), conc, true));
	REQUIRE (bs.SetObject (1, 42, objs[1].data(), objs[1].size(), conc, true));
	Matrix rot;
	rot.Set (Vector (0.3, -1.1, 0.7));
	Vector rpos = mul (rot, Vector (0, 6371000.0 + 25, 0));
	bs.Finish (rpos, rot, 6371000.0);
	CHECK (bs.version == 1);
	REQUIRE (bs.nObj () == 2);
	CHECK (bs.Obj (0).obj == 17);
	CHECK (bs.Obj (1).obj == 42);
	CHECK (bs.Material (1) == conc);
	for (const CollSrc &s : bs.Obj (1).geom.srcTab) { CHECK (s.grp != 1); CHECK (s.grp != 2); CHECK (s.owner == 42); }
	double rmax, htop;
	BaseBrute (objs, rpos, rot, 6371000.0, rmax, htop);
	CHECK (fabs (bs.rmax - rmax) < 1e-9);
	CHECK (fabs (bs.htop - htop) < 1e-9);

	// RenderFeature round trip to (obj, grp, otri)
	for (uint32_t i = 0; i < 2; i++) {
		const CollGeom &G = bs.Obj (i).geom;
		for (uint32_t t = 0; t < G.tri.size(); t++) {
			uint32_t o, g, ot;
			REQUIRE (bs.RenderFeature (i, t, o, g, ot));
			CHECK (o == bs.Obj (i).obj);
			for (int k = 0; k < 3; k++) {
				const CollVtx &v = objs[i][g].vtx[objs[i][g].idx[ot*3+k]];
				CHECK ((G.Pos (G.tri[t].v[k]) - Vector (v.x, v.y, v.z)).length () <= COLL_WELD_DEFAULT);
			}
		}
	}

	// dent: vertices move, topology kept: Refit, same ids and part version, positions equal the input
	uint32_t ver0 = bs.Obj (0).geom.version;
	std::vector<CollTri> tri0 = bs.Obj (0).geom.tri;
	for (CollGroupData &g : objs[0]) for (CollVtx &v : g.vtx) if (v.x > 105) { v.y -= 0.25f; v.x += 0.125f; }
	REQUIRE (bs.SetObject (0, 17, objs[0].data(), objs[0].size(), conc, false));
	bs.Finish (rpos, rot, 6371000.0);
	CHECK (bs.version == 1);
	const CollGeom &G0 = bs.Obj (0).geom;
	CHECK (G0.version == ver0);
	REQUIRE (G0.tri.size() == tri0.size());
	for (size_t t = 0; t < tri0.size(); t++) CHECK (memcmp (&G0.tri[t], &tri0[t], sizeof (CollTri)) == 0);
	for (uint32_t v = 0; v < G0.vtx.size(); v++) {
		const CollRef &r = G0.ref[G0.refOfs[v]];
		const CollVtx &c = objs[0][G0.srcTab[r.src].grp].vtx[r.vtx];
		CHECK ((G0.Pos (v) - Vector (c.x, c.y, c.z)).length () == 0);
	}
	for (const CollNode &nd : G0.node)
		if (nd.count)
			for (uint32_t q = nd.first; q < nd.first + nd.count; q++)
				for (int k = 0; k < 3; k++) {
					const Vector &x = G0.Pos (G0.tri[G0.perm[q]].v[k]);
					for (int j = 0; j < 3; j++) { CHECK (x.data[j] >= nd.mn[j]); CHECK (x.data[j] <= nd.mx[j]); }
				}
	BaseBrute (objs, rpos, rot, 6371000.0, rmax, htop);
	CHECK (fabs (bs.rmax - rmax) < 1e-9);
	CHECK (fabs (bs.htop - htop) < 1e-9);

	// topology change: rebuild, part version and base version bump
	objs[0].push_back (Box (Vector (100, 13, -40), Vector (1, 1, 1)));
	REQUIRE (bs.SetObject (0, 17, objs[0].data(), objs[0].size(), conc, true));
	bs.Finish (rpos, rot, 6371000.0);
	CHECK (bs.Obj (0).geom.version != ver0);
	CHECK (bs.version == 2);
	CHECK (bs.Obj (0).geom.tri.size() == tri0.size() + 12);

	// an object without collider triangles
	CollGroupData only = shadow;
	CHECK_FALSE (bs.SetObject (2, 50, &only, 1, conc, true));
	CHECK (bs.nObj () == 3);
	CHECK (bs.Obj (2).geom.tri.empty ());
}

// brute force over long random paths (slow)

TEST_CASE("Atlantis random paths: part invariants, motion bound and rays against brute force", "[collshape][stock][.slow]")
{
	CollTemplateCache cache;
	AtlRig r (Side (SC_ATL, "Src/Vessel/Atlantis/Atlantis/Meshes/Atlantis.msh", ATL_NGRP));
	TestRng rng (99);
	r.Update (cache);
	std::vector<CollAffine> last (r.sh.nPart ());
	for (uint32_t i = 0; i < r.sh.nPart (); i++) last[i] = r.sh.Part (i).anim[1];
	double worstRay = 0;
	int rays = 0;
	for (int f = 0; f < 600; f++) {
		for (UINT i = 0; i < r.v.nanim; i++)
			if (rng.P (0.3)) r.v.anim[i].state = std::min (1.0, std::max (0.0, r.v.anim[i].state + rng.U (-0.08, 0.08)));
		if (rng.P (0.05)) for (auto &m : r.mi) m.ofs += Vector (rng.U (-0.1, 0.1), 0, 0);
		uint32_t fl = r.Update (cache);
		REQUIRE_FALSE ((fl & (COLLSH_REBUILT | COLLSH_PARTITION)));
		Vector c; double rb;
		r.sh.Bound (1, c, rb);
		for (uint32_t i = 0; i < r.sh.nPart (); i++) {
			const CollPart &P = r.sh.Part (i);
			for (int k = 0; k < 9; k++) REQUIRE (P.anim[0].A.data[k] == last[i].A.data[k]);
			last[i] = P.anim[1];
			REQUIRE (P.sc[1].length () + P.sr[1] <= rb);
			// every group of the part has the part's matrix (bitwise, 3.3)
			for (uint32_t g = 0; g < ATL_NGRP; g++) {
				if (r.sh.PartOf (1, g) != (int)i) continue;
				CollAffine F;
				r.ca.GroupTransform (1, g, F);
				for (int k = 0; k < 9; k++) REQUIRE (F.A.data[k] == P.anim[1].A.data[k]);
				for (int k = 0; k < 3; k++) REQUIRE (F.t.data[k] == P.anim[1].t.data[k]);
			}
			// motion bound: no vertex moves farther over the step path
			const CollGeom &G = P.Geom ();
			if (P.motion > 0)
				for (uint32_t v = 0; v < G.vtx.size(); v += 7)
					for (double tau : { 0.25, 0.5, 1.0 })
						REQUIRE ((CollApply (CollPoseAt (P.pose[0], P.pose[1], G.bsCentre, tau), G.Pos (v)) - CollApply (P.pose[0], G.Pos (v))).length () <= P.motion + 1e-9);
		}
		if (f % 20 == 0) {
			for (int k = 0; k < 10; k++) {
				uint32_t grp = (k % 2) ? (uint32_t)ATL_wrist : 0u;
				Vector o (rng.U (-4, 4), rng.U (-3, 4), rng.U (-14, 16)), d (rng.N (), rng.N (), rng.N ());
				d /= d.length ();
				CollRayHit h;
				double tb;
				bool hit = r.sh.RayRest (1, grp, o, d, 0, 40, h);
				bool bh = BruteRay (r.sh, r.sh.Part (r.sh.PartOf (1, grp)).pose[1], o, d, 40, tb);
				REQUIRE (hit == bh);
				if (hit) worstRay = std::max (worstRay, fabs (h.t - tb));
				rays++;
			}
		}
	}
	std::printf ("CollShape Atlantis 600 frames: %d rays, RayRest vs brute force %.2e m\n", rays, worstRay);
	CHECK (worstRay < 1e-9);
}

// code review C-A-a fixes (v2.2)

static std::vector<std::string> *g_logSink;
static void LogSink (int, const char *msg) { if (g_logSink) g_logSink->push_back (msg); }
struct LogCapture {
	std::vector<std::string> line;
	LogCapture () { g_logSink = &line; g_collLog = LogSink; }
	~LogCapture () { g_collLog = nullptr; g_logSink = nullptr; }
	int Count (const char *part) const { int n = 0; for (const std::string &s : line) if (s.find (part) != std::string::npos) n++; return n; }
};

// collider positions of one group's render vertices (render vertex -> welded position)
static std::map<uint32_t, Vector> GroupPositions (const CollShape &sh, uint32_t mesh, uint32_t grp)
{
	std::map<uint32_t, Vector> m;
	int p = sh.PartOf (mesh, grp);
	if (p < 0) return m;
	const CollGeom &G = sh.Part (p).Geom ();
	for (uint32_t v = 0; v < G.vtx.size(); v++)
		for (uint32_t q = G.refOfs[v]; q < G.refOfs[v+1]; q++)
			if (G.srcTab[G.ref[q].src].grp == grp) m[G.ref[q].vtx] = G.Pos (v);
	return m;
}

static bool SamePositions (const std::map<uint32_t, Vector> &a, const std::map<uint32_t, Vector> &b)
{
	if (a.size() != b.size()) return false;
	for (auto ia = a.begin(), ib = b.begin(); ia != a.end(); ++ia, ++ib)
		if (ia->first != ib->first || ia->second.x != ib->second.x || ia->second.y != ib->second.y || ia->second.z != ib->second.z) return false;
	return true;
}

TEST_CASE("Sidecar: FOLLOW groups are capped per file (code review C-A-a 1)", "[collshape]")
{
	// a 2 kB line used to expand to 16 million entries
	std::string t = "COLLIDER-V1\nMESH x\nFOLLOW";
	while (t.size() < 2000) t += " 0-65535";
	t += " 1\n";
	CollSidecar sc;
	std::vector<std::string> w;
	REQUIRE (Parse (t, sc, w));
	CHECK (HasWarn (w, "test.col:3: FOLLOW lists more than 65536 groups"));
	size_t n = 0;
	for (auto &f : sc.follow) n += f.first.size();
	CHECK (n == 0);
	// the first line fits, the second would pass the cap, the third fits again
	REQUIRE (Parse ("COLLIDER-V1\nMESH x\nFOLLOW 0-39999 1\nFOLLOW 40000-79999 2\nFOLLOW 7 3\n", sc, w));
	REQUIRE (sc.follow.size() == 2);
	CHECK (sc.follow[0].first.size() == 40000);
	CHECK (sc.follow[1].first == std::vector<uint32_t> { 7 });
	CHECK (HasWarn (w, "test.col:4:"));
	// exactly the cap
	REQUIRE (Parse ("COLLIDER-V1\nMESH x\nFOLLOW 0-65535 1\n", sc, w));
	REQUIRE (sc.follow.size() == 1);
	CHECK (sc.follow[0].first.size() == 65536);
	CHECK (w.empty ());
}

TEST_CASE("Partition change in a moving frame: inherited anim[0], new version, same motion as without the change (D1 3.3, code review C-A-a 5)", "[collshape][stock]")
{
	CollTemplateCache cache;
	AtlRig r, twin;
	REQUIRE (r.mi[1].rest->grp[45].idx.size() >= 3);
	for (int k = 0; k <= 5; k++) {
		r.v.SetAnimation (r.a.anim_door, k * 0.1); twin.v.SetAnimation (twin.a.anim_door, k * 0.1);
		r.Update (cache); twin.Update (cache);
	}
	REQUIRE (r.sh.PartOf (1, 45) == r.sh.PartOf (1, 0));
	ANIMATIONCOMP *extra = nullptr;
	UINT an = 0;
	for (int step = 0; step < 2; step++) {
		INFO ((step ? "component deleted" : "component added"));
		int door = r.sh.PartOf (1, ATL_cargodooroutL);
		CollAffine old1 = r.sh.Part (door).anim[1];
		uint32_t ver = r.sh.Part (door).version;
		// one frame: left door (0.5368-1) moves on while the module adds, then deletes, a static component
		double s = 0.6 + 0.1 * step;
		r.v.SetAnimation (r.a.anim_door, s); twin.v.SetAnimation (twin.a.anim_door, s);
		if (!step) {
			an = r.v.CreateAnimation (0);
			extra = r.v.AddAnimationComponent (an, 0, 1, r.mod.Rot (1, r.mod.Grp ({45}), 1, _V(0,0,0), _V(1,0,0), 0.1f));
		} else REQUIRE (r.v.DelAnimationComponent (an, extra));
		uint32_t f = r.Update (cache), ft = twin.Update (cache);
		CHECK ((f & COLLSH_PARTITION));
		CHECK_FALSE ((ft & COLLSH_PARTITION));
		CHECK (r.sh.nPart () == (step ? 26u : 27u));
		CHECK ((r.sh.PartOf (1, 45) == r.sh.PartOf (1, 0)) == (step == 1));
		door = r.sh.PartOf (1, ATL_cargodooroutL);
		const CollPart &D = r.sh.Part (door), &T = twin.sh.Part (twin.sh.PartOf (1, ATL_cargodooroutL));
		CHECK (D.version != ver);
		for (int k = 0; k < 9; k++) CHECK (D.anim[0].A.data[k] == old1.A.data[k]);
		for (int k = 0; k < 3; k++) CHECK (D.anim[0].t.data[k] == old1.t.data[k]);
		for (int k = 0; k < 9; k++) CHECK (D.anim[1].A.data[k] == T.anim[1].A.data[k]);
		CHECK (D.motion > 0.01);
		CHECK (D.motion == T.motion);
		CHECK (r.sh.Part (r.sh.PartOf (1, 0)).motion == 0);
	}
}

TEST_CASE("Partition change drops private copies; Replaced names the mesh and re-applied dents equal the old ones (code review C-A-a 3)", "[collshape][stock]")
{
	CollTemplateCache cache;
	AtlRig r;
	r.mi.resize (4);
	r.mi[3] = r.mi[1];                 // a second Atlantis mesh in slot 3 (no animation): carried across slot 1's partition change
	r.v.meshGrp.push_back (ATL_NGRP);
	r.Update (cache);
	CHECK (r.sh.Replaced (1));         // first build: CollSource applies the records
	CHECK (r.sh.Replaced (3));
	CHECK_FALSE (r.sh.Replaced (0));   // no collider before or after
	CHECK_FALSE (r.sh.Replaced (2));
	CHECK_FALSE (r.sh.Replaced (4));
	r.Update (cache);
	CHECK_FALSE (r.sh.Replaced (1));
	uint32_t g0 = 0;
	auto field = [] (const void *, const Vector &x) { return Vector (0, x.y > 0 ? 0.05 : 0.0, x.z * 1e-3); };
	REQUIRE (r.sh.ApplyDent (1, &g0, 1, field, nullptr) > 0);
	REQUIRE (r.sh.ApplyDent (3, &g0, 1, field, nullptr) > 0);
	std::map<uint32_t, Vector> dented = GroupPositions (r.sh, 1, 0), dented3 = GroupPositions (r.sh, 3, 0);
	REQUIRE (!dented.empty ());
	// a component on group 45 of mesh 1 only: mesh 1 is re-partitioned, mesh 3 keeps its parts
	UINT an = r.v.CreateAnimation (0);
	r.v.AddAnimationComponent (an, 0, 1, r.mod.Rot (1, r.mod.Grp ({45}), 1, _V(0,0,0), _V(1,0,0), 0.1f));
	uint32_t f = r.Update (cache);
	REQUIRE ((f & COLLSH_PARTITION));
	CHECK (r.sh.Replaced (1));
	CHECK_FALSE (r.sh.Replaced (3));
	CHECK (r.sh.Part (r.sh.PartOf (1, 0)).own == nullptr);
	CHECK (r.sh.Part (r.sh.PartOf (3, 0)).own != nullptr);
	CHECK_FALSE (SamePositions (GroupPositions (r.sh, 1, 0), dented));
	CHECK (SamePositions (GroupPositions (r.sh, 3, 0), dented3));
	// CollSource: Damage::ReapplyDents (v, 1) for the replaced mesh only
	REQUIRE (r.sh.ApplyDent (1, &g0, 1, field, nullptr) > 0);
	CHECK (SamePositions (GroupPositions (r.sh, 1, 0), dented));
	r.Update (cache);
	CHECK_FALSE (r.sh.Replaced (1));
	// INSMESH of slot 3: rebuilt, dents gone, Replaced
	r.mi[3].serial++; r.ca.OnMeshInsert (3);
	f = r.Update (cache);
	CHECK ((f & COLLSH_REBUILT));
	CHECK (r.sh.Replaced (3));
	CHECK_FALSE (r.sh.Replaced (1));
	CHECK (r.sh.Part (r.sh.PartOf (3, 0)).own == nullptr);
	// slot dropped: Replaced once, then not
	r.mi[3].collide = false;
	r.Update (cache);
	CHECK (r.sh.Replaced (3));
	r.Update (cache);
	CHECK_FALSE (r.sh.Replaced (3));
}

TEST_CASE("Base collider: Follow stores BaseGeomObj's version and topo, SetObject leaves them (D1 5.4, code review C-A-a 4)", "[collshape]")
{
	std::vector<CollGroupData> o = { Box (Vector (0, 5, 0), Vector (5, 5, 5)) };
	CollBaseShape bs;
	REQUIRE (bs.SetObject (0, 3, o.data(), o.size(), 1, true));
	bs.Follow (0, 7, 2);
	CHECK (bs.Obj (0).geomVersion == 7);
	CHECK (bs.Obj (0).topo == 2);
	// two dents between syncs (BaseGeomObj::version 7 -> 9): one move + refit; stored pair matches
	for (CollVtx &v : o[0].vtx) if (v.y > 5) v.y += 0.5f;
	REQUIRE (bs.SetObject (0, 3, o.data(), o.size(), 1, false));
	CHECK (bs.Obj (0).geomVersion == 7);
	CHECK (bs.Obj (0).topo == 2);
	bs.Follow (0, 9, 2);
	CHECK (bs.Obj (0).geomVersion == 9);
	REQUIRE (bs.SetObject (0, 3, o.data(), o.size(), 1, true));
	CHECK (bs.Obj (0).topo == 2);
	bs.Follow (4, 1, 1);
	CHECK (bs.nObj () == 1);
	// index guard (no resize wrap)
	CHECK_FALSE (bs.SetObject (~0u, 3, o.data(), o.size(), 1, true));
	CHECK_FALSE (bs.SetObject (COLL_RANGE_MAX, 3, o.data(), o.size(), 1, true));
	CHECK (bs.nObj () == 1);
	// a non-finite input vertex in a dent-only update stays where it was, logged once; the others move
	LogCapture lc;
	const CollGeom &G = bs.Obj (0).geom;
	const CollRef &r0 = G.ref[G.refOfs[0]];
	Vector keep = G.Pos (0);
	for (CollVtx &v : o[0].vtx) v.x += 0.25f;
	o[0].vtx[r0.vtx].x = std::numeric_limits<float>::quiet_NaN();
	REQUIRE (bs.SetObject (0, 3, o.data(), o.size(), 1, false));
	REQUIRE (bs.SetObject (0, 3, o.data(), o.size(), 1, false));
	CHECK (G.Pos (0).x == keep.x);
	for (uint32_t v = 1; v < G.vtx.size(); v++) CHECK (std::isfinite (G.Pos (v).x));
	for (const CollNode &n : G.node) for (int k = 0; k < 3; k++) { CHECK (std::isfinite (n.mn[k])); CHECK (std::isfinite (n.mx[k])); }
	CHECK (lc.Count ("non-finite") == 1);
}

TEST_CASE("Scale to zero: the part is left out of queries until its mesh is re-inserted (D1 8, code review C-A-a 9)", "[collshape]")
{
	auto box = std::make_shared<CollRestMesh> ();
	box->grp.push_back (Box (Vector (0, 0, 0), Vector (1, 1, 1)));
	box->grp.push_back (Box (Vector (4, 0, 0), Vector (1, 1, 1)));
	TestVessel v; TestModule mod; CollAnim ca; CollShape sh; CollTemplateCache cache;
	v.coll = &ca;
	v.meshGrp = { 2 };
	UINT an = v.CreateAnimation (0);
	v.AddAnimationComponent (an, 0, 1, mod.Scl (0, mod.Grp ({1}), 1, _V(4,0,0), _V(0,1,1)));
	CollMeshInfo mi;
	mi.present = mi.collide = true; mi.serial = 1; mi.key = "F:boxes"; mi.rest = box;
	auto upd = [&] () { v.Step (); return sh.Update (&mi, 1, ca, v.anim, v.nanim, cache); };
	LogCapture lc;
	upd ();
	int ps = sh.PartOf (0, 0), pk = sh.PartOf (0, 1);
	REQUIRE (pk >= 0);
	REQUIRE (ps != pk);
	CHECK (sh.GroupMask (pk) == nullptr);
	v.SetAnimation (an, 0.5); upd ();
	CHECK (sh.GroupMask (pk) == nullptr);   // det 0.5
	v.SetAnimation (an, 1.0); upd ();
	const uint8_t *mk = sh.GroupMask (pk);
	REQUIRE (mk != nullptr);
	for (size_t k = 0; k < sh.Part (pk).Geom ().srcTab.size(); k++) CHECK (mk[k] != 0);
	CHECK (sh.GroupMask (ps) == nullptr);
	v.SetAnimation (an, 0.5); upd ();       // scaling back divides by zero in the client: the replica stays at zero
	CHECK (sh.GroupMask (pk) != nullptr);
	upd ();
	CHECK (sh.GroupMask (pk) != nullptr);
	// SetGroupHidden on the other part keeps the singular mask
	sh.SetGroupHidden (0, 0, true);
	CHECK (sh.GroupMask (pk) != nullptr);
	CHECK (sh.GroupMask (ps) != nullptr);
	sh.SetGroupHidden (0, 0, false);
	// INSMESH: fresh identity matrices, back in queries
	mi.serial++; ca.OnMeshInsert (0);
	upd ();
	CHECK (sh.GroupMask (sh.PartOf (0, 1)) == nullptr);
	CHECK (lc.Count ("singular") == 1);
}

TEST_CASE("Hidden groups end with the client mesh; bad indices ignored; fewer slots drop their parts (code review C-A-a 15)", "[collshape]")
{
	auto box = std::make_shared<CollRestMesh> ();
	box->grp.push_back (Box (Vector (0, 0, 0), Vector (1, 1, 1)));
	box->grp.push_back (Box (Vector (4, 0, 0), Vector (1, 1, 1)));
	TestVessel v; TestModule mod; CollAnim ca; CollShape sh; CollTemplateCache cache;
	v.coll = &ca;
	v.meshGrp = { 2, 2 };
	UINT an = v.CreateAnimation (0);
	v.AddAnimationComponent (an, 0, 1, mod.Rot (0, mod.Grp ({1}), 1, _V(3,0,0), _V(0,0,1), (float)(PI/2)));
	CollMeshInfo mi[2];
	for (int m = 0; m < 2; m++) { mi[m].present = mi[m].collide = true; mi[m].serial = 1; mi[m].key = "F:boxes"; mi[m].rest = box; mi[m].ofs = Vector (0, 0, 10.0 * m); }
	uint32_t nm = 2;
	auto upd = [&] () { v.Step (); return sh.Update (mi, nm, ca, v.anim, v.nanim, cache); };
	upd ();
	REQUIRE (sh.nPart () == 3);         // mesh 0: static and rotating box; mesh 1: one static part
	sh.SetGroupHidden (0, 1, true);
	sh.SetGroupHidden (1, 1, true);
	CHECK (sh.GroupMask (sh.PartOf (0, 1)) != nullptr);
	v.SetAnimation (an, 0.5);
	upd ();
	CHECK (sh.GroupMask (sh.PartOf (0, 1)) != nullptr);
	// INSMESH of slot 0: the client's hide flags died with its old mesh; slot 1 keeps them
	mi[0].serial++; ca.OnMeshInsert (0);
	CHECK ((upd () & COLLSH_REBUILT));
	CHECK (sh.GroupMask (sh.PartOf (0, 1)) == nullptr);
	CHECK (sh.GroupMask (sh.PartOf (1, 1)) != nullptr);
	sh.SetGroupHidden (~0u, 0, true);
	sh.SetGroupHidden (0, ~0u, true);
	sh.SetGroupHidden (COLL_RANGE_MAX, 0, true);
	sh.SetGroupHidden (0, COLL_RANGE_MAX, true);
	CHECK (sh.GroupMask (sh.PartOf (0, 0)) == nullptr);
	CHECK (sh.GroupMask (sh.PartOf (0, 1)) == nullptr);
	// one slot fewer: its parts go, the rest is carried
	uint32_t ver0 = sh.Part (sh.PartOf (0, 0)).version;
	nm = 1;
	uint32_t f = upd ();
	CHECK ((f & COLLSH_REBUILT));
	CHECK (sh.nPart () == 2);
	CHECK (sh.PartOf (1, 0) == -1);
	CHECK (sh.Part (sh.PartOf (0, 0)).version == ver0);
	CHECK (sh.OffsetChange (1).length () == 0);
	CHECK_FALSE (sh.Replaced (0));
	CHECK_FALSE (sh.Replaced (1));
}

TEST_CASE("ApplyDent: a non-finite displacement leaves the vertex in place (code review C-A-a 8)", "[collshape]")
{
	auto box = std::make_shared<CollRestMesh> ();
	box->grp.push_back (Box (Vector (0, 0, 0), Vector (1, 1, 1)));
	CollAnim ca; CollShape sh; CollTemplateCache cache;
	CollMeshInfo mi;
	mi.present = mi.collide = true; mi.serial = 1; mi.rest = box;
	sh.Update (&mi, 1, ca, nullptr, 0, cache);
	REQUIRE (sh.nPart () == 1);
	LogCapture lc;
	auto field = [] (const void *, const Vector &x) { return x.x > 0 ? Vector (std::numeric_limits<double>::quiet_NaN(), 0, 0) : Vector (0, -0.1, 0); };
	CHECK (sh.ApplyDent (0, nullptr, 0, field, nullptr) == 4);
	CHECK (sh.ApplyDent (0, nullptr, 0, field, nullptr) == 4);
	const CollGeom &G = sh.Part (0).Geom ();
	for (uint32_t v = 0; v < G.vtx.size(); v++) {
		const Vector &p = G.Pos (v), &q = G.RestPos (v);
		CHECK (p.x == q.x); CHECK (p.z == q.z);
		CHECK (fabs (p.y - (q.x > 0 ? q.y : q.y - 0.2)) < 1e-12);
	}
	for (const CollNode &n : G.node) for (int k = 0; k < 3; k++) { CHECK (std::isfinite (n.mn[k])); CHECK (std::isfinite (n.mx[k])); }
	CHECK (std::isfinite (G.bsRadius));
	CHECK (lc.Count ("non-finite") == 1);
}

TEST_CASE("Template build warnings: one line per mesh with its name (D1 1.2, code review C-A-a 13)", "[collshape]")
{
	auto m = std::make_shared<CollRestMesh> ();
	m->name = "hull_bad";
	m->grp.push_back (Box (Vector (0, 0, 0), Vector (1, 1, 1)));
	m->grp.push_back (Box (Vector (4, 0, 0), Vector (1, 1, 1)));
	for (CollGroupData &g : m->grp) for (uint16_t i : { 0, 1, 900 }) g.idx.push_back (i);  // one triangle with an index out of range per group
	TestVessel v; TestModule mod; CollAnim ca; CollShape sh; CollTemplateCache cache;
	v.coll = &ca;
	v.meshGrp = { 2 };
	UINT an = v.CreateAnimation (0);
	v.AddAnimationComponent (an, 0, 1, mod.Lin (0, mod.Grp ({1}), 1, _V(0,1,0)));
	CollMeshInfo mi;
	mi.present = mi.collide = true; mi.serial = 1; mi.key = "F:hull_bad"; mi.rest = m;
	LogCapture lc;
	v.Step ();
	sh.Update (&mi, 1, ca, v.anim, v.nanim, cache);
	REQUIRE (sh.nPart () == 2);
	CHECK (lc.Count ("index out of range") == 1);
	CHECK (lc.Count ("'hull_bad': 2 triangles with an index out of range") == 1);
}

TEST_CASE("Tag scanner: a huge GEOM triangle count does not overflow (code review C-A-a 15)", "[collshape]")
{
	std::string t = "MSHX1\nGROUPS 2\nLABEL a\nGEOM 3 1\n0 0 0\n1 0 0\n0 1 0\n0 1 2\nLABEL b\nGEOM 3 715827883\n0 0 0\n1 0 0\n0 1 0\n0 1 2\n";
	CollMeshTags tg;
	std::vector<std::string> w;
	REQUIRE (CollScanMeshTags (t.data(), t.size(), tg, w));
	CHECK (tg.label == std::vector<std::string> { "a" });  // the core reads past the end of the file: group dropped
}

TEST_CASE("Tag scanner: huge MATERIALS and TEXTURES counts stop at the end of the file (fix1)", "[collshape]")
{
	auto peak = [] { struct rusage u; getrusage (RUSAGE_SELF, &u); return (long)u.ru_maxrss; }; // kB
	long p0 = peak ();
	const std::string g = "MSHX1\nGROUPS 2\nMATERIAL 2\nGEOM 3 1\n0 0 0\n1 0 0\n0 1 0\n0 1 2\nMATERIAL 1\nTEXTURE 1\nGEOM 3 1\n0 0 0\n1 0 0\n0 1 0\n0 1 2\n";
	CollMeshTags tg;
	std::vector<std::string> w;
	std::string t = g + "MATERIALS 300000000\nhull\nglass\n";
	REQUIRE (CollScanMeshTags (t.data(), t.size(), tg, w));
	CHECK (tg.material == std::vector<std::string> { "glass", "hull" });
	t = g + "MATERIALS 2\nhull\nglass\nMATERIAL hull\n1 1 1 1\n1 1 1 1\n1 1 1 1\n0 0 0 1\nMATERIAL glass\n1 1 1 1\n1 1 1 1\n1 1 1 1\n0 0 0 1\nTEXTURES 300000000\nskin.dds\n";
	REQUIRE (CollScanMeshTags (t.data(), t.size(), tg, w));
	CHECK (tg.material == std::vector<std::string> { "glass", "hull" });
	CHECK (tg.texture == std::vector<std::string> { "default", "skin.dds" });
	CHECK (peak () - p0 < 50000);
}

TEST_CASE("ApplyDent: part spheres and the vessel bound follow the dent (fix1)", "[collshape]")
{
	auto box = std::make_shared<CollRestMesh> ();
	box->grp.push_back (Box (Vector (0, 0, 0), Vector (1, 1, 1)));
	box->grp.push_back (Box (Vector (4, 0, 0), Vector (1, 1, 1)));
	TestVessel v; TestModule mod; CollAnim ca; CollShape sh; CollTemplateCache cache;
	v.coll = &ca;
	v.meshGrp = { 2 };
	UINT an = v.CreateAnimation (0);
	v.AddAnimationComponent (an, 0, 1, mod.Lin (0, mod.Grp ({1}), 1, _V(0,1,0)));
	CollMeshInfo mi;
	mi.present = mi.collide = true; mi.serial = 1; mi.rest = box; mi.ofs = Vector (0, 0, 2);
	v.Step ();
	sh.Update (&mi, 1, ca, v.anim, v.nanim, cache);
	REQUIRE (sh.nPart () == 2);
	int p1 = sh.PartOf (0, 1);
	REQUIRE (p1 >= 0);
	uint32_t g1 = 1;
	auto field = [] (const void *, const Vector &x) { return x.x > 4.5 ? Vector (6, 0, 0) : Vector (); }; // outward dent of the far box's +x face
	REQUIRE (sh.ApplyDent (0, &g1, 1, field, nullptr) == 4);
	const CollPart &P = sh.Part (p1);
	const CollGeom &G = P.Geom ();
	for (int k = 0; k < 2; k++) {
		Vector c; double r;
		sh.Bound (k, c, r);
		for (uint32_t i = 0; i < G.vtx.size(); i++) {
			Vector x = CollApply (P.pose[k], G.Pos (i));
			CHECK ((x - P.sc[k]).length () <= P.sr[k] + 1e-9);
			CHECK (x.length () <= r + 1e-9);
		}
		CHECK (P.sc[k].x == CollApply (P.pose[k], G.bsCentre).x);
		CHECK (P.sr[k] == G.bsRadius);
	}
}
