// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ground materials' texture array (design-ground-detail 1)

#pragma once

class VkDev;
class VkTex;

namespace GroundMat {
bool Load (VkDev *dev, int quality);	// Textures/GroundDetail/<name>_a.dds, _b.dds; quality 1 drops mip 0, 0 or any failure: off
void Release ();
VkTex *Tex ();							// layers 0-5 albedo, height; 6-11 normal x y, ao; null when off
}
