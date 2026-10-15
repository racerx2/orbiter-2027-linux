// not upstream: a base file's clear zones (BaseClear.cpp, design base-clear)
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "BaseClear.h"
#include <cmath>
#include <sstream>

using Catch::Matchers::WithinAbs;

namespace {
const double DEG = std::acos (-1.0) / 180.0;
}

TEST_CASE ("clear zones: good lines, comments, CRLF and indents; a bad line is skipped with a warning; END ends the block", "[BaseClear]")
{
	std::istringstream is (
		"\t-80.6040720 28.6083850 400 ; LC-39A\r\n"
		"; a comment\r\n"
		"\r\n"
		"  -80.6208620   28.6272150\t400\r\n"
		"-80.6 28.6\r\n"
		"-80.6 95 100\r\n"
		"-80.6 28.6 0\r\n"
		"-80.6 28.6 -5\r\n"
		"-80.6 28.6 nan\r\n"
		"10 20 9000\r\n"
		"  end_clearzones\r\n"
		"1 2 3\r\n");
	std::vector<BaseClearZone> z;
	std::vector<std::string> warn;
	ReadClearZones (is, z, warn);
	REQUIRE (z.size () == 3);
	CHECK_THAT (z[0].lng, WithinAbs (-80.6040720 * DEG, 1e-12));
	CHECK_THAT (z[0].lat, WithinAbs (28.6083850 * DEG, 1e-12));
	CHECK (z[0].rad == 400.0);
	CHECK_THAT (z[1].lng, WithinAbs (-80.6208620 * DEG, 1e-12));
	CHECK (z[1].rad == 400.0);
	CHECK_THAT (z[2].lat, WithinAbs (20.0 * DEG, 1e-12));
	CHECK (z[2].rad == BaseClearMax);
	CHECK (warn.size () == 6);
	for (const std::string &w : warn) CHECK (w.find ('\r') == std::string::npos);
}

TEST_CASE ("clear zones: a block without END runs to the end of the file; an empty block gives none", "[BaseClear]")
{
	std::istringstream a ("1 2 3\n4 5 6");
	std::vector<BaseClearZone> z;
	std::vector<std::string> warn;
	ReadClearZones (a, z, warn);
	CHECK (z.size () == 2);
	CHECK (warn.empty ());
	std::istringstream b ("END_CLEARZONES\n1 2 3\n");
	z.clear ();
	ReadClearZones (b, z, warn);
	CHECK (z.empty ());
	CHECK (warn.empty ());
}
