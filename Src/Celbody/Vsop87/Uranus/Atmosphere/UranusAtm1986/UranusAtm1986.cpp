// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.cpp

// ======================================================================
// class UranusAtmosphere_1986
// Uranus atmosphere from Voyager 2 radio occultation and UVS, and the NASA fact sheet 1 bar values
// ======================================================================

#define ORBITER_MODULE
#include "UranusAtm1986.h"
#include "../../../../common/layeratm.h"

// geometric altitude above the 1 bar level [m], temperature [K]
static const AtmNode NODE[] = {
	{0.0, 76.0}, // 1 bar
	{50e3, 53.0}, // tropopause, 0.1 bar
	{160e3, 100.0}, // hydrocarbon layer
	{320e3, 130.0},
	{600e3, 150.0},
	{2500e3, 800.0}, // thermosphere
};
static const double ATM_P0  = 1e5; // pressure at Size [Pa]
static const double ATM_R   = 3149.0; // H2 82.5%, He 15.2%, CH4 2.3%: mean molecular weight 2.64 [J/(kg K)]
static const double ATM_RAD = 2.5362e7; // Size in Uranus.cfg [m]
static const LayerAtm atm (NODE, 6, ATM_P0, ATM_R, ATM_RAD, 8.6832054e25*GGRAV/(ATM_RAD*ATM_RAD)); // Mass in Uranus.cfg [kg]

bool UranusAtmosphere_1986::clbkConstants (ATMCONST *atmc) const
{
	atmc->p0       = ATM_P0;
	atmc->rho0     = ATM_P0/(ATM_R*NODE[0].T);
	atmc->R        = ATM_R;
	atmc->gamma    = 1.42; // ideal-gas mix; cold H2 is a little stiffer
	atmc->altlimit = 2600e3;
	return true;
}

const char *UranusAtmosphere_1986::clbkName () const
{
	return "1986 Voyager 2 model";
}

bool UranusAtmosphere_1986::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm)
{
	double alt = (prm_in->flag & PRM_ALT ? prm_in->alt : 0.0);
	atm.Get (alt, prm->T, prm->p, prm->rho);
	return true;
}


// ======================================================================
// API interface
// ======================================================================

DLLCLBK void InitModule (void *hModule)
{}

DLLCLBK void ExitModule (void *hModule)
{}

DLLCLBK ATMOSPHERE *CreateAtmosphere (CELBODY2 *cbody)
{
	return new UranusAtmosphere_1986 (cbody);
}

DLLCLBK void DeleteAtmosphere (ATMOSPHERE *atm)
{
	delete (UranusAtmosphere_1986*)atm;
}

DLLCLBK char *ModelName ()
{
	return (char*)"Uranus 1986 Voyager 2 atmosphere model";
}

DLLCLBK char *ModelDesc ()
{
	return (char*)"A static Uranus atmosphere model in layers, from Voyager 2 radio occultation and UVS, and the NASA fact sheet 1 bar values.";
}
