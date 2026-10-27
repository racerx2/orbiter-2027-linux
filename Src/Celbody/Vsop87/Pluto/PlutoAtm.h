// not upstream: Pluto's atmosphere, used by the Pluto module and PlutoAtm2017
#ifndef __PLUTOATM_H
#define __PLUTOATM_H

#include "../../common/layeratm.h"

// the N2 atmosphere from New Horizons (Gladstone et al. 2016, Hinson et al. 2017): geometric altitude [m], temperature [K]
static const AtmNode PLUTO_ATM[] = {
	{  0.0,  38.0}, // boundary layer
	{  4e3,  38.0}, // inversion
	{ 14e3, 100.0},
	{ 30e3, 108.0}, // stratopause
	{200e3,  74.0},
};
static const int PLUTO_ATM_N = 5;
static const double PLUTO_ATM_P0 = 1.15; // surface pressure [Pa]
static const double PLUTO_ATM_R = 296.8; // N2 [J/(kg K)]
static const double PLUTO_ATM_RAD = 1.1883e6; // Size in Pluto.cfg [m]
static const double PLUTO_ATM_G0 = 869.3261e9/(PLUTO_ATM_RAD*PLUTO_ATM_RAD); // surface gravity [m/s^2], plu060 GM

#endif // !__PLUTOATM_H
