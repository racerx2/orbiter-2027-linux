// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.h

#ifndef __JUPITERATM1995_H
#define __JUPITERATM1995_H

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"

// ======================================================================
// class JupiterAtmosphere_1995
// Jupiter atmosphere: 1995 Galileo probe model
// ======================================================================

class JupiterAtmosphere_1995: public ATMOSPHERE {
public:
	JupiterAtmosphere_1995 (CELBODY2 *body): ATMOSPHERE (body) {}
	const char *clbkName () const;
	bool clbkConstants (ATMCONST *atmc) const;
	bool clbkParams (const PRM_IN *prm_in, PRM_OUT *prm);
};

#endif // !__JUPITERATM1995_H
