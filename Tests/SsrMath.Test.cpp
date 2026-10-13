// not upstream: screen-space reflection tracing maths (OVP/VulkanClient/SsrMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "SsrMath.h"

TEST_CASE("SsrMath: unit present", "[ssrmath]")
{
	CHECK(SsrMath::Version() == 1);
}
