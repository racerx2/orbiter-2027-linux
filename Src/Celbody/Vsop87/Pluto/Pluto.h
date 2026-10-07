// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from Jupiter.h for the Pluto system

#ifndef __VSOP87_PLUTO
#define __VSOP87_PLUTO

#include "../../common/psrmodule.h"

// ======================================================================
// class Pluto: interface
// ======================================================================

class Pluto: public PsrModule {
public:
	Pluto ();
	bool clbkAtmParam (double alt, ATMPARAM *prm) override;
private:
	double pb[5]; // pressure at the base of each atmosphere layer [Pa]
};

#endif // !__VSOP87_PLUTO
