// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ground material sets, Config/GroundDetail.cfg (design-ground-detail 1, 4)

#pragma once

#include <string>

class VkDev;
class VkTex;

namespace GroundMat {
enum Mode { CLASS = 0, FREE = 1 };		// picked by the land class tiles; by slope and patches
struct Set {
	std::string name;
	int mode = CLASS;
	int n = 0;							// materials: 6 CLASS, 4 FREE; layers 0..n-1 colour, height; n..2n-1 normal x y, ao
	VkTex *tex = nullptr;
};
bool Load (VkDev *dev, int quality);	// quality 1 drops mip 0; 0, a bad file or a bad line: that set (or all) off, logged
void Release ();
const Set *ForBody (const char *body);	// null: no set for it
bool Any ();							// a set loaded: the terrain shaders take _GROUNDMAT
}
