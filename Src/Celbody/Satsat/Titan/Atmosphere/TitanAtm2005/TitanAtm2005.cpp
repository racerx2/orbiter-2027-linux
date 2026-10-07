// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.cpp

// ======================================================================
// class TitanAtmosphere_2005
// Titan atmosphere from the Huygens HASI profile (Fulchignoni et al. 2005) and Cassini
// ======================================================================

#define ORBITER_MODULE
#include "TitanAtm2005.h"
#include "../../../../common/layeratm.h"

// geometric altitude [m], temperature [K]
static const AtmNode NODE[] = {
	{  0.0,  93.65}, // surface
	{ 44e3,  70.4},  // tropopause
	{260e3, 187.0},  // stratopause
	{500e3, 150.0},  // mesopause
	{700e3, 170.0},  // thermosphere
};
static const double ATM_P0  = 146.7e3; // surface pressure [Pa]
static const double ATM_R   = 300.0;   // N2 with a few % CH4 [J/(kg K)]
static const double ATM_RAD = 2.575e6; // Size in Titan.cfg [m]
static const LayerAtm atm (NODE, 5, ATM_P0, ATM_R, ATM_RAD, 1.35e23*GGRAV/(ATM_RAD*ATM_RAD)); // Mass in Titan.cfg [kg]

bool TitanAtmosphere_2005::clbkConstants (ATMCONST *atmc) const
{
	atmc->p0       = ATM_P0;
	atmc->rho0     = ATM_P0/(ATM_R*NODE[0].T);
	atmc->R        = ATM_R;
	atmc->gamma    = 1.3941;
	atmc->altlimit = 1200e3;
	return true;
}

const char *TitanAtmosphere_2005::clbkName () const
{
	return "2005 Huygens model";
}

bool TitanAtmosphere_2005::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm)
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
	return new TitanAtmosphere_2005 (cbody);
}

DLLCLBK void DeleteAtmosphere (ATMOSPHERE *atm)
{
	delete (TitanAtmosphere_2005*)atm;
}

DLLCLBK char *ModelName ()
{
	return (char*)"Titan 2005 Huygens atmosphere model";
}

DLLCLBK char *ModelDesc ()
{
	return (char*)"A static Titan atmosphere model in layers, from the Huygens descent profile and Cassini's upper atmosphere.";
}
