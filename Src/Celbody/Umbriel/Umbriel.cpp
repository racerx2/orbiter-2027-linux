// Umbriel (Uranus II) - Uranus satellite ephemeris module for 64-bit Orbiter (Win64 + Linux).
//
// Model    : precessing Kepler orbit plus periodic terms; see ../common/ephemeris.h.
// Elements : fitted elements at 2000-01-01.5 TDB (MJD 51544.5), ecliptic J2000, Uranus-centred.
// Rates    : mean motion, node and apse precession fitted to JPL Horizons 1800-2200 (ura184_merged).
// Axis     : precession axis (equatorial J2000) fitted to the same data.
// Valid    : 1800-2200 (the fit span); the error grows outside it.
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 265979.93445181393, 0.0038325269121545569, 97.725097633915652, 167.67275125868116, 348.50334341020994, 257.67162292451553,
                                  0.0010053368335373211, 2.0475938897908821e-08, 1.3090111956094717e-07, 51544.5, 0 };
static const eph::Planet PRIM = { 0.00334343, 25559.0, 257.32112667441419, -15.156182656213106 };

class Umbriel final : public MoonModule { public: Umbriel() : MoonModule(ELEM, PRIM) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Umbriel(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Umbriel*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20251001; }
