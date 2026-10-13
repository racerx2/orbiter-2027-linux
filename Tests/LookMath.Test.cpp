// not upstream: look pass maths (OVP/VulkanClient/LookMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "LookMath.h"
#include <cmath>

using namespace LookMath;

TEST_CASE("LookMath: the filmic curve matches 1 - exp(-x) in the mid tones", "[lookmath]")
{
	for (double x = 0.3; x <= 2.0001; x += 0.05) CHECK(std::fabs(Filmic(x) - (1.0 - std::exp(-x))) < 0.04);
	CHECK(Filmic(0.0) == 0.0);
	CHECK(Filmic(100.0) <= 1.0);
}
