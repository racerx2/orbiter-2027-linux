// not upstream: cascade fitting and texel snapping for the scene shadows (OVP/VulkanClient/CsmMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "CsmMath.h"

TEST_CASE("CsmMath: unit present", "[csmmath]")
{
	CHECK(CsmMath::Version() == 1);
}
