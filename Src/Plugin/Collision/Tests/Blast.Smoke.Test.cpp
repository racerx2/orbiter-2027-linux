// not upstream: smoke test of the vendored NVIDIA Blast subset (Design CA-blast 1): asset, family, fracture, split, stress solver
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>
#if !defined(NDEBUG) && !defined(_DEBUG)
#define _DEBUG // Blast's headers want exactly one of the two; CollBlastLib gives it, a bare compile may not
#endif
#include "NvBlast.h"
#include "NvBlastExtStressSolver.h"

namespace {

std::vector<std::string> g_log;

void Log (int type, const char *msg, const char *file, int line)
{
	if (type <= NvBlastMessage::Warning)
		g_log.push_back (std::string (msg ? msg : "") + " (" + (file ? file : "") + ":" + std::to_string (line) + ")");
}

struct alignas(16) Block { unsigned char b[16]; };

struct Mem { // 16-byte aligned storage as the low-level API wants
	std::vector<Block> v;
	void *Get (size_t n) { v.assign (n / sizeof (Block) + 1, Block {}); return v.data (); }
};

// four support chunks on the x axis, one root each, bonds 0-1, 1-2, 2-3 with the given areas
struct Chain {
	Mem assetMem, familyMem, scratch;
	NvBlastAsset *asset = nullptr;
	NvBlastFamily *family = nullptr;
	NvBlastActor *actor = nullptr;

	explicit Chain (const float area[3])
	{
		NvBlastChunkDesc chunks[4];
		for (uint32_t i = 0; i < 4; i++)
			chunks[i] = NvBlastChunkDesc { { float (i), 0.0f, 0.0f }, 1.0f, UINT32_MAX, NvBlastChunkDesc::SupportFlag, i };
		NvBlastBondDesc bonds[3];
		for (uint32_t i = 0; i < 3; i++)
			bonds[i] = NvBlastBondDesc { NvBlastBond { { 1.0f, 0.0f, 0.0f }, area[i], { float (i) + 0.5f, 0.0f, 0.0f }, i }, { i, i + 1 } };
		NvBlastAssetDesc desc { 4, chunks, 3, bonds };
		void *sc = scratch.Get (NvBlastGetRequiredScratchForCreateAsset (&desc, Log));
		asset = NvBlastCreateAsset (assetMem.Get (NvBlastGetAssetMemorySize (&desc, Log)), &desc, sc, Log);
		REQUIRE (asset);
		family = NvBlastAssetCreateFamily (familyMem.Get (NvBlastAssetGetFamilyMemorySize (asset, Log)), asset, Log);
		REQUIRE (family);
		std::vector<float> health (area, area + 3); // a bond's health is its remaining area
		NvBlastActorDesc ad {};
		ad.initialBondHealths = health.data ();
		ad.uniformInitialLowerSupportChunkHealth = 1.0f;
		actor = NvBlastFamilyCreateFirstActor (family, &ad, scratch.Get (NvBlastFamilyGetRequiredScratchForCreateFirstActor (family, Log)), Log);
		REQUIRE (actor);
	}

	std::vector<NvBlastActor *> Split (NvBlastActor *a)
	{
		std::vector<NvBlastActor *> out (NvBlastActorGetMaxActorCountForSplit (a, Log));
		NvBlastActorSplitEvent ev { nullptr, out.data () };
		uint32_t n = NvBlastActorSplit (&ev, a, uint32_t (out.size ()), scratch.Get (NvBlastActorGetRequiredScratchForSplit (a, Log)), Log, nullptr);
		out.resize (n);
		return out;
	}
};

uint32_t Nodes (const NvBlastActor *a)
{
	return NvBlastActorGetGraphNodeCount (a, Log);
}

// pulls chunk 0 along -x for up to 8 solver frames; the overstressed bond count of the last frame
uint32_t Pull (Nv::Blast::ExtStressSolver &s, const NvBlastActor &a, float force)
{
	uint32_t n = 0;
	for (int f = 0; f < 8 && !n; f++) {
		REQUIRE (s.addForce (a, NvcVec3 { 0.0f, 0.0f, 0.0f }, NvcVec3 { -force, 0.0f, 0.0f }));
		s.update ();
		n = s.getOverstressedBondCount ();
	}
	return n;
}

Nv::Blast::ExtStressSolverSettings Settings ()
{
	Nv::Blast::ExtStressSolverSettings st;
	st.maxSolverIterationsPerFrame = 64;
	st.compressionElasticLimit = 100.0f; // Pa
	st.compressionFatalLimit = 200.0f;
	return st;
}

} // namespace

TEST_CASE ("Blast asset, family, fracture of the middle bond, split into 2 actors", "[blast]")
{
	g_log.clear ();
	const float area[3] = { 1.0f, 1.0f, 1.0f };
	Chain c (area);
	CHECK (NvBlastAssetGetChunkCount (c.asset, Log) == 4);
	CHECK (NvBlastAssetGetSupportChunkCount (c.asset, Log) == 4);
	CHECK (NvBlastAssetGetBondCount (c.asset, Log) == 3);
	CHECK (NvBlastFamilyGetActorCount (c.family, Log) == 1);
	CHECK (Nodes (c.actor) == 4);

	NvBlastBondFractureData bf { 0, 1, 2, 10.0f }; // the 1-2 bond, damage above its health
	NvBlastFractureBuffers cmd { 1, 0, &bf, nullptr };
	NvBlastActorApplyFracture (nullptr, c.actor, &cmd, Log, nullptr);
	const float *h = NvBlastActorGetBondHealths (c.actor, Log);
	CHECK (h[0] > 0.0f);
	CHECK (h[1] <= 0.0f);
	CHECK (h[2] > 0.0f);
	REQUIRE (NvBlastActorIsSplitRequired (c.actor, Log));

	std::vector<NvBlastActor *> parts = c.Split (c.actor);
	REQUIRE (parts.size () == 2);
	CHECK (NvBlastFamilyGetActorCount (c.family, Log) == 2);
	CHECK (Nodes (parts[0]) == 2);
	CHECK (Nodes (parts[1]) == 2);
	for (NvBlastActor *a : parts) {
		uint32_t nodes[4] = {};
		uint32_t n = NvBlastActorGetGraphNodeIndices (nodes, 4, a, Log);
		REQUIRE (n == 2);
		CHECK ((nodes[0] < 2) == (nodes[1] < 2)); // {0,1} and {2,3} stay together
	}
	CHECK (g_log.empty ());
}

TEST_CASE ("Blast ExtStressSolver: a large pull breaks the weak bond, a small one breaks nothing", "[blast]")
{
	g_log.clear ();
	const float area[3] = { 0.01f, 1.0f, 1.0f }; // bond 0-1 weak
	const auto setup = [] (Chain &c) {
		Nv::Blast::ExtStressSolver *s = Nv::Blast::ExtStressSolver::create (*c.family, Settings ());
		REQUIRE (s);
		const NvBlastSupportGraph g = NvBlastAssetGetSupportGraph (c.asset, Log);
		REQUIRE (g.nodeCount == 4);
		for (uint32_t i = 0; i < g.nodeCount; i++)
			s->setNodeInfo (i, 1.0f, 1.0f, NvcVec3 { float (g.chunkIndices[i]), 0.0f, 0.0f }); // no static node: a floating ship
		REQUIRE (s->notifyActorCreated (*c.actor));
		return s;
	};

	SECTION ("small force") {
		Chain c (area);
		Nv::Blast::ExtStressSolver *s = setup (c);
		CHECK (Pull (*s, *c.actor, 0.1f) == 0); // weak bond ~7.5 Pa < 100 Pa elastic
		NvBlastFractureBuffers cmd {};
		s->generateFractureCommands (*c.actor, cmd);
		CHECK (cmd.bondFractureCount == 0);
		s->release ();
	}

	SECTION ("large force") {
		Chain c (area);
		Nv::Blast::ExtStressSolver *s = setup (c);
		CHECK (Pull (*s, *c.actor, 10.0f) > 0); // weak bond ~750 Pa > 200 Pa fatal, strong ones <= 5 Pa
		NvBlastFractureBuffers cmd {};
		s->generateFractureCommands (*c.actor, cmd);
		REQUIRE (cmd.bondFractureCount >= 1);
		NvBlastActorApplyFracture (nullptr, c.actor, &cmd, Log, nullptr);
		const float *h = NvBlastActorGetBondHealths (c.actor, Log);
		CHECK (h[0] <= 0.0f);
		CHECK (h[1] > 0.0f);
		CHECK (h[2] > 0.0f);
		s->notifyActorDestroyed (*c.actor);
		std::vector<NvBlastActor *> parts = c.Split (c.actor);
		REQUIRE (parts.size () == 2);
		uint32_t n[2] = { Nodes (parts[0]), Nodes (parts[1]) };
		CHECK (((n[0] == 1 && n[1] == 3) || (n[0] == 3 && n[1] == 1)));
		for (NvBlastActor *a : parts)
			s->notifyActorCreated (*a);
		s->release ();
	}
	CHECK (g_log.empty ());
}
