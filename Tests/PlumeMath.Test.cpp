// not upstream: plume, smoke and plasma maths (OVP/VulkanClient/PlumeMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "PlumeMath.h"

TEST_CASE("PlumeMath: unit present", "[plumemath]")
{
	CHECK(PlumeMath::Version() == 1);
}
