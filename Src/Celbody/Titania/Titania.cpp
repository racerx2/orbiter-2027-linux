// Titania (Uranus III) - Uranus satellite ephemeris module for 64-bit Orbiter (Win64 + Linux).
//
// Model    : precessing Kepler orbit plus periodic terms; see ../common/ephemeris.h.
// Elements : fitted elements at 2000-01-01.5 TDB (MJD 51544.5), ecliptic J2000, Uranus-centred.
// Rates    : mean motion, node and apse precession fitted to JPL Horizons 1800-2200 (ura184_merged).
// Axis     : precession axis (equatorial J2000) fitted to the same data.
// Valid    : 1800-2200 (the fit span); the error grows outside it.
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 436281.46930080047, 0.0013198634183311882, 97.803752005331262, 167.62950345538135, 217.08520331603904, 59.416936751912829,
                                  0.0004785922137976306, 4.1383393301383564e-08, 9.4891509034026153e-08, 51544.5, 0 };
static const eph::Planet PRIM = { 0.00334343, 25559.0, 257.34349800395222, -15.083101851652707 };

// comp (0 R, 1 T, 2 N), k, nu [rad/s], c [m], s [m]
static const eph::Term TERMS[] = {
  { 1, 1, -2.6437162516168971e-09, -438158.0194762008, 549451.36137967929 },
  { 1, 1, 5.0042104885750854e-07, 661216.14772497257, 27765.229515963278 },
};

class Titania final : public MoonModule { public: Titania() : MoonModule(ELEM, PRIM, TERMS, sizeof(TERMS)/sizeof(TERMS[0])) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Titania(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Titania*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20251001; }
