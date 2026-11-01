// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ground materials' texture array (design-ground-detail 1)

#include "GroundMat.h"
#include "GroundGeom.h"
#include "VkCore.h"
#include "VkTexFile.h"
#include "Log.h"
#include "OrbiterAPI.h"
#include <string>

namespace GroundMat {

static VkTex *tex = nullptr;

bool Load (VkDev *dev, int quality)
{
	Release ();
	if (quality <= 0) return false;
	VkPixels all;
	for (int k = 0; k < 2 * GroundGeom::NMAT; k++) {
		std::string f = std::string ("Textures/GroundDetail/") + GroundGeom::Name (k % GroundGeom::NMAT) + (k < GroundGeom::NMAT ? "_a.dds" : "_b.dds");
		VkPixels px;
		if (!VkLoadPixels (oapiResolvePath (f.c_str()).c_str(), px) || px.layers != 1 || px.depth != 1) {
			LogErr("GroundMat: %s missing or not a 2D texture: ground materials off", f.c_str());
			return false;
		}
		if (k == 0) {
			UINT skip = (quality == 1 && px.levels > 1) ? 1 : 0;
			all.w = px.w >> skip; all.h = px.h >> skip; all.levels = px.levels - skip; all.layers = 2 * GroundGeom::NMAT;
			all.fmt = px.fmt; all.swz = px.swz;
			all.data.assign (all.layers * all.levels, std::vector<BYTE> ());
			VkFormatProperties fp;
			vkGetPhysicalDeviceFormatProperties (dev->phys, all.fmt, &fp);
			VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			if ((fp.optimalTilingFeatures & need) != need) {
				LogErr("GroundMat: the device can't sample format %d with filtering: ground materials off", (int)all.fmt);
				return false;
			}
		}
		UINT skip = px.levels >= all.levels ? px.levels - all.levels : 0;
		if (px.levels < all.levels || px.fmt != all.fmt || px.swz != all.swz || (px.w >> skip) != all.w || (px.h >> skip) != all.h) {
			LogErr("GroundMat: %s differs from %s_a.dds in size, levels or format: ground materials off", f.c_str(), GroundGeom::Name (0));
			return false;
		}
		for (UINT l = 0; l < all.levels; l++) all.Level (l, k) = std::move (px.Level (l + skip));
	}
	tex = new VkTex (dev, all.w, all.h, all.levels, all.fmt, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, all.layers, false, VK_SAMPLE_COUNT_1_BIT, true); // not VkCreateTexture: it takes 6 layers for a cube
	if (tex->img && all.swz != SWZ_NONE) tex->SetSwizzle (VkSwizzleMap (all.swz));
	if (!tex->img || !VkUploadPixels (tex, all)) {
		LogErr("GroundMat: the texture array could not be made: ground materials off");
		Release ();
		return false;
	}
	LogAlw("GroundMat: %d materials, %ux%u, %u levels", GroundGeom::NMAT, all.w, all.h, all.levels);
	return true;
}

void Release ()
{
	delete tex;
	tex = nullptr;
}

VkTex *Tex () { return tex; }

} // namespace GroundMat
