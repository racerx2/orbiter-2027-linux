// Ariel (Uranus I) - Uranus satellite ephemeris module for 64-bit Orbiter (Win64 + Linux).
//
// Model    : precessing Kepler orbit plus periodic terms; see ../common/ephemeris.h.
// Elements : fitted elements at 2000-01-01.5 TDB (MJD 51544.5), ecliptic J2000, Uranus-centred.
// Rates    : mean motion, node and apse precession fitted to JPL Horizons 1800-2200 (ura184_merged).
// Axis     : precession axis fitted to the same data (arbitrary here: the node rate is near 0).
// Valid    : 1800-2200 (the fit span); the error grows outside it.
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 190924.79333332164, 0.0010908568687170615, 97.729134472616536, 167.65167321974567, 74.350454835709073, 123.44169391606461,
                                  0.0016529924061964951, 6.3255354744110757e-11, 1.9814551941846504e-07, 51544.5, 0 };
static const eph::Planet PRIM = { 0.00334343, 25559.0, 256.03660068597918, -15.120404073486931 };

// comp (0 R, 1 T, 2 N), k, nu [rad/s], c [m], s [m]
static const eph::Term TERMS[] = {
  { 1, 0, 5.1029603328832891e-10, 1516507.2215450183, -5190.0286786664083 },
  { 1, 0, 1.0890464125055801e-09, -336041.5002390524, -7056.8266932462375 },
  { 1, 0, 1.5856515766081246e-08, 222154.03723858093, -241499.30120065538 },
  { 1, 1, -1.5795623285403456e-09, 310971.60875985946, 26818.22647067055 },
  { 1, 0, 6.2231223571747384e-12, -337506.27575711918, -2636605.0158064603 },
};

class Ariel final : public MoonModule { public: Ariel() : MoonModule(ELEM, PRIM, TERMS, sizeof(TERMS)/sizeof(TERMS[0])) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Ariel(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Ariel*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20251001; }
