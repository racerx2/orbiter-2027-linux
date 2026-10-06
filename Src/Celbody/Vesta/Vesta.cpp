// not upstream: Vesta (asteroid 4) ephemeris module: a Chebyshev table of JPL Horizons JPL#36, 1600-2500
// Model: Config/Vesta/Data/vesta.cheb (../common/chebyshev.h); outside it, the fallback orbit below (../common/ephemeris.h), fitted to Horizons 1800-2200, Sun-centred
#include "../common/chebyshev.h"

//                               a[km]            e               inc[deg]        node[deg]       peri[deg]       M0[deg]
//                               n[deg/s]         nodeRate[deg/s]  periRate[deg/s]  epoch[MJD]  ndot[deg/s^2]
static const eph::Elem   ELEM = { 353280597.81854677, 0.089375857488650109, 7.137047192639991, 103.96022932412832, 150.32529925452911, 340.13921054210391,
                                  3.1427229071669631e-06, -3.4694697994664035e-10, 3.8119841438485769e-10, 51544.5, 0 };
static const eph::Planet PRIM = { 0.0, 695700.0, 273.36804431774112, 66.554929894869829 };

// comp (0 R, 1 T, 2 N), k, nu [rad/s], c [m], s [m]
static const eph::Term TERMS[] = {
  { 1, 1, -5.9367476537245898e-08, 495976324.29448366, -618209476.63507831 },
  { 1, 1, 4.5066513367745351e-09, 484103851.94619048, 198301644.56836313 },
  { 0, 0, 5.9368587287447047e-08, -196312717.07475379, 213777780.58630595 },
  { 1, 1, -5.4855712828295698e-08, -1352393577.0636878, -3622032208.3582687 },
  { 1, 1, -7.6151237534540607e-08, 139667218.71371907, -219956839.2034159 },
  { 1, 1, 2.1284189211712081e-08, 205655462.22988325, 122761173.66408774 },
  { 1, 0, 3.8079285703552257e-08, 60196690.352406457, 185967446.17207152 },
  { 0, 1, -5.4855712828295698e-08, 612219511.4397136, 1656478119.0760968 },
  { 0, 1, 2.1284189211712081e-08, -77748479.841965079, 126387793.02079055 },
  { 1, 2, -1.6488673922546592e-07, -68278381.641916022, 150005490.36509895 },
  { 1, 1, -1.093951571665571e-07, 113590562.22963847, -97734831.181740776 },
  { 1, 2, -1.1101939210177916e-07, 66518128.686349623, -59153480.911849067 },
};

class Vesta final : public ChebModule { public: Vesta() : ChebModule("Config/Vesta/Data/vesta.cheb", ELEM, PRIM, TERMS, sizeof(TERMS)/sizeof(TERMS[0])) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Vesta(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Vesta*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20261001; }
