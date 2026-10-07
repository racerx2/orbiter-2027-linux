// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.h

#ifndef __TITANATM2005_H
#define __TITANATM2005_H

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"

// ======================================================================
// class TitanAtmosphere_2005
// Titan atmosphere model from the Huygens descent (2005) and Cassini
// ======================================================================

class TitanAtmosphere_2005: public ATMOSPHERE {
public:
	TitanAtmosphere_2005 (CELBODY2 *body): ATMOSPHERE (body) {}
	const char *clbkName () const;
	bool clbkConstants (ATMCONST *atmc) const;
	bool clbkParams (const PRM_IN *prm_in, PRM_OUT *prm);
};

#endif // !__TITANATM2005_H
