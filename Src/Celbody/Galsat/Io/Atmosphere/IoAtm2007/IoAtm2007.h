// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.h

#ifndef __IOATM2007_H
#define __IOATM2007_H

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"

// ======================================================================
// class IoAtmosphere_2007
// Io atmosphere: 2007 SO2 model
// ======================================================================

class IoAtmosphere_2007: public ATMOSPHERE {
public:
	IoAtmosphere_2007 (CELBODY2 *body): ATMOSPHERE (body) {}
	const char *clbkName () const;
	bool clbkConstants (ATMCONST *atmc) const;
	bool clbkParams (const PRM_IN *prm_in, PRM_OUT *prm);
};

#endif // !__IOATM2007_H
