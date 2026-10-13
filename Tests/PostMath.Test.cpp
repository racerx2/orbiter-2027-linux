// not upstream: TAA maths (OVP/VulkanClient/PostMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "PostMath.h"
#include <cmath>

using namespace PostMath;

TEST_CASE("PostMath: Halton base 2 and 3", "[postmath]")
{
	CHECK(std::fabs(Halton(1, 2) - 0.5) < 1e-12);
	CHECK(std::fabs(Halton(2, 2) - 0.25) < 1e-12);
	CHECK(std::fabs(Halton(1, 3) - 1.0 / 3.0) < 1e-12);
	CHECK(std::fabs(Halton(4, 3) - 4.0 / 9.0) < 1e-12);
}
