// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: pure maths of the look passes (design-visual-batch 13)

#include "LookMath.h"
#include <algorithm>

namespace LookMath {

double Filmic (double x)
{
	x *= 0.568;
	return std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

}
