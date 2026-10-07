// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.h

#ifndef __NEPTUNEATM1989_H
#define __NEPTUNEATM1989_H

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"

// ======================================================================
// class NeptuneAtmosphere_1989
// Neptune atmosphere: 1989 Voyager 2 model
// ======================================================================

class NeptuneAtmosphere_1989: public ATMOSPHERE {
public:
	NeptuneAtmosphere_1989 (CELBODY2 *body): ATMOSPHERE (body) {}
	const char *clbkName () const;
	bool clbkConstants (ATMCONST *atmc) const;
	bool clbkParams (const PRM_IN *prm_in, PRM_OUT *prm);
};

#endif // !__NEPTUNEATM1989_H
