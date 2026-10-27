// not upstream: Triton's atmosphere, used by the Triton module and TritonAtm2022
#ifndef __TRITONATM_H
#define __TRITONATM_H

#include "../common/layeratm.h"

// the N2 atmosphere from Voyager 2 and the 2017-2022 occultations (Oliva et al. 2022): geometric altitude [m], temperature [K]
static const AtmNode TRITON_ATM[] = {
	{  0.0,  38.0}, // troposphere
	{  8e3,  37.2}, // tropopause
	{ 25e3,  50.0},
	{ 50e3,  50.0},
	{150e3,  85.0},
	{400e3, 100.0}, // thermosphere
};
static const int TRITON_ATM_N = 6;
static const double TRITON_ATM_P0 = 1.45; // surface pressure [Pa]
static const double TRITON_ATM_R = 297.0; // N2 [J/(kg K)]
static const double TRITON_ATM_RAD = 1.3525e6; // Size in Triton.cfg [m]
static const double TRITON_ATM_G0 = 2.14e22*GGRAV/(TRITON_ATM_RAD*TRITON_ATM_RAD); // surface gravity [m/s^2], Mass in Triton.cfg [kg]

#endif // !__TRITONATM_H
