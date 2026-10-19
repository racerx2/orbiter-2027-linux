# Neptune JPL ephemeris in Orbiter 2027 Linux

Neptune now uses a compact JPL-derived table of the **Neptune system
barycenter relative to the Sun**. The old VSOP87B implementation remains
the fallback. No CSPICE, new runtime libraries, or internet access are
required to run Orbiter.

Source: NASA JPL Horizons API, target 8 (Neptune system barycenter,
DE441), origin 500@10 (Sun), geometric vectors (no light-time correction),
J2000 mean ecliptic / ICRF, TDB, km and km/s.

Generated data: Src/Celbody/Vsop87/Neptune/Config/Neptune/Data/neptune.cheb
Format: existing CHEB1 little-endian double format, 32-day segments,
12 coefficients per axis, 4566 segments. Coverage: MJD -21504 to 124608,
roughly 1800-01-01 to 2200-01-16 TDB, last endpoint exclusive.
The generated data file is about 1.3 MB. Neptune's module returns the
system barycenter, and existing Orbiter code calculates its physical
center using the masses and trajectories of its satellites.

To rebuild the table (developer machine only; Python 3, NumPy, access
to JPL Horizons required):

    python3 Tools/ephemeris/build_neptune_cheb.py
    cmake --build out/build/linux-x64-release --target Neptune Celbody.Test -j 4
    ctest --test-dir out/build/linux-x64-release -R '^Celbody.Test$' --output-on-failure

The table generator stages output outside CMake-watched Config directories.
It installs the data file atomically only if numerical-error thresholds
are satisfied. Chebyshev segment endpoints are pinned to JPL positions
and velocities, giving continuous position and velocity between segments.
A one-year quintic transition inside each end of the data span makes the
VSOP87B fallback continuous in position and velocity.

Independent JPL Horizons comparisons at unsampled epochs and fallback
boundary tests are included in Tests/Celbody.Test.cpp.

CAUTION: Horizons target 899 (physical Neptune center; nep098_merged)
and target 8 (Neptune system barycenter; DE441) use DIFFERENT planetary
reference solutions. At the 2026 test epoch their positions differ by
~318 km; at the Voyager 2 encounter in 1989 the difference is ~74 km.
This inconsistency between source products is not a CHEB1 fitting error.
Do not adjust a DE441 barycenter to match the separate #899 ephemeris.

This Neptune data improvement does not by itself make simulated historical
Voyager flyby trajectories match JPL; spacecraft force models and specific
historical trajectories are separate problems.
