// Deimos (Mars II) - Mars satellite ephemeris module for 64-bit Orbiter (Win64 + Linux).
//
// Model    : precessing Kepler orbit plus periodic terms; see ../common/ephemeris.h.
// Elements : fitted elements at 2000-01-01.5 TDB (MJD 51544.5), ecliptic J2000, Mars-centred.
// Rates    : mean motion, node and apse precession and d(n)/dt fitted to JPL Horizons 1800-2200 (mar099).
// Axis     : precession axis (equatorial J2000) fitted to the same data.
// Valid    : 1800-2200 (the fit span); the error grows outside it.
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 23457.376888025916, 0.00024858900350291407, 27.578170221194629, 83.680208009298838, 205.70345129515957, 11.352338157548161,
                                  0.0033002788081838807, -2.0916322911034369e-07, 2.0596601447035666e-07, 51544.5, -4.878165656032824e-22 };
static const eph::Planet PRIM = { 0.0019566, 3396.19, 316.64272345825771, 53.53362443741193 };

class Deimos final : public MoonModule { public: Deimos() : MoonModule(ELEM, PRIM) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Deimos(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Deimos*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20251001; }
