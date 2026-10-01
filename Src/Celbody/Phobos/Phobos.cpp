// Phobos (Mars I) - Mars satellite ephemeris module for 64-bit Orbiter (Win64 + Linux).
//
// Model    : precessing Kepler orbit plus periodic terms; see ../common/ephemeris.h.
// Elements : fitted elements at 2000-01-01.5 TDB (MJD 51544.5), ecliptic J2000, Mars-centred.
// Rates    : mean motion, node and apse precession and d(n)/dt fitted to JPL Horizons 1800-2200 (mar099).
// Axis     : precession axis (equatorial J2000) fitted to the same data.
// Valid    : 1800-2200 (the fit span); the error grows outside it.
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 9374.9191019229656, 0.01512417705976658, 26.055006782741263, 84.817717194638661, 343.05858824495351, 189.55304354940699,
                                  0.013060295983456588, -5.0438609250944591e-06, 5.0368427572506758e-06, 51544.5, 2.5300831034040661e-18 };
static const eph::Planet PRIM = { 0.0019566, 3396.19, 317.66550615480395, 52.890264143631711 };

class Phobos final : public MoonModule { public: Phobos() : MoonModule(ELEM, PRIM) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Phobos(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Phobos*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20251001; }
