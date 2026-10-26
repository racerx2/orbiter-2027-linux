// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: .msh labels, material and texture names as the core reads groups (design 4)

#include <catch2/catch_test_macros.hpp>
#include "MshText.h"
#include <fstream>
#include <sstream>

TEST_CASE ("Labels follow the groups the core loads", "[MshText]")
{
	std::istringstream is (
		"MSHX1\r\n"
		"GROUPS 3\r\n"
		"LABEL Nose\r\n"
		"MATERIAL 1\r\n"
		"GEOM 3 1\r\n"
		"0 0 0 0 1 0\r\n1 0 0 0 1 0\r\n0 1 0 0 1 0\r\n"
		"0 1 2\r\n"
		"LABEL Broken\r\n"
		"GEOM 0 0\r\n"
		"GEOM 3 1\r\n"
		"0 0 0\r\n1 0 0\r\n0 1 0\r\n"
		"0 1 2\r\n"
		"MATERIALS 2\r\n"
		"hull\r\nglass\r\n"
		"MATERIAL hull\r\n1 1 1 1\r\n1 1 1 1\r\n0 0 0 1\r\n0 0 0 1\r\n"
		"MATERIAL glass\r\n1 1 1 0.5\r\n1 1 1 1\r\n0 0 0 1\r\n0 0 0 1\r\n"
		"TEXTURES 2\r\n"
		"DG\\hull.dds\r\n"
		"DG\\panel.dds D\r\n");
	GxMshText t;
	REQUIRE (GxReadMshText (is, t));
	// group 2's bad GEOM ends that group (no label); the third GEOM is the next group, the core's two groups
	REQUIRE (t.labels.size () == 2);
	CHECK (t.labels[0] == "Nose");
	CHECK (t.labels[1] == "");
	CHECK (t.materials == std::vector<std::string>{"hull", "glass"});
	CHECK (t.textures == std::vector<std::string>{"DG\\hull.dds", "DG\\panel.dds"});
}

TEST_CASE ("The stock DG meshes", "[MshText]")
{
	std::string root = GLTFX_SOURCE_ROOT;
	std::ifstream ext (root + "/Src/Vessel/DeltaGlider/Meshes/deltaglider.msh");
	if (!ext) SKIP ("no DG mesh in the source tree");
	GxMshText t;
	REQUIRE (GxReadMshText (ext, t));
	CHECK (t.labels.size () == 122);
	int named = 0;
	for (auto &l : t.labels) if (!l.empty ()) named++;
	CHECK (named == 82);
	CHECK (t.materials.size () == 27);
	REQUIRE (t.textures.size () >= 2);
	CHECK (t.textures[1] == "DG\\DGMK4_1.dds");
}
