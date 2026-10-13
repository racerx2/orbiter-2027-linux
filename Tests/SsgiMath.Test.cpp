// not upstream: screen-space GI maths (OVP/VulkanClient/SsgiMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "SsgiMath.h"

TEST_CASE("SsgiMath: unit present", "[ssgimath]")
{
	CHECK(SsgiMath::Version() == 1);
}
