// not upstream: Nereid (Neptune II) ephemeris module: a Chebyshev table of JPL Horizons nep098_merged, 1800-01-02 to 2199-12-30
// Model: Config/Nereid/Data/nereid.cheb (../common/chebyshev.h); outside it, the fallback orbit below (../common/ephemeris.h), fitted to Horizons 1800-2200, Neptune-centred
#include "../common/chebyshev.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 5513744.7946839239, 0.75065190800515247, 5.0365858781249617, 320.00810857379514, 296.09872481010007, 216.76696160239874,
                                  1.1569582755211465e-05, 5.3825591214969263e-10, 1.2669378001094601e-09, 51544.5, 0 };
static const eph::Planet PRIM = { 0.003411, 24764.0, 105.75298668082569, -76.812777337234536 };

// comp (0 R, 1 T, 2 N), k, nu [rad/s], c [m], s [m]
static const eph::Term TERMS[] = {
  { 1, 0, 1.9951952589337941e-07, 56552858.339831769, -32681650.520197887 },
  { 0, 3, -6.0585096633236525e-07, 380838781.48176229, 141290529.02412808 },
  { 0, 2, -2.0437245058172663e-07, -24477423.2574948, 25776122.279173288 },
  { 1, 4, -4.0635010980612322e-07, -5274647.3079327391, -31052146.019997336 },
};

class Nereid final : public ChebModule { public: Nereid() : ChebModule("Config/Nereid/Data/nereid.cheb", ELEM, PRIM, TERMS, sizeof(TERMS)/sizeof(TERMS[0])) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Nereid(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Nereid*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20261001; }
