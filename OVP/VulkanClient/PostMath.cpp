// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: pure maths of the TAA pass (design-visual-batch 13)

#include "PostMath.h"

namespace PostMath {

double Halton (int i, int b)
{
	double f = 1.0, r = 0.0;
	for (; i > 0; i /= b) { f /= b; r += f * (i % b); }
	return r;
}

}
