// not upstream: Proteus (Neptune VIII) ephemeris module, fitted to JPL Horizons nep098_merged 1800-2200
// Model: precessing Kepler orbit plus periodic terms (../common/ephemeris.h); mean elements at MJD 51544.5, ecliptic J2000, Neptune-centred
#include "../common/ephemeris.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 117646.99129552505, 2.3776057900615059e-07, 28.978595434802585, 48.344943271131221, 16.733447894805522, 233.63646399714685,
                                  0.003712390163436506, 1.6672673734809822e-08, 1.7505217328465161e-07, 51544.5, 0 };
static const eph::Planet PRIM = { 0.003411, 24764.0, 299.41278714550634, 43.358843943402164 };

// comp (0 R, 1 T, 2 N), k, nu [rad/s], c [m], s [m]
static const eph::Term TERMS[] = {
  { 1, 1, -1.5562918265107382e-08, 45569.104828510601, 105956.15816907531 },
  { 2, 1, 1.5565139765470387e-08, 70111.838795653181, 7614.9377944653797 },
  { 0, 1, -1.5562918265107382e-08, -53014.163567883013, 22758.001236360422 },
  { 1, 0, 5.5385788978855216e-10, 6575.7044484823273, -4637.2210396176979 },
};

class Proteus final : public MoonModule { public: Proteus() : MoonModule(ELEM, PRIM, TERMS, sizeof(TERMS)/sizeof(TERMS[0])) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Proteus(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Proteus*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20261001; }
