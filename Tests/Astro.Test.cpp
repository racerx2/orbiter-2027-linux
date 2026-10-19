// not upstream: unit tests for Src/Orbiter/Astro and TimeData
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstring>
#include "Astro.h"
#include "TimeData.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("MJD date conversions", "[astro]")
{
	struct tm *d = mjddate(MJD2000);  // Orbiter keeps tm_mon 1-based here
	REQUIRE(d->tm_year == 100);
	REQUIRE(d->tm_mon == 1);
	REQUIRE(d->tm_mday == 1);
	REQUIRE(d->tm_hour == 12);
	REQUIRE_THAT(date2mjd(d), WithinAbs(MJD2000, 1e-9));
	REQUIRE(std::strcmp(DateStr(MJD2000), "Sat Jan 01 12:00:00 2000") == 0);
	REQUIRE_THAT(MJD((time_t)0), WithinAbs(40587.0, 1e-12));
	REQUIRE_THAT(JC2MJD(MJD2JC(60000.25)), WithinAbs(60000.25, 1e-9));
}

TEST_CASE("UTC to TT uses historical TAI-UTC values", "[astro][timescale]")
{
    // Independent published UTC-TAI table epochs, MJD at 00:00 UTC.
    // UTC-TAI was 19 s at STS-1; 32 s at STS-107; 37 s since 2017.
    struct Epoch { double mjd, taiUtc; };
    const Epoch epochs[] = {
        {41317.0, 10.0}, // 1972-01-01
        {41499.0, 11.0}, // 1972-07-01: first leap second took effect
        {44706.0, 19.0}, // 1981-04-12: STS-1
        {52671.0, 32.0}, // 2003-02-01: STS-107
        {57203.0, 35.0}, // 2015-06-30: before leap second
        {57204.0, 36.0}, // 2015-07-01
        {57753.0, 36.0}, // 2016-12-31: before leap second
        {57754.0, 37.0}, // 2017-01-01
        {61331.0, 37.0}, // 2026-10-18: no leap seconds after 2016
    };
    for (const Epoch &e : epochs) {
        INFO("UTC MJD " << e.mjd);
        REQUIRE_THAT(TaiMinusUtc(e.mjd), WithinAbs(e.taiUtc, 1e-10));
        REQUIRE_THAT((UtcToTtMjd(e.mjd) - e.mjd)*86400.0,
                     WithinAbs(e.taiUtc + 32.184, 1e-5));
    }
}

TEST_CASE("Pre-1972 UTC drift includes Apollo era", "[astro][timescale]")
{
    // 1969-07-16: ERFA/SOFA expression 4.213170 + (MJD-39126)*0.002592.
    const double apollo11 = 40418.0;
    const double taiUtc = 4.213170 + (apollo11 - 39126.0)*0.002592;
    REQUIRE_THAT(TaiMinusUtc(apollo11), WithinAbs(taiUtc, 1e-10));
    REQUIRE_THAT((UtcToTtMjd(apollo11) - apollo11)*86400.0,
                 WithinAbs(taiUtc + 32.184, 1e-5));
    // Earlier UTC was introduced with small fractional steps and drift.
    REQUIRE_THAT(TaiMinusUtc(37300.0), WithinAbs(1.422818, 1e-10));
    REQUIRE_THAT(TaiMinusUtc(37512.0), WithinAbs(1.372818 + 212*0.001296, 1e-10));
    REQUIRE_THAT(TaiMinusUtc(37665.0), WithinAbs(1.845858, 1e-10));
    REQUIRE_THAT(TaiMinusUtc(39887.0), WithinAbs(4.213170 + 761*0.002592, 1e-10));
    // No defined UTC conversion prior to the introduction of UTC in 1960.
    REQUIRE(std::isnan(TaiMinusUtc(36933.0)));
    REQUIRE(std::isnan(UtcToTtMjd(36933.0)));
}

TEST_CASE("Coordinate transformations", "[astro]")
{
	REQUIRE_THAT(Obliquity(0.0), WithinAbs(0.4090928042, 1e-12));
	double ob = Obliquity(0.2), l, b, ra, dc;
	Equ2Ecl(cos(ob), sin(ob), 1.2, -0.3, l, b);
	Ecl2Equ(cos(ob), sin(ob), l, b, ra, dc);
	REQUIRE_THAT(ra, WithinAbs(1.2, 1e-12));
	REQUIRE_THAT(dc, WithinAbs(-0.3, 1e-12));
	REQUIRE_THAT(Orthodome(0.0, 0.0, PI05, 0.0), WithinAbs(PI05, 1e-12));
	double dist, dir;
	Orthodome(0.0, 0.0, 0.0, 0.5, dist, dir);
	REQUIRE_THAT(dist, WithinAbs(0.5, 1e-12));
	REQUIRE_THAT(dir, WithinAbs(0.0, 1e-12));
}

TEST_CASE("Number formatting", "[astro]")
{
	REQUIRE(std::strcmp(FloatStr(12346.0), " 12.35k") == 0);
	REQUIRE(std::strcmp(DistStr(10.0*AU), " 10.00AU") == 0);
	REQUIRE(std::strcmp(SciStr(1.5e-7, 3), "1.5\xc2\xb7" "10^-7") == 0);
}

TEST_CASE("TimeData stepping and warp", "[timedata]")
{
	TimeData td;
	td.Reset(MJD2000);
	td.BeginStep(0.5, true);
	td.EndStep(true);
	REQUIRE_THAT(td.SimT0, WithinAbs(0.5, 1e-15));
	REQUIRE_THAT(td.MJD0, WithinAbs(MJD2000 + 0.5/86400.0, 1e-12));
	td.SetWarp(10.0);
	REQUIRE(td.WarpChanged());
	td.BeginStep(0.5, true);
	td.EndStep(true);
	REQUIRE_THAT(td.SimT0, WithinAbs(5.5, 1e-12));
	td.BeginStep(0.5, false);  // paused: sim time stands still
	td.EndStep(false);
	REQUIRE_THAT(td.SimT0, WithinAbs(5.5, 1e-12));
	REQUIRE_THAT(td.JumpTo(MJD2000 + 1.0), WithinAbs(86400.0 - 5.5, 1e-6));
	REQUIRE(td.FrameCount() == 3);
}
