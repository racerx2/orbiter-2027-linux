// Triton (Neptune I) - Neptune satellite ephemeris module for 64-bit Orbiter (Win64 + Linux).
//
// Model    : precessing Kepler orbit plus periodic terms; see ../common/ephemeris.h.
// Elements : fitted elements at 2000-01-01.5 TDB (MJD 51544.5), ecliptic J2000, Neptune-centred.
// Rates    : mean motion, node and apse precession fitted to JPL Horizons 1800-2200 (nep098_merged).
// Axis     : precession axis (equatorial J2000) fitted to the same data.
// Valid    : 1800-2200 (the fit span); the error grows outside it.
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 354759.02801489836, 0.00012653915841163993, 130.25165557753897, 215.85544778817575, 93.765959539396832, 340.86583469885636,
                                  0.00070898327955231971, -1.6673719871839443e-08, 1.2803957417377906e-08, 51544.5, 0 };
static const eph::Planet PRIM = { 0.003411, 24764.0, 119.40444074210525, -43.327874006931751 };

class Triton final : public MoonModule { public: Triton() : MoonModule(ELEM, PRIM) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Triton(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Triton*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20251001; }
