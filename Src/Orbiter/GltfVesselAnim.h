// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// not upstream: a vessel's animations on meshes from a drop-in file (claude/design-gltf-roundtrip.md 5.4)

#ifndef __GLTFVESSELANIM_H
#define __GLTFVESSELANIM_H

#include "GltfDropIn.h"
#include "OrbiterAPI.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

class Vessel;

class GltfVesselAnim {
public:
	~GltfVesselAnim ();

	// hooks, called by Vessel; each one is a pointer test for a vessel without such a mesh
	static void InsertedMesh (Vessel *v, UINT idx);
	static void CreatedAnimation (Vessel *v, UINT an);
	static void AddedComponent (Vessel *v, UINT an, ANIMATIONCOMP *ac);
	static void DeletingComponent (Vessel *v, ANIMATIONCOMP *ac);
	static void DeletingMesh (Vessel *v, UINT idx, bool retainAnim);
	static void ClearingAnimations (Vessel *v);
	static void Check (Vessel *v); // after class caps and post creation: warnings, once

private:
	explicit GltfVesselAnim (Vessel *v);

	struct Slot {
		GltfDropSlot src;
		std::vector<int> replaced;
		std::vector<MGROUP_TRANSFORM*> trans;   // per component spec
		std::vector<ANIMATIONCOMP*> made;       // per spec, null until added
		std::map<std::pair<int, int>, int> pivot; // (animation, program index) -> spec
		std::set<int> skipped;
		bool logged = false;
	};
	struct Shadow {
		UINT slot = 0;
		int anim = 0, index = 0;
		MGROUP_TRANSFORM *original = nullptr;
	};

	Vessel *v;
	std::map<UINT, Slot> slots;
	std::map<ANIMATIONCOMP*, Shadow> shadows;
	std::map<ANIMATIONCOMP*, std::pair<UINT, int>> ours; // file component -> slot, spec
	std::map<ANIMATIONCOMP*, ANIMATIONCOMP*> moved;       // program component re-parented onto a file component -> its parent before
	std::map<UINT, int> programCount;                     // program components added per animation
	std::vector<MGROUP_TRANSFORM*> owned;
	std::vector<std::unique_ptr<std::vector<UINT>>> lists;
	std::set<std::string> warned;
	bool adding = false;

	const std::vector<int> *Replaced (UINT mesh) const;
	UINT *List (const std::vector<UINT> &g);
	MGROUP_TRANSFORM *Copy (const MGROUP_TRANSFORM *t, UINT *grp, UINT ngrp);
	void Shade (ANIMATIONCOMP *ac, UINT slot, int anim, int index);
	void DropSlot (UINT idx);
	void AddPending ();
	void Reparent (ANIMATIONCOMP *ac);
	void Warn (const std::string &w);
	void Report (UINT idx, Slot &s);
};

#endif // !__GLTFVESSELANIM_H
