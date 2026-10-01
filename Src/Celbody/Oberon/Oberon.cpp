// Oberon (Uranus IV) - Uranus satellite ephemeris module for 64-bit Orbiter (Win64 + Linux).
//
// Model    : precessing Kepler orbit plus periodic terms; see ../common/ephemeris.h.
// Elements : fitted elements at 2000-01-01.5 TDB (MJD 51544.5), ecliptic J2000, Uranus-centred.
// Rates    : mean motion, node and apse precession fitted to JPL Horizons 1800-2200 (ura184_merged).
// Axis     : precession axis (equatorial J2000) fitted to the same data.
// Valid    : 1800-2200 (the fit span); the error grows outside it.
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 583450.00562916021, 0.0012548222432500777, 97.861541143066916, 167.74343451975301, 196.69551846020769, 150.82186822938081,
                                  0.00030947188347426154, -6.1017684229327198e-08, 1.2863992992978163e-08, 51544.5, 0 };
static const eph::Planet PRIM = { 0.00334343, 25559.0, 77.367900310826826, 15.052959188063371 };

// comp (0 R, 1 T, 2 N), k, nu [rad/s], c [m], s [m]
static const eph::Term TERMS[] = {
  { 1, 0, 5.9033907767078202e-06, 582147.64224187308, 752023.88237135962 },
  { 1, 0, 5.4003322347208848e-06, -758493.75846334291, -66438.639263336343 },
};

class Oberon final : public MoonModule { public: Oberon() : MoonModule(ELEM, PRIM, TERMS, sizeof(TERMS)/sizeof(TERMS[0])) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Oberon(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Oberon*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20251001; }
