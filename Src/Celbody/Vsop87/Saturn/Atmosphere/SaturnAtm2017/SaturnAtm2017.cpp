// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.cpp

// ======================================================================
// class SaturnAtmosphere_2017
// Saturn atmosphere from Cassini CIRS and radio occultations, the UVIS thermosphere (Koskinen et al. 2013) and the NASA fact sheet 1 bar values
// ======================================================================

#define ORBITER_MODULE
#include "SaturnAtm2017.h"
#include "../../../../common/layeratm.h"

// geometric altitude above the 1 bar level [m], temperature [K]
static const AtmNode NODE[] = {
	{0.0, 134.0}, // 1 bar
	{100e3, 82.0}, // tropopause, 0.08 bar
	{300e3, 140.0}, // stratosphere
	{800e3, 150.0},
	{2000e3, 420.0}, // thermosphere
};
static const double ATM_P0  = 1e5; // pressure at Size [Pa]
static const double ATM_R   = 4017.0; // H2 96.3%, He 3.25%: mean molecular weight 2.07 [J/(kg K)]
static const double ATM_RAD = 5.8232e7; // Size in Saturn.cfg [m]
static const LayerAtm atm (NODE, 5, ATM_P0, ATM_R, ATM_RAD, 5.6846272e26*GGRAV/(ATM_RAD*ATM_RAD)); // Mass in Saturn.cfg [kg]

bool SaturnAtmosphere_2017::clbkConstants (ATMCONST *atmc) const
{
	atmc->p0       = ATM_P0;
	atmc->rho0     = ATM_P0/(ATM_R*NODE[0].T);
	atmc->R        = ATM_R;
	atmc->gamma    = 1.41; // ideal-gas mix; cold H2 is a little stiffer
	atmc->altlimit = 2900e3;
	return true;
}

const char *SaturnAtmosphere_2017::clbkName () const
{
	return "2017 Cassini model";
}

bool SaturnAtmosphere_2017::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm)
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
	return new SaturnAtmosphere_2017 (cbody);
}

DLLCLBK void DeleteAtmosphere (ATMOSPHERE *atm)
{
	delete (SaturnAtmosphere_2017*)atm;
}

DLLCLBK char *ModelName ()
{
	return (char*)"Saturn 2017 Cassini atmosphere model";
}

DLLCLBK char *ModelDesc ()
{
	return (char*)"A static Saturn atmosphere model in layers, from Cassini CIRS and radio occultations, the UVIS thermosphere (Koskinen et al. 2013) and the NASA fact sheet 1 bar values.";
}
