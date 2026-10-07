// not upstream: unit tests for the .msh reader in Src/Orbiter/Mesh.cpp
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <sstream>
#include "Mesh.h"
#include "Config.h"
#include "Util.h"

// stubs: Mesh.cpp links against the core for paths, logging and textures
class Orbiter;
Orbiter *g_pOrbiter = nullptr;
char *Config::MeshPath (const char *name) { static char p[256]; strncpy (p, name, 255); return p; }
DWORDLONG Str2Crc (const char *str) { return 0; }
void LogOut_Error (const char *func, const char *file, int line, const char *msg, ...) {}

static const char *tri3 =
	"0 0 0 0 0 1 0 0\n"
	"1 0 0 0 0 1 1 0\n"
	"0 1 0 0 0 1 0 1\n";

static void Load (Mesh &mesh, const std::string &body)
{
	std::istringstream is ("MSHX1\n" + body);
	is >> mesh;
}

TEST_CASE("A valid mesh loads", "[mesh]")
{
	Mesh mesh;
	Load (mesh, std::string ("GROUPS 1\nFLAG 5\nGEOM 3 1\n") + tri3 + "0 1 2\nMATERIALS 0\nTEXTURES 0\n");
	REQUIRE(mesh.nGroup () == 1);
	REQUIRE(mesh.GetGroup (0)->nVtx == 3);
	REQUIRE(mesh.GetGroup (0)->nIdx == 3);
	REQUIRE(mesh.GetGroup (0)->Idx[2] == 2);
	REQUIRE(mesh.GetGroupUsrFlag (0) == 5);
}

TEST_CASE("An index past the vertex list makes a null triangle", "[mesh]")
{
	Mesh mesh;
	Load (mesh, std::string ("GROUPS 1\nNONORMAL\nGEOM 3 2\n") +
		"0 0 0 0 0\n1 0 0 1 0\n0 1 0 0 1\n0 1 2\n0 1 7\n");
	REQUIRE(mesh.nGroup () == 1);
	WORD *idx = mesh.GetGroup (0)->Idx;
	REQUIRE((idx[0] == 0 && idx[1] == 1 && idx[2] == 2));
	REQUIRE((idx[3] == 0 && idx[4] == 0 && idx[5] == 0));
}

TEST_CASE("A GEOM line with one count skips the group", "[mesh]")
{
	Mesh mesh;
	Load (mesh, std::string ("GROUPS 2\nGEOM 3 1\n") + tri3 + "0 1 2\nGEOM 3\n");
	REQUIRE(mesh.nGroup () == 1);
}

TEST_CASE("Counts out of range skip the group", "[mesh]")
{
	Mesh mesh;
	Load (mesh, "GROUPS 3\nGEOM -1 1\nGEOM 3 -5\nGEOM 2000000 1\n");
	REQUIRE(mesh.nGroup () == 0);
}

TEST_CASE("A skipped group leaves its flags off the next group", "[mesh]")
{
	Mesh mesh;
	Load (mesh, std::string ("GROUPS 2\nFLAG 9\nGEOM 3\nFLAG 5\nGEOM 3 1\n") + tri3 + "0 1 2\n");
	REQUIRE(mesh.nGroup () == 1);
	REQUIRE(mesh.GetGroupUsrFlag (0) == 5);
}

TEST_CASE("Material and texture counts out of range are ignored", "[mesh]")
{
	Mesh mesh;
	Load (mesh, std::string ("GROUPS 1\nGEOM 3 1\n") + tri3 + "0 1 2\nMATERIALS -1\nTEXTURES -1\n");
	REQUIRE(mesh.nGroup () == 1);
	REQUIRE(mesh.nMaterial () == 0);
	REQUIRE(mesh.nTexture () == 0);
}

TEST_CASE("A short material list stops at the end of the file", "[mesh]")
{
	Mesh mesh;
	Load (mesh, std::string ("GROUPS 1\nGEOM 3 1\n") + tri3 + "0 1 2\nMATERIALS 3\na\nb\nc\n"
		"MATERIAL a\n1 1 1 1\n1 1 1 1\n0 0 0 1 0\n0 0 0 1\n");
	REQUIRE(mesh.nMaterial () == 1);
}
