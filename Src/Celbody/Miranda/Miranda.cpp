// Miranda (Uranus V) - Uranus satellite ephemeris module for 64-bit Orbiter (Win64 + Linux).
//
// Model    : precessing Kepler orbit plus periodic terms; see ../common/ephemeris.h.
// Elements : fitted elements at 2000-01-01.5 TDB (MJD 51544.5), ecliptic J2000, Uranus-centred.
// Rates    : mean motion, node and apse precession fitted to JPL Horizons 1800-2200 (ura184_merged).
// Axis     : precession axis (equatorial J2000) fitted to the same data.
// Valid    : 1800-2200 (the fit span); the error grows outside it.
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 129824.87951559261, 0.0013544078666573066, 97.255417104007762, 172.08709662716691, 251.15710786755798, 72.660274022421163,
                                  0.0029471738946113273, -6.4135635666334844e-07, 6.3468377019065942e-07, 51544.5, 0 };
static const eph::Planet PRIM = { 0.00334343, 25559.0, 77.311808198991059, 15.171635272330313 };

// comp (0 R, 1 T, 2 N), k, nu [rad/s], c [m], s [m]
static const eph::Term TERMS[] = {
  { 1, 0, 1.5875185133152773e-08, -2184128.8564877687, 2399207.5343381991 },
  { 1, 0, 5.0407291093115425e-10, 1007291.6069955772, 28693.976754131931 },
  { 1, 0, 3.1750370266305539e-08, -394157.97147983959, 38347.524006977706 },
};

class Miranda final : public MoonModule { public: Miranda() : MoonModule(ELEM, PRIM, TERMS, sizeof(TERMS)/sizeof(TERMS[0])) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Miranda(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Miranda*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20251001; }
