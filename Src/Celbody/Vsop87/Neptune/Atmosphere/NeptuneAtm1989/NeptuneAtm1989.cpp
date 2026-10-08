// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.cpp

// ======================================================================
// class NeptuneAtmosphere_1989
// Neptune atmosphere from Voyager 2 radio occultation and UVS, and the NASA fact sheet 1 bar values
// ======================================================================

#define ORBITER_MODULE
#include "NeptuneAtm1989.h"
#include "../../../../common/layeratm.h"

// geometric altitude above the 1 bar level [m], temperature [K]
static const AtmNode NODE[] = {
	{0.0, 72.0}, // 1 bar
	{40e3, 52.0}, // tropopause, 0.1 bar
	{200e3, 130.0}, // stratosphere
	{500e3, 160.0},
	{2000e3, 750.0}, // thermosphere
};
static const double ATM_P0  = 1e5; // pressure at Size [Pa]
static const double ATM_R   = 3186.0; // H2 80%, He 19%, CH4 1.5%: mean molecular weight 2.61 [J/(kg K)]
static const double ATM_RAD = 2.4624e7; // Size in Neptune.cfg [m]
static const LayerAtm atm (NODE, 5, ATM_P0, ATM_R, ATM_RAD, 1.024569e26*GGRAV/(ATM_RAD*ATM_RAD)); // Mass in Neptune.cfg [kg]

bool NeptuneAtmosphere_1989::clbkConstants (ATMCONST *atmc) const
{
	atmc->p0       = ATM_P0;
	atmc->rho0     = ATM_P0/(ATM_R*NODE[0].T);
	atmc->R        = ATM_R;
	atmc->gamma    = 1.43; // ideal-gas mix; cold H2 is a little stiffer
	atmc->altlimit = 1800e3;
	return true;
}

const char *NeptuneAtmosphere_1989::clbkName () const
{
	return "1989 Voyager 2 model";
}

bool NeptuneAtmosphere_1989::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm)
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
	return new NeptuneAtmosphere_1989 (cbody);
}

DLLCLBK void DeleteAtmosphere (ATMOSPHERE *atm)
{
	delete (NeptuneAtmosphere_1989*)atm;
}

DLLCLBK char *ModelName ()
{
	return (char*)"Neptune 1989 Voyager 2 atmosphere model";
}

DLLCLBK char *ModelDesc ()
{
	return (char*)"A static Neptune atmosphere model in layers, from Voyager 2 radio occultation and UVS, and the NASA fact sheet 1 bar values.";
}
