// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.cpp

// ======================================================================
// class JupiterAtmosphere_1995
// Jupiter atmosphere from the Galileo probe descent (Seiff et al. 1998) and the NASA fact sheet 1 bar values
// ======================================================================

#define ORBITER_MODULE
#include "JupiterAtm1995.h"
#include "../../../../common/layeratm.h"

// geometric altitude above the 1 bar level [m], temperature [K]
static const AtmNode NODE[] = {
	{0.0, 166.0}, // 1 bar
	{50e3, 110.0}, // tropopause, 0.1 bar
	{90e3, 160.0},
	{320e3, 200.0}, // top of the stratosphere, 1 ubar
	{1000e3, 1000.0}, // thermosphere, 1 nbar
};
static const double ATM_P0  = 1e5; // pressure at Size [Pa]
static const double ATM_R   = 3745.0; // H2 89.8%, He 10.2%: mean molecular weight 2.22 [J/(kg K)]
static const double ATM_RAD = 6.9911e7; // Size in Jupiter.cfg [m]
static const LayerAtm atm (NODE, 5, ATM_P0, ATM_R, ATM_RAD, 1.8986111e27*GGRAV/(ATM_RAD*ATM_RAD)); // Mass in Jupiter.cfg [kg]

bool JupiterAtmosphere_1995::clbkConstants (ATMCONST *atmc) const
{
	atmc->p0       = ATM_P0;
	atmc->rho0     = ATM_P0/(ATM_R*NODE[0].T);
	atmc->R        = ATM_R;
	atmc->gamma    = 1.42; // ideal-gas mix; cold H2 is a little stiffer
	atmc->altlimit = 3200e3;
	return true;
}

const char *JupiterAtmosphere_1995::clbkName () const
{
	return "1995 Galileo probe model";
}

bool JupiterAtmosphere_1995::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm)
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
	return new JupiterAtmosphere_1995 (cbody);
}

DLLCLBK void DeleteAtmosphere (ATMOSPHERE *atm)
{
	delete (JupiterAtmosphere_1995*)atm;
}

DLLCLBK char *ModelName ()
{
	return (char*)"Jupiter 1995 Galileo probe atmosphere model";
}

DLLCLBK char *ModelDesc ()
{
	return (char*)"A static Jupiter atmosphere model in layers, from the Galileo probe descent (Seiff et al. 1998) and the NASA fact sheet 1 bar values.";
}
