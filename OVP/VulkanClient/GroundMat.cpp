// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ground material sets, Config/GroundDetail.cfg (design-ground-detail 1, 4)

#include "GroundMat.h"
#include "VkCore.h"
#include "VkTexFile.h"
#include "Log.h"
#include "OrbiterAPI.h"
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

namespace GroundMat {

static std::vector<std::unique_ptr<Set>> sets;
static std::map<std::string, const Set *> bodies;

static VkTex *LoadSet (VkDev *dev, int quality, const std::string &name, const std::vector<std::string> &stems)
{
	int n = (int)stems.size();
	VkPixels all;
	for (int k = 0; k < 2 * n; k++) {
		std::string f = "Textures/GroundDetail/" + stems[k % n] + (k < n ? "_a.dds" : "_b.dds");
		VkPixels px;
		if (!VkLoadPixels (oapiResolvePath (f.c_str()).c_str(), px) || px.layers != 1 || px.depth != 1) {
			LogErr("GroundMat: %s missing or not a 2D texture: set %s off", f.c_str(), name.c_str());
			return nullptr;
		}
		if (k == 0) {
			UINT skip = (quality == 1 && px.levels > 1) ? 1 : 0;
			all.w = px.w >> skip; all.h = px.h >> skip; all.levels = px.levels - skip; all.layers = 2 * n;
			all.fmt = px.fmt; all.swz = px.swz;
			all.data.assign (all.layers * all.levels, std::vector<BYTE> ());
			VkFormatProperties fp;
			vkGetPhysicalDeviceFormatProperties (dev->phys, all.fmt, &fp);
			VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			if ((fp.optimalTilingFeatures & need) != need) {
				LogErr("GroundMat: the device can't sample format %d with filtering: set %s off", (int)all.fmt, name.c_str());
				return nullptr;
			}
		}
		UINT skip = px.levels >= all.levels ? px.levels - all.levels : 0;
		if (px.levels < all.levels || px.fmt != all.fmt || px.swz != all.swz || (px.w >> skip) != all.w || (px.h >> skip) != all.h) {
			LogErr("GroundMat: %s differs from %s_a.dds in size, levels or format: set %s off", f.c_str(), stems[0].c_str(), name.c_str());
			return nullptr;
		}
		for (UINT l = 0; l < all.levels; l++) all.Level (l, k) = std::move (px.Level (l + skip));
	}
	VkTex *t = new VkTex (dev, all.w, all.h, all.levels, all.fmt, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, all.layers, false, VK_SAMPLE_COUNT_1_BIT, true); // not VkCreateTexture: it takes 6 layers for a cube
	if (t->img && all.swz != SWZ_NONE) t->SetSwizzle (VkSwizzleMap (all.swz));
	if (!t->img || !VkUploadPixels (t, all)) {
		LogErr("GroundMat: the texture array of set %s could not be made: set off", name.c_str());
		delete t;
		return nullptr;
	}
	LogAlw("GroundMat: set %s, %d materials, %ux%u, %u levels", name.c_str(), n, all.w, all.h, all.levels);
	return t;
}

bool Load (VkDev *dev, int quality)
{
	Release ();
	if (quality <= 0) return false;
	std::ifstream in (oapiResolvePath ("Config/GroundDetail.cfg"));
	if (!in) { LogErr("GroundMat: Config/GroundDetail.cfg missing: ground materials off"); return false; }
	std::string line;
	int ln = 0;
	while (std::getline (in, line)) {
		ln++;
		std::string::size_type c = line.find (';');
		if (c != std::string::npos) line.erase (c);
		std::istringstream ss (line);
		std::string key, name;
		if (!(ss >> key)) continue;
		if (key == "SET") {
			std::string mode, s;
			std::vector<std::string> stems;
			ss >> name >> mode;
			while (ss >> s) stems.push_back (s);
			int m = mode == "CLASS" ? CLASS : (mode == "FREE" ? FREE : -1);
			if (name.empty() || m < 0 || (int)stems.size() != (m == CLASS ? 6 : 4)) {
				LogErr("GroundMat: GroundDetail.cfg line %d: SET <name> CLASS <6 materials> or FREE <4 materials>", ln);
				continue;
			}
			auto st = std::make_unique<Set> ();
			st->name = name; st->mode = m; st->n = (int)stems.size();
			st->tex = LoadSet (dev, quality, name, stems);
			if (st->tex) sets.push_back (std::move (st));
		}
		else if (key == "BODY") {
			std::string set;
			ss >> name >> set;
			const Set *found = nullptr;
			for (auto &s : sets) if (s->name == set) found = s.get();
			if (found) bodies[name] = found;
			else LogErr("GroundMat: GroundDetail.cfg line %d: no loaded set %s for %s", ln, set.c_str(), name.c_str());
		}
		else LogErr("GroundMat: GroundDetail.cfg line %d: unknown key %s", ln, key.c_str());
	}
	return !sets.empty();
}

void Release ()
{
	bodies.clear ();
	for (auto &s : sets) delete s->tex;
	sets.clear ();
}

const Set *ForBody (const char *body)
{
	auto it = body ? bodies.find (body) : bodies.end ();
	return it == bodies.end () ? nullptr : it->second;
}

bool Any () { return !sets.empty (); }

} // namespace GroundMat
