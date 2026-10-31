// not upstream (nose-wheel): the nose wheel's swivel, steering angle and frames
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include "NoseWheel.h"

using namespace NoseWheel;
using Catch::Matchers::WithinAbs;

static const double DEG = 3.14159265358979323846 / 180.0;

TEST_CASE ("A wheel rolls more easily than it slides", "[NoseWheel]")
{
	CHECK (IsWheel (0.1, 1.6));
	CHECK_FALSE (IsWheel (3.0, 3.0));
	CHECK_FALSE (IsWheel (0.8, 0.5));
}

TEST_CASE ("Without steering it swivels at taxi speed and grips from twice that", "[NoseWheel]")
{
	CHECK (Grip (0.0, 0.0) == 0.0);
	CHECK (Grip (4.9, 0.0) == 0.0);
	CHECK (Grip (-3.0, 0.0) == 0.0);
	CHECK_THAT (Grip (7.5, 0.0), WithinAbs (0.5, 1e-12));
	CHECK (Grip (10.0, 0.0) == 1.0);
	CHECK (Grip (80.0, 0.0) == 1.0);
	CHECK (Grip (2.0, 0.3) == 1.0);
	CHECK (Grip (2.0, -0.02) == 1.0);
	CHECK (Grip (2.0, 0.005) == 0.0);
}

TEST_CASE ("The steering angle shrinks with speed", "[NoseWheel]")
{
	CHECK_THAT (SteerMax (0.0), WithinAbs (60 * DEG, 1e-12));
	CHECK_THAT (SteerMax (5.0), WithinAbs (60 * DEG, 1e-12));
	CHECK_THAT (SteerMax (10.0), WithinAbs (30 * DEG, 1e-12));
	CHECK_THAT (SteerMax (-20.0), WithinAbs (15 * DEG, 1e-12));
	CHECK_THAT (SteerMax (100.0), WithinAbs (6 * DEG, 1e-12));
	for (double v = 0; v < 120; v += 0.5) CHECK (SteerMax (v + 0.5) <= SteerMax (v));
}

TEST_CASE ("The command follows at its rate and stays within -1..1", "[NoseWheel]")
{
	CHECK_THAT (Follow (0.0, 1.0, 0.1), WithinAbs (0.1, 1e-12));
	CHECK_THAT (Follow (0.95, 1.0, 0.1), WithinAbs (1.0, 1e-12));
	CHECK_THAT (Follow (0.5, -3.0, 2.0), WithinAbs (-1.0, 1e-12));
	CHECK_THAT (Follow (-0.2, 0.0, 0.05), WithinAbs (-0.15, 1e-12));
	CHECK (Follow (0.3, 0.3, 0.0) == 0.3);
}

TEST_CASE ("The wheel's frame turns with the steering angle and back", "[NoseWheel]")
{
	double vw, vn;
	Frame (0.0, 3.0, -1.0, vw, vn);
	CHECK_THAT (vw, WithinAbs (3.0, 1e-12));
	CHECK_THAT (vn, WithinAbs (-1.0, 1e-12));
	// steered 30 degrees right while rolling straight ahead: the ground moves across the wheel to its left
	Frame (30 * DEG, 4.0, 0.0, vw, vn);
	CHECK_THAT (vw, WithinAbs (4.0 * std::cos (30 * DEG), 1e-12));
	CHECK (vn < 0.0);
	// a grip against that slide pushes the nose to the right (positive lateral), and so on in reverse
	double flon, flat;
	ToVessel (30 * DEG, 0.0, -vn, flon, flat);
	CHECK (flat > 0.0);
	Frame (30 * DEG, -4.0, 0.0, vw, vn);
	ToVessel (30 * DEG, 0.0, -vn, flon, flat);
	CHECK (flat < 0.0);
	// the round trip keeps the force
	double a = 0.7, fw = 120.0, fn = -45.0;
	ToVessel (a, fw, fn, flon, flat);
	double bw, bn;
	Frame (a, flon, flat, bw, bn);
	CHECK_THAT (bw, WithinAbs (fw, 1e-9));
	CHECK_THAT (bn, WithinAbs (fn, 1e-9));
}
