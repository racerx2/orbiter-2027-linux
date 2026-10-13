// not upstream: height fog maths (OVP/VulkanClient/FogMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "FogMath.h"

TEST_CASE("FogMath: unit present", "[fogmath]")
{
	CHECK(FogMath::Version() == 1);
}
